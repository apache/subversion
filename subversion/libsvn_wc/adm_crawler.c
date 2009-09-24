/*
 * adm_crawler.c:  report local WC mods to an Editor.
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

/* ==================================================================== */


#include <string.h>

#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_io.h"
#include "svn_base64.h"
#include "svn_delta.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"

#include "private/svn_wc_private.h"

#include "wc.h"
#include "adm_files.h"
#include "props.h"
#include "translate.h"
#include "entries.h"
#include "lock.h"

#include "svn_private_config.h"
#include "private/svn_debug.h"


/* Helper for report_revisions_and_depths().

   Perform an atomic restoration of the file LOCAL_ABSPATH; that is, copy
   the file's text-base to the administrative tmp area, and then move
   that file to LOCAL_ABSPATH with possible translations/expansions.  If
   USE_COMMIT_TIMES is set, then set working file's timestamp to
   last-commit-time.  Either way, set entry-timestamp to match that of
   the working file when all is finished. 
   
   Not that a valid access baton with a write lock to the directory of
   LOCAL_ABSPATH must be available in DB.*/
static svn_error_t *
restore_file(svn_wc__db_t *db,
             const char *local_abspath,
             svn_boolean_t use_commit_times,
             apr_pool_t *pool)
{
  svn_stream_t *src_stream;
  svn_boolean_t special;
  svn_wc_entry_t newentry;
  const char *adm_dir, *file_path;
  svn_wc_adm_access_t *adm_access;

  svn_dirent_split(local_abspath, &adm_dir, &file_path, pool);

  adm_access = svn_wc__adm_retrieve_internal2(db, adm_dir, pool);
  SVN_ERR_ASSERT(adm_access != NULL);

  file_path = svn_dirent_join(svn_wc_adm_access_path(adm_access), file_path,
                              pool);

  SVN_ERR(svn_wc__get_pristine_contents(&src_stream, db, local_abspath, pool,
                                        pool));

  SVN_ERR(svn_wc__get_special(&special, db, local_abspath, pool));
  if (special)
    {
      svn_stream_t *dst_stream;

      /* Copy the source into the destination to create the special file.
         The creation wil happen atomically. */
      SVN_ERR(svn_subst_create_specialfile(&dst_stream, file_path,
                                           pool, pool));
      /* ### need a cancel_func/baton */
      SVN_ERR(svn_stream_copy3(src_stream, dst_stream, NULL, NULL, pool));
    }
  else
    {
      svn_subst_eol_style_t style;
      const char *eol_str;
      apr_hash_t *keywords;
      const char *tmp_dir;
      const char *tmp_file;
      svn_stream_t *tmp_stream;

      SVN_ERR(svn_wc__get_eol_style(&style, &eol_str, db, local_abspath,
                                    pool, pool));
      SVN_ERR(svn_wc__get_keywords(&keywords, db, local_abspath, NULL, pool,
                                   pool));

      /* Get a temporary destination so we can use a rename to create the
         real destination atomically. */
      tmp_dir = svn_wc__adm_child(svn_wc_adm_access_path(adm_access),
                                  SVN_WC__ADM_TMP, pool);
      SVN_ERR(svn_stream_open_unique(&tmp_stream, &tmp_file, tmp_dir,
                                     svn_io_file_del_none, pool, pool));

      /* Wrap the (temp) destination stream with a translating stream. */
      if (svn_subst_translation_required(style, eol_str, keywords,
                                         FALSE /* special */,
                                         TRUE /* force_eol_check */))
        {
          tmp_stream = svn_subst_stream_translated(tmp_stream,
                                                   eol_str,
                                                   TRUE /* repair */,
                                                   keywords,
                                                   TRUE /* expand */,
                                                   pool);
        }

      SVN_ERR(svn_stream_copy3(src_stream, tmp_stream, NULL, NULL, pool));
      /* ### need a cancel_func/baton */
      SVN_ERR(svn_io_file_rename(tmp_file, file_path, pool));
    }

  SVN_ERR(svn_wc__maybe_set_read_only(NULL, db, local_abspath, pool));

  /* If necessary, tweak the new working file's executable bit. */
  SVN_ERR(svn_wc__maybe_set_executable(NULL, db, local_abspath, pool));

  /* Remove any text conflict */
  SVN_ERR(svn_wc_resolved_conflict4(file_path, adm_access, TRUE, FALSE,
                                    FALSE, svn_depth_empty,
                                    svn_wc_conflict_choose_merged,
                                    NULL, NULL, NULL, NULL, pool));

  /* Possibly set timestamp to last-commit-time. */
  if (use_commit_times && (! special))
    {
      apr_time_t changed_date;

      SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL,
                                   NULL, NULL, NULL,
                                   NULL, &changed_date, NULL,
                                   NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   db, local_abspath,
                                   pool, pool));

      SVN_ERR(svn_io_set_file_affected_time(changed_date, file_path, pool));

      newentry.text_time = changed_date;
    }
  else
    {
      SVN_ERR(svn_io_file_affected_time(&newentry.text_time,
                                        file_path, pool));
    }

  /* Modify our entry's text-timestamp to match the working file. */
  return svn_error_return(
    svn_wc__entry_modify2(db, local_abspath, svn_node_file, FALSE,
                          &newentry, SVN_WC__ENTRY_MODIFY_TEXT_TIME, pool));
}

