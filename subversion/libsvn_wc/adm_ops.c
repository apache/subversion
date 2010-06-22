/*
 * adm_ops.c: routines for affecting working copy administrative
 *            information.  NOTE: this code doesn't know where the adm
 *            info is actually stored.  Instead, generic handles to
 *            adm data are requested via a reference to some PATH
 *            (PATH being a regular, non-administrative directory or
 *            file in the working copy).
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



#include <string.h>

#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include <apr_time.h>
#include <apr_errno.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_io.h"
#include "svn_xml.h"
#include "svn_time.h"
#include "svn_diff.h"
#include "svn_sorts.h"

#include "wc.h"
#include "log.h"
#include "adm_files.h"
#include "adm_ops.h"
#include "entries.h"
#include "lock.h"
#include "props.h"
#include "translate.h"
#include "tree_conflicts.h"
#include "workqueue.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"



struct svn_wc_committed_queue_t
{
  /* The pool in which ->queue is allocated. */
  apr_pool_t *pool;
  /* Mapping (const char *) local_abspath to (committed_queue_item_t *). */
  apr_hash_t *queue;
  /* Is any item in the queue marked as 'recursive'? */
  svn_boolean_t have_recursive;
};

typedef struct
{
  const char *local_abspath;
  svn_boolean_t recurse;
  svn_boolean_t no_unlock;
  svn_boolean_t keep_changelist;

  /* The pristine text checksum(s). Either or both may be present. */
  const svn_checksum_t *md5_checksum;
  const svn_checksum_t *sha1_checksum;

  apr_hash_t *new_dav_cache;
} committed_queue_item_t;


apr_pool_t *
svn_wc__get_committed_queue_pool(const struct svn_wc_committed_queue_t *queue)
{
  return queue->pool;
}



/*** Finishing updates and commits. ***/

/* Helper for svn_wc__do_update_cleanup().
 *
 * Tweak the information for LOCAL_ABSPATH in DB.  If NEW_REPOS_RELPATH is
 * non-NULL update the entry to the new url specified by NEW_REPOS_RELPATH,
 * NEW_REPOS_ROOT_URL, NEW_REPOS_UUID..  If NEW_REV is valid, make this the
 * node's working revision.
 *
 * If ALLOW_REMOVAL is TRUE the tweaks might cause the node for
 * LOCAL_ABSPATH to be removed from the WC; if ALLOW_REMOVAL is FALSE this
 * will not happen.
 */
static svn_error_t *
tweak_node(svn_wc__db_t *db,
           const char *local_abspath,
           svn_wc__db_kind_t kind,
           svn_boolean_t parent_stub,
           const char *new_repos_relpath,
           const char *new_repos_root_url,
           const char *new_repos_uuid,
           svn_revnum_t new_rev,
           svn_boolean_t allow_removal,
           apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_wc__db_kind_t db_kind;
  svn_revnum_t revision;
  const char *repos_relpath, *repos_root_url, *repos_uuid;
  svn_boolean_t set_repos_relpath = FALSE;
  svn_error_t *err;

  err = svn_wc__db_base_get_info(&status, &db_kind, &revision,
                                 &repos_relpath, &repos_root_url,
                                 &repos_uuid, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, db, local_abspath,
                                 scratch_pool, scratch_pool);

  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      /* ### Tweaking should never be necessary for nodes that don't
         ### have a base node, but we still get here from many tests */
      svn_error_clear(err);
      return SVN_NO_ERROR; /* No BASE_NODE -> Added node */
    }
  else
    SVN_ERR(err);

  SVN_ERR_ASSERT(db_kind == kind);

  /* As long as this function is only called as a helper to
     svn_wc__do_update_cleanup, then it's okay to remove any entry
     under certain circumstances:

     If the entry is still marked 'deleted', then the server did not
     re-add it.  So it's really gone in this revision, thus we remove
     the entry.

     If the entry is still marked 'absent' and yet is not the same
     revision as new_rev, then the server did not re-add it, nor
     re-absent it, so we can remove the entry.

     ### This function cannot always determine whether removal is
     ### appropriate, hence the ALLOW_REMOVAL flag.  It's all a bit of a
     ### mess. */
  if (allow_removal
      && (status == svn_wc__db_status_not_present
          || (status == svn_wc__db_status_absent && revision != new_rev)))
    {
      return svn_error_return(
                svn_wc__db_temp_op_remove_entry(db, local_abspath,
                                                scratch_pool));

    }

  if (new_repos_relpath != NULL)
    {
      if (!repos_relpath)
        SVN_ERR(svn_wc__db_scan_base_repos(&repos_relpath, &repos_root_url,
                                           &repos_uuid, db, local_abspath,
                                           scratch_pool, scratch_pool));

      if (strcmp(repos_relpath, new_repos_relpath))
          set_repos_relpath = TRUE;
    }

  if (SVN_IS_VALID_REVNUM(new_rev) && new_rev == revision)
    new_rev = SVN_INVALID_REVNUM;

  if (SVN_IS_VALID_REVNUM(new_rev) || set_repos_relpath)
    {
      svn_boolean_t update_stub = 
            (db_kind == svn_wc__db_kind_dir && parent_stub);

      SVN_ERR(svn_wc__db_temp_op_set_rev_and_repos_relpath(db, local_abspath,
                                                          new_rev,
                                                          set_repos_relpath,
                                                          new_repos_relpath,
                                                          repos_root_url,
                                                          repos_uuid,
                                                          update_stub,
                                                          scratch_pool));
    }

  return SVN_NO_ERROR;
}


/* The main body of svn_wc__do_update_cleanup. */
static svn_error_t *
tweak_entries(svn_wc__db_t *db,
              const char *dir_abspath,
              const char *new_repos_relpath,
              const char *new_repos_root_url,
              const char *new_repos_uuid,
              svn_revnum_t new_rev,
              svn_wc_notify_func2_t notify_func,
              void *notify_baton,
              svn_depth_t depth,
              apr_hash_t *exclude_paths,
              apr_pool_t *pool)
{
  apr_pool_t *iterpool;
  svn_wc_notify_t *notify;
  const apr_array_header_t *children;
  int i;

  /* Skip an excluded path and its descendants. */
  if (apr_hash_get(exclude_paths, dir_abspath, APR_HASH_KEY_STRING))
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(pool);

  /* Tweak "this_dir" */
  SVN_ERR(tweak_node(db, dir_abspath, svn_wc__db_kind_dir, FALSE,
                     new_repos_relpath, new_repos_root_url, new_repos_uuid,
                     new_rev, FALSE /* allow_removal */, iterpool));

  if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  /* Early out */
  if (depth <= svn_depth_empty)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__db_base_get_children(&children, db, dir_abspath,
                                       pool, iterpool));
  for (i = 0; i < children->nelts; i++)
    {
      const char *child_basename = APR_ARRAY_IDX(children, i, const char *);
      const char *child_abspath;
      svn_wc__db_kind_t kind;
      svn_wc__db_status_t status;

      const char *child_repos_relpath = NULL;
      svn_boolean_t excluded;

      svn_pool_clear(iterpool);

      /* Derive the new URL for the current (child) entry */
      if (new_repos_relpath)
        child_repos_relpath = svn_relpath_join(new_repos_relpath,
                                               child_basename, iterpool);

      child_abspath = svn_dirent_join(dir_abspath, child_basename, iterpool);
      excluded = (apr_hash_get(exclude_paths, child_abspath,
                               APR_HASH_KEY_STRING) != NULL);

      SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL,
                                   db, child_abspath, iterpool, iterpool));

      /* If a file, or deleted, excluded or absent dir, then tweak the
         entry but don't recurse.

         ### how does this translate into wc_db land? */
      if (kind == svn_wc__db_kind_file
            || status == svn_wc__db_status_not_present
            || status == svn_wc__db_status_absent
            || status == svn_wc__db_status_excluded)
        {
          if (excluded)
            continue;

          if (kind == svn_wc__db_kind_dir)
            SVN_ERR(tweak_node(db, child_abspath, svn_wc__db_kind_dir, TRUE,
                               child_repos_relpath, new_repos_root_url,
                               new_repos_uuid, new_rev,
                               TRUE /* allow_removal */, iterpool));
          else
            SVN_ERR(tweak_node(db, child_abspath, kind, FALSE,
                               child_repos_relpath, new_repos_root_url,
                               new_repos_uuid, new_rev,
                               TRUE /* allow_removal */, iterpool));
        }

      /* If a directory and recursive... */
      else if ((depth == svn_depth_infinity
                || depth == svn_depth_immediates)
               && (kind == svn_wc__db_kind_dir))
        {
          svn_depth_t depth_below_here = depth;

          if (depth == svn_depth_immediates)
            depth_below_here = svn_depth_empty;

          /* If the directory is 'missing', remove it.  This is safe as
             long as this function is only called as a helper to
             svn_wc__do_update_cleanup, since the update will already have
             restored any missing items that it didn't want to delete. */
          if (svn_wc__adm_missing(db, child_abspath, iterpool))
            {
              if ( (status == svn_wc__db_status_added
                    || status == svn_wc__db_status_obstructed_add)
                  && !excluded)
                {
                  SVN_ERR(svn_wc__db_temp_op_remove_entry(db, child_abspath,
                                                          iterpool));

                  if (notify_func)
                    {
                      notify = svn_wc_create_notify(child_abspath,
                                                    svn_wc_notify_delete,
                                                    iterpool);

                      if (kind == svn_wc__db_kind_dir)
                        notify->kind = svn_node_dir;
                      else if (kind == svn_wc__db_kind_file)
                        notify->kind = svn_node_file;
                      else
                        notify->kind = svn_node_unknown;

                      (* notify_func)(notify_baton, notify, iterpool);
                    }
                }
              /* Else if missing item is schedule-add, do nothing. */
            }

          /* Not missing, deleted, or absent, so recurse. */
          else
            {
              SVN_ERR(tweak_entries(db, child_abspath, child_repos_relpath,
                                    new_repos_root_url, new_repos_uuid,
                                    new_rev, notify_func, notify_baton,
                                    depth_below_here,
                                    exclude_paths, iterpool));
            }
        }
    }

  /* Cleanup */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__do_update_cleanup(svn_wc__db_t *db,
                          const char *local_abspath,
                          svn_depth_t depth,
                          const char *new_repos_relpath,
                          const char *new_repos_root_url,
                          const char *new_repos_uuid,
                          svn_revnum_t new_revision,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          apr_hash_t *exclude_paths,
                          apr_pool_t *pool)
{
  svn_wc__db_status_t status;
  svn_wc__db_kind_t kind;
  svn_error_t *err;

  if (apr_hash_get(exclude_paths, local_abspath, APR_HASH_KEY_STRING))
    return SVN_NO_ERROR;

  err = svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL,
                             db, local_abspath, pool, pool);
  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR(err);

  switch (status)
    {
      case svn_wc__db_status_excluded:
      case svn_wc__db_status_absent:
      case svn_wc__db_status_not_present:
        return SVN_NO_ERROR;
      case svn_wc__db_status_obstructed:
      case svn_wc__db_status_obstructed_add:
      case svn_wc__db_status_obstructed_delete:
        /* There is only a parent stub. That's fine... just tweak it
           and avoid directory recursion.  */
        SVN_ERR(tweak_node(db, local_abspath, svn_wc__db_kind_dir, TRUE,
                           new_repos_relpath, new_repos_root_url,
                           new_repos_uuid, new_revision,
                           FALSE /* allow_removal */, pool));
        return SVN_NO_ERROR;

      /* Explicitly ignore other statii */
      default:
        break;
    }

  if (kind == svn_wc__db_kind_file || kind == svn_wc__db_kind_symlink)
    {
      /* Parent not updated so don't remove PATH entry.  */
      SVN_ERR(tweak_node(db, local_abspath, kind, FALSE,
                         new_repos_relpath, new_repos_root_url, new_repos_uuid,
                         new_revision, FALSE /* allow_removal */, pool));
    }
  else if (kind == svn_wc__db_kind_dir)
    {
      SVN_ERR(tweak_entries(db, local_abspath, new_repos_relpath,
                            new_repos_root_url, new_repos_uuid, new_revision,
                            notify_func, notify_baton,
                            depth, exclude_paths, pool));
    }
  else
    return svn_error_createf(SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                             _("Unrecognized node kind: '%s'"),
                             svn_dirent_local_style(local_abspath, pool));

  return SVN_NO_ERROR;
}


