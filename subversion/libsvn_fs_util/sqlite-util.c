/* sqlite-util.c
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <apr_general.h>
#include <apr_pools.h>

#include <sqlite3.h>

#include "svn_types.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_mergeinfo.h"
#include "svn_pools.h"

#include "private/svn_fs_sqlite.h"
#include "../libsvn_fs/fs-loader.h"
#include "svn_private_config.h"

#include "sqlite-util.h"

#ifdef SQLITE3_DEBUG
/* An sqlite query execution callback. */
static void
sqlite_tracer(void *data, const char *sql)
{
  /*  sqlite3 *db = data; */
  fprintf(stderr, "SQLITE SQL is \"%s\"\n", sql);
}
#endif

struct svn_fs__sqlite_stmt_t
{
  sqlite3_stmt *s3stmt;
  sqlite3 *db;
};

/* Convert SQLite error codes to SVN */
#define SQLITE_ERROR_CODE(x) ((x) == SQLITE_READONLY ?     \
                                SVN_ERR_FS_SQLITE_READONLY \
                              : SVN_ERR_FS_SQLITE_ERROR )

/* SQLITE->SVN quick error wrap, much like SVN_ERR. */
#define SQLITE_ERR(x, db) do                                     \
{                                                                \
  int sqlite_err__temp = (x);                                    \
  if (sqlite_err__temp != SQLITE_OK)                             \
    return svn_error_create(SQLITE_ERROR_CODE(sqlite_err__temp), \
                            NULL, sqlite3_errmsg((db)));         \
} while (0)


