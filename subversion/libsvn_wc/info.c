/**
 * @copyright
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
 * @endcopyright
 */

#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_wc.h"

#include "wc.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"



/* Set *INFO to a new struct, allocated in RESULT_POOL, built from the WC
   metadata of LOCAL_ABSPATH.  Pointer fields are copied by reference, not
   dup'd. */
static svn_error_t *
build_info_for_entry(svn_info2_t **info,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     svn_node_kind_t kind,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_info2_t *tmpinfo;
  svn_boolean_t is_copy_target;
  svn_boolean_t exclude = FALSE;
  svn_boolean_t is_copy;
  svn_revnum_t rev;
  const char *repos_relpath;

  if (kind == svn_node_none)
    {
      svn_wc__db_status_t status;
      svn_error_t *err = svn_wc__db_read_info(&status, NULL, NULL, NULL, NULL,
                                              NULL, NULL, NULL, NULL, NULL,
                                              NULL, NULL, NULL, NULL, NULL,
                                              NULL, NULL, NULL, NULL, NULL,
                                              NULL, NULL, NULL, NULL, NULL,
                                              NULL, NULL,
                                              db, local_abspath,
                                              scratch_pool, scratch_pool);

      if ((! err) && (status == svn_wc__db_status_excluded))
        exclude = TRUE;

      svn_error_clear(err);
      if (! exclude)
        return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                 _("The node '%s' was not found."),
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool));
    }

  tmpinfo = apr_pcalloc(result_pool, sizeof(*tmpinfo));
  tmpinfo->kind = kind;

  tmpinfo->wc_info = apr_pcalloc(result_pool, sizeof(*tmpinfo->wc_info));

  SVN_ERR(svn_wc__internal_get_origin(&is_copy, &rev, &repos_relpath,
                                      &tmpinfo->repos_root_URL,
                                      &tmpinfo->repos_UUID,
                                      db, local_abspath, TRUE,
                                      result_pool, scratch_pool));

  /* If we didn't get an origin, get it directly */
  if (!tmpinfo->repos_root_URL)
    {
      SVN_ERR(svn_wc__internal_get_repos_info(&tmpinfo->repos_root_URL,
                                              &tmpinfo->repos_UUID,
                                              db, local_abspath,
                                              result_pool, scratch_pool));
    }

  if (repos_relpath)
    {
      SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL, NULL,
                                   &tmpinfo->last_changed_rev,
                                   &tmpinfo->last_changed_date,
                                   &tmpinfo->last_changed_author,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   db, local_abspath, result_pool,
                                   scratch_pool));
    }
  else
    tmpinfo->last_changed_rev = SVN_INVALID_REVNUM;

  if (is_copy)
    SVN_ERR(svn_wc__internal_get_commit_base_rev(&tmpinfo->rev, db,
                                                 local_abspath, scratch_pool));
  else
    tmpinfo->rev = rev;

  /* ### We should also just be fetching the true BASE revision
     ### above, which means copied items would also not have a
     ### revision to display.  But WC-1 wants to show the revision of
     ### copy targets as the copyfrom-rev.  *sigh*
  */
  tmpinfo->wc_info->copyfrom_rev = SVN_INVALID_REVNUM;

  if (is_copy)
    {
      SVN_ERR(svn_wc__internal_get_copyfrom_info(NULL, NULL, NULL, NULL,
                                                 &is_copy_target,
                                                 db, local_abspath,
                                                 scratch_pool, scratch_pool));

      if (is_copy_target)
        {
          tmpinfo->wc_info->copyfrom_url = svn_path_url_add_component2(
                                               tmpinfo->repos_root_URL,
                                               repos_relpath, result_pool);
          tmpinfo->wc_info->copyfrom_rev = rev;
        }
    }
  else if (repos_relpath)
    tmpinfo->URL = svn_path_url_add_component2(tmpinfo->repos_root_URL,
                                               repos_relpath,
                                               result_pool);

  /* Don't create a URL for local additions */
  if (!tmpinfo->URL)
    SVN_ERR(svn_wc__db_read_url(&tmpinfo->URL, db, local_abspath,
                                result_pool, scratch_pool));

  if (tmpinfo->kind == svn_node_file)
    {
      SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL,
                                   &tmpinfo->wc_info->checksum, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   &tmpinfo->wc_info->changelist,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   db, local_abspath, result_pool,
                                   scratch_pool));
    }

  if (tmpinfo->kind == svn_node_dir)
    {
      SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, &tmpinfo->wc_info->depth,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL,
                                   db, local_abspath, scratch_pool,
                                   scratch_pool));

      if (tmpinfo->wc_info->depth == svn_depth_unknown)
        tmpinfo->wc_info->depth = svn_depth_infinity;
    }
  else
    tmpinfo->wc_info->depth = svn_depth_infinity;

  if (exclude)
    tmpinfo->wc_info->depth = svn_depth_exclude;

  /* A default */
  tmpinfo->size = SVN_INVALID_FILESIZE;

  SVN_ERR(svn_wc__internal_node_get_schedule(&tmpinfo->wc_info->schedule, NULL,
                                             db, local_abspath,
                                             scratch_pool));

  SVN_ERR(svn_wc__db_get_wcroot(&tmpinfo->wc_info->wcroot_abspath, db,
                                local_abspath, result_pool, scratch_pool));

  SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL,
                               &tmpinfo->wc_info->working_size,
                               &tmpinfo->wc_info->text_time,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__db_read_conflicts(&tmpinfo->wc_info->conflicts, db,
                                    local_abspath, result_pool, scratch_pool));

  /* lock stuff */
  if (kind == svn_node_file)
    {
      svn_wc__db_lock_t *lock;

      svn_error_t *err = svn_wc__db_base_get_info(NULL, NULL, NULL, NULL,
                                                  NULL, NULL, NULL, NULL,
                                                  NULL, NULL, NULL, NULL,
                                                  &lock, NULL, NULL, NULL,
                                                  db, local_abspath,
                                                  result_pool, scratch_pool);

      if (err && (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND))
        {
          svn_error_clear(err);
          lock = NULL;
        }
      else if (err)
        return svn_error_return(err);

      if (lock)
        {
          tmpinfo->lock = apr_pcalloc(result_pool, sizeof(*(tmpinfo->lock)));
          tmpinfo->lock->token         = lock->token;
          tmpinfo->lock->owner         = lock->owner;
          tmpinfo->lock->comment       = lock->comment;
          tmpinfo->lock->creation_date = lock->date;
        }
    }

  *info = tmpinfo;
  return SVN_NO_ERROR;
}


