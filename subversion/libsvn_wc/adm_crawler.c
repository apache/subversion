/*
 * adm_crawler.c:  report local WC mods to an Editor.
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* ==================================================================== */


#include <string.h>

#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_hash.h>
#include <apr_md5.h>

#include <assert.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_io.h"
#include "svn_md5.h"
#include "svn_base64.h"
#include "svn_delta.h"
#include "svn_path.h"

#include "private/svn_wc_private.h"

#include "wc.h"
#include "adm_files.h"
#include "props.h"
#include "translate.h"
#include "entries.h"
#include "lock.h"

#include "svn_private_config.h"


/* Helper for report_revisions_and_depths().

   Perform an atomic restoration of the file FILE_PATH; that is, copy
   the file's text-base to the administrative tmp area, and then move
   that file to FILE_PATH with possible translations/expansions.  If
   USE_COMMIT_TIMES is set, then set working file's timestamp to
   last-commit-time.  Either way, set entry-timestamp to match that of
   the working file when all is finished. */
static svn_error_t *
restore_file(const char *file_path,
             svn_wc_adm_access_t *adm_access,
             svn_boolean_t use_commit_times,
             apr_pool_t *pool)
{
  const char *tmp_file, *text_base_path;
  svn_wc_entry_t newentry;
  const char *bname;
  svn_boolean_t special;

  text_base_path = svn_wc__text_base_path(file_path, FALSE, pool);
  bname = svn_path_basename(file_path, pool);

  /* Copy / translate into a temporary file, which afterwards can
     be atomically moved over the original working copy file. */

  SVN_ERR(svn_wc_translated_file2(&tmp_file,
                                  text_base_path, file_path, adm_access,
                                  SVN_WC_TRANSLATE_FROM_NF
                                  | SVN_WC_TRANSLATE_FORCE_COPY, pool));

  SVN_ERR(svn_io_file_rename(tmp_file, file_path, pool));

  SVN_ERR(svn_wc__maybe_set_read_only(NULL, file_path, adm_access, pool));

  /* If necessary, tweak the new working file's executable bit. */
  SVN_ERR(svn_wc__maybe_set_executable(NULL, file_path, adm_access, pool));

  /* Remove any text conflict */
  SVN_ERR(svn_wc_resolved_conflict3(file_path, adm_access, TRUE, FALSE,
                                    svn_depth_empty,
                                    svn_wc_conflict_choose_merged,
                                    NULL, NULL, NULL, NULL, pool));

  if (use_commit_times)
    {
      SVN_ERR(svn_wc__get_special(&special, file_path, adm_access, pool));
    }

  /* Possibly set timestamp to last-commit-time. */
  if (use_commit_times && (! special))
    {
      const svn_wc_entry_t *entry;

      SVN_ERR(svn_wc_entry(&entry, file_path, adm_access, FALSE, pool));
      SVN_ERR_ASSERT(entry != NULL);

      SVN_ERR(svn_io_set_file_affected_time(entry->cmt_date,
                                            file_path, pool));

      newentry.text_time = entry->cmt_date;
    }
  else
    {
      SVN_ERR(svn_io_file_affected_time(&newentry.text_time,
                                        file_path, pool));
    }

  /* Modify our entry's text-timestamp to match the working file. */
  SVN_ERR(svn_wc__entry_modify(adm_access, bname,
                               &newentry, SVN_WC__ENTRY_MODIFY_TEXT_TIME,
                               TRUE /* do_sync now */, pool));

  return SVN_NO_ERROR;
}


/* The recursive crawler that describes a mixed-revision working
   copy to an RA layer.  Used to initiate updates.

   This is a depth-first recursive walk of DIR_PATH under ADM_ACCESS.
   Look at each entry and check if its revision is different than
   DIR_REV.  If so, report this fact to REPORTER.  If an entry is
   missing from disk, report its absence to REPORTER.  If an entry has
   a different URL than expected, report that to REPORTER.  If an
   entry has a different depth than its parent, report that to
   REPORTER.

   Alternatively, if REPORT_EVERYTHING is set, then report all
   children unconditionally.

   DEPTH is actually the *requested* depth for the update-like
   operation for which we are reporting working copy state.  However,
   certain requested depths affect the depth of the report crawl.  For
   example, if the requested depth is svn_depth_empty, there's no
   point descending into subdirs, no matter what their depths.  So:

   If DEPTH is svn_depth_empty, don't report any files and don't
   descend into any subdirs.  If svn_depth_files, report files but
   still don't descend into subdirs.  If svn_depth_immediates, report
   files, and report subdirs themselves but not their entries.  If
   svn_depth_infinity or svn_depth_unknown, report everything all the
   way down.  (That last sentence might sound counterintuitive, but
   since you can't go deeper than the local ambient depth anyway,
   requesting svn_depth_infinity really means "as deep as the various
   parts of this working copy go".  Of course, the information that
   comes back from the server will be different for svn_depth_unknown
   than for svn_depth_infinity.)

   DEPTH_COMPATIBILITY_TRICK means the same thing here as it does
   in svn_wc_crawl_revisions3().

   If TRAVERSAL_INFO is non-null, record this directory's
   value of svn:externals in both TRAVERSAL_INFO->externals_old and
   TRAVERSAL_INFO->externals_new, using wc_path + dir_path as the key,
   and the raw (unparsed) value of the property as the value; store
   this directory's depth in TRAVERSAL_INFO->depths, using the same
   key and svn_depth_to_word(depth) as the value.  (Note: We set the
   property value in both places, because its absence in just one or
   the other place signals that the property was added or deleted;
   thus, storing it in both places signals that it is present and, by
   default, unchanged.)

   If RESTORE_FILES is set, then unexpectedly missing working files
   will be restored from text-base and NOTIFY_FUNC/NOTIFY_BATON
   will be called to report the restoration.  USE_COMMIT_TIMES is
   passed to restore_file() helper. */
