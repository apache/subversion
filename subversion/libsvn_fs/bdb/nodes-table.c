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


/* Compare two node ID's, according to the rules in `structure'.  
   We have heirarchical sorting here:

      1.  by node id
      2.  by copy id
      3.  by txn id  

   Sorting happens in ascending order. */
static int
compare_ids (svn_fs_id_t *a, svn_fs_id_t *b)
{
  int cmp;

  /* Compare node ids. */
  if ((cmp = svn_fs__key_compare (svn_fs__id_node_id (a), 
                                  svn_fs__id_node_id (b))))
    return cmp;

  /* Compare copy ids. */
  if ((cmp = svn_fs__key_compare (svn_fs__id_copy_id (a), 
                                  svn_fs__id_copy_id (b))))
    return cmp;

  /* Compare txn ids. */
  if ((cmp = svn_fs__key_compare (svn_fs__id_txn_id (a), 
                                  svn_fs__id_txn_id (b))))
    return cmp;

  /* These appear to be equivalent. */
  return 0;
}


/* Parse a node revision ID from D.  The ID returned is allocated
   using `malloc', not in an APR pool.  Return zero if D does not
   contain a well-formed node revision ID.  */
static svn_fs_id_t *
parse_node_revision_dbt (const DBT *d)
{
  return svn_fs_parse_id (d->data, d->size, 0);
}


/* The key comparison function for the `nodes' table.

   Strictly speaking, this function only needs to handle strings that
   we actually use as keys in the table.  However, if we happen to
   insert garbage keys, and this comparison function doesn't do
   something consistent with them (i.e., something transitive and
   reflexive), we can actually corrupt the btree structure.  Which
   seems unfriendly.

   So this function tries to act as a proper comparison for any two
   arbitrary byte strings.  Two well-formed node revisions ID's compare
   according to the rules described in the `structure' file; any
   malformed key comes before any well-formed key; and two malformed
   keys come in byte-by-byte order.

   NOTE WELL: this function and its helpers use `malloc' to get space
   for the parsed node revision ID's.  In general, we try to use pools
   for everything in Subversion, but in this case it's not practical.
   Berkeley DB doesn't provide any way to pass a baton through to the
   btree comparison function.  Even if it did, since Berkeley DB needs
   to invoke the comparison function at pretty arbitrary times, you'd
   have to pass the baton to almost every Berkeley DB operation.  You
   could stuff a pool pointer in a global variable, but then you'd
   have to make sure the pool was up to date before every Berkeley DB
   operation; you'd surely forget, leading to crashes...  Using malloc
   is more maintainable.  Since the comparison function isn't allowed
   to signal an error anyway, the need for pools isn't quite as urgent
   as in other code, but we still need to take care.  */
static int
compare_nodes_keys (DB *dummy, const DBT *ak, const DBT *bk)
{
  svn_fs_id_t *a = parse_node_revision_dbt (ak);
  svn_fs_id_t *b = parse_node_revision_dbt (bk);
  int result;

  /* Two well-formed keys are compared by the rules in `structure'.  */
  if (a && b)
    result = compare_ids (a, b);

  /* Malformed keys come before well-formed keys.  */
  else if (a)
    result = 1;
  else if (b)
    result = -1;

  /* Two malformed keys are compared byte-by-byte.  */
  else
    result = svn_fs__compare_dbt (ak, bk);

  if (a)
    {
      free ((void*)a->node_id);  /* cast to remove const */
      free (a);
    }
  if (b)
    {
      free ((void*)b->node_id);  /* cast to remove const */
      free (b);
    }

  return result;
}


int
svn_fs__open_nodes_table (DB **nodes_p,
                          DB_ENV *env,
                          int create)
{
  DB *nodes;

  DB_ERR (db_create (&nodes, env, 0));
  DB_ERR (nodes->set_bt_compare (nodes, compare_nodes_keys));
  DB_ERR (nodes->open (nodes, "nodes", 0, DB_BTREE,
                       create ? (DB_CREATE | DB_EXCL) : 0,
                       0666));

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
  int db_err;
  DBC *cursor = 0;
  DBT key, value;
  svn_fs_id_t *id;
  const char *node_id;
  char next_key[200];
  apr_size_t len;

  /* TXN_ID is required! */
  assert (txn_id);

  /* Create a database cursor.  */
  SVN_ERR (DB_WRAP (fs, "choosing new node ID (creating cursor)",
                    fs->nodes->cursor (fs->nodes, trail->db_txn, &cursor, 0)));

  /* Find the last entry in the `nodes' table, and increment its node
     number.  */
  db_err = cursor->c_get (cursor,
                          svn_fs__result_dbt (&key),
                          svn_fs__nodata_dbt (&value),
                          DB_LAST);
  svn_fs__track_dbt (&key, trail->pool);
  if (db_err)
    {
      /* Free the cursor.  Ignore any error value --- the error above
         is more interesting.  */
      cursor->c_close (cursor);

      /* The root directory should always be present, at least, so a
         NOTFOUND error is badness..  */
      if (db_err == DB_NOTFOUND)
        return svn_error_createf
          (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
           "root directory missing from `nodes' table, in filesystem `%s'",
           fs->path);
      
      return DB_WRAP (fs, "choosing new node ID (finding last entry)", db_err);
    }

  /* Try to parse the key as a node revision ID.  */
  id = svn_fs_parse_id (key.data, key.size, trail->pool);
  if (! id)
    {
      cursor->c_close (cursor);
      return svn_fs__err_corrupt_nodes_key (fs);
    }

  /* We've got the value; close the cursor.  */
  SVN_ERR (DB_WRAP (fs, "choosing new node ID (closing cursor)",
                    cursor->c_close (cursor)));

  /* Given the ID of the last node revision, what's the ID of the
     first revision of an entirely new node?  */
  node_id = svn_fs__id_node_id (id);
  len = strlen (node_id);
  svn_fs__next_key (node_id, &len, next_key);

  /* Create and return the new node id. */
  *id_p = svn_fs__create_id (next_key, "0", txn_id, trail->pool);
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
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
