/* reps-table.c : operations on the `representations' table
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include "db.h"
#include "svn_fs.h"
#include "fs.h"
#include "err.h"
#include "dbt.h"
#include "trail.h"
#include "strings-table.h"
#include "reps-table.h"



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

  *reps_p = reps;
  return 0;
}



/*** Storing and retrieving reps.  ***/

svn_error_t *
svn_fs__read_rep (skel_t **skel_p,
                  svn_fs_t *fs,
                  const char *key,
                  trail_t *trail)
{
  skel_t *skel;
  int db_err;
  DBT query, result;

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
  *skel_p = skel;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__write_rep (svn_fs_t *fs,
                   const char *key,
                   skel_t *skel,
                   trail_t *trail)
{
  DBT query, result;

  SVN_ERR (DB_WRAP (fs, "storing representation",
                    fs->representations->put
                    (fs->representations, trail->db_txn,
                     svn_fs__str_to_dbt (&query, (char *) key),
                     svn_fs__skel_to_dbt (&result, skel, trail->pool), 0)));

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
 * eval: (load-file "../svn-dev.el")
 * end:
 */