/* Queue work items that will finish a commit of the file or directory
 * LOCAL_ABSPATH in DB:
 *   - queue the removal of any "revert-base" props and text files;
 *   - queue an update of the DB entry for this node
 *
 * ### The Pristine Store equivalent should be:
 *   - remember the old BASE_NODE and WORKING_NODE pristine text c'sums;
 *   - queue an update of the DB entry for this node (incl. updating the
 *       BASE_NODE c'sum and setting the WORKING_NODE c'sum to NULL);
 *   - queue deletion of the old pristine texts by the remembered checksums.
 *
 * CHECKSUM is the checksum of the new text base for LOCAL_ABSPATH, and must
 * be provided if there is one, else NULL. */
static svn_error_t *
process_committed_leaf(svn_wc__db_t *db,
                       const char *local_abspath,
                       svn_revnum_t new_revnum,
                       apr_time_t new_date,
                       const char *rev_author,
                       apr_hash_t *new_dav_cache,
                       svn_boolean_t no_unlock,
                       svn_boolean_t keep_changelist,
                       const svn_checksum_t *checksum,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_wc__db_kind_t kind;
  const svn_checksum_t *copied_checksum;
  const char *adm_abspath;
  const char *tmp_text_base_abspath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL,
                               NULL, NULL, NULL,
                               NULL, NULL, NULL,
                               NULL, NULL, &copied_checksum,
                               NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));

  if (kind == svn_wc__db_kind_dir)
    adm_abspath = local_abspath;
  else
    adm_abspath = svn_dirent_dirname(local_abspath, scratch_pool);
  SVN_ERR(svn_wc__write_check(db, adm_abspath, scratch_pool));

  if (status == svn_wc__db_status_deleted
      || status == svn_wc__db_status_obstructed_delete)
    {
      return svn_error_return(svn_wc__wq_add_deletion_postcommit(
                                db, local_abspath, new_revnum, no_unlock,
                                scratch_pool));
    }

#if (SVN_WC__VERSION < SVN_WC__PROPS_IN_DB)
  /* Queue a removal of any "revert" properties now. These correspond to
     the BASE properties, but hidden by new pristine props in WORKING.
     Regardless, the commit will be installing new BASE props.  */
  /* ### this goes away once props are fully in the database  */
  {
    const char *revert_props_abspath;
    svn_skel_t *work_item;

    /* ### this breaks the abstraction of svn_wc__props_delete, but
       ### screw it. this is transitional code.  */
    /* ### what happens if the node changes its KIND? should be okay
       ### since we disallow that today, and props should be in the DB
       ### by the time that we DO allow that.  */
    SVN_ERR(svn_wc__prop_path(&revert_props_abspath, local_abspath, kind,
                              svn_wc__props_revert, scratch_pool));

    SVN_ERR(svn_wc__wq_build_file_remove(&work_item,
                                         db, revert_props_abspath,
                                         scratch_pool, scratch_pool));
    SVN_ERR(svn_wc__db_wq_add(db, adm_abspath, work_item, scratch_pool));
  }
#endif

  /* ### this picks up file and symlink  */
  if (kind != svn_wc__db_kind_dir)
    {
#ifdef SVN_EXPERIMENTAL_PRISTINE
      /* The old pristine text will be dereferenced and (possibly) removed
         from the pristine store when the new one replaces it. */
#else
      /* Queue a removal any "revert" text base now.  */
      {
        const char *revert_abspath;
        svn_skel_t *work_item;

        SVN_ERR(svn_wc__text_revert_path(&revert_abspath, db, local_abspath,
                                         scratch_pool));
        SVN_ERR(svn_wc__wq_build_file_remove(&work_item, db, revert_abspath,
                                             scratch_pool, scratch_pool));
        SVN_ERR(svn_wc__db_wq_add(db, adm_abspath, work_item, scratch_pool));
      }
#endif

      /* If we sent a delta (meaning: post-copy modification),
         then this file will appear in the queue and so we should have
         its checksum already. */
      if (checksum == NULL)
        {
          /* It was copied and not modified. We should have a text
             base for it. And the entry should have a checksum. */
          if (copied_checksum != NULL)
            {
              checksum = copied_checksum;
            }
#ifdef SVN_DEBUG
          else
            {
              /* If we copy a deleted file, then it will become scheduled
                 for deletion, but there is no base text for it. So we
                 cannot get/compute a checksum for this file. */
              SVN_ERR_ASSERT(
                status == svn_wc__db_status_deleted
                || status == svn_wc__db_status_obstructed_delete);

              /* checksum will remain NULL in this one case. */
            }
#endif
        }
    }

  if (!no_unlock)
    {
      svn_skel_t *work_item;

      SVN_ERR(svn_wc__loggy_delete_lock(&work_item, db, adm_abspath,
                                        local_abspath, scratch_pool));
      SVN_ERR(svn_wc__db_wq_add(db, adm_abspath, work_item, scratch_pool));
    }

#ifdef SVN_EXPERIMENTAL_PRISTINE
  /* Set TMP_TEXT_BASE_ABSPATH to NULL.  The new text base will be found in
     the pristine store by its checksum. */
  tmp_text_base_abspath = NULL;
#else
  /* Set TMP_TEXT_BASE_ABSPATH to the new text base to be installed, if any.
     In effect, retrieve the temporary file that was laid down by
     svn_wc__internal_transmit_text_deltas(). */
  {
    svn_node_kind_t new_base_kind;

    SVN_ERR(svn_wc__text_base_deterministic_tmp_path(&tmp_text_base_abspath,
                                                     db, local_abspath,
                                                     scratch_pool));
    SVN_ERR(svn_io_check_path(tmp_text_base_abspath, &new_base_kind,
                              scratch_pool));
    if (new_base_kind != svn_node_file)
      tmp_text_base_abspath = NULL;
  }
#endif

  SVN_ERR(svn_wc__wq_add_postcommit(db, local_abspath, tmp_text_base_abspath,
                                    new_revnum,
                                    new_date, rev_author, checksum,
                                    new_dav_cache, keep_changelist,
                                    scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__process_committed_internal(svn_wc__db_t *db,
                                   const char *local_abspath,
                                   svn_boolean_t recurse,
                                   svn_revnum_t new_revnum,
                                   apr_time_t new_date,
                                   const char *rev_author,
                                   apr_hash_t *new_dav_cache,
                                   svn_boolean_t no_unlock,
                                   svn_boolean_t keep_changelist,
                                   const svn_checksum_t *md5_checksum,
                                   const svn_checksum_t *sha1_checksum,
                                   const svn_wc_committed_queue_t *queue,
                                   apr_pool_t *scratch_pool)
{
  svn_wc__db_kind_t kind;

  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, TRUE, scratch_pool));

  SVN_ERR(process_committed_leaf(db, local_abspath,
                                 new_revnum, new_date, rev_author,
                                 new_dav_cache,
                                 no_unlock, keep_changelist,
                                 md5_checksum, /* ### not yet: sha1_checksum, */
                                 scratch_pool));

  if (recurse && kind == svn_wc__db_kind_dir)
    {
      const apr_array_header_t *children;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i;

      /* Run the log. It might delete this node, leaving us nothing
         more to do.  */
      SVN_ERR(svn_wc__wq_run(db, local_abspath, NULL, NULL, iterpool));
      SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, TRUE, iterpool));
      if (kind == svn_wc__db_kind_unknown)
        return SVN_NO_ERROR;  /* it got deleted!  */

      /* Read PATH's entries;  this is the absolute path. */
      SVN_ERR(svn_wc__db_read_children(&children, db, local_abspath,
                                       scratch_pool, iterpool));

      /* Recursively loop over all children. */
      for (i = 0; i < children->nelts; i++)
        {
          const char *name = APR_ARRAY_IDX(children, i, const char *);
          const char *this_abspath;
          svn_wc__db_status_t status;

          svn_pool_clear(iterpool);

          this_abspath = svn_dirent_join(local_abspath, name, iterpool);

          SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL,
                                       NULL, NULL, NULL,
                                       NULL, NULL, NULL,
                                       NULL, NULL, NULL,
                                       NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL,
                                       db, this_abspath,
                                       iterpool, iterpool));

          /* We come to this branch since we have committed a copied tree.
             svn_depth_exclude is possible in this situation. So check and
             skip */
          if (status == svn_wc__db_status_excluded)
            continue;

          md5_checksum = NULL;
          sha1_checksum = NULL;
          if (kind != svn_wc__db_kind_dir)
            {
              /* Suppress log creation for deleted entries in a replaced
                 directory.  By the time any log we create here is run,
                 those entries will already have been removed (as a result
                 of running the log for the replaced directory that was
                 created at the start of this function). */
              if (status == svn_wc__db_status_deleted
                  || status == svn_wc__db_status_obstructed_delete)
                {
                  svn_boolean_t replaced;

                  SVN_ERR(svn_wc__internal_is_replaced(&replaced,
                                                       db, local_abspath,
                                                       iterpool));
                  if (replaced)
                    continue;
                }

              if (queue != NULL)
                {
                  const committed_queue_item_t *cqi
                    = apr_hash_get(queue->queue, this_abspath,
                                   APR_HASH_KEY_STRING);

                  if (cqi != NULL)
                    {
                      md5_checksum = cqi->md5_checksum;
                      sha1_checksum = cqi->sha1_checksum;
                    }
                }
            }

          /* Recurse.  Pass NULL for NEW_DAV_CACHE, because the
             ones present in the current call are only applicable to
             this one committed item. */
          SVN_ERR(svn_wc__process_committed_internal(db, this_abspath,
                                                     TRUE /* recurse */,
                                                     new_revnum, new_date,
                                                     rev_author,
                                                     NULL,
                                                     TRUE /* no_unlock */,
                                                     keep_changelist,
                                                     md5_checksum,
                                                     sha1_checksum,
                                                     queue, iterpool));

          if (kind == svn_wc__db_kind_dir)
            SVN_ERR(svn_wc__wq_run(db, this_abspath, NULL, NULL, iterpool));
        }

      svn_pool_destroy(iterpool);
   }

  return SVN_NO_ERROR;
}


apr_hash_t *
svn_wc__prop_array_to_hash(const apr_array_header_t *props,
                           apr_pool_t *result_pool)
{
  int i;
  apr_hash_t *prophash;

  if (props == NULL || props->nelts == 0)
    return NULL;

  prophash = apr_hash_make(result_pool);

  for (i = 0; i < props->nelts; i++)
    {
      const svn_prop_t *prop = APR_ARRAY_IDX(props, i, const svn_prop_t *);
      if (prop->value != NULL)
        apr_hash_set(prophash, prop->name, APR_HASH_KEY_STRING, prop->value);
    }

  return prophash;
}


svn_wc_committed_queue_t *
svn_wc_committed_queue_create(apr_pool_t *pool)
{
  svn_wc_committed_queue_t *q;

  q = apr_palloc(pool, sizeof(*q));
  q->pool = pool;
  q->queue = apr_hash_make(pool);
  q->have_recursive = FALSE;

  return q;
}

