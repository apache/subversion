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

#include "private/svn_fs_sqlite.h"
#include "private/svn_fs_node_origins.h"
#include "../libsvn_fs/fs-loader.h"
#include "svn_private_config.h"

#include "sqlite-util.h"


/* A flow-control helper macro for sending processing to the 'cleanup'
  label when the local variable 'err' is not SVN_NO_ERROR. */
#define MAYBE_CLEANUP if (err) goto cleanup

static svn_error_t *
set_origin(sqlite3 *db,
           const char *node_id,
           const svn_string_t *node_rev_id,
           apr_pool_t *pool)
{
  sqlite3_stmt *stmt;
  int sqlite_result;

  /* First figure out if it's already there.  (Don't worry, we're in a
     transaction.) */

  SVN_FS__SQLITE_ERR(sqlite3_prepare
                     (db,
                      "SELECT node_rev_id FROM node_origins "
                      "WHERE node_id = ?",
                      -1, &stmt, NULL), db);
  SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 1, node_id, -1,
                                       SQLITE_TRANSIENT), db);
  sqlite_result = sqlite3_step(stmt);
  if (sqlite_result != SQLITE_DONE && sqlite_result != SQLITE_ROW)
    return svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,
                            sqlite3_errmsg(db));
  else if (sqlite_result == SQLITE_ROW)
    {
      const char *old_node_rev_id = (const char *) sqlite3_column_text(stmt, 0);
      SVN_FS__SQLITE_ERR(sqlite3_finalize(stmt), db);

      if (!strcmp(node_rev_id->data, old_node_rev_id))
        return SVN_NO_ERROR;
      else
        return svn_error_createf
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Node origin for '%s' exists with a different "
             "value (%s) than what we were about to store (%s)"),
           node_id, old_node_rev_id, node_rev_id->data);
    }

  SVN_FS__SQLITE_ERR(sqlite3_finalize(stmt), db);


  SVN_FS__SQLITE_ERR(sqlite3_prepare
                     (db,
                      "INSERT INTO node_origins (node_id, "
                      "node_rev_id) VALUES (?, ?);",
                      -1, &stmt, NULL), db);
  SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 1, node_id, -1,
                                       SQLITE_TRANSIENT), db);
  SVN_FS__SQLITE_ERR(sqlite3_bind_text(stmt, 2, node_rev_id->data, -1,
                                       SQLITE_TRANSIENT), db);

  if (sqlite3_step(stmt) != SQLITE_DONE)
    return svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,
                            sqlite3_errmsg(db));

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

  SVN_ERR(svn_fs__sqlite_open(&db, fs->path, pool));
  err = svn_fs__sqlite_exec(db, "BEGIN TRANSACTION;");
  MAYBE_CLEANUP;

  /* XXXdsg Check for conflicts! */
  
  for (hi = apr_hash_first(pool, node_origins);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *node_id;
      const svn_fs_id_t *node_rev_id;

      apr_hash_this(hi, &key, NULL, &val);
      node_id = key;
      node_rev_id = val;

      /* XXXdsg pool management */

      err = set_origin(db, node_id,
                       svn_fs_unparse_id(node_rev_id, pool), pool);
      MAYBE_CLEANUP;
    }

  err = svn_fs__sqlite_exec(db, "COMMIT TRANSACTION;");
  MAYBE_CLEANUP;

 cleanup:
  return svn_fs__sqlite_close(db, err);
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
svn_fs__get_node_origin(const svn_fs_id_t **origin_id,
                        svn_fs_t *fs,
                        const char *node_id,
                        apr_pool_t *pool)
{
  /* XXXdsg Implement! */
  return svn_error_createf(SVN_ERR_FS_NO_SUCH_NODE_ORIGIN, NULL,
                           _("No cached node origin for node id '%s' in "
                             "filesystem '%s'"), node_id, fs->path);
}
