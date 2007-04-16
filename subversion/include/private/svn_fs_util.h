/*
 * svn_fs_util.h: Declarations for the APIs of libsvn_fs_util to be 
 * consumed by only fs_* libs.
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

#ifndef SVN_FS_UTIL_H
#define SVN_FS_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Return a canonicalized version of a filesystem PATH, allocated in
   POOL.  While the filesystem API is pretty flexible about the
   incoming paths (they must be UTF-8 with '/' as separators, but they
   don't have to begin with '/', and multiple contiguous '/'s are
   ignored) we want any paths that are physically stored in the
   underlying database to look consistent.  Specifically, absolute
   filesystem paths should begin with '/', and all redundant and trailing '/'
   characters be removed.  */
const char *
svn_fs__canonicalize_abspath(const char *path, apr_pool_t *pool);

/* Verify that FS refers to an open database; return an appropriate
   error if this is not the case.  */
svn_error_t *svn_fs__check_fs(svn_fs_t *fs);

/* SVN_ERR_FS_NOT_MUTABLE: the caller attempted to change a node
   outside of a transaction.  */
svn_error_t *svn_fs__err_not_mutable(svn_fs_t *fs, svn_revnum_t rev,
                                        const char *path);

/* SVN_ERR_FS_NOT_DIRECTORY: PATH does not refer to a directory in FS.  */
svn_error_t *svn_fs__err_not_directory(svn_fs_t *fs, const char *path);

/* SVN_ERR_FS_NOT_FILE: PATH does not refer to a file in FS.  */
svn_error_t *svn_fs__err_not_file(svn_fs_t *fs, const char *path);

/* SVN_ERR_FS_PATH_ALREADY_LOCKED: a path is already locked.  */
svn_error_t *svn_fs__err_path_already_locked(svn_fs_t *fs,
                                                svn_lock_t *lock);

/* SVN_ERR_FS_NO_SUCH_LOCK: there is no lock on PATH in FS.  */
svn_error_t *svn_fs__err_no_such_lock(svn_fs_t *fs, const char *path);

/* SVN_ERR_FS_LOCK_EXPIRED: TOKEN's lock in FS has been auto-expired. */
svn_error_t *svn_fs__err_lock_expired(svn_fs_t *fs, const char *token);

/* SVN_ERR_FS_NO_USER: FS does not have a user associated with it. */
svn_error_t *svn_fs__err_no_user(svn_fs_t *fs);

/* SVN_ERR_FS_LOCK_OWNER_MISMATCH: trying to use a lock whose LOCK_OWNER
   doesn't match the USERNAME associated with FS.  */
svn_error_t *svn_fs__err_lock_owner_mismatch(svn_fs_t *fs,
                                                const char *username,
                                                const char *lock_owner);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_FS_UTIL_H */
