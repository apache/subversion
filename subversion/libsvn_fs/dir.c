/* dir.c : operations on directories
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <string.h>

#include "svn_fs.h"
#include "fs.h"
#include "dir-low.h"


svn_error_t *
svn_fs_open_node (svn_fs_node_t **child_p,
		  svn_fs_node_t *parent,
		  const char *path,
		  apr_pool_t *pool)
{
  
}