svn_error_t *
svn_wc_queue_committed3(svn_wc_committed_queue_t *queue,
                        const char *local_abspath,
                        svn_boolean_t recurse,
                        const apr_array_header_t *wcprop_changes,
                        svn_boolean_t remove_lock,
                        svn_boolean_t remove_changelist,
                        const svn_checksum_t *md5_checksum,
                        const svn_checksum_t *sha1_checksum,
                        apr_pool_t *scratch_pool)
{
  committed_queue_item_t *cqi;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  queue->have_recursive |= recurse;

  /* Use the same pool as the one QUEUE was allocated in,
     to prevent lifetime issues.  Intermediate operations
     should use SCRATCH_POOL. */

  /* Add to the array with paths and options */
  cqi = apr_palloc(queue->pool, sizeof(*cqi));
  cqi->local_abspath = local_abspath;
  cqi->recurse = recurse;
  cqi->no_unlock = !remove_lock;
  cqi->keep_changelist = !remove_changelist;
  cqi->md5_checksum = md5_checksum;
  cqi->sha1_checksum = sha1_checksum;
  cqi->new_dav_cache = svn_wc__prop_array_to_hash(wcprop_changes, queue->pool);

  apr_hash_set(queue->queue, local_abspath, APR_HASH_KEY_STRING, cqi);

  return SVN_NO_ERROR;
}

/* Return TRUE if any item of QUEUE is a parent of ITEM and will be
   processed recursively, return FALSE otherwise.

   The algorithmic complexity of this search implementation is O(queue
   length), but it's quite quick.
*/
static svn_boolean_t
have_recursive_parent(apr_hash_t *queue,
                      const committed_queue_item_t *item,
                      apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  const char *local_abspath = item->local_abspath;

  for (hi = apr_hash_first(scratch_pool, queue); hi; hi = apr_hash_next(hi))
    {
      const committed_queue_item_t *qi = svn__apr_hash_index_val(hi);

      if (qi == item)
        continue;

      if (qi->recurse && svn_dirent_is_child(qi->local_abspath, local_abspath,
                                             NULL))
        return TRUE;
    }

  return FALSE;
}

