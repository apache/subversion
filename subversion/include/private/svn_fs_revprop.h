/*
 * svn_fs_revprop.h: Declarations for the APIs of libsvn_fs_util to
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

#ifndef SVN_FS_REVPROP_H
#define SVN_FS_REVPROP_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* The name of the sqlite revprop database. */
#define SVN_FS_REVPROP__DB_NAME "revprop.db"

/* Create the revprop index under PATH.  Use POOL for any temporary
   allocations. */
svn_error_t *
svn_fs_revprop__create_index(const char *path, apr_pool_t *pool);

/* Update the revprop index for revision REV in filesystem FS.
   REVPROPS are the revision properties for the revision (a mapping of
   const char * -> svn_string_t *), possibly an empty hash.  Use POOL
   for any temporary allocations. */
svn_error_t *
svn_fs_revprop__update_index(svn_fs_t *fs,
                             svn_revnum_t rev,
                             apr_hash_t *revprops,
                             apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_FS_REVPROP_H */
