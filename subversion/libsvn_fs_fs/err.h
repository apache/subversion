/*
 * err.h : interface to routines for returning Berkeley DB errors
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



#ifndef SVN_LIBSVN_FS_ERR_H
#define SVN_LIBSVN_FS_ERR_H

#include <apr_pools.h>

#include "svn_error.h"
#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Verify that FS refers to an open database; return an appropriate
   error if this is not the case.  */
svn_error_t *svn_fs_fs__check_fs(svn_fs_t *fs);



/* Building common error objects.  */


/* SVN_ERR_FS_ID_NOT_FOUND: something in FS refers to node revision
   ID, but that node revision doesn't exist.  */
svn_error_t *svn_fs_fs__err_dangling_id(svn_fs_t *fs,
                                        const svn_fs_id_t *id);

/* SVN_ERR_FS_NOT_MUTABLE: the caller attempted to change a node
   outside of a transaction.  */
svn_error_t *svn_fs_fs__err_not_mutable(svn_fs_t *fs, svn_revnum_t rev,
                                        const char *path);

/* SVN_ERR_FS_TRANSACTION_NOT_MUTABLE: trying to change the
   unchangeable transaction named TXN in FS.  */
svn_error_t *svn_fs_fs__err_txn_not_mutable(svn_fs_t *fs, const char *txn);

/* SVN_ERR_FS_NOT_DIRECTORY: PATH does not refer to a directory in FS.  */
svn_error_t *svn_fs_fs__err_not_directory(svn_fs_t *fs, const char *path);

/* SVN_ERR_FS_NOT_FILE: PATH does not refer to a file in FS.  */
svn_error_t *svn_fs_fs__err_not_file(svn_fs_t *fs, const char *path);

/* SVN_ERR_FS_CORRUPT: the lockfile for PATH in FS is corrupt.  */
svn_error_t *svn_fs_fs__err_corrupt_lockfile(svn_fs_t *fs,
                                             const char *path);

/* SVN_ERR_FS_PATH_ALREADY_LOCKED: a path is already locked.  */
svn_error_t *svn_fs_fs__err_path_already_locked(svn_fs_t *fs,
                                                svn_lock_t *lock);

/* SVN_ERR_FS_NO_SUCH_LOCK: there is no lock on PATH in FS.  */
svn_error_t *svn_fs_fs__err_no_such_lock(svn_fs_t *fs, const char *path);

/* SVN_ERR_FS_LOCK_EXPIRED: TOKEN's lock in FS has been auto-expired. */
svn_error_t *svn_fs_fs__err_lock_expired(svn_fs_t *fs, const char *token);

/* SVN_ERR_FS_NO_USER: FS does not have a user associated with it. */
svn_error_t *svn_fs_fs__err_no_user(svn_fs_t *fs);

/* SVN_ERR_FS_LOCK_OWNER_MISMATCH: trying to use a lock whose LOCK_OWNER
   doesn't match the USERNAME associated with FS.  */
svn_error_t *svn_fs_fs__err_lock_owner_mismatch(svn_fs_t *fs,
                                                const char *username,
                                                const char *lock_owner);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_ERR_H */
