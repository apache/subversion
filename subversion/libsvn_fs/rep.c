/* rep.c --- storing and retrieving NODE-VERSION skels
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <db.h>

#include "svn_fs.h"
#include "rep.h"
#include "dbt.h"
#include "err.h"


/* Creating and opening the Berkeley DB `nodes' table.  */


/* Compare two node ID's, according to the rules in `structure'.  */
static int
compare_ids (svn_fs_id_t *a, svn_fs_id_t *b)
{
  int i = 0;

  while (a[i] == b[i])
    {
      if (a[i] == -1)
        return 0;
      i++;
    }

  /* Different nodes, or different branches, are ordered by their
     node / branch numbers.  */
  if ((i & 1) == 0)
    return a[i] - b[i];

  /* This function is only prepared to handle node revision ID's.  */
  if (a[i] == -1 || b[i] == -1)
    abort ();

  /* Different revisions of the same node are ordered by revision number.  */
  if (a[i + 1] == -1 && b[i + 1] == -1)
    return a[i] - b[i];

  /* A branch off of any revision of a node comes after all revisions
     of that node.  */
  if (a[i + 1] == -1)
    return -1;
  if (b[i + 1] == -1)
    return 1;

  /* Branches are ordered by increasing revision number.  */
  return a[i] - b[i];
}


/* Parse a node revision ID from D.
   Return zero if D does not contain a well-formed node revision ID.  */
static svn_fs_id_t *
parse_node_revision_dbt (const DBT *d)
{
  svn_fs_id_t *id = svn_fs_parse_id (d->data, d->size, 0);

  if (! id)
    return 0;

  /* It must be a node revision ID, not a node ID.  */
  if (svn_fs_id_length (id) & 1)
    {
      free (id);
      return 0;
    }

  return id;
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
   keys come in byte-by-byte order.  */
static int
compare_nodes_keys (const DBT *ak, const DBT *bk)
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

  if (a) free (a);
  if (b) free (b);

  return result;
}


/* Open / create FS's `nodes' table.  FS->env must already be open;
   this function initializes FS->nodes.  If CREATE is non-zero, assume
   we are creating the filesystem afresh; otherwise, assume we are
   simply opening an existing database.  */
static svn_error_t *
make_nodes (svn_fs_t *fs, int create)
{
  SVN_ERR (DB_WRAP (fs, "allocating `nodes' table object",
                    db_create (&fs->nodes, fs->env, 0)));
  SVN_ERR (DB_WRAP (fs, "setting `nodes' comparison function",
                    fs->nodes->set_bt_compare (fs->nodes, 
                                               compare_nodes_keys)));
  SVN_ERR (DB_WRAP (fs,
                    (create
                     ? "creating `nodes' table"
                     : "opening `nodes' table"),
                    fs->nodes->open (fs->nodes, "nodes", 0, DB_BTREE,
                                     create ? (DB_CREATE | DB_EXCL) : 0,
                                     0666)));

  return 0;
}


svn_error_t *
svn_fs__create_nodes (svn_fs_t *fs)
{
  return make_nodes (fs, 1);
}


svn_error_t *
svn_fs__open_nodes (svn_fs_t *fs)
{
  return make_nodes (fs, 0);
}



/* Storing and retrieving node revision REPRESENTATION skels.  */


/* Set *SKEL_P to point to the REPRESENTATION skel for the node ID in
   FS, as part of the Berkeley DB transaction DB_TXN.  Allocate the
   skel and the data it points into in POOL.

   Beyond verifying that it's a syntactically valid skel, this doesn't
   validate the data returned at all.  */
static svn_error_t *
get_representation_skel (skel_t **skel_p,
                         svn_fs_t *fs,
                         DB_TXN *db_txn,
                         const svn_fs_id_t *id,
                         apr_pool_t *pool)
{
  int db_err;
  DBT key, value;
  skel_t *skel;

  /* Generate the ASCII form of the node revision ID.  */
  db_err = fs->nodes->get (fs->nodes, db_txn,
                           svn_fs__id_to_dbt (&key, id, pool),
                           svn_fs__result_dbt (&value),
                           0);
  if (db_err == DB_NOTFOUND)
    return svn_fs__err_dangling_id (fs, id);
  SVN_ERR (DB_WRAP (fs, "reading node representation", db_err));
  svn_fs__track_dbt (&value, pool);

  skel = svn_fs__parse_skel (value.data, value.size, pool);
  if (! skel)
    return svn_fs__err_corrupt_representation (fs, id);

  *skel_p = skel;
  return 0;
}


