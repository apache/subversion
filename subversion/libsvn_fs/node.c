/* node.c : implementation of node functions
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

#include <string.h>

#include "apr.h"
#include "apr_pools.h"
#include "apr_hash.h"

#include "svn_string.h"
#include "svn_error.h"
#include "svn_fs.h"

#include "fs.h"
#include "node.h"
#include "file.h"
#include "dir.h"
#include "skel.h"
#include "err.h"
#include "dbt.h"
#include "txn.h"


/* The node structure.  */

/* The different kinds of nodes.  */
typedef enum {
  kind_file,
  kind_directory
} kind_t;


/* This is the structure underlying the public svn_fs_node_t typedef.  */

struct svn_fs_node_t {

  /* The node's pool, a subpool of FS->pool.  */
  apr_pool_t *pool;

  /* The filesystem to which this node belongs.  */
  svn_fs_t *fs;

  /* This node's ID in FS.  */
  svn_fs_id_t *id;

  /* How many times this node has been opened.  If this count is zero,
     we can safely free it, without annoying code outside this file.  */
  int open_count;

  /* What kind of node this is.  We can cache this even if the node is
     mutable, since nodes don't change their types.  */
  kind_t kind;

  /* If this is a mutable node, the ID of the transaction to which it
     belongs.  Otherwise, this is zero.  */
  char *txn_id;

  /* If this is an immutable node, this is its NODE-REVISION skel.  The
     skel, and the data it points into, are allocated from POOL.

     On mutable nodes, this should be zero.  We don't cache the
     NODE-REVISION skel of mutable nodes, since it could change while
     we have the node open.  */
  skel_t *node_revision;

};



/* Building error objects.  */

static svn_error_t *
corrupt_id (const char *fmt, svn_fs_id_t *id, svn_fs_t *fs)
{
  svn_string_t *unparsed_id = svn_fs_unparse_id (id, fs->pool);

  return svn_error_createf (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
			    fmt, unparsed_id->data, fs->env_path);
}


static svn_error_t *
corrupt_representation (svn_fs_t *fs, svn_fs_id_t *id)
{
  return
    corrupt_id ("corrupt representation for node `%s' in filesystem `%s'",
		id, fs);
}


static svn_error_t *
corrupt_node_revision (svn_fs_t *fs, svn_fs_id_t *id)
{
  return
    corrupt_id ("corrupt node revision for node `%s' in filesystem `%s'",
		id, fs);
}


static svn_error_t *
corrupt_dangling_id (svn_fs_t *fs, svn_fs_id_t *id)
{
  return
    corrupt_id ("reference to non-existent node `%s' in filesystem `%s'",
		id, fs);
}


static svn_error_t *
not_a_node_revision_id (svn_fs_t *fs, svn_fs_id_t *id)
{
  return
    corrupt_id ("Bogus node revision id `%s' appears in filesystem `%s'",
		id, fs);
}


static svn_error_t *
corrupt_nodes_key (svn_fs_t *fs)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
     "malformed ID as key in `nodes' table of filesystem `%s'", fs->env_path);
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
			 svn_fs_id_t *id,
			 DB_TXN *db_txn,
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
    return corrupt_dangling_id (fs, id);
  SVN_ERR (DB_WRAP (fs, "reading node representation", db_err));
  svn_fs__track_dbt (&value, pool);

  skel = svn_fs__parse_skel (value.data, value.size, pool);
  if (! skel)
    return corrupt_representation (fs, id);

  *skel_p = skel;
  return 0;
}


/* Set the REPRESENTATION skel for node ID in filesystem FS to SKEL,
   as part of the Berkeley DB transaction TXN.  TXN may be zero, in
   which case the change is done outside of any transaction.  Do any
   necessary temporary allocation in POOL.  */
static svn_error_t *
put_representation_skel (svn_fs_t *fs,
			 const svn_fs_id_t *id, 
			 skel_t *skel,
			 DB_TXN *txn,
			 apr_pool_t *pool)
{
  DBT key, value;

  SVN_ERR (DB_WRAP (fs, "storing node representation",
		    fs->nodes->put (fs->nodes, txn,
				    svn_fs__id_to_dbt (&key, id, pool),
				    svn_fs__skel_to_dbt (&value, skel, pool),
				    0)));

  return 0;
}
			 