static svn_error_t *
report_revisions_and_depths(svn_wc_adm_access_t *adm_access,
                            const char *dir_path,
                            svn_revnum_t dir_rev,
                            const svn_ra_reporter3_t *reporter,
                            void *report_baton,
                            svn_wc_notify_func2_t notify_func,
                            void *notify_baton,
                            svn_boolean_t restore_files,
                            svn_depth_t depth,
                            svn_boolean_t depth_compatibility_trick,
                            svn_boolean_t report_everything,
                            svn_boolean_t use_commit_times,
                            svn_wc_traversal_info_t *traversal_info,
                            apr_pool_t *pool)
{
  apr_hash_t *entries, *dirents;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create(pool), *iterpool;
  const svn_wc_entry_t *dot_entry;
  const char *this_url, *this_path, *full_path, *this_full_path;
  svn_wc_adm_access_t *dir_access;
  svn_wc_notify_t *notify;

  /* Get both the SVN Entries and the actual on-disk entries.   Also
     notice that we're picking up hidden entries too. */
  full_path = svn_path_join(svn_wc_adm_access_path(adm_access),
                            dir_path, subpool);
  SVN_ERR(svn_wc_adm_retrieve(&dir_access, adm_access, full_path, subpool));
  SVN_ERR(svn_wc_entries_read(&entries, dir_access, TRUE, subpool));
  SVN_ERR(svn_io_get_dir_filenames(&dirents, full_path, subpool));

  /*** Do the real reporting and recursing. ***/

  /* First, look at "this dir" to see what its URL is. */
  dot_entry = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR,
                           APR_HASH_KEY_STRING);

  /* If "this dir" has "svn:externals" property set on it, store its name
     and depth in traversal_info. */
  if (traversal_info)
    {
      const svn_string_t *val;
      SVN_ERR(svn_wc_prop_get(&val, SVN_PROP_EXTERNALS, full_path, adm_access,
                              subpool));
      if (val)
        {
          apr_pool_t *dup_pool = traversal_info->pool;
          const char *dup_path = apr_pstrdup(dup_pool, full_path);
          const char *dup_val = apr_pstrmemdup(dup_pool, val->data, val->len);
          apr_hash_set(traversal_info->externals_old,
                       dup_path, APR_HASH_KEY_STRING, dup_val);
          apr_hash_set(traversal_info->externals_new,
                       dup_path, APR_HASH_KEY_STRING, dup_val);
          apr_hash_set(traversal_info->depths,
                       dup_path, APR_HASH_KEY_STRING,
                       svn_depth_to_word(dot_entry->depth));
        }
    }

  /* Looping over current directory's SVN entries: */
  iterpool = svn_pool_create(subpool);

  for (hi = apr_hash_first(subpool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      const svn_wc_entry_t *current_entry;
      svn_io_dirent_t *dirent;
      svn_node_kind_t dirent_kind;
      svn_boolean_t missing = FALSE;

      /* Clear the iteration subpool here because the loop has a bunch
         of 'continue' jump statements. */
      svn_pool_clear(iterpool);

      /* Get the next entry */
      apr_hash_this(hi, &key, &klen, &val);
      current_entry = val;

      /* Compute the name of the entry.  Skip THIS_DIR altogether. */
      if (! strcmp(key, SVN_WC_ENTRY_THIS_DIR))
        continue;

      /* Compute the paths and URLs we need. */
      this_url = svn_path_url_add_component(dot_entry->url, key, iterpool);
      this_path = svn_path_join(dir_path, key, iterpool);
      this_full_path = svn_path_join(full_path, key, iterpool);

      /*** The Big Tests: ***/

      /* If the entry is 'deleted' or 'absent', make sure the server
         knows it's gone... */
      if (current_entry->deleted || current_entry->absent)
        {
          /* ...unless we're reporting everything, in which case we're
             going to report it missing later anyway. */
          if (! report_everything)
            SVN_ERR(reporter->delete_path(report_baton, this_path, iterpool));
          continue;
        }

      /* Is the entry on disk?  Set a flag if not. */
      dirent = apr_hash_get(dirents, key, klen);
      if (! dirent)
        {
          /* It is possible on a case insensitive system that the
             entry is not really missing, so we call our trusty but
             expensive friend svn_io_check_path to be sure. */
          SVN_ERR(svn_io_check_path(this_full_path, &dirent_kind,
                                    iterpool));
          if (dirent_kind == svn_node_none)
            missing = TRUE;
        }

      /* From here on out, ignore any entry scheduled for addition */
      if (current_entry->schedule == svn_wc_schedule_add)
        continue;

      /*** Files ***/
      if (current_entry->kind == svn_node_file)
        {
          /* If the item is missing from disk, and we're supposed to
             restore missing things, and it isn't missing as a result
             of a scheduling operation, then ... */
          if (missing
              && restore_files
              && (current_entry->schedule != svn_wc_schedule_delete)
              && (current_entry->schedule != svn_wc_schedule_replace))
            {
              /* ... recreate file from text-base, and ... */
              SVN_ERR(restore_file(this_full_path, dir_access,
                                   use_commit_times, iterpool));

              /* ... report the restoration to the caller.  */
              if (notify_func != NULL)
                {
                  notify = svn_wc_create_notify(this_full_path,
                                                svn_wc_notify_restore,
                                                iterpool);
                  notify->kind = svn_node_file;
                  (*notify_func)(notify_baton, notify, iterpool);
                }
            }

          if (report_everything)
            {
              /* Report the file unconditionally, one way or another. */
              if (strcmp(current_entry->url, this_url) != 0)
                SVN_ERR(reporter->link_path(report_baton, this_path,
                                            current_entry->url,
                                            current_entry->revision,
                                            current_entry->depth,
                                            FALSE, current_entry->lock_token,
                                            iterpool));
              else
                SVN_ERR(reporter->set_path(report_baton, this_path,
                                           current_entry->revision,
                                           current_entry->depth,
                                           FALSE, current_entry->lock_token,
                                           iterpool));
            }

          /* Possibly report a disjoint URL ... */
          else if ((current_entry->schedule != svn_wc_schedule_add)
                   && (current_entry->schedule != svn_wc_schedule_replace)
                   && (strcmp(current_entry->url, this_url) != 0))
            SVN_ERR(reporter->link_path(report_baton,
                                        this_path,
                                        current_entry->url,
                                        current_entry->revision,
                                        current_entry->depth,
                                        FALSE,
                                        current_entry->lock_token,
                                        iterpool));
          /* ... or perhaps just a differing revision or lock token,
             or the mere presence of the file in a depth-empty dir. */
          else if (current_entry->revision != dir_rev
                   || current_entry->lock_token
                   || dot_entry->depth == svn_depth_empty)
            SVN_ERR(reporter->set_path(report_baton,
                                       this_path,
                                       current_entry->revision,
                                       current_entry->depth,
                                       FALSE,
                                       current_entry->lock_token,
                                       iterpool));
        } /* end file case */

      /*** Directories (in recursive mode) ***/
      else if (current_entry->kind == svn_node_dir
               && (depth > svn_depth_files
                   || depth == svn_depth_unknown))
        {
          svn_wc_adm_access_t *subdir_access;
          const svn_wc_entry_t *subdir_entry;
          svn_boolean_t start_empty;

          /* If a directory is missing from disk, we have no way to
             recreate it locally, so report as missing and move
             along.  Again, don't bother if we're reporting
             everything, because the dir is already missing on the server. */
          if (missing)
            {
              if (! report_everything)
                SVN_ERR(reporter->delete_path(report_baton, this_path,
                                              iterpool));
              continue;
            }

          /* We need to read the full entry of the directory from its
             own "this dir", if available. */
          if (svn_wc__adm_missing(adm_access, this_full_path))
            continue;
          SVN_ERR(svn_wc_adm_retrieve(&subdir_access, adm_access,
                                      this_full_path, iterpool));
          SVN_ERR(svn_wc_entry(&subdir_entry, this_full_path, subdir_access,
                               TRUE, iterpool));

          start_empty = subdir_entry->incomplete;
          if (depth_compatibility_trick
              && subdir_entry->depth <= svn_depth_files
              && depth > subdir_entry->depth)
            {
              start_empty = TRUE;
            }

          if (report_everything)
            {
              /* Report the dir unconditionally, one way or another. */
              if (strcmp(subdir_entry->url, this_url) != 0)
                SVN_ERR(reporter->link_path(report_baton, this_path,
                                            subdir_entry->url,
                                            subdir_entry->revision,
                                            subdir_entry->depth,
                                            start_empty,
                                            subdir_entry->lock_token,
                                            iterpool));
              else
                SVN_ERR(reporter->set_path(report_baton, this_path,
                                           subdir_entry->revision,
                                           subdir_entry->depth,
                                           start_empty,
                                           subdir_entry->lock_token,
                                           iterpool));
            }

          /* Possibly report a disjoint URL ... */
          else if (strcmp(subdir_entry->url, this_url) != 0)
            SVN_ERR(reporter->link_path(report_baton,
                                        this_path,
                                        subdir_entry->url,
                                        subdir_entry->revision,
                                        subdir_entry->depth,
                                        start_empty,
                                        subdir_entry->lock_token,
                                        iterpool));
          /* ... or perhaps just a differing revision, lock token, incomplete
             subdir, the mere presence of the directory in a depth-empty or
             depth-files dir, or if the parent dir is at depth-immediates but
             the child is not at depth-empty. */
          else if (subdir_entry->revision != dir_rev
                   || subdir_entry->lock_token
                   || subdir_entry->incomplete
                   || dot_entry->depth == svn_depth_empty
                   || dot_entry->depth == svn_depth_files
                   || (dot_entry->depth == svn_depth_immediates
                       && subdir_entry->depth != svn_depth_empty))
            SVN_ERR(reporter->set_path(report_baton,
                                       this_path,
                                       subdir_entry->revision,
                                       subdir_entry->depth,
                                       start_empty,
                                       subdir_entry->lock_token,
                                       iterpool));

          if (SVN_DEPTH_IS_RECURSIVE(depth))
            SVN_ERR(report_revisions_and_depths(adm_access, this_path,
                                                subdir_entry->revision,
                                                reporter, report_baton,
                                                notify_func, notify_baton,
                                                restore_files, depth,
                                                depth_compatibility_trick,
                                                start_empty,
                                                use_commit_times,
                                                traversal_info,
                                                iterpool));
        } /* end directory case */
    } /* end main entries loop */

  /* We're done examining this dir's entries, so free everything. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/*------------------------------------------------------------------*/
/*** Public Interfaces ***/


svn_error_t *
svn_wc_crawl_revisions3(const char *path,
                        svn_wc_adm_access_t *adm_access,
                        const svn_ra_reporter3_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_depth_t depth,
                        svn_boolean_t depth_compatibility_trick,
                        svn_boolean_t use_commit_times,
                        svn_wc_notify_func2_t notify_func,
                        void *notify_baton,
                        svn_wc_traversal_info_t *traversal_info,
                        apr_pool_t *pool)
{
  svn_error_t *fserr, *err = SVN_NO_ERROR;
  const svn_wc_entry_t *entry;
  svn_revnum_t base_rev = SVN_INVALID_REVNUM;
  svn_boolean_t missing = FALSE;
  const svn_wc_entry_t *parent_entry = NULL;
  svn_wc_notify_t *notify;
  svn_boolean_t start_empty;

  /* The first thing we do is get the base_rev from the working copy's
     ROOT_DIRECTORY.  This is the first revnum that entries will be
     compared to. */
  SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));

  if ((! entry) || ((entry->schedule == svn_wc_schedule_add)
                    && (entry->kind == svn_node_dir)))
    {
      /* There aren't any versioned paths to crawl which are known to
         the repository. */
      SVN_ERR(svn_wc__entry_versioned(&parent_entry,
                                      svn_path_dirname(path, pool),
                                      adm_access, FALSE, pool));

      base_rev = parent_entry->revision;

      /* If no versioned path exists, we use the requested depth, which
         is the depth at which the new path should be brought in.  Default
         to infinity if no explicit depth was given. */
      if (depth == svn_depth_unknown)
        depth = svn_depth_infinity;

      SVN_ERR(reporter->set_path(report_baton, "", base_rev, depth,
                                 entry ? entry->incomplete : TRUE,
                                 NULL, pool));
      SVN_ERR(reporter->delete_path(report_baton, "", pool));

      /* Finish the report, which causes the update editor to be
         driven. */
      SVN_ERR(reporter->finish_report(report_baton, pool));

      return SVN_NO_ERROR;
    }

  base_rev = entry->revision;

  start_empty = entry->incomplete;
  if (depth_compatibility_trick
      && entry->depth <= svn_depth_immediates
      && depth > entry->depth)
    {
      start_empty = TRUE;
    }

  if (base_rev == SVN_INVALID_REVNUM)
    {
      const char *dirname = svn_path_dirname(path, pool);
      SVN_ERR(svn_wc__entry_versioned(&parent_entry, dirname, adm_access,
                                      FALSE, pool));
      base_rev = parent_entry->revision;
    }

  /* The first call to the reporter merely informs it that the
     top-level directory being updated is at BASE_REV.  Its PATH
     argument is ignored. */
  SVN_ERR(reporter->set_path(report_baton, "", base_rev, entry->depth,
                             start_empty, NULL, pool));

  if (entry->schedule != svn_wc_schedule_delete)
    {
      apr_finfo_t info;
      err = svn_io_stat(&info, path, APR_FINFO_MIN, pool);
      if (err)
        {
          if (APR_STATUS_IS_ENOENT(err->apr_err))
            missing = TRUE;
          svn_error_clear(err);
          err = NULL;
        }
    }

  if (entry->kind == svn_node_dir)
    {
      if (missing)
        {
          /* Always report directories as missing;  we can't recreate
             them locally. */
          err = reporter->delete_path(report_baton, "", pool);
          if (err)
            goto abort_report;
        }
      else if (depth != svn_depth_empty)
        {
          /* Recursively crawl ROOT_DIRECTORY and report differing
             revisions. */
          err = report_revisions_and_depths(adm_access,
                                            "",
                                            base_rev,
                                            reporter, report_baton,
                                            notify_func, notify_baton,
                                            restore_files, depth,
                                            depth_compatibility_trick,
                                            start_empty,
                                            use_commit_times,
                                            traversal_info,
                                            pool);
          if (err)
            goto abort_report;
        }
    }

  else if (entry->kind == svn_node_file)
    {
      const char *pdir, *bname;

      if (missing && restore_files)
        {
          /* Recreate file from text-base. */
          err = restore_file(path, adm_access, use_commit_times, pool);
          if (err)
            goto abort_report;

          /* Report the restoration to the caller. */
          if (notify_func != NULL)
            {
              notify = svn_wc_create_notify(path, svn_wc_notify_restore,
                                            pool);
              notify->kind = svn_node_file;
              (*notify_func)(notify_baton, notify, pool);
            }
        }

      /* Split PATH into parent PDIR and basename BNAME. */
      svn_path_split(path, &pdir, &bname, pool);
      if (! parent_entry)
        {
          err = svn_wc_entry(&parent_entry, pdir, adm_access, FALSE, pool);
          if (err)
            goto abort_report;
        }

      if (parent_entry
          && parent_entry->url
          && entry->url
          && strcmp(entry->url,
                    svn_path_url_add_component(parent_entry->url,
                                               bname, pool)))
        {
          /* This file is disjoint with respect to its parent
             directory.  Since we are looking at the actual target of
             the report (not some file in a subdirectory of a target
             directory), and that target is a file, we need to pass an
             empty string to link_path. */
          err = reporter->link_path(report_baton,
                                    "",
                                    entry->url,
                                    entry->revision,
                                    entry->depth,
                                    FALSE,
                                    entry->lock_token,
                                    pool);
          if (err)
            goto abort_report;
        }
      else if (entry->revision != base_rev || entry->lock_token)
        {
          /* If this entry is a file node, we just want to report that
             node's revision.  Since we are looking at the actual target
             of the report (not some file in a subdirectory of a target
             directory), and that target is a file, we need to pass an
             empty string to set_path. */
          err = reporter->set_path(report_baton, "", base_rev, entry->depth,
                                   FALSE,
                                   entry->lock_token, pool);
          if (err)
            goto abort_report;
        }
    }

  /* Finish the report, which causes the update editor to be driven. */
  return reporter->finish_report(report_baton, pool);

 abort_report:
  /* Clean up the fs transaction. */
  if ((fserr = reporter->abort_report(report_baton, pool)))
    {
      fserr = svn_error_quick_wrap(fserr, _("Error aborting report"));
      svn_error_compose(err, fserr);
    }
  return err;
}


