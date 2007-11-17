/*
 * svn_fs_sqlite.h: Declarations for APIs of libsvn_fs_util to
 * be consumed by only fs_* libs.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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

#ifndef SVN_FS_SQLITE_H
#define SVN_FS_SQLITE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* The name of the sqlite index database. */
#define SVN_FS__SQLITE_DB_NAME "indexes.sqlite"

/* Create index database under PATH.  Use POOL for any temporary
   allocations. */
svn_error_t *
svn_fs__sqlite_create_index(const char *path, apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_FS_SQLITE_H */
