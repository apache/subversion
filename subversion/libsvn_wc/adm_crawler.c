/*
 * adm_crawler.c:  report local WC mods to an Editor.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
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
#include "workqueue.h"
#include "conflicts.h"

#include "svn_private_config.h"


/* Helper for report_revisions_and_depths().

   Perform an atomic restoration of the file LOCAL_ABSPATH; that is, copy
   the file's text-base to the administrative tmp area, and then move
   that file to LOCAL_ABSPATH with possible translations/expansions.  If
   USE_COMMIT_TIMES is set, then set working file's timestamp to
   last-commit-time.  Either way, set entry-timestamp to match that of
   the working file when all is finished.

   If REMOVE_TEXT_CONFLICT is TRUE, remove an existing text conflict
   from LOCAL_ABSPATH.

   Not that a valid access baton with a write lock to the directory of
   LOCAL_ABSPATH must be available in DB.*/
static svn_error_t *
restore_file(svn_wc__db_t *db,
             const char *local_abspath,
             svn_boolean_t use_commit_times,
             svn_boolean_t remove_text_conflicts,
             apr_pool_t *scratch_pool)
{
  svn_skel_t *work_item;

  SVN_ERR(svn_wc__wq_build_file_install(&work_item,
                                        db, local_abspath,
                                        NULL /* source_abspath */,
                                        use_commit_times,
                                        TRUE /* record_fileinfo */,
                                        scratch_pool, scratch_pool));
  /* ### we need an existing path for wq_add. not entirely WRI_ABSPATH yet  */
  SVN_ERR(svn_wc__db_wq_add(db,
                            svn_dirent_dirname(local_abspath, scratch_pool),
                            work_item, scratch_pool));

  /* Run the work item immediately.  */
  SVN_ERR(svn_wc__wq_run(db, local_abspath,
                         NULL, NULL, /* ### nice to have cancel_func/baton */
                         scratch_pool));

  /* Remove any text conflict */
  if (remove_text_conflicts)
    SVN_ERR(svn_wc__resolve_text_conflict(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_restore(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_boolean_t use_commit_times,
               apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_wc__db_kind_t kind;
  svn_node_kind_t disk_kind;

  SVN_ERR(svn_io_check_path(local_abspath, &disk_kind, scratch_pool));

  if (disk_kind != svn_node_none)
    return svn_error_createf(SVN_ERR_WC_PATH_FOUND, NULL,
                             _("The existing node '%s' can not be restored."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));



  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL,
                               wc_ctx->db, local_abspath,
                               scratch_pool, scratch_pool));

  switch (status)
    {
      case svn_wc__db_status_added:
        SVN_ERR(svn_wc__db_scan_addition(&status, NULL, NULL, NULL, NULL, NULL,
                                         NULL, NULL, NULL,
                                         wc_ctx->db, local_abspath,
                                         scratch_pool, scratch_pool));
        if (status != svn_wc__db_status_added)
          break; /* Has pristine version */
      case svn_wc__db_status_deleted:
      case svn_wc__db_status_not_present:
      case svn_wc__db_status_absent:
      case svn_wc__db_status_excluded:
        return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                 _("The node '%s' can not be restored."),
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool));
      default:
        break;
    }

  if (kind == svn_wc__db_kind_file || kind == svn_wc__db_kind_symlink)
    SVN_ERR(restore_file(wc_ctx->db, local_abspath, use_commit_times, FALSE,
                         scratch_pool));
  else
    SVN_ERR(svn_io_dir_make(local_abspath, APR_OS_DEFAULT, scratch_pool));

  return SVN_NO_ERROR;
}

/* Try to restore LOCAL_ABSPATH of node type KIND and if successfull,
   notify that the node is restored.  Use DB for accessing the working copy.
   If USE_COMMIT_TIMES is set, then set working file's timestamp to
   last-commit-time.

   This function does all temporary allocations in SCRATCH_POOL
 */
