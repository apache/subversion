/* locks-table.c : operations on the `locks' table
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
#include "locks-table.h"
#include "lock-tokens-table.h"



int
svn_fs_bdb__open_locks_table (DB **locks_p,
                              DB_ENV *env,
                              svn_boolean_t create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *locks;

  BDB_ERR (svn_fs_bdb__check_version());
  BDB_ERR (db_create (&locks, env, 0));
  BDB_ERR (locks->open (SVN_BDB_OPEN_PARAMS(locks, NULL),
                        "locks", 0, DB_BTREE,
                        open_flags | SVN_BDB_AUTO_COMMIT,
                        0666));

  *locks_p = locks;
  return 0;
}



svn_error_t *
svn_fs_bdb__lock_add (svn_fs_t *fs,
                      const char *lock_token,
                      svn_lock_t *lock,
                      trail_t *trail)
{
  base_fs_data_t *bfd = fs->fsap_data;
  skel_t *lock_skel;
  DBT key, value;

  /* Convert native type to skel. */
  SVN_ERR (svn_fs_base__unparse_lock_skel (&lock_skel, lock,
                                           trail->pool));

  svn_fs_base__str_to_dbt (&key, lock_token);
  svn_fs_base__skel_to_dbt (&value, lock_skel, trail->pool);
  svn_fs_base__trail_debug (trail, "lock", "add");
  SVN_ERR (BDB_WRAP (fs, "storing lock record",
                     bfd->locks->put (bfd->locks, trail->db_txn,
                                      &key, &value, 0)));

  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_bdb__lock_delete (svn_fs_t *fs,
                         const char *lock_token,
                         trail_t *trail)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT key;
  int db_err;

  svn_fs_base__str_to_dbt (&key, lock_token);
  svn_fs_base__trail_debug (trail, "locks", "del");
  db_err = bfd->locks->del (bfd->locks, trail->db_txn, &key, 0);
  
  if (db_err == DB_NOTFOUND)
    return svn_fs_base__err_bad_lock_token (fs, lock_token);
  SVN_ERR (BDB_WRAP (fs, "deleting lock from 'locks' table", db_err));

  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_bdb__lock_get (svn_lock_t **lock_p,
                      svn_fs_t *fs,
                      const char *lock_token,
                      trail_t *trail)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT key, value;
  int db_err;
  skel_t *skel;
  svn_lock_t *lock;

  svn_fs_base__trail_debug (trail, "lock", "get");
  db_err = bfd->locks->get (bfd->locks, trail->db_txn,
                            svn_fs_base__str_to_dbt (&key, lock_token),
                            svn_fs_base__result_dbt (&value),
                            0);
  svn_fs_base__track_dbt (&value, trail->pool);

  if (db_err == DB_NOTFOUND)
    return svn_fs_base__err_bad_lock_token (fs, lock_token);
  SVN_ERR (BDB_WRAP (fs, "reading lock", db_err));

  /* Parse TRANSACTION skel */
  skel = svn_fs_base__parse_skel (value.data, value.size, trail->pool);
  if (! skel)
    return svn_fs_base__err_corrupt_lock (fs, lock_token);

  /* Convert skel to native type. */
  SVN_ERR (svn_fs_base__parse_lock_skel (&lock, skel,
                                         trail->pool));

  /* Possibly auto-expire the lock. */
  if (lock->expiration_date
      && (apr_time_now() > lock->expiration_date))
    {
      SVN_ERR (svn_fs_bdb__lock_delete (fs, lock_token, trail));
      return svn_fs_base__err_lock_expired (fs, lock_token); 
    }

  *lock_p = lock;
  return SVN_NO_ERROR;
}




svn_error_t *
svn_fs_bdb__locks_get (apr_hash_t **locks_p,
                       svn_fs_t *fs,
                       const char *path,
                       const svn_node_kind_t kind,
                       trail_t *trail)
{
  base_fs_data_t *bfd = fs->fsap_data;
  const char *lookup_path = path;
  DBC *cursor;
  DBT key, value;
  int db_err;
  apr_pool_t *subpool = svn_pool_create (trail->pool);

  apr_hash_t *locks = apr_hash_make(trail->pool);

  if (kind == svn_node_dir)
    lookup_path = apr_pstrcat (trail->pool, path, "/", NULL);

  svn_fs_base__trail_debug (trail, "lock-tokens", "cursor");
  db_err = bfd->lock_tokens->cursor (bfd->lock_tokens, trail->db_txn,
                                     &cursor, 0);  
  SVN_ERR (BDB_WRAP (fs, "creating cursor for reading lock tokens", db_err));

  /* Since the key is going to be returned as well as the value
   * make sure BDB malloc's the returned key.
   */
  svn_fs_base__str_to_dbt (&key, lookup_path);
  key.flags |= DB_DBT_MALLOC;

  /* Get the first matching key that is either equal or greater
   * than the one passed in, by passing in the DB_RANGE_SET flag.
   */
  db_err = cursor->c_get(cursor, &key, svn_fs_base__result_dbt (&value),
                         DB_SET_RANGE);

  /* As long as the prefix of the returned KEY matches LOOKUP_PATH 
   * we know it is either LOOKUP_PATH or a decendant thereof.
   */
  while (! db_err && strncmp(lookup_path, key.data, strlen(lookup_path)) == 0)
    {
      const char *lock_token;
      char *child_path;
      svn_node_kind_t child_kind;
      svn_lock_t *lock;
      svn_error_t *err;

      svn_pool_clear (subpool);

      svn_fs_base__track_dbt (&key, subpool);      
      svn_fs_base__track_dbt (&value, subpool);

      /* Create a usable path and token in temporary memory. */
      child_path = apr_pstrmemdup (subpool, key.data, key.size);
      lock_token = apr_pstrmemdup (subpool, value.data, value.size);

      /* Figure out the node_kind of this child path. */
      child_kind =
        (child_path[key.size - 1] == '/') ? svn_node_dir : svn_node_file;

      /* If the child_path has a trailing '/', we need to remove it,
         to stay compatible with the rest of the fs library. */
      if (child_path[key.size - 1] == '/')
          child_path[key.size - 1] = '\0';

      /* Make sure the token points to an existing, non-expired lock,
         by doing a lookup in the `locks' table. */
      err = svn_fs_bdb__lock_get (&lock, fs, lock_token, trail);
      if (err && ((err->apr_err == SVN_ERR_FS_LOCK_EXPIRED)
                  || (err->apr_err == SVN_ERR_FS_BAD_LOCK_TOKEN)))
        {
          svn_error_clear (err);

          /* If `locks' doesn't have the lock, then we should lose it
             from `lock-tokens' table as well, then skip to the next
             matching path-key. */
          SVN_ERR (svn_fs_bdb__lock_token_delete (fs, child_path,
                                                  child_kind, trail));
          continue;
        }
      else if (err)
        return err;

      /* Lock is verified, return it in the hash. */
      apr_hash_set (locks, apr_pstrdup(trail->pool, child_path), 
                    APR_HASH_KEY_STRING, lock);

      svn_fs_base__result_dbt (&key);
      svn_fs_base__result_dbt (&value);
      db_err = cursor->c_get(cursor, &key, &value, DB_NEXT);
    }

  svn_pool_destroy (subpool);
  cursor->c_close(cursor);

  if (db_err && (db_err != DB_NOTFOUND)) 
    SVN_ERR (BDB_WRAP (fs, "fetching lock tokens", db_err));

  *locks_p = locks;
  return SVN_NO_ERROR;
}