/*** Compatibility wrapper: turns an svn_ra_reporter2_t into an
     svn_ra_reporter3_t.

     This code looks like it duplicates code in libsvn_ra/ra_loader.c,
     but it does not.  That code makes an new thing look like an old
     thing; this code makes an old thing look like a new thing. ***/

struct wrap_3to2_report_baton {
  const svn_ra_reporter2_t *reporter;
  void *baton;
};

static svn_error_t *wrap_3to2_set_path(void *report_baton,
                                       const char *path,
                                       svn_revnum_t revision,
                                       svn_depth_t depth,
                                       svn_boolean_t start_empty,
                                       const char *lock_token,
                                       apr_pool_t *pool)
{
  struct wrap_3to2_report_baton *wrb = report_baton;

  return wrb->reporter->set_path(wrb->baton, path, revision, start_empty,
                                 lock_token, pool);
}

static svn_error_t *wrap_3to2_delete_path(void *report_baton,
                                          const char *path,
                                          apr_pool_t *pool)
{
  struct wrap_3to2_report_baton *wrb = report_baton;

  return wrb->reporter->delete_path(wrb->baton, path, pool);
}

static svn_error_t *wrap_3to2_link_path(void *report_baton,
                                        const char *path,
                                        const char *url,
                                        svn_revnum_t revision,
                                        svn_depth_t depth,
                                        svn_boolean_t start_empty,
                                        const char *lock_token,
                                        apr_pool_t *pool)
{
  struct wrap_3to2_report_baton *wrb = report_baton;

  return wrb->reporter->link_path(wrb->baton, path, url, revision,
                                  start_empty, lock_token, pool);
}