/* Helper: build an svn_info_t *INFO struct with minimal content, to be
   used in reporting info for unversioned tree conflict victims. */
/* ### Some fields we could fill out based on the parent dir's entry
       or by looking at an obstructing item. */
static svn_error_t *
build_info_for_unversioned(svn_info2_t **info,
                           apr_pool_t *pool)
{
  svn_info2_t *tmpinfo = apr_pcalloc(pool, sizeof(*tmpinfo));
  tmpinfo->wc_info = apr_pcalloc(pool, sizeof (*tmpinfo->wc_info));

  tmpinfo->URL                  = NULL;
  tmpinfo->rev                  = SVN_INVALID_REVNUM;
  tmpinfo->kind                 = svn_node_none;
  tmpinfo->repos_UUID           = NULL;
  tmpinfo->repos_root_URL       = NULL;
  tmpinfo->last_changed_rev     = SVN_INVALID_REVNUM;
  tmpinfo->last_changed_date    = 0;
  tmpinfo->last_changed_author  = NULL;
  tmpinfo->lock                 = NULL;
  tmpinfo->size                 = SVN_INVALID_FILESIZE;

  *info = tmpinfo;
  return SVN_NO_ERROR;
}

/* Callback and baton for crawl_entries() walk over entries files. */
struct found_entry_baton
{
  svn_info_receiver2_t receiver;
  void *receiver_baton;
  svn_wc__db_t *db;
  /* The set of tree conflicts that have been found but not (yet) visited by
   * the tree walker.  Map of abspath -> svn_wc_conflict_description2_t. */
  apr_hash_t *tree_conflicts;
};

/* Call WALK_BATON->receiver with WALK_BATON->receiver_baton, passing to it
 * info about the path LOCAL_ABSPATH.
 * An svn_wc__node_found_func_t callback function. */
