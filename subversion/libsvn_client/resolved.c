/*
 * resolved.c:  wrapper around wc resolved functionality.
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



/*** Includes. ***/

#include "svn_types.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_hash.h"
#include "svn_sorts.h"
#include "client.h"
#include "private/svn_sorts_private.h"
#include "private/svn_wc_private.h"

#include "svn_private_config.h"

/*** Code. ***/

svn_error_t *
svn_client__resolve_conflicts(svn_boolean_t *conflicts_remain,
                              apr_hash_t *conflicted_paths,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *array;
  int i;

  if (conflicts_remain)
    *conflicts_remain = FALSE;

  SVN_ERR(svn_hash_keys(&array, conflicted_paths, scratch_pool));
  svn_sort__array(array, svn_sort_compare_paths);

  for (i = 0; i < array->nelts; i++)
    {
      const char *local_abspath = APR_ARRAY_IDX(array, i, const char *);

      svn_pool_clear(iterpool);
      SVN_ERR(svn_wc__resolve_conflicts(ctx->wc_ctx, local_abspath,
                                        svn_depth_empty,
                                        TRUE /* resolve_text */,
                                        "" /* resolve_prop (ALL props) */,
                                        TRUE /* resolve_tree */,
                                        svn_wc_conflict_choose_unspecified,
                                        ctx->conflict_func2,
                                        ctx->conflict_baton2,
                                        ctx->cancel_func, ctx->cancel_baton,
                                        ctx->notify_func2, ctx->notify_baton2,
                                        iterpool));

      if (conflicts_remain && !*conflicts_remain)
        {
          svn_error_t *err;
          svn_boolean_t text_c, prop_c, tree_c;

          err = svn_wc_conflicted_p3(&text_c, &prop_c, &tree_c,
                                     ctx->wc_ctx, local_abspath,
                                     iterpool);
          if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
            {
              svn_error_clear(err);
              text_c = prop_c = tree_c = FALSE;
            }
          else
            {
              SVN_ERR(err);
            }
          if (text_c || prop_c || tree_c)
            *conflicts_remain = TRUE;
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_resolve(const char *path,
                   svn_depth_t depth,
                   svn_wc_conflict_choice_t conflict_choice,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  const char *local_abspath;
  svn_error_t *err;
  const char *lock_abspath;

  if (svn_path_is_url(path))
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' is not a local path"), path);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  /* Similar to SVN_WC__CALL_WITH_WRITE_LOCK but using a custom
     locking function. */

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath, pool, pool));
  err = svn_wc__resolve_conflicts(ctx->wc_ctx, local_abspath,
                                  depth,
                                  TRUE /* resolve_text */,
                                  "" /* resolve_prop (ALL props) */,
                                  TRUE /* resolve_tree */,
                                  conflict_choice,
                                  ctx->conflict_func2,
                                  ctx->conflict_baton2,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  ctx->notify_func2, ctx->notify_baton2,
                                  pool);

  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 pool));
  svn_io_sleep_for_timestamps(path, pool);

  return svn_error_trace(err);
}


/*** Dealing with conflicts. ***/

struct svn_client_conflict_t
{
  const char *local_abspath;
  svn_client_ctx_t *ctx;

  const svn_wc_conflict_description2_t *desc2; /* ### temporary */
};

svn_client_conflict_t *
svn_client_conflict_get(const char *local_abspath,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_client_conflict_t *conflict;

  conflict = apr_pcalloc(result_pool, sizeof(*conflict));
  conflict->local_abspath = apr_pstrdup(result_pool, local_abspath);
  conflict->ctx = ctx;

  return conflict;
}

svn_client_conflict_t *
svn_client_conflict_from_wc_description2_t(
  const svn_wc_conflict_description2_t *desc,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  svn_client_conflict_t *conflict;

  conflict = svn_client_conflict_get(desc->local_abspath, NULL,
                                     result_pool, scratch_pool);
  conflict->desc2 = desc;

  return conflict;
}

svn_wc_conflict_kind_t
svn_client_conflict_get_kind(const svn_client_conflict_t *conflict)
{
  return conflict->desc2->kind;
}

const char *
svn_client_conflict_get_local_abspath(const svn_client_conflict_t *conflict)
{
  return conflict->local_abspath;
}

svn_wc_operation_t
svn_client_conflict_get_operation(const svn_client_conflict_t *conflict)
{
  return conflict->desc2->operation;
}

svn_wc_conflict_action_t
svn_client_conflict_get_incoming_change(const svn_client_conflict_t *conflict)
{
  return conflict->desc2->action;
}

svn_wc_conflict_reason_t
svn_client_conflict_get_local_change(const svn_client_conflict_t *conflict)
{
  return conflict->desc2->reason;
}

