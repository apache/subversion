/*
 * resolved.c:  wrapper around wc resolved functionality.
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

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_error.h"
#include "client.h"



/*** Code. ***/

svn_error_t *
svn_client_resolved(const char *path,
                    svn_boolean_t recursive,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;

  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path, TRUE,
                                 recursive ? -1 : 0,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));

  SVN_ERR(svn_wc_resolved_conflict2(path, adm_access, TRUE, TRUE, recursive,
                                    ctx->notify_func2, ctx->notify_baton2,
                                    ctx->cancel_func, ctx->cancel_baton,
                                    pool));

  SVN_ERR(svn_wc_adm_close(adm_access));

  return SVN_NO_ERROR;
}
