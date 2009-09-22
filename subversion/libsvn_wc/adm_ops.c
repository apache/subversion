/*
 * adm_ops.c: routines for affecting working copy administrative
 *            information.  NOTE: this code doesn't know where the adm
 *            info is actually stored.  Instead, generic handles to
 *            adm data are requested via a reference to some PATH
 *            (PATH being a regular, non-administrative directory or
 *            file in the working copy).
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
  apr_pool_t *pool;
  apr_array_header_t *queue;
  svn_boolean_t have_recursive;
};

typedef struct
{
  const char *path;
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t recurse;
  svn_boolean_t remove_lock;
  svn_boolean_t remove_changelist;
  apr_array_header_t *wcprop_changes;
  svn_checksum_t *checksum;
} committed_queue_item_t;



/*** Finishing updates and commits. ***/


/* The main body of svn_wc__do_update_cleanup. */
static svn_error_t *
tweak_entries(svn_wc__db_t *db,
              const char *dir_abspath,
              const char *base_url,
              svn_revnum_t new_rev,
              svn_wc_notify_func2_t notify_func,
              void *notify_baton,
              svn_boolean_t remove_missing_dirs,
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

  /* Tweak "this_dir" */
  SVN_ERR(svn_wc__tweak_entry(db, dir_abspath, base_url, new_rev,
                              TRUE, FALSE /* allow_removal */, pool));

  if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  /* Early out */
  if (depth <= svn_depth_empty)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__db_read_children(&children, db, dir_abspath, pool, pool));

  iterpool = svn_pool_create(pool);
  for (i = 0; i < children->nelts; i++)
    {
      const char *child_basename = APR_ARRAY_IDX(children, i, const char *);
      const char *child_abspath;
      svn_wc__db_kind_t kind;
      svn_depth_t child_depth;
      svn_wc__db_status_t status;

      const char *child_url = NULL;
      svn_boolean_t excluded;

      svn_pool_clear(iterpool);

      /* Derive the new URL for the current (child) entry */
      if (base_url)
        child_url = svn_path_url_add_component2(base_url, child_basename,
                                                iterpool);

      child_abspath = svn_dirent_join(dir_abspath, child_basename, iterpool);
      excluded = (apr_hash_get(exclude_paths, child_abspath,
                               APR_HASH_KEY_STRING) != NULL);

      SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, &child_depth, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   db, child_abspath, iterpool, iterpool));

      /* If a file, or deleted, excluded or absent dir, then tweak the
         entry but don't recurse.
         
         ### how does this translate into wc_db land? */
      if (kind == svn_wc__db_kind_file
            || status == svn_wc__db_status_not_present
            || status == svn_wc__db_status_absent
            || child_depth == svn_depth_exclude)
        {
          if (! excluded)
            SVN_ERR(svn_wc__tweak_entry(db, child_abspath,
                                        child_url, new_rev,
                                        FALSE, TRUE /* allow_removal */,
                                        pool));
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
          if (remove_missing_dirs
              && svn_wc__adm_missing(db, child_abspath, iterpool))
            {
              if ( (status == svn_wc__db_status_added
                    || status == svn_wc__db_status_obstructed_add)
                  && !excluded)
                {
                  SVN_ERR(svn_wc__entry_remove(db, child_abspath, iterpool));

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
              SVN_ERR(tweak_entries(db, child_abspath, child_url,
                                    new_rev, notify_func, notify_baton,
                                    remove_missing_dirs, depth_below_here,
                                    exclude_paths, iterpool));
            }
        }
    }

  /* Cleanup */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
remove_revert_files(svn_stringbuf_t **logtags,
                    svn_wc_adm_access_t *adm_access,
                    const char *path,
                    apr_pool_t * pool)
{
  const char *revert_file, *local_abspath;
  svn_node_kind_t kind;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));


  SVN_ERR(svn_wc__text_revert_path(&revert_file,
                                   svn_wc__adm_get_db(adm_access),
                                   local_abspath,
                                   pool));

  SVN_ERR(svn_io_check_path(revert_file, &kind, pool));
  if (kind == svn_node_file)
    SVN_ERR(svn_wc__loggy_remove(logtags,
                                 svn_wc__adm_access_abspath(adm_access),
                                 revert_file, pool, pool));

  return svn_wc__loggy_props_delete(logtags, path, svn_wc__props_revert,
                                    adm_access, pool);
}

svn_error_t *
svn_wc__do_update_cleanup(svn_wc__db_t *db,
                          const char *local_abspath,
                          svn_depth_t depth,
                          const char *base_url,
                          const char *repos,
                          svn_revnum_t new_revision,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_boolean_t remove_missing_dirs,
                          apr_hash_t *exclude_paths,
                          apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;
  svn_error_t *err;

  err = svn_wc__get_entry(&entry, db, local_abspath, TRUE, svn_node_unknown,
                          FALSE, pool, pool);
  if ((err && err->apr_err == SVN_ERR_NODE_UNEXPECTED_KIND) || (entry == NULL))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else if (err)
    return svn_error_return(err);

  if (entry->kind == svn_node_file
      || (entry->kind == svn_node_dir
          && (entry->deleted || entry->absent
              || entry->depth == svn_depth_exclude)))
    {
      if (apr_hash_get(exclude_paths, local_abspath, APR_HASH_KEY_STRING))
        return SVN_NO_ERROR;

      /* Parent not updated so don't remove PATH entry.  */
      SVN_ERR(svn_wc__tweak_entry(db, local_abspath,
                                  base_url, new_revision,
                                  FALSE, FALSE /* allow_removal */,
                                  pool));
    }

  else if (entry->kind == svn_node_dir)
    {
      SVN_ERR(tweak_entries(db, local_abspath, base_url, new_revision,
                            notify_func, notify_baton, remove_missing_dirs,
                            depth, exclude_paths, pool));
    }

  else
    return svn_error_createf(SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                             _("Unrecognized node kind: '%s'"),
                             svn_dirent_local_style(local_abspath, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
process_committed_leaf(int log_number,
                       const char *path,
                       svn_wc_adm_access_t *adm_access,
                       const svn_wc_entry_t *entry,
                       svn_revnum_t new_revnum,
                       const char *rev_date,
                       const char *rev_author,
                       apr_array_header_t *wcprop_changes,
                       svn_boolean_t remove_lock,
                       svn_boolean_t remove_changelist,
                       svn_checksum_t *checksum,
                       svn_wc_committed_queue_t *queue,
                       apr_pool_t *pool)
{
  svn_wc_entry_t tmp_entry;
  apr_uint64_t modify_flags = 0;
  svn_stringbuf_t *logtags = svn_stringbuf_create("", pool);

  SVN_ERR(svn_wc__adm_write_check(adm_access, pool));

  /* Set PATH's working revision to NEW_REVNUM; if REV_DATE and
     REV_AUTHOR are both non-NULL, then set the 'committed-rev',
     'committed-date', and 'last-author' entry values; and set the
     checksum if a file. */

  if (entry->kind == svn_node_file)
    {
      /* If the props or text revert file exists it needs to be deleted when
       * the file is committed. */
      /* ### don't directories have revert props? */
      SVN_ERR(remove_revert_files(&logtags, adm_access, path, pool));

      if (checksum == NULL)
        {
          /* checksum will be NULL for recursive commits, which means that
             a directory was copied. When we recurse on that directory, the
             checksum will be NULL for all files. */

          /* If we sent a delta (meaning: post-copy modification),
             then this file will appear in the queue.  See if we can
             find it. */
          int i;

          /* ### this is inefficient. switch to hash. that's round #2 */

          if (queue != NULL)
            for (i = 0; i < queue->queue->nelts; i++)
              {
                committed_queue_item_t *cqi
                  = APR_ARRAY_IDX(queue->queue, i, committed_queue_item_t *);
                if (strcmp(path, cqi->path) == 0)
                  {
                    checksum = cqi->checksum;
                    break;
                  }
              }
          if (checksum == NULL)
            {
              /* It was copied and not modified. We should have a text
                 base for it. And the entry should have a checksum. */
              if (entry->checksum != NULL)
                {
                  SVN_ERR(svn_checksum_parse_hex(&checksum,
                                                 svn_checksum_md5,
                                                 entry->checksum,
                                                 pool));
                }
#ifdef SVN_DEBUG
              else
                {
                  /* If we copy a deleted file, then it will become scheduled
                     for deletion, but there is no base text for it. So we
                     cannot get/compute a checksum for this file. */
                  SVN_ERR_ASSERT(entry->schedule == svn_wc_schedule_delete);

                  /* checksum will remain NULL in this one case. */
                }
#endif
            }
        }
    }


  /* Append a log command to set (overwrite) the 'committed-rev',
     'committed-date', 'last-author', and possibly 'checksum'
     attributes in the entry.

     Note: it's important that this log command come *before* the
     LOG_COMMITTED command, because log_do_committed() might actually
     remove the entry! */
  if (rev_date)
    {
      tmp_entry.cmt_rev = new_revnum;
      SVN_ERR(svn_time_from_cstring(&tmp_entry.cmt_date, rev_date, pool));
      modify_flags |= SVN_WC__ENTRY_MODIFY_CMT_REV
        | SVN_WC__ENTRY_MODIFY_CMT_DATE;
    }

  if (rev_author)
    {
      tmp_entry.cmt_rev = new_revnum;
      tmp_entry.cmt_author = rev_author;
      modify_flags |= SVN_WC__ENTRY_MODIFY_CMT_REV
        | SVN_WC__ENTRY_MODIFY_CMT_AUTHOR;
    }

  if (checksum)
    {
      tmp_entry.checksum = svn_checksum_to_cstring(checksum, pool);
      modify_flags |= SVN_WC__ENTRY_MODIFY_CHECKSUM;
    }

  if (modify_flags)
    SVN_ERR(svn_wc__loggy_entry_modify(&logtags,
                                       svn_wc__adm_access_abspath(adm_access),
                                       path, &tmp_entry, modify_flags,
                                       pool, pool));

  if (remove_lock)
    SVN_ERR(svn_wc__loggy_delete_lock(&logtags,
                                      svn_wc__adm_access_abspath(adm_access),
                                      path, pool, pool));

  if (remove_changelist)
    SVN_ERR(svn_wc__loggy_delete_changelist(&logtags,
                                      svn_wc__adm_access_abspath(adm_access),
                                      path, pool, pool));

  /* Regardless of whether it's a file or dir, the "main" logfile
     contains a command to bump the revision attribute (and
     timestamp). */
  SVN_ERR(svn_wc__loggy_committed(&logtags,
                                  svn_wc__adm_access_abspath(adm_access),
                                  path, new_revnum, pool, pool));


  /* Do wcprops in the same log txn as revision, etc. */
  if (wcprop_changes && (wcprop_changes->nelts > 0))
    {
      int i;

      for (i = 0; i < wcprop_changes->nelts; i++)
        {
          svn_prop_t *prop = APR_ARRAY_IDX(wcprop_changes, i, svn_prop_t *);

          SVN_ERR(svn_wc__loggy_modify_wcprop(&logtags,
                                       svn_wc__adm_access_abspath(adm_access),
                                       path, prop->name,
                                       prop->value ? prop->value->data : NULL,
                                       pool, pool));
        }
    }

  /* Write our accumulation of log entries into a log file */
  return svn_wc__write_log(adm_access, log_number, logtags, pool);
}


static svn_error_t *
process_committed_internal(int *log_number,
                           const char *path,
                           svn_wc_adm_access_t *adm_access,
                           svn_boolean_t recurse,
                           svn_revnum_t new_revnum,
                           const char *rev_date,
                           const char *rev_author,
                           apr_array_header_t *wcprop_changes,
                           svn_boolean_t remove_lock,
                           svn_boolean_t remove_changelist,
                           svn_checksum_t *checksum,
                           svn_wc_committed_queue_t *queue,
                           apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;
  const char *local_abspath;
  svn_wc__db_t *db = svn_wc__adm_get_db(adm_access);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));
  if (entry == NULL)
    return SVN_NO_ERROR;  /* deleted/absent. (?) ... nothing to do. */

  SVN_ERR(process_committed_leaf((*log_number)++, path, adm_access, entry,
                                 new_revnum, rev_date, rev_author,
                                 wcprop_changes,
                                 remove_lock, remove_changelist,
                                 checksum, queue, pool));

  if (recurse && entry->kind == svn_node_dir)
    {
      const apr_array_header_t *children;
      apr_pool_t *subpool = svn_pool_create(pool);
      int i;

      /* Read PATH's entries;  this is the absolute path. */
      SVN_ERR(svn_wc__db_read_children(&children, db, local_abspath, pool,
                                       subpool));

      /* Recursively loop over all children. */
      for (i = 0; i < children->nelts; i++)
        {
          const char *name = APR_ARRAY_IDX(children, i, const char *);
          const svn_wc_entry_t *current_entry;
          const char *this_path;
          const char *this_abspath;
          svn_error_t *err;

          svn_pool_clear(subpool);

          this_abspath = svn_dirent_join(local_abspath, name, subpool);

          err = svn_wc__get_entry(&current_entry, db, this_abspath, FALSE,
                                  svn_node_unknown, FALSE, subpool, subpool);

          if (err && err->apr_err == SVN_ERR_NODE_UNEXPECTED_KIND)
            svn_error_clear(err);
          else
            SVN_ERR(err);

          /* We come to this branch since we have committed a copied tree.
             svn_depth_exclude is possible in this situation. So check and
             skip */
          if (current_entry->depth == svn_depth_exclude)
            continue;

          /* Create child path by telescoping the main path. */
          this_path = svn_dirent_join(path, name, subpool);

          /* Recurse, but only allow further recursion if the child is
             a directory.  Pass null for wcprop_changes, because the
             ones present in the current call are only applicable to
             this one committed item. */
          if (current_entry->kind == svn_node_dir)
            {
              svn_wc_adm_access_t *child_access;
              int inner_log = 0;

              SVN_ERR(svn_wc_adm_retrieve(&child_access, adm_access,
                                          this_path, subpool));

              SVN_ERR(process_committed_internal(&inner_log,
                                                 this_path, child_access,
                                                 TRUE /* recurse */,
                                                 new_revnum, rev_date,
                                                 rev_author,
                                                 NULL, FALSE /* remove_lock */,
                                                 remove_changelist, NULL,
                                                 queue, subpool));
              SVN_ERR(svn_wc__run_log(child_access, pool));
            }
          else
            {
              /* Suppress log creation for deleted entries in a replaced
                 directory.  By the time any log we create here is run,
                 those entries will already have been removed (as a result
                 of running the log for the replaced directory that was
                 created at the start of this function). */
              if (current_entry->schedule == svn_wc_schedule_delete)
                {
                  if (entry->schedule == svn_wc_schedule_replace)
                    continue;
                }
              SVN_ERR(process_committed_leaf
                      ((*log_number)++, this_path, adm_access, current_entry,
                       new_revnum, rev_date, rev_author, NULL, FALSE,
                       remove_changelist, NULL, queue, subpool));
            }
        }

      svn_pool_destroy(subpool);
   }

  return SVN_NO_ERROR;
}


svn_wc_committed_queue_t *
svn_wc_committed_queue_create(apr_pool_t *pool)
{
  svn_wc_committed_queue_t *q;

  q = apr_palloc(pool, sizeof(*q));
  q->pool = pool;
  q->queue = apr_array_make(pool, 1, sizeof(committed_queue_item_t *));
  q->have_recursive = FALSE;

  return q;
}

svn_error_t *
svn_wc_queue_committed2(svn_wc_committed_queue_t *queue,
                        const char *path,
                        svn_wc_adm_access_t *adm_access,
                        svn_boolean_t recurse,
                        apr_array_header_t *wcprop_changes,
                        svn_boolean_t remove_lock,
                        svn_boolean_t remove_changelist,
                        svn_checksum_t *checksum,
                        apr_pool_t *scratch_pool)
{
  committed_queue_item_t *cqi;

  queue->have_recursive |= recurse;

  /* Use the same pool as the one QUEUE was allocated in,
     to prevent lifetime issues.  Intermediate operations
     should use SCRATCH_POOL. */

  /* Add to the array with paths and options */
  cqi = apr_palloc(queue->pool, sizeof(*cqi));
  cqi->path = path;
  cqi->adm_access = adm_access;
  cqi->recurse = recurse;
  cqi->remove_lock = remove_lock;
  cqi->remove_changelist = remove_changelist;
  cqi->wcprop_changes = wcprop_changes;
  cqi->checksum = checksum;

  APR_ARRAY_PUSH(queue->queue, committed_queue_item_t *) = cqi;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_queue_committed(svn_wc_committed_queue_t **queue,
                       const char *path,
                       svn_wc_adm_access_t *adm_access,
                       svn_boolean_t recurse,
                       apr_array_header_t *wcprop_changes,
                       svn_boolean_t remove_lock,
                       svn_boolean_t remove_changelist,
                       const unsigned char *digest,
                       apr_pool_t *pool)
{
  svn_checksum_t *checksum;

  if (digest)
    checksum = svn_checksum__from_digest(digest, svn_checksum_md5,
                                         (*queue)->pool);
  else
    checksum = NULL;

  return svn_wc_queue_committed2(*queue, path, adm_access, recurse,
                                 wcprop_changes, remove_lock,
                                 remove_changelist,
                                 checksum, pool);
}

typedef struct affected_adm_t
{
  int next_log;
  svn_wc_adm_access_t *adm_access;
} affected_adm_t;


/* Return TRUE if any item of QUEUE is a parent of ITEM and will be
   processed recursively, return FALSE otherwise.
*/
static svn_boolean_t
have_recursive_parent(apr_array_header_t *queue,
                      int item,
                      apr_pool_t *pool)
{
  int i;
  const char *path
    = APR_ARRAY_IDX(queue, item, committed_queue_item_t *)->path;

  for (i = 0; i < queue->nelts; i++)
    {
      committed_queue_item_t *qi;

      if (i == item)
        continue;

      qi = APR_ARRAY_IDX(queue, i, committed_queue_item_t *);
      if (qi->recurse && svn_dirent_is_child(qi->path, path, NULL))
        return TRUE;
    }

  return FALSE;
}

svn_error_t *
svn_wc_process_committed_queue(svn_wc_committed_queue_t *queue,
                               svn_wc_adm_access_t *adm_access,
                               svn_revnum_t new_revnum,
                               const char *rev_date,
                               const char *rev_author,
                               apr_pool_t *pool)
{
  int i;
  apr_hash_index_t *hi;
  apr_hash_t *updated_adms = apr_hash_make(pool);
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Now, we write all log files, collecting the affected adms in
     the process ... */
  for (i = 0; i < queue->queue->nelts; i++)
    {
      affected_adm_t *affected_adm;
      const char *adm_path;
      committed_queue_item_t *cqi = APR_ARRAY_IDX(queue->queue,
                                                  i, committed_queue_item_t *);

      svn_pool_clear(iterpool);

      /* If there are some recursive items, then see if this item is a
         child of one, and will (implicitly) be accounted for. */
      if (queue->have_recursive
          && have_recursive_parent(queue->queue, i, iterpool))
        continue;

      adm_path = svn_wc_adm_access_path(cqi->adm_access);
      affected_adm = apr_hash_get(updated_adms,
                                  adm_path, APR_HASH_KEY_STRING);
      if (! affected_adm)
        {
          /* allocate in pool instead of iterpool:
             we don't want this cleared at the next iteration */
          affected_adm = apr_palloc(pool, sizeof(*affected_adm));
          affected_adm->next_log = 0;
          affected_adm->adm_access = cqi->adm_access;
          apr_hash_set(updated_adms, adm_path, APR_HASH_KEY_STRING,
                       affected_adm);
        }

      SVN_ERR(process_committed_internal(&affected_adm->next_log, cqi->path,
                                         cqi->adm_access, cqi->recurse,
                                         new_revnum, rev_date, rev_author,
                                         cqi->wcprop_changes,
                                         cqi->remove_lock,
                                         cqi->remove_changelist,
                                         cqi->checksum, queue, iterpool));
    }

  /* ... and then we run them; all at once.

         This prevents writing the entries file
         more than once per adm area */
  for (hi = apr_hash_first(pool, updated_adms); hi; hi = apr_hash_next(hi))
    {
      void *val;
      affected_adm_t *this_adm;

      svn_pool_clear(iterpool);

      apr_hash_this(hi, NULL, NULL, &val);
      this_adm = val;

      SVN_ERR(svn_wc__run_log(this_adm->adm_access, iterpool));
    }

  queue->queue->nelts = 0;

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_process_committed4(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t recurse,
                          svn_revnum_t new_revnum,
                          const char *rev_date,
                          const char *rev_author,
                          apr_array_header_t *wcprop_changes,
                          svn_boolean_t remove_lock,
                          svn_boolean_t remove_changelist,
                          const unsigned char *digest,
                          apr_pool_t *pool)
{
  svn_checksum_t *checksum;
  int log_number = 0;

  if (digest)
    checksum = svn_checksum__from_digest(digest, svn_checksum_md5, pool);
  else
    checksum = NULL;

  SVN_ERR(process_committed_internal(&log_number,
                                     path, adm_access, recurse,
                                     new_revnum, rev_date, rev_author,
                                     wcprop_changes, remove_lock,
                                     remove_changelist, checksum, NULL, pool));

  /* Run the log file(s) we just created. */
  return svn_wc__run_log(adm_access, pool);
}



/* Recursively mark a tree LOCAL_ABSPATH with a SCHEDULE, COPIED and/or
   KEEP_LOCAL flag, depending on the state of MODIFY_FLAGS (which may contain
   only a subset of the possible modification flags, namely, those indicating
   a change to one of the three flags mentioned above).  If setting the COPIED
   flag, skip items scheduled for deletion. */
static svn_error_t *
mark_tree(svn_wc__db_t *db,
          const char *dir_abspath,
          apr_uint64_t modify_flags,
          svn_wc_schedule_t schedule,
          svn_boolean_t copied,
          svn_boolean_t keep_local,
          svn_cancel_func_t cancel_func,
          void *cancel_baton,
          svn_wc_notify_func2_t notify_func,
          void *notify_baton,
          apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  const apr_array_header_t *children;
  const svn_wc_entry_t *entry;
  svn_wc_entry_t tmp_entry;
  apr_uint64_t this_dir_flags;
  int i;

  /* Read the entries file for this directory. */
  SVN_ERR(svn_wc__db_read_children(&children, db, dir_abspath, pool, pool));

  /* Mark each entry in the entries file. */
  for (i = 0; i < children->nelts; i++)
    {
      const char *child_basename = APR_ARRAY_IDX(children, i, const char *);
      const char *child_abspath;
      svn_boolean_t hidden;

      /* Clear our per-iteration pool. */
      svn_pool_clear(iterpool);

      child_abspath = svn_dirent_join(dir_abspath, child_basename, iterpool);

      /* We exclude hidden nodes from this operation. */
      SVN_ERR(svn_wc__db_node_hidden(&hidden, db, child_abspath, iterpool));
      if (hidden)
        continue;

      SVN_ERR(svn_wc__get_entry(&entry, db, child_abspath, FALSE,
                                svn_node_unknown, FALSE, iterpool, iterpool));

      /* If setting the COPIED flag, skip deleted items. */
      if (copied && entry->schedule == svn_wc_schedule_delete)
        continue;
  
      /* If this is a directory, recurse. */
      if (entry->kind == svn_node_dir)
        {
          SVN_ERR(mark_tree(db, child_abspath, modify_flags,
                            schedule, copied, keep_local,
                            cancel_func, cancel_baton,
                            notify_func, notify_baton,
                            iterpool));
        }

      tmp_entry.schedule = schedule;
      tmp_entry.copied = copied;
      SVN_ERR(svn_wc__entry_modify2(db, child_abspath, svn_node_unknown,
                            TRUE, &tmp_entry,
                            modify_flags & (SVN_WC__ENTRY_MODIFY_SCHEDULE
                                           | SVN_WC__ENTRY_MODIFY_COPIED),
                            iterpool));

      if (copied)
        {
          /* Remove now obsolete dav cache values.  */
          SVN_ERR(svn_wc__db_base_set_dav_cache(db, child_abspath, NULL,
                                                iterpool));
        }

      /* Tell someone what we've done. */
      if (schedule == svn_wc_schedule_delete && notify_func != NULL)
        (*notify_func)(notify_baton,
                       svn_wc_create_notify(child_abspath,
                                            svn_wc_notify_delete,
                                            iterpool), iterpool);
    }

  /* Handle "this dir" for states that need it done post-recursion. */
  SVN_ERR(svn_wc__get_entry(&entry, db, dir_abspath, FALSE,
                            svn_node_dir, FALSE, iterpool, iterpool));
  this_dir_flags = 0;

  /* Uncommitted directories (schedule add) that are to be scheduled for
     deletion are a special case, they don't need to be changed as they
     will be removed from their parent's entry list.
     The files and directories are left on the disk in this special
     case, so KEEP_LOCAL doesn't need to be set either. */
  if (! (entry->schedule == svn_wc_schedule_add
         && schedule == svn_wc_schedule_delete))
  {
    if (modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE)
      {
        tmp_entry.schedule = schedule;
        this_dir_flags |= SVN_WC__ENTRY_MODIFY_SCHEDULE;
      }

    /* If setting the COPIED flag, skip deleted items. */
    if (modify_flags & SVN_WC__ENTRY_MODIFY_COPIED
        && entry->schedule != svn_wc_schedule_delete)
      {
        tmp_entry.copied = copied;
        this_dir_flags |= SVN_WC__ENTRY_MODIFY_COPIED;
      }

    /* Set keep_local on the "this dir", if requested. */
    if (modify_flags & SVN_WC__ENTRY_MODIFY_KEEP_LOCAL)
      {
        tmp_entry.keep_local = keep_local;
        this_dir_flags |= SVN_WC__ENTRY_MODIFY_KEEP_LOCAL;
      }
  }
  else
    {
      return svn_error_return(
        svn_wc__db_temp_forget_directory(db, dir_abspath, pool));
    }

  /* Modify this_dir entry if requested. */
  if (this_dir_flags)
    SVN_ERR(svn_wc__entry_modify2(db, dir_abspath, svn_node_dir, FALSE,
                                  &tmp_entry, this_dir_flags, iterpool));

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
              svn_node_kind_t kind,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool)
{
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  if (kind == svn_node_file)
    SVN_ERR(svn_io_remove_file2(local_abspath, TRUE, scratch_pool));

  else if (kind == svn_node_dir)
    /* This must be a directory or absent */
    {
      const apr_array_header_t *children;
      svn_wc__db_kind_t wc_kind;
      apr_pool_t *iterpool;
      apr_hash_t *versioned_dirs = apr_hash_make(scratch_pool);
      apr_hash_t *unversioned;
      apr_hash_index_t *hi;
      svn_error_t *err;
      int i;

      SVN_ERR(svn_wc__db_read_kind(&wc_kind, db, local_abspath, TRUE,
                                   scratch_pool));

      if (wc_kind != svn_wc__db_kind_dir)
        return SVN_NO_ERROR;

      iterpool = svn_pool_create(scratch_pool);

      SVN_ERR(svn_wc__db_read_children(&children, db, local_abspath,
                                       scratch_pool, iterpool));
      for (i = 0; i < children->nelts; i++)
        {
          const char *name = APR_ARRAY_IDX(children, i, const char *);
          svn_wc__db_status_t status;
          svn_depth_t depth;
          const char *node_abspath;

          svn_pool_clear(iterpool);

          node_abspath = svn_dirent_join(local_abspath, name, iterpool);

          SVN_ERR(svn_wc__db_read_info(&status, &wc_kind, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, &depth,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL,
                                       db, node_abspath, iterpool, iterpool));

          if (status == svn_wc__db_status_absent ||
              status == svn_wc__db_status_not_present ||
              status == svn_wc__db_status_obstructed ||
              status == svn_wc__db_status_obstructed_add ||
              status == svn_wc__db_status_obstructed_delete ||
              status == svn_wc__db_status_excluded ||
              depth == svn_depth_exclude)
            continue; /* Not here */

          /* ### We don't have to record dirs once we have a single database */
          if (wc_kind == svn_wc__db_kind_dir)
            apr_hash_set(versioned_dirs, name, APR_HASH_KEY_STRING, name);

          SVN_ERR(erase_from_wc(db, node_abspath,
                                (wc_kind == svn_wc__db_kind_dir)
                                         ? svn_node_dir
                                         : svn_node_file,
                                cancel_func, cancel_baton,
                                iterpool));
        }

      /* Now handle any remaining unversioned items */
      err = svn_io_get_dirents2(&unversioned, local_abspath, scratch_pool);
      if (err)
        {
          if (APR_STATUS_IS_ENOTDIR(err->apr_err) ||
              APR_STATUS_IS_ENOENT(err->apr_err))
            {
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }
          else
            SVN_ERR(err);
        }

      for (hi = apr_hash_first(scratch_pool, unversioned);
           hi;
           hi = apr_hash_next(hi))
        {
          const void *key;
          const char *name;

          apr_hash_this(hi, &key, NULL, NULL);
          name = key;
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
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *pool)
{
  const char *path;
  svn_wc_adm_access_t *adm_access;

  const svn_wc_entry_t *entry;
  svn_boolean_t was_schedule;
  svn_node_kind_t was_kind;
  svn_boolean_t was_copied;
  svn_boolean_t was_deleted = FALSE; /* Silence a gcc uninitialized warning */
  svn_error_t *err;
  const char *parent_abspath = svn_dirent_dirname(local_abspath, pool);

  SVN_ERR(svn_wc__temp_get_relpath(&path, wc_ctx->db, local_abspath,
                                   pool, pool));

  adm_access = svn_wc__adm_retrieve_internal2(wc_ctx->db, parent_abspath,
                                              pool);

  SVN_ERR_ASSERT(adm_access != NULL);
  err = svn_wc__get_entry(&entry, wc_ctx->db, local_abspath, TRUE,
                          svn_node_unknown, FALSE, pool, pool);

  if (err && err->apr_err == SVN_ERR_NODE_UNEXPECTED_KIND)
    svn_error_clear(err);
  else
    SVN_ERR(err);

  if (!entry)
    {
      if (!keep_local)
        SVN_ERR(erase_unversioned_from_wc(path, cancel_func, cancel_baton,
                pool));

      return SVN_NO_ERROR;
    }

  /* A file external should not be deleted since the file external is
     implemented as a switched file and it would delete the file the
     file external is switched to, which is not the behavior the user
     would probably want. */
  if (entry->file_external_path)
    return svn_error_createf(SVN_ERR_WC_CANNOT_DELETE_FILE_EXTERNAL, NULL,
                             _("Cannot remove the file external at '%s'; "
                               "please propedit or propdel the svn:externals "
                               "description that created it"),
                             svn_dirent_local_style(path, pool));

  /* Note: Entries caching?  What happens to this entry when the entries
     file is updated?  Lets play safe and copy the values */
  was_schedule = entry->schedule;
  was_kind = entry->kind;
  was_copied = entry->copied;

  if (was_kind == svn_node_dir)
    {
      const svn_wc_entry_t *entry_in_parent;

      /* The deleted state is only available in the entry in parent's
         entries file */
      /* We don't need to check for excluded item, since we won't fall into
         this code path in that case. */
      SVN_ERR(svn_wc__get_entry(&entry_in_parent, wc_ctx->db, local_abspath,
                                FALSE, svn_node_dir, TRUE, pool, pool));

      was_deleted = entry_in_parent ? entry_in_parent->deleted : FALSE;

      if (was_schedule == svn_wc_schedule_add && !was_deleted)
        {
          /* Deleting a directory that has been added but not yet
             committed is easy, just remove the administrative dir. */

          SVN_ERR(svn_wc__remove_from_revision_control_internal(
                                           wc_ctx->db,
                                           local_abspath,
                                           FALSE, FALSE,
                                           cancel_func, cancel_baton,
                                           pool));
        }
      else if (was_schedule != svn_wc_schedule_add)
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
              SVN_ERR(mark_tree(wc_ctx->db,
                                local_abspath,
                                SVN_WC__ENTRY_MODIFY_SCHEDULE
                                | SVN_WC__ENTRY_MODIFY_KEEP_LOCAL,
                                svn_wc_schedule_delete, FALSE, keep_local,
                                cancel_func, cancel_baton,
                                notify_func, notify_baton,
                                pool));
            }
        }
      /* else
         ### Handle added directory that is deleted in parent_access
             (was_deleted=TRUE). The current behavior is to just delete the 
             directory with its administrative area inside, which is OK for WC-1.0,
             but when we move to a single database per working copy something
             must unversion the directory. */
    }

  if (!(was_kind == svn_node_dir && was_schedule == svn_wc_schedule_add
        && !was_deleted))
    {
      /* We need to mark this entry for deletion in its parent's entries
         file, so we split off base_name from the parent path, then fold in
         the addition of a delete flag. */
      svn_stringbuf_t *log_accum = svn_stringbuf_create("", pool);
      svn_wc_entry_t tmp_entry;

      /* Edit the entry to reflect the now deleted state.
         entries.c:fold_entry() clears the values of copied, copyfrom_rev
         and copyfrom_url. */
      tmp_entry.schedule = svn_wc_schedule_delete;
      SVN_ERR(svn_wc__loggy_entry_modify(&log_accum,
                                         parent_abspath,
                                         path, &tmp_entry,
                                         SVN_WC__ENTRY_MODIFY_SCHEDULE,
                                         pool, pool));

      /* is it a replacement with history? */
      if (was_schedule == svn_wc_schedule_replace && was_copied)
        {
          const char *text_base, *text_revert;

          SVN_ERR(svn_wc__text_base_path(&text_base, wc_ctx->db, local_abspath,
                                         FALSE, pool));

          SVN_ERR(svn_wc__text_revert_path(&text_revert, wc_ctx->db,
                                           local_abspath, pool));

          if (was_kind != svn_node_dir) /* Dirs don't have text-bases */
            /* Restore the original text-base */
            SVN_ERR(svn_wc__loggy_move(&log_accum,
                                       parent_abspath,
                                       text_revert, text_base,
                                       pool, pool));

          SVN_ERR(svn_wc__loggy_revert_props_restore(&log_accum,
                                                     path, adm_access, pool));
        }
      if (was_schedule == svn_wc_schedule_add)
        SVN_ERR(svn_wc__loggy_props_delete(&log_accum, path,
                                           svn_wc__props_base,
                                           adm_access, pool));

      SVN_ERR(svn_wc__write_log(adm_access, 0, log_accum, pool));

      SVN_ERR(svn_wc__run_log(adm_access, pool));

    }

  /* Report the deletion to the caller. */
  if (notify_func != NULL)
    (*notify_func)(notify_baton,
                   svn_wc_create_notify(path, svn_wc_notify_delete,
                                        pool), pool);

  if (was_schedule == svn_wc_schedule_add && entry &&
      svn_path_is_empty(entry->name))
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
      if (was_schedule == svn_wc_schedule_add)
        SVN_ERR(erase_unversioned_from_wc(local_abspath,
                                          cancel_func, cancel_baton,
                                          pool));
      else
        SVN_ERR(erase_from_wc(wc_ctx->db, local_abspath, was_kind,
                              cancel_func, cancel_baton, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__internal_get_ancestry(const char **url,
                              svn_revnum_t *rev,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  const svn_wc_entry_t *ent;

  SVN_ERR(svn_wc__get_entry(&ent, db, local_abspath, FALSE,
                            svn_node_unknown, FALSE,
                            scratch_pool, scratch_pool));

  if (url)
    *url = apr_pstrdup(result_pool, ent->url);

  if (rev)
    *rev = ent->revision;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_ancestry2(const char **url,
                     svn_revnum_t *rev,
                     svn_wc_context_t *wc_ctx,
                     const char *local_abspath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  return svn_error_return(
    svn_wc__internal_get_ancestry(url, rev, wc_ctx->db, local_abspath,
                                  result_pool, scratch_pool));
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
            apr_pool_t *pool)
{
  const char *parent_dir, *base_name;
  const svn_wc_entry_t *orig_entry, *parent_entry;
  svn_wc_entry_t tmp_entry;
  svn_boolean_t is_replace = FALSE;
  svn_node_kind_t kind;
  apr_uint64_t modify_flags = 0;
  svn_wc_adm_access_t *p_access, *adm_access;
  svn_wc__db_t *db = wc_ctx->db;
  const char *path;

  p_access = svn_wc__adm_retrieve_internal2(db,
                                            svn_dirent_dirname(local_abspath,
                                                               pool),
                                            pool);

  /* Check if we are not adding the working copy root
     'svn wc add --force .' */
  if (p_access == NULL)
    p_access = svn_wc__adm_retrieve_internal2(db, local_abspath, pool);

  SVN_ERR_ASSERT(p_access != NULL);

  path = svn_dirent_join(svn_wc_adm_access_path(p_access),
                         svn_dirent_skip_ancestor(
                                     svn_wc__adm_access_abspath(p_access),
                                     local_abspath),
                         pool);

  SVN_ERR(svn_path_check_valid(local_abspath, pool));

  /* Make sure something's there. */
  SVN_ERR(svn_io_check_path(local_abspath, &kind, pool));
  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("'%s' not found"),
                             svn_dirent_local_style(path, pool));
  if (kind == svn_node_unknown)
    return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                             _("Unsupported node kind for path '%s'"),
                             svn_dirent_local_style(path, pool));

  /* Get the original entry for this path if one exists (perhaps
     this is actually a replacement of a previously deleted thing).

     Note that this is one of the few functions that is allowed to see
     'deleted' entries;  it's totally fine to have an entry that is
     scheduled for addition and still previously 'deleted'.  */
  SVN_ERR(svn_wc_adm_probe_try3(&adm_access, p_access, path,
                                TRUE, copyfrom_url != NULL ? -1 : 0,
                                cancel_func, cancel_baton, pool));
  if (adm_access)
    SVN_ERR(svn_wc_entry(&orig_entry, path, adm_access, TRUE, pool));
  else
    orig_entry = NULL;

  /* You can only add something that is not in revision control, or
     that is slated for deletion from revision control, or has been
     previously 'deleted', unless, of course, you're specifying an
     addition with -history-; then it's okay for the object to be
     under version control already; it's not really new.
     Also, if the target is recorded as excluded from wc, it really
     exists in repos. Report error on this situation too. */
  if (orig_entry)
    {
      if (((! copyfrom_url)
           && (orig_entry->schedule != svn_wc_schedule_delete)
           && (! orig_entry->deleted))
          || (orig_entry->depth == svn_depth_exclude))
        {
          return svn_error_createf
            (SVN_ERR_ENTRY_EXISTS, NULL,
             _("'%s' is already under version control"),
             svn_dirent_local_style(path, pool));
        }
      else if (orig_entry->kind != kind)
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
             svn_dirent_local_style(path, pool),
             svn_dirent_local_style(path, pool));
        }
      if (orig_entry->schedule == svn_wc_schedule_delete)
        is_replace = TRUE;
    }

  /* Split off the base_name from the parent directory. */
  svn_dirent_split(path, &parent_dir, &base_name, pool);
  SVN_ERR(svn_wc_entry(&parent_entry, parent_dir, p_access, FALSE,
                       pool));
  if (! parent_entry)
    return svn_error_createf
      (SVN_ERR_ENTRY_NOT_FOUND, NULL,
       _("Can't find parent directory's entry while trying to add '%s'"),
       svn_dirent_local_style(path, pool));
  if (svn_wc_is_adm_dir(base_name, pool))
    return svn_error_createf
      (SVN_ERR_ENTRY_FORBIDDEN, NULL,
       _("Can't create an entry with a reserved name while trying to add '%s'"),
       svn_dirent_local_style(path, pool));
  if (parent_entry->schedule == svn_wc_schedule_delete)
    return svn_error_createf
      (SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
       _("Can't add '%s' to a parent directory scheduled for deletion"),
       svn_dirent_local_style(path, pool));

  /* Init the modify flags. */
  modify_flags = SVN_WC__ENTRY_MODIFY_SCHEDULE | SVN_WC__ENTRY_MODIFY_KIND;
  if (! (is_replace || copyfrom_url))
    modify_flags |= SVN_WC__ENTRY_MODIFY_REVISION;

  /* If a copy ancestor was given, make sure the copyfrom URL is in the same
     repository (if possible) and put the proper ancestry info in the new
     entry */
  if (copyfrom_url)
    {
      if (parent_entry->repos
          && ! svn_uri_is_ancestor(parent_entry->repos, copyfrom_url))
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

  tmp_entry.revision = 0;
  tmp_entry.kind = kind;
  tmp_entry.schedule = svn_wc_schedule_add;

  /* Now, add the entry for this item to the parent_dir's
     entries file, marking it for addition. */
  SVN_ERR(svn_wc__entry_modify(p_access, base_name, &tmp_entry,
                               modify_flags, pool));


  /* If this is a replacement without history, we need to reset the
     properties for PATH. */
  /* ### this is totally bogus. we clear these cuz turds might have been
     ### left around. thankfully, this will be properly managed during the
     ### wc-ng upgrade process. for now, we try to compensate... */
  if (copyfrom_url == NULL)
    SVN_ERR(svn_wc__props_delete(db, local_abspath, svn_wc__props_working,
                                 pool));

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
      SVN_ERR(svn_wc__wq_prepare_revert_files(db, local_abspath, pool));
      SVN_ERR(svn_wc__wq_run(db, local_abspath,
                             cancel_func, cancel_baton, pool));
    }

  if (kind == svn_node_dir) /* scheduling a directory for addition */
    {

      if (! copyfrom_url)
        {
          const svn_wc_entry_t *p_entry; /* ### why not use parent_entry? */
          const char *new_url;

          /* Get the entry for this directory's parent.  We need to snatch
             the ancestor path out of there. */
          SVN_ERR(svn_wc_entry(&p_entry, parent_dir, p_access, FALSE,
                               pool));

          /* Derive the parent path for our new addition here. */
          new_url = svn_path_url_add_component2(p_entry->url, base_name, pool);

          /* Make sure this new directory has an admistrative subdirectory
             created inside of it */
          SVN_ERR(svn_wc__internal_ensure_adm(db, local_abspath, p_entry->uuid,
                                              new_url, p_entry->repos, 0,
                                              depth, pool));
        }
      else
        {
          /* When we are called with the copyfrom arguments set and with
             the admin directory already in existence, then the dir will
             contain the copyfrom settings.  So we need to pass the
             copyfrom arguments to the ensure call. */
          SVN_ERR(svn_wc__internal_ensure_adm(db, local_abspath,
                                              parent_entry->uuid, copyfrom_url,
                                              parent_entry->repos,
                                              copyfrom_rev, depth, pool));
        }

      /* We want the locks to persist, so use the access baton's pool */
      if (! orig_entry || orig_entry->deleted)
        {
          apr_pool_t* access_pool = svn_wc_adm_access_pool(p_access);
          SVN_ERR(svn_wc_adm_open3(&adm_access, p_access, path,
                                   TRUE, copyfrom_url != NULL ? -1 : 0,
                                   cancel_func, cancel_baton,
                                   access_pool));
        }

      /* We're making the same mods we made above, but this time we'll
         force the scheduling.  Also make sure to undo the
         'incomplete' flag which svn_wc__internal_ensure_adm() sets by
         default. */
      modify_flags |= SVN_WC__ENTRY_MODIFY_FORCE;
      modify_flags |= SVN_WC__ENTRY_MODIFY_INCOMPLETE;
      tmp_entry.schedule = is_replace
                           ? svn_wc_schedule_replace
                           : svn_wc_schedule_add;
      tmp_entry.incomplete = FALSE;
      SVN_ERR(svn_wc__entry_modify(adm_access, NULL, &tmp_entry,
                                   modify_flags, pool));

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

          /* Figure out what the new url should be. */
          const char *new_url =
            svn_path_url_add_component2(parent_entry->url, base_name, pool);

          /* Change the entry urls recursively (but not the working rev). */
          SVN_ERR(svn_wc__do_update_cleanup(db, local_abspath,
                                            depth, new_url,
                                            parent_entry->repos,
                                            SVN_INVALID_REVNUM, NULL,
                                            NULL, FALSE, apr_hash_make(pool),
                                            pool));

          /* Recursively add the 'copied' existence flag as well!  */
          SVN_ERR(mark_tree(db, svn_wc__adm_access_abspath(adm_access),
                            SVN_WC__ENTRY_MODIFY_COPIED,
                            svn_wc_schedule_normal, TRUE, FALSE,
                            cancel_func,
                            cancel_baton,
                            NULL, NULL, /* N/A cuz we aren't deleting */
                            pool));

          /* Clean out the now-obsolete dav cache values.  */
          SVN_ERR(svn_wc__db_base_set_dav_cache(db, local_abspath, NULL,
                                                pool));
        }
    }

  /* Report the addition to the caller. */
  if (notify_func != NULL)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(path, svn_wc_notify_add,
                                                     pool);
      notify->kind = kind;
      (*notify_func)(notify_baton, notify, pool);
    }

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


/* Revert PATH of on-disk KIND.  ENTRY is the working copy entry for
   PATH.  *DEPTH is the depth of the reversion crawl the caller is
   using; this function may choose to override that value as needed.

   See svn_wc_revert3() for the interpretations of PARENT_ACCESS,
   USE_COMMIT_TIMES, CANCEL_FUNC and CANCEL_BATON.

   Set *DID_REVERT to true if actually reverting anything, else do not
   touch *DID_REVERT.

   Use POOL for allocations.
 */
static svn_error_t *
revert_entry(svn_depth_t *depth,
             svn_wc__db_t *db,
             const char *local_abspath,
             svn_node_kind_t kind,
             const svn_wc_entry_t *entry,
             svn_boolean_t use_commit_times,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             svn_boolean_t *did_revert,
             apr_pool_t *pool)
{

  /* Initialize this even though revert_admin_things() is guaranteed
     to set it, because we don't know that revert_admin_things() will
     be called. */
  svn_boolean_t reverted = FALSE;

  /* Additions. */
  if (entry->schedule == svn_wc_schedule_add)
    {
      /* Before removing item from revision control, notice if the
         entry is in a 'deleted' state; this is critical for
         directories, where this state only exists in its parent's
         entry. */
      svn_boolean_t was_deleted = FALSE;

      if (entry->kind == svn_node_file)
        {
          was_deleted = entry->deleted;
          SVN_ERR(svn_wc__remove_from_revision_control_internal(db,
                                                                local_abspath,
                                                                FALSE, FALSE,
                                                                cancel_func,
                                                                cancel_baton,
                                                                pool));
        }
      else if (entry->kind == svn_node_dir)
        {
          const char *path;
          SVN_ERR(svn_wc__temp_get_relpath(&path, db, local_abspath,
                                           pool, pool));

          /* We are trying to revert the current directory which is
             scheduled for addition. This is supposed to fail (Issue #854) */
          if (path[0] == '\0')
            return svn_error_create(SVN_ERR_WC_INVALID_OP_ON_CWD, NULL,
                                    _("Cannot revert addition of current "
                                      "directory; please try again from the "
                                      "parent directory"));

          /* We don't need to check for excluded item, since we won't fall
             into this code path in that case. */

          SVN_ERR(svn_wc__node_is_deleted(&was_deleted, db, local_abspath,
                                          pool));

          if (kind == svn_node_none
              || svn_wc__adm_missing(db, local_abspath, pool))
            {
              /* Schedule add but missing, just remove the entry
                 or it's missing an adm area in which case
                 svn_wc_adm_probe_retrieve() returned the parent's
                 adm_access, for which we definitely can't use the 'else'
                 code path (as it will remove the parent from version
                 control... (See issue 2425) */
              SVN_ERR(svn_wc__entry_remove(db, local_abspath, pool));
            }
          else
            {
              SVN_ERR(svn_wc__remove_from_revision_control_internal(
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
          svn_wc_entry_t tmp_entry;

          /* ### this is some pretty bogus stuff here. for directories,
             ### this will mark the stub as DELETED. the kind will be
             ### unchanged. for files, the entry has been removed above,
             ### so this will *RE-INSERT* an entry for this node, marked
             ### as a "file" that is DELETED. lovely, how entry_modify(2)
             ### can actually insert new nodes...  */
          tmp_entry.kind = entry->kind;
          tmp_entry.deleted = TRUE;

          if (entry->kind == svn_node_dir)
            {
              SVN_ERR(svn_wc__entry_modify2(db, local_abspath,
                                            svn_node_dir, TRUE, &tmp_entry,
                                            SVN_WC__ENTRY_MODIFY_KIND
                                            | SVN_WC__ENTRY_MODIFY_DELETED,
                                            pool));
            }
          else
            {
              SVN_ERR(svn_wc__entry_modify2(db, local_abspath,
                                            svn_node_file, FALSE, &tmp_entry,
                                            SVN_WC__ENTRY_MODIFY_KIND
                                            | SVN_WC__ENTRY_MODIFY_DELETED,
                                            pool));
            }
        }
    }
  /* Regular prop and text edit. */
  /* Deletions and replacements. */
  else if (entry->schedule == svn_wc_schedule_normal
           || entry->schedule == svn_wc_schedule_delete
           || entry->schedule == svn_wc_schedule_replace)
    {
      /* Revert the prop and text mods (if any). */
      SVN_ERR(revert_admin_things(&reverted, db, local_abspath,
                                  use_commit_times, pool));

      /* Force recursion on replaced directories. */
      if (entry->kind == svn_node_dir
          && entry->schedule == svn_wc_schedule_replace)
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
  svn_node_kind_t kind;
  const svn_wc_entry_t *entry;
  svn_wc_conflict_description2_t *tree_conflict;
  const char *path;
  svn_error_t *err;

  SVN_ERR(svn_wc__temp_get_relpath(&path, db, local_abspath, pool, pool));

  /* Check cancellation here, so recursive calls get checked early. */
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));


  /* Safeguard 1: the item must be versioned for any reversion to make sense,
     except that a tree conflict can exist on an unversioned item. */
  err = svn_wc__get_entry(&entry, db, local_abspath, TRUE, svn_node_unknown,
                          FALSE, pool, pool);

  if (err && err->apr_err == SVN_ERR_NODE_UNEXPECTED_KIND)
    svn_error_clear(err);
  else
    SVN_ERR(err);

  SVN_ERR(svn_wc__db_op_read_tree_conflict(&tree_conflict, db, local_abspath,
                                           pool, pool));
  if (entry == NULL && tree_conflict == NULL)
    return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                             _("Cannot revert unversioned item '%s'"), path);

  /* Safeguard 1.5:  is this a missing versioned directory? */
  SVN_ERR(svn_io_check_path(local_abspath, &kind, pool));
  if (entry && (entry->kind == svn_node_dir))
    {
      if ((kind != svn_node_dir) && (entry->schedule != svn_wc_schedule_add))
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
  if (entry && (entry->kind != svn_node_file) && (entry->kind != svn_node_dir))
    return svn_error_createf
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("Cannot revert '%s': unsupported entry node kind"),
       svn_dirent_local_style(local_abspath, pool));

  /* Safeguard 3:  can we deal with the node kind of PATH currently in
     the working copy? */
  if ((kind != svn_node_none)
      && (kind != svn_node_file)
      && (kind != svn_node_dir))
    return svn_error_createf
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("Cannot revert '%s': unsupported node kind in working copy"),
       svn_dirent_local_style(local_abspath, pool));

  /* If the entry passes changelist filtering, revert it!  */
  if (svn_wc__internal_changelist_match(db, local_abspath, changelist_hash,
                                        pool))
    {
      svn_boolean_t reverted = FALSE;
      svn_wc_conflict_description2_t *conflict;

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
      if (entry)
        SVN_ERR(revert_entry(&depth, db, local_abspath, kind, entry,
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
  if (entry && entry->kind == svn_node_dir && depth > svn_depth_empty)
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
          svn_wc__db_kind_t db_kind;

          svn_pool_clear(iterpool);

          node_abspath = svn_dirent_join(local_abspath, name, iterpool);

          SVN_ERR(svn_wc__db_node_hidden(&hidden, db, node_abspath, iterpool));

          if (hidden)
            continue;

          apr_hash_set(nodes, name, APR_HASH_KEY_STRING, name);

          SVN_ERR(svn_wc__db_read_kind(&db_kind, db, node_abspath, FALSE,
                                       iterpool));

          /* Skip subdirectories if we're called with depth-files. */
          if ((depth == svn_depth_files) && 
              (db_kind != svn_wc__db_kind_file) &&
              (db_kind != svn_wc__db_kind_symlink))
            continue;

          /* Revert the entry. */
          SVN_ERR(revert_internal(db, node_abspath,
                                  depth_under_here, use_commit_times,
                                  changelist_hash, cancel_func, cancel_baton,
                                  notify_func, notify_baton, iterpool));
        }

      /* Visit any unversioned children that are tree conflict victims. */
      {
        apr_hash_t *conflicts;
        apr_hash_index_t *hi2;

        /* Loop through all the tree conflict victims */
        SVN_ERR(svn_wc__read_tree_conflicts(&conflicts,
                                            entry->tree_conflict_data,
                                            path, pool));

        for (hi2 = apr_hash_first(pool, conflicts); hi2;
                                                     hi2 = apr_hash_next(hi2))
          {
            const svn_wc_conflict_description2_t *conflict =
                                                svn_apr_hash_index_val(hi2);

            svn_pool_clear(iterpool);

            /* If this victim is not in this dir's entries ... */
            if (apr_hash_get(nodes,
                             svn_dirent_basename(conflict->local_abspath,
                                                 pool),
                             APR_HASH_KEY_STRING) == NULL)
              {
                /* Revert the entry. */
                SVN_ERR(revert_internal(db, conflict->local_abspath,
                                        svn_depth_empty,
                                        use_commit_times, changelist_hash,
                                        cancel_func, cancel_baton,
                                        notify_func, notify_baton, iterpool));
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
  svn_wc_context_t *wc_ctx;
  const char *local_abspath;

  SVN_ERR(svn_wc_context_create(&wc_ctx, NULL, pool, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(svn_wc__text_base_path(pristine_path, wc_ctx->db, local_abspath,
                                          FALSE, pool));
  return SVN_NO_ERROR;
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
svn_wc__get_pristine_contents(svn_stream_t **contents,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  const char *text_base;
  
  SVN_ERR(svn_wc__text_base_path(&text_base, db, local_abspath, FALSE,
                                 scratch_pool));

  if (text_base == NULL)
    {
      *contents = NULL;
      return SVN_NO_ERROR;
    }

  return svn_stream_open_readonly(contents, text_base, result_pool,
                                  scratch_pool);
}


svn_error_t *
svn_wc__remove_from_revision_control_internal(svn_wc__db_t *db,
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
      const char *text_base_file;

      /* Only check if the file was modified when it wasn't overwritten with a
         special file */
      SVN_ERR(svn_wc__get_special(&wc_special, db, local_abspath,
                                  scratch_pool));
      SVN_ERR(svn_io_check_special_path(local_abspath, &on_disk,
                                        &local_special, scratch_pool));
      if (wc_special || ! local_special)
        {
          /* Check for local mods. before removing entry */
          SVN_ERR(svn_wc__text_modified_internal_p(&text_modified_p, db,
                                                   local_abspath, FALSE,
                                                   TRUE, scratch_pool));
          if (text_modified_p && instant_error)
            return svn_error_createf(SVN_ERR_WC_LEFT_LOCAL_MOD, NULL,
                   _("File '%s' has local modifications"),
                   svn_dirent_local_style(local_abspath, scratch_pool));
        }

      SVN_ERR(svn_wc__text_base_path(&text_base_file, db, local_abspath,
                                     FALSE, scratch_pool));

      /* Clear the dav cache.  */
      /* ### one day... (now?) this will simply be part of removing the
         ### BASE_NODE row.  */
      SVN_ERR(svn_wc__db_base_set_dav_cache(db, local_abspath, NULL,
                                            scratch_pool));

      /* Remove prop/NAME, prop-base/NAME.svn-base. */
      SVN_ERR(svn_wc__props_delete(db, local_abspath, svn_wc__props_working,
                                   scratch_pool));
      SVN_ERR(svn_wc__props_delete(db, local_abspath, svn_wc__props_base,
                                   scratch_pool));

      /* Remove NAME from PATH's entries file: */
      SVN_ERR(svn_wc__entry_remove(db, local_abspath, scratch_pool));

      /* Remove text-base/NAME.svn-base */
      SVN_ERR(svn_io_remove_file2(text_base_file,
                                  TRUE, scratch_pool));

      /* If we were asked to destroy the working file, do so unless
         it has local mods. */
      if (destroy_wf)
        {
          /* Don't kill local mods. */
          if (text_modified_p || (! wc_special && local_special))
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
      SVN_ERR(svn_wc__entry_remove(db, local_abspath, scratch_pool));
    }
  else /* looking at THIS_DIR */
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      const apr_array_header_t *children;
      svn_wc_entry_t incomplete_entry;
      int i;

      /* ### sanity check:  check 2 places for DELETED flag? */

      /* Get rid of the dav cache for this directory.  */
      SVN_ERR(svn_wc__db_base_set_dav_cache(db, local_abspath, NULL,
                                            iterpool));

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
              SVN_ERR(svn_wc__entry_remove(db, entry_abspath, iterpool));
              continue;
            }

          err = svn_wc__remove_from_revision_control_internal(
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

        SVN_ERR(svn_wc__check_wc_root(&is_root, NULL, db, local_abspath,
                                      iterpool));

        /* If full_path is not the top of a wc, then its parent
           directory is also a working copy and has an entry for
           full_path.  We need to remove that entry: */
        if (! is_root)
          {
            const svn_wc_entry_t *dir_entry;

            SVN_ERR(svn_wc__get_entry(&dir_entry, db, local_abspath, FALSE,
                                      svn_node_dir, TRUE, iterpool, iterpool));

            /* An exception: When the path is at svn_depth_exclude,
               the entry in the parent directory should be preserved
               for bookkeeping purpose. This only happens when the
               function is called by svn_wc_crop_tree(). */
            if (dir_entry
                && dir_entry->depth != svn_depth_exclude)
              {
                SVN_ERR(svn_wc__entry_remove(db, local_abspath, iterpool));
              }
          }
      }

      /* Remove the entire administrative .svn area, thereby removing
         _this_ dir from revision control too.  */
      {
        svn_wc_adm_access_t *adm_access =
            svn_wc__adm_retrieve_internal2(db, local_abspath, iterpool);

        SVN_ERR(svn_wc__adm_destroy(adm_access, iterpool));
      }

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
      svn_wc__remove_from_revision_control_internal(wc_ctx->db,
                                                    local_abspath,
                                                    destroy_wf,
                                                    instant_error,
                                                    cancel_func,
                                                    cancel_baton,
                                                    scratch_pool));
}



/*** Resolving a conflict automatically ***/


/* Helper for resolve_conflict_on_entry.  Delete the file BASE_NAME in
   PARENT_DIR if it exists.  Set WAS_PRESENT to TRUE if the file existed,
   and to FALSE otherwise. */
static svn_error_t *
attempt_deletion(const char *parent_dir,
                 const char *base_name,
                 svn_boolean_t *was_present,
                 apr_pool_t *pool)
{
  const char *full_path = svn_dirent_join(parent_dir, base_name, pool);
  svn_error_t *err = svn_io_remove_file2(full_path, FALSE, pool);

  *was_present = ! err || ! APR_STATUS_IS_ENOENT(err->apr_err);
  if (*was_present)
    return err;

  svn_error_clear(err);
  return SVN_NO_ERROR;
}


/* Conflict resolution involves removing the conflict files, if they exist,
   and clearing the conflict filenames from the entry.  The latter needs to
   be done whether or not the conflict files exist.

   Tree conflicts are not resolved here, because the data stored in one
   entry does not refer to that entry but to children of it.

   PATH is the path to the item to be resolved, BASE_NAME is the basename
   of PATH, and CONFLICT_DIR is the access baton for PATH.  ORIG_ENTRY is
   the entry prior to resolution. RESOLVE_TEXT and RESOLVE_PROPS are TRUE
   if text and property conflicts respectively are to be resolved.

   If this call marks any conflict as resolved, set *DID_RESOLVE to true,
   else do not change *DID_RESOLVE.

   See svn_wc_resolved_conflict3() for how CONFLICT_CHOICE behaves.

   ### FIXME: This function should be loggy, otherwise an interruption can
   ### leave, for example, one of the conflict artifact files deleted but
   ### the entry still referring to it and trying to use it for the next
   ### attempt at resolving.
*/
static svn_error_t *
resolve_conflict_on_node(svn_wc__db_t *db,
                         const char *local_abspath,
                         svn_boolean_t resolve_text,
                         svn_boolean_t resolve_props,
                         svn_wc_conflict_choice_t conflict_choice,
                         svn_boolean_t *did_resolve,
                         apr_pool_t *pool)
{
  svn_boolean_t was_present, need_feedback = FALSE;
  apr_uint64_t modify_flags = 0;
  svn_wc_entry_t tmp_entry;
  const char *conflict_old;
  const char *conflict_new;
  const char *conflict_working;
  const char *prop_reject_file;
  svn_wc__db_kind_t kind;
  const char *conflict_dir_abspath;

  SVN_ERR(svn_wc__db_read_info(NULL, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, &conflict_old, &conflict_new,
                               &conflict_working, &prop_reject_file,
                               NULL, NULL,
                               db, local_abspath, pool, pool));

  if (kind == svn_wc__db_kind_dir)
    conflict_dir_abspath = local_abspath;
  else
    conflict_dir_abspath = svn_dirent_dirname(local_abspath, pool);

  if (resolve_text)
    {
      const char *auto_resolve_src;

      /* Handle automatic conflict resolution before the temporary files are
       * deleted, if necessary. */
      switch (conflict_choice)
        {
        case svn_wc_conflict_choose_base:
          auto_resolve_src = conflict_old;
          break;
        case svn_wc_conflict_choose_mine_full:
          auto_resolve_src = conflict_working;
          break;
        case svn_wc_conflict_choose_theirs_full:
          auto_resolve_src = conflict_new;
          break;
        case svn_wc_conflict_choose_merged:
          auto_resolve_src = NULL;
          break;
        case svn_wc_conflict_choose_theirs_conflict:
        case svn_wc_conflict_choose_mine_conflict:
          {
            if (conflict_old && conflict_working && conflict_new)
              {
                apr_file_t *tmp_f;
                svn_stream_t *tmp_stream;
                svn_diff_t *diff;
                svn_diff_conflict_display_style_t style =
                  conflict_choice == svn_wc_conflict_choose_theirs_conflict
                  ? svn_diff_conflict_display_latest
                  : svn_diff_conflict_display_modified;

                SVN_ERR(svn_wc_create_tmp_file2(&tmp_f,
                                                &auto_resolve_src,
                                                conflict_dir_abspath,
                                                svn_io_file_del_none,
                                                pool));
                tmp_stream = svn_stream_from_aprfile2(tmp_f, FALSE, pool);
                SVN_ERR(svn_diff_file_diff3_2(&diff,
                                              conflict_old,
                                              conflict_working,
                                              conflict_new,
                                              svn_diff_file_options_create(pool),
                                              pool));
                SVN_ERR(svn_diff_file_output_merge2(tmp_stream, diff,
                                                    conflict_old,
                                                    conflict_working,
                                                    conflict_new,
                                                    /* markers ignored */
                                                    NULL, NULL, NULL, NULL,
                                                    style,
                                                    pool));
                SVN_ERR(svn_stream_close(tmp_stream));
              }
            else
              auto_resolve_src = NULL;
            break;
          }
        default:
          return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                                  _("Invalid 'conflict_result' argument"));
        }

      if (auto_resolve_src)
        SVN_ERR(svn_io_copy_file(
          svn_dirent_join(conflict_dir_abspath, auto_resolve_src, pool),
          local_abspath, TRUE, pool));
    }

  /* Yes indeed, being able to map a function over a list would be nice. */
  if (resolve_text && conflict_old)
    {
      SVN_ERR(attempt_deletion(conflict_dir_abspath, conflict_old,
                               &was_present, pool));
      modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_OLD;
      tmp_entry.conflict_old = NULL;
      need_feedback |= was_present;
    }
  if (resolve_text && conflict_new)
    {
      SVN_ERR(attempt_deletion(conflict_dir_abspath, conflict_new,
                               &was_present, pool));
      modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_NEW;
      tmp_entry.conflict_new = NULL;
      need_feedback |= was_present;
    }
  if (resolve_text && conflict_working)
    {
      SVN_ERR(attempt_deletion(conflict_dir_abspath, conflict_working,
                               &was_present, pool));
      modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_WRK;
      tmp_entry.conflict_wrk = NULL;
      need_feedback |= was_present;
    }
  if (resolve_props && prop_reject_file)
    {
      SVN_ERR(attempt_deletion(conflict_dir_abspath, prop_reject_file,
                               &was_present, pool));
      modify_flags |= SVN_WC__ENTRY_MODIFY_PREJFILE;
      tmp_entry.prejfile = NULL;
      need_feedback |= was_present;
    }

  if (modify_flags)
    {
      /* Although removing the files is sufficient to indicate that the
         conflict is resolved, if we update the entry as well future checks
         for conflict state will be more efficient. */
      SVN_ERR(svn_wc__entry_modify2(db, local_abspath, svn_node_unknown,
                                    FALSE, &tmp_entry, modify_flags, pool));

      /* No feedback if no files were deleted and all we did was change the
         entry, such a file did not appear as a conflict */
      if (need_feedback)
        *did_resolve = TRUE;
    }

  return SVN_NO_ERROR;
}

/* Machinery for an automated entries walk... */

struct resolve_callback_baton
{
  svn_wc__db_t *db;

  /* TRUE if text conflicts are to be resolved. */
  svn_boolean_t resolve_text;
  /* TRUE if property conflicts are to be resolved. */
  svn_boolean_t resolve_props;
  /* TRUE if tree conflicts are to be resolved. */
  svn_boolean_t resolve_tree;
  /* The type of automatic conflict resolution to perform */
  svn_wc_conflict_choice_t conflict_choice;
  /* Notification function and baton */
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
};

/* An svn_wc_entry_callbacks2_t callback function.
 *
 * Mark as resolved any tree conflict on PATH, even if ENTRY is null, and
 * any other conflicts on (PATH, ENTRY). Send a notification if any such
 * change is made. Ignore the entry if it is deleted or absent, or if it is
 * a duplicate report of a directory.
 *
 * Do this all according to the contents of (struct resolve_callback_baton
 * *)WALK_BATON.
 */
static svn_error_t *
resolve_found_entry_callback(const char *path,
                             const svn_wc_entry_t *entry,
                             void *walk_baton,
                             apr_pool_t *pool)
{
  struct resolve_callback_baton *baton = walk_baton;
  svn_boolean_t resolved = FALSE;
  svn_boolean_t wc_root = FALSE;
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  /* We're going to receive dirents twice;  we want to ignore the
     first one (where it's a child of a parent dir), and only process
     the second one (where we're looking at THIS_DIR). */
  if (entry && (entry->kind == svn_node_dir)
      && (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) != 0))
    return SVN_NO_ERROR;


  /* Make sure we do not end up looking for tree conflict
   * info above the working copy root. */

  if (entry && (entry->kind == svn_node_dir))
    {
      SVN_ERR(svn_wc__check_wc_root(&wc_root, NULL, baton->db, local_abspath,
                                    pool));

      if (wc_root)
        {
          /* Switched subtrees are considered working copy roots by
           * svn_wc__check_wc_root(). But it's OK to check for tree conflict
           * info in the parent of a switched subtree, because the
           * subtree itself might be a tree conflict victim. */
          svn_boolean_t switched;
          svn_error_t *err;
          err = svn_wc__internal_path_switched(&switched, baton->db,
                                               local_abspath, pool);
          if (err && (err->apr_err == SVN_ERR_ENTRY_MISSING_URL))
            svn_error_clear(err);
          else
            {
              SVN_ERR(err);
              wc_root = ! switched;
            }
        }
    }

  /* If asked to, clear any tree conflict on the path.
   * If the target is a working copy root, don't check on the target itself.*/
  if (baton->resolve_tree && ! wc_root) /* but possibly a switched subdir */
    {
      svn_wc_conflict_description2_t *conflict;
      svn_boolean_t tree_conflict;

      SVN_ERR(svn_wc__db_op_read_tree_conflict(&conflict, baton->db,
                                               local_abspath, pool, pool));
      if (conflict)
        {
          svn_error_t *err;

          /* For now, we only clear tree conflict information and resolve
           * to the working state. There is no way to pick theirs-full
           * or mine-full, etc. Throw an error if the user expects us
           * to be smarter than we really are. */
          if (baton->conflict_choice != svn_wc_conflict_choose_merged)
            {
              return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                                       NULL,
                                       _("Tree conflicts can only be resolved "
                                         "to 'working' state; "
                                         "'%s' not resolved"),
                                       svn_dirent_local_style(local_abspath,
                                                              pool));
            }

          SVN_ERR(svn_wc__db_op_set_tree_conflict(baton->db, local_abspath,
                                                  NULL, pool));

          /* Sanity check:  see if libsvn_wc *still* thinks this item is in a
             state of conflict that we have asked to resolve. If so,
             don't flag RESOLVED_TREE after all. */

          err = svn_wc__internal_conflicted_p(NULL, NULL, &tree_conflict,
                                              baton->db, local_abspath,
                                              pool);
          if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
            {
              svn_error_clear(err);
              tree_conflict = FALSE;
            }
          else if (err)
            return err;

          if (!tree_conflict)
            resolved = TRUE;
        }
    }


  /* If this is a versioned entry, resolve its other conflicts, if any. */
  if (entry && (baton->resolve_text || baton->resolve_props))
    {
      svn_boolean_t did_resolve = FALSE;

      SVN_ERR(resolve_conflict_on_node(baton->db, local_abspath,
                                       baton->resolve_text,
                                       baton->resolve_props,
                                       baton->conflict_choice,
                                       &did_resolve, pool));

      /* Sanity check: see if libsvn_wc *still* thinks this item is in a
         state of conflict that we have asked to resolve. If so,
         don't flag RESOLVED after all. */
      if (did_resolve)
        {
          svn_boolean_t text_conflict, prop_conflict;
          svn_error_t *err;

          err = svn_wc__internal_conflicted_p(&text_conflict, &prop_conflict,
                                              NULL, baton->db, local_abspath,
                                              pool);

          if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
            {
              svn_error_clear(err);
              text_conflict = FALSE;
              prop_conflict = FALSE;
            }
          else if (err)
            return err;

          if ((baton->resolve_text && text_conflict)
              || (baton->resolve_props && prop_conflict))
            /* Explicitly overwrite a possible TRUE from tree-conflict
             * resolution. If this part failed, it shouldn't be notified
             * as resolved. (Keeping in an "if...else" for clarity.)
             * (Actually, we defined that a node can't have other conflicts
             * when it is tree-conflicted. This here can't hurt though.)*/
            resolved = FALSE;
          else
            resolved = TRUE;
        }
    }

  /* Notify */
  if (baton->notify_func && resolved)
    (*baton->notify_func)(baton->notify_baton,
                          svn_wc_create_notify(path, svn_wc_notify_resolved,
                                               pool),
                          pool);

  return SVN_NO_ERROR;
}

static const svn_wc_entry_callbacks2_t
resolve_walk_callbacks =
  {
    resolve_found_entry_callback,
    svn_wc__walker_default_error_handler
  };


/* The public function */
svn_error_t *
svn_wc_resolved_conflict4(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t resolve_text,
                          svn_boolean_t resolve_props,
                          svn_boolean_t resolve_tree,
                          svn_depth_t depth,
                          svn_wc_conflict_choice_t conflict_choice,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *pool)
{
  struct resolve_callback_baton rcb;

  rcb.db = svn_wc__adm_get_db(adm_access);
  rcb.resolve_text = resolve_text;
  rcb.resolve_props = resolve_props;
  rcb.resolve_tree = resolve_tree;
  rcb.conflict_choice = conflict_choice;
  rcb.notify_func = notify_func;
  rcb.notify_baton = notify_baton;

  return svn_wc__walk_entries_and_tc(path, adm_access,
                                     &resolve_walk_callbacks, &rcb, depth,
                                     cancel_func, cancel_baton, pool);
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
                               NULL, NULL, NULL, NULL, NULL, NULL,
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

  /* If the path is already a member of a changelist, warn the
     user about this, but still allow the reassignment to happen. */
  if (existing_changelist && changelist && notify_func)
    {
      svn_error_t *reassign_err =
        svn_error_createf(SVN_ERR_WC_CHANGELIST_MOVE, NULL,
                          _("Removing '%s' from changelist '%s'."),
                          local_abspath, existing_changelist);
      notify = svn_wc_create_notify(local_abspath,
                                    svn_wc_notify_changelist_moved,
                                    scratch_pool);
      notify->err = reassign_err;
      notify_func(notify_baton, notify, scratch_pool);
      svn_error_clear(notify->err);
    }

  /* Set the changelist. */
  SVN_ERR(svn_wc__db_op_set_changelist(wc_ctx->db, local_abspath, changelist,
                                       scratch_pool));

  /* And tell someone what we've done. */
  if (notify_func)
    {
      notify = svn_wc_create_notify(local_abspath,
                                    changelist
                                    ? svn_wc_notify_changelist_set
                                    : svn_wc_notify_changelist_clear,
                                    scratch_pool);
      notify->changelist_name = changelist;
      notify_func(notify_baton, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__set_file_external_location(svn_wc_adm_access_t *adm_access,
                                   const char *name,
                                   const char *url,
                                   const svn_opt_revision_t *peg_rev,
                                   const svn_opt_revision_t *rev,
                                   const char *repos_root_url,
                                   apr_pool_t *pool)
{
  svn_wc_entry_t entry = { 0 };

  if (url)
    {
      /* A repository root relative path is stored in the entry. */
      SVN_ERR_ASSERT(peg_rev);
      SVN_ERR_ASSERT(rev);
      entry.file_external_path = url + strlen(repos_root_url);
      entry.file_external_peg_rev = *peg_rev;
      entry.file_external_rev = *rev;
    }
  else
    {
      entry.file_external_path = NULL;
      entry.file_external_peg_rev.kind = svn_opt_revision_unspecified;
      entry.file_external_rev.kind = svn_opt_revision_unspecified;
    }

  SVN_ERR(svn_wc__entry_modify(adm_access, name, &entry,
                               SVN_WC__ENTRY_MODIFY_FILE_EXTERNAL, pool));

  return SVN_NO_ERROR;
}


svn_boolean_t
svn_wc__internal_changelist_match(svn_wc__db_t *db,
                                  const char *local_abspath,
                                  const apr_hash_t *clhash,
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
                             NULL, NULL, NULL, NULL, NULL, NULL,
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
                         const apr_hash_t *clhash,
                         apr_pool_t *scratch_pool)
{
  return svn_wc__internal_changelist_match(wc_ctx->db, local_abspath, clhash,
                                           scratch_pool);
}
