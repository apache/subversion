/* bdb_fs.h : interface to Subversion filesystem, private to libsvn_fs/bdb
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

#ifndef SVN_BDB_FS_H
#define SVN_BDB_FS_H

#include <db.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * Temporary public declarations. The functions listed here become static
 * when vtables are implemented in a later patch.
 * When that occurs the prefix will be bdb_
 */
svn_error_t *
svn_fs__bdb_set_berkeley_errcall (svn_fs_t *fs, 
                                  void (*db_errcall_fcn) (const char *errpfx,
                                                          char *msg));
apr_status_t svn_fs__bdb_cleanup_fs_apr (void *data);
svn_error_t *svn_fs__bdb_create_fs (svn_fs_t *fs, const char *path, void *cfg);
svn_error_t *svn_fs__bdb_open_fs (svn_fs_t *fs, const char *path);
svn_error_t *svn_fs__bdb_recover_fs (const char *path, apr_pool_t *pool);
svn_error_t *svn_fs__bdb_delete_fs (const char *path, apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_BDB_FS_H */
