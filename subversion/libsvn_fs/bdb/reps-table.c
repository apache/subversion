/* reps-table.c : operations on the `representations' table
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

#include <db.h>
#include "svn_fs.h"
#include "../fs.h"
#include "../util/fs_skels.h"
#include "../err.h"
#include "dbt.h"
#include "../trail.h"
#include "../key-gen.h"
#include "reps-table.h"
#include "strings-table.h"



/*** Creating and opening the representations table. ***/

int
svn_fs__open_reps_table (DB **reps_p,
                         DB_ENV *env,
                         int create)
{
  DB *reps;

  DB_ERR (db_create (&reps, env, 0));
  DB_ERR (reps->open (reps, "representations", 0, DB_BTREE,
                      create ? (DB_CREATE | DB_EXCL) : 0,
                      0666));

  /* Create the `next-key' table entry.  */
  if (create)
  {
    DBT key, value;

    DB_ERR (reps->put
            (reps, 0,
             svn_fs__str_to_dbt (&key, (char *) svn_fs__next_key_key),
             svn_fs__str_to_dbt (&value, (char *) "0"),
             0));
  }

  *reps_p = reps;
  return 0;
}



/*** Storing and retrieving reps.  ***/

svn_error_t *
svn_fs__read_rep (svn_fs__representation_t **rep_p,
                  svn_fs_t *fs,
                  const char *key,
                  trail_t *trail)
{
  skel_t *skel;
  int db_err;
  DBT query, result;
  svn_fs__representation_t *rep;

  db_err = fs->representations->get
    (fs->representations,
     trail->db_txn,
     svn_fs__str_to_dbt (&query, (char *) key),
     svn_fs__result_dbt (&result), 0);

  svn_fs__track_dbt (&result, trail->pool);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_REPRESENTATION, 0, 0, fs->pool,
       "svn_fs__read_rep: no such representation `%s'", key);

  /* Handle any other error conditions.  */
  SVN_ERR (DB_WRAP (fs, "reading representation", db_err));

  /* Parse the REPRESENTATION skel.  */
  skel = svn_fs__parse_skel (result.data, result.size, trail->pool);

  /* Convert to a native type.  */
  SVN_ERR (svn_fs__parse_representation_skel (&rep, skel, trail->pool));
  *rep_p = rep;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__write_rep (svn_fs_t *fs,
                   const char *key,
                   const svn_fs__representation_t *rep,
                   trail_t *trail)
{
  DBT query, result;
  skel_t *skel;

  /* Convert from native type to skel. */
  SVN_ERR (svn_fs__unparse_representation_skel (&skel, rep, trail->pool));

  /* Now write the record. */
  SVN_ERR (DB_WRAP (fs, "storing representation",
                    fs->representations->put
                    (fs->representations, trail->db_txn,
                     svn_fs__str_to_dbt (&query, (char *) key),
                     svn_fs__skel_to_dbt (&result, skel, trail->pool), 0)));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__write_new_rep (const char **key,
                       svn_fs_t *fs,
                       const svn_fs__representation_t *rep,
                       trail_t *trail)
{
  DBT query, result;
  int db_err;
  apr_size_t len;
  char next_key[200];    /* This will be a problem if the number of
                            representations in a filesystem ever
                            exceeds 1821797716821872825139468712408937
                            126733897152817476066745969754933395997209
                            053270030282678007662838673314795994559163
                            674524215744560596468010549540621501770423
                            499988699078859474399479617124840673097380
                            736524850563115569208508785942830080999927
                            310762507339484047393505519345657439796788
                            24151197232629947748581376.  Somebody warn
                            my grandchildren. */
  
  /* ### todo: see issue #409 for why bumping the key as part of this
     trail is problematic. */

  /* Get the current value associated with `next-key'.  */
  svn_fs__str_to_dbt (&query, (char *) svn_fs__next_key_key);
  SVN_ERR (DB_WRAP (fs, "allocating new representation (getting next-key)",
                    fs->representations->get (fs->representations,
                                              trail->db_txn,
                                              &query,
                                              svn_fs__result_dbt (&result),
                                              0)));

  svn_fs__track_dbt (&result, trail->pool);

  /* Store the new rep. */
  *key = apr_pstrmemdup (trail->pool, result.data, result.size);
  SVN_ERR (svn_fs__write_rep (fs, *key, rep, trail));

  /* Bump to future key. */
  len = result.size;
  svn_fs__next_key (result.data, &len, next_key);
  db_err = fs->representations->put
    (fs->representations, trail->db_txn,
     svn_fs__str_to_dbt (&query, (char *) svn_fs__next_key_key),
     svn_fs__str_to_dbt (&result, (char *) next_key),
     0);

  SVN_ERR (DB_WRAP (fs, "bumping next representation key", db_err));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__delete_rep (svn_fs_t *fs, const char *key, trail_t *trail)
{
  int db_err;
  DBT query;

  db_err = fs->representations->del
    (fs->representations, trail->db_txn,
     svn_fs__str_to_dbt (&query, (char *) key), 0);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_REPRESENTATION, 0, 0, fs->pool,
       "svn_fs__delete_rep: no such representation `%s'", key);

  /* Handle any other error conditions.  */
  SVN_ERR (DB_WRAP (fs, "deleting representation", db_err));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */

