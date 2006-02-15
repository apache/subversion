/*
 * ctx.c:  intialization function for client context
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

#include <apr_pools.h>
#include "svn_client.h"
#include "svn_error.h"


/*** Code. ***/

/* Call the notify_func of the context provided by BATON, if non-NULL. */
static void
call_notify_func(void *baton, const svn_wc_notify_t *n, apr_pool_t *pool)
{
  const svn_client_ctx_t *ctx = baton;

  if (ctx->notify_func)
    ctx->notify_func(ctx->notify_baton, n->path, n->action, n->kind,
                     n->mime_type, n->content_state, n->prop_state,
                     n->revision);
}

svn_error_t *
svn_client_create_context(svn_client_ctx_t **ctx,
                          apr_pool_t *pool)
{
  *ctx = apr_pcalloc(pool, sizeof(svn_client_ctx_t));
  (*ctx)->notify_func2 = call_notify_func;
  (*ctx)->notify_baton2 = *ctx;
  return SVN_NO_ERROR;
}