static svn_error_t *
restore_node(svn_wc__db_t *db,
             const char *local_abspath,
             svn_wc__db_kind_t kind,
             svn_boolean_t use_commit_times,
             svn_wc_notify_func2_t notify_func,
             void *notify_baton,
             apr_pool_t *scratch_pool)
{
  if (kind == svn_wc__db_kind_file || kind == svn_wc__db_kind_symlink)
    {
      /* Recreate file from text-base */
      SVN_ERR(restore_file(db, local_abspath, use_commit_times, TRUE,
                           scratch_pool));
    }
  else if (kind == svn_wc__db_kind_dir)
    {
      /* Recreating a directory is just a mkdir */
      SVN_ERR(svn_io_dir_make(local_abspath, APR_OS_DEFAULT, scratch_pool));
    }

  /* ... report the restoration to the caller.  */
  if (notify_func != NULL)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(local_abspath,
                                                     svn_wc_notify_restore,
                                                     scratch_pool);
      notify->kind = svn_node_file;
      (*notify_func)(notify_baton, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* Check if there is an externals definition stored on LOCAL_ABSPATH
   using DB.  In that case send the externals definition and DEPTH to
   EXTERNAL_FUNC.  Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
read_externals_info(svn_wc__db_t *db,
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

   If EXTERNAL_FUNC is non-NULL, then send externals information with
   the help of EXTERNAL_BATON

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
                            apr_pool_t *scratch_pool)
{
  const char *dir_abspath;
  apr_hash_t *base_children;
  apr_hash_t *dirents;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;
  const char *dir_repos_root, *dir_repos_relpath;
  svn_depth_t dir_depth;
  svn_error_t *err;


  /* Get both the SVN Entries and the actual on-disk entries.   Also
     notice that we're picking up hidden entries too (read_children never
     hides children). */
  dir_abspath = svn_dirent_join(anchor_abspath, dir_path, scratch_pool);
  SVN_ERR(svn_wc__db_base_get_children_info(&base_children, db, dir_abspath,
                                            scratch_pool, iterpool));

  err = svn_io_get_dirents3(&dirents, dir_abspath, TRUE,
                            scratch_pool, scratch_pool);

  if (err && (APR_STATUS_IS_ENOENT(err->apr_err)
              || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err)))
    {
      svn_error_clear(err);
      dirents = apr_hash_make(scratch_pool);
    }
  else
    SVN_ERR(err);

  /*** Do the real reporting and recursing. ***/

  /* First, look at "this dir" to see what its URL and depth are. */
  SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, NULL, &dir_repos_relpath,
                                   &dir_repos_root, NULL, NULL, NULL, NULL,
                                   NULL, &dir_depth, NULL, NULL, NULL, NULL,
                                   NULL,
                                   db, dir_abspath,
                                   scratch_pool, iterpool));

  /* If the directory has no url, search its parents */
  if (dir_repos_relpath == NULL)
    SVN_ERR(svn_wc__db_scan_base_repos(&dir_repos_relpath, &dir_repos_root,
                                       NULL, db, dir_abspath,
                                       scratch_pool, iterpool));

  /* If "this dir" has "svn:externals" property set on it,
   * call the external_func callback. */
  if (external_func)
    SVN_ERR(read_externals_info(db, dir_abspath, external_func,
                                external_baton, dir_depth, iterpool));

  /* Looping over current directory's BASE children: */
  for (hi = apr_hash_first(scratch_pool, base_children); 
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const char *child = svn__apr_hash_index_key(hi);
      const char *this_path, *this_abspath;
      svn_boolean_t this_switched = FALSE;
      struct svn_wc__db_base_info_t *ths = svn__apr_hash_index_val(hi);

      /* Clear the iteration subpool here because the loop has a bunch
         of 'continue' jump statements. */
      svn_pool_clear(iterpool);

      /* Compute the paths and URLs we need. */
      this_path = svn_dirent_join(dir_path, child, iterpool);
      this_abspath = svn_dirent_join(dir_abspath, child, iterpool);

      /* First check for exclusion */
      if (ths->status == svn_wc__db_status_excluded)
        {
          if (honor_depth_exclude)
            {
              /* Report the excluded path, no matter whether report_everything
                 flag is set.  Because the report_everything flag indicates
                 that the server will treat the wc as empty and thus push
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
              /* We want to pull in the excluded target. So, report it as
                 deleted, and server will respond properly. */
              if (! report_everything)
                SVN_ERR(reporter->delete_path(report_baton,
                                              this_path, iterpool));
            }
          continue;
        }

      /*** The Big Tests: ***/
      if (ths->status == svn_wc__db_status_absent
          || ths->status == svn_wc__db_status_not_present)
        {
          /* If the entry is 'absent' or 'not-present', make sure the server
             knows it's gone...
             ...unless we're reporting everything, in which case we're
             going to report it missing later anyway.

             This instructs the server to send it back to us, if it is
             now available (an addition after a not-present state), or if
             it is now authorized (change in authz for the absent item).  */
          if (! report_everything)
            SVN_ERR(reporter->delete_path(report_baton, this_path, iterpool));
          continue;
        }

      /* Is the entry NOT on the disk? We may be able to restore it.  */
      if (apr_hash_get(dirents, child, APR_HASH_KEY_STRING) == NULL)
        {
          svn_wc__db_status_t wrk_status;
          svn_wc__db_kind_t wrk_kind;

          SVN_ERR(svn_wc__db_read_info(&wrk_status, &wrk_kind, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL,
                                       db, this_abspath, iterpool, iterpool));

          if (wrk_status == svn_wc__db_status_added)
            SVN_ERR(svn_wc__db_scan_addition(&wrk_status, NULL, NULL, NULL,
                                             NULL, NULL, NULL, NULL, NULL,
                                             db, this_abspath,
                                             iterpool, iterpool));

          if (restore_files
              && wrk_status != svn_wc__db_status_added
              && wrk_status != svn_wc__db_status_deleted
              && wrk_status != svn_wc__db_status_excluded
              && wrk_status != svn_wc__db_status_not_present
              && wrk_status != svn_wc__db_status_absent)
            {
              svn_node_kind_t dirent_kind;

              /* It is possible on a case insensitive system that the
                 entry is not really missing, but just cased incorrectly.
                 In this case we can't overwrite it with the pristine
                 version */
              SVN_ERR(svn_io_check_path(this_abspath, &dirent_kind, iterpool));

              if (dirent_kind == svn_node_none)
                {
                  SVN_ERR(restore_node(db, this_abspath, wrk_kind,
                                       use_commit_times, notify_func,
                                       notify_baton, iterpool));
                }
            }
        }

      /* And finally prepare for reporting */
      if (!ths->repos_relpath)
        {
          ths->repos_relpath = svn_relpath_join(dir_repos_relpath, child,
                                                iterpool);
        }
      else
        {
          const char *childname = svn_relpath_is_child(dir_repos_relpath,
                                                       ths->repos_relpath,
                                                       NULL);

          if (childname == NULL || strcmp(childname, child) != 0)
            {
              this_switched = TRUE;
            }
        }

      /* Tweak THIS_DEPTH to a useful value.  */
      if (ths->depth == svn_depth_unknown)
        ths->depth = svn_depth_infinity;

      /* Obstructed nodes might report SVN_INVALID_REVNUM. Tweak it.

         ### it seems that obstructed nodes should be handled quite a
         ### bit differently. maybe reported as missing, like not-present
         ### or absent nodes?  */
      if (!SVN_IS_VALID_REVNUM(ths->revnum))
        ths->revnum = dir_rev;

      /*** File Externals **/
      if (ths->update_root)
        {
          /* File externals are ... special.  We ignore them. */;
        }

      /*** Files ***/
      else if (ths->kind == svn_wc__db_kind_file ||
               ths->kind == svn_wc__db_kind_symlink)
        {
          if (report_everything)
            {
              /* Report the file unconditionally, one way or another. */
              if (this_switched)
                SVN_ERR(reporter->link_path(report_baton,
                                            this_path,
                                            svn_path_url_add_component2(
                                                dir_repos_root,
                                                ths->repos_relpath, iterpool),
                                            ths->revnum,
                                            ths->depth,
                                            FALSE,
                                            ths->lock ? ths->lock->token : NULL,
                                            iterpool));
              else
                SVN_ERR(reporter->set_path(report_baton,
                                           this_path,
                                           ths->revnum,
                                           ths->depth,
                                           FALSE,
                                           ths->lock ? ths->lock->token : NULL,
                                           iterpool));
            }

          /* Possibly report a disjoint URL ... */
          else if (this_switched)
            SVN_ERR(reporter->link_path(report_baton,
                                        this_path,
                                        svn_path_url_add_component2(
                                                dir_repos_root,
                                                ths->repos_relpath, iterpool),
                                        ths->revnum,
                                        ths->depth,
                                        FALSE,
                                        ths->lock ? ths->lock->token : NULL,
                                        iterpool));
          /* ... or perhaps just a differing revision or lock token,
             or the mere presence of the file in a depth-empty dir. */
          else if (ths->revnum != dir_rev
                   || ths->lock
                   || dir_depth == svn_depth_empty)
            SVN_ERR(reporter->set_path(report_baton,
                                       this_path,
                                       ths->revnum,
                                       ths->depth,
                                       FALSE,
                                       ths->lock ? ths->lock->token : NULL,
                                       iterpool));
        } /* end file case */

      /*** Directories (in recursive mode) ***/
      else if (ths->kind == svn_wc__db_kind_dir
               && (depth > svn_depth_files
                   || depth == svn_depth_unknown))
        {
          svn_boolean_t is_incomplete;
          svn_boolean_t start_empty;

          is_incomplete = (ths->status == svn_wc__db_status_incomplete);
          start_empty = is_incomplete;

          if (depth_compatibility_trick
              && ths->depth <= svn_depth_files
              && depth > ths->depth)
            {
              start_empty = TRUE;
            }

          if (report_everything)
            {
              /* Report the dir unconditionally, one way or another... */
              if (this_switched)
                SVN_ERR(reporter->link_path(report_baton,
                                            this_path,
                                            svn_path_url_add_component2(
                                                dir_repos_root,
                                                ths->repos_relpath, iterpool),
                                            ths->revnum,
                                            ths->depth,
                                            start_empty,
                                            ths->lock ? ths->lock->token
                                                      : NULL,
                                            iterpool));
              else
                SVN_ERR(reporter->set_path(report_baton,
                                           this_path,
                                           ths->revnum,
                                           ths->depth,
                                           start_empty,
                                           ths->lock ? ths->lock->token : NULL,
                                           iterpool));
            }
          else if (this_switched)
            {
              /* ...or possibly report a disjoint URL ... */
              SVN_ERR(reporter->link_path(report_baton,
                                          this_path,
                                          svn_path_url_add_component2(
                                              dir_repos_root,
                                              ths->repos_relpath, iterpool),
                                          ths->revnum,
                                          ths->depth,
                                          start_empty,
                                          ths->lock ? ths->lock->token : NULL,
                                          iterpool));
            }
          else if (ths->revnum != dir_rev
                   || ths->lock
                   || is_incomplete
                   || dir_depth == svn_depth_empty
                   || dir_depth == svn_depth_files
                   || (dir_depth == svn_depth_immediates
                       && ths->depth != svn_depth_empty)
                   || (ths->depth < svn_depth_infinity
                       && depth == svn_depth_infinity))
            {
              /* ... or perhaps just a differing revision, lock token,
                 incomplete subdir, the mere presence of the directory
                 in a depth-empty or depth-files dir, or if the parent
                 dir is at depth-immediates but the child is not at
                 depth-empty.  Also describe shallow subdirs if we are
                 trying to set depth to infinity. */
              SVN_ERR(reporter->set_path(report_baton,
                                         this_path,
                                         ths->revnum,
                                         ths->depth,
                                         start_empty,
                                         ths->lock ? ths->lock->token : NULL,
                                         iterpool));
            }

          /* Finally, recurse if necessary and appropriate. */
          if (SVN_DEPTH_IS_RECURSIVE(depth))
            SVN_ERR(report_revisions_and_depths(db,
                                                anchor_abspath,
                                                this_path,
                                                ths->revnum,
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
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Helper for svn_wc_crawl_revisions5() that finds a base revision for a node
   that doesn't have one itself. */
static svn_error_t *
find_base_rev(svn_revnum_t *base_rev,
              svn_wc__db_t *db,
              const char *local_abspath,
              const char *top_local_abspath,
              apr_pool_t *pool)
{
  const char *op_root_abspath;
  svn_wc__db_status_t status;
  svn_boolean_t have_base;

  SVN_ERR(svn_wc__db_read_info(&status, NULL, base_rev, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               &have_base, NULL, NULL, NULL,
                               db, local_abspath, pool, pool));

  if (SVN_IS_VALID_REVNUM(*base_rev))
      return SVN_NO_ERROR;

  if (have_base)
    return svn_error_return(
        svn_wc__db_base_get_info(NULL, NULL, base_rev, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL,
                                 db, local_abspath, pool, pool));

  if (status == svn_wc__db_status_added)
    {
      SVN_ERR(svn_wc__db_scan_addition(NULL, &op_root_abspath, NULL, NULL,
                                       NULL, NULL, NULL, NULL,  NULL,
                                       db, local_abspath, pool, pool));

      return svn_error_return(
                 find_base_rev(base_rev,
                               db, svn_dirent_dirname(op_root_abspath, pool),
                               top_local_abspath,
                               pool));
    }
  else if (status == svn_wc__db_status_deleted)
    {
      const char *work_del_abspath;
       SVN_ERR(svn_wc__db_scan_deletion(NULL, NULL, &work_del_abspath,
                                       db, local_abspath, pool, pool));

      if (work_del_abspath != NULL)
        return svn_error_return(
                 find_base_rev(base_rev,
                               db, work_del_abspath,
                               top_local_abspath,
                               pool));
    }

  return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                           _("Can't retrieve base revision for %s"),
                           svn_dirent_local_style(top_local_abspath, pool));
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
                        apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = wc_ctx->db;
  svn_error_t *fserr, *err;
  svn_revnum_t target_rev = SVN_INVALID_REVNUM;
  svn_boolean_t start_empty;
  svn_wc__db_status_t status;
  svn_wc__db_kind_t target_kind = svn_wc__db_kind_unknown;
  const char *repos_relpath=NULL, *repos_root=NULL;
  svn_depth_t target_depth = svn_depth_unknown;
  svn_wc__db_lock_t *target_lock = NULL;
  svn_node_kind_t disk_kind;
  svn_boolean_t explicit_rev;
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* The first thing we do is get the base_rev from the working copy's
     ROOT_DIRECTORY.  This is the first revnum that entries will be
     compared to. */
  err = svn_wc__db_base_get_info(&status, &target_kind, &target_rev,
                                 &repos_relpath, &repos_root,
                                 NULL, NULL, NULL, NULL, NULL,
                                 &target_depth, NULL, NULL, NULL,
                                 &target_lock, NULL,
                                 db, local_abspath, scratch_pool,
                                 scratch_pool);

  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_return(err);

      svn_error_clear(err);
      SVN_ERR(svn_wc__db_read_kind(&target_kind, db, local_abspath, TRUE,
                                   scratch_pool));

      if (target_kind == svn_wc__db_kind_file
          || target_kind == svn_wc__db_kind_symlink)
        status = svn_wc__db_status_absent; /* Crawl via parent dir */
      else
        status = svn_wc__db_status_not_present; /* As checkout */
    }

  if (status == svn_wc__db_status_not_present
      || status == svn_wc__db_status_absent
      || (target_kind == svn_wc__db_kind_dir
          && status != svn_wc__db_status_normal
          && status != svn_wc__db_status_incomplete))
    {
      /* The target does not exist or is a local addition */

      if (!SVN_IS_VALID_REVNUM(target_rev))
        target_rev = 0;

      if (depth == svn_depth_unknown)
        depth = svn_depth_infinity;

      SVN_ERR(reporter->set_path(report_baton, "", target_rev, depth,
                                 FALSE,
                                 NULL,
                                 scratch_pool));
      SVN_ERR(reporter->delete_path(report_baton, "", scratch_pool));

      /* Finish the report, which causes the update editor to be
         driven. */
      SVN_ERR(reporter->finish_report(report_baton, scratch_pool));

      return SVN_NO_ERROR;
    }

  if (!repos_root || !repos_relpath)
    {
      /* Ok, that leaves a local addition. Deleted and not existing nodes
         are already handled. */
      SVN_ERR(svn_wc__db_scan_addition(NULL, NULL, &repos_relpath,
                                       &repos_root, NULL, NULL, NULL, NULL,
                                       NULL, db, local_abspath,
                                       scratch_pool, scratch_pool));
    }

  if (!SVN_IS_VALID_REVNUM(target_rev))
    {
      SVN_ERR(find_base_rev(&target_rev, db, local_abspath, local_abspath,
                            scratch_pool));
      explicit_rev = TRUE;
    }
  else
    explicit_rev = FALSE;

  start_empty = (status == svn_wc__db_status_incomplete);
  if (depth_compatibility_trick
      && target_depth <= svn_depth_immediates
      && depth > target_depth)
    {
      start_empty = TRUE;
    }

  if (target_depth == svn_depth_unknown)
    target_depth = svn_depth_infinity;

  SVN_ERR(svn_io_check_path(local_abspath, &disk_kind, scratch_pool));

  /* Determine if there is a missing node that should be restored */
  if (disk_kind == svn_node_none)
    {
      svn_wc__db_status_t wrk_status;
      err = svn_wc__db_read_info(&wrk_status, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL,
                                 db, local_abspath,
                                 scratch_pool, scratch_pool);


      if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          svn_error_clear(err);
          wrk_status = svn_wc__db_status_not_present;
        }
      else
        SVN_ERR(err);

      if (wrk_status == svn_wc__db_status_added)
        SVN_ERR(svn_wc__db_scan_addition(&wrk_status, NULL, NULL, NULL, NULL,
                                         NULL, NULL, NULL, NULL,
                                         db, local_abspath,
                                         scratch_pool, scratch_pool));

      if (restore_files
          && wrk_status != svn_wc__db_status_added
          && wrk_status != svn_wc__db_status_deleted
          && wrk_status != svn_wc__db_status_excluded
          && wrk_status != svn_wc__db_status_not_present
          && wrk_status != svn_wc__db_status_absent)
        {
          SVN_ERR(restore_node(wc_ctx->db, local_abspath,
                               target_kind, use_commit_times,
                               notify_func, notify_baton,
                               scratch_pool));
        }
    }

  /* The first call to the reporter merely informs it that the
     top-level directory being updated is at BASE_REV.  Its PATH
     argument is ignored. */
  SVN_ERR(reporter->set_path(report_baton, "", target_rev, target_depth,
                             start_empty, NULL, scratch_pool));

  if (target_kind == svn_wc__db_kind_dir)
    {
      if (depth != svn_depth_empty)
        {
          /* Recursively crawl ROOT_DIRECTORY and report differing
             revisions. */
          err = report_revisions_and_depths(wc_ctx->db,
                                            local_abspath,
                                            "",
                                            target_rev,
                                            reporter, report_baton,
                                            external_func, external_baton,
                                            notify_func, notify_baton,
                                            restore_files, depth,
                                            honor_depth_exclude,
                                            depth_compatibility_trick,
                                            start_empty,
                                            use_commit_times,
                                            scratch_pool);
          if (err)
            goto abort_report;
        }
    }

  else if (target_kind == svn_wc__db_kind_file ||
           target_kind == svn_wc__db_kind_symlink)
    {
      svn_boolean_t skip_set_path  = FALSE;
      const char *parent_abspath, *base;
      svn_wc__db_status_t parent_status;
      const char *parent_repos_relpath;

      svn_dirent_split(&parent_abspath, &base, local_abspath,
                       scratch_pool);

      /* We can assume a file is in the same repository as its parent
         directory, so we only look at the relpath. */
      err = svn_wc__db_base_get_info(&parent_status, NULL, NULL,
                                     &parent_repos_relpath, NULL, NULL, NULL,
                                     NULL, NULL, NULL, NULL, NULL, NULL,
                                     NULL, NULL, NULL,
                                     db, parent_abspath,
                                     scratch_pool, scratch_pool);

      if (err)
        goto abort_report;

      if (strcmp(repos_relpath,
                 svn_relpath_join(parent_repos_relpath, base,
                                  scratch_pool)) != 0)
        {
          /* This file is disjoint with respect to its parent
             directory.  Since we are looking at the actual target of
             the report (not some file in a subdirectory of a target
             directory), and that target is a file, we need to pass an
             empty string to link_path. */
          err = reporter->link_path(report_baton,
                                    "",
                                    svn_path_url_add_component2(
                                                    repos_root,
                                                    repos_relpath,
                                                    scratch_pool),
                                    target_rev,
                                    target_depth,
                                    FALSE,
                                    target_lock ? target_lock->token : NULL,
                                    scratch_pool);
          if (err)
            goto abort_report;
          skip_set_path = TRUE;
        }

      if (!skip_set_path && (explicit_rev || target_lock))
        {
          /* If this entry is a file node, we just want to report that
             node's revision.  Since we are looking at the actual target
             of the report (not some file in a subdirectory of a target
             directory), and that target is a file, we need to pass an
             empty string to set_path. */
          err = reporter->set_path(report_baton, "", target_rev,
                                   target_depth,
                                   FALSE,
                                   target_lock ? target_lock->token : NULL,
                                   scratch_pool);
          if (err)
            goto abort_report;
        }
    }

  /* Finish the report, which causes the update editor to be driven. */
  return svn_error_return(reporter->finish_report(report_baton, scratch_pool));

 abort_report:
  /* Clean up the fs transaction. */
  if ((fserr = reporter->abort_report(report_baton, scratch_pool)))
    {
      fserr = svn_error_quick_wrap(fserr, _("Error aborting report"));
      svn_error_compose(err, fserr);
    }
  return svn_error_return(err);
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


/* */
static svn_error_t *
read_handler_copy(void *baton, char *buffer, apr_size_t *len)
{
  struct copying_stream_baton *btn = baton;

  SVN_ERR(svn_stream_read(btn->source, buffer, len));

  return svn_stream_write(btn->target, buffer, len);
}

/* */
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
                                      const svn_checksum_t **new_text_base_md5_checksum,
                                      const svn_checksum_t **new_text_base_sha1_checksum,
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
  const svn_checksum_t *expected_md5_checksum;
  svn_checksum_t *verify_checksum = NULL;  /* calc'd MD5 of BASE_STREAM */
  svn_checksum_t *local_md5_checksum;  /* calc'd MD5 of LOCAL_STREAM */
  svn_checksum_t *local_sha1_checksum;  /* calc'd SHA1 of LOCAL_STREAM */
  const char *new_pristine_tmp_abspath;
  svn_error_t *err;
  svn_stream_t *base_stream;  /* delta source */
  svn_stream_t *local_stream;  /* delta target: LOCAL_ABSPATH transl. to NF */

  /* Translated input */
  SVN_ERR(svn_wc__internal_translated_stream(&local_stream, db,
                                             local_abspath, local_abspath,
                                             SVN_WC_TRANSLATE_TO_NF,
                                             scratch_pool, scratch_pool));

  /* If the caller wants a copy of the working file translated to
   * repository-normal form, make the copy by tee-ing the stream and set
   * *TEMPFILE to the path to it.  This is only needed for the 1.6 API,
   * 1.7 doesn't set TEMPFILE.  Even when using the 1.6 API this file
   * is not used by the functions that would have used it when using
   * the 1.6 code.  It's possible that 3rd party users (if there are any)
   * might expect this file to be a text-base. */
  if (tempfile)
    {
      svn_stream_t *tempstream;

      /* It can't be the same location as in 1.6 because the admin directory
         no longer exists. */
      SVN_ERR(svn_stream_open_unique(&tempstream, tempfile,
                                     NULL, svn_io_file_del_none,
                                     result_pool, scratch_pool));

      /* Wrap the translated stream with a new stream that writes the
         translated contents into the new text base file as we read from it.
         Note that the new text base file will be closed when the new stream
         is closed. */
      local_stream = copying_stream(local_stream, tempstream, scratch_pool);
    }
  if (new_text_base_sha1_checksum)
    {
      svn_stream_t *new_pristine_stream;

      SVN_ERR(svn_wc__open_writable_base(&new_pristine_stream,
                                         &new_pristine_tmp_abspath,
                                         NULL, &local_sha1_checksum,
                                         db, local_abspath,
                                         scratch_pool, scratch_pool));
      local_stream = copying_stream(local_stream, new_pristine_stream,
                                    scratch_pool);
    }

  /* If sending a full text is requested, or if there is no pristine text
   * (e.g. the node is locally added), then set BASE_STREAM to an empty
   * stream and leave EXPECTED_MD5_CHECKSUM and VERIFY_CHECKSUM as NULL.
   *
   * Otherwise, set BASE_STREAM to a stream providing the base (source) text
   * for the delta, set EXPECTED_MD5_CHECKSUM to its stored MD5 checksum,
   * and arrange for its VERIFY_CHECKSUM to be calculated later. */
  if (! fulltext)
    {
      /* Compute delta against the pristine contents */
      SVN_ERR(svn_wc__get_pristine_contents(&base_stream, db, local_abspath,
                                            scratch_pool, scratch_pool));
      if (base_stream == NULL)
        {
          base_stream = svn_stream_empty(scratch_pool);
          expected_md5_checksum = NULL;
        }
      else
        {
          SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL,
                                       NULL, NULL, NULL,
                                       NULL, NULL, NULL,
                                       NULL, NULL,
                                       &expected_md5_checksum, NULL,
                                       NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL,
                                       NULL, NULL, NULL,
                                       db, local_abspath,
                                       scratch_pool, scratch_pool));
          if (expected_md5_checksum == NULL)
            return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                     _("Pristine checksum for file '%s' is missing"),
                                     svn_dirent_local_style(local_abspath,
                                                            scratch_pool));
          /* We need to get the MD5 checksum because we want to pass it to
           * apply_textdelta(). */
          if (expected_md5_checksum->kind != svn_checksum_md5)
            SVN_ERR(svn_wc__db_pristine_get_md5(&expected_md5_checksum,
                                                db, local_abspath,
                                                expected_md5_checksum,
                                                scratch_pool, scratch_pool));

          /* Arrange to set VERIFY_CHECKSUM to the MD5 of what is *actually*
             found when the base stream is read. */
          base_stream = svn_stream_checksummed2(base_stream, &verify_checksum,
                                                NULL, svn_checksum_md5, TRUE,
                                                scratch_pool);
        }
    }
  else
    {
      /* Send a fulltext. */
      base_stream = svn_stream_empty(scratch_pool);
      expected_md5_checksum = NULL;
    }

  /* Tell the editor that we're about to apply a textdelta to the
     file baton; the editor returns to us a window consumer and baton.  */
  {
    /* apply_textdelta() is working against a base with this checksum */
    const char *base_digest_hex = NULL;

    if (expected_md5_checksum)
      /* ### Why '..._display()'?  expected_md5_checksum should never be all-
       * zero, but if it is, we would want to pass NULL not an all-zero
       * digest to apply_textdelta(), wouldn't we? */
      base_digest_hex = svn_checksum_to_cstring_display(expected_md5_checksum,
                                                        scratch_pool);

    SVN_ERR(editor->apply_textdelta(file_baton, base_digest_hex, scratch_pool,
                                    &handler, &wh_baton));
  }

  /* Run diff processing, throwing windows at the handler. */
  err = svn_txdelta_run(base_stream, local_stream,
                        handler, wh_baton,
                        svn_checksum_md5, &local_md5_checksum,
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
  if (expected_md5_checksum && verify_checksum
      && !svn_checksum_match(expected_md5_checksum, verify_checksum))
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

      return svn_error_create(SVN_ERR_WC_CORRUPT_TEXT_BASE,
            svn_checksum_mismatch_err(expected_md5_checksum, verify_checksum,
                            scratch_pool,
                            _("Checksum mismatch for text base of '%s'"),
                            svn_dirent_local_style(local_abspath,
                                                   scratch_pool)),
            NULL);
    }

  /* Now, handle that delta transmission error if any, so we can stop
     thinking about it after this point. */
  SVN_ERR_W(err, apr_psprintf(scratch_pool,
                              _("While preparing '%s' for commit"),
                              svn_dirent_local_style(local_abspath,
                                                     scratch_pool)));

  if (new_text_base_md5_checksum)
    *new_text_base_md5_checksum = svn_checksum_dup(local_md5_checksum,
                                                   result_pool);
  if (new_text_base_sha1_checksum)
    {
      SVN_ERR(svn_wc__db_pristine_install(db, new_pristine_tmp_abspath,
                                          local_sha1_checksum,
                                          local_md5_checksum,
                                          scratch_pool));
      *new_text_base_sha1_checksum = svn_checksum_dup(local_sha1_checksum,
                                                      result_pool);
    }

  /* Close the file baton, and get outta here. */
  return editor->close_file(file_baton,
                            svn_checksum_to_cstring(local_md5_checksum,
                                                    scratch_pool),
                            scratch_pool);
}

