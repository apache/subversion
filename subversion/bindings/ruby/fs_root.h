/*
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
#ifndef SVN_RUBY__FS_ROOT_H
#define SVN_RUBY__FS_ROOT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

VALUE svn_ruby_fs_rev_root_new (svn_fs_root_t *root, apr_pool_t *pool);
VALUE svn_ruby_fs_txn_root_new (svn_fs_root_t *root, apr_pool_t *pool);
svn_fs_root_t *svn_ruby_fs_root (VALUE aRoot);
svn_boolean_t svn_ruby_is_fs_root (VALUE obj);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_RUBY__FS_ROOT_H */