static svn_error_t *wrap_3to2_finish_report(void *report_baton,
                                            apr_pool_t *pool)
{
  struct wrap_3to2_report_baton *wrb = report_baton;

  return wrb->reporter->finish_report(wrb->baton, pool);
}

static svn_error_t *wrap_3to2_abort_report(void *report_baton,
                                           apr_pool_t *pool)
{
  struct wrap_3to2_report_baton *wrb = report_baton;

  return wrb->reporter->abort_report(wrb->baton, pool);
}

static const svn_ra_reporter3_t wrap_3to2_reporter = {
  wrap_3to2_set_path,
  wrap_3to2_delete_path,
  wrap_3to2_link_path,
  wrap_3to2_finish_report,
  wrap_3to2_abort_report
};

svn_error_t *
svn_wc_crawl_revisions2(const char *path,
                        svn_wc_adm_access_t *adm_access,
                        const svn_ra_reporter2_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_boolean_t recurse,
                        svn_boolean_t use_commit_times,
                        svn_wc_notify_func2_t notify_func,
                        void *notify_baton,
                        svn_wc_traversal_info_t *traversal_info,
                        apr_pool_t *pool)
{
  struct wrap_3to2_report_baton wrb;
  wrb.reporter = reporter;
  wrb.baton = report_baton;

  return svn_wc_crawl_revisions3(path,
                                 adm_access,
                                 &wrap_3to2_reporter, &wrb,
                                 restore_files,
                                 SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                 FALSE,
                                 use_commit_times,
                                 notify_func,
                                 notify_baton,
                                 traversal_info,
                                 pool);
}


