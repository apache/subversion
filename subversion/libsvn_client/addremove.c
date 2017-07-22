/*
 * addremove.c: integrate unversioned structural changes into working copy
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

#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_io.h"
#include "client.h"

#include "private/svn_client_private.h"
#include "private/svn_wc_private.h"

#include "svn_private_config.h"



/*** Code. ***/

struct addremove_status_baton {
  /* Status info for missing paths. */
  apr_hash_t *missing;

  /* Status info for unversioned paths. */
  apr_hash_t *unversioned;
};

/* Implements svn_wc_status_func4_t. */
static svn_error_t *
addremove_status_func(void *baton, const char *local_abspath,
                      const svn_wc_status3_t *status,
                      apr_pool_t *scratch_pool)
{
  struct addremove_status_baton *b = baton;

  switch (status->node_status)
    {
      case svn_wc_status_unversioned:
        {
          apr_hash_t *hash = b->unversioned;
          apr_pool_t *result_pool = apr_hash_pool_get(hash);

          svn_hash_sets(hash, apr_pstrdup(result_pool, local_abspath),
                        svn_wc_dup_status3(status, result_pool));
          break;
        }

      case svn_wc_status_missing:
        {

          apr_hash_t *hash = b->missing;
          apr_pool_t *result_pool = apr_hash_pool_get(hash);

          svn_hash_sets(hash, apr_pstrdup(result_pool, local_abspath),
                        svn_wc_dup_status3(status, result_pool));
          break;
        }

      default:
        break;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
addremove(const char *local_abspath, svn_depth_t depth,
          svn_client_ctx_t *ctx, apr_pool_t *scratch_pool)
{
  struct addremove_status_baton b;
  struct svn_wc_status3_t *status;
  svn_node_kind_t kind_on_disk;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  /* Our target must be a versioned directory. */
  SVN_ERR(svn_wc_status3(&status, ctx->wc_ctx, local_abspath,
                         scratch_pool, scratch_pool));
  SVN_ERR(svn_io_check_path(local_abspath, &kind_on_disk, scratch_pool));
  if (status->kind != svn_node_dir || kind_on_disk != svn_node_dir ||
      !status->versioned)
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' is not a versioned directory"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  b.missing = apr_hash_make(scratch_pool);
  b.unversioned = apr_hash_make(scratch_pool);

  SVN_ERR(svn_wc_walk_status(ctx->wc_ctx, local_abspath, depth,
                             TRUE, FALSE, FALSE, NULL,
                             addremove_status_func, &b,
                             ctx->cancel_func, ctx->cancel_baton,
                             scratch_pool));

  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, b.unversioned); hi;
       hi = apr_hash_next(hi))
    {
      const char *unversioned_abspath = apr_hash_this_key(hi);

      svn_pool_clear(iterpool);

      SVN_ERR(svn_io_check_path(unversioned_abspath, &kind_on_disk,
                                scratch_pool));

      if (kind_on_disk == svn_node_file)
        {
          SVN_ERR(svn_client__add_file(unversioned_abspath,
                                       NULL, /* TODO: magic cookie */
                                       NULL, /* TODO: autoprops */
                                       TRUE, /* TODO: !no_autoprops */
                                       ctx, iterpool));
        }
      else if (kind_on_disk == svn_node_dir && depth >= svn_depth_immediates)
        {
          svn_depth_t depth_below_here = depth;

          if (depth == svn_depth_immediates)
            depth_below_here = svn_depth_empty;

          SVN_ERR(svn_client__add_dir_recursive(
                    unversioned_abspath, depth_below_here,
                    FALSE, /* force */
                    TRUE, /* TODO: !no_autoprops */
                    NULL, /* TODO: magic cookie */
                    NULL, /* TODO: autoprops */
                    FALSE, /* TODO: refresh_ignores */
                    NULL, /* TODO: ignores */
                    ctx, iterpool, iterpool));
        }
    }

  for (hi = apr_hash_first(scratch_pool, b.missing); hi;
       hi = apr_hash_next(hi))
    {
      const char *missing_abspath = apr_hash_this_key(hi);

      svn_pool_clear(iterpool);

      SVN_ERR(svn_wc_delete4(ctx->wc_ctx, missing_abspath,
                             FALSE, /* keep_local */
                             FALSE, /* delete_unversioned_target */
                             ctx->cancel_func, ctx->cancel_baton,
                             ctx->notify_func2, ctx->notify_baton2,
                             iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_addremove(const char *local_path,
                     svn_depth_t depth,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *scratch_pool)
{
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, local_path, scratch_pool));

  SVN_WC__CALL_WITH_WRITE_LOCK(
    addremove(local_abspath, depth, ctx, scratch_pool),
    ctx->wc_ctx, local_abspath, TRUE, scratch_pool);

  return SVN_NO_ERROR;
}