/* Try to restore LOCAL_ABSPATH of node type KIND and if successfull,
   notify that the node is restored.  Use DB for accessing the working copy.
   If USE_COMMIT_TIMES is set, then set working file's timestamp to 
   last-commit-time.

   Set RESTORED to TRUE if the node is successfull restored. RESTORED will
   be FALSE if restoring this node is not supported.

   This function does all temporary allocations in SCRATCH_POOL 
 */
static svn_error_t *
restore_node(svn_boolean_t *restored,
             svn_wc__db_t *db,
             const char *local_abspath,
             svn_node_kind_t kind,
             svn_boolean_t use_commit_times,
             svn_wc_notify_func2_t notify_func,
             void *notify_baton,
             apr_pool_t *scratch_pool)
{
  *restored = FALSE;

  /* Currently we can only restore files, but we will be able to restore
     directories after we move to a single database and pristine store. */
  if (kind == svn_node_file)
    {
      /* ... recreate file from text-base, and ... */
      SVN_ERR(restore_file(db, local_abspath, use_commit_times,
                           scratch_pool));

      *restored = TRUE;
      /* ... report the restoration to the caller.  */
      if (notify_func != NULL)
        {
          svn_wc_notify_t *notify = svn_wc_create_notify(
                                            local_abspath,
                                            svn_wc_notify_restore,
                                            scratch_pool);
          notify->kind = svn_node_file;
          (*notify_func)(notify_baton, notify, scratch_pool);
        }

      /* Keep missing = FALSE */
    }

  return SVN_NO_ERROR;
}