/*** Compatibility wrapper: turns an svn_ra_reporter_t into an
     svn_ra_reporter2_t.

     This code looks like it duplicates code in libsvn_ra/ra_loader.c,
     but it does not.  That code makes an new thing look like an old
     thing; this code makes an old thing look like a new thing. ***/

struct wrap_2to1_report_baton {
  const svn_ra_reporter_t *reporter;
  void *baton;
};

static svn_error_t *wrap_2to1_set_path(void *report_baton,
                                       const char *path,
                                       svn_revnum_t revision,
                                       svn_boolean_t start_empty,
                                       const char *lock_token,
                                       apr_pool_t *pool)
{
  struct wrap_2to1_report_baton *wrb = report_baton;

  return wrb->reporter->set_path(wrb->baton, path, revision, start_empty,
                                 pool);
}

static svn_error_t *wrap_2to1_delete_path(void *report_baton,
                                          const char *path,
                                          apr_pool_t *pool)
{
  struct wrap_2to1_report_baton *wrb = report_baton;

  return wrb->reporter->delete_path(wrb->baton, path, pool);
}

static svn_error_t *wrap_2to1_link_path(void *report_baton,
                                        const char *path,
                                        const char *url,
                                        svn_revnum_t revision,
                                        svn_boolean_t start_empty,
                                        const char *lock_token,
                                        apr_pool_t *pool)
{
  struct wrap_2to1_report_baton *wrb = report_baton;

  return wrb->reporter->link_path(wrb->baton, path, url, revision,
                                  start_empty, pool);
}

