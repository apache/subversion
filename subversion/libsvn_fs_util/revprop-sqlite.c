/* revprop-sqlite.c
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

#include <sqlite3.h>

#include "svn_fs.h"
#include "svn_path.h"

#include "private/svn_fs_revprop.h"
#include "../libsvn_fs/fs-loader.h"
#include "svn_private_config.h"

/* This is a macro implementation of svn_fs_revision_root_revision(), which
   we cannot call from here, because it would create a circular dependency. */
#define REV_ROOT_REV(root)       \
  ((root)->is_txn_root? SVN_INVALID_REVNUM : (root)->rev)

/* SQLITE->SVN quick error wrap, much like SVN_ERR. */
#define SQLITE_ERR(x, db) do                                    \
{                                                               \
  if ((x) != SQLITE_OK)                                         \
    return svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,      \
                            sqlite3_errmsg((db)));              \
} while (0)

#ifdef SQLITE3_DEBUG
/* An sqlite query execution callback. */
static void
sqlite_tracer(void *data, const char *sql)
{
  /*  sqlite3 *db = data; */
  fprintf(stderr, "SQLITE SQL is \"%s\"\n", sql);
}
#endif

/* Execute SQL on the sqlite database DB, and raise an SVN error if the
   result is not okay.  */

static svn_error_t *
util_sqlite_exec(sqlite3 *db, const char *sql,
                 sqlite3_callback callback,
                 void *callbackdata)
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

/* The version number of the schema used to store the revprop index. */
#define REVPROP_INDEX_SCHEMA_FORMAT 1

/* Return SVN_ERR_FS_GENERAL if the schema doesn't exist,
   SVN_ERR_FS_UNSUPPORTED_FORMAT if the schema format is invalid, or
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
      if (schema_format == REVPROP_INDEX_SCHEMA_FORMAT)
        {
          err = SVN_NO_ERROR;
        }
      else if (schema_format == 0)
        {
          /* This is likely a freshly-created database in which the
             schema doesn't yet exist. */
          err = svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                                 _("Revprop schema format not set"));
        }
      else if (schema_format > REVPROP_INDEX_SCHEMA_FORMAT)
        {
          err = svn_error_createf(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
                                  _("Revprop schema format %d "
                                    "not recognized"), schema_format);
        }
      /* else, we may one day want to perform a schema migration. */

      SQLITE_ERR(sqlite3_finalize(stmt), db);
    }
  else
    {
      err = svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,
                             sqlite3_errmsg(db));
    }
  return err;
}

const char SVN_REVPROP_CREATE_SQL[] = "PRAGMA auto_vacuum = 1;"
  APR_EOL_STR 
  "CREATE TABLE revprops (revision INTEGER NOT NULL, name TEXT NOT "
  "NULL, value TEXT NOT NULL);"
  APR_EOL_STR
  "CREATE INDEX rp_revision_idx ON revprops (revision);"
  APR_EOL_STR
  "CREATE INDEX rp_name_idx ON revprops (name);"
  APR_EOL_STR
  "PRAGMA user_version = " APR_STRINGIFY(REVPROP_INDEX_SCHEMA_FORMAT) ";"
  APR_EOL_STR;

/* Open a connection in *DB to the revprop database under
   REPOS_PATH.  Validate the schema, creating it if it doesn't yet
   exist.  This provides a migration path for pre-1.5 repositories. */
static svn_error_t *
open_db(sqlite3 **db, const char *repos_path, apr_pool_t *pool)
{
  svn_error_t *err;
  const char *db_path = svn_path_join(repos_path, 
                                      SVN_FS_REVPROP__DB_NAME, pool);
  SQLITE_ERR(sqlite3_open(db_path, db), *db);
#ifdef SQLITE3_DEBUG
  sqlite3_trace(*db, sqlite_tracer, *db);
#endif

  /* Validate the schema. */
  err = check_format(*db);
  if (err && err->apr_err == SVN_ERR_FS_GENERAL)
    {
      /* Assume that we've just created an empty index by way of
         sqlite3_open() (likely from accessing a pre-1.5 repository),
         and need to create the schema. */
      svn_error_clear(err);
      err = util_sqlite_exec(*db, SVN_REVPROP_CREATE_SQL, NULL, NULL);
    }
  return err;
}

/* Create an sqlite DB for our revprop index under PATH.  Use POOL
   for temporary allocations. */
svn_error_t *
svn_fs_revprop__create_index(const char *path, apr_pool_t *pool)
{
  sqlite3 *db;
  SVN_ERR(open_db(&db, path, pool));
  SQLITE_ERR(sqlite3_close(db), db);
  return SVN_NO_ERROR;
}


/* Index the revprops for each path in REVPROPS (a mapping of const
   char * -> to svn_string_t *). */
static svn_error_t *
index_revprops(sqlite3 *db,
               svn_revnum_t rev,
               apr_hash_t *revprops,
               apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, revprops);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const void *name_v;
      void *value_v;
      const char *name;
      svn_string_t *value;
      sqlite3_stmt *stmt;

      apr_hash_this(hi, &name_v, NULL, &value_v);
      name = name_v;
      value = value_v;
      
      SQLITE_ERR(sqlite3_prepare(db,
                                 "INSERT INTO revprops (revision, name, value) "
                                 "VALUES (?, ?, ?);", -1, &stmt, NULL),
                 db);
      SQLITE_ERR(sqlite3_bind_int64(stmt, 1, rev), db);
      SQLITE_ERR(sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT),
                 db);
      SQLITE_ERR(sqlite3_bind_text(stmt, 3, value->data, value->len, 
                                   SQLITE_TRANSIENT), db);

      if (sqlite3_step(stmt) != SQLITE_DONE)
        return svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,
                                sqlite3_errmsg(db));

      SQLITE_ERR(sqlite3_finalize(stmt), db);
    }
  return SVN_NO_ERROR;
}

/* Replace the revprops for revision REV in filesystem FS with the
   contents of REVPROPS. */
svn_error_t *
svn_fs_revprop__update_index(svn_fs_t *fs, 
                             svn_revnum_t rev,
                             apr_hash_t *revprops,
                             apr_pool_t *pool)
{
  const char *deletestring;
  sqlite3 *db;

  SVN_ERR(open_db(&db, fs->path, pool));
  SVN_ERR(util_sqlite_exec(db, "BEGIN TRANSACTION;", NULL, NULL));

  /* Cleanup the leftovers of any previous, failed transactions
   * involving NEW_REV. */
  deletestring = apr_psprintf(pool,
                              "DELETE FROM revprops WHERE "
                              "revision = %ld;",
                              rev);
  SVN_ERR(util_sqlite_exec(db, deletestring, NULL, NULL));

  /* Record the revprops from the current transaction. */
  SVN_ERR(index_revprops(db, rev, revprops, pool));

  /* This is moved here from FSFS's commit_txn, because we don't want to
   * write the final current file if the sqlite commit fails.
   * On the other hand, if we commit the transaction and end up failing
   * the current file, we just end up with inaccessible data in the
   * database, not a real problem.  */
  SVN_ERR(util_sqlite_exec(db, "COMMIT TRANSACTION;", NULL, NULL));
  SQLITE_ERR(sqlite3_close(db), db);

  return SVN_NO_ERROR;
}
