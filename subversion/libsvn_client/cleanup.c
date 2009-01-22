/*
 * cleanup.c:  wrapper around wc cleanup functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#include "svn_time.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_config.h"
#include "client.h"

#include "svn_private_config.h"


/*** Code. ***/

svn_error_t *
svn_client_cleanup(const char *dir,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  const char *diff3_cmd;
  svn_error_t *err;
  svn_config_t *cfg = ctx->config
    ? apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_CONFIG,
                   APR_HASH_KEY_STRING)
    : NULL;

  svn_config_get(cfg, &diff3_cmd, SVN_CONFIG_SECTION_HELPERS,
                 SVN_CONFIG_OPTION_DIFF3_CMD, NULL);

  err = svn_wc_cleanup2(dir, diff3_cmd, ctx->cancel_func, ctx->cancel_baton,
                        pool);
  svn_io_sleep_for_timestamps(dir, pool);
  return err;
}