svn_error_t *
svn_wc_process_committed_queue2(svn_wc_committed_queue_t *queue,
                                svn_wc_context_t *wc_ctx,
                                svn_revnum_t new_revnum,
                                const char *rev_date,
                                const char *rev_author,
                                apr_pool_t *scratch_pool)
{
  apr_array_header_t *sorted_queue;
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_time_t new_date;

  if (rev_date)
    SVN_ERR(svn_time_from_cstring(&new_date, rev_date, iterpool));
  else
    new_date = 0;

  /* Process the queued items in order of their paths.  (The requirement is
   * probably just that a directory must be processed before its children.) */
  sorted_queue = svn_sort__hash(queue->queue, svn_sort_compare_items_as_paths,
                                scratch_pool);
  for (i = 0; i < sorted_queue->nelts; i++)
    {
      const svn_sort__item_t *sort_item
        = &APR_ARRAY_IDX(sorted_queue, i, svn_sort__item_t);
      const committed_queue_item_t *cqi = sort_item->value;

      svn_pool_clear(iterpool);

      /* Skip this item if it is a child of a recursive item, because it has
         been (or will be) accounted for when that recursive item was (or
         will be) processed. */
      if (queue->have_recursive && have_recursive_parent(queue->queue, cqi,
                                                         iterpool))
        continue;

      SVN_ERR(svn_wc__process_committed_internal(wc_ctx->db, cqi->local_abspath,
                                                 cqi->recurse, new_revnum,
                                                 new_date, rev_author,
                                                 cqi->new_dav_cache,
                                                 cqi->no_unlock,
                                                 cqi->keep_changelist,
                                                 cqi->md5_checksum,
                                                 cqi->sha1_checksum, queue,
                                                 iterpool));

      SVN_ERR(svn_wc__wq_run(wc_ctx->db, cqi->local_abspath, NULL, NULL,
                             iterpool));
    }

  svn_hash__clear(queue->queue, iterpool);

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Recursively mark a tree DIR_ABSPATH with schedule svn_wc_schedule_delete
   and a KEEP_LOCAL flag. */
static svn_error_t *
mark_tree_deleted(svn_wc__db_t *db,
                 const char *dir_abspath,
                 svn_boolean_t keep_local,
                 svn_wc_notify_func2_t notify_func,
                 void *notify_baton,
                 apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  const apr_array_header_t *children;
  int i;

  /* Read the entries file for this directory. */
  SVN_ERR(svn_wc__db_read_children(&children, db, dir_abspath,
                                   pool, iterpool));

  /* Mark each entry in the entries file. */
  for (i = 0; i < children->nelts; i++)
    {
      const char *child_basename = APR_ARRAY_IDX(children, i, const char *);
      const char *child_abspath;
      svn_boolean_t hidden;
      svn_wc__db_kind_t kind;

      /* Clear our per-iteration pool. */
      svn_pool_clear(iterpool);

      child_abspath = svn_dirent_join(dir_abspath, child_basename, iterpool);

      /* We exclude hidden nodes from this operation. */
      SVN_ERR(svn_wc__db_node_hidden(&hidden, db, child_abspath, iterpool));
      if (hidden)
        continue;

      SVN_ERR(svn_wc__db_read_kind(&kind, db, child_abspath, FALSE, iterpool));

      /* If this is a directory, recurse; otherwise, delete. */
      if (kind == svn_wc__db_kind_dir)
        {
          SVN_ERR(mark_tree_deleted(db, child_abspath,
                                    keep_local,
                                    notify_func, notify_baton,
                                    iterpool));
        }
      else
        {
          SVN_ERR(svn_wc__db_temp_op_delete(db, child_abspath, iterpool));
        }

      /* Tell someone what we've done. */
      if (notify_func != NULL)
        notify_func(notify_baton,
                    svn_wc_create_notify(child_abspath,
                                         svn_wc_notify_delete,
                                         iterpool),
                    iterpool);
    }

  /* Handle directories now, after handling their kiddos. */
  SVN_ERR(svn_wc__db_temp_op_delete(db, dir_abspath, iterpool));
  if (keep_local)
    SVN_ERR(svn_wc__db_temp_set_keep_local(db, dir_abspath, TRUE, iterpool));

  /* Destroy our per-iteration pool. */
  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Remove/erase PATH from the working copy. This involves deleting PATH
 * from the physical filesystem. PATH is assumed to be an unversioned file
 * or directory.
 *
 * If CANCEL_FUNC is non-null, invoke it with CANCEL_BATON at various
 * points, return any error immediately.
 */
static svn_error_t *
erase_unversioned_from_wc(const char *path,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *pool)
{
  svn_error_t *err;

  /* Optimize the common case: try to delete the file */
  err = svn_io_remove_file2(path, FALSE, pool);
  if (err)
    {
      /* Then maybe it was a directory? */
      svn_error_clear(err);

      err = svn_io_remove_dir2(path, FALSE, cancel_func, cancel_baton, pool);

      if (err)
        {
          /* We're unlikely to end up here. But we need this fallback
             to make sure we report the right error *and* try the
             correct deletion at least once. */
          svn_node_kind_t kind;

          svn_error_clear(err);
          SVN_ERR(svn_io_check_path(path, &kind, pool));
          if (kind == svn_node_file)
            SVN_ERR(svn_io_remove_file2(path, FALSE, pool));
          else if (kind == svn_node_dir)
            SVN_ERR(svn_io_remove_dir2(path, FALSE,
                                       cancel_func, cancel_baton, pool));
          else if (kind == svn_node_none)
            return svn_error_createf(SVN_ERR_BAD_FILENAME, NULL,
                                     _("'%s' does not exist"),
                                     svn_dirent_local_style(path, pool));
          else
            return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                     _("Unsupported node kind for path '%s'"),
                                     svn_dirent_local_style(path, pool));

        }
    }

  return SVN_NO_ERROR;
}

/* Remove/erase LOCAL_ABSPATH from the working copy. For files this involves
 * deletion from the physical filesystem.  For directories it involves the
 * deletion from the filesystem of all unversioned children, and all
 * versioned children that are files. By the time we get here, added but
 * not committed items will have been scheduled for deletion which means
 * they have become unversioned.
 *
 * The result is that all that remains are versioned directories, each with
 * its .svn directory and .svn contents.
 *
 * If CANCEL_FUNC is non-null, invoke it with CANCEL_BATON at various
 * points, return any error immediately.
 *
 * KIND is the node kind appropriate for PATH
 */
static svn_error_t *
erase_from_wc(svn_wc__db_t *db,
              const char *local_abspath,
              svn_wc__db_kind_t kind,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool)
{
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  if (kind == svn_wc__db_kind_file || kind == svn_wc__db_kind_symlink)
    {
      SVN_ERR(svn_io_remove_file2(local_abspath, TRUE, scratch_pool));
    }
  else if (kind == svn_wc__db_kind_dir)
    /* This must be a directory or absent */
    {
      const apr_array_header_t *children;
      svn_wc__db_kind_t db_kind;
      apr_pool_t *iterpool;
      apr_hash_t *versioned_dirs = apr_hash_make(scratch_pool);
      apr_hash_t *unversioned;
      apr_hash_index_t *hi;
      svn_error_t *err;
      int i;

      SVN_ERR(svn_wc__db_read_kind(&db_kind, db, local_abspath, TRUE,
                                   scratch_pool));
      if (db_kind != svn_wc__db_kind_dir)
        return SVN_NO_ERROR;

      iterpool = svn_pool_create(scratch_pool);

      SVN_ERR(svn_wc__db_read_children(&children, db, local_abspath,
                                       scratch_pool, iterpool));
      for (i = 0; i < children->nelts; i++)
        {
          const char *name = APR_ARRAY_IDX(children, i, const char *);
          svn_wc__db_status_t status;
          const char *node_abspath;

          svn_pool_clear(iterpool);

          node_abspath = svn_dirent_join(local_abspath, name, iterpool);

          SVN_ERR(svn_wc__db_read_info(&status, &db_kind, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL,
                                       db, node_abspath, iterpool, iterpool));

          if (status == svn_wc__db_status_absent ||
              status == svn_wc__db_status_not_present ||
              status == svn_wc__db_status_obstructed ||
              status == svn_wc__db_status_obstructed_add ||
              status == svn_wc__db_status_obstructed_delete ||
              status == svn_wc__db_status_excluded)
            continue; /* Not here */

          /* ### We don't have to record dirs once we have a single database */
          if (db_kind == svn_wc__db_kind_dir)
            apr_hash_set(versioned_dirs, name, APR_HASH_KEY_STRING, name);

          SVN_ERR(erase_from_wc(db, node_abspath, db_kind,
                                cancel_func, cancel_baton,
                                iterpool));
        }

      /* Now handle any remaining unversioned items */
      err = svn_io_get_dirents2(&unversioned, local_abspath, scratch_pool);
      if (err)
        {
          svn_pool_destroy(iterpool);

          if (APR_STATUS_IS_ENOTDIR(err->apr_err) ||
              APR_STATUS_IS_ENOENT(err->apr_err))
            {
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }

          return svn_error_return(err);
        }

      for (hi = apr_hash_first(scratch_pool, unversioned);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *name = svn__apr_hash_index_key(hi);

          svn_pool_clear(iterpool);

          /* The admin directory will show up, we don't want to delete it */
          if (svn_wc_is_adm_dir(name, iterpool))
            continue;

          /* Versioned directories will show up, don't delete those either */
          if (apr_hash_get(versioned_dirs, name, APR_HASH_KEY_STRING))
            continue;

          SVN_ERR(erase_unversioned_from_wc(svn_dirent_join(local_abspath,
                                                            name, iterpool),
                                            cancel_func, cancel_baton,
                                            iterpool));
        }

      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_delete4(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_boolean_t keep_local,
               svn_boolean_t delete_unversioned_target,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *pool)
{
  svn_wc__db_t *db = wc_ctx->db;
  svn_boolean_t was_add = FALSE, was_replace = FALSE;
  svn_boolean_t was_copied = FALSE;
  svn_boolean_t was_deleted = FALSE; /* Silence a gcc uninitialized warning */
  svn_error_t *err;
  svn_wc__db_status_t status;
  svn_wc__db_kind_t kind;
  svn_boolean_t have_base;

  err = svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL,
                             &have_base, NULL, NULL, NULL,
                             db, local_abspath, pool, pool);

  if (delete_unversioned_target &&
      err != NULL && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);

      if (!keep_local)
        SVN_ERR(erase_unversioned_from_wc(local_abspath,
                                          cancel_func, cancel_baton,
                                          pool));
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR(err);

  switch (status)
    {
      case svn_wc__db_status_absent:
      case svn_wc__db_status_excluded:
      case svn_wc__db_status_not_present:
        return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                 _("'%s' cannot be deleted"),
                                 svn_dirent_local_style(local_abspath, pool));

      /* Explicitly ignore other statii */
      default:
        break;
    }

  if (status == svn_wc__db_status_added)
    {
      const char *op_root_abspath;
      SVN_ERR(svn_wc__db_scan_addition(&status, &op_root_abspath, NULL, NULL,
                                       NULL, NULL, NULL, NULL,  NULL,
                                       db, local_abspath, pool, pool));

      was_copied = (status == svn_wc__db_status_copied ||
                    status == svn_wc__db_status_moved_here);

      if (!have_base)
        was_add = strcmp(op_root_abspath, local_abspath) == 0;
      else
        was_replace = TRUE;
    }

  /* ### Maybe we should disallow deleting switched nodes here? */

  if (kind == svn_wc__db_kind_dir)
    {
      svn_revnum_t unused_base_rev;

      SVN_ERR(svn_wc__db_temp_is_dir_deleted(&was_deleted, &unused_base_rev,
                                             db, local_abspath, pool));

      if (was_add && !was_deleted)
        {
          /* Deleting a directory that has been added but not yet
             committed is easy, just remove the administrative dir. */

          SVN_ERR(svn_wc__internal_remove_from_revision_control(
                                           wc_ctx->db,
                                           local_abspath,
                                           FALSE, FALSE,
                                           cancel_func, cancel_baton,
                                           pool));
        }
      else if (!was_add)
        {
          svn_boolean_t available;

          /* If the working copy in the subdirectory is not available,
             we can't mark its tree as deleted. */
          SVN_ERR(svn_wc__adm_available(&available, NULL, NULL,
                                        wc_ctx->db, local_abspath,
                                        pool));

          if (available)
            {
              /* Recursively mark a whole tree for deletion. */
              SVN_ERR(mark_tree_deleted(wc_ctx->db,
                                        local_abspath,
                                        keep_local,
                                        notify_func, notify_baton,
                                        pool));
            }
        }
      /* else
         ### Handle added directory that is deleted in parent_access
             (was_deleted=TRUE). The current behavior is to just delete the
             directory with its administrative area inside, which is OK for
             WC-1.0, but when we move to a single database per working copy
             something must unversion the directory. */
    }

  if (kind != svn_wc__db_kind_dir || !was_add || was_deleted)
    {
      const char *parent_abspath = svn_dirent_dirname(local_abspath, pool);

      /* ### The following two operations should be inside one SqLite
             transaction. For even better behavior the tree operation
             before this block needs the same handling.
             Luckily most of this is for free once properties and pristine
             are handled in the WC-NG way. */
      SVN_ERR(svn_wc__db_temp_op_delete(wc_ctx->db, local_abspath, pool));
      SVN_ERR(svn_wc__wq_add_delete(wc_ctx->db, parent_abspath, local_abspath,
                                    kind, was_add, was_copied, was_replace,
                                    pool));

      SVN_ERR(svn_wc__wq_run(db, parent_abspath, cancel_func, cancel_baton,
                             pool));
    }

  /* Report the deletion to the caller. */
  if (notify_func != NULL)
    (*notify_func)(notify_baton,
                   svn_wc_create_notify(local_abspath, svn_wc_notify_delete,
                                        pool), pool);

  if (kind == svn_wc__db_kind_dir && was_add)
    {
      /* We have to release the WC-DB handles, to allow removing
         the directory on windows.

         A better solution would probably be to call svn_wc__adm_destroy()
         in the right place, but we can't do that without breaking the API. */

      SVN_ERR(svn_wc__db_temp_forget_directory(
                               wc_ctx->db,
                               local_abspath,
                               pool));
    }

  /* By the time we get here, anything that was scheduled to be added has
     become unversioned */
  if (!keep_local)
    {
      if (was_add)
        SVN_ERR(erase_unversioned_from_wc(local_abspath,
                                          cancel_func, cancel_baton,
                                          pool));
      else
        SVN_ERR(erase_from_wc(wc_ctx->db, local_abspath, kind,
                              cancel_func, cancel_baton, pool));
    }

  return SVN_NO_ERROR;
}

/* Helper for mark_tree_copied(), handling the property juggling and
   state changes for a single item LOCAL_ABSPATH (of kind LOCAL_KIND). */
static svn_error_t *
mark_item_copied(svn_wc__db_t *db,
                 const char *local_abspath,
                 svn_wc__db_kind_t local_kind,
                 apr_pool_t *scratch_pool)
{
  apr_hash_t *props;
  svn_wc_entry_t tmp_entry;
  svn_node_kind_t kind = 
    local_kind == svn_wc__db_kind_dir ? svn_node_dir : svn_node_unknown;

  /* Squirrel away the pristine properties to install them on
     working, because we might delete the base table */
  SVN_ERR(svn_wc__db_read_pristine_props(&props, db, local_abspath,
                                         scratch_pool, scratch_pool));
  tmp_entry.copied = TRUE;
  SVN_ERR(svn_wc__entry_modify(db, local_abspath, kind, &tmp_entry,
                               SVN_WC__ENTRY_MODIFY_COPIED, scratch_pool));

  /* Reinstall the pristine properties on WORKING */
  /* ### this is all pretty crappy code anyways. not much of a problem
     ### to pile on here.
     ### thus... the node's original state may suggest there are no
     ### pristine properties. in this case, we should say this (copied)
     ### node has an empty set of properties.  */
  if (props == NULL)
    props = apr_hash_make(scratch_pool);
  SVN_ERR(svn_wc__db_temp_working_set_props(db, local_abspath, props,
                                            scratch_pool));

  /* Remove now obsolete dav cache values.  */
  SVN_ERR(svn_wc__db_base_set_dav_cache(db, local_abspath, NULL,
                                        scratch_pool));
  
  return SVN_NO_ERROR;
}

/* Recursively mark a tree DIR_ABSPATH (whose status is DIR_STATUS)
   with a COPIED flag, skip items scheduled for deletion. */
static svn_error_t *
mark_tree_copied(svn_wc__db_t *db,
                 const char *dir_abspath,
                 svn_wc__db_status_t dir_status,
                 const char *new_repos_relpath,
                 const char *new_repos_root_url,
                 const char *new_repos_uuid,
                 apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  const apr_array_header_t *children;
  int i;

  /* Tweak "this_dir" */
  SVN_ERR(tweak_node(db, dir_abspath, svn_wc__db_kind_dir, FALSE,
                     new_repos_relpath, new_repos_root_url, new_repos_uuid,
                     SVN_INVALID_REVNUM, FALSE /* allow_removal */, iterpool));

  /* Read the entries file for this directory. */
  SVN_ERR(svn_wc__db_read_children(&children, db, dir_abspath,
                                   pool, iterpool));

  /* Mark each entry in the entries file. */
  for (i = 0; i < children->nelts; i++)
    {
      const char *child_basename = APR_ARRAY_IDX(children, i, const char *);
      const char *child_abspath;
      svn_wc__db_status_t child_status;
      svn_wc__db_kind_t child_kind;
      const char *child_relpath = NULL;

      /* Clear our per-iteration pool. */
      svn_pool_clear(iterpool);

      /* Derive the new URL for the current (child) entry */
      if (new_repos_relpath)
        child_relpath = svn_relpath_join(new_repos_relpath, child_basename,
                                         iterpool);

      child_abspath = svn_dirent_join(dir_abspath, child_basename, iterpool);

      SVN_ERR(svn_wc__db_read_info(&child_status, &child_kind, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   db, child_abspath, iterpool, iterpool));

      /* ### "svn add" won't create excluded nodes, but "svn copy" will.
         ### it copies all metadata, carrying over the excluded nodes.  */

      /* If a file, or deleted, excluded or absent dir, then tweak the
         entry but don't recurse.

         ### how does this translate into wc_db land? */
      if (child_kind == svn_wc__db_kind_file
          || child_status == svn_wc__db_status_not_present
          || child_status == svn_wc__db_status_absent
          || child_status == svn_wc__db_status_excluded)
        {
          if (child_kind == svn_wc__db_kind_dir)
            SVN_ERR(tweak_node(db, child_abspath, svn_wc__db_kind_dir,
                               TRUE /* parent_stub */, child_relpath,
                               new_repos_root_url, new_repos_uuid,
                               SVN_INVALID_REVNUM, TRUE /* allow_removal */,
                               iterpool));
          else
            SVN_ERR(tweak_node(db, child_abspath, child_kind,
                               FALSE /* parent_stub */, child_relpath,
                               new_repos_root_url, new_repos_uuid,
                               SVN_INVALID_REVNUM, TRUE /* allow_removal */,
                               iterpool));
        }

      /* Skip deleted items, or otherwise "not really here" nodes.  */
      if (child_status == svn_wc__db_status_deleted
          || child_status == svn_wc__db_status_obstructed_delete
          || child_status == svn_wc__db_status_not_present
          || child_status == svn_wc__db_status_absent
          || child_status == svn_wc__db_status_excluded)
        continue;

      /* If this is a directory, recurse; otherwise, do real work. */
      if (child_kind == svn_wc__db_kind_dir)
        {
          SVN_ERR(mark_tree_copied(db, child_abspath, child_status, child_relpath,
                                   new_repos_root_url, new_repos_uuid, iterpool));
        }
      else
        {
          SVN_ERR(mark_item_copied(db, child_abspath, child_kind, iterpool));
        }
    }

  /* Here's where we handle directories. */
  if (dir_status != svn_wc__db_status_deleted
      && dir_status != svn_wc__db_status_obstructed_delete)
    {
      SVN_ERR(mark_item_copied(db, dir_abspath, svn_wc__db_kind_dir,
                               iterpool));
    }

  /* Destroy our per-iteration pool. */
  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_add4(svn_wc_context_t *wc_ctx,
            const char *local_abspath,
            svn_depth_t depth,
            const char *copyfrom_url,
            svn_revnum_t copyfrom_rev,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            svn_wc_notify_func2_t notify_func,
            void *notify_baton,
            apr_pool_t *scratch_pool)
{
  const char *parent_abspath;
  const char *base_name;
  const char *parent_repos_relpath;
  const char *repos_root_url, *repos_uuid;
  svn_boolean_t is_replace = FALSE;
  svn_node_kind_t kind;
  int modify_flags;
  svn_wc__db_t *db = wc_ctx->db;
  svn_error_t *err;
  svn_wc__db_status_t status;
  svn_wc__db_kind_t db_kind;
  svn_boolean_t exists;
  apr_hash_t *props;

  svn_dirent_split(local_abspath, &parent_abspath, &base_name, scratch_pool);
  if (svn_wc_is_adm_dir(base_name, scratch_pool))
    return svn_error_createf
      (SVN_ERR_ENTRY_FORBIDDEN, NULL,
       _("Can't create an entry with a reserved name while trying to add '%s'"),
       svn_dirent_local_style(local_abspath, scratch_pool));

  SVN_ERR(svn_path_check_valid(local_abspath, scratch_pool));

  /* Make sure something's there. */
  SVN_ERR(svn_io_check_path(local_abspath, &kind, scratch_pool));
  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("'%s' not found"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));
  if (kind == svn_node_unknown)
    return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                             _("Unsupported node kind for path '%s'"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  /* Get the original entry for this path if one exists (perhaps
     this is actually a replacement of a previously deleted thing).

     Note that this is one of the few functions that is allowed to see
     'deleted' entries;  it's totally fine to have an entry that is
     scheduled for addition and still previously 'deleted'.  */

  err = svn_wc__db_read_info(&status, &db_kind, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL,
                             db, local_abspath, scratch_pool, scratch_pool);

  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);
      exists = FALSE;
    }
  else
    {
      svn_wc__db_kind_t on_disk;

      SVN_ERR(err);
      exists = TRUE;

      on_disk = ((kind == svn_node_dir)
                   ? svn_wc__db_kind_dir
                   : svn_wc__db_kind_file);

      switch (status)
        {
          case svn_wc__db_status_not_present:
            exists = TRUE; /* ### Make FALSE once we use SHA1 based pristine */
            break; /* Already gone */
          case svn_wc__db_status_deleted:
          case svn_wc__db_status_obstructed_delete:
            exists = TRUE; /* ### Make FALSE once we use SHA1 based pristine */
            is_replace = TRUE;
            break; /* Safe to add */

          default:
            if (copyfrom_url != NULL)
              break; /* Just register as copied */
            /* else fall through */

          case svn_wc__db_status_excluded:
          case svn_wc__db_status_absent:
            return svn_error_createf(
                             SVN_ERR_ENTRY_EXISTS, NULL,
                             _("'%s' is already under version control"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));
        }

      /* ### Remove this check once we are fully switched to one wcroot db */
      if (exists && db_kind != on_disk)
        {
          /* ### todo: At some point, we obviously don't want to block
             replacements where the node kind changes.  When this
             happens, svn_wc_revert3() needs to learn how to revert
             this situation.  At present we are using a specific node-change
             error so that clients can detect it. */
          return svn_error_createf
            (SVN_ERR_WC_NODE_KIND_CHANGE, NULL,
             _("Can't replace '%s' with a node of a differing type; "
               "the deletion must be committed and the parent updated "
               "before adding '%s'"),
             svn_dirent_local_style(local_abspath, scratch_pool),
             svn_dirent_local_style(local_abspath, scratch_pool));
        }
    }

  {
    svn_wc__db_status_t parent_status;
    svn_wc__db_kind_t parent_kind;

    err = svn_wc__db_read_info(&parent_status, &parent_kind, NULL,
                               &parent_repos_relpath, &repos_root_url,
                               &repos_uuid, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
                               db, parent_abspath, scratch_pool, scratch_pool);

    if (err
        || parent_status == svn_wc__db_status_not_present
        || parent_status == svn_wc__db_status_excluded
        || parent_status == svn_wc__db_status_absent
        || parent_status == svn_wc__db_status_obstructed
        || parent_status == svn_wc__db_status_obstructed_add)
      {
        return
          svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, err,
                            _("Can't find parent directory's entry while"
                              " trying to add '%s'"),
                            svn_dirent_local_style(local_abspath,
                                                   scratch_pool));
      }
    else if (parent_status == svn_wc__db_status_deleted
             || parent_status == svn_wc__db_status_obstructed_delete)
      {
        return
          svn_error_createf(SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
                            _("Can't add '%s' to a parent directory"
                              " scheduled for deletion"),
                            svn_dirent_local_style(local_abspath,
                                                   scratch_pool));
      }
    else if (parent_kind != svn_wc__db_kind_dir)
      /* Can't happen until single db; but then it causes serious
         trouble if we allow this. */
      return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                               _("Can't schedule an addition of '%s'"
                                 " below a not-directory node"),
                               svn_dirent_local_style(local_abspath,
                                                   scratch_pool));

    if (!repos_root_url)
      {
        if (parent_status == svn_wc__db_status_added)
          SVN_ERR(svn_wc__db_scan_addition(NULL, NULL, &parent_repos_relpath,
                                           &repos_root_url, &repos_uuid, NULL,
                                           NULL, NULL, NULL,
                                           db, parent_abspath,
                                           scratch_pool, scratch_pool));
        else
          SVN_ERR(svn_wc__db_scan_base_repos(&parent_repos_relpath,
                                             &repos_root_url, &repos_uuid,
                                             db, parent_abspath,
                                             scratch_pool, scratch_pool));
      }
  }

  {
    svn_wc_entry_t tmp_entry;

    /* Init the modify flags. */
    tmp_entry.schedule = svn_wc_schedule_add;
    tmp_entry.kind = kind;
    modify_flags = SVN_WC__ENTRY_MODIFY_SCHEDULE | SVN_WC__ENTRY_MODIFY_KIND;

    /* If a copy ancestor was given, make sure the copyfrom URL is in the same
       repository (if possible) and put the proper ancestry info in the new
       entry */
    if (copyfrom_url)
      {
        if (repos_root_url
            && ! svn_uri_is_ancestor(repos_root_url, copyfrom_url))
          return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                   _("The URL '%s' has a different repository "
                                     "root than its parent"), copyfrom_url);
        tmp_entry.copyfrom_url = copyfrom_url;
        tmp_entry.copyfrom_rev = copyfrom_rev;
        tmp_entry.copied = TRUE;
        modify_flags |= SVN_WC__ENTRY_MODIFY_COPYFROM_URL;
        modify_flags |= SVN_WC__ENTRY_MODIFY_COPYFROM_REV;
        modify_flags |= SVN_WC__ENTRY_MODIFY_COPIED;
      }

    /* If this is a replacement we want to remove the checksum and the property
       flags so they are not set to their respective old values. */
    if (is_replace)
      {
        tmp_entry.checksum = NULL;
        modify_flags |= SVN_WC__ENTRY_MODIFY_CHECKSUM;
      }


    /* Store the pristine properties to install them on working, because
       we might delete the base table */
    if ((exists && status != svn_wc__db_status_not_present)
        && !is_replace && copyfrom_url != NULL)
      {
        /* NOTE: the conditions to reach here *exactly* match the
           conditions used below when PROPS is referenced.
           Be careful to keep these sets of conditionals aligned to avoid
           an uninitialized PROPS value.  */
        SVN_ERR(svn_wc__db_read_pristine_props(&props, db, local_abspath,
                                               scratch_pool, scratch_pool));
      }

    if (modify_flags)
      {
        if (kind == svn_node_dir)
          SVN_ERR(svn_wc__entry_modify_stub(db, local_abspath,
                                            &tmp_entry, modify_flags,
                                            scratch_pool));
        else
          SVN_ERR(svn_wc__entry_modify(db, local_abspath, kind,
                                       &tmp_entry, modify_flags,
                                       scratch_pool));
      }

