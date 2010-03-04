/*
 * cleanup.c:  wrapper around wc cleanup functionality.
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

#include "svn_time.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_config.h"
#include "svn_dirent_uri.h"
#include "svn_pools.h"
#include "client.h"

#include "svn_private_config.h"


/*** Code. ***/

svn_error_t *
svn_client_cleanup(const char *path,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  svn_error_t *err;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));

  err = svn_wc_cleanup3(ctx->wc_ctx, local_abspath, ctx->cancel_func,
                        ctx->cancel_baton, scratch_pool);
  svn_io_sleep_for_timestamps(path, scratch_pool);
  return svn_error_return(err);
}


/* callback baton for fetch_repos_info */
struct repos_info_baton
{
  apr_pool_t *pool;
  svn_client_ctx_t *ctx;
  const char *last_repos;
  const char *last_uuid;
};

/* svn_wc_upgrade_get_repos_info_t implementation for calling
   svn_wc_upgrade() from svn_client_upgrade() */
static svn_error_t *
fetch_repos_info(const char **repos_root,
                 const char **repos_uuid,
                 void *baton,
                 const char *url,
                 apr_pool_t *scratch_pool,
                 apr_pool_t *result_pool)
{
  struct repos_info_baton *ri = baton;
  apr_pool_t *subpool;
  svn_ra_session_t *ra_session;

  /* The same info is likely to retrieved multiple times (e.g. externals) */
  if (ri->last_repos && svn_uri_is_child(ri->last_repos, url, NULL))
    {
      *repos_root = apr_pstrdup(result_pool, ri->last_repos);
      *repos_uuid = apr_pstrdup(result_pool, ri->last_uuid);
      return SVN_NO_ERROR;
    }

  subpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_client_open_ra_session(&ra_session, url, ri->ctx, subpool));

  SVN_ERR(svn_ra_get_repos_root2(ra_session, repos_root, result_pool));
  SVN_ERR(svn_ra_get_uuid2(ra_session, repos_uuid, result_pool));

  /* Store data for further calls */
  ri->last_repos = apr_pstrdup(ri->pool, *repos_root);
  ri->last_uuid = apr_pstrdup(ri->pool, *repos_uuid);

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_upgrade(const char *path,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  struct repos_info_baton info_baton;
  info_baton.pool = scratch_pool;
  info_baton.ctx = ctx;
  info_baton.last_repos = NULL;
  info_baton.last_uuid = NULL;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));
  SVN_ERR(svn_wc_upgrade(ctx->wc_ctx, local_abspath,
                         fetch_repos_info, &info_baton,
                         ctx->cancel_func, ctx->cancel_baton,
                         ctx->notify_func2, ctx->notify_baton2,
                         scratch_pool));

  return SVN_NO_ERROR;
}
