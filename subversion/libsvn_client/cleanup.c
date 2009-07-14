/*
 * cleanup.c:  wrapper around wc cleanup functionality.
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



/*** Includes. ***/

#include "svn_time.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_config.h"
#include "svn_dirent_uri.h"
#include "client.h"

#include "svn_private_config.h"


/*** Code. ***/

svn_error_t *
svn_client_cleanup(const char *path,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *scratch_pool)
{
  const char *diff3_cmd;
  svn_error_t *err;
  svn_config_t *cfg = ctx->config
    ? apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_CONFIG,
                   APR_HASH_KEY_STRING)
    : NULL;

  svn_config_get(cfg, &diff3_cmd, SVN_CONFIG_SECTION_HELPERS,
                 SVN_CONFIG_OPTION_DIFF3_CMD, NULL);

  err = svn_wc_cleanup2(path, diff3_cmd, ctx->cancel_func,
                        ctx->cancel_baton, scratch_pool);
  svn_io_sleep_for_timestamps(path, scratch_pool);
  return svn_error_return(err);
}


svn_error_t *
svn_client_upgrade(const char *path,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *scratch_pool)
{
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));
  SVN_ERR(svn_wc_upgrade(ctx->wc_ctx, local_abspath, ctx->cancel_func,
                         ctx->cancel_baton, scratch_pool));

  return SVN_NO_ERROR;
}
