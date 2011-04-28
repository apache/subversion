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
#include "svn_wc.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"



/* Set *INFO to a new struct, allocated in RESULT_POOL, built from the WC
   metadata of LOCAL_ABSPATH.  Pointer fields are copied by reference, not
   dup'd. */
static svn_error_t *
build_info_for_entry(svn_info2_t **info,
                     svn_wc_context_t *wc_ctx,
                     const char *local_abspath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_info2_t *tmpinfo;
  svn_boolean_t is_copy_target;
  apr_time_t lock_date;
  svn_node_kind_t kind;
  svn_boolean_t exclude = FALSE;
  svn_boolean_t is_copy;
  svn_revnum_t rev;
  const char *repos_relpath;

  SVN_ERR(svn_wc_read_kind(&kind, wc_ctx, local_abspath, FALSE, scratch_pool));

  if (kind == svn_node_none)
    {
      svn_error_clear(svn_wc__node_depth_is_exclude(&exclude, wc_ctx,
                                                    local_abspath,
                                                    scratch_pool));
      if (! exclude)
        return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                 _("The node '%s' was not found."),
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool));
    }

  tmpinfo = apr_pcalloc(result_pool, sizeof(*tmpinfo));
  tmpinfo->kind = kind;

  /* ### This is dangerous, though temporary.  Per the docstring, we shouldn't
     ### be alloc'ing this struct directly, but let libsvn_wc do it.  Nowhere
     ### in libsvn_wc do we currently alloc this struct, so we do it here.
     ### All this code will shortly move to libsvn_wc, anyway. */
  tmpinfo->wc_info = apr_pcalloc(result_pool, sizeof(*tmpinfo->wc_info));

  SVN_ERR(svn_wc__node_get_origin(&is_copy, &rev, &repos_relpath,
                                  &tmpinfo->repos_root_URL,
                                  &tmpinfo->repos_UUID,
                                  wc_ctx, local_abspath, TRUE,
                                  result_pool, scratch_pool));

  /* If we didn't get an origin, get it directly */
  if (!tmpinfo->repos_root_URL)
    {
      SVN_ERR(svn_wc__node_get_repos_info(&tmpinfo->repos_root_URL,
                                          &tmpinfo->repos_UUID,
                                          wc_ctx, local_abspath, TRUE, TRUE,
                                          result_pool, scratch_pool));
    }

  if (repos_relpath)
    {
      SVN_ERR(svn_wc__node_get_changed_info(&tmpinfo->last_changed_rev,
                                            &tmpinfo->last_changed_date,
                                            &tmpinfo->last_changed_author,
                                            wc_ctx, local_abspath,
                                            result_pool, scratch_pool));
    }
  else
    tmpinfo->last_changed_rev = SVN_INVALID_REVNUM;

  if (is_copy)
    SVN_ERR(svn_wc__node_get_commit_base_rev(&tmpinfo->rev, wc_ctx,
                                             local_abspath, scratch_pool));
  else
    tmpinfo->rev = rev;

  /* ### FIXME: For now, we'll tweak an SVN_INVALID_REVNUM and make it
     ### 0.  In WC-1, files scheduled for addition were assigned
     ### revision=0.  This is wrong, and we're trying to remedy that,
     ### but for the sake of test suite and code sanity now in WC-NG,
     ### we'll just maintain the old behavior.
     ###
     ### We should also just be fetching the true BASE revision
     ### above, which means copied items would also not have a
     ### revision to display.  But WC-1 wants to show the revision of
     ### copy targets as the copyfrom-rev.  *sigh*
  */
  if (! SVN_IS_VALID_REVNUM(tmpinfo->rev))
    tmpinfo->rev = 0;

  tmpinfo->wc_info->copyfrom_rev = SVN_INVALID_REVNUM;

  if (is_copy)
    {
      SVN_ERR(svn_wc__node_get_copyfrom_info(NULL, NULL, NULL, NULL,
                                             &is_copy_target,
                                             wc_ctx, local_abspath,
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
    SVN_ERR(svn_wc__node_get_url(&tmpinfo->URL, wc_ctx, local_abspath,
                                 result_pool, scratch_pool));

  if (tmpinfo->kind == svn_node_file)
    {
      const svn_checksum_t *checksum;

      SVN_ERR(svn_wc__node_get_changelist(&tmpinfo->wc_info->changelist, wc_ctx,
                                          local_abspath,
                                          result_pool, scratch_pool));

      SVN_ERR(svn_wc__node_get_checksum(&checksum, wc_ctx, local_abspath,
                                        scratch_pool, scratch_pool));

      tmpinfo->wc_info->checksum = svn_checksum_to_cstring(checksum,
                                                           result_pool);
    }

  if (tmpinfo->kind == svn_node_dir)
    {
      SVN_ERR(svn_wc__node_get_depth(&tmpinfo->wc_info->depth, wc_ctx,
                                     local_abspath, scratch_pool));

      if (tmpinfo->wc_info->depth == svn_depth_unknown)
        tmpinfo->wc_info->depth = svn_depth_infinity;
    }
  else
    tmpinfo->wc_info->depth = svn_depth_infinity;

  if (exclude)
    tmpinfo->wc_info->depth = svn_depth_exclude;

  SVN_ERR(svn_wc__node_get_schedule(&tmpinfo->wc_info->schedule, NULL,
                                    wc_ctx, local_abspath, scratch_pool));

  SVN_ERR(svn_wc_get_wc_root(&tmpinfo->wc_info->wcroot_abspath, wc_ctx,
                             local_abspath, result_pool, scratch_pool));

  SVN_ERR(svn_wc__node_get_recorded_info(&tmpinfo->wc_info->working_size,
                                         &tmpinfo->wc_info->text_time,
                                         wc_ctx, local_abspath, scratch_pool));

  /* lock stuff */
  if (kind == svn_node_file)
    {
      const char *lock_token;
      const char *lock_owner;
      const char *lock_comment;

      SVN_ERR(svn_wc__node_get_lock_info(&lock_token, &lock_owner,
                                         &lock_comment, &lock_date,
                                         wc_ctx, local_abspath,
                                         result_pool, scratch_pool));

      if (lock_token)  /* the token is the critical bit. */
        {
          tmpinfo->lock = apr_pcalloc(result_pool, sizeof(*(tmpinfo->lock)));
          tmpinfo->lock->token         = lock_token;
          tmpinfo->lock->owner         = lock_owner;
          tmpinfo->lock->comment       = lock_comment;
          tmpinfo->lock->creation_date = lock_date;
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
  tmpinfo->size                 = SVN_INFO_SIZE_UNKNOWN;

  *info = tmpinfo;
  return SVN_NO_ERROR;
}

/* Callback and baton for crawl_entries() walk over entries files. */
struct found_entry_baton
{
  svn_info_receiver2_t receiver;
  void *receiver_baton;
  svn_wc_context_t *wc_ctx;
};

/* An svn_wc__node_found_func_t callback function. */
static svn_error_t *
info_found_node_callback(const char *local_abspath,
                         svn_node_kind_t kind,
                         void *walk_baton,
                         apr_pool_t *pool)
{
  struct found_entry_baton *fe_baton = walk_baton;
  svn_info2_t *info = NULL;
  const svn_wc_conflict_description2_t *tree_conflict = NULL;
  svn_error_t *err;

  SVN_ERR(svn_wc__get_tree_conflict(&tree_conflict, fe_baton->wc_ctx,
                                    local_abspath, pool, pool));

  err = build_info_for_entry(&info, fe_baton->wc_ctx, local_abspath,
                             pool, pool);
  if (err && (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
      && tree_conflict)
    {
      svn_error_clear(err);

      SVN_ERR(build_info_for_unversioned(&info, pool));
      SVN_ERR(svn_wc__node_get_repos_info(&(info->repos_root_URL),
                                          NULL,
                                          fe_baton->wc_ctx,
                                          local_abspath, FALSE, FALSE,
                                          pool, pool));
    }
  else if (err)
    return svn_error_return(err);

  SVN_ERR_ASSERT(info != NULL);

  if (tree_conflict)
    {
      info->wc_info->conflicts = apr_array_make(pool, 1,
                            sizeof(const svn_wc_conflict_description2_t *));

      APR_ARRAY_PUSH(info->wc_info->conflicts,
                     const svn_wc_conflict_description2_t *) = tree_conflict;
    }

  SVN_ERR(fe_baton->receiver(fe_baton->receiver_baton, local_abspath,
                             info, pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__get_info(svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 svn_depth_t depth,
                 svn_info_receiver2_t receiver,
                 void *receiver_baton,
                 const apr_array_header_t *changelists,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  struct found_entry_baton fe_baton;
  svn_error_t *err;

  fe_baton.receiver = receiver;
  fe_baton.receiver_baton = receiver_baton;
  fe_baton.wc_ctx = wc_ctx;

  err = svn_wc__node_walk_children(wc_ctx, local_abspath, FALSE,
                                   changelists,
                                   info_found_node_callback, &fe_baton, depth,
                                   cancel_func, cancel_baton, scratch_pool);

  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      /* Check for a tree conflict on the root node of the info, and if there
         is one, send a minimal info struct. */
      const svn_wc_conflict_description2_t *tree_conflict;

      SVN_ERR(svn_wc__get_tree_conflict(&tree_conflict, wc_ctx,
                                        local_abspath, scratch_pool,
                                        scratch_pool));

      if (tree_conflict)
        {
          svn_info2_t *info;
          svn_error_clear(err);

          SVN_ERR(build_info_for_unversioned(&info, scratch_pool));
          if (tree_conflict)
            {
              info->wc_info->conflicts = apr_array_make(scratch_pool, 1,
                            sizeof(const svn_wc_conflict_description2_t *));

              APR_ARRAY_PUSH(info->wc_info->conflicts,
                     const svn_wc_conflict_description2_t *) = tree_conflict;
            }

          SVN_ERR(svn_wc__node_get_repos_info(&(info->repos_root_URL),
                                              NULL, wc_ctx,
                                              local_abspath, FALSE, FALSE,
                                              scratch_pool, scratch_pool));

          SVN_ERR(receiver(receiver_baton, local_abspath, info, scratch_pool));
        }
      else
        return svn_error_return(err);
    }
  else if (err)
    return svn_error_return(err);

  return SVN_NO_ERROR;
}
