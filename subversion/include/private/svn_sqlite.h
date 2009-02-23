/* svn_sqlite.h
 *
 * ====================================================================
 * Copyright (c) 2008-2009 CollabNet.  All rights reserved.
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


#ifndef SVN_SQLITE_H
#define SVN_SQLITE_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


typedef struct svn_sqlite__db_t svn_sqlite__db_t;
typedef struct svn_sqlite__stmt_t svn_sqlite__stmt_t;

typedef enum svn_sqlite__mode_e {
    svn_sqlite__mode_readonly,   /* open the database read-only */
    svn_sqlite__mode_readwrite,  /* open the database read-write */
    svn_sqlite__mode_rwcreate    /* open/create the database read-write */
} svn_sqlite__mode_t;


/* Steps the given statement; if it returns SQLITE_DONE, resets the statement.
   Otherwise, raises an SVN error.  */
svn_error_t *
svn_sqlite__step_done(svn_sqlite__stmt_t *stmt);

/* Steps the given statement; raises an SVN error (and resets the
   statement) if it doesn't return SQLITE_ROW. */
svn_error_t *
svn_sqlite__step_row(svn_sqlite__stmt_t *stmt);

/* Steps the given statement; raises an SVN error (and resets the
   statement) if it doesn't return SQLITE_DONE or SQLITE_ROW.  Sets
   *GOT_ROW to true iff it got SQLITE_ROW.
*/
svn_error_t *
svn_sqlite__step(svn_boolean_t *got_row, svn_sqlite__stmt_t *stmt);

/* Perform an insert as given by the prepared and bound STMT, and set
   *ROW_ID to the id of the inserted row if ROW_ID is non-NULL.
   STMT will be reset prior to returning. */
svn_error_t *
svn_sqlite__insert(apr_int64_t *row_id, svn_sqlite__stmt_t *stmt);

/* Execute SQL on the sqlite database DB, and raise an SVN error if the
   result is not okay.  */
svn_error_t *
svn_sqlite__exec(svn_sqlite__db_t *db, const char *sql);

/* Return in *VERSION the version of the schema for the database as PATH.
   Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_sqlite__get_schema_version(int *version,
                               const char *path,
                               apr_pool_t *scratch_pool);

/* Open a connection in *DB to the database at PATH. Validate the schema,
   creating/upgrading to LATEST_SCHEMA if needed using the instructions
   in UPGRADE_SQL. The resulting DB is allocated in RESULT_POOL, and any
   temporary allocations are made in SCRATCH_POOL.

   STATEMENTS is an array of strings which may eventually be executed, the
   last element of which should be NULL.  These strings are not duplicated
   internally, and should have a lifetime at least as long as RESULT_POOL.
   STATEMENTS itself may be NULL, in which case it has no impact.
   See svn_sqlite__get_statement() for how these strings are used.

   The statements will be finalized and the SQLite database will be closed
   when RESULT_POOL is cleaned up. */
svn_error_t *
svn_sqlite__open(svn_sqlite__db_t **db, const char *repos_path,
                 svn_sqlite__mode_t mode, const char * const statements[],
                 int latest_schema, const char * const *upgrade_sql,
                 apr_pool_t *result_pool, apr_pool_t *scratch_pool);

/* Returns the statement in *STMT which has been prepared from the
   STATEMENTS[STMT_IDX] string.  This statement is allocated in the same
   pool as the DB, and will be cleaned up with DB is closed. */
svn_error_t *
svn_sqlite__get_statement(svn_sqlite__stmt_t **stmt, svn_sqlite__db_t *db,
                          int stmt_idx);

/* Prepares TEXT as a statement in DB, returning a statement in *STMT,
   allocated in RESULT_POOL. */
svn_error_t *
svn_sqlite__prepare(svn_sqlite__stmt_t **stmt, svn_sqlite__db_t *db,
                    const char *text, apr_pool_t *result_pool);


/* ---------------------------------------------------------------------

   BINDING VALUES

*/

/* Bind values to arguments in STMT, according to FMT.  FMT may contain:

   Spec  Argument type       Item type
   ----  -----------------   ---------
   i     apr_int64_t         Number
   s     const char **       String
   b     const void *        Blob (must be followed by an additional argument
                                   of type apr_size_t with the number of bytes
                                   in the object)

  Each character in FMT maps to one argument, in the order they appear.
*/
svn_error_t *
svn_sqlite__bindf(svn_sqlite__stmt_t *stmt, const char *fmt, ...);

/* Error-handling wrapper around sqlite3_bind_int64. */
svn_error_t *
svn_sqlite__bind_int(svn_sqlite__stmt_t *stmt, int slot, int val);

/* Error-handling wrapper around sqlite3_bind_int64. */
svn_error_t *
svn_sqlite__bind_int64(svn_sqlite__stmt_t *stmt, int slot,
                       apr_int64_t val);

/* Error-handling wrapper around sqlite3_bind_text. VAL cannot contain
   zero bytes; we always pass SQLITE_TRANSIENT. */
