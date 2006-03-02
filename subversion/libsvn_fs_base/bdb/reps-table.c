/* reps-table.c : operations on the `representations' table
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

#include "bdb_compat.h"
#include "svn_fs.h"
#include "../fs.h"
#include "../util/fs_skels.h"
#include "../err.h"
#include "dbt.h"
#include "../trail.h"
#include "../key-gen.h"
#include "../../libsvn_fs/fs-loader.h"
#include "bdb-err.h"
#include "reps-table.h"
#include "strings-table.h"


#include "svn_private_config.h"


/*** Creating and opening the representations table. ***/

int
svn_fs_bdb__open_reps_table(DB **reps_p,
                            DB_ENV *env,
                            svn_boolean_t create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *reps;

  BDB_ERR(svn_fs_bdb__check_version());
  BDB_ERR(db_create(&reps, env, 0));
  BDB_ERR(reps->open(SVN_BDB_OPEN_PARAMS(reps, NULL),
                     "representations", 0, DB_BTREE,
                     open_flags, 0666));

  /* Create the `next-key' table entry.  */
  if (create)
  {
    DBT key, value;

    BDB_ERR(reps->put
            (reps, 0,
             svn_fs_base__str_to_dbt(&key, NEXT_KEY_KEY),
             svn_fs_base__str_to_dbt(&value, "0"), 0));
  }

  *reps_p = reps;
  return 0;
}



/*** Storing and retrieving reps.  ***/

svn_error_t *
svn_fs_bdb__read_rep(representation_t **rep_p,
                     svn_fs_t *fs,
                     const char *key,
                     trail_t *trail,
                     apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  skel_t *skel;
  int db_err;
  DBT query, result;

  svn_fs_base__trail_debug(trail, "representations", "get");
  db_err = bfd->representations->get(bfd->representations,
                                     trail->db_txn,
                                     svn_fs_base__str_to_dbt(&query, key),
                                     svn_fs_base__result_dbt(&result), 0);
  svn_fs_base__track_dbt(&result, pool);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_REPRESENTATION, 0,
       _("No such representation '%s'"), key);

  /* Handle any other error conditions.  */
  SVN_ERR(BDB_WRAP(fs, _("reading representation"), db_err));

  /* Parse the REPRESENTATION skel.  */
  skel = svn_fs_base__parse_skel(result.data, result.size, pool);

  /* Convert to a native type.  */
  SVN_ERR(svn_fs_base__parse_representation_skel(rep_p, skel, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_bdb__write_rep(svn_fs_t *fs,
                      const char *key,
                      const representation_t *rep,
                      trail_t *trail,
                      apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT query, result;
  skel_t *skel;

  /* Convert from native type to skel. */
  SVN_ERR(svn_fs_base__unparse_representation_skel(&skel, rep, pool));

  /* Now write the record. */
  svn_fs_base__trail_debug(trail, "representations", "put");
  SVN_ERR(BDB_WRAP(fs, _("storing representation"),
                   bfd->representations->put
                   (bfd->representations, trail->db_txn,
                    svn_fs_base__str_to_dbt(&query, key),
                    svn_fs_base__skel_to_dbt(&result, skel, pool),
                    0)));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_bdb__write_new_rep(const char **key,
                          svn_fs_t *fs,
                          const representation_t *rep,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT query, result;
  int db_err;
  apr_size_t len;
  char next_key[MAX_KEY_SIZE];

  /* ### todo: see issue #409 for why bumping the key as part of this
     trail is problematic. */

  /* Get the current value associated with `next-key'.  */
  svn_fs_base__str_to_dbt(&query, NEXT_KEY_KEY);
  svn_fs_base__trail_debug(trail, "representations", "get");
  SVN_ERR(BDB_WRAP(fs, _("allocating new representation (getting next-key)"),
                   bfd->representations->get
                   (bfd->representations, trail->db_txn, &query,
                    svn_fs_base__result_dbt(&result), 0)));

  svn_fs_base__track_dbt(&result, pool);

  /* Store the new rep. */
  *key = apr_pstrmemdup(pool, result.data, result.size);
  SVN_ERR(svn_fs_bdb__write_rep(fs, *key, rep, trail, pool));

  /* Bump to future key. */
  len = result.size;
  svn_fs_base__next_key(result.data, &len, next_key);
  svn_fs_base__trail_debug(trail, "representations", "put");
  db_err = bfd->representations->put
    (bfd->representations, trail->db_txn,
     svn_fs_base__str_to_dbt(&query, NEXT_KEY_KEY),
     svn_fs_base__str_to_dbt(&result, next_key),
     0);

  SVN_ERR(BDB_WRAP(fs, _("bumping next representation key"), db_err));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_bdb__delete_rep(svn_fs_t *fs, 
                       const char *key, 
                       trail_t *trail,
                       apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  int db_err;
  DBT query;

  svn_fs_base__trail_debug(trail, "representations", "del");
  db_err = bfd->representations->del
    (bfd->representations, trail->db_txn,
     svn_fs_base__str_to_dbt(&query, key), 0);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_REPRESENTATION, 0,
       _("No such representation '%s'"), key);

  /* Handle any other error conditions.  */
  SVN_ERR(BDB_WRAP(fs, _("deleting representation"), db_err));

  return SVN_NO_ERROR;
}