static svn_error_t *wrap_2to1_finish_report(void *report_baton,
                                            apr_pool_t *pool)
{
  struct wrap_2to1_report_baton *wrb = report_baton;

  return wrb->reporter->finish_report(wrb->baton, pool);
}

static svn_error_t *wrap_2to1_abort_report(void *report_baton,
                                           apr_pool_t *pool)
{
  struct wrap_2to1_report_baton *wrb = report_baton;

  return wrb->reporter->abort_report(wrb->baton, pool);
}

static const svn_ra_reporter2_t wrap_2to1_reporter = {
  wrap_2to1_set_path,
  wrap_2to1_delete_path,
  wrap_2to1_link_path,
  wrap_2to1_finish_report,
  wrap_2to1_abort_report
};

svn_error_t *
svn_wc_crawl_revisions(const char *path,
                       svn_wc_adm_access_t *adm_access,
                       const svn_ra_reporter_t *reporter,
                       void *report_baton,
                       svn_boolean_t restore_files,
                       svn_boolean_t recurse,
                       svn_boolean_t use_commit_times,
                       svn_wc_notify_func_t notify_func,
                       void *notify_baton,
                       svn_wc_traversal_info_t *traversal_info,
                       apr_pool_t *pool)
{
  struct wrap_2to1_report_baton wrb;
  svn_wc__compat_notify_baton_t nb;

  wrb.reporter = reporter;
  wrb.baton = report_baton;

  nb.func = notify_func;
  nb.baton = notify_baton;

  return svn_wc_crawl_revisions2(path, adm_access, &wrap_2to1_reporter, &wrb,
                                 restore_files, recurse, use_commit_times,
                                 svn_wc__compat_call_notify_func, &nb,
                                 traversal_info,
                                 pool);
}


