/*
 * context.c:  routines for managing a working copy context
 *
 * ====================================================================
 * Copyright (c) 2009 CollabNet.  All rights reserved.
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

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"

#include "wc.h"
#include "wc_db.h"

#include "svn_private_config.h"



struct svn_wc_context_t
{
  /* The wc_db handle for this working copy. */
  svn_wc__db_t *db;

  /* The state pool for this context. */
  apr_pool_t *state_pool;
};


/* APR cleanup function used to explicitly close any of our dependent
   data structures before we disappear ourselves. */
static apr_status_t
close_ctx_apr(void *data)
{
  svn_wc_context_t *ctx = data;
  svn_error_t *err;

  /* We can use the state pool here, because this handler will only get
     run if the state pool is being cleaned up posthaste. */
  err = svn_wc__db_close(ctx->db, ctx->state_pool);
  if (err)
    {
      int result = err->apr_err;
      svn_error_clear(err);
      return result;
    }

  return APR_SUCCESS;
}


svn_error_t *
svn_wc_context_create(svn_wc_context_t **wc_ctx,
                      svn_config_t *config,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_wc_context_t *ctx = apr_pcalloc(result_pool, sizeof(*ctx));
 
  /* Create the state_pool, and open up a wc_db in it. */
  ctx->state_pool = svn_pool_create(result_pool);
  SVN_ERR(svn_wc__db_open(&ctx->db, svn_wc__db_openmode_readwrite, config,
                          ctx->state_pool, scratch_pool));

  apr_pool_cleanup_register(ctx->state_pool, ctx, close_ctx_apr,
                            apr_pool_cleanup_null);

  *wc_ctx = ctx;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_context_destroy(svn_wc_context_t *wc_ctx)
{
  /* Because we added the cleanup handler in svn_wc_context_create(), we
     can just destory the state pool.  Voilà!  Everything is closed and
     freed. */
  svn_pool_destroy(wc_ctx->state_pool);

  return SVN_NO_ERROR;
}
