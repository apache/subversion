/* file.h : interface to file nodes --- private to libsvn_fs
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

/* ==================================================================== */



#ifndef SVN_LIBSVN_FS_FILE_H
#define SVN_LIBSVN_FS_FILE_H

#include "apr_pools.h"
#include "svn_fs.h"
#include "skel.h"


/* Set *NODE to a new a svn_fs_file_t for node ID in filesystem FS,
   whose NODE-REVISION skel is NV.  NV is allocated in SKEL_POOL, as is
   the data it points to.  NV must be a list skel of at least two
   elements, whose first element is the atom "file".

   The new node must not be added to the node cache, and its open
   count must be zero.

   This function will use SKEL_POOL for any temporary storage it needs
   while constructing the new node.  (Any memory actually used by NODE
   itself goes in NODE's pool, of course.)  */
svn_error_t *svn_fs__file_from_skel (svn_fs_node_t **node,
				     svn_fs_t *fs, 
				     svn_fs_id_t *id,
				     skel_t *nv, 
				     apr_pool_t *skel_pool);


#endif /* SVN_LIBSVN_FS_FILE_H */