/*** Copying stream ***/

/* A copying stream is a bit like the unix tee utility:
 *
 * It reads the SOURCE when asked for data and while returning it,
 * also writes the same data to TARGET.
 */
struct copying_stream_baton
{
  /* Stream to read input from. */
  svn_stream_t *source;

  /* Stream to write all data read to. */
  svn_stream_t *target;
};


static svn_error_t *
read_handler_copy(void *baton, char *buffer, apr_size_t *len)
{
  struct copying_stream_baton *btn = baton;

  SVN_ERR(svn_stream_read(btn->source, buffer, len));

  return svn_stream_write(btn->target, buffer, len);
}

static svn_error_t *
close_handler_copy(void *baton)
{
  struct copying_stream_baton *btn = baton;

  SVN_ERR(svn_stream_close(btn->target));
  return svn_stream_close(btn->source);
}


/* Return a stream - allocated in POOL - which reads its input
 * from SOURCE and, while returning that to the caller, at the
 * same time writes that to TARGET.
 */
static svn_stream_t *
copying_stream(svn_stream_t *source,
               svn_stream_t *target,
               apr_pool_t *pool)
{
  struct copying_stream_baton *baton;
  svn_stream_t *stream;

  baton = apr_palloc(pool, sizeof (*baton));
  baton->source = source;
  baton->target = target;

  stream = svn_stream_create(baton, pool);
  svn_stream_set_read(stream, read_handler_copy);
  svn_stream_set_close(stream, close_handler_copy);

  return stream;
}