svn_error_t *
svn_client_conflict_get_repos_info(const char **repos_root_url,
                                   const char **repos_uuid,
                                   const svn_client_conflict_t *conflict,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  if (repos_root_url)
    {
      if (conflict->desc2->src_left_version)
        *repos_root_url = conflict->desc2->src_left_version->repos_url;
      else if (conflict->desc2->src_right_version)
        *repos_root_url = conflict->desc2->src_right_version->repos_url;
      else
        *repos_root_url = NULL;
    }

  if (repos_uuid)
    {
      if (conflict->desc2->src_left_version)
        *repos_uuid = conflict->desc2->src_left_version->repos_uuid;
      else if (conflict->desc2->src_right_version)
        *repos_uuid = conflict->desc2->src_right_version->repos_uuid;
      else
        *repos_uuid = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_get_incoming_old_repos_location(
  const char **incoming_old_repos_relpath,
  svn_revnum_t *incoming_old_pegrev,
  svn_node_kind_t *incoming_old_node_kind,
  const svn_client_conflict_t *conflict,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  if (incoming_old_repos_relpath)
    {
      if (conflict->desc2->src_left_version)
        *incoming_old_repos_relpath =
          conflict->desc2->src_left_version->path_in_repos;
      else
        *incoming_old_repos_relpath = NULL;
    }

  if (incoming_old_pegrev)
    {
      if (conflict->desc2->src_left_version)
        *incoming_old_pegrev = conflict->desc2->src_left_version->peg_rev;
      else
        *incoming_old_pegrev = SVN_INVALID_REVNUM;
    }

  if (incoming_old_node_kind)
    {
      if (conflict->desc2->src_left_version)
        *incoming_old_node_kind = conflict->desc2->src_left_version->node_kind;
      else
        *incoming_old_node_kind = svn_node_none;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_get_incoming_new_repos_location(
  const char **incoming_new_repos_relpath,
  svn_revnum_t *incoming_new_pegrev,
  svn_node_kind_t *incoming_new_node_kind,
  const svn_client_conflict_t *conflict,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  if (incoming_new_repos_relpath)
    {
      if (conflict->desc2->src_right_version)
        *incoming_new_repos_relpath =
          conflict->desc2->src_right_version->path_in_repos;
      else
        *incoming_new_repos_relpath = NULL;
    }

  if (incoming_new_pegrev)
    {
      if (conflict->desc2->src_right_version)
        *incoming_new_pegrev = conflict->desc2->src_right_version->peg_rev;
      else
        *incoming_new_pegrev = SVN_INVALID_REVNUM;
    }

  if (incoming_new_node_kind)
    {
      if (conflict->desc2->src_right_version)
        *incoming_new_node_kind = conflict->desc2->src_right_version->node_kind;
      else
        *incoming_new_node_kind = svn_node_none;
    }

  return SVN_NO_ERROR;
}

svn_node_kind_t
svn_client_conflict_tree_get_victim_node_kind(
  const svn_client_conflict_t *conflict)
{
  SVN_ERR_ASSERT_NO_RETURN(svn_client_conflict_get_kind(conflict)
      == svn_wc_conflict_kind_tree);

  return conflict->desc2->node_kind;
}

const char *
svn_client_conflict_prop_get_propname(const svn_client_conflict_t *conflict)
{
  SVN_ERR_ASSERT_NO_RETURN(svn_client_conflict_get_kind(conflict)
      == svn_wc_conflict_kind_property);

  return conflict->desc2->property_name;
}

svn_error_t *
svn_client_conflict_prop_get_propvals(const svn_string_t **base_propval,
                                      const svn_string_t **working_propval,
                                      const svn_string_t **incoming_old_propval,
                                      const svn_string_t **incoming_new_propval,
                                      const svn_client_conflict_t *conflict,
  apr_pool_t *result_pool)
{
  SVN_ERR_ASSERT(svn_client_conflict_get_kind(conflict) ==
                 svn_wc_conflict_kind_property);

  if (base_propval)
    *base_propval = svn_string_dup(conflict->desc2->prop_value_base,
                                   result_pool);

  if (working_propval)
    *working_propval = svn_string_dup(conflict->desc2->prop_value_working,
                                      result_pool);

  if (incoming_old_propval)
    *incoming_old_propval =
      svn_string_dup(conflict->desc2->prop_value_incoming_old, result_pool);

  if (incoming_new_propval)
    *incoming_new_propval =
      svn_string_dup(conflict->desc2->prop_value_incoming_new, result_pool);

  return SVN_NO_ERROR;
}

const char *
svn_client_conflict_text_get_mime_type(const svn_client_conflict_t *conflict)
{
  SVN_ERR_ASSERT_NO_RETURN(svn_client_conflict_get_kind(conflict)
      == svn_wc_conflict_kind_text);

  return conflict->desc2->mime_type;
}

svn_error_t *
svn_client_conflict_text_get_contents(const char **base_abspath,
                                      const char **working_abspath,
                                      const char **incoming_old_abspath,
                                      const char **incoming_new_abspath,
                                      const svn_client_conflict_t *conflict,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_client_conflict_get_kind(conflict)
      == svn_wc_conflict_kind_text);

  if (base_abspath)
    {
      if (svn_client_conflict_get_operation(conflict) ==
          svn_wc_operation_merge)
        *base_abspath = NULL; /* ### WC base contents not available yet */
      else /* update/switch */
        *base_abspath = conflict->desc2->base_abspath;
    }

  if (working_abspath)
    *working_abspath = conflict->desc2->my_abspath;

  if (incoming_old_abspath)
    *incoming_old_abspath = conflict->desc2->base_abspath;

  if (incoming_new_abspath)
    *incoming_new_abspath = conflict->desc2->their_abspath;

  return SVN_NO_ERROR;
}
