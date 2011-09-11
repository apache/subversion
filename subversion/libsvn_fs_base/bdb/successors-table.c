/* successors-table.c : operations on the `successors' table
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

#include "bdb_compat.h"

#include <apr_hash.h>
#include <apr_tables.h>

#include "svn_fs.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "../fs.h"
#include "../err.h"
#include "../trail.h"
#include "../id.h"
#include "../util/fs_skels.h"
#include "../../libsvn_fs/fs-loader.h"
#include "bdb-err.h"
#include "dbt.h"
#include "successors-table.h"


#include "svn_private_config.h"


/*** Creating and opening the successors table. ***/

int
svn_fs_bdb__open_successors_table(DB **successors_p,
                                  DB_ENV *env,
                                  svn_boolean_t create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *successors;
  int error;

  BDB_ERR(svn_fs_bdb__check_version());
  BDB_ERR(db_create(&successors, env, 0));

  /* Enable duplicate keys. This allows us to store the successors
     one-per-row.  Note: this must occur before ->open().  */
  BDB_ERR(successors->set_flags(successors, DB_DUP));

  error = successors->open(SVN_BDB_OPEN_PARAMS(successors, NULL),
                           "successors", 0, DB_BTREE,
                           open_flags, 0666);

  /* Create the table if it doesn't yet exist.  This is a form of
     automagical repository upgrading. */
  if (error == ENOENT && (! create))
    {
      BDB_ERR(successors->close(successors, 0));
      return svn_fs_bdb__open_successors_table(successors_p, env, TRUE);
    }
  BDB_ERR(error);

  *successors_p = successors;
  return 0;
}



/*** Storing and retrieving successors.  ***/

svn_error_t *
svn_fs_bdb__successors_add(svn_fs_t *fs,
                           const char *node_id,
                           const char *succ_id,
                           trail_t *trail,
                           apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT query, value;

  SVN_DBG(("BDB successors:\n"));
  SVN_DBG(("pred: %s\n", node_id));
  SVN_DBG(("      %s\n", succ_id));

  /* Store a new record into the database. */
  svn_fs_base__str_to_dbt(&query, node_id);
  svn_fs_base__str_to_dbt(&value, succ_id);
  svn_fs_base__trail_debug(trail, "successors", "put");
  SVN_ERR(BDB_WRAP(fs, _("creating successor"),
                   bfd->successors->put(bfd->successors, trail->db_txn,
                                        &query, &value, 0)));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_bdb__successors_delete(svn_fs_t *fs,
                              const char *node_id,
                              const char *succ_id,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBC *cursor;
  DBT key, value;
  int db_err = 0, db_c_err = 0;
  svn_error_t *err = SVN_NO_ERROR;

  /* Get a cursor on the first record matching NODE_ID, and then loop
     over the records, adding them to the return array. */
  svn_fs_base__trail_debug(trail, "successors", "cursor");
  SVN_ERR(BDB_WRAP(fs, _("creating cursor for reading successors"),
                   bfd->successors->cursor(bfd->successors, trail->db_txn,
                                           &cursor, 0)));

  /* Advance the cursor to the key that we're looking for. */
  svn_fs_base__str_to_dbt(&key, node_id);
  svn_fs_base__str_to_dbt(&value, succ_id);
  db_err = svn_bdb_dbc_get(cursor, &key, &value, DB_GET_BOTH);
  if (! db_err)
    {
      /* Delete the item at the cursor. */
      db_err = svn_bdb_dbc_del(cursor, 0);
    }

  /* Record any errors we caught.  We'll return them post-cleanup. */
  if (db_err)
    err = BDB_WRAP(fs, _("deleting successor"), db_err);

  /* Close the cursor. */
  db_c_err = svn_bdb_dbc_close(cursor);

  /* If we had an error prior to closing the cursor, return the error. */
  if (err)
    return err;

  /* If our only error thus far was when we closed the cursor, return
     that error. */
  if (db_c_err)
    SVN_ERR(BDB_WRAP(fs, _("closing successors cursor"), db_c_err));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_bdb__successors_delete_all(svn_fs_t *fs,
                                  const char *node_id,
                                  trail_t *trail,
                                  apr_pool_t *pool)
{
  int db_err;
  DBT query;
  base_fs_data_t *bfd = fs->fsap_data;
  
  svn_fs_base__trail_debug(trail, "successors", "del");
  db_err = bfd->successors->del(bfd->successors, trail->db_txn,
                                svn_fs_base__str_to_dbt(&query, node_id), 0);

  /* If there're no successors for NODE_ID, that is acceptable.  Any
     other error should be propogated to the caller, though.  */
  if ((db_err) && (db_err != DB_NOTFOUND))
    {
      SVN_ERR(BDB_WRAP(fs, _("deleting successors"), db_err));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_bdb__successors_fetch(apr_array_header_t **successors_p,
                             svn_fs_t *fs,
                             const char *node_id,
                             trail_t *trail,
                             apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBC *cursor;
  DBT key, value;
  int db_err = 0, db_c_err = 0;
  svn_error_t *err = SVN_NO_ERROR;
  apr_array_header_t *successors = apr_array_make(pool, 8, sizeof(const char *));

  /* Get a cursor on the first record matching NODE_ID, and then loop
     over the records, adding them to the return array. */
  svn_fs_base__trail_debug(trail, "successors", "cursor");
  SVN_ERR(BDB_WRAP(fs, _("creating cursor for reading successors"),
                   bfd->successors->cursor(bfd->successors, trail->db_txn,
                                           &cursor, 0)));

  /* Advance the cursor to the key that we're looking for. */
  svn_fs_base__str_to_dbt(&key, node_id);
  svn_fs_base__result_dbt(&value);
  db_err = svn_bdb_dbc_get(cursor, &key, &value, DB_SET);
  if (! db_err)
    svn_fs_base__track_dbt(&value, pool);

  while (! db_err)
    {
      APR_ARRAY_PUSH(successors, const char *) = 
        apr_pstrmemdup(pool, value.data, value.size);

      /* Advance the cursor to the next record with this same NODE_ID,
         and fetch that record. */
      svn_fs_base__result_dbt(&value);
      db_err = svn_bdb_dbc_get(cursor, &key, &value, DB_NEXT_DUP);
      if (! db_err)
        svn_fs_base__track_dbt(&value, pool);
    }

  /* If there are no (more) successor records for this KEY, we're
     finished.  Just return the (possibly empty) array.  Any other
     error, however, needs to get handled appropriately.  */
  if (db_err && (db_err != DB_NOTFOUND))
    err = BDB_WRAP(fs, _("fetching successors"), db_err);

  /* Close the cursor. */
  db_c_err = svn_bdb_dbc_close(cursor);

  /* If we had an error prior to closing the cursor, return the error. */
  if (err)
    return err;

  /* If our only error thus far was when we closed the cursor, return
     that error. */
  if (db_c_err)
    SVN_ERR(BDB_WRAP(fs, _("closing successors cursor"), db_c_err));

  /* Finally, set our return variable and get outta here. */
  *successors_p = successors;
  return SVN_NO_ERROR;
}