/* Storing and retrieving NODE-REVISION skels.  */


/* Set *SKEL_P to point to the NODE-REVISION skel for the node ID in FS,
   as part of the Berkeley DB transaction DB_TXN.  Allocate the skel
   and the data it points into in POOL.

   This takes care of applying any necessary deltas to reconstruct the
   node revision.  */

static svn_error_t *
get_node_revision_skel (skel_t **skel_p,
		       svn_fs_t *fs,
		       svn_fs_id_t *id,
		       DB_TXN *db_txn,
		       apr_pool_t *pool)
{
  skel_t *skel;

  /* This is where we would handle diffy representations, to construct
     a NODE-REVISION given its REPRESENTATION.  But I want to get the
     essentials working before I add that to the mix.  */
  SVN_ERR (get_representation_skel (&skel, fs, id, db_txn, pool));
  if (svn_fs__list_length (skel) != 2
      || ! svn_fs__is_atom (skel->children, "fulltext"))
    return corrupt_representation (fs, id);

  *skel_p = skel->children->next;
  return 0;
}


/* Store SKEL as the NODE-REVISION skel for the node ID in FS, as part
   of the Berkeley DB transaction DB_TXN.  Use POOL for any necessary
   temporary allocation.  */
static svn_error_t *
put_node_revision_skel (svn_fs_t *fs,
		       svn_fs_id_t *id,
		       skel_t *skel,
		       DB_TXN *db_txn,
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

  SVN_ERR (put_representation_skel (fs, id, &rep[0], db_txn, pool));

  return 0;
}



/* Examining and building HEADER skels.  */


/* A structure for the various items that might be in a HEADER skel.  */
typedef struct header_values_t {

  /* The header's KIND field.  */
  skel_t *kind;

  /* The header's property list.  */
  skel_t *proplist;

  /* The transaction ID in the header's "mutable" flag, or zero
     if the flag is absent.  */
  skel_t *mutable;

} header_values_t;


/* Parse the HEADER in the NODE-REVISION skel SKEL, and fill in VALUES
   with pointers to the appropriate sub-skels.  Return non-zero if
   HEADER is well-formed, zero otherwise.  */

static int
parse_header (skel_t *skel, header_values_t *values)
{
  skel_t *header;

  memset (values, 0, sizeof (*values));

  /* The NODE-REVISION skel must be a list of at least one element.  */
  if (skel->is_atom
      || ! skel->children)
    return 0;
  header = skel->children;

  /* Check the form of the HEADER skel, up to the flags.  */
  if (header->is_atom
      || ! header->children
      || ! header->children->is_atom
      || ! header->children->next
      || header->children->next->is_atom)
    return 0;

  /* Extract the fixed portion.  */
  values->kind = header->children;
  values->proplist = header->children->next;

  /* Parse the FLAG ... list.  */
  {
    skel_t *flag;

    for (flag = header->children->next->next; flag; flag = flag->next)
      /* Is it a "mutable" flag?  */
      if (svn_fs__list_length (flag) == 2
	  && svn_fs__is_atom (flag->children, "mutable")
	  && flag->children->next->is_atom)
	values->mutable = flag->children->next;
      else
	return 0;
  }

  return 1;
}



/* The node cache.  */

/* The interfaces to these functions will need to change if the
   filesystem becomes multi-threaded.  Suppose one thread checks the
   cache for a node, doesn't find it, and decides to go read it from
   the database and put it in the cache.  While it's off doing that,
   another thread comes in looking for the same node.  That thread
   should *not* also go off and try to read the node from the database
   --- perhaps it should wait for the first thread to finish doing so,
   or perhaps something else should happen.  But the race condition
   needs to be settled somehow.  */


/* Look for the node named by ID in FS's node cache.  If we find the
   node, increment its open count by one, and return it.  Otherwise,
   return zero.  */
