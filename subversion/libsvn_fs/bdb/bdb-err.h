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



#ifndef SVN_LIBSVN_FS_BDB_ERR_H
#define SVN_LIBSVN_FS_BDB_ERR_H

#include <apr_pools.h>

#include "svn_error.h"
#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Return an svn_error_t object that reports a Berkeley DB error.
   BDB_ERR is the error value returned by the Berkeley DB routine.
   Allocate the error object from POOL.  */
svn_error_t *svn_fs__bdb_dberr (int db_err);


/* Allocate an error object for a Berkeley DB error, with a formatted message.

   POOL is the APR pool to allocate the svn_error_t object from.
   BDB_ERR is the Berkeley DB error code.
   FMT is a printf-style format string, describing how to format any
      subsequent arguments.

   The svn_error_t object returned has a message consisting of:
   - the text specified by FMT and the subsequent arguments, and
   - the Berkeley DB error message for the error code BDB_ERR.

   There is no separator between the two messages; if you want one,
   you should include it in FMT.  */
svn_error_t *svn_fs__bdb_dberrf (int db_err,
                                 const char *fmt, ...);


/* Check the return status from the Berkeley DB operation.  If the
   operation succeeded, return zero.  Otherwise, construct an
   appropriate Subversion error object describing what went wrong.
   - FS is the Subversion filesystem we're operating on.
   - OPERATION is a gerund clause describing what we were trying to do.
   - BDB_ERR is the return status from the Berkeley DB function.  */
svn_error_t *svn_fs__bdb_wrap_db (svn_fs_t *fs,
                                  const char *operation,
                                  int db_err);


/* A terse wrapper for svn_fs__check_db.  */
#define BDB_WRAP(fs, op, err) (svn_fs__bdb_wrap_db ((fs), (op), (err)))


/* If EXPR returns a non-zero value, return it.  This is like SVN_ERR,
   but for functions that return a Berkeley DB error code.  */
#define BDB_ERR(expr)                            \
  do {                                          \
    int db_err__temp = (expr);                  \
    if (db_err__temp)                           \
      return db_err__temp;                      \
  } while (0)


/* Verify that FS refers to an open database; return an appropriate
   error if this is not the case.  */
svn_error_t *svn_fs__bdb_check_fs (svn_fs_t *fs);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_BDB_ERR_H */
