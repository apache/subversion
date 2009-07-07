/*
 * ctx.c:  initialization function for client context
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
  SVN_ERR(svn_wc_context_create(&(*ctx)->wc_ctx, NULL /* config */, pool,
                                pool));
  (*ctx)->notify_baton2 = *ctx;
  return SVN_NO_ERROR;
}