static svn_fs_node_t *
get_cached_node (svn_fs_t *fs, svn_fs_id_t *id)
{
  int id_size = svn_fs_id_length (id) * sizeof (id[0]);
  svn_fs_node_t *node = apr_hash_get (fs->node_cache, id, id_size);

  /* It's important that we increment the open count now, instead of
     letting the caller elect to do it.  When our caching policy
     decides to free up some memory, we'll make a pass through this
     table and free up nodes whose open count is zero.  Incrementing
     the open count now ensures that, as long as this function's
     operation is atomic, we won't "clean up" the node before the
     caller is done with it.  Of course, the consequence is that the
     caller must make sure the open count gets decremented when it's
     done.  */
  if (node)
    node->open_count++;

  return node;
}


/* A pool cleanup function that removes NODE from its filesystem's
   cache.  */
static apr_status_t
pool_uncache_node (void *node_ptr)
{
  svn_fs_node_t *node = node_ptr;
  apr_hash_t *cache = node->fs->node_cache;
  svn_fs_id_t *id = node->id;
  int id_size = svn_fs_id_length (id) * sizeof (id[0]);

  /* Remove NODE's entry from the node cache.  */
  apr_hash_set (cache, id, id_size, 0);

  return 0;
}


/* Add NODE to its filesystem's node cache, under its ID.
   Set its open count to 1.  */
static void
cache_node (svn_fs_node_t *node)
{
  svn_fs_t *fs = node->fs;
  svn_fs_id_t *id = node->id;
  int id_size = svn_fs_id_length (id) * sizeof (id[0]);

  /* Sanity check: the new node object's open count must be zero.  */
  if (node->open_count != 0)
    abort ();

  /* Sanity check: make sure we're not writing over another node
     object that's already in the cache.  */
  {
    svn_fs_node_t *other = apr_hash_get (fs->node_cache, id, id_size);

    if (other)
      abort ();
  }

  node->open_count = 1;
  apr_hash_set (fs->node_cache, id, id_size, node);
  apr_register_cleanup (node->pool, node, pool_uncache_node, apr_null_cleanup);
}


/* Decrement NODE's open count.  If it's zero, we assume that there
   are no more references to the node outside the cache, and we may
   dispose of it at will.  */
static void
close_node (svn_fs_node_t *node)
{
  node->open_count--;

  /* At the moment, our cache policy is trivial: if the node's open
     count drops to zero, we free it.  In other words, we only cache
     nodes that are currently open.

     This kind of sucks, especially for directory traversal --- the
     nodes towards the top of the filesystem are going to get hit
     pretty frequently, so it would be worthwhile keeping them in
     the cache even when nobody has them open.  */
  if (node->open_count == 0)
    /* The node's pool's cleanup function takes care of removing the
       node from the node cache.  */
    apr_destroy_pool (node->pool);
}



/* Read a node object from the database, given its ID.  */

svn_error_t *
svn_fs__open_node_by_id (svn_fs_node_t **node_p,
			 svn_fs_t *fs,
			 svn_fs_id_t *id,
			 DB_TXN *db_txn)
{
  svn_fs_node_t *node = get_cached_node (fs, id);

  /* If the node wasn't in the cache, we'll have to read it in
     ourselves.  */
  if (! node)
    {
      apr_pool_t *skel_pool = svn_pool_create (fs->pool);
      skel_t *nv;
      header_values_t values;

      SVN_ERR (get_node_revision_skel (&nv, fs, id, db_txn, skel_pool));
      if (! parse_header (nv, &values))
	return corrupt_node_revision (fs, id);

      /* If the node is immutable, use SKEL_POOL as the node's pool.
	 We're going to keep the skel around, so this puts the node
	 and its skel in the same pool.

	 For mutable nodes, we allocate the node in a separate pool
	 and toss the node's skel, because it may change.  */
      {
	apr_pool_t *node_pool
	  = values.mutable ? svn_pool_create (fs->pool) : skel_pool;

	node = apr_pcalloc (node_pool, sizeof (*node));
	node->pool = node_pool;
      }

      node->fs = fs;
      node->id = svn_fs_copy_id (id, node->pool);

      /* What kind of node is this?  */
      if (svn_fs__is_atom (values.kind, "file"))
	node->kind = kind_file;
      else if (svn_fs__is_atom (values.kind, "dir"))
	node->kind = kind_directory;
      else
	return corrupt_node_revision (fs, id);

      if (values.mutable)
	/* For mutable nodes, we record the transaction ID.  */
	node->txn_id = apr_pstrndup (node->pool,
				     values.mutable->data,
				     values.mutable->len);
      else
	/* For immutable nodes, we save the node revision skel.  */
	node->node_revision = nv;

      /* Add NODE to the filesystem's cache.  */
      cache_node (node);

      /* Free the skel, if appropriate.  */
      if (skel_pool != node->pool)
	apr_destroy_pool (skel_pool);
    }

  *node_p = node;
  return 0;
}