/* Check if there is an externals definition stored on LOCAL_ABSPATH
   using DB.  In that case send the externals definition and DEPTH to
   EXTERNAL_FUNC.  Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
read_traversal_info(svn_wc__db_t *db,
                    const char *local_abspath,
                    svn_wc_external_update_t external_func,
                    void *external_baton,
                    svn_depth_t depth,
                    apr_pool_t *scratch_pool)
{
  const svn_string_t *val;

  SVN_ERR_ASSERT(external_func != NULL);

  SVN_ERR(svn_wc__internal_propget(&val, db, local_abspath,
                                   SVN_PROP_EXTERNALS,
                                   scratch_pool, scratch_pool));

  if (val)
    {
      SVN_ERR((external_func)(external_baton, local_abspath, val, val, depth,
                              scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* The recursive crawler that describes a mixed-revision working
   copy to an RA layer.  Used to initiate updates.

   This is a depth-first recursive walk of DIR_PATH under ANCHOR_ABSPATH,
   using DB.  Look at each entry and check if its revision is different
   than DIR_REV.  If so, report this fact to REPORTER.  If an entry is
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
report_revisions_and_depths(svn_wc__db_t *db,
                            const char *anchor_abspath,
                            const char *dir_path,
                            svn_revnum_t dir_rev,
                            const svn_ra_reporter3_t *reporter,
                            void *report_baton,
                            svn_wc_external_update_t external_func,
                            void *external_baton,
                            svn_wc_notify_func2_t notify_func,
                            void *notify_baton,
                            svn_boolean_t restore_files,
                            svn_depth_t depth,
                            svn_boolean_t honor_depth_exclude,
                            svn_boolean_t depth_compatibility_trick,
                            svn_boolean_t report_everything,
                            svn_boolean_t use_commit_times,
                            apr_pool_t *pool)
{
  const char *dir_abspath;
  const apr_array_header_t *children;
  apr_hash_t *dirents;
  apr_pool_t *subpool = svn_pool_create(pool), *iterpool;
  int i;
  const char *dir_repos_root, *dir_repos_relpath, *dir_url;
  svn_depth_t dir_depth;


  /* Get both the SVN Entries and the actual on-disk entries.   Also
     notice that we're picking up hidden entries too (read_children never
     hides children). */
  dir_abspath = svn_dirent_join(anchor_abspath, dir_path, subpool);
  SVN_ERR(svn_wc__db_read_children(&children, db, dir_abspath,
                                   subpool, subpool));
  SVN_ERR(svn_io_get_dir_filenames(&dirents, dir_abspath, subpool));

  /*** Do the real reporting and recursing. ***/

  /* First, look at "this dir" to see what its URL and depth are. */
  SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, &dir_repos_relpath,
                               &dir_repos_root, NULL, NULL, NULL, NULL, NULL,
                               &dir_depth, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
                               db, dir_abspath,
                               subpool, subpool));

  /* If the directory has no url, search its parents */
  if (dir_repos_relpath == NULL)
    SVN_ERR(svn_wc__db_scan_base_repos(&dir_repos_relpath, &dir_repos_root,
                                       NULL, db, dir_abspath,
                                       subpool, subpool));

  /* ### The entries based code still uses full urls */
  dir_url = svn_path_url_add_component2(dir_repos_root, dir_repos_relpath,
                                        subpool);

  /* If "this dir" has "svn:externals" property set on it, store its name
     and depth in traversal_info. */
  if (external_func)
    {
      SVN_ERR(read_traversal_info(db, dir_abspath, external_func,
                                  external_baton, dir_depth, subpool));
    }

  /* Looping over current directory's SVN entries: */
  iterpool = svn_pool_create(subpool);

  for (i = 0; i < children->nelts; ++i)
    {
      const char *child = APR_ARRAY_IDX(children, i, const char *);
      const char *this_path, *this_abspath;
      const char *this_repos_root_url, *this_repos_relpath;
      const char *this_original_repos_relpath;
      svn_wc__db_status_t this_status;
      svn_wc__db_kind_t this_kind;
      svn_revnum_t this_rev, this_base_rev;
      svn_depth_t this_depth;
      svn_wc__db_lock_t *this_lock;
      svn_boolean_t this_shadows_base, this_switched, replaced = FALSE;
      

      /* Clear the iteration subpool here because the loop has a bunch
         of 'continue' jump statements. */
      svn_pool_clear(iterpool);

      /* Compute the paths and URLs we need. */
      this_path = svn_dirent_join(dir_path, child, iterpool);
      this_abspath = svn_dirent_join(dir_abspath, child, iterpool);

      SVN_ERR(svn_wc__db_read_info(&this_status, &this_kind, &this_rev,
                                   &this_repos_relpath, &this_repos_root_url,
                                   NULL, NULL, NULL, NULL, NULL, &this_depth,
                                   NULL, NULL, NULL, NULL,
                                   &this_original_repos_relpath,
                                   NULL, NULL, NULL, NULL, NULL,
                                   &this_shadows_base, NULL, NULL, NULL,
                                   NULL, &this_lock,
                                   db, this_abspath, iterpool, iterpool));

      /* First check the depth */
      if (this_depth == svn_depth_exclude)
        {
          if (honor_depth_exclude)
            {
              /* Report the excluded path, no matter whether report_everything
                 flag is set.  Because the report_everything flag indicates
                 that the server will treate the wc as empty and thus push
                 full content of the files/subdirs. But we want to prevent the
                 server from pushing the full content of this_path at us. */

              /* The server does not support link_path report on excluded
                 path. We explicitly prohibit this situation in
                 svn_wc_crop_tree(). */
              SVN_ERR(reporter->set_path(report_baton,
                                         this_path,
                                         dir_rev,
                                         svn_depth_exclude,
                                         FALSE,
                                         NULL,
                                         iterpool));
            }
          else
            {
              /* We want to pull in the excluded target. So, report it as deleted,
                 and server will respond properly. */
              if (! report_everything)
                SVN_ERR(reporter->delete_path(report_baton,
                                              this_path, iterpool));
            }
          continue;
        }

      if (this_kind == svn_wc__db_kind_dir)
        {
          svn_revnum_t del_rev;
          SVN_ERR(svn_wc__db_temp_is_dir_deleted(&replaced, &del_rev,
                                                 db, this_abspath,
                                                 iterpool));
        }

      /*** The Big Tests: ***/

      if (this_shadows_base)
        {
          svn_wc__db_status_t this_base_status;
          SVN_ERR(svn_wc__db_base_get_info(&this_base_status, NULL,
                                           &this_base_rev,
                                           NULL, NULL, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL, NULL,
                                           NULL, NULL,
                                           db, this_abspath,
                                           iterpool, iterpool));

          if (!replaced)
            replaced = (this_base_status == svn_wc__db_status_not_present);
        }

      {
        svn_boolean_t this_absent;

        if (replaced ||
            this_status == svn_wc__db_status_absent ||
            this_status == svn_wc__db_status_excluded ||
            this_status == svn_wc__db_status_not_present)
          {
            this_absent = TRUE;
          }
        else if (this_status == svn_wc__db_status_deleted && !this_shadows_base)
          this_absent = TRUE;
        else
          this_absent = FALSE;

        /* If the entry is 'deleted' or 'absent', make sure the server
           knows it's gone... */
        if (this_absent)
          {
            /* ...unless we're reporting everything, in which case we're
               going to report it missing later anyway. */
            if (! report_everything)
              SVN_ERR(reporter->delete_path(report_baton, this_path, iterpool));
            continue;
          }
      }

      /* From here on out, ignore any entry scheduled for addition */
      if ((this_status == svn_wc__db_status_added) ||
          (this_status == svn_wc__db_status_obstructed_add))
      {
        if (!replaced)
          continue;

        if (!this_shadows_base && this_original_repos_relpath)
          continue; /* Skip copy roots (and all children) */
      }

      /* Is the entry on disk? */
      if (apr_hash_get(dirents, child, APR_HASH_KEY_STRING) == NULL)
        {
          svn_boolean_t missing = FALSE; 
          if (restore_files && this_status != svn_wc__db_status_deleted
                            && !replaced)
            {
              svn_node_kind_t dirent_kind;

              /* It is possible on a case insensitive system that the
                 entry is not really missing, but just cased incorrectly.
                 In this case we can't overwrite it with the pristine
                 version */
              SVN_ERR(svn_io_check_path(this_abspath, &dirent_kind, iterpool));

              if (dirent_kind == svn_node_none)
                {
                  svn_boolean_t restored;
                  svn_node_kind_t kind = (this_kind == svn_wc__db_kind_dir)
                                                     ? svn_node_dir
                                                     : svn_node_file;

                  SVN_ERR(restore_node(&restored, db, this_abspath, kind,
                                       use_commit_times, notify_func,
                                       notify_baton, iterpool));

                  if (!restored)
                    missing = TRUE;
                }
            }
          else
            missing = TRUE;

          /* If a directory is missing from disk, we have no way to
             recreate it locally, so report as missing and move
             along.  Again, don't bother if we're reporting
             everything, because the dir is already missing on the server. */
          if (missing && this_kind == svn_wc__db_kind_dir
               && (depth > svn_depth_files || depth == svn_depth_unknown))
            {
              if (! report_everything)
                SVN_ERR(reporter->delete_path(report_baton, this_path,
                                              iterpool));
              continue;
            }
        }

      /* And finally prepare for reporting */
      if (!this_repos_relpath)
        {
          this_switched = FALSE;
          this_repos_relpath = svn_uri_join(dir_repos_relpath, child,
                                            iterpool);
        }
      else
        {
          const char *childname = svn_uri_is_child(dir_repos_relpath,
                                                   this_repos_relpath, NULL);

          if (!childname || strcmp(childname, child) != 0)
            this_switched = TRUE;
          else
            this_switched = FALSE;
        }

      if (this_depth == svn_depth_unknown)
        this_depth = svn_depth_infinity;

      if (this_rev == SVN_INVALID_REVNUM)
        {
          /* For added and replaced nodes use their base revision
             in reports */
          this_rev = this_shadows_base ? this_base_rev : dir_rev;
        }

      /*** Files ***/
      if (this_kind == svn_wc__db_kind_file ||
          this_kind == svn_wc__db_kind_symlink)
        {
          const char *url = NULL;

          if (this_switched)
            url = svn_path_url_add_component2(dir_repos_root, this_repos_relpath, iterpool);

          if (report_everything)
            {
              /* Report the file unconditionally, one way or another. */
              if (this_switched)
                SVN_ERR(reporter->link_path(report_baton,
                                            this_path,
                                            url,
                                            this_rev,
                                            this_depth,
                                            FALSE,
                                            this_lock ? this_lock->token : NULL,
                                            iterpool));
              else
                SVN_ERR(reporter->set_path(report_baton,
                                           this_path,
                                           this_rev,
                                           this_depth,
                                           FALSE,
                                           this_lock ? this_lock->token : NULL,
                                           iterpool));
            }

          /* Possibly report a disjoint URL ... */
          else if (this_switched && !this_shadows_base)
            SVN_ERR(reporter->link_path(report_baton,
                                        this_path,
                                        url,
                                        this_rev,
                                        this_depth,
                                        FALSE,
                                        this_lock ? this_lock->token : NULL,
                                        iterpool));
          /* ... or perhaps just a differing revision or lock token,
             or the mere presence of the file in a depth-empty dir. */
          else if (this_rev != dir_rev
                   || this_lock
                   || dir_depth == svn_depth_empty)
            SVN_ERR(reporter->set_path(report_baton,
                                       this_path,
                                       this_rev,
                                       this_depth,
                                       FALSE,
                                       this_lock ? this_lock->token : NULL,
                                       iterpool));
        } /* end file case */

      /*** Directories (in recursive mode) ***/
      else if (this_kind == svn_wc__db_kind_dir
               && (depth > svn_depth_files
                   || depth == svn_depth_unknown))
        {
          const char *url = NULL;
          svn_boolean_t start_empty;
          svn_boolean_t is_incomplete = (this_status == svn_wc__db_status_incomplete);

          if (this_switched)
            url = svn_path_url_add_component2(dir_repos_root, this_repos_relpath, iterpool);

          start_empty = is_incomplete;

          if (depth_compatibility_trick
              && this_depth <= svn_depth_files
              && depth > this_depth)
            {
              start_empty = TRUE;
            }

          if (report_everything)
            {
              /* Report the dir unconditionally, one way or another. */
              if (this_switched)
                SVN_ERR(reporter->link_path(report_baton,
                                            this_path,
                                            url,
                                            this_rev,
                                            this_depth,
                                            start_empty,
                                            this_lock ? this_lock->token : NULL,
                                            iterpool));
              else
                SVN_ERR(reporter->set_path(report_baton,
                                           this_path,
                                           this_rev,
                                           this_depth,
                                           start_empty,
                                           this_lock ? this_lock->token : NULL,
                                           iterpool));
            }

          /* Possibly report a disjoint URL ... */
          else if (this_switched)
            SVN_ERR(reporter->link_path(report_baton,
                                        this_path,
                                        url,
                                        this_rev,
                                        this_depth,
                                        start_empty,
                                        this_lock ? this_lock->token : NULL,
                                        iterpool));
          /* ... or perhaps just a differing revision, lock token, incomplete
             subdir, the mere presence of the directory in a depth-empty or
             depth-files dir, or if the parent dir is at depth-immediates but
             the child is not at depth-empty.  Also describe shallow subdirs
             if we are trying to set depth to infinity. */
          else if (this_rev != dir_rev
                   || this_lock
                   || is_incomplete
                   || dir_depth == svn_depth_empty
                   || dir_depth == svn_depth_files
                   || (dir_depth == svn_depth_immediates
                       && this_depth != svn_depth_empty)
                   || (this_depth < svn_depth_infinity
                       && depth == svn_depth_infinity))
            SVN_ERR(reporter->set_path(report_baton,
                                       this_path,
                                       this_rev,
                                       this_depth,
                                       start_empty,
                                       this_lock ? this_lock->token : NULL,
                                       iterpool));

          if (SVN_DEPTH_IS_RECURSIVE(depth))
             SVN_ERR(report_revisions_and_depths(db,
                                                anchor_abspath,
                                                this_path,
                                                this_rev,
                                                reporter, report_baton,
                                                external_func, external_baton,
                                                notify_func, notify_baton,
                                                restore_files, depth,
                                                honor_depth_exclude,
                                                depth_compatibility_trick,
                                                start_empty,
                                                use_commit_times,
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
svn_wc_crawl_revisions5(svn_wc_context_t *wc_ctx,
                        const char *local_abspath,
                        const svn_ra_reporter3_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_depth_t depth,
                        svn_boolean_t honor_depth_exclude,
                        svn_boolean_t depth_compatibility_trick,
                        svn_boolean_t use_commit_times,
                        svn_wc_external_update_t external_func,
                        void *external_baton,
                        svn_wc_notify_func2_t notify_func,
                        void *notify_baton,
                        apr_pool_t *pool)
{
  svn_error_t *fserr, *err;
  const svn_wc_entry_t *entry;
  svn_revnum_t base_rev = SVN_INVALID_REVNUM;
  svn_boolean_t missing = FALSE;
  const svn_wc_entry_t *parent_entry = NULL;
  svn_boolean_t start_empty;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* The first thing we do is get the base_rev from the working copy's
     ROOT_DIRECTORY.  This is the first revnum that entries will be
     compared to. */
  err = svn_wc__get_entry(&entry, wc_ctx->db, local_abspath, TRUE,
                          svn_node_unknown, FALSE, pool, pool);

  if (err && err->apr_err != SVN_ERR_NODE_UNEXPECTED_KIND)
    return svn_error_return(err);

  if (err)
    {
      entry = NULL;
      svn_error_clear(err);
      err = NULL;
    }

  if ((! entry) || entry->deleted || ((entry->schedule == svn_wc_schedule_add)
                                      && (entry->kind == svn_node_dir)))
    {
        /* Don't check the exclude flag for the target.

         If we report the target itself as excluded, the server will
         send us nothing about the target -- but we want to permit
         targets to be explicitly pulled in.  For example, 'svn up A'
         should always work, even if its parent is svn_depth_empty or
         svn_depth_files, or even if A was explicitly excluded from a
         parent at svn_depth_immediates or svn_depth_infinity.
         Whatever the case, we want A back now. */

      /* There aren't any versioned paths to crawl which are known to
         the repository. */
      SVN_ERR(svn_wc__get_entry(&parent_entry, wc_ctx->db,
                                svn_dirent_dirname(local_abspath, pool),
                                FALSE, svn_node_dir, FALSE, pool, pool));

      base_rev = parent_entry->revision;

      /* If no versioned path exists, we use the requested depth, which
         is the depth at which the new path should be brought in.  Default
         to infinity if no explicit depth was given. */
      if (depth == svn_depth_unknown)
        depth = svn_depth_infinity;

      SVN_ERR(reporter->set_path(report_baton, "", base_rev, depth,
                                 entry ? entry->incomplete : TRUE,
                                 entry ? entry->lock_token : NULL, pool));
      SVN_ERR(reporter->delete_path(report_baton, "", pool));

      /* Finish the report, which causes the update editor to be
         driven. */
      return reporter->finish_report(report_baton, pool);
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
      SVN_ERR(svn_wc__get_entry(&parent_entry, wc_ctx->db,
                                svn_dirent_dirname(local_abspath, pool),
                                FALSE, svn_node_dir, FALSE, pool, pool));
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
      err = svn_io_stat(&info, local_abspath, APR_FINFO_MIN, pool);
      if (err)
        {
          if (APR_STATUS_IS_ENOENT(err->apr_err))
            missing = TRUE;
          svn_error_clear(err);
          err = NULL;
        }
    }

  if (missing && restore_files)
    {
      svn_boolean_t restored;

      err = restore_node(&restored, wc_ctx->db, local_abspath,
                         entry->kind, use_commit_times,
                         notify_func, notify_baton,
                         pool);

      if (err)
        {
          SVN_ERR_ASSERT(0 && "Restore failed");
          goto abort_report;
        }

      if (restored)
        missing = FALSE;
    }

  if (entry->kind == svn_node_dir)
    {
      if (missing)
        {
          /* Report missing directories as deleted to retrieve them
             from the repository. */
          err = reporter->delete_path(report_baton, "", pool);
          if (err)
            goto abort_report;
        }
      else if (depth != svn_depth_empty)
        {
          /* Recursively crawl ROOT_DIRECTORY and report differing
             revisions. */
          err = report_revisions_and_depths(wc_ctx->db,
                                            local_abspath,
                                            "",
                                            base_rev,
                                            reporter, report_baton,
                                            external_func, external_baton,
                                            notify_func, notify_baton,
                                            restore_files, depth,
                                            honor_depth_exclude,
                                            depth_compatibility_trick,
                                            start_empty,
                                            use_commit_times,
                                            pool);
          if (err)
            goto abort_report;
        }
    }

  else if (entry->kind == svn_node_file)
    {
      const char *pdir, *bname;

      /* Split PATH into parent PDIR and basename BNAME. */
      svn_dirent_split(local_abspath, &pdir, &bname, pool);
      if (! parent_entry)
        {
          err = svn_wc__get_entry(&parent_entry, wc_ctx->db, pdir,
                                  FALSE, svn_node_dir, FALSE, pool, pool);
          if (err)
            goto abort_report;
        }

      if (parent_entry
          && parent_entry->url
          && entry->url
          && strcmp(entry->url,
                    svn_path_url_add_component2(parent_entry->url,
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
svn_wc__internal_transmit_text_deltas(const char **tempfile,
                                      unsigned char digest[],
                                      svn_wc__db_t *db,
                                      const char *local_abspath,
                                      svn_boolean_t fulltext,
                                      const svn_delta_editor_t *editor,
                                      void *file_baton,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
  svn_txdelta_window_handler_t handler;
  void *wh_baton;
  const char *base_digest_hex;
  svn_checksum_t *expected_checksum = NULL;
  svn_checksum_t *verify_checksum = NULL;
  svn_checksum_t *local_checksum;
  svn_error_t *err;
  svn_stream_t *base_stream;
  svn_stream_t *local_stream;

  /* Translated input */
  SVN_ERR(svn_wc__internal_translated_stream(&local_stream, db,
                                             local_abspath, local_abspath,
                                             SVN_WC_TRANSLATE_TO_NF,
                                             scratch_pool, scratch_pool));

  /* Alert the caller that we have created a temporary file that might
     need to be cleaned up, if he asked for one. */
  if (tempfile)
    {
      const char *tmp_base;
      apr_file_t *tempbasefile;

      SVN_ERR(svn_wc__text_base_path(&tmp_base, db, local_abspath, TRUE,
                                     scratch_pool));

      *tempfile = tmp_base;

      /* Make an untranslated copy of the working file in the
         administrative tmp area because a) we need to detranslate eol
         and keywords anyway, and b) after the commit, we're going to
         copy the tmp file to become the new text base anyway. */
      SVN_ERR(svn_io_file_open(&tempbasefile, tmp_base,
                               APR_WRITE | APR_CREATE, APR_OS_DEFAULT,
                               result_pool));

      /* Wrap the translated stream with a new stream that writes the
         translated contents into the new text base file as we read from it.
         Note that the new text base file will be closed when the new stream
         is closed. */
      local_stream
        = copying_stream(local_stream,
                         svn_stream_from_aprfile2(tempbasefile, FALSE,
                                                  scratch_pool),
                         scratch_pool);
    }

  if (! fulltext)
    {
      /* Compute delta against the pristine contents */
      SVN_ERR(svn_wc__get_pristine_contents(&base_stream, db, local_abspath,
                                            scratch_pool, scratch_pool));

      SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL,
                                   NULL, NULL, NULL,
                                   NULL, NULL, NULL,
                                   NULL, NULL,
                                   &expected_checksum, NULL,
                                   NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   db, local_abspath,
                                   scratch_pool, scratch_pool));

      /* ### We want expected_checksum to ALWAYS be present, but on old
         ### working copies maybe it won't be (unclear?). If it is there,
         ### then we can use it as an expected value. If it is NOT there,
         ### then we must compute it for the apply_textdelta() call. */
      if (expected_checksum)
        {
          /* Compute a checksum for what is *actually* found */
          base_stream = svn_stream_checksummed2(base_stream, &verify_checksum,
                                                NULL, svn_checksum_md5, TRUE,
                                                scratch_pool);
        }
      else
        {
          svn_stream_t *p_stream;

          /* ### we should ALREADY have the checksum for pristine. */
          SVN_ERR(svn_wc__get_pristine_contents(&p_stream, db, local_abspath,
                                               scratch_pool, scratch_pool));
          p_stream = svn_stream_checksummed2(p_stream, &expected_checksum,
                                             NULL, svn_checksum_md5, TRUE,
                                             scratch_pool);

          /* Closing this will cause a full read/checksum. */
          SVN_ERR(svn_stream_close(p_stream));
        }

      /* apply_textdelta() is working against a base with this checksum */
      base_digest_hex = svn_checksum_to_cstring_display(expected_checksum,
                                                        scratch_pool);
    }
  else
    {
      /* Send a fulltext. */
      base_stream = svn_stream_empty(scratch_pool);
      base_digest_hex = NULL;
    }

  /* Tell the editor that we're about to apply a textdelta to the
     file baton; the editor returns to us a window consumer and baton.  */
  SVN_ERR(editor->apply_textdelta(file_baton, base_digest_hex, scratch_pool,
                                  &handler, &wh_baton));

  /* Run diff processing, throwing windows at the handler. */
  err = svn_txdelta_run(base_stream, local_stream,
                        handler, wh_baton,
                        svn_checksum_md5, &local_checksum,
                        NULL, NULL,
                        scratch_pool, scratch_pool);

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
  if (expected_checksum && verify_checksum
      && !svn_checksum_match(expected_checksum, verify_checksum))
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
      if (tempfile)
        svn_error_clear(svn_io_remove_file2(*tempfile, TRUE, scratch_pool));

      {
        const char *text_base;
        
        SVN_ERR(svn_wc__text_base_path(&text_base, db, local_abspath, FALSE,
                                       scratch_pool));

        return svn_error_createf(SVN_ERR_WC_CORRUPT_TEXT_BASE, NULL,
                      _("Checksum mismatch for '%s':\n"
                        "   expected:  %s\n"
                        "     actual:  %s\n"),
                      svn_dirent_local_style(text_base, scratch_pool),
                      svn_checksum_to_cstring_display(expected_checksum,
                                                      scratch_pool),
                      svn_checksum_to_cstring_display(verify_checksum,
                                                      scratch_pool));
      }
    }

  /* Now, handle that delta transmission error if any, so we can stop
     thinking about it after this point. */
  SVN_ERR_W(err, apr_psprintf(scratch_pool,
                              _("While preparing '%s' for commit"),
                              svn_dirent_local_style(local_abspath,
                                                     scratch_pool)));

  if (digest)
    memcpy(digest, local_checksum->digest, svn_checksum_size(local_checksum));

  /* Close the file baton, and get outta here. */
  return editor->close_file(file_baton,
                            svn_checksum_to_cstring(local_checksum,
                                                    scratch_pool),
                            scratch_pool);
}

svn_error_t *
svn_wc_transmit_text_deltas3(const char **tempfile,
                             unsigned char digest[],
                             svn_wc_context_t *wc_ctx,
                             const char *local_abspath,
                             svn_boolean_t fulltext,
                             const svn_delta_editor_t *editor,
                             void *file_baton,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  return svn_wc__internal_transmit_text_deltas(tempfile, digest, wc_ctx->db,
                                               local_abspath, fulltext, editor,
                                               file_baton, result_pool,
                                               scratch_pool);
}

svn_error_t *
svn_wc__internal_transmit_prop_deltas(svn_wc__db_t *db,
                                     const char *local_abspath,
                                     const svn_delta_editor_t *editor,
                                     void *baton,
                                     apr_pool_t *scratch_pool)
{
  int i;
  apr_array_header_t *propmods;
  svn_wc__db_kind_t kind;

  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, FALSE, scratch_pool));

  /* Get an array of local changes by comparing the hashes. */
  SVN_ERR(svn_wc__internal_propdiff(&propmods, NULL, db, local_abspath,
                                    scratch_pool, scratch_pool));

  /* Apply each local change to the baton */
  for (i = 0; i < propmods->nelts; i++)
    {
      const svn_prop_t *p = &APR_ARRAY_IDX(propmods, i, svn_prop_t);
      if (kind == svn_wc__db_kind_file)
        SVN_ERR(editor->change_file_prop(baton, p->name, p->value, 
                                         scratch_pool));
      else
        SVN_ERR(editor->change_dir_prop(baton, p->name, p->value, 
                                        scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_transmit_prop_deltas2(svn_wc_context_t *wc_ctx,
                             const char *local_abspath,
                             const svn_delta_editor_t *editor,
                             void *baton,
                             apr_pool_t *scratch_pool)
{
  return svn_wc__internal_transmit_prop_deltas(wc_ctx->db, local_abspath,
                                               editor, baton, scratch_pool);
}