#if (SVN_WC__VERSION < SVN_WC__PROPS_IN_DB)
    /* If this is a replacement without history, we need to reset the
       properties for PATH. */
    /* ### this is totally bogus. we clear these cuz turds might have been
       ### left around. thankfully, this will be properly managed during the
       ### wc-ng upgrade process. for now, we try to compensate... */
    if (((exists && status != svn_wc__db_status_not_present) || is_replace)
        && copyfrom_url == NULL)
      SVN_ERR(svn_wc__props_delete(db, local_abspath, svn_wc__props_working,
                                   scratch_pool));
#endif

  if (is_replace)
    {
      /* We don't want the old base text (if any) and base props to be
         mistakenly used as the bases for the new, replacement object.
         So, move them out of the way. */

      /* ### TODO: In an ideal world, this whole function would be loggy.
       * ### But the directory recursion code below is already tangled
       * ### enough, and re-doing the code above would require setting
       * ### up more of tmp_entry.  It's more than a SMOP.  For now,
       * ### I'm leaving it be, though we set up the revert base(s)
       * ### loggily because that's Just How It's Done.
       */
      SVN_ERR(svn_wc__wq_prepare_revert_files(db, local_abspath,
                                              scratch_pool));
      SVN_ERR(svn_wc__wq_run(db, local_abspath,
                             cancel_func, cancel_baton, scratch_pool));
    }

  if (kind == svn_node_dir) /* scheduling a directory for addition */
    {
      if (! copyfrom_url)
        {
          const char *new_url;

          /* Derive the parent path for our new addition here. */
          new_url = svn_path_url_add_component2(repos_root_url, base_name,
                                                scratch_pool);

          /* Make sure this new directory has an admistrative subdirectory
             created inside of it.

             This creates a BASE_NODE for an added directory, really
             it should create a WORKING_NODE.  It gets removed by the
             next modify2 call. */
          SVN_ERR(svn_wc__internal_ensure_adm(db, local_abspath,
                                              new_url, repos_root_url,
                                              repos_uuid, 0,
                                              depth, scratch_pool));
        }
      else
        {
          /* When we are called with the copyfrom arguments set and with
             the admin directory already in existence, then the dir will
             contain the copyfrom settings.  So we need to pass the
             copyfrom arguments to the ensure call. */
          SVN_ERR(svn_wc__internal_ensure_adm(db, local_abspath,
                                              copyfrom_url,
                                              repos_root_url,
                                              repos_uuid, copyfrom_rev,
                                              depth, scratch_pool));
        }

      /* ### This block can be removed after we centralise the db and have
         ### infinite depth admin locks. */
      if (! exists)
        {
          /* Lock on parent needs to be propogated into the child db. */
          SVN_ERR(svn_wc__db_wclock_set(db, local_abspath, 0, scratch_pool));
          SVN_ERR(svn_wc__db_temp_mark_locked(db, local_abspath, scratch_pool));
        }

      /* We're making the same mods we made above, but this time we'll
         force the scheduling.  Also make sure to undo the
         'incomplete' flag which svn_wc__internal_ensure_adm() sets by
         default.

         This deletes the erroneous BASE_NODE for added directories and
         adds a WORKING_NODE. */
      if (modify_flags)
        {
          modify_flags |= SVN_WC__ENTRY_MODIFY_FORCE;
          tmp_entry.schedule = (is_replace
                                ? svn_wc_schedule_replace
                                : svn_wc_schedule_add);
          SVN_ERR(svn_wc__entry_modify(db, local_abspath, svn_node_dir,
                                       &tmp_entry, modify_flags,
                                       scratch_pool));
        }

      SVN_ERR(svn_wc__db_temp_op_set_working_incomplete(db, local_abspath,
                                                        FALSE, scratch_pool));

      if (copyfrom_url)
        {
          /* If this new directory has ancestry, it's not enough to
             schedule it for addition with copyfrom args.  We also
             need to rewrite its ancestor-url, and rewrite the
             ancestor-url of ALL its children!

             We're doing this because our current commit model (for
             hysterical raisins, presumably) assumes an entry's URL is
             correct before commit -- i.e. the URL is not tweaked in
             the post-commit bumping process.  We might want to change
             this model someday. */

          /* ### copy.c will copy .svn subdirs, which means the whole
             ### subtree is already "versioned". we now need to rejigger
             ### the metadata to make it Proper for this location.  */

          /* Recursively add the 'copied' existence flag as well!  */

          SVN_ERR(mark_tree_copied(db, local_abspath,
                                   exists ? status : svn_wc__db_status_added,
                                   svn_relpath_join(parent_repos_relpath,
                                                    base_name, scratch_pool),
                                   repos_root_url,
                                   repos_uuid,
                                   scratch_pool));

          /* Clean out the now-obsolete dav cache values.  */
          /* ### put this into above walk. clear all cached values.  */
          SVN_ERR(svn_wc__db_base_set_dav_cache(db, local_abspath, NULL,
                                                scratch_pool));
        }
    }
  }

  /* Set the pristine properties in WORKING_NODE, by copying them from the
     deleted BASE_NODE record. Or set them to empty to make sure we don't
     inherit wrong properties from BASE */
  if (exists && status != svn_wc__db_status_not_present)
    {
      if (!is_replace && copyfrom_url != NULL)
        {
          /* NOTE: the conditions to reach here *exactly* match the
             conditions that were used to initialize the PROPS localvar.
             Be careful to keep these sets of conditionals aligned to avoid
             an uninitialized PROPS value.  */
          SVN_ERR(svn_wc__db_temp_working_set_props(db, local_abspath, props,
                                                    scratch_pool));
        }
      else
        SVN_ERR(svn_wc__db_temp_working_set_props(db, local_abspath,
                                                  apr_hash_make(scratch_pool),
                                                  scratch_pool));
    }

  /* Report the addition to the caller. */
  if (notify_func != NULL)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(local_abspath,
                                                     svn_wc_notify_add,
                                                     scratch_pool);
      notify->kind = kind;
      (*notify_func)(notify_baton, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_register_file_external(svn_wc_context_t *wc_ctx,
                              const char *local_abspath,
                              const char *external_url,
                              const svn_opt_revision_t *external_peg_rev,
                              const svn_opt_revision_t *external_rev,
                              apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = wc_ctx->db;
  const char *parent_abspath, *base_name, *repos_root_url;

  svn_dirent_split(local_abspath, &parent_abspath, &base_name, scratch_pool);

  SVN_ERR(svn_wc_add4(wc_ctx, local_abspath, svn_depth_infinity,
                      NULL, SVN_INVALID_REVNUM, NULL, NULL, NULL, NULL,
                      scratch_pool));
  SVN_ERR(svn_wc__db_scan_base_repos(NULL, &repos_root_url, NULL, db,
                                     parent_abspath, scratch_pool,
                                     scratch_pool));
  SVN_ERR(svn_wc__set_file_external_location(wc_ctx, local_abspath,
                                             external_url, external_peg_rev,
                                             external_rev, repos_root_url,
                                             scratch_pool));
  return SVN_NO_ERROR;
}


/* Thoughts on Reversion.

    What does is mean to revert a given PATH in a tree?  We'll
    consider things by their modifications.

    Adds

    - For files, svn_wc_remove_from_revision_control(), baby.

    - Added directories may contain nothing but added children, and
      reverting the addition of a directory necessarily means reverting
      the addition of all the directory's children.  Again,
      svn_wc_remove_from_revision_control() should do the trick.

    Deletes

    - Restore properties to their unmodified state.

    - For files, restore the pristine contents, and reset the schedule
      to 'normal'.

    - For directories, reset the schedule to 'normal'.  All children
      of a directory marked for deletion must also be marked for
      deletion, but it's okay for those children to remain deleted even
      if their parent directory is restored.  That's what the
      recursive flag is for.

    Replaces

    - Restore properties to their unmodified state.

    - For files, restore the pristine contents, and reset the schedule
      to 'normal'.

    - For directories, reset the schedule to normal.  A replaced
      directory can have deleted children (left over from the initial
      deletion), replaced children (children of the initial deletion
      now re-added), and added children (new entries under the
      replaced directory).  Since this is technically an addition, it
      necessitates recursion.

    Modifications

    - Restore properties and, for files, contents to their unmodified
      state.

*/


/* */
static svn_error_t *
revert_admin_things(svn_boolean_t *reverted,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    svn_boolean_t use_commit_times,
                    apr_pool_t *pool)
{
  SVN_ERR(svn_wc__wq_add_revert(reverted, db, local_abspath, use_commit_times,
                                pool));
  SVN_ERR(svn_wc__wq_run(db, local_abspath, NULL, NULL, pool));

  return SVN_NO_ERROR;
}


/* Revert LOCAL_ABSPATH in DB, where the on-disk node kind is DISK_KIND.
   *DEPTH is the depth of the reversion crawl the caller is
   using; this function may choose to override that value as needed.

   See svn_wc_revert4() for the interpretations of
   USE_COMMIT_TIMES, CANCEL_FUNC and CANCEL_BATON.

   Set *DID_REVERT to true if actually reverting anything, else do not
   touch *DID_REVERT.

   Use POOL for allocations.
 */
static svn_error_t *
revert_entry(svn_depth_t *depth,
             svn_wc__db_t *db,
             const char *local_abspath,
             svn_node_kind_t disk_kind,
             svn_boolean_t use_commit_times,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             svn_boolean_t *did_revert,
             apr_pool_t *pool)
{
  svn_wc__db_status_t status;
  svn_wc__db_kind_t kind;
  svn_boolean_t replaced;

  /* Initialize this even though revert_admin_things() is guaranteed
     to set it, because we don't know that revert_admin_things() will
     be called. */
  svn_boolean_t reverted = FALSE;

  SVN_ERR(svn_wc__db_read_info(&status, &kind,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL,
                               db, local_abspath, pool, pool));

  SVN_ERR(svn_wc__internal_is_replaced(&replaced, db, local_abspath, pool));

  /* Additions. */
  if ((status == svn_wc__db_status_added
       || status == svn_wc__db_status_obstructed_add)
      && !replaced)
    {
      svn_revnum_t base_revision;
      const char *repos_relpath;
      const char *repos_root_url;
      const char *repos_uuid;
      /* Before removing item from revision control, notice if the
         entry is in a 'deleted' state; this is critical for
         directories, where this state only exists in its parent's
         entry. */
      svn_boolean_t was_deleted = FALSE;

      /* NOTE: if WAS_DELETED gets set, then we have BASE nodes. The code
         below will then figure out the repository information, so that
         we can later insert a node for the same repository.  */

      /* ### much of this is probably bullshit. we should be able to just
         ### remove the WORKING and ACTUAL rows, and be done. but we're
         ### not quite there yet, so nodes get fully removed and then
         ### shoved back into the database. this is why we need to record
         ### the repository information, and the BASE revision.  */

      if (kind == svn_wc__db_kind_file)
        {
          was_deleted = (status == svn_wc__db_status_not_present);
          if (was_deleted)
            {
              /* Remember the BASE revision.  */
              SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, &base_revision,
                                               NULL, NULL, NULL, NULL, NULL,
                                               NULL, NULL, NULL, NULL,
                                               NULL, NULL, NULL,
                                               db, local_abspath,
                                               pool, pool));

              /* Remember the repository this node is associated with.  */
              SVN_ERR(svn_wc__db_scan_base_repos(&repos_relpath,
                                                 &repos_root_url,
                                                 &repos_uuid,
                                                 db, local_abspath,
                                                 pool, pool));
            }

          SVN_ERR(svn_wc__internal_remove_from_revision_control(db,
                                                                local_abspath,
                                                                FALSE, FALSE,
                                                                cancel_func,
                                                                cancel_baton,
                                                                pool));
        }
      else if (kind == svn_wc__db_kind_dir)
        {
          /* We don't need to check for excluded item, since we won't fall
             into this code path in that case. */

          SVN_ERR(svn_wc__node_is_deleted(&was_deleted, db, local_abspath,
                                          pool));
          if (was_deleted)
            {
              /* Remember the BASE revision.  */
              SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, &base_revision,
                                               NULL, NULL, NULL, NULL, NULL,
                                               NULL, NULL, NULL, NULL,
                                               NULL, NULL, NULL,
                                               db, local_abspath,
                                               pool, pool));

              /* Remember the repository this node is associated with.  */
              SVN_ERR(svn_wc__db_scan_base_repos(&repos_relpath,
                                                 &repos_root_url,
                                                 &repos_uuid,
                                                 db, local_abspath,
                                                 pool, pool));
            }

          if (disk_kind == svn_node_none
              || svn_wc__adm_missing(db, local_abspath, pool))
            {
              /* Schedule add but missing, just remove the entry
                 or it's missing an adm area in which case
                 svn_wc_adm_probe_retrieve() returned the parent's
                 adm_access, for which we definitely can't use the 'else'
                 code path (as it will remove the parent from version
                 control... (See issue 2425) */
              SVN_ERR(svn_wc__db_temp_op_remove_entry(db, local_abspath,
                                                      pool));
            }
          else
            {
              SVN_ERR(svn_wc__internal_remove_from_revision_control(
                                           db,
                                           local_abspath,
                                           FALSE, FALSE,
                                           cancel_func, cancel_baton,
                                           pool));
            }
        }
      else  /* Else it's `none', or something exotic like a symlink... */
        {
          return svn_error_createf(SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                                   _("Unknown or unexpected kind for path "
                                     "'%s'"),
                                   svn_dirent_local_style(local_abspath,
                                                          pool));

        }

      /* Recursivity is taken care of by svn_wc_remove_from_revision_control,
         and we've definitely reverted PATH at this point. */
      *depth = svn_depth_empty;
      reverted = TRUE;

      /* If the removed item was *also* in a 'deleted' state, make
         sure we leave just a plain old 'deleted' entry behind in the
         parent. */
      if (was_deleted)
        {
          SVN_ERR(svn_wc__db_base_add_absent_node(
                    db, local_abspath,
                    repos_relpath, repos_root_url, repos_uuid,
                    base_revision,
                    kind,
                    svn_wc__db_status_not_present,
                    NULL, NULL,
                    pool));
        }
    }
  /* Regular prop and text edit. */
  /* Deletions and replacements. */
  else if (status == svn_wc__db_status_normal
           || status == svn_wc__db_status_deleted
           || replaced)
    {
      /* Revert the prop and text mods (if any). */
      SVN_ERR(revert_admin_things(&reverted, db, local_abspath,
                                  use_commit_times, pool));

      /* Force recursion on replaced directories. */
      if (kind == svn_wc__db_kind_dir && replaced)
        *depth = svn_depth_infinity;
    }

  /* If PATH was reverted, tell our client that. */
  if (reverted)
    *did_revert = TRUE;

  return SVN_NO_ERROR;
}