/* Creating new nodes.  */


/* Check FS's `nodes' table to find an unused node number, and set *ID_P
   to the ID of the first revision of an entirely new node in FS, as
   part of DB_TXN.  Allocate the new ID in POOL.  */
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

  /* Try to parse the key as a node ID.  */
  id = svn_fs_parse_id (key.data, key.size, pool);
  if (! id)
    {
      cursor->c_close (cursor);
      return corrupt_nodes_key (fs);
    }

  /* We've got the value; close the cursor.  */
  SVN_ERR (DB_WRAP (fs, "choosing new node ID (closing cursor)",
		    cursor->c_close (cursor)));

  *id_p = id;
  return 0;
}


svn_error_t *
svn_fs__create_node (svn_fs_node_t **node_p,
		     svn_fs_t *fs,
		     skel_t *skel,
		     DB_TXN *db_txn,
		     apr_pool_t *pool)
{
  svn_fs_node_t *node;
  header_values_t values;

  /* Check that SKEL has a well-formed header, with its "mutable" flag set.  */
  if (! parse_header (skel, &values)
      || ! values.mutable)
    abort ();

  /* Allocate a new node structure.  */
  node = NEW (pool, svn_fs_node_t);
  memset (node, 0, sizeof (*node));
  node->pool = pool;
  node->fs = fs;
  node->txn_id = apr_pstrndup (node->pool,
			       values.mutable->data,
			       values.mutable->len);

  /* Find an ID for the node.  */
  SVN_ERR (new_node_id (&node->id, node->fs, db_txn, node->pool));

  /* Store its representation.  */
  SVN_ERR (put_representation_skel (node->fs, node->id, skel, db_txn,
				    node->pool));

  /* Add it to the cache.  */
  cache_node (node);

  *node_p = node;
  return 0;
}



/* Creating successor node revisions.  */


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
		  svn_fs_id_t *id,
		  DB_TXN *db_txn, 
		  apr_pool_t *pool)
{
  int id_len = svn_fs_id_length (id);
  svn_fs_id_t *new_id;
  DBT key, value;
  int db_err;

  /* Make sure ID is really a node revision ID.  */
  if (id_len & 1)
    return not_a_node_revision_id (fs, id);

  /* Set NEW_ID to the next node revision after ID.  Allocate some
     extra room, in case we need to construct a branch ID below.  */
  new_id = NEWARRAY (pool, svn_fs_id_t, (id_len + 3) * sizeof (*id));
  memcpy (new_id, id, (id_len + 1) * sizeof (*id)); /* copy the -1 */
  new_id[id_len - 1]++;		/* increment the revision number */

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

  {
    svn_fs_id_t *last_branch_id = svn_fs_parse_id (key.data, key.size, pool);
    int last_branch_len;
    if (! last_branch_id)
      return corrupt_nodes_key (fs);
    last_branch_len = svn_fs_id_length (last_branch_id);

    /* Only node revision ID's may appear as keys in the `nodes' table.  */
    if (last_branch_len & 1)
      return corrupt_nodes_key (fs);

    /* If the last key before NEW_ID is just another revision of node N,
       then there are no branches.  */
    if (last_branch_len == id_len)
      {
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
	memcpy (new_id, last_branch_id, (id_len + 1) * sizeof (*id));
	new_id[id_len + 0]++;
	new_id[id_len + 1] = 1;
	new_id[id_len + 2] = -1;

	*successor_p = new_id;
	return 0;
      }

    /* Otherwise, something strange is going on.  */
    else
      return corrupt_nodes_key (fs);
  }
}


