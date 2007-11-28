/* sqlite-util.h
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


#ifndef SVN_LIBSVN_FS_UTIL_SQLITE_H
#define SVN_LIBSVN_FS_UTIL_SQLITE_H

#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Steps the given statement; raises an SVN error (and finalizes the
   statement) if it doesn't return SQLITE_DONE. */
svn_error_t *
svn_fs__sqlite_step_done(sqlite3_stmt *stmt);

/* Steps the given statement; raises an SVN error (and finalizes the
   statement) if it doesn't return SQLITE_ROW. */
svn_error_t *
svn_fs__sqlite_step_row(sqlite3_stmt *stmt);

/* Steps the given statement; raises an SVN error (and finalizes the
   statement) if it doesn't return SQLITE_DONE or SQLITE_ROW.  Sets 
   *GOT_ROW to true iff it got SQLITE_ROW.
*/
svn_error_t *
svn_fs__sqlite_step(svn_boolean_t *got_row, sqlite3_stmt *stmt);

/* Execute SQL on the sqlite database DB, and raise an SVN error if the
   result is not okay.  */
svn_error_t *
svn_fs__sqlite_exec(sqlite3 *db, const char *sql);

/* Open a connection in *DB to the index database under REPOS_PATH.
   Validate the merge schema, creating it if it doesn't yet exist.
   This provides a migration path for pre-1.5 repositories.  Perform
   temporary allocations in POOL. */
svn_error_t *
svn_fs__sqlite_open(sqlite3 **db, const char *repos_path, apr_pool_t *pool);

/* Prepares TEXT as a statement in DB, returning a statement in *STMT. */
svn_error_t *
svn_fs__sqlite_prepare(sqlite3_stmt **stmt, sqlite3 *db, const char *text);

/* Error-handling wrapper around sqlite3_bind_int64. */
svn_error_t *
svn_fs__sqlite_bind_int64(sqlite3_stmt *stmt, int slot, sqlite_int64 val);

/* Error-handling wrapper around sqlite3_bind_text. VAL cannot contain
   zero bytes; we always pass SQLITE_TRANSIENT. */
svn_error_t *
svn_fs__sqlite_bind_text(sqlite3_stmt *stmt, int slot, const char *val);

/* Error-handling wrapper around sqlite3_finalize. */
svn_error_t *
svn_fs__sqlite_finalize(sqlite3_stmt *stmt);

/* Error-handling wrapper around sqlite3_reset. */
svn_error_t *
svn_fs__sqlite_reset(sqlite3_stmt *stmt);

/* Close DB, returning any ERR which may've necessitated an early connection
   closure, or -- if none -- the error from the closure itself. */
svn_error_t *
svn_fs__sqlite_close(sqlite3 *db, svn_error_t *err);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_UTIL_SQLITE_H */