svn_error_t *
svn_fs__sqlite_exec(sqlite3 *db, const char *sql)
{
  char *err_msg;
  svn_error_t *err;
  int sqlite_err = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
  if (sqlite_err != SQLITE_OK)
    {
      err = svn_error_create(SQLITE_ERROR_CODE(sqlite_err), NULL, 
                             err_msg);
      sqlite3_free(err_msg);
      return err;
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__sqlite_prepare(svn_fs__sqlite_stmt_t **stmt, sqlite3 *db,
                       const char *text, apr_pool_t *pool)
{
  svn_fs__sqlite_stmt_t *s = apr_palloc(pool, sizeof(*s));
  s->db = db;
  SQLITE_ERR(sqlite3_prepare(db, text, -1, &(s->s3stmt), NULL), db);
  *stmt = s;
  return SVN_NO_ERROR;
}

static svn_error_t *
step_with_expectation(svn_fs__sqlite_stmt_t* stmt, 
                      svn_boolean_t expecting_row)
{
  svn_boolean_t got_row;
  SVN_ERR(svn_fs__sqlite_step(&got_row, stmt));
  if ((got_row && !expecting_row)
      ||
      (!got_row && expecting_row))
    return svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,
                            expecting_row
                            ? _("Expected database row missing")
                            : _("Extra database row found"));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__sqlite_step_done(svn_fs__sqlite_stmt_t *stmt)
{
  return step_with_expectation(stmt, FALSE);
}

svn_error_t *
svn_fs__sqlite_step_row(svn_fs__sqlite_stmt_t *stmt)
{
  return step_with_expectation(stmt, TRUE);
}

svn_error_t *
svn_fs__sqlite_step(svn_boolean_t *got_row, svn_fs__sqlite_stmt_t *stmt)
{
  int sqlite_result = sqlite3_step(stmt->s3stmt);
  if (sqlite_result != SQLITE_DONE && sqlite_result != SQLITE_ROW)
    {
      /* Extract the real error value with finalize. */
      SVN_ERR(svn_fs__sqlite_finalize(stmt));
      /* This really should have thrown an error! */
      abort();
    }

  *got_row = (sqlite_result == SQLITE_ROW);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__sqlite_bind_int64(svn_fs__sqlite_stmt_t *stmt,
                          int slot,
                          sqlite_int64 val)
{
  SQLITE_ERR(sqlite3_bind_int64(stmt->s3stmt, slot, val), stmt->db);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__sqlite_bind_text(svn_fs__sqlite_stmt_t *stmt,
                         int slot,
                         const char *val)
{
  SQLITE_ERR(sqlite3_bind_text(stmt->s3stmt, slot, val, -1, SQLITE_TRANSIENT), 
             stmt->db);
  return SVN_NO_ERROR;
}

const char *
svn_fs__sqlite_column_text(svn_fs__sqlite_stmt_t *stmt, int column)
{
  return (const char *) sqlite3_column_text(stmt->s3stmt, column);
}

svn_revnum_t
svn_fs__sqlite_column_revnum(svn_fs__sqlite_stmt_t *stmt, int column)
{
  return (svn_revnum_t) sqlite3_column_int64(stmt->s3stmt, column);
}

svn_boolean_t
svn_fs__sqlite_column_boolean(svn_fs__sqlite_stmt_t *stmt, int column)
{
  return (sqlite3_column_int64(stmt->s3stmt, column) == 0
          ? FALSE : TRUE);
}

int
svn_fs__sqlite_column_int(svn_fs__sqlite_stmt_t *stmt, int column)
{
  return sqlite3_column_int(stmt->s3stmt, column);
}

svn_error_t *
svn_fs__sqlite_finalize(svn_fs__sqlite_stmt_t *stmt)
{
  SQLITE_ERR(sqlite3_finalize(stmt->s3stmt), stmt->db);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__sqlite_reset(svn_fs__sqlite_stmt_t *stmt)
{
  SQLITE_ERR(sqlite3_reset(stmt->s3stmt), stmt->db);
  return SVN_NO_ERROR;
}

/* Time (in milliseconds) to wait for sqlite locks before giving up. */
#define BUSY_TIMEOUT 10000

static const char *schema_create_sql[] = {
  NULL, /* An empty database is format 0 */

  /* USER_VERSION 1 */
  "PRAGMA auto_vacuum = 1;"
  APR_EOL_STR
  "CREATE TABLE mergeinfo (revision INTEGER NOT NULL, mergedfrom TEXT NOT "
  "NULL, mergedto TEXT NOT NULL, mergedrevstart INTEGER NOT NULL, "
  "mergedrevend INTEGER NOT NULL, inheritable INTEGER NOT NULL);"
  APR_EOL_STR
  "CREATE INDEX mi_mergedfrom_idx ON mergeinfo (mergedfrom);"
  APR_EOL_STR
  "CREATE INDEX mi_mergedto_idx ON mergeinfo (mergedto);"
  APR_EOL_STR
  "CREATE INDEX mi_revision_idx ON mergeinfo (revision);"
  APR_EOL_STR
  "CREATE TABLE mergeinfo_changed (revision INTEGER NOT NULL, path TEXT "
  "NOT NULL);"
  APR_EOL_STR
  "CREATE UNIQUE INDEX mi_c_revpath_idx ON mergeinfo_changed (revision, path);"
  APR_EOL_STR
  "CREATE INDEX mi_c_path_idx ON mergeinfo_changed (path);"
  APR_EOL_STR
  "CREATE INDEX mi_c_revision_idx ON mergeinfo_changed (revision);"
  APR_EOL_STR,

  /* USER_VERSION 2 */
  "CREATE TABLE node_origins (node_id TEXT NOT NULL, node_rev_id TEXT NOT "
  "NULL);"
  APR_EOL_STR
  "CREATE UNIQUE INDEX no_ni_idx ON node_origins (node_id);"
  APR_EOL_STR
};

static const int latest_schema_format =
  sizeof(schema_create_sql)/sizeof(schema_create_sql[0]) - 1;


static svn_error_t *
upgrade_format(sqlite3 *db, int current_format, apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);

  while (current_format < latest_schema_format)
    {
      const char *pragma_cmd;

      svn_pool_clear(iterpool);

      /* Go to the next format */
      current_format++;

      /* Run the upgrade SQL */
      SVN_ERR(svn_fs__sqlite_exec(db, schema_create_sql[current_format]));

      /* Update the user version pragma */
      pragma_cmd = apr_psprintf(iterpool,
                                "PRAGMA user_version = %d;",
                                current_format);
      SVN_ERR(svn_fs__sqlite_exec(db, pragma_cmd));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Check the schema format of the database, upgrading it if necessary.
   Return SVN_ERR_FS_UNSUPPORTED_FORMAT if the schema format is too new, or
   SVN_ERR_FS_SQLITE_ERROR if an sqlite error occurs during
   validation.  Return SVN_NO_ERROR if everything is okay. */
static svn_error_t *
check_format(sqlite3 *db, apr_pool_t *pool)
{
  svn_fs__sqlite_stmt_t *stmt;
  int schema_format;
  
  SVN_ERR(svn_fs__sqlite_prepare(&stmt, db, "PRAGMA user_version;", pool));
  SVN_ERR(svn_fs__sqlite_step_row(stmt));

  /* Validate that the schema exists as expected and that the
     schema and repository versions match. */
  schema_format = svn_fs__sqlite_column_int(stmt, 0);

  SVN_ERR(svn_fs__sqlite_finalize(stmt));

  if (schema_format == latest_schema_format)
    return SVN_NO_ERROR;
  else if (schema_format < latest_schema_format)
    return upgrade_format(db, schema_format, pool);
  else 
    return svn_error_createf(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
                             _("Index schema format %d not "
                               "recognized"), schema_format);
}

/* If possible, verify that SQLite was compiled in a thread-safe
   manner. */
static svn_error_t *
init_sqlite(apr_pool_t *pool)
{
  svn_boolean_t is_threadsafe = TRUE;

  /* SQLite 3.5 allows verification of its thread-safety at runtime.
     Older versions are simply expected to have been configured with
     --enable-threadsafe, which compiles with -DSQLITE_THREADSAFE=1
     (or -DTHREADSAFE, for older versions). */
#ifdef SVN_HAVE_SQLITE_THREADSAFE_PREDICATE
  /* sqlite3_threadsafe() was available at Subversion 'configure'-time. */
  is_threadsafe = sqlite3_threadsafe();
#endif  /* SVN_HAVE_SQLITE_THREADSAFE_PREDICATE */

  if (! is_threadsafe)
    return svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,
                            _("SQLite is required to be compiled and run in "
                              "thread-safe mode"));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__sqlite_open(sqlite3 **db, const char *repos_path, apr_pool_t *pool)
{
  const char *db_path;
  static svn_boolean_t sqlite_initialized = FALSE;

  if (! sqlite_initialized)
    {
      /* There is a potential initialization race condition here, but
         it currently isn't worth guarding against (e.g. with a mutex). */
      SVN_ERR(init_sqlite(pool));
      sqlite_initialized = TRUE;
    }

  db_path = svn_path_join(repos_path, SVN_FS__SQLITE_DB_NAME, pool);

  /* Open the database. */
  SQLITE_ERR(sqlite3_open(db_path, db), *db);
  /* Retry until timeout when database is busy. */
  SQLITE_ERR(sqlite3_busy_timeout(*db, BUSY_TIMEOUT), *db);
#ifdef SQLITE3_DEBUG
  sqlite3_trace(*db, sqlite_tracer, *db);
#endif

  SVN_ERR(svn_fs__sqlite_exec(*db, "PRAGMA case_sensitive_like=on;"));

  /* Validate the schema, upgrading if necessary. */
  return check_format(*db, pool);
}

svn_error_t *
svn_fs__sqlite_close(sqlite3 *db, svn_error_t *err)
{
  int result = sqlite3_close(db);
  /* If there's a pre-existing error, return it. */
  /* ### If the connection close also fails, say something about it as well? */
  SVN_ERR(err);
  SQLITE_ERR(result, db);
  return SVN_NO_ERROR;
}


/* Create an sqlite DB for our mergeinfo index under PATH.  Use POOL
   for temporary allocations. */
svn_error_t *
svn_fs__sqlite_create_index(const char *path, apr_pool_t *pool)
{
  sqlite3 *db;
  /* Opening the database will create it + schema if it's not already there. */
  SVN_ERR(svn_fs__sqlite_open(&db, path, pool));
  return svn_fs__sqlite_close(db, SVN_NO_ERROR);
}
