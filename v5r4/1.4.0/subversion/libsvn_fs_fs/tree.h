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



/* Set *ROOT_P to the root directory of revision REV in filesystem FS.
   Allocate the structure in POOL. */
svn_error_t *svn_fs_fs__revision_root(svn_fs_root_t **root_p, svn_fs_t *fs,
                                      svn_revnum_t rev, apr_pool_t *pool);

/* Does nothing, but included for Subversion 1.0.x compatibility. */
svn_error_t *svn_fs_fs__deltify(svn_fs_t *fs, svn_revnum_t rev,
                                apr_pool_t *pool);

/* Commit the transaction TXN as a new revision.  Return the new
   revision in *NEW_REV.  If the transaction conflicts with other
   changes return SVN_ERR_FS_CONFLICT and set *CONFLICT_P to a string
   that details the cause of the conflict.  Perform temporary
   allocations in POOL. */
svn_error_t *svn_fs_fs__commit_txn(const char **conflict_p,
                                   svn_revnum_t *new_rev, svn_fs_txn_t *txn,
                                   apr_pool_t *pool);

/* Set ROOT_P to the root directory of transaction TXN.  Allocate the
   structure in POOL. */
svn_error_t *svn_fs_fs__txn_root(svn_fs_root_t **root_p, svn_fs_txn_t *txn,
                                 apr_pool_t *pool);

/* Set KIND_P to the node kind of the node at PATH in ROOT.
   Allocate the structure in POOL. */
svn_error_t *
svn_fs_fs__check_path(svn_node_kind_t *kind_p,
                      svn_fs_root_t *root,
                      const char *path,
                      apr_pool_t *pool);

/* Set *REVISION to the revision in which PATH under ROOT was created.
   Use POOL for any temporary allocations.  If PATH is in an
   uncommitted transaction, *REVISION will be set to
   SVN_INVALID_REVNUM. */
svn_error_t *
svn_fs_fs__node_created_rev(svn_revnum_t *revision,
                            svn_fs_root_t *root,
                            const char *path,
                            apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_TREE_H */