svn_error_t *
svn_fs__create_successor (svn_fs_node_t **new_p,
			  svn_fs_node_t *old,
			  char *svn_txn_id,
			  DB_TXN *db_txn)
{
  svn_fs_node_t *new;
  skel_t *old_skel, *new_skel;

  /* We should never be creating successors of mutable nodes.  */
  if (svn_fs_node_is_mutable (old))
    abort ();

  /* Allocate the new node.  */
  {
    apr_pool_t *pool = svn_pool_create (old->fs->pool);
    new = NEW (pool, svn_fs_node_t);
    new->pool = pool;
  }
  new->fs = old->fs;
  new->kind = old->kind;
  new->txn_id = apr_pstrdup (new->pool, svn_txn_id);

  /* Choose an ID for the new node, and store it in the database.  */
  SVN_ERR (new_successor_id (&new->id, new->fs, old->id, db_txn, new->pool));

  /* Get a copy of the old node's contents.  */
  SVN_ERR (svn_fs__get_node_revision (&old_skel, old, db_txn, new->pool));
  new_skel = svn_fs__copy_skel (old_skel, new->pool);

  /* Add a "mutable" flag to the HEADER skel for SVN_TXN.  */
  {
    /* Build a skel of the form ("mutable" TXN-ID).  */
    skel_t *mutable_flag = svn_fs__make_empty_list (new->pool);

    svn_fs__prepend (svn_fs__make_atom (svn_txn_id, new->pool), mutable_flag);
    svn_fs__prepend (svn_fs__make_atom ("mutable", new->pool), mutable_flag);
    
    /* Insert this at the beginning of the new skel's flag list.  We
       know there is no "mutable" flag there already, since we've
       checked that OLD is immutable.  */
    {
      skel_t *proplist = new_skel->children->children->next;
      mutable_flag->next = proplist;
      proplist->next = mutable_flag;
    }
  }

  /* Write this out as the new node's contents.  */
  SVN_ERR (put_node_revision_skel (new->fs, new->id, new_skel, db_txn,
				  new->pool));

  /* Add it to the cache.  */
  cache_node (new);

  *new_p = new;
  return 0;
}



/* Intra-filesystem functions for reading and writing node contents.  */

svn_error_t *
svn_fs__get_node_revision (skel_t **skel_p,
                           svn_fs_node_t *node,
                           DB_TXN *db_txn,
                           apr_pool_t *pool)
{
  skel_t *skel;
  header_values_t values;

  /* If this is an immutable node, we can simply hand out a pointer
     to our cached contents.  */
  if (node->node_version)
    {
      *skel_p = node->node_version;
      return 0;
    }
  else
    {
      SVN_ERR (get_node_version_skel (&skel, node->fs, node->id, db_txn,
				      pool));

      if (! parse_header (skel, &values))
	return corrupt_node_version (node->fs, node->id);

      *skel_p = skel;
      return 0;
    }
}


svn_error_t *
svn_fs__put_node_revision (svn_fs_node_t *node,
                           skel_t *skel,
                           DB_TXN *db_txn)
{
  header_values_t values;

  /* Check that SKEL has a well-formed header, with its "mutable" flag set.  */
  if (! parse_header (skel, &values)
      || ! values.mutable)
    abort ();

  SVN_ERR (put_node_revision_skel (node->fs, node->id, skel, db_txn,
                                   node->pool));

  return 0;
}



/* Deltifying nodes.  */


svn_error_t *
svn_fs__stable_node (svn_fs_node_t *node)
{
  /* Not yet.  */
  return 0;
}



/* Retrieving node properties.  */

svn_error_t *
svn_fs_get_node_prop (svn_string_t **value_p,
		      svn_fs_node_t *node,
		      svn_string_t *propname,
		      apr_pool_t *pool)
{
  apr_pool_t *skel_pool;
  skel_t *node_revision;
  header_values_t values;

  /* If the node is mutable, we'll get our own copy of the entire
     node skeleton.  Don't keep that around.  */
  if (svn_fs_node_is_mutable (node))
    skel_pool = svn_pool_create (pool);
  else
    skel_pool = pool;

  SVN_ERR (svn_fs__get_node_revision (&node_revision, node, 0, skel_pool));

  if (! parse_header (node_revision, &values))
    return corrupt_node_revision (node->fs, node->id);

  /* Scan the property list for a property with the right name.  */
  {
    skel_t *prop;

    /* Walk the property list two elements at a time.  */
    for (prop = values.proplist->children; prop; prop = prop->next->next)
      {
	/* The proplist must be composed of pairs of atoms.  */ 
	if (! prop->is_atom
	    || ! prop->next
	    || ! prop->next->is_atom)
	  return corrupt_node_revision (node->fs, node->id);

	if (prop->len == propname->len
	    && ! memcmp (prop->data, propname->data, prop->len))
	  break;
      }

    /* Did we find something?  */
    if (prop)
      *value_p = svn_string_ncreate (prop->next->data,
				     prop->next->len,
				     pool);
    else
      *value_p = 0;

    /* If we read in a copy of the skel just for this operation, free it.  */
    if (skel_pool != pool)
      apr_destroy_pool (skel_pool);

    return 0;
  }
}


