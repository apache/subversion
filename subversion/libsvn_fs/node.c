/* node.c : implementation of node functions
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of Collab.Net.
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
#include "id.h"
#include "skel.h"
#include "err.h"
#include "dbt.h"


/* Building some often-used error objects.  */

static svn_error_t *
corrupt_id (const char *fmt, svn_fs_id_t *id, svn_fs_t *fs)
{
  svn_string_t *unparsed_id = svn_fs__unparse_id (id, fs->pool);

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
corrupt_node_version (svn_fs_t *fs, svn_fs_id_t *id)
{
  return
    corrupt_id ("corrupt node version for node `%s' in filesystem `%s'",
		id, fs);
}


static svn_error_t *
corrupt_dangling_id (svn_fs_t *fs, svn_fs_id_t *id)
{
  return
    corrupt_id ("reference to non-existent node `%s' in filesystem `%s'",
		id, fs);
}


/* Reading node REPRESENTATION skels from the database.  */


/* Set *SKEL to point to the REPRESENTATION skel for the node ID in FS.
   Allocate the skel and the data it points into in POOL.

   Beyond verifying that it's a syntactically valid skel, this doesn't
   validate the data returned at all.  */
static svn_error_t *
get_representation_skel (skel_t **skel, svn_fs_t *fs, svn_fs_id_t *id,
			 apr_pool_t *pool)
{
  DBT key, value;
  int db_err;
  svn_string_t *unparsed_id;
  skel_t *rep_skel;

  SVN_ERR (svn_fs__check_fs (fs));

  /* Generate the ASCII form of the node version ID.  */
  unparsed_id = svn_fs__unparse_id (id, pool);
  svn_fs__set_dbt (&key, unparsed_id->data, unparsed_id->len);
  svn_fs__result_dbt (&value);
  db_err = fs->nodes->get (fs->nodes, 0, /* no transaction */ &key, &value, 0);
  if (db_err == DB_NOTFOUND)
    return corrupt_dangling_id (fs, id);
  SVN_ERR (DB_WRAP (fs, "reading node representation", db_err));
  svn_fs__track_dbt (&value, pool);

  rep_skel = svn_fs__parse_skel (value.data, value.size, pool);
  if (! rep_skel)
    return corrupt_representation (fs, id);

  *skel = rep_skel;
  return 0;
}



/* Writing node REPRESENTATION skels to the database.  */ 


/* Set the representation skel for node ID in filesystem FS to
   REPRESENTATION_SKEL, as part of the Berkeley DB transaction TXN.
   TXN may be zero, in which case the change is done outside of any
   transaction.  Do any necessary temporary allocation in POOL.  */
static svn_error_t *
put_representation_skel (svn_fs_t *fs, DB_TXN *txn,
			 svn_fs_id_t *id, skel_t *representation_skel,
			 apr_pool_t *pool)
{
  svn_string_t *unparsed_id;
  svn_string_t *unparsed_rep;
  DBT key, value;

  SVN_ERR (svn_fs__check_fs (fs));

  unparsed_id = svn_fs__unparse_id (id, pool);
  svn_fs__set_dbt (&key, unparsed_id->data, unparsed_id->len);

  unparsed_rep = svn_fs__unparse_skel (representation_skel, pool);
  svn_fs__set_dbt (&value, unparsed_rep->data, unparsed_rep->len);

  SVN_ERR (DB_WRAP (fs, "storing node representation",
		   fs->nodes->put (fs->nodes, txn, &key, &value, 0)));

  return 0;
}
			 


/* Recovering the full text of NODE-VERSION skels from the database.  */


/* Set SKEL to point to the NODE-VERSION skel for the node ID in FS.
   Allocate the skel and the data it points into in POOL.

   This takes care of applying any necessary deltas to reconstruct the
   node version.  */

static svn_error_t *
get_node_version_skel (skel_t **skel, svn_fs_t *fs, svn_fs_id_t *id,
		       apr_pool_t *pool)
{
  skel_t *rep;

  /* Well, this would take care of applying any necessary deltas, but
     we don't have anything that generates vcdiff-format output yet,
     so I can't store deltas in the database.

     So for now, every node is stored using the "fulltext"
     representation.  I'm off the hook!!

     In the future, it would be nice to have a cache of fulltexts, to
     help us compute nearby versions quickly.  */
  SVN_ERR (get_representation_skel (&rep, fs, id, pool));
  if (svn_fs__list_length (rep) != 2
      || ! svn_fs__is_atom (rep->children, "fulltext"))
    return corrupt_representation (fs, id);

  *skel = rep->children->next;
  return 0;
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
			 svn_fs_id_t *id)
{
  svn_fs_node_t *node = get_cached_node (fs, id);

  /* If the node wasn't in the cache, we'll have to read it in
     ourselves.  */
  if (! node)
    {
      apr_pool_t *skel_pool = svn_pool_create (fs->pool);
      skel_t *nv, *kind;

      SVN_ERR (get_node_version_skel (&nv, fs, id, skel_pool));
      if (svn_fs__list_length (nv) < 2
	  || ! nv->children->is_atom)
	return corrupt_node_version (fs, id);

      kind = nv->children;
      if (svn_fs__is_atom (kind, "file"))
	SVN_ERR (svn_fs__file_from_skel (&node, fs, id, nv, skel_pool));
      else if (svn_fs__is_atom (kind, "dir"))
	SVN_ERR (svn_fs__dir_from_skel (&node, fs, id, nv, skel_pool));
      else
	return corrupt_node_version (fs, id);

      cache_node (node);

      apr_destroy_pool (skel_pool);
    }

  *node_p = node;
  return 0;
}



/* Common initialization for all new node objects.  */

svn_fs_node_t *
svn_fs__init_node (apr_size_t size,
		   svn_fs_t *fs,
		   svn_fs_id_t *id,
		   kind_t kind)
{
  /* Create the node's subpool.  */
  apr_pool_t *pool = svn_pool_create (fs->pool);
  svn_fs_node_t *node = apr_pcalloc (pool, size);

  node->fs = fs;
  node->pool = pool;
  node->id = svn_fs_copy_id (id, node->pool);
  node->kind = kind;

  return node;
}



/* Creating and opening a filesystem's `nodes' table.  */


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

  /* An id that ends after a node/branch number isn't well-formed.  */
  if (a[i] == -1)
    return -1;
  if (b[i] == -1)
    return 1;

  /* Different versions of the same node are ordered by version
     number, with "head" coming after all versions.  */
  if (a[i + 1] == -1 && b[i + 1] == -1)
    {
      if (a[i] == -2)
	return 1;
      if (b[i] == -2)
	return -1;
      return a[i] - b[i];
    }

  /* A branch off of any version of a node comes after all versions
     of that node.  */
  if (a[i + 1] == -1)
    return -1;
  if (b[i + 1] == -1)
    return 1;

  /* Branches are ordered by increasing version number.  */
  return a[i] - b[i];
}


