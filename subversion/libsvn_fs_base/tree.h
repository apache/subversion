/* tree.h : internal interface to tree node functions
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

#ifndef SVN_LIBSVN_FS_TREE_H
#define SVN_LIBSVN_FS_TREE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* These functions implement some of the calls in the FS loader
   library's fs and txn vtables. */

svn_error_t *svn_fs_base__revision_root (svn_fs_root_t **root_p, svn_fs_t *fs,
                                         svn_revnum_t rev, apr_pool_t *pool);

svn_error_t *svn_fs_base__deltify (svn_fs_t *fs, svn_revnum_t rev,
                                   apr_pool_t *pool);

svn_error_t *svn_fs_base__commit_txn (const char **conflict_p,
                                      svn_revnum_t *new_rev, svn_fs_txn_t *txn,
                                      apr_pool_t *pool);

svn_error_t *svn_fs_base__txn_root (svn_fs_root_t **root_p, svn_fs_txn_t *txn,
                                    apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_TREE_H */