/* This is just the guts of svn_wc_revert4() save that it accepts a
   hash CHANGELIST_HASH whose keys are changelist names instead of an
   array of said names.  See svn_wc_revert4() for additional
   documentation. */
static svn_error_t *
revert_internal(svn_wc__db_t *db,
                const char *local_abspath,
                svn_depth_t depth,
                svn_boolean_t use_commit_times,
                apr_hash_t *changelist_hash,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                svn_wc_notify_func2_t notify_func,
                void *notify_baton,
                apr_pool_t *pool)
{
  svn_node_kind_t disk_kind;
  svn_wc__db_status_t status;
  svn_wc__db_kind_t db_kind;
  svn_boolean_t unversioned;
  const svn_wc_conflict_description2_t *tree_conflict;
  svn_error_t *err;

  /* Check cancellation here, so recursive calls get checked early. */
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));


  /* Safeguard 1: the item must be versioned for any reversion to make sense,
     except that a tree conflict can exist on an unversioned item. */
  err = svn_wc__db_read_info(&status, &db_kind,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL,
                             db, local_abspath, pool, pool);

  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);
      unversioned = TRUE;
    }
  else if (err)
    return svn_error_return(err);
  else
    unversioned = FALSE;

  SVN_ERR(svn_wc__db_op_read_tree_conflict(&tree_conflict, db, local_abspath,
                                           pool, pool));
  if (unversioned && tree_conflict == NULL)
    return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                             _("Cannot revert unversioned item '%s'"),
                             svn_dirent_local_style(local_abspath, pool));

  /* Safeguard 1.5:  is this a missing versioned directory? */
  SVN_ERR(svn_io_check_path(local_abspath, &disk_kind, pool));
  if (!unversioned && (db_kind == svn_wc__db_kind_dir))
    {
      if ((disk_kind != svn_node_dir)
          && (status != svn_wc__db_status_added)
          && (status != svn_wc__db_status_obstructed_add))
        {
          /* When the directory itself is missing, we can't revert without
             hitting the network.  Someday a '--force' option will
             make this happen.  For now, send notification of the failure. */
          if (notify_func != NULL)
            {
              svn_wc_notify_t *notify =
                svn_wc_create_notify(local_abspath,
                                     svn_wc_notify_failed_revert,
                                     pool);
              notify_func(notify_baton, notify, pool);
            }
          return SVN_NO_ERROR;
        }
    }

  /* Safeguard 2:  can we handle this entry's recorded kind? */
  if (!unversioned
      && (db_kind != svn_wc__db_kind_file) && (db_kind != svn_wc__db_kind_dir))
    return svn_error_createf
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("Cannot revert '%s': unsupported entry node kind"),
       svn_dirent_local_style(local_abspath, pool));

  /* Safeguard 3:  can we deal with the node kind of PATH currently in
     the working copy? */
  if ((disk_kind != svn_node_none)
      && (disk_kind != svn_node_file)
      && (disk_kind != svn_node_dir))
    return svn_error_createf
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("Cannot revert '%s': unsupported node kind in working copy"),
       svn_dirent_local_style(local_abspath, pool));

  /* If the entry passes changelist filtering, revert it!  */
  if (svn_wc__internal_changelist_match(db, local_abspath, changelist_hash,
                                        pool))
    {
      svn_boolean_t reverted = FALSE;
      const svn_wc_conflict_description2_t *conflict;

      /* Clear any tree conflict on the path, even if it is not a versioned
         resource. */
      SVN_ERR(svn_wc__db_op_read_tree_conflict(&conflict, db, local_abspath,
                                               pool, pool));
      if (conflict)
        {
          SVN_ERR(svn_wc__db_op_set_tree_conflict(db, local_abspath, NULL,
                                                  pool));
          reverted = TRUE;
        }

      /* Actually revert this entry.  If this is a working copy root,
         we provide a base_name from the parent path. */
      if (!unversioned)
        SVN_ERR(revert_entry(&depth, db, local_abspath, disk_kind,
                             use_commit_times,
                             cancel_func, cancel_baton,
                             &reverted, pool));

      /* Notify */
      if (notify_func && reverted)
        (*notify_func)(notify_baton,
                       svn_wc_create_notify(local_abspath,
                                            svn_wc_notify_revert, pool),
                       pool);
    }

  /* Finally, recurse if requested. */
  if (!unversioned && db_kind == svn_wc__db_kind_dir && depth > svn_depth_empty)
    {
      const apr_array_header_t *children;
      apr_hash_t *nodes = apr_hash_make(pool);
      svn_depth_t depth_under_here = depth;
      int i;
      apr_pool_t *iterpool = svn_pool_create(pool);

      if (depth == svn_depth_files || depth == svn_depth_immediates)
        depth_under_here = svn_depth_empty;

      SVN_ERR(svn_wc__db_read_children(&children, db, local_abspath, pool,
                                       iterpool));

      for (i = 0; i < children->nelts; i++)
        {
          const char *name = APR_ARRAY_IDX(children, i, const char *);
          const char *node_abspath;
          svn_boolean_t hidden;
          svn_wc__db_kind_t child_db_kind;

          svn_pool_clear(iterpool);

          node_abspath = svn_dirent_join(local_abspath, name, iterpool);

          SVN_ERR(svn_wc__db_node_hidden(&hidden, db, node_abspath, iterpool));

          if (hidden)
            continue;

          apr_hash_set(nodes, name, APR_HASH_KEY_STRING, name);

          SVN_ERR(svn_wc__db_read_kind(&child_db_kind, db, node_abspath, FALSE,
                                       iterpool));

          /* Skip subdirectories if we're called with depth-files. */
          if ((depth == svn_depth_files) &&
              (child_db_kind != svn_wc__db_kind_file) &&
              (child_db_kind != svn_wc__db_kind_symlink))
            continue;

          /* Revert the entry. */
          SVN_ERR(revert_internal(db, node_abspath,
                                  depth_under_here, use_commit_times,
                                  changelist_hash, cancel_func, cancel_baton,
                                  notify_func, notify_baton, iterpool));
        }

      /* Visit any unversioned children that are tree conflict victims. */
      {
        const apr_array_header_t *conflict_victims;

        /* Loop through all the tree conflict victims */
        SVN_ERR(svn_wc__db_read_conflict_victims(&conflict_victims,
                                                 db, local_abspath,
                                                 pool, pool));

        for (i = 0; i < conflict_victims->nelts; ++i)
          {
            int j;
            const apr_array_header_t *child_conflicts;
            const char *child_name;
            const char *child_abspath;

            svn_pool_clear(iterpool);

            child_name = APR_ARRAY_IDX(conflict_victims, i, const char *);

            /* Skip if in this dir's entries, we only want unversioned */
            if (apr_hash_get(nodes, child_name, APR_HASH_KEY_STRING))
              continue;

            child_abspath = svn_dirent_join(local_abspath, child_name,
                                            iterpool);

            SVN_ERR(svn_wc__db_read_conflicts(&child_conflicts,
                                              db, child_abspath,
                                              iterpool, iterpool));

            for (j = 0; j < child_conflicts->nelts; ++j)
              {
                const svn_wc_conflict_description2_t *conflict =
                  APR_ARRAY_IDX(child_conflicts, j,
                                const svn_wc_conflict_description2_t *);

                if (conflict->kind == svn_wc_conflict_kind_tree)
                  SVN_ERR(revert_internal(db, conflict->local_abspath,
                                          svn_depth_empty,
                                          use_commit_times, changelist_hash,
                                          cancel_func, cancel_baton,
                                          notify_func, notify_baton,
                                          iterpool));
              }
          }
      }

      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_revert4(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_depth_t depth,
               svn_boolean_t use_commit_times,
               const apr_array_header_t *changelists,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *pool)
{
  apr_hash_t *changelist_hash = NULL;

  if (changelists && changelists->nelts)
    SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelists, pool));

  return svn_error_return(revert_internal(wc_ctx->db,
                                          local_abspath, depth,
                                          use_commit_times, changelist_hash,
                                          cancel_func, cancel_baton,
                                          notify_func, notify_baton,
                                          pool));
}


