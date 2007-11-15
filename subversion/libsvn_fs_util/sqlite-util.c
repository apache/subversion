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

#include "private/svn_fs_sqlite.h"
#include "../libsvn_fs/fs-loader.h"
#include "svn_private_config.h"

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
  if (sqlite3_exec(db, sql, NULL, NULL, &err_msg) != SQLITE_OK)
    {
      err = svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL, err_msg);
      sqlite3_free(err_msg);
      return err;
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
};

static const int latest_schema_format =
  sizeof(db_create_sql)/sizeof(db_create_sql[0]) - 1;

  "PRAGMA user_version = " APR_STRINGIFY(MERGE_INFO_INDEX_SCHEMA_FORMAT) ";"

/* Return SVN_ERR_FS_GENERAL if the schema format is old or nonexistent,
   SVN_ERR_FS_UNSUPPORTED_FORMAT if the schema format is too new, or
   SVN_ERR_FS_SQLITE_ERROR if an sqlite error occurs during
   validation.  Return SVN_NO_ERROR if everything is okay. */
static svn_error_t *
check_format(sqlite3 *db)
{
  svn_error_t *err = SVN_NO_ERROR;
  sqlite3_stmt *stmt;

  SQLITE_ERR(sqlite3_prepare(db, "PRAGMA user_version;", -1, &stmt, NULL), db);
  if (sqlite3_step(stmt) == SQLITE_ROW)
    {
      /* Validate that the schema exists as expected and that the
         schema and repository versions match. */
      int schema_format = sqlite3_column_int(stmt, 0);
      if (schema_format == latest_schema_format)
        {
          err = SVN_NO_ERROR;
        }
      else if (schema_format < latest_schema_format)
        {
          /* This is likely a freshly-created database in which the
             merge tracking schema doesn't yet exist. */
          err = svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                                  _("Index schema format '%d' needs to be "
                                    "upgraded to current '%d'"),
                                  schema_format, latest_schema_format);
        }
      else 
        {
          err = svn_error_createf(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
                                  _("Index schema format %d not "
                                    "recognized"), schema_format);
        }

      SQLITE_ERR(sqlite3_finalize(stmt), db);
    }
  else
    {
      err = svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,
                             sqlite3_errmsg(db));
    }
  return err;
}

/* Open a connection in *DB to the mergeinfo database under
   REPOS_PATH.  Validate the merge tracking schema, creating it if it
   doesn't yet exist.  This provides a migration path for pre-1.5
   repositories. */
static svn_error_t *
open_db(sqlite3 **db, const char *repos_path, apr_pool_t *pool)
{
  svn_error_t *err;
  const char *db_path = svn_path_join(repos_path,
                                      SVN_FS__SQLITE_DB_NAME, pool);
  SQLITE_ERR(sqlite3_open(db_path, db), *db);
  /* Retry until timeout when database is busy. */
  SQLITE_ERR(sqlite3_busy_timeout(*db, BUSY_TIMEOUT), *db);
#ifdef SQLITE3_DEBUG
  sqlite3_trace(*db, sqlite_tracer, *db);
#endif

  /* Validate the schema. */
  err = check_format(*db);
  if (err && err->apr_err == SVN_ERR_FS_GENERAL)
    {
      /* Assume that we've just created an empty mergeinfo index by
         way of sqlite3_open() (likely from accessing a pre-1.5
         repository), and need to create the merge tracking schema. */
      svn_error_clear(err);
      err = svn_fs__sqlite_exec(*db, SVN_MTD_CREATE_SQL);
    }
  return err;
}