static svn_error_t *
info_found_node_callback(const char *local_abspath,
                         svn_node_kind_t kind,
                         void *walk_baton,
                         apr_pool_t *pool)
{
  struct found_entry_baton *fe_baton = walk_baton;
  svn_info2_t *info;

  SVN_ERR(build_info_for_entry(&info, fe_baton->db, local_abspath,
                               kind, pool, pool));

  SVN_ERR_ASSERT(info != NULL && info->wc_info != NULL);
  SVN_ERR(fe_baton->receiver(fe_baton->receiver_baton, local_abspath,
                             info, pool));

  /* If this node is a versioned directory, make a note of any tree conflicts
   * on all immediate children.  Some of these may be visited later in this
   * walk, at which point they will be removed from the list, while any that
   * are not visited will remain in the list. */
  if (kind == svn_node_dir)
    {
      apr_hash_t *conflicts;
      apr_hash_index_t *hi;

      SVN_ERR(svn_wc__db_op_read_all_tree_conflicts(
                &conflicts, fe_baton->db, local_abspath,
                apr_hash_pool_get(fe_baton->tree_conflicts), pool));
      for (hi = apr_hash_first(pool, conflicts); hi;
           hi = apr_hash_next(hi))
        {
          const char *this_basename = svn__apr_hash_index_key(hi);

          apr_hash_set(fe_baton->tree_conflicts,
                       svn_dirent_join(local_abspath, this_basename, pool),
                       APR_HASH_KEY_STRING, svn__apr_hash_index_val(hi));
        }
    }

  /* Delete this path which we are currently visiting from the list of tree
   * conflicts.  This relies on the walker visiting a directory before visiting
   * its children. */
  apr_hash_set(fe_baton->tree_conflicts, local_abspath, APR_HASH_KEY_STRING,
               NULL);

  return SVN_NO_ERROR;
}


/* Return TRUE iff the subtree at ROOT_ABSPATH, restricted to depth DEPTH,
 * would include the path CHILD_ABSPATH of kind CHILD_KIND. */
static svn_boolean_t
depth_includes(const char *root_abspath,
               svn_depth_t depth,
               const char *child_abspath,
               svn_node_kind_t child_kind,
               apr_pool_t *scratch_pool)
{
  const char *parent_abspath = svn_dirent_dirname(child_abspath, scratch_pool);

  return (depth == svn_depth_infinity
          || ((depth == svn_depth_immediates
               || (depth == svn_depth_files && child_kind == svn_node_file))
              && strcmp(root_abspath, parent_abspath) == 0)
          || strcmp(root_abspath, child_abspath) == 0);
}


svn_error_t *
svn_wc__get_info(svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 svn_depth_t depth,
                 svn_info_receiver2_t receiver,
                 void *receiver_baton,
                 const apr_array_header_t *changelist_filter,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  struct found_entry_baton fe_baton;
  const svn_wc_conflict_description2_t *root_tree_conflict;
  svn_error_t *err;
  apr_pool_t *iterpool;
  apr_hash_index_t *hi;

  fe_baton.receiver = receiver;
  fe_baton.receiver_baton = receiver_baton;
  fe_baton.db = wc_ctx->db;
  fe_baton.tree_conflicts = apr_hash_make(scratch_pool);

  SVN_ERR(svn_wc__db_op_read_tree_conflict(&root_tree_conflict,
                                           wc_ctx->db, local_abspath,
                                           scratch_pool, scratch_pool));
  if (root_tree_conflict)
    {
      apr_hash_set(fe_baton.tree_conflicts, local_abspath, APR_HASH_KEY_STRING,
                   root_tree_conflict);
    }

  err = svn_wc__internal_walk_children(wc_ctx->db, local_abspath,
                                       FALSE /* show_hidden */,
                                       changelist_filter,
                                       info_found_node_callback,
                                       &fe_baton, depth,
                                       cancel_func, cancel_baton,
                                       scratch_pool);

  /* If the target root node is not present, svn_wc__internal_walk_children()
     returns a PATH_NOT_FOUND error and doesn't call the callback.  If there
     is a tree conflict on this node, that is not an error. */
  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND && root_tree_conflict)
    svn_error_clear(err);
  else if (err)
    return svn_error_return(err);

  /* If there are any tree conflicts that we have found but have not reported,
   * send a minimal info struct for each one now. */
  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, fe_baton.tree_conflicts); hi;
       hi = apr_hash_next(hi))
    {
      const char *this_abspath = svn__apr_hash_index_key(hi);
      const svn_wc_conflict_description2_t *tree_conflict
        = svn__apr_hash_index_val(hi);

      svn_pool_clear(iterpool);

      if (depth_includes(local_abspath, depth, tree_conflict->local_abspath,
                         tree_conflict->kind, iterpool))
        {
          apr_array_header_t *conflicts = apr_array_make(iterpool,
            1, sizeof(const svn_wc_conflict_description2_t *));
          svn_info2_t *info;

          SVN_ERR(build_info_for_unversioned(&info, iterpool));
          SVN_ERR(svn_wc__internal_get_repos_info(&info->repos_root_URL,
                                                  &info->repos_UUID,
                                                  fe_baton.db,
                                                  local_abspath,
                                                  iterpool, iterpool));
          APR_ARRAY_PUSH(conflicts, const svn_wc_conflict_description2_t *)
            = tree_conflict;
          info->wc_info->conflicts = conflicts;

          SVN_ERR(receiver(receiver_baton, this_abspath, info, iterpool));
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