/* The key comparison function for the `nodes' table.

   Strictly speaking, this function only needs to handle strings that
   we actually use as keys in the table.  However, if we happen to
   insert garbage keys, and this comparison function doesn't do
   something consistent with them (i.e., something transitive and
   reflexive), we can actually corrupt the btree structure.  Which
   seems unfriendly.

   So this function tries to act as a proper comparison for any two
   arbitrary byte strings.  Two well-formed node versions ID's compare
   according to the rules described in the `structure' file; any
   malformed key comes before any well-formed key; and two malformed
   keys come in byte-by-byte order.  */
static int
compare_nodes_keys (const DBT *ak, const DBT *bk)
{
  svn_fs_id_t *a = svn_fs__parse_id (ak->data, ak->size, svn_fs__key_id, 0);
  svn_fs_id_t *b = svn_fs__parse_id (bk->data, bk->size, svn_fs__key_id, 0);

  /* Is either a or b malformed?  */
  if (! a && ! b)
    return svn_fs__compare_dbt (ak, bk);
  else if (! a)
    {
      free (b);
      return -1;
    }
  else if (! b)
    {
      free (a);
      return 1;
    }

  /* Okay, we've got two well-formed keys.  Compare them according to
     the ordering described in `structure'.  */
  {
    int cmp = compare_ids (a, b);
    free (a);
    free (b);
    return cmp;
  }
}


/* Open / create FS's `nodes' table.  FS->env must already be open;
   this function initializes FS->nodes.  If CREATE is non-zero, assume
   we are creating the filesystem afresh; otherwise, assume we are
   simply opening an existing database.  */
static svn_error_t *
make_nodes (svn_fs_t *fs, int create)
{
  DB *nodes;

  SVN_ERR (DB_WRAP (fs, "allocating `nodes' table object",
		    db_create (&nodes, fs->env, 0)));
  SVN_ERR (DB_WRAP (fs, "setting `nodes' comparison function",
		    nodes->set_bt_compare (nodes, compare_nodes_keys)));
  SVN_ERR (DB_WRAP (fs,
		    (create
		     ? "creating `nodes' table"
		     : "opening `nodes' table"),
		    nodes->open (nodes, "nodes", 0, DB_BTREE,
				 create ? (DB_CREATE | DB_EXCL) : 0,
				 0666)));

  if (create)
    {
      /* Create node 0.0, the initial root directory.  */
      static char node_0_0[] = "(fulltext (directory () ()))";
      skel_t *rep_skel = svn_fs__parse_skel (node_0_0,
					     sizeof (node_0_0) - 1,
					     fs->pool);
      static svn_fs_id_t id_0_0[] = { 0, 0, -1 };
      
      SVN_ERR (put_representation_skel (fs, 0, id_0_0, rep_skel, fs->pool));
    }

  fs->nodes = nodes;
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



/* Casting, typing, and other trivial bookkeeping operations on nodes.  */

int
svn_fs_node_is_dir (svn_fs_node_t *node)
{
  return node->kind == kind_dir;
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


svn_fs_proplist_t *
svn_fs_node_proplist (svn_fs_node_t *node)
{
  return node->proplist;
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