svn_error_t *
svn_wc_transmit_text_deltas3(const svn_checksum_t **new_text_base_md5_checksum,
                             const svn_checksum_t **new_text_base_sha1_checksum,
                             svn_wc_context_t *wc_ctx,
                             const char *local_abspath,
                             svn_boolean_t fulltext,
                             const svn_delta_editor_t *editor,
                             void *file_baton,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  return svn_wc__internal_transmit_text_deltas(NULL,
                                               new_text_base_md5_checksum,
                                               new_text_base_sha1_checksum,
                                               wc_ctx->db, local_abspath,
                                               fulltext, editor,
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
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;
  apr_array_header_t *propmods;
  svn_wc__db_kind_t kind;

  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, FALSE, iterpool));

  /* Get an array of local changes by comparing the hashes. */
  SVN_ERR(svn_wc__internal_propdiff(&propmods, NULL, db, local_abspath,
                                    scratch_pool, iterpool));

  /* Apply each local change to the baton */
  for (i = 0; i < propmods->nelts; i++)
    {
      const svn_prop_t *p = &APR_ARRAY_IDX(propmods, i, svn_prop_t);

      svn_pool_clear(iterpool);

      if (kind == svn_wc__db_kind_file)
        SVN_ERR(editor->change_file_prop(baton, p->name, p->value,
                                         iterpool));
      else
        SVN_ERR(editor->change_dir_prop(baton, p->name, p->value,
                                        iterpool));
    }

  svn_pool_destroy(iterpool);
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