svn_error_t *
svn_fs_get_node_proplist (apr_hash_t **table_p,
			  svn_fs_node_t *node,
			  apr_pool_t *pool)
{
  apr_pool_t *skel_pool;
  skel_t *node_revision;
  header_values_t values;
  apr_hash_t *table;

  /* If the node is mutable, we'll get our own copy of the entire
     node skeleton.  Don't keep that around.  */
  if (svn_fs_node_is_mutable (node))
    skel_pool = svn_pool_create (pool);
  else
    skel_pool = pool;

  SVN_ERR (svn_fs__get_node_revision (&node_revision, node, 0, skel_pool));

  if (! parse_header (node_revision, &values))
    return corrupt_node_revision (node->fs, node->id);

  table = apr_make_hash (pool);

  /* Scan the property list and build a hash table.  */
  {
    skel_t *prop;

    /* Walk the property list two elements at a time.  */
    for (prop = values.proplist->children; prop; prop = prop->next->next)
      {
	/* The proplist must be composed of pairs of atoms.  */ 
	if (! prop->is_atom
	    || ! prop->next
	    || ! prop->next->is_atom)
	  return corrupt_node_revision (node->fs, node->id);

	/* Copy the name and value.  If NODE is mutable, the originals
           are allocated in skel_pool, which will go away soon.  If
           it's immutable, they're allocated in the node's pool, which
           will go away when the node is closed.  */
	{
	  char *name = apr_pstrndup (pool, prop->data, prop->len);
	  svn_string_t *value = svn_string_ncreate (prop->next->data,
						    prop->next->len,
						    pool);

	  apr_hash_set (table, name, prop->len, value);
	}
      }
  }

  if (skel_pool != pool)
    apr_destroy_pool (skel_pool);

  *table_p = table;
  return 0;
}



/* Casting, typing, and other trivial bookkeeping operations on nodes.  */

int
svn_fs_node_is_dir (svn_fs_node_t *node)
{
  return node->kind == kind_directory;
}


int
svn_fs_node_is_file (svn_fs_node_t *node)
{
  return node->kind == kind_file;
}


void
svn_fs_close_node (svn_fs_node_t *node)
{
  close_node (node);
}


svn_fs_node_t *
svn_fs__reopen_node (svn_fs_node_t *node)
{
  node->open_count++;
  return node;
}


int
svn_fs_node_is_mutable (svn_fs_node_t *node)
{
  return node->txn_id != 0;
}


svn_fs_t *
svn_fs__node_fs (svn_fs_node_t *node)
{
  return node->fs;
}


svn_fs_id_t *
svn_fs__node_id (svn_fs_node_t *node)
{
  return node->id;
}



/* Node cleanups.  */

static apr_status_t
apr_cleanup_node (void *node_ptr)
{
  svn_fs_node_t *node = node_ptr;

  svn_fs_close_node (node);

  return 0;
}


void
svn_fs_cleanup_node (apr_pool_t *pool, svn_fs_node_t *node)
{
  apr_register_cleanup (pool, node, apr_cleanup_node, apr_null_cleanup);
}


void
svn_fs_kill_cleanup_node (apr_pool_t *pool, svn_fs_node_t *node)
{
  apr_kill_cleanup (pool, node, apr_cleanup_node);
}


void
svn_fs_run_cleanup_node (apr_pool_t *pool, svn_fs_node_t *node)
{
  apr_run_cleanup (pool, node, apr_cleanup_node);
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
