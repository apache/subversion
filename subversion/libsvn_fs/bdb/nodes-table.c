/* nodes-table.c : working with the `nodes' table
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

#include <string.h>
#include <assert.h>
#include <db.h>

#include "svn_fs.h"

#include "../fs.h"
#include "../err.h"
#include "dbt.h"
#include "../util/skel.h"
#include "../util/fs_skels.h"
#include "../trail.h"
#include "../key-gen.h"
#include "../id.h"
#include "nodes-table.h"



/* Opening/creating the `nodes' table.  */


int
svn_fs__open_nodes_table (DB **nodes_p,
                          DB_ENV *env,
                          int create)
{
  DB *nodes;

  DB_ERR (db_create (&nodes, env, 0));
  DB_ERR (nodes->open (nodes, "nodes", 0, DB_BTREE,
                       create ? (DB_CREATE | DB_EXCL) : 0,
                       0666));

  /* Create the `next-id' table entry (use '1' because '0' is
     reserved for the root directory to use). */
  if (create)
  {
    DBT key, value;

    DB_ERR (nodes->put (nodes, 0,
                        svn_fs__str_to_dbt (&key, 
                                            (char *) svn_fs__next_key_key),
                        svn_fs__str_to_dbt (&value, (char *) "1"),
                        0));
  }

  *nodes_p = nodes;
  return 0;
}



/* Choosing node revision ID's.  */

svn_error_t *
svn_fs__new_node_id (svn_fs_id_t **id_p,
                     svn_fs_t *fs,
                     const char *txn_id,
                     trail_t *trail)
{
  DBT query, result;
  apr_size_t len;
  char next_key[200];
  int db_err;
  const char *next_node_id;

  /* TXN_ID is required! */
  assert (txn_id);

  /* Get the current value associated with the `next-key' key in the table.  */
  svn_fs__str_to_dbt (&query, (char *) svn_fs__next_key_key);
  SVN_ERR (DB_WRAP (fs, "allocating new node ID (getting `next-key')",
                    fs->nodes->get (fs->nodes, trail->db_txn,
                                    &query, 
                                    svn_fs__result_dbt (&result), 
                                    0)));
  svn_fs__track_dbt (&result, trail->pool);

  /* Squirrel away our next node id value. */
  next_node_id = apr_pstrmemdup (trail->pool, result.data, result.size);

  /* Bump to future key. */
  len = result.size;
  svn_fs__next_key (result.data, &len, next_key);
  db_err = fs->copies->put (fs->nodes, trail->db_txn,
                            svn_fs__str_to_dbt (&query, 
                                                (char *) svn_fs__next_key_key),
                            svn_fs__str_to_dbt (&result, (char *) next_key), 
                            0);
  SVN_ERR (DB_WRAP (fs, "bumping next node ID key", db_err));

  /* Create and return the new node id. */
  *id_p = svn_fs__create_id (next_node_id, "0", txn_id, trail->pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__new_successor_id (svn_fs_id_t **successor_p,
                          svn_fs_t *fs,
                          const svn_fs_id_t *id,
                          const char *copy_id,
                          const char *txn_id,
                          trail_t *trail)
{
  svn_fs_id_t *new_id;
  svn_error_t *err;

  /* TXN_ID is required! */
  assert (txn_id);

  /* Create and return the new successor ID.  */
  new_id = svn_fs__create_id (svn_fs__id_node_id (id),
                              copy_id ? copy_id : svn_fs__id_copy_id (id),
                              txn_id, 
                              trail->pool);

  /* Now, make sure this NEW_ID doesn't already exist in FS. */
  err = svn_fs__get_node_revision (NULL, fs, new_id, trail);
  if ((! err) || (err->apr_err != SVN_ERR_FS_ID_NOT_FOUND))
    {
      svn_string_t *id_str = svn_fs_unparse_id (id, trail->pool);
      svn_string_t *new_id_str = svn_fs_unparse_id (new_id, trail->pool);
      return svn_error_createf 
        (SVN_ERR_FS_ALREADY_EXISTS, 0, err, trail->pool,
         "successor id `%s' (for `%s') already exists in filesystem %s",  
         new_id_str->data, id_str->data, fs->path);
    }

  /* Return the new node revision ID. */
  *successor_p = new_id;
  return SVN_NO_ERROR;
}



/* Removing node revisions.  */
svn_error_t *
svn_fs__delete_nodes_entry (svn_fs_t *fs,
                            const svn_fs_id_t *id,
                            trail_t *trail)
{
  DBT key;
  
  SVN_ERR (DB_WRAP (fs, "deleting entry from `nodes' table",
                    fs->nodes->del (fs->nodes,
                                    trail->db_txn,
                                    svn_fs__id_to_dbt (&key, id, trail->pool),
                                    0)));
  
  return SVN_NO_ERROR;
}




/* Storing and retrieving NODE-REVISIONs.  */


svn_error_t *
svn_fs__get_node_revision (svn_fs__node_revision_t **noderev_p,
                           svn_fs_t *fs,
                           const svn_fs_id_t *id,
                           trail_t *trail)
{
  svn_fs__node_revision_t *noderev;
  skel_t *skel;
  int db_err;
  DBT key, value;

  db_err = fs->nodes->get (fs->nodes, trail->db_txn,
                           svn_fs__id_to_dbt (&key, id, trail->pool),
                           svn_fs__result_dbt (&value),
                           0);
  svn_fs__track_dbt (&value, trail->pool);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_fs__err_dangling_id (fs, id);

  /* Handle any other error conditions.  */
  SVN_ERR (DB_WRAP (fs, "reading node revision", db_err));

  /* If our caller doesn't really care about the return value here,
     just return successfully. */
  if (! noderev_p)
    return SVN_NO_ERROR;

  /* Parse and the NODE-REVISION skel.  */
  skel = svn_fs__parse_skel (value.data, value.size, trail->pool);

  /* Convert to a native FS type. */
  SVN_ERR (svn_fs__parse_node_revision_skel (&noderev, skel, trail->pool));
  *noderev_p = noderev;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__put_node_revision (svn_fs_t *fs,
                           const svn_fs_id_t *id,
                           svn_fs__node_revision_t *noderev,
                           trail_t *trail)
{
  DB_TXN *db_txn = trail->db_txn;
  apr_pool_t *pool = trail->pool;
  DBT key, value;
  skel_t *skel;

  /* Convert from native type into skel */
  SVN_ERR (svn_fs__unparse_node_revision_skel (&skel, noderev, pool));
  return DB_WRAP (fs, "storing node revision",
                  fs->nodes->put (fs->nodes, db_txn,
                                  svn_fs__id_to_dbt (&key, id, pool),
                                  svn_fs__skel_to_dbt (&value, skel, pool),
                                  0));
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