/* Set the REPRESENTATION skel for node ID in filesystem FS to SKEL,
   as part of the Berkeley DB transaction TXN.  Do any necessary
   temporary allocation in POOL.  */
static svn_error_t *
put_representation_skel (svn_fs_t *fs,
                         DB_TXN *db_txn,
                         const svn_fs_id_t *id, 
                         skel_t *skel,
                         apr_pool_t *pool)
{
  DBT key, value;

  SVN_ERR (DB_WRAP (fs, "storing node representation",
                    fs->nodes->put (fs->nodes, db_txn,
                                    svn_fs__id_to_dbt (&key, id, pool),
                                    svn_fs__skel_to_dbt (&value, skel, pool),
                                    0)));

  return 0;
}
                         


/* Storing and retrieving NODE-REVISION skels.  */


svn_error_t *
svn_fs__get_node_revision (skel_t **skel_p,
                           svn_fs_t *fs,
                           DB_TXN *db_txn,
                           const svn_fs_id_t *id,
                           apr_pool_t *pool)
{
  skel_t *skel;

  SVN_ERR (get_representation_skel (&skel, fs, db_txn, id, pool));

  /* This is where we would handle diffy representations, to construct
     a NODE-REVISION given its REPRESENTATION.  But I want to get the
     essentials working before I add that to the mix.

     When we add diffy representations, we'll need to start caching
     the expanded contents of immutable nodes.  */
  if (svn_fs__list_length (skel) != 2
      || ! svn_fs__is_atom (skel->children, "fulltext"))
    return svn_fs__err_corrupt_representation (fs, id);

  *skel_p = skel->children->next;
  return 0;
}


/* Store SKEL as the NODE-REVISION skel for the node ID in FS, as part
   of the Berkeley DB transaction DB_TXN.  Use POOL for any necessary
   temporary allocation.  */
svn_error_t *
svn_fs__put_node_revision (svn_fs_t *fs,
                           DB_TXN *db_txn,
                           const svn_fs_id_t *id,
                           skel_t *skel,
                           apr_pool_t *pool)
{
  /* We always write out new nodes as fulltext.  Converting older
     nodes to deltas against this one happens later, when we call
     svn_fs__stable_node.  */

  /* Start with a dummy "fulltext" skel, and just drop in the
     NODE-REVISION skel for this node.  */
  skel_t rep[] = {
    { 0, 0, 0, &rep[1], 0 },
    { 1, "fulltext", 8, 0, 0 }
  };

  rep[1].next = skel;
  skel->next = 0;

  SVN_ERR (put_representation_skel (fs, db_txn, id, &rep[0], pool));

  return 0;
}



/* Creating completely new nodes.  */


/* Check FS's `nodes' table to find an unused node number, and set
   *ID_P to the ID of the first revision of an entirely new node in
   FS, as part of DB_TXN.  Allocate the new ID, and do all temporary
   allocation, in POOL.  */
