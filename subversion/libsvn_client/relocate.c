/*
 * relocate.c:  wrapper around wc relocation functionality.
 *
 * ====================================================================
 * Copyright (c) 2002 CollabNet.  All rights reserved.
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
svn_client_relocate (const char *path,
                     const char *from,
                     const char *to,
                     svn_boolean_t recurse,
                     apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;

  SVN_ERR (svn_wc_adm_probe_open(&adm_access, NULL, path,
                                 TRUE, recurse, pool));

  SVN_ERR(svn_wc_relocate(path, adm_access, from, to, recurse, pool));

  SVN_ERR(svn_wc_adm_close(adm_access));

  return SVN_NO_ERROR;
}
