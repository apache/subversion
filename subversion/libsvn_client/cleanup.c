/*
 * cleanup.c:  wrapper around wc cleanup functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
svn_client_cleanup (const char *dir,
                    apr_pool_t *pool)
{
  svn_node_kind_t kind;

  SVN_ERR (svn_io_check_path (dir, &kind, pool));
  if (kind != svn_node_dir)
    return svn_error_createf (SVN_ERR_WC_NOT_DIRECTORY, 0, NULL, pool,
                              "Cannot cleanup '%s' -- not a directory", 
                              dir);

  return svn_wc_cleanup (dir, NULL, pool);
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
