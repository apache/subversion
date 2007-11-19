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

svn_error_t *
svn_fs__sqlite_exec(sqlite3 *db, const char *sql)
{
  char *err_msg;
  svn_error_t *err;
  int sqlite_err = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
  if (sqlite_err != SQLITE_OK)
    {
      err = svn_error_create(SVN_FS__SQLITE_ERROR_CODE(sqlite_err), NULL, 
                             err_msg);
      sqlite3_free(err_msg);
      return err;
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__sqlite_step_done(sqlite3_stmt *stmt)
{
  int sqlite_result = sqlite3_step(stmt);
  if (sqlite_result != SQLITE_DONE)
    {
      sqlite3 *db = sqlite3_db_handle(stmt);
      sqlite3_finalize(stmt);
      return svn_error_create(SVN_FS__SQLITE_ERROR_CODE(sqlite_result),
                              NULL,
                              sqlite3_errmsg(db));
    }
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
  sqlite3_stmt *stmt;
  int sqlite_result;

  SVN_FS__SQLITE_ERR(sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, 
                                        NULL), db);
  sqlite_result = sqlite3_step(stmt);

  if (sqlite_result == SQLITE_ROW)
    {
      /* Validate that the schema exists as expected and that the
         schema and repository versions match. */
      int schema_format = sqlite3_column_int(stmt, 0);

      SVN_FS__SQLITE_ERR(sqlite3_finalize(stmt), db);

      if (schema_format == latest_schema_format)
        return SVN_NO_ERROR;
      else if (schema_format < latest_schema_format)
        return upgrade_format(db, schema_format, pool);
      else 
        return svn_error_createf(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
                                 _("Index schema format %d not "
                                   "recognized"), schema_format);
    }
  else
    return svn_error_create(SVN_FS__SQLITE_ERROR_CODE(sqlite_result), NULL,
                            sqlite3_errmsg(db));
}

svn_error_t *
svn_fs__sqlite_open(sqlite3 **db, const char *repos_path, apr_pool_t *pool)
{
  const char *db_path = svn_path_join(repos_path,
                                      SVN_FS__SQLITE_DB_NAME, pool);
  SVN_FS__SQLITE_ERR(sqlite3_open(db_path, db), *db);
  /* Retry until timeout when database is busy. */
  SVN_FS__SQLITE_ERR(sqlite3_busy_timeout(*db, BUSY_TIMEOUT), *db);
#ifdef SQLITE3_DEBUG
  sqlite3_trace(*db, sqlite_tracer, *db);
#endif

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
  SVN_FS__SQLITE_ERR(result, db);
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