svn_error_t *
svn_wc_get_pristine_copy_path(const char *path,
                              const char **pristine_path,
                              apr_pool_t *pool)
{
  svn_wc__db_t *db;
  const char *local_abspath;
  svn_error_t *err;

  SVN_ERR(svn_wc__db_open(&db, svn_wc__db_openmode_readonly, NULL,
                          TRUE, TRUE, pool, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  err = svn_wc__text_base_path_to_read(pristine_path, db, local_abspath,
                                       pool, pool);
  if (err && err->apr_err == SVN_ERR_WC_PATH_UNEXPECTED_STATUS)
    {
      const char *adm_abspath = svn_dirent_dirname(local_abspath, pool);

      svn_error_clear(err);
      *pristine_path = svn_wc__nonexistent_path(db, adm_abspath, pool);
      return SVN_NO_ERROR;
    }
   SVN_ERR(err);

  return svn_error_return(svn_wc__db_close(db));
}

svn_error_t *
svn_wc_get_pristine_contents2(svn_stream_t **contents,
                              svn_wc_context_t *wc_ctx,
                              const char *local_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  return svn_error_return(svn_wc__get_pristine_contents(contents,
                                                        wc_ctx->db,
                                                        local_abspath,
                                                        result_pool,
                                                        scratch_pool));
}

svn_error_t *
svn_wc__internal_remove_from_revision_control(svn_wc__db_t *db,
                                              const char *local_abspath,
                                              svn_boolean_t destroy_wf,
                                              svn_boolean_t instant_error,
                                              svn_cancel_func_t cancel_func,
                                              void *cancel_baton,
                                              apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_boolean_t left_something = FALSE;
  svn_wc__db_kind_t kind;

  /* ### This whole function should be rewritten to run inside a transaction,
     ### to allow a stable cancel behavior.
     ###
     ### Subversion < 1.7 marked the directory as incomplete to allow updating
     ### it from a canceled state. But this would not work because update
     ### doesn't retrieve deleted items.
     ###
     ### WC-NG doesn't support a delete+incomplete state, but we can't build
     ### transactions over multiple databases yet. */

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Check cancellation here, so recursive calls get checked early. */
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, FALSE, scratch_pool));

  if (kind == svn_wc__db_kind_file || kind == svn_wc__db_kind_symlink)
    {
      svn_node_kind_t on_disk;
      svn_boolean_t wc_special, local_special;
      svn_boolean_t text_modified_p;
#ifdef SVN_EXPERIMENTAL_PRISTINE
      const svn_checksum_t *base_sha1_checksum, *working_sha1_checksum;
#else
      const char *text_base_file;
#endif

      /* Only check if the file was modified when it wasn't overwritten with a
         special file */
      SVN_ERR(svn_wc__get_special(&wc_special, db, local_abspath,
                                  scratch_pool));
      SVN_ERR(svn_io_check_special_path(local_abspath, &on_disk,
                                        &local_special, scratch_pool));
      if (wc_special || ! local_special)
        {
          /* Check for local mods. before removing entry */
          SVN_ERR(svn_wc__internal_text_modified_p(&text_modified_p, db,
                                                   local_abspath, FALSE,
                                                   TRUE, scratch_pool));
          if (text_modified_p && instant_error)
            return svn_error_createf(SVN_ERR_WC_LEFT_LOCAL_MOD, NULL,
                   _("File '%s' has local modifications"),
                   svn_dirent_local_style(local_abspath, scratch_pool));
        }

#ifdef SVN_EXPERIMENTAL_PRISTINE
      /* Find the checksum(s) of the node's one or two pristine texts.  Note
         that read_info() may give us the one from BASE_NODE again. */
      err = svn_wc__db_base_get_info(NULL, NULL, NULL, NULL, NULL, NULL,
                                     NULL, NULL, NULL, NULL, NULL,
                                     &base_sha1_checksum,
                                     NULL, NULL, NULL,
                                     db, local_abspath,
                                     scratch_pool, scratch_pool);
      if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          svn_error_clear(err);
          base_sha1_checksum = NULL;
        }
      else
        SVN_ERR(err);
      err = svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL,
                                 &working_sha1_checksum,
                                 NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL,
                                 db, local_abspath,
                                 scratch_pool, scratch_pool);
      if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          svn_error_clear(err);
          working_sha1_checksum = NULL;
        }
      else
        SVN_ERR(err);
#else
      SVN_ERR(svn_wc__text_base_path(&text_base_file, db, local_abspath,
                                     scratch_pool));
#endif

#if (SVN_WC__VERSION < SVN_WC__PROPS_IN_DB)
      /* Remove prop/NAME, prop-base/NAME.svn-base. */
      SVN_ERR(svn_wc__props_delete(db, local_abspath, svn_wc__props_working,
                                   scratch_pool));
      SVN_ERR(svn_wc__props_delete(db, local_abspath, svn_wc__props_base,
                                   scratch_pool));
#endif

      /* Remove NAME from PATH's entries file: */
      SVN_ERR(svn_wc__db_temp_op_remove_entry(db, local_abspath,
                                              scratch_pool));

      /* Remove text-base/NAME.svn-base */
