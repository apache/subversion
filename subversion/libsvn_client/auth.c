/*
 * auth.c:  routines that drive "authenticator" objects received from RA.
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

#include "svn_error.h"
#include "svn_path.h"
#include "client.h"

/*-----------------------------------------------------------------------*/


svn_error_t *
svn_client__dir_if_wc (const char **dir_p,
                       const char *dir,
                       apr_pool_t *pool)
{
  int wc_format;
  
  SVN_ERR (svn_wc_check_wc (dir, &wc_format, pool));
  
  if (wc_format == 0)
    *dir_p = NULL;
  else
    *dir_p = dir;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__default_auth_dir (const char **auth_dir_p,
                              const char *path,
                              apr_pool_t *pool)
{
  svn_node_kind_t kind;

  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if (kind == svn_node_dir)
    {
      SVN_ERR (svn_client__dir_if_wc (auth_dir_p, path, pool));

      /* Handle unversioned dir in a versioned parent. */
      if (! *auth_dir_p)
        goto try_parent;
    }
  else if ((kind == svn_node_file) || (kind == svn_node_none))
    {
    try_parent:
      svn_path_split (path, auth_dir_p, NULL, pool);
      SVN_ERR (svn_client__dir_if_wc (auth_dir_p, *auth_dir_p, pool));
    }
  else
    {
      return svn_error_createf
        (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
         "unknown node kind for '%s'", path);
    }
  
  return SVN_NO_ERROR;
}
