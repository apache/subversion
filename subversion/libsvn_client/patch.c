/*
 * patch.c:  wrapper around wc patch functionality.
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
svn_client_patch(const char *patch_path,
                 const char *wc_path,
                 svn_boolean_t dry_run,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  /*...*/
  return SVN_NO_ERROR;
}
