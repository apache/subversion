/* tree.h : internal interface to tree node functions
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

#ifndef SVN_LIBSVN_FS_TREE_H
#define SVN_LIBSVN_FS_TREE_H


/* Set *ROOT_P to a node referring to the root of TXN in FS, as part
   of TRAIL.  */
svn_error_t *svn_fs__txn_root_node (svn_fs_node_t **root_p,
                                    svn_fs_t *fs,
                                    const char *txn,
                                    trail_t *trail);


#endif /* SVN_LIBSVN_FS_TREE_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
