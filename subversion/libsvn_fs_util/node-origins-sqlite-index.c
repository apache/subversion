/* node-origins-sqlite-index.c
 *
 * ====================================================================
 * Copyright (c) 2006-2007 CollabNet.  All rights reserved.
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
#include "svn_pools.h"

#include "private/svn_fs_sqlite.h"
#include "private/svn_fs_node_origins.h"
#include "../libsvn_fs/fs-loader.h"
#include "svn_private_config.h"

#include "sqlite-util.h"


/* A flow-control helper macro for sending processing to the 'cleanup'
  label when the local variable 'err' is not SVN_NO_ERROR. */
#define MAYBE_CLEANUP if (err) goto cleanup

static svn_error_t *
get_origin(const char **node_rev_id,
           sqlite3 *db,
           const char *node_id,
           apr_pool_t *pool)
{
  sqlite3_stmt *stmt;
  int sqlite_result;

  SVN_FS__SQLITE_ERR(sqlite3_prepare_v2
                     (db,
                      "SELECT node_rev_id FROM node_origins "
                      "WHERE node_id = ?",
                      -1, &stmt, NULL), db);
  SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 1, node_id, -1,
                                       SQLITE_TRANSIENT), db);
  sqlite_result = sqlite3_step(stmt);
  if (sqlite_result != SQLITE_DONE && sqlite_result != SQLITE_ROW)
    return svn_error_create(SVN_FS__SQLITE_ERROR_CODE(sqlite_result), NULL,
                            sqlite3_errmsg(db));
  else if (sqlite_result == SQLITE_ROW)
    *node_rev_id = apr_pstrdup(pool,
                               (const char *) sqlite3_column_text(stmt, 0));
  else
    *node_rev_id = NULL;

  SVN_FS__SQLITE_ERR(sqlite3_finalize(stmt), db);

  return SVN_NO_ERROR;
}

static svn_error_t *
set_origin(sqlite3 *db,
           const char *node_id,
           const svn_string_t *node_rev_id,
           apr_pool_t *pool)
{
  sqlite3_stmt *stmt;
  const char *old_node_rev_id;

  /* First figure out if it's already there.  (Don't worry, we're in a
     transaction.) */
  SVN_ERR(get_origin(&old_node_rev_id, db, node_id, pool));
  if (old_node_rev_id != NULL)
    {
      if (!strcmp(node_rev_id->data, old_node_rev_id))
        return SVN_NO_ERROR;
      else
        return svn_error_createf
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Node origin for '%s' exists with a different "
             "value (%s) than what we were about to store (%s)"),
           node_id, old_node_rev_id, node_rev_id->data);
    }

  SVN_FS__SQLITE_ERR(sqlite3_prepare_v2
                     (db,
                      "INSERT INTO node_origins (node_id, "
                      "node_rev_id) VALUES (?, ?);",
                      -1, &stmt, NULL), db);
  SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 1, node_id, -1,
                                       SQLITE_TRANSIENT), db);
  SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 2, node_rev_id->data, -1,
                                       SQLITE_TRANSIENT), db);

  SVN_ERR(svn_fs__sqlite_step_done(stmt));

  SVN_FS__SQLITE_ERR(sqlite3_finalize(stmt), db);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__set_node_origins(svn_fs_t *fs,
                         apr_hash_t *node_origins,
                         apr_pool_t *pool)
{
  sqlite3 *db;
  apr_hash_index_t *hi;
  svn_error_t *err;
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_pool_t *iterpool = svn_pool_create(subpool);

  SVN_ERR(svn_fs__sqlite_open(&db, fs->path, subpool));
  err = svn_fs__sqlite_exec(db, "BEGIN TRANSACTION;");
  MAYBE_CLEANUP;

  for (hi = apr_hash_first(subpool, node_origins);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *node_id;
      const svn_fs_id_t *node_rev_id;

      svn_pool_clear(iterpool);

      apr_hash_this(hi, &key, NULL, &val);
      node_id = key;
      node_rev_id = val;

      err = set_origin(db, node_id,
                       svn_fs_unparse_id(node_rev_id, iterpool),
                       iterpool);
      MAYBE_CLEANUP;
    }

  err = svn_fs__sqlite_exec(db, "COMMIT TRANSACTION;");
  MAYBE_CLEANUP;

 cleanup:
  /* It's just an "optional" cache, so it's OK if the database is
     readonly. */
  /* ### Instead of checking twice here, maybe add an IGNORE_READONLY
     ### argument to svn_fs__sqlite_close? */
  if (err && err->apr_err == SVN_ERR_FS_SQLITE_READONLY)
    {
      svn_error_clear(err);
      err = NULL;
    }
  err = svn_fs__sqlite_close(db, err);
  if (err && err->apr_err == SVN_ERR_FS_SQLITE_READONLY)
    {
      svn_error_clear(err);
      err = NULL;
    }
  svn_pool_destroy(iterpool);
  svn_pool_destroy(subpool);
  return err;
}

svn_error_t *
svn_fs__set_node_origin(svn_fs_t *fs,
                        const char *node_id,
                        const svn_fs_id_t *node_rev_id,
                        apr_pool_t *pool)
{
  apr_hash_t *origins = apr_hash_make(pool);
  
  apr_hash_set(origins, node_id, APR_HASH_KEY_STRING, node_rev_id);
  
  return svn_fs__set_node_origins(fs, origins, pool);
}

svn_error_t *
svn_fs__get_node_origin(const char **origin_id,
                        svn_fs_t *fs,
                        const char *node_id,
                        apr_pool_t *pool)
{
  sqlite3 *db;
  svn_error_t *err;

  SVN_ERR(svn_fs__sqlite_open(&db, fs->path, pool));

  err = get_origin(origin_id, db, node_id, pool);

  return svn_fs__sqlite_close(db, err);
}
