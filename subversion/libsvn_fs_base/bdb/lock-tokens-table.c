/* lock-tokens-table.c : operations on the `lock-tokens' table
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include <string.h>
#include <assert.h>
#include "bdb_compat.h"

#include "svn_pools.h"
#include "dbt.h"
#include "../err.h"
#include "../fs.h"
#include "../key-gen.h"
#include "../util/skel.h"
#include "../util/fs_skels.h"
#include "../trail.h"
#include "../../libsvn_fs/fs-loader.h"
#include "bdb-err.h"
#include "lock-tokens-table.h"
#include "locks-table.h"


int
svn_fs_bdb__open_lock_tokens_table (DB **lock_tokens_p,
                                    DB_ENV *env,
                                    svn_boolean_t create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *lock_tokens;

  BDB_ERR (svn_fs_bdb__check_version());
  BDB_ERR (db_create (&lock_tokens, env, 0));
  BDB_ERR (lock_tokens->open (SVN_BDB_OPEN_PARAMS(lock_tokens, NULL),
                              "lock-tokens", 0, DB_BTREE,
                              open_flags | SVN_BDB_AUTO_COMMIT,
                              0666));

  *lock_tokens_p = lock_tokens;
  return 0;
}


svn_error_t *
svn_fs_bdb__lock_token_add (svn_fs_t *fs,
                            const char *path,
                            svn_node_kind_t kind,
                            const char *lock_token,
                            trail_t *trail)
{

  base_fs_data_t *bfd = fs->fsap_data;
  DBT key, value;

  if (kind == svn_node_dir)
    path = apr_pstrcat (trail->pool, path, "/", NULL);

  svn_fs_base__str_to_dbt (&key, path);
  svn_fs_base__str_to_dbt (&value, lock_token);
  svn_fs_base__trail_debug (trail, "lock-tokens", "add");
  SVN_ERR (BDB_WRAP (fs, "storing lock token record",
                     bfd->lock_tokens->put (bfd->lock_tokens, trail->db_txn,
                                           &key, &value, 0)));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_bdb__lock_token_delete (svn_fs_t *fs,
                               const char *path,
                               svn_node_kind_t kind,
                               trail_t *trail)
{
  base_fs_data_t *bfd = fs->fsap_data;
  const char *lookup_path = path;
  DBT key;
  int db_err;

  if (kind == svn_node_dir)
    lookup_path = apr_pstrcat (trail->pool, path, "/", NULL);

  svn_fs_base__str_to_dbt (&key, lookup_path);
  svn_fs_base__trail_debug (trail, "lock-tokens", "del");
  db_err = bfd->lock_tokens->del (bfd->lock_tokens, trail->db_txn, &key, 0);
  
  if (db_err == DB_NOTFOUND)
    return svn_fs_base__err_no_such_lock (fs, path); 
  SVN_ERR (BDB_WRAP (fs, "deleting entry from 'lock-tokens' table", db_err));
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_bdb__lock_token_get (const char **lock_token_p,
                            svn_fs_t *fs,
                            const char *path,
                            svn_node_kind_t kind,
                            trail_t *trail)
{
  base_fs_data_t *bfd = fs->fsap_data;
  const char *lookup_path = path;
  DBT key, value;
  svn_error_t *err;
  svn_lock_t *lock;
  const char *lock_token;
  int db_err;

  if (kind == svn_node_dir)
    lookup_path = apr_pstrcat (trail->pool, path, "/", NULL);

  svn_fs_base__trail_debug (trail, "lock-tokens", "get");
  db_err = bfd->lock_tokens->get (bfd->lock_tokens, trail->db_txn,
                                  svn_fs_base__str_to_dbt (&key, lookup_path),
                                  svn_fs_base__result_dbt (&value),
                                  0);
  svn_fs_base__track_dbt (&value, trail->pool);

  if (db_err == DB_NOTFOUND)
    return svn_fs_base__err_no_such_lock (fs, path);
  SVN_ERR (BDB_WRAP (fs, "reading lock token", db_err));

  lock_token = apr_pstrmemdup (trail->pool, value.data, value.size);

  /* Make sure the token still points to an existing, non-expired
     lock, by doing a lookup in the `locks' table. */
  err = svn_fs_bdb__lock_get (&lock, fs, lock_token, trail);
  if (err && ((err->apr_err == SVN_ERR_FS_LOCK_EXPIRED)
              || (err->apr_err == SVN_ERR_FS_BAD_LOCK_TOKEN)))
    {
      /* If `locks' doesn't have the lock, then we should lose it too. */
      svn_error_t *delete_err;
      delete_err = svn_fs_bdb__lock_token_delete (fs, path, kind, trail);
      if (delete_err)
        svn_error_compose (err, delete_err);
      return err;
    }
  else if (err)
    return err;

  *lock_token_p = lock_token;
  return SVN_NO_ERROR;
}


