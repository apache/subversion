/*
 * context.c:  code to manage a client's context.
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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


/*** Includes. ***/

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_pools.h"
#include "svn_error.h"



/*** Code. ***/

struct svn_client_ctx_t {
  svn_client_auth_baton_t *old_auth_baton;
  svn_auth_baton_t *auth_baton;
};

svn_client_ctx_t *
svn_client_ctx_create (apr_pool_t *pool)
{
  return apr_pcalloc (pool, sizeof (svn_client_ctx_t));
}

void
svn_client_ctx_set_auth_baton (svn_client_ctx_t *ctx,
                               svn_client_auth_baton_t *old_auth_baton,
                               svn_auth_baton_t *auth_baton)
{
  ctx->old_auth_baton = old_auth_baton;
  ctx->auth_baton = auth_baton;
}

svn_error_t *
svn_client_ctx_get_old_auth_baton (svn_client_ctx_t *ctx,
                                   svn_client_auth_baton_t **auth_baton)
{
  *auth_baton = ctx->old_auth_baton;

  if (! *auth_baton)
    return svn_error_create (SVN_ERR_CLIENT_CTX_NOT_FOUND, NULL,
                             "no authentication baton found");

  return SVN_NO_ERROR;
}