svn_error_t *
svn_sqlite__bind_text(svn_sqlite__stmt_t *stmt, int slot,
                      const char *val);

/* Error-handling wrapper around sqlite3_bind_blob. */
svn_error_t *
svn_sqlite__bind_blob(svn_sqlite__stmt_t *stmt,
                      int slot,
                      const void *val,
                      apr_size_t len);

/* Bind a set of properties to the given slot. If PROPS is NULL, then no
   binding will occur. PROPS will be stored as a serialized skel. */
svn_error_t *
svn_sqlite__bind_properties(svn_sqlite__stmt_t *stmt,
                            int slot,
                            const apr_hash_t *props,
                            apr_pool_t *scratch_pool);

/* Bind a checksum's value to the given slot. If CHECKSUM is NULL, then no
   binding will occur. */
svn_error_t *
svn_sqlite__bind_checksum(svn_sqlite__stmt_t *stmt,
                          int slot,
                          const svn_checksum_t *checksum,
                          apr_pool_t *scratch_pool);


/* ---------------------------------------------------------------------

   FETCHING VALUES

*/

/* Wrapper around sqlite3_column_blob and sqlite3_column_bytes. The return
   value will be NULL if the column is null. */
const void *
svn_sqlite__column_blob(svn_sqlite__stmt_t *stmt, int column, apr_size_t *len);

/* Wrapper around sqlite3_column_text. If the column is null, then the
   return value will be NULL. If RESULT_POOL is not NULL, allocate the
   return value (if any) in it. Otherwise, the value will become invalid
   on the next invocation of svn_sqlite__column_* */
const char *
svn_sqlite__column_text(svn_sqlite__stmt_t *stmt, int column,
                        apr_pool_t *result_pool);

/* Wrapper around sqlite3_column_int64. If the column is null, then the
   return value will be SVN_INVALID_REVNUM. */
svn_revnum_t
svn_sqlite__column_revnum(svn_sqlite__stmt_t *stmt, int column);

/* Wrapper around sqlite3_column_int64. If the column is null, then the
   return value will be FALSE. */
svn_boolean_t
svn_sqlite__column_boolean(svn_sqlite__stmt_t *stmt, int column);

/* Wrapper around sqlite3_column_int. If the column is null, then the
   return value will be 0. */
int
svn_sqlite__column_int(svn_sqlite__stmt_t *stmt, int column);

/* Wrapper around sqlite3_column_int64. If the column is null, then the
   return value will be 0. */
apr_int64_t
svn_sqlite__column_int64(svn_sqlite__stmt_t *stmt, int column);

/* Return the column as a hash of const char * => const svn_string_t *.
   If the column is null, then NULL will be stored into *PROPS. The
   results will be allocated in RESULT_POOL, and any temporary allocations
   will be made in SCRATCH_POOL. */
svn_error_t *
svn_sqlite__column_properties(apr_hash_t **props,
                              svn_sqlite__stmt_t *stmt,
                              int column,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);

/* Return the column as a checksum. If the column is null, then NULL will
   be stored into *CHECKSUM. The result will be allocated in RESULT_POOL. */
svn_error_t *
svn_sqlite__column_checksum(svn_checksum_t **checksum,
                            svn_sqlite__stmt_t *stmt,
                            int column,
                            apr_pool_t *result_pool);

/* Return TRUE if the result of selecting the column is null,
   FALSE otherwise */
svn_boolean_t
svn_sqlite__column_is_null(svn_sqlite__stmt_t *stmt, int column);


/* --------------------------------------------------------------------- */


/* Error-handling wrapper around sqlite3_finalize. */
svn_error_t *
svn_sqlite__finalize(svn_sqlite__stmt_t *stmt);

/* Error-handling wrapper around sqlite3_reset. */
svn_error_t *
svn_sqlite__reset(svn_sqlite__stmt_t *stmt);


/* Wrapper around sqlite transaction handling. */
svn_error_t *
svn_sqlite__transaction_begin(svn_sqlite__db_t *db);

/* Wrapper around sqlite transaction handling. */
svn_error_t *
svn_sqlite__transaction_commit(svn_sqlite__db_t *db);

/* Wrapper around sqlite transaction handling. */
svn_error_t *
svn_sqlite__transaction_rollback(svn_sqlite__db_t *db);

/* Callback function to for use with svn_sqlite__with_transaction(). */
typedef svn_error_t *(*svn_sqlite__transaction_callback_t)
  (void *baton, svn_sqlite__db_t *db);

/* Helper function to handle SQLite transactions.  All the work done inside
   CB_FUNC will be wrapped in an SQLite transaction, which will be committed
   if CB_FUNC does not return an error.  If any error is returned from CB_FUNC,
   the transaction will be rolled back.  DB and CB_BATON will be passed to
   CB_FUNC. */
svn_error_t *
svn_sqlite__with_transaction(svn_sqlite__db_t *db,
                             svn_sqlite__transaction_callback_t cb_func,
                             void *cb_baton);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_SQLITE_H */