#ifdef SVN_EXPERIMENTAL_PRISTINE
      /* Having removed the checksums that reference the pristine texts,
         remove the pristine texts (if now totally unreferenced) from the
         pristine store.  Don't try to remove the same pristine text twice.
         The two checksums might be the same, either because the copied base
         was exactly the same as the replaced base, or just because the
         ..._read_info() code above sets WORKING_SHA1_CHECKSUM to the base
         checksum if there is no WORKING_NODE row. */
      if (base_sha1_checksum)
        SVN_ERR(svn_wc__db_pristine_remove(db, local_abspath,
                                           base_sha1_checksum,
                                           scratch_pool));
      if (working_sha1_checksum
          && ! svn_checksum_match(base_sha1_checksum, working_sha1_checksum))
        SVN_ERR(svn_wc__db_pristine_remove(db, local_abspath,
                                           working_sha1_checksum,
                                           scratch_pool));
#else
      SVN_ERR(svn_io_remove_file2(text_base_file, TRUE, scratch_pool));
#endif

      /* If we were asked to destroy the working file, do so unless
         it has local mods. */
      if (destroy_wf)
        {
          /* Don't kill local mods. */
          if ((! wc_special && local_special) || text_modified_p)
            return svn_error_create(SVN_ERR_WC_LEFT_LOCAL_MOD, NULL, NULL);
          else  /* The working file is still present; remove it. */
            SVN_ERR(svn_io_remove_file2(local_abspath, TRUE, scratch_pool));
        }

    }  /* done with file case */
  else if (svn_wc__adm_missing(db, local_abspath, scratch_pool))
    {
      /* The directory is missing  so don't try to recurse,
         just delete the entry in the parent directory.

         ### This case disappears after we move to one DB. */
      SVN_ERR(svn_wc__db_temp_op_remove_entry(db, local_abspath,
                                              scratch_pool));
    }
  else /* looking at THIS_DIR */
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      const apr_array_header_t *children;
      int i;

      /* ### sanity check:  check 2 places for DELETED flag? */

      /* Walk over every entry. */
      SVN_ERR(svn_wc__db_read_children(&children, db, local_abspath,
                                       scratch_pool, iterpool));

      for (i = 0; i < children->nelts; i++)
        {
          const char *entry_name = APR_ARRAY_IDX(children, i, const char*);
          const char *entry_abspath;
          svn_boolean_t hidden;

          svn_pool_clear(iterpool);

          entry_abspath = svn_dirent_join(local_abspath, entry_name, iterpool);

          /* ### where did the adm_missing and depth_exclude test go?!?  */

          SVN_ERR(svn_wc__db_node_hidden(&hidden, db, entry_abspath,
                                         iterpool));
          if (hidden)
            {
              SVN_ERR(svn_wc__db_temp_op_remove_entry(db, entry_abspath,
                                                      iterpool));
              continue;
            }

          err = svn_wc__internal_remove_from_revision_control(
            db, entry_abspath,
            destroy_wf, instant_error,
            cancel_func, cancel_baton,
            iterpool);

          if (err && (err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD))
            {
              if (instant_error)
                return svn_error_return(err);
              else
                {
                  svn_error_clear(err);
                  left_something = TRUE;
                }
            }
          else if (err)
            return svn_error_return(err);
        }

      /* At this point, every directory below this one has been
         removed from revision control. */

      /* Remove self from parent's entries file, but only if parent is
         a working copy.  If it's not, that's fine, we just move on. */
      {
        svn_boolean_t is_root;

        SVN_ERR(svn_wc__check_wc_root(&is_root, NULL, NULL,
                                      db, local_abspath, iterpool));

        /* If full_path is not the top of a wc, then its parent
           directory is also a working copy and has an entry for
           full_path.  We need to remove that entry: */
        if (! is_root)
          {
            SVN_ERR(svn_wc__db_temp_op_remove_entry(db, local_abspath,
                                                    iterpool));
          }
      }

      /* Remove the entire administrative .svn area, thereby removing
         _this_ dir from revision control too.  */
      SVN_ERR(svn_wc__adm_destroy(db, local_abspath, iterpool));

      /* If caller wants us to recursively nuke everything on disk, go
         ahead, provided that there are no dangling local-mod files
         below */
      if (destroy_wf && (! left_something))
        {
          /* If the dir is *truly* empty (i.e. has no unversioned
             resources, all versioned files are gone, all .svn dirs are
             gone, and contains nothing but empty dirs), then a
             *non*-recursive dir_remove should work.  If it doesn't,
             no big deal.  Just assume there are unversioned items in
             there and set "left_something" */
          err = svn_io_dir_remove_nonrecursive(local_abspath, iterpool);
          if (err)
            {
              left_something = TRUE;
              svn_error_clear(err);
            }
        }

      svn_pool_destroy(iterpool);

    }  /* end of directory case */

  if (left_something)
    return svn_error_create(SVN_ERR_WC_LEFT_LOCAL_MOD, NULL, NULL);
  else
    return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_remove_from_revision_control2(svn_wc_context_t *wc_ctx,
                                    const char *local_abspath,
                                    svn_boolean_t destroy_wf,
                                    svn_boolean_t instant_error,
                                    svn_cancel_func_t cancel_func,
                                    void *cancel_baton,
                                    apr_pool_t *scratch_pool)
{
  return svn_error_return(
      svn_wc__internal_remove_from_revision_control(wc_ctx->db,
                                                    local_abspath,
                                                    destroy_wf,
                                                    instant_error,
                                                    cancel_func,
                                                    cancel_baton,
                                                    scratch_pool));
}

svn_error_t *
svn_wc_add_lock2(svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 const svn_lock_t *lock,
                 apr_pool_t *scratch_pool)
{
  svn_wc__db_lock_t db_lock;
  svn_error_t *err;
  const svn_string_t *needs_lock;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  db_lock.token = lock->token;
  db_lock.owner = lock->owner;
  db_lock.comment = lock->comment;
  db_lock.date = lock->creation_date;
  err = svn_wc__db_lock_add(wc_ctx->db, local_abspath, &db_lock, scratch_pool);
  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_return(err);

      /* Remap the error.  */
      svn_error_clear(err);
      return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                               _("'%s' is not under version control"),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }

  /* if svn:needs-lock is present, then make the file read-write. */
  SVN_ERR(svn_wc__internal_propget(&needs_lock, wc_ctx->db, local_abspath,
                                   SVN_PROP_NEEDS_LOCK, scratch_pool,
                                   scratch_pool));
  if (needs_lock)
    SVN_ERR(svn_io_set_file_read_write(local_abspath, FALSE, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_remove_lock2(svn_wc_context_t *wc_ctx,
                    const char *local_abspath,
                    apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  const svn_string_t *needs_lock;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  err = svn_wc__db_lock_remove(wc_ctx->db, local_abspath, scratch_pool);
  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_return(err);

      /* Remap the error.  */
      svn_error_clear(err);
      return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                               _("'%s' is not under version control"),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }

  /* if svn:needs-lock is present, then make the file read-only. */
  SVN_ERR(svn_wc__internal_propget(&needs_lock, wc_ctx->db, local_abspath,
                                   SVN_PROP_NEEDS_LOCK, scratch_pool,
                                   scratch_pool));
  if (needs_lock)
    SVN_ERR(svn_io_set_file_read_only(local_abspath, FALSE, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_set_changelist2(svn_wc_context_t *wc_ctx,
                       const char *local_abspath,
                       const char *changelist,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       svn_wc_notify_func2_t notify_func,
                       void *notify_baton,
                       apr_pool_t *scratch_pool)
{
  svn_wc_notify_t *notify;
  const char *existing_changelist;
  svn_wc__db_kind_t kind;

  /* Assert that we aren't being asked to set an empty changelist. */
  SVN_ERR_ASSERT(! (changelist && changelist[0] == '\0'));

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_read_info(NULL, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               &existing_changelist,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL,
                               wc_ctx->db, local_abspath, scratch_pool,
                               scratch_pool));

  /* We can't do changelists on directories. */
  if (kind == svn_wc__db_kind_dir)
    return svn_error_createf(SVN_ERR_CLIENT_IS_DIRECTORY, NULL,
                             _("'%s' is a directory, and thus cannot"
                               " be a member of a changelist"), local_abspath);

  /* If the path has no changelist and we're removing changelist, skip it.
     ### the db actually does this check, too, but for notification's sake,
     ### we add it here as well. */
  if (! (changelist || existing_changelist))
    return SVN_NO_ERROR;

  /* If the path is already assigned to the changelist we're
     trying to assign, skip it.
     ### the db actually does this check, too, but for notification's sake,
     ### we add it here as well. */
  if (existing_changelist
      && changelist
      && strcmp(existing_changelist, changelist) == 0)
    return SVN_NO_ERROR;

  /* Set the changelist. */
  SVN_ERR(svn_wc__db_op_set_changelist(wc_ctx->db, local_abspath, changelist,
                                       scratch_pool));

  /* And tell someone what we've done. */
  if (notify_func)
    {
      if (existing_changelist)
        {
          notify = svn_wc_create_notify(local_abspath,
                                        svn_wc_notify_changelist_clear,
                                        scratch_pool);
          notify->changelist_name = existing_changelist;
          notify_func(notify_baton, notify, scratch_pool);
        }

      if (changelist)
        {
          notify = svn_wc_create_notify(local_abspath,
                                        svn_wc_notify_changelist_set,
                                        scratch_pool);
          notify->changelist_name = changelist;
          notify_func(notify_baton, notify, scratch_pool);
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__set_file_external_location(svn_wc_context_t *wc_ctx,
                                   const char *local_abspath,
                                   const char *url,
                                   const svn_opt_revision_t *peg_rev,
                                   const svn_opt_revision_t *rev,
                                   const char *repos_root_url,
                                   apr_pool_t *scratch_pool)
{
  const char *external_repos_relpath;
  const svn_opt_revision_t unspecified_rev = { svn_opt_revision_unspecified };

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(!url || svn_uri_is_canonical(url, scratch_pool));

  if (url)
    {
      external_repos_relpath = svn_uri_is_child(repos_root_url, url, NULL);
      SVN_ERR_ASSERT(external_repos_relpath != NULL);

      external_repos_relpath = svn_path_uri_decode(external_repos_relpath,
                                                   scratch_pool);

      SVN_ERR_ASSERT(peg_rev != NULL);
      SVN_ERR_ASSERT(rev != NULL);
    }
  else
    {
      external_repos_relpath = NULL;
      peg_rev = &unspecified_rev;
      rev = &unspecified_rev;
    }

  SVN_ERR(svn_wc__db_temp_op_set_file_external(wc_ctx->db, local_abspath,
                                               external_repos_relpath, peg_rev,
                                               rev, scratch_pool));

  return SVN_NO_ERROR;
}


svn_boolean_t
svn_wc__internal_changelist_match(svn_wc__db_t *db,
                                  const char *local_abspath,
                                  apr_hash_t *clhash,
                                  apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  const char *changelist;

  if (clhash == NULL)
    return TRUE;

  err = svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             &changelist,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL,
                             db, local_abspath, scratch_pool, scratch_pool);

  if (err)
    {
      svn_error_clear(err);
      return FALSE;
    }

  return (changelist
            && apr_hash_get(clhash, changelist, APR_HASH_KEY_STRING) != NULL);
}

svn_boolean_t
svn_wc__changelist_match(svn_wc_context_t *wc_ctx,
                         const char *local_abspath,
                         apr_hash_t *clhash,
                         apr_pool_t *scratch_pool)
{
  return svn_wc__internal_changelist_match(wc_ctx->db, local_abspath, clhash,
                                           scratch_pool);
}
