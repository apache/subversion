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
#include "adm_files.h"
#include "entries.h"
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
                       svn_boolean_t via_recurse,
                       svn_revnum_t new_revnum,
                       apr_time_t new_changed_date,
                       const char *new_changed_author,
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
  svn_revnum_t new_changed_rev = new_revnum;

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

  if (status == svn_wc__db_status_deleted)
    {
      return svn_error_return(svn_wc__wq_add_deletion_postcommit(
                                db, local_abspath, new_revnum, no_unlock,
                                scratch_pool));
    }

  /* ### this picks up file and symlink  */
  if (kind != svn_wc__db_kind_dir)
    {
      /* If we sent a delta (meaning: post-copy modification),
         then this file will appear in the queue and so we should have
         its checksum already. */
      if (checksum == NULL)
        {
          /* It was copied and not modified. We must have a text
             base for it. And the node should have a checksum. */
          SVN_ERR_ASSERT(copied_checksum != NULL);

          checksum = copied_checksum;

          if (via_recurse)
            {
              /* If a copied node itself is not modified, but the op_root of
                the copy is committed we have to make sure that changed_rev,
                changed_date and changed_author don't change or the working
                copy used for committing will show different last modified
                information then a clean checkout of exactly the same
                revisions. (Issue #3676) */

                svn_boolean_t props_modified;
                svn_revnum_t changed_rev;
                const char *changed_author;
                apr_time_t changed_date;

                SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL,
                                             NULL, &changed_rev, &changed_date,
                                             &changed_author, NULL, NULL, NULL,
                                             NULL, NULL, NULL, NULL, NULL,
                                             NULL, NULL, &props_modified, NULL,
                                             NULL, NULL, NULL,
                                             db, local_abspath,
                                             scratch_pool, scratch_pool));

                if (!props_modified)
                  {
                    /* Unmodified child of copy: We keep changed_rev */
                    new_changed_rev = changed_rev;
                    new_changed_date = changed_date;
                    new_changed_author = changed_author;
                  }
            }
        }
    }
  else
    {
      /* ### If we can determine that nothing below this node was changed
         ### via this commit, we should keep new_changed_rev at its old
         ### value, like how we handle files. */
    }

  /* Set TMP_TEXT_BASE_ABSPATH to NULL.  The new text base will be found in
     the pristine store by its checksum. */
  /* ### TODO: Remove this parameter. */
  tmp_text_base_abspath = NULL;

  SVN_ERR(svn_wc__wq_add_postcommit(db, local_abspath, tmp_text_base_abspath,
                                    new_revnum,
                                    new_changed_rev, new_changed_date,
                                    new_changed_author, checksum,
                                    new_dav_cache, keep_changelist, no_unlock,
                                    scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__process_committed_internal(svn_wc__db_t *db,
                                   const char *local_abspath,
                                   svn_boolean_t recurse,
                                   svn_boolean_t top_of_recurse,
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

  SVN_ERR(process_committed_leaf(db, local_abspath, !top_of_recurse,
                                 new_revnum, new_date, rev_author,
                                 new_dav_cache,
                                 no_unlock, keep_changelist,
                                 sha1_checksum,
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
              if (status == svn_wc__db_status_deleted)
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
                                                     FALSE,
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
                                                 cqi->recurse, TRUE, new_revnum,
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



/* Remove/erase PATH from the working copy. This involves deleting PATH
 * from the physical filesystem. PATH is assumed to be an unversioned file
 * or directory.
 *
 * If ignore_enoent is TRUE, ignore missing targets.
 *
 * If CANCEL_FUNC is non-null, invoke it with CANCEL_BATON at various
 * points, return any error immediately.
 */
static svn_error_t *
erase_unversioned_from_wc(const char *path,
                          svn_boolean_t ignore_enoent,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  /* Optimize the common case: try to delete the file */
  err = svn_io_remove_file2(path, ignore_enoent, scratch_pool);
  if (err)
    {
      /* Then maybe it was a directory? */
      svn_error_clear(err);

      err = svn_io_remove_dir2(path, ignore_enoent, cancel_func, cancel_baton,
                               scratch_pool);

      if (err)
        {
          /* We're unlikely to end up here. But we need this fallback
             to make sure we report the right error *and* try the
             correct deletion at least once. */
          svn_node_kind_t kind;

          svn_error_clear(err);
          SVN_ERR(svn_io_check_path(path, &kind, scratch_pool));
          if (kind == svn_node_file)
            SVN_ERR(svn_io_remove_file2(path, ignore_enoent, scratch_pool));
          else if (kind == svn_node_dir)
            SVN_ERR(svn_io_remove_dir2(path, ignore_enoent,
                                       cancel_func, cancel_baton,
                                       scratch_pool));
          else if (kind == svn_node_none)
            return svn_error_createf(SVN_ERR_BAD_FILENAME, NULL,
                                     _("'%s' does not exist"),
                                     svn_dirent_local_style(path,
                                                            scratch_pool));
          else
            return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                     _("Unsupported node kind for path '%s'"),
                                     svn_dirent_local_style(path, scratch_pool));

        }
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
  svn_boolean_t was_add = FALSE;
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
        SVN_ERR(erase_unversioned_from_wc(local_abspath, FALSE,
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

      if (!have_base)
        was_add = strcmp(op_root_abspath, local_abspath) == 0;
      else
        {
          svn_wc__db_status_t base_status;
          SVN_ERR(svn_wc__db_base_get_info(&base_status, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL, NULL,
                                           db, local_abspath, pool, pool));

          if (base_status == svn_wc__db_status_not_present)
            was_add = TRUE;
        }
    }

  if (kind == svn_wc__db_kind_dir)
    {
      /* ### NODE_DATA We recurse into the subtree here, which is fine,
         except that we also need to record the op_depth to pass to
         svn_wc__db_temp_op_delete(), which is determined by the original
         path for which svn_wc_delete4() was called. We need a helper
         function which receives the op_depth as an argument to apply to
         the entire subtree.
       */
      apr_pool_t *iterpool = svn_pool_create(pool);
      const apr_array_header_t *children;
      int i;

      SVN_ERR(svn_wc__db_read_children(&children, db, local_abspath,
                                       pool, pool));

      for (i = 0; i < children->nelts; i++)
        {
          const char *child_basename = APR_ARRAY_IDX(children, i, const char *);
          const char *child_abspath;
          svn_boolean_t hidden;

          svn_pool_clear(iterpool);

          child_abspath = svn_dirent_join(local_abspath, child_basename,
                                          iterpool);
          SVN_ERR(svn_wc__db_node_hidden(&hidden, db, child_abspath, iterpool));
          if (hidden)
            continue;

          SVN_ERR(svn_wc_delete4(wc_ctx, child_abspath,
                                 keep_local, delete_unversioned_target,
                                 cancel_func, cancel_baton,
                                 notify_func, notify_baton,
                                 iterpool));
        }

      svn_pool_destroy(iterpool);
    }

  /* ### Maybe we should disallow deleting switched nodes here? */

    {
      /* ### The following two operations should be inside one SqLite
             transaction. For even better behavior the tree operation
             before this block needs the same handling.
             Luckily most of this is for free once properties and pristine
             are handled in the WC-NG way. */
      SVN_ERR(svn_wc__db_temp_op_delete(wc_ctx->db, local_abspath, pool));
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
        SVN_ERR(erase_unversioned_from_wc(local_abspath, TRUE,
                                          cancel_func, cancel_baton,
                                          pool));
    }

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
  svn_boolean_t is_wc_root = FALSE;
  svn_node_kind_t kind;
  svn_wc__db_t *db = wc_ctx->db;
  svn_error_t *err;
  svn_wc__db_status_t status;
  svn_wc__db_kind_t db_kind;
  svn_boolean_t exists;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(!copyfrom_url || (svn_uri_is_canonical(copyfrom_url,
                                                        scratch_pool)
                                   && SVN_IS_VALID_REVNUM(copyfrom_rev)));

  svn_dirent_split(&parent_abspath, &base_name, local_abspath, scratch_pool);
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

  /* Get the node information for this path if one exists (perhaps
     this is actually a replacement of a previously deleted thing). */
  err = svn_wc__db_read_info(&status, &db_kind, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL,
                             db, local_abspath,
                             scratch_pool, scratch_pool);

  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_return(err);

      svn_error_clear(err);
      exists = FALSE;
      is_wc_root = FALSE;
    }
  else
    {
      is_wc_root = FALSE;
      exists = TRUE;
      switch (status)
        {
          case svn_wc__db_status_not_present:
            break;
          case svn_wc__db_status_deleted:
            /* A working copy root should never have a WORKING_NODE */
            SVN_ERR_ASSERT(!is_wc_root);
            break;
          case svn_wc__db_status_normal:
            if (copyfrom_url)
              {
                SVN_ERR(svn_wc__check_wc_root(&is_wc_root, NULL, NULL,
                                              db, local_abspath,
                                              scratch_pool));

                if (is_wc_root)
                  break;
              }
            /* else: Fall through in default error */

          default:
            return svn_error_createf(
                             SVN_ERR_ENTRY_EXISTS, NULL,
                             _("'%s' is already under version control"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));
        }
    } /* err */

  SVN_ERR(svn_wc__write_check(db, parent_abspath, scratch_pool));

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
        || parent_status == svn_wc__db_status_absent)
      {
        return
          svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, err,
                            _("Can't find parent directory's node while"
                              " trying to add '%s'"),
                            svn_dirent_local_style(local_abspath,
                                                   scratch_pool));
      }
    else if (parent_status == svn_wc__db_status_deleted)
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

    if (copyfrom_url
        && !svn_uri_is_ancestor(repos_root_url, copyfrom_url))
      return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                               _("The URL '%s' has a different repository "
                                 "root than its parent"), copyfrom_url);
  }

  /* Verify that we can actually integrate the inner working copy */
  if (is_wc_root)
    {
      const char *repos_relpath, *inner_repos_root_url, *inner_repos_uuid;
      const char *inner_url;

      SVN_ERR(svn_wc__db_scan_base_repos(&repos_relpath,
                                         &inner_repos_root_url,
                                         &inner_repos_uuid,
                                         db, local_abspath,
                                         scratch_pool, scratch_pool));

      if (strcmp(inner_repos_uuid, repos_uuid)
          || strcmp(repos_root_url, inner_repos_root_url))
        return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                 _("Can't schedule the working copy at '%s' "
                                   "from repository '%s' with uuid '%s' "
                                   "for addition under a working copy from "
                                   "repository '%s' with uuid '%s'."),
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool),
                                 inner_repos_root_url, inner_repos_uuid,
                                 repos_root_url, repos_uuid);

      if (!svn_uri_is_ancestor(repos_root_url, copyfrom_url))
        return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                 _("The URL '%s' is not in repository '%s'"),
                                 copyfrom_url, repos_root_url);

      inner_url = svn_path_url_add_component2(repos_root_url, repos_relpath,
                                             scratch_pool);

      if (strcmp(copyfrom_url, inner_url))
        return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                 _("Can't add '%s' with URL '%s', but with "
                                   "the data from '%s'"),
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool),
                                 copyfrom_url, inner_url);
    }

  if (kind == svn_node_file)
    {
      if (!copyfrom_url)
        SVN_ERR(svn_wc__db_op_add_file(db, local_abspath, NULL, scratch_pool));
      else
        {
          /* This code should never be used, as it doesn't install proper
             pristine and/or properties. But it was not an error in the old
             version of this function. 

             ===> Use svn_wc_add_repos_file4() directly! */
          svn_stream_t *content = svn_stream_empty(scratch_pool);

          SVN_ERR(svn_wc_add_repos_file4(wc_ctx, local_abspath,
                                         content, NULL,
                                         NULL, NULL,
                                         copyfrom_url, copyfrom_rev,
                                         cancel_func, cancel_baton,
                                         NULL, NULL,
                                         scratch_pool));
        }
    }
  else if (!copyfrom_url)
    {
      SVN_ERR(svn_wc__db_op_add_directory(db, local_abspath, NULL,
                                          scratch_pool));
      if (!exists)
        {
          /* If using the legacy 1.6 interface the parent lock may not
             be recursive and add is expected to lock the new dir.

             ### Perhaps the lock should be created in the same
             transaction that adds the node? */
          svn_boolean_t owns_lock;
          SVN_ERR(svn_wc__db_wclock_owns_lock(&owns_lock, db, local_abspath,
                                              FALSE, scratch_pool));
          if (!owns_lock)
            SVN_ERR(svn_wc__db_wclock_obtain(db, local_abspath, 0, FALSE,
                                             scratch_pool));
        }
    }
  else if (!is_wc_root)
    SVN_ERR(svn_wc__db_op_copy_dir(db,
                                   local_abspath,
                                   apr_hash_make(scratch_pool),
                                   copyfrom_rev,
                                   0,
                                   NULL,
                                   svn_path_uri_decode(
                                        svn_uri_skip_ancestor(repos_root_url,
                                                              copyfrom_url),
                                        scratch_pool),
                                   repos_root_url,
                                   repos_uuid,
                                   copyfrom_rev,
                                   NULL,
                                   depth,
                                   NULL,
                                   NULL,
                                   scratch_pool));
  else
    {
      svn_boolean_t owns_lock;
      const char *tmpdir_abspath, *moved_abspath, *moved_adm_abspath;
      const char *adm_abspath = svn_wc__adm_child(local_abspath, "",
                                                  scratch_pool);

      /* Drop any references to the wc that is to be rewritten */
      SVN_ERR(svn_wc__db_drop_root(db, local_abspath, scratch_pool));

      /* Move the admin dir from the wc to a temporary location */
      SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&tmpdir_abspath, db,
                                             parent_abspath,
                                             scratch_pool, scratch_pool));
      SVN_ERR(svn_io_open_unique_file3(NULL, &moved_abspath, tmpdir_abspath,
                                       svn_io_file_del_on_close,
                                       scratch_pool, scratch_pool));
      SVN_ERR(svn_io_dir_make(moved_abspath, APR_OS_DEFAULT, scratch_pool));
      moved_adm_abspath = svn_wc__adm_child(moved_abspath, "", scratch_pool);
      SVN_ERR(svn_io_file_move(adm_abspath, moved_adm_abspath, scratch_pool));

      /* Copy entries from temporary location into the main db */
      SVN_ERR(svn_wc_copy3(wc_ctx, moved_abspath, local_abspath,
                           TRUE /* metadata_only */,
                           NULL, NULL, NULL, NULL, scratch_pool));

      /* Cleanup the temporary admin dir */
      SVN_ERR(svn_wc__db_drop_root(db, moved_abspath, scratch_pool));
      SVN_ERR(svn_io_remove_dir2(moved_abspath, FALSE, NULL, NULL,
                                 scratch_pool));

      /* The subdir is now part of our parent working copy. Our caller assumes
         that we return the new node locked, so obtain a lock if we didn't
         receive the lock via our depth infinity lock */
      SVN_ERR(svn_wc__db_wclock_owns_lock(&owns_lock, db, local_abspath, FALSE,
                                          scratch_pool));
      if (!owns_lock)
        SVN_ERR(svn_wc__db_wclock_obtain(db, local_abspath, 0, FALSE,
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
svn_wc__register_file_external(svn_wc_context_t *wc_ctx,
                               const char *local_abspath,
                               const char *external_url,
                               const svn_opt_revision_t *external_peg_rev,
                               const svn_opt_revision_t *external_rev,
                               apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = wc_ctx->db;
  const char *parent_abspath, *base_name, *repos_root_url;

  svn_dirent_split(&parent_abspath, &base_name, local_abspath, scratch_pool);

  SVN_ERR(svn_wc__db_scan_base_repos(NULL, &repos_root_url, NULL, db,
                                     parent_abspath, scratch_pool,
                                     scratch_pool));

  SVN_ERR(svn_wc__db_op_add_file(wc_ctx->db, local_abspath, NULL, scratch_pool));

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
  svn_wc__db_status_t status, base_status;
  svn_wc__db_kind_t kind, base_kind;
  svn_boolean_t replaced;
  svn_boolean_t have_base;
  svn_revnum_t base_revision;
  svn_boolean_t is_add_root;

  /* Initialize this even though revert_admin_things() is guaranteed
     to set it, because we don't know that revert_admin_things() will
     be called. */
  svn_boolean_t reverted = FALSE;

  SVN_ERR(svn_wc__db_read_info(&status, &kind,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, &have_base, NULL,
                               NULL, NULL,
                               db, local_abspath, pool, pool));

  if (have_base)
    SVN_ERR(svn_wc__db_base_get_info(&base_status, &base_kind, &base_revision,
                                     NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                     NULL, NULL, NULL, NULL, NULL,
                                     db, local_abspath, pool, pool));

  replaced = (status == svn_wc__db_status_added
              && have_base
              && base_status != svn_wc__db_status_not_present);

  if (status == svn_wc__db_status_added)
    {
      const char *op_root_abspath;
      SVN_ERR(svn_wc__db_scan_addition(NULL, &op_root_abspath, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL,
                                       db, local_abspath, pool, pool));

      is_add_root = (strcmp(op_root_abspath, local_abspath) == 0);
    }
  else
    is_add_root = FALSE;

  /* Additions. */
  if (!replaced
      && is_add_root)
    {
      const char *repos_relpath;
      const char *repos_root_url;
      const char *repos_uuid;
      /* Before removing item from revision control, notice if the
         BASE_NODE is in a 'not-present' state. */
      svn_boolean_t was_not_present = FALSE;

      /* NOTE: if WAS_NOT_PRESENT gets set, then we have BASE nodes.
         The code below will then figure out the repository information, so
         that we can later insert a node for the same repository. */

      if (have_base
          && base_status == svn_wc__db_status_not_present)
        {
          /* Remember the BASE revision. (already handled)  */
          /* Remember the repository this node is associated with.  */

          was_not_present = TRUE;

          SVN_ERR(svn_wc__db_scan_base_repos(&repos_relpath,
                                             &repos_root_url,
                                             &repos_uuid,
                                             db, local_abspath,
                                             pool, pool));
        }

      /* ### much of this is probably bullshit. we should be able to just
         ### remove the WORKING and ACTUAL rows, and be done. but we're
         ### not quite there yet, so nodes get fully removed and then
         ### shoved back into the database. this is why we need to record
         ### the repository information, and the BASE revision.  */

      if (kind == svn_wc__db_kind_file)
        {
          SVN_ERR(svn_wc__internal_remove_from_revision_control(db,
                                                                local_abspath,
                                                                FALSE, FALSE,
                                                                cancel_func,
                                                                cancel_baton,
                                                                pool));
        }
      else if (kind == svn_wc__db_kind_dir)
        {
          /* Before single-db we didn't have to perform a recursive delete
             here. With single-db we really must delete missing nodes */
          SVN_ERR(svn_wc__internal_remove_from_revision_control(db,
                                                                local_abspath,
                                                                FALSE, FALSE,
                                                                cancel_func,
                                                                cancel_baton,
                                                                pool));
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

      /* If the removed item was *also* in a 'not-present' state, make
         sure we leave a not-present node behind */
      if (was_not_present)
        {
          SVN_ERR(svn_wc__db_base_add_absent_node(
                    db, local_abspath,
                    repos_relpath, repos_root_url, repos_uuid,
                    base_revision,
                    base_kind,
                    svn_wc__db_status_not_present,
                    NULL, NULL,
                    pool));
        }
    }
  /* Regular prop and text edit. */
  /* Deletions and replacements. */
  else if (status == svn_wc__db_status_normal
           || status == svn_wc__db_status_deleted
           || replaced
           || (status == svn_wc__db_status_added && !is_add_root))
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

/* Verifies if an add (or copy) to LOCAL_ABSPATH can be reverted with depth
 * DEPTH, without touching nodes that are filtered by DEPTH.
 *
 * Use ROOT_ABSPATH for generating error messages.
 */
static svn_error_t *
verify_revert_depth(svn_wc__db_t *db,
                    const char *local_abspath,
                    svn_depth_t depth,
                    const char *root_abspath,
                    apr_pool_t *scratch_pool)
{
  const apr_array_header_t *children;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;

  SVN_ERR_ASSERT(depth >= svn_depth_empty && depth < svn_depth_infinity);

  SVN_ERR(svn_wc__db_read_children(&children, db, local_abspath,
                                   scratch_pool, iterpool));

  for (i = 0; i < children->nelts; i++)
    {
      const char *name = APR_ARRAY_IDX(children, i, const char *);
      const char *child_abspath;
      svn_wc__db_status_t status;
      svn_wc__db_kind_t kind;

      svn_pool_clear(iterpool);

      child_abspath = svn_dirent_join(local_abspath, name, iterpool);

      SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL,
                                   db, child_abspath, iterpool, iterpool));

      /* Not-here and deleted nodes won't be reverted by reverting an operation
         on a parent, so we can just skip them for the depth check. */
      if (status == svn_wc__db_status_not_present
          || status == svn_wc__db_status_absent
          || status == svn_wc__db_status_excluded
          || status == svn_wc__db_status_deleted)
        continue;

      if (depth == svn_depth_empty
          || (depth == svn_depth_files && kind == svn_wc__db_kind_dir))
        {
          return svn_error_createf(
                        SVN_ERR_WC_INVALID_OPERATION_DEPTH, NULL,
                        _("Can't revert '%s' with this depth, as that requires"
                          " reverting '%s'."),
                        svn_dirent_local_style(root_abspath, iterpool),
                        svn_dirent_local_style(child_abspath, iterpool));
        }

      if (kind == svn_wc__db_kind_dir)
        SVN_ERR(verify_revert_depth(db, child_abspath, svn_depth_empty,
                                    root_abspath, iterpool));
    }

  svn_pool_destroy(iterpool);

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
    switch (status)
      {
        case svn_wc__db_status_not_present:
        case svn_wc__db_status_absent:
        case svn_wc__db_status_excluded:
          unversioned = TRUE;
          break;
        default:
          unversioned = FALSE;
          break;
      }

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
      if (disk_kind == svn_node_file)
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
      && (db_kind != svn_wc__db_kind_file)
      && (db_kind != svn_wc__db_kind_dir)
      && (db_kind != svn_wc__db_kind_symlink))
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

  /* Safeguard 4:  Make sure we don't revert deeper then asked */
  if (status == svn_wc__db_status_added
      && db_kind == svn_wc__db_kind_dir
      && depth >= svn_depth_empty
      && depth < svn_depth_infinity)
    {
      const char *op_root_abspath;
      SVN_ERR(svn_wc__db_scan_addition(NULL, &op_root_abspath, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL,
                                       db, local_abspath, pool, pool));

      /* If this node is an operation root for a copy/add, then reverting
         it will change its descendants, if it has any. */
      if (strcmp(local_abspath, op_root_abspath) == 0)
        SVN_ERR(verify_revert_depth(db, local_abspath, depth,
                                    local_abspath, pool));
    }

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
  svn_wc__db_status_t status;
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

  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL,
                               db, local_abspath, scratch_pool, scratch_pool));

  if (kind == svn_wc__db_kind_file || kind == svn_wc__db_kind_symlink)
    {
      svn_node_kind_t on_disk;
      svn_boolean_t wc_special, local_special;
      svn_boolean_t text_modified_p;
      const svn_checksum_t *base_sha1_checksum, *working_sha1_checksum;

      /* Only check if the file was modified when it wasn't overwritten with a
         special file */
      SVN_ERR(svn_wc__get_translate_info(NULL, NULL, NULL,
                                         &wc_special, db, local_abspath,
                                         scratch_pool, scratch_pool));
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

      /* Remove NAME from PATH's entries file: */
      SVN_ERR(svn_wc__db_temp_op_remove_entry(db, local_abspath,
                                              scratch_pool));

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

          /* ### where did the adm_missing and depth_exclude test go?!? 

             ### BH: depth exclude is handled by hidden and missing is ok
                     for this temp_op. */

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
      SVN_ERR(svn_wc__adm_destroy(db, local_abspath,
                                  cancel_func, cancel_baton, iterpool));

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
              if (!APR_STATUS_IS_ENOENT(err->apr_err))
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

      if (external_repos_relpath == NULL)
          return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                   _("Can't add a file external to '%s' as it"
                                     " is not a file in repository '%s'."),
                                   url, repos_root_url);

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