static svn_error_t *
new_node_id (svn_fs_id_t **id_p,
             svn_fs_t *fs,
             DB_TXN *db_txn,
             apr_pool_t *pool)
{
  int db_err;
  DBC *cursor = 0;
  DBT key, value;
  svn_fs_id_t *id;

  /* Create a database cursor.  */
  SVN_ERR (DB_WRAP (fs, "choosing new node ID (creating cursor)",
                    fs->nodes->cursor (fs->nodes, db_txn, &cursor, 0)));

  /* Find the last entry in the `nodes' table, and increment its node
     number.  */
  db_err = cursor->c_get (cursor,
                          svn_fs__result_dbt (&key),
                          svn_fs__nodata_dbt (&value),
                          DB_LAST);
  if (db_err)
    {
      /* Free the cursor.  Ignore any error value --- the error above
         is more interesting.  */
      cursor->c_close (cursor);

      if (db_err == DB_NOTFOUND)
        /* The root directory should always be present, at least.  */
        return
          svn_error_createf
          (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
           "root directory missing from `nodes' table, in filesystem `%s'",
           fs->env_path);
      
      SVN_ERR (DB_WRAP (fs, "choosing new node ID (finding last entry)",
                        db_err));
    }
  svn_fs__track_dbt (&key, pool);

  /* Try to parse the key as a node revision ID.  */
  id = svn_fs_parse_id (key.data, key.size, pool);
  if (! id
      || svn_fs_id_length (id) < 2)
    {
      cursor->c_close (cursor);
      return svn_fs__err_corrupt_nodes_key (fs);
    }

  /* We've got the value; close the cursor.  */
  SVN_ERR (DB_WRAP (fs, "choosing new node ID (closing cursor)",
                    cursor->c_close (cursor)));

  /* Given the ID of the last node revision, what's the ID of the
     first revision of an entirely new node?  */
  id[0]++;
  id[1] = 1;
  id[2] = -1;

  *id_p = id;
  return 0;
}


svn_error_t *
svn_fs__create_node (svn_fs_id_t **id_p,
                     svn_fs_t *fs,
                     DB_TXN *db_txn,
                     skel_t *skel,
                     apr_pool_t *pool)
{
  svn_fs_id_t *id;

  /* Find an unused ID for the node.  */
  SVN_ERR (new_node_id (&id, fs, db_txn, pool));

  /* Store its representation.  */
  SVN_ERR (put_representation_skel (fs, db_txn, id, skel, pool));

  *id_p = id;
  return 0;
}



/* Creating new revisions of existing nodes.  */


/* Find the last entry before KEY in the btree table DB.

   KEY must be initialized as for any normal Berkeley DB operation.
   The settings of KEY->flags and KEY's other members control how the
   value is returned.

   If DB_TXN is non-zero, perform the operation as part of that
   Berkeley DB transaction.  */
static int
last_key_before (DB *db,
                 DB_TXN *db_txn,
                 DBT *key)
{
  int db_err;
  DBC *cursor;
  DBT temp_key, value;

  /* Create a cursor into the table.  */
  DB_ERR (db->cursor (db, db_txn, &cursor, 0));

  /* Position CURSOR to the first table entry at or after KEY.
     Don't bother retrieving the key or value we find there.  */
  svn_fs__nodata_dbt (&temp_key);
  temp_key.data = key->data;
  temp_key.size = key->size;
  svn_fs__nodata_dbt (&value);
  db_err = cursor->c_get (cursor, &temp_key, &value, DB_SET_RANGE);
  if (db_err && db_err != DB_NOTFOUND)
    {
      cursor->c_close (cursor);
      return db_err;
    }

  /* If db_err == 0, we found the first table entry at or after KEY;
     the record we want comes immediately before that.

     If db_err == DB_NOTFOUND, then we couldn't find any entry at or
     after KEY, so the record we want must be the last record in the
     table.  */
  db_err = cursor->c_get (cursor, key, svn_fs__nodata_dbt (&value),
                          db_err == DB_NOTFOUND ? DB_LAST : DB_PREV);
  if (db_err)
    {
      cursor->c_close (cursor);
      return db_err;
    }

  /* We're finished with the cursor now.  */
  DB_ERR (cursor->c_close (cursor));

  return 0;
}


/* Set *SUCCESSOR_P to the ID of an immediate successor to node
   revision ID in FS that does not exist yet, as part of the Berkeley
   DB transaction DB_TXN.  Do any needed temporary allocation in POOL.

   If ID is the youngest revision of its node, then the successor is
   simply ID with its rightmost revision number increased; otherwise,
   the successor is a new branch from ID.  */
