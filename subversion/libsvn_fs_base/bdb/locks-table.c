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

  svn_fs_base__str_to_dbt (&key, lock_token);
  svn_fs_base__trail_debug (trail, "locks", "del");
  SVN_ERR (BDB_WRAP (fs, "deleting entry from 'locks' table",
                     bfd->locks->del (bfd->locks,
                                      trail->db_txn, &key, 0)));
  
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
  *lock_p = lock;
  return SVN_NO_ERROR;
}

