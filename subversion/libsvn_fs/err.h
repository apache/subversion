/*
 * err.h : interface to routines for returning Berkeley DB errors
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



#ifndef SVN_LIBSVN_FS_ERR_H
#define SVN_LIBSVN_FS_ERR_H

#include "apr_pools.h"
#include "svn_error.h"
#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Return an svn_error_t object that reports a Berkeley DB error.
   DB_ERR is the error value returned by the Berkeley DB routine.
   Allocate the error object from POOL.  */
svn_error_t *svn_fs__dberr (apr_pool_t *pool, int db_err);


/* Allocate an error object for a Berkeley DB error, with a formatted message.

   POOL is the APR pool to allocate the svn_error_t object from.
   DB_ERR is the Berkeley DB error code.
   FMT is a printf-style format string, describing how to format any
      subsequent arguments.

   The svn_error_t object returned has a message consisting of:
   - the text specified by FMT and the subsequent arguments, and
   - the Berkeley DB error message for the error code DB_ERR.

   There is no separator between the two messages; if you want one,
   you should include it in FMT.  */
svn_error_t *svn_fs__dberrf (apr_pool_t *pool, int db_err,
                             const char *fmt, ...);


/* Check the return status from the Berkeley DB operation.  If the
   operation succeeded, return zero.  Otherwise, construct an
   appropriate Subversion error object describing what went wrong.
   - FS is the Subversion filesystem we're operating on.
   - OPERATION is a gerund clause describing what we were trying to do.
   - DB_ERR is the return status from the Berkeley DB function.  */
svn_error_t *svn_fs__wrap_db (svn_fs_t *fs,
                              const char *operation,
                              int db_err);


/* A terse wrapper for svn_fs__check_db.  */
#define DB_WRAP(fs, op, err) (svn_fs__wrap_db ((fs), (op), (err)))


/* If EXPR returns a non-zero value, return it.  This is like SVN_ERR,
   but for functions that return a Berkeley DB error code.  */
#define DB_ERR(expr)                            \
  do {                                          \
    int db_err__temp = (expr);                  \
    if (db_err__temp)                           \
      return db_err__temp;                      \
  } while (0)


/* Verify that FS refers to an open database; return an appropriate
   error if this is not the case.  */
svn_error_t *svn_fs__check_fs (svn_fs_t *fs);



/* Building common error objects.  */


/* SVN_ERR_FS_CORRUPT: the REPRESENTATION skel of node ID in FS is corrupt.  */
svn_error_t *svn_fs__err_corrupt_representation (svn_fs_t *fs,
                                                 const svn_fs_id_t *id);

/* SVN_ERR_FS_CORRUPT: the NODE-REVISION skel of node ID in FS is corrupt.  */
svn_error_t *svn_fs__err_corrupt_node_revision (svn_fs_t *fs,
                                                const svn_fs_id_t *id);

/* SVN_ERR_FS_CORRUPT: the REVISION skel of revision REV in FS is corrupt.  */
svn_error_t *svn_fs__err_corrupt_fs_revision (svn_fs_t *fs,
                                              svn_revnum_t rev);

/* SVN_ERR_FS_CORRUPT: ID is a node ID, not a node revision ID.  */
svn_error_t *svn_fs__err_corrupt_id (svn_fs_t *fs, const svn_fs_id_t *id);

/* SVN_ERR_FS_CORRUPT: the clone record for BASE_PATH in SVN_TXN in FS
   is corrupt.  */
svn_error_t *svn_fs__err_corrupt_clone (svn_fs_t *fs,
                                        const char *svn_txn,
                                        const char *base_path);

/* SVN_ERR_FS_ID_NOT_FOUND: something in FS refers to node revision
   ID, but that node revision doesn't exist.  */
svn_error_t *svn_fs__err_dangling_id (svn_fs_t *fs, const svn_fs_id_t *id);

/* SVN_ERR_FS_CORRUPT: something in FS refers to filesystem revision REV,
   but that filesystem revision doesn't exist.  */
svn_error_t *svn_fs__err_dangling_rev (svn_fs_t *fs, svn_revnum_t rev);

/* SVN_ERR_FS_CORRUPT: a key in FS's `nodes' table is bogus.  */
svn_error_t *svn_fs__err_corrupt_nodes_key (svn_fs_t *fs);

/* SVN_ERR_FS_CORRUPT: the `next-id' value is the `transactions' table
   is bogus.  */
svn_error_t *svn_fs__err_corrupt_next_txn_id (svn_fs_t *fs);

/* SVN_ERR_FS_CORRUPT: the entry for TXN in the `transactions' table
   is corrupt.  */
svn_error_t *svn_fs__err_corrupt_txn (svn_fs_t *fs, const char *txn);

/* SVN_ERR_FS_CORRUPT: the entry for COPY_ID in the `copies' table
   is corrupt.  */
svn_error_t *svn_fs__err_corrupt_copy (svn_fs_t *fs, const char *copy_id);

/* SVN_ERR_FS_NOT_MUTABLE: the caller attempted to change a node
   outside of a transaction.  */
svn_error_t *svn_fs__err_not_mutable (svn_fs_t *fs, svn_revnum_t rev,
                                      const char *path);

/* SVN_ERR_FS_PATH_SYNTAX: PATH is not a valid path name.  */
svn_error_t *svn_fs__err_path_syntax (svn_fs_t *fs, const char *path);

/* SVN_ERR_FS_NO_SUCH_TRANSACTION: there is no transaction named TXN in FS.  */
svn_error_t *svn_fs__err_no_such_txn (svn_fs_t *fs, const char *txn);

/* SVN_ERR_FS_NO_SUCH_COPY: there is no copy with id COPY_ID in FS.  */
svn_error_t *svn_fs__err_no_such_copy (svn_fs_t *fs, const char *copy_id);

/* SVN_ERR_FS_NOT_DIRECTORY: PATH does not refer to a directory in FS.  */
svn_error_t *svn_fs__err_not_directory (svn_fs_t *fs, const char *path);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_ERR_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