static svn_error_t *
new_successor_id (svn_fs_id_t **successor_p,
                  svn_fs_t *fs,
                  DB_TXN *db_txn, 
                  svn_fs_id_t *id,
                  apr_pool_t *pool)
{
  int id_len = svn_fs_id_length (id);
  svn_fs_id_t *new_id;
  DBT key, value;
  int db_err;

  /* Make sure ID is really a node revision ID.  */
  if (id_len & 1)
    return svn_fs__err_corrupt_id (fs, id);

  /* Set NEW_ID to the next node revision after ID.  Allocate some
     extra room, in case we need to construct a branch ID below.  */
  new_id = NEWARRAY (pool, svn_fs_id_t, (id_len + 3) * sizeof (*id));
  memcpy (new_id, id, (id_len + 1) * sizeof (*id)); /* copy the -1 */
  new_id[id_len - 1]++;         /* increment the revision number */

  /* Check to see if there already exists a node whose ID is NEW_ID.  */
  db_err = fs->nodes->get (fs->nodes, db_txn,
                           svn_fs__id_to_dbt (&key, new_id, pool),
                           svn_fs__nodata_dbt (&value),
                           0);
  if (db_err == DB_NOTFOUND)
    {
      /* NEW_ID isn't currently in use, so return that.  */
      *successor_p = new_id;
      return 0;
    }
  else
    SVN_ERR (DB_WRAP (fs, "checking for next node revision", db_err));

  /* Okay, the next revision of ID already exists, so we'll need to make
     a new branch.  What's the next available branch number?

     The sort order for the nodes table says that all revisions of a
     node come together, followed by all branches from any revision of
     that node; the branches are sorted by the revision they branch
     from, and then by branch number.

     So, if our node revision ID is N.V, then all its branches will
     come immediately before the first branch from N.(V+1).  So we
     find the last node in the table before node ID N.(V+1).1.1; that
     node is (perhaps a branch from) the last branch from N.V.

     NEW_ID is currently N.(V+1); stick on the ".1.1".  */
  new_id[id_len + 0] = 1;
  new_id[id_len + 1] = 1;
  new_id[id_len + 2] = -1;
  SVN_ERR (DB_WRAP (fs, "checking for next node branch",
                    last_key_before (fs->nodes, db_txn,
                                     svn_fs__result_dbt (&key))));
  svn_fs__track_dbt (&key, pool);

  {
    svn_fs_id_t *last_branch_id = svn_fs_parse_id (key.data, key.size, pool);
    int last_branch_len;

    if (! last_branch_id)
      return svn_fs__err_corrupt_nodes_key (fs);

    last_branch_len = svn_fs_id_length (last_branch_id);

    /* Only node revision ID's may appear as keys in the `nodes' table.  */
    if (last_branch_len & 1)
      return svn_fs__err_corrupt_nodes_key (fs);

    /* If the last key before NEW_ID is just another revision of node
       N (specifically, the last revision), then there are no branches
       yet.  */
    if (last_branch_len == id_len)
      {
        /* The first branch from N.V is N.V.1.1.  */
        memcpy (new_id, id, id_len * sizeof (*id));
        new_id[id_len + 0] = 1;
        new_id[id_len + 1] = 1;
        new_id[id_len + 2] = -1;

        *successor_p = new_id;
        return 0;
      }

    /* If the last key before NEW_ID is a branch off of ID, then
       choose the next branch number.  */
    else if (last_branch_len > id_len)
      {
        /* The last key has the form N.V.B... so the first revision
           on our new branch is N.V.(B+1).1.  */
        memcpy (new_id, last_branch_id, (id_len + 1) * sizeof (*id));
        new_id[id_len + 0]++;
        new_id[id_len + 1] = 1;
        new_id[id_len + 2] = -1;

        *successor_p = new_id;
        return 0;
      }

    /* Otherwise, something strange is going on.  */
    else
      return svn_fs__err_corrupt_nodes_key (fs);
  }
}


svn_error_t *
svn_fs__create_successor (svn_fs_id_t **new_id_p,
                          svn_fs_t *fs,
                          DB_TXN *db_txn,
                          svn_fs_id_t *old_id,
                          skel_t *new_skel,
                          apr_pool_t *pool)
{
  svn_fs_id_t *new_id;

  /* Choose an ID for the new node, and store it in the database.  */
  SVN_ERR (new_successor_id (&new_id, fs, db_txn, old_id, pool));

  /* Store the new skel under that ID.  */
  SVN_ERR (put_representation_skel (fs, db_txn, new_id, new_skel, pool));

  *new_id_p = new_id;
  return 0;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