svn_error_t *
svn_wc_transmit_text_deltas2(const char **tempfile,
                             unsigned char digest[],
                             const char *path,
                             svn_wc_adm_access_t *adm_access,
                             svn_boolean_t fulltext,
                             const svn_delta_editor_t *editor,
                             void *file_baton,
                             apr_pool_t *pool)
{
  const char *tmp_base;
  svn_txdelta_window_handler_t handler;
  void *wh_baton;
  svn_txdelta_stream_t *txdelta_stream;
  apr_file_t *basefile = NULL;
  apr_file_t *tempbasefile;
  const char *base_digest_hex = NULL;
  const unsigned char *base_digest = NULL;
  const unsigned char *local_digest = NULL;
  svn_error_t *err;
  const svn_wc_entry_t *ent;
  svn_stream_t *base_stream;
  svn_stream_t *local_stream;
  apr_time_t wf_time;

  SVN_ERR(svn_wc_entry(&ent, path, adm_access, FALSE, pool));

  /* Get timestamp of working file, to check for modifications during
     commit. */
  SVN_ERR(svn_io_file_affected_time(&wf_time, path, pool));

  /* Translated input */
  SVN_ERR(svn_wc_translated_stream(&local_stream, path, path,
                                   adm_access, SVN_WC_TRANSLATE_TO_NF, pool));



  tmp_base = svn_wc__text_base_path(path, TRUE, pool);
  /* Alert the caller that we have created a temporary file that might
     need to be cleaned up, if he asked for one. */
  if (tempfile)
    {
      *tempfile = tmp_base;

      /* Make an untranslated copy of the working file in the
         administrative tmp area because a) we need to detranslate eol
         and keywords anyway, and b) after the commit, we're going to
         copy the tmp file to become the new text base anyway. */
      SVN_ERR(svn_io_file_open(&tempbasefile, tmp_base,
                               APR_WRITE | APR_CREATE, APR_OS_DEFAULT, pool));

      /* Wrap the translated stream with a new stream that writes the
         translated contents into the new text base file as we read from it.
         Note that the new text base file will be closed when the new stream
         is closed. */
      local_stream
        = copying_stream(local_stream,
                         svn_stream_from_aprfile2(tempbasefile, FALSE, pool),
                         pool);
    }

  if (! fulltext)
    {
      if (! ent->checksum)
        {
          /*### FIXME: The entries file should hold a checksum */
          unsigned char tmp_digest[APR_MD5_DIGESTSIZE];

          /* If there's no checksum in this entry, calculate one */
          const char *tb = svn_wc__text_base_path (path, FALSE, pool);

          SVN_ERR (svn_io_file_checksum (tmp_digest, tb, pool));
          base_digest_hex = svn_md5_digest_to_cstring_display(tmp_digest, pool);
        }
      else
        base_digest_hex = ent->checksum;

      SVN_ERR(svn_wc__open_text_base(&basefile, path, APR_READ, pool));
    }

  /* Tell the editor that we're about to apply a textdelta to the
     file baton; the editor returns to us a window consumer and baton.  */
  SVN_ERR(editor->apply_textdelta
          (file_baton, base_digest_hex, pool, &handler, &wh_baton));

  /* Create a text-delta stream object that pulls
     data out of the two files. */
  base_stream = svn_stream_from_aprfile2(basefile, TRUE, pool);
  if (! fulltext)
    base_stream
      = svn_stream_checksummed(base_stream, &base_digest, NULL, TRUE, pool);

  svn_txdelta(&txdelta_stream, base_stream, local_stream, pool);

  /* Pull windows from the delta stream and feed to the consumer. */
  err = svn_txdelta_send_txstream(txdelta_stream, handler, wh_baton, pool);

  /* Close the two streams to force writing the digest,
     if we already have an error, ignore this one. */
  if (err)
    {
      svn_error_clear(svn_stream_close(base_stream));
      svn_error_clear(svn_stream_close(local_stream));
    }
  else
    {
      SVN_ERR(svn_stream_close(base_stream));
      SVN_ERR(svn_stream_close(local_stream));
    }

  /* If we have an error, it may be caused by a corrupt text base.
     Check the checksum and discard `err' if they don't match. */
  if (! fulltext && ent->checksum && base_digest)
    {
      /*### FIXME: The entries file should hold a checksum,
        meaning the above condition should not include ent->checksum */

      base_digest_hex = svn_md5_digest_to_cstring_display(base_digest, pool);

      if (strcmp(base_digest_hex, ent->checksum) != 0)
        {
          /* The entry checksum does not match the actual text
             base checksum.  Extreme badness. Of course,
             theoretically we could just switch to
             fulltext transmission here, and everything would
             work fine; after all, we're going to replace the
             text base with a new one in a moment anyway, and
             we'd fix the checksum then.  But it's better to
             error out.  People should know that their text
             bases are getting corrupted, so they can
             investigate.  Other commands could be affected,
             too, such as `svn diff'.  */

          /* Deliberately ignore errors; the error about the
             checksum mismatch is more important to return. */
          svn_error_clear(err);
          svn_error_clear(svn_io_remove_file(tmp_base, pool));

          return svn_error_createf
            (SVN_ERR_WC_CORRUPT_TEXT_BASE, NULL,
             _("Checksum mismatch for '%s'; "
               "expected: '%s', actual: '%s'"),
             svn_path_local_style(svn_wc__text_base_path(path, FALSE, pool),
                                   pool),
             ent->checksum, base_digest_hex);
        }
   }

  /* Now, handle that delta transmission error if any, so we can stop
     thinking about it after this point. */
  SVN_ERR_W(err, apr_psprintf(pool,
                              _("While preparing '%s' for commit"),
                              svn_path_local_style(path, pool)));

  /* Close base file, if it was opened. */
  if (basefile)
    SVN_ERR(svn_wc__close_text_base(basefile, path, 0, pool));

  local_digest = svn_txdelta_md5_digest(txdelta_stream);

  if (digest)
    memcpy(digest, local_digest, APR_MD5_DIGESTSIZE);

  /* Close the file baton, and get outta here. */
  return editor->close_file
    (file_baton, svn_md5_digest_to_cstring(local_digest, pool), pool);
}

svn_error_t *
svn_wc_transmit_text_deltas(const char *path,
                            svn_wc_adm_access_t *adm_access,
                            svn_boolean_t fulltext,
                            const svn_delta_editor_t *editor,
                            void *file_baton,
                            const char **tempfile,
                            apr_pool_t *pool)
{
  return svn_wc_transmit_text_deltas2(tempfile, NULL, path, adm_access,
                                      fulltext, editor, file_baton, pool);
}


svn_error_t *
svn_wc_transmit_prop_deltas(const char *path,
                            svn_wc_adm_access_t *adm_access,
                            const svn_wc_entry_t *entry,
                            const svn_delta_editor_t *editor,
                            void *baton,
                            const char **tempfile,
                            apr_pool_t *pool)
{
  int i;
  apr_array_header_t *propmods;

  if (tempfile)
    *tempfile = NULL;

  /* Get an array of local changes by comparing the hashes. */
  SVN_ERR(svn_wc_get_prop_diffs(&propmods, NULL,
                                path, adm_access, pool));

  /* Apply each local change to the baton */
  for (i = 0; i < propmods->nelts; i++)
    {
      const svn_prop_t *p = &APR_ARRAY_IDX(propmods, i, svn_prop_t);
      if (entry->kind == svn_node_file)
        SVN_ERR(editor->change_file_prop(baton, p->name, p->value, pool));
      else
        SVN_ERR(editor->change_dir_prop(baton, p->name, p->value, pool));
    }

  return SVN_NO_ERROR;
}
