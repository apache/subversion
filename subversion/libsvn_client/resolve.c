/*
 * resolve.c:  wrapper around wc resolve functionality.
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"



/*** Code. ***/

svn_error_t *
svn_client_resolve (const char *path,
                    svn_wc_notify_func_t notify_func,
                    void *notify_baton,
                    svn_boolean_t recursive,
                    apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;

  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, path, TRUE, recursive,
                                  pool));

  SVN_ERR (svn_wc_resolve_conflict (path, adm_access, TRUE, TRUE, recursive,
                                    notify_func, notify_baton, pool));

  SVN_ERR (svn_wc_adm_close (adm_access));

  return SVN_NO_ERROR;
}
