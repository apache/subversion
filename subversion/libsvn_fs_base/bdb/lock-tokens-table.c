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
                            const char *lock_token,
                            trail_t *trail)
{

  base_fs_data_t *bfd = fs->fsap_data;
  DBT key, value;

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
                               trail_t *trail)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT key;
  int db_err;

  svn_fs_base__str_to_dbt (&key, path);
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
                            trail_t *trail)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT key, value;
  int db_err;

  svn_fs_base__trail_debug (trail, "lock-tokens", "get");
  db_err = bfd->lock_tokens->get (bfd->lock_tokens, trail->db_txn,
                                  svn_fs_base__str_to_dbt (&key, path),
                                  svn_fs_base__result_dbt (&value),
                                  0);
  svn_fs_base__track_dbt (&value, trail->pool);

  if (db_err == DB_NOTFOUND)
    return svn_fs_base__err_no_such_lock (fs, path);
  SVN_ERR (BDB_WRAP (fs, "reading lock token", db_err));

  *lock_token_p = apr_pstrmemdup(trail->pool, value.data, value.size);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_bdb__lock_tokens_get (apr_hash_t **lock_tokens_p,
                             svn_fs_t *fs,
                             const char *path,
                             trail_t *trail)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBC *cursor;
  DBT key, value;
  int db_err;
  apr_pool_t *subpool = svn_pool_create (trail->pool);

  apr_hash_t *lock_tokens = apr_hash_make(trail->pool);

  svn_fs_base__trail_debug (trail, "lock-tokens", "cursor");
  db_err = bfd->lock_tokens->cursor (bfd->lock_tokens, trail->db_txn,
                                     &cursor, 0);  
  SVN_ERR (BDB_WRAP (fs, "creating cursor for reading lock tokens", db_err));

  /* Since the key is going to be returned as well as the value
   * make sure BDB malloc's the returned key.
   */
  svn_fs_base__str_to_dbt (&key, path);
  key.flags |= DB_DBT_MALLOC;

  /* Get the first matching key that is either equal or greater
   * than the one passed in, by passing in the DB_RANGE_SET flag.
   */
  db_err = cursor->c_get(cursor, &key, svn_fs_base__result_dbt (&value),
                         DB_SET_RANGE);

  /* As long as the prefix of the returned KEY matches PATH 
   * we know it is either PATH or a decendant thereof.
   */
  while (! db_err && strncmp(path, key.data, key.size) != 0)
    {
      svn_pool_clear (subpool);

      svn_fs_base__track_dbt (&key, subpool);      
      svn_fs_base__track_dbt (&value, subpool);

      apr_hash_set (lock_tokens,
                    apr_pstrmemdup(trail->pool, key.data, key.size), key.size,
                    apr_pstrmemdup(trail->pool, value.data, value.size));

      svn_fs_base__result_dbt (&key);
      svn_fs_base__result_dbt (&value);
      db_err = cursor->c_get(cursor, &key, &value, DB_NEXT);
    }

  svn_pool_destroy (subpool);
  cursor->c_close(cursor);

  if (db_err && (db_err != DB_NOTFOUND)) 
    SVN_ERR (BDB_WRAP (fs, "fetching lock tokens", db_err));

  *lock_tokens_p = lock_tokens;
  return SVN_NO_ERROR;
}


