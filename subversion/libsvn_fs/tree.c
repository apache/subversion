/* tree.c : tree-like filesystem, built on DAG filesystem
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


#include <stdlib.h>
#include <string.h>
#include "svn_fs.h"
#include "skel.h"
#include "id.h"
#include "fs.h"
#include "err.h"
#include "clones-table.h"
#include "dag.h"



/* The node structure.  */

typedef enum {
  kind_unset,
  kind_file,
  kind_directory
} kind_t;


/* The path from some node to the root of a revision.  This is the
   path of directories we need to clone to change this node.  */
struct clone_path_t {

  /* The path from the parent of this node to the root, or zero if
     this is the root.  */
  struct clone_path_t *parent;

  /* The name of this node in that parent directory.  */
  const char *name;

};


typedef struct clone_path_t clone_path_t;

struct svn_fs_node_t {

  /* What filesystem does this node belong to?  */
  svn_fs_t *fs;

  /* All data belonging to this node is allocated in this pool.  Destroying
     this pool will correctly free all resources the node holds.  */
  apr_pool_t *pool;

  /* What kind of node is this?  */
  kind_t kind;

  /* If this node was reached from the root of a transaction, the name
     of that transaction, allocated in POOL.  Otherwise, this is zero.  */
  const char *txn;

  /* If this node was reached from the root of a revision, this is
     that revision.  Otherwise, it's -1.  */
  svn_revnum_t rev;

  /* If this node was reached from the root of a transaction, and it
     has *not* been cloned (so far as we know), this is the path from
     the node to the root of the transaction's base revision.
     Otherwise, this is zero.  */
  clone_path_t *clone_path;

  /* This node, as managed by the DAG filesystem.

     If we reached this node via the root of a transaction (that is,
     txn != 0), and we don't think it's been cloned yet (that is,
     clone_path != 0), then we must be cautious in using this field,
     since another process could clone the node at any time.

     You can call either `make_clone' or `check_for_clone' as part of
     a Berkeley DB transaction to get an appropriate node to use.
     They'll cache their results in the node, so they'll be quick after
     the first call.  */
  dag_node_t *dag_node;


  /* The first time we call `make_clone' or `check_for_clone' on this
     this node within some Berkeley DB transaction, we cache the
     result here.  As the first action within a new Berkeley DB
     transaction, it's very important to call `txn_prep_node', which
     will clear this cache.  It's nice, but not necessary, to call
     `txn_finalize_node' after a transaction succeeds, which installs
     this value in a more permanent place.  */
  dag_node_t *clone_cache;
};



/* Internal node operations.  */


/* Return a new, partially initialized node object in FS, allocated
   from POOL.  */
static svn_fs_node_t *
new_node_object (svn_fs_t *fs,
		 apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_fs_node_t *node = apr_pcalloc (subpool, sizeof (*node));

  node->fs = fs;
  node->pool = pool;
  node->kind = kind_unset;
  node->rev = -1;

  return node;
}


/* Create a clone_path_t node in POOL, using PARENT and NAME.  */
static clone_path_t *
new_child (clone_path_t *parent,
	   const char *name,
	   apr_pool_t *pool)
{
  clone_path_t *path = apr_pcalloc (pool, sizeof (*path));

  path->parent = parent;
  path->name = apr_pstrdup (pool, name);

  return path;
}



/* Clone tracking.  */


/* Prepare NODE for participation in a Berkeley DB transaction.  You
   must call this on every pre-existing node you'll use in a
   transaction, to make sure it doesn't carry clones from aborted
   transactions.  */
static void
txn_prep_node (svn_fs_node_t *node)
{
  node->clone_cache = 0;
}


/* Finalize NODE after a successful transaction.  You can call this
   after a Berkeley DB transaction using NODE succeeds, to permanently
   record any clones found of NODE.  */
static void
txn_finalize_node (svn_fs_node_t *node)
{
  if (node->clone_cache
      && node->clone_cache != node->dag_node)
    {
      node->clone_path = 0;
      node->dag_node = node->clone_cache;
    }

  node->clone_cache = 0;
}


/* If NODE has been cloned in DB_TXN, set *DAG_P to the clone,
   allocated in POOL.  Otherwise, set *DAG_P to the original node,
   shared with NODE.

   When it does find a clone, it would be nice if this function could
   simply stash the new DAG node in NODE->dag_node, and zero
   NODE->clone_path --- effectively caching the results of the clone
   check in the node itself.  However, if DB_TXN aborts, we need to
   unwind that side effect.  At the moment, it looks easier to simply
   put off any side effects to the node structure until the
   transaction succeeds.  */
static svn_error_t *
check_for_clone (dag_node_t **node_p,
		 svn_fs_node_t *node,
		 DB_TXN *db_txn,
		 apr_pool_t *pool)
{
  abort ();
}


/* Return a string representing the path from the root directory
   represented by PATH, allocated in POOL.  */
static char *
path_to_string (clone_path_t *path,
		apr_pool_t *pool)
{
  abort ();
}


/* Clone PATH, and set *CLONE_P to a dag_node_t for the clone we make
   (or find) of PATH->dag_node.  FS, SVN_TXN, and DB_TXN are the
   filesystem, Subversion transaction, and Berkeley DB transaction the
   clone takes place in.  Allocate *CLONE_P, and do any other
   necessary temporary allocation in POOL.  */
static svn_error_t *
clone_path (dag_node_t **clone_p,
	    svn_fs_t *fs,
	    const char *svn_txn,
	    clone_path_t *path,
	    DB_TXN *db_txn,
	    apr_pool_t *pool)
{
  /* Are we trying to clone a subdirectory, or the root directory?  */
  if (path)
    {
      /* We're trying to clone a subdirectory, not the root.  Has this
         node already been cloned?  */
      char *base_path = path_to_string (path, pool);
      skel_t *clone_info;
      skel_t *clone_id_skel, *parent_clone_id_skel, *entry_name_skel;

      /* Check the `clones' table for an entry for this path.  */
      SVN_ERR (svn_fs__check_clone (&clone_info, fs, svn_txn, base_path,
				    db_txn, pool));

      if (! clone_info)
	{
	  /* According to the `clones' table, this node hasn't been
	     cloned yet.  Recursively clone the parent, then clone
	     the node itself.  */
	  dag_node_t *parent_clone, *child_clone;
	    
	  SVN_ERR (clone_path (&parent_clone, fs, svn_txn, path->parent,
			       db_txn, pool));
	  SVN_ERR (svn_fs__dag_clone (&child_clone, parent_clone, path->name,
				      db_txn, pool));
	  svn_fs__dag_close (parent_clone);

	  *clone_p = child_clone;
	  return 0;
	}
      else if (svn_fs__is_cloned (&clone_id_skel, clone_info))
	{
	  svn_fs_id_t *clone_id = svn_fs_parse_id (clone_id_skel->data,
						   clone_id_skel->len,
						   pool);
	  dag_node_t *clone;

	  SVN_ERR (svn_fs__dag_txn_node (&clone, fs, svn_txn, clone_id,
					 db_txn, pool));

	  *clone_p = clone;
	  return 0;
	}
      else if (svn_fs__is_renamed (&parent_clone_id_skel, &entry_name_skel,
				   clone_info))
	{
	  svn_fs_id_t *parent_clone_id
	    = svn_fs_parse_id (parent_clone_id_skel->data,
			       parent_clone_id_skel->len,
			       pool);
	  dag_node_t *parent_clone, *child_clone;

	  SVN_ERR (svn_fs__dag_txn_node (&parent_clone, fs, svn_txn,
					 parent_clone_id,
					 db_txn, pool));
	  SVN_ERR (svn_fs__dag_clone (&child_clone, parent_clone, path->name,
				      db_txn, pool));
	  svn_fs__dag_close (parent_clone);
	    
	  *clone_p = child_clone;
	  return 0;
	}
      else
	abort ();
    }
  else
    {
      /* We're cloning the root directory.  */
      abort ();
    }
}
	    

/* Clone NODE in DB_TXN, if it hasn't been cloned already.  In either
   case, set *NODE_P to the clone, allocated either in POOL, or in
   NODE->pool.  */
static svn_error_t *
make_clone (dag_node_t **node_p,
	    svn_fs_node_t *node,
	    DB_TXN *db_txn,
	    apr_pool_t *pool)
{
  abort ();
}



/* Generic node operations.  */


void
svn_fs_close_node (svn_fs_node_t *node)
{
  apr_destroy_pool (node->pool);
}


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


svn_fs_id_t *
svn_fs_get_node_id (svn_fs_node_t *node,
		    apr_pool_t *pool)
{
  /* We could call `check_for_clone' here, but there's not much point,
     as the node could get cloned by another process as soon as we
     return.  If you think the correctness of your code depends on
     having a more up-to-date ID, you may be right --- but that's not
     all you'll need.  */

  return svn_fs_copy_id (svn_fs__dag_get_id (node->dag_node), pool);
}


const char *
svn_fs_get_node_txn (svn_fs_node_t *node,
		     apr_pool_t *pool)
{
  if (node->txn)
    return apr_pstrdup (pool, node->txn);
  else
    return 0;
}


svn_revnum_t
svn_fs_get_node_rev (svn_fs_node_t *node)
{
  return node->rev;
}


struct get_node_prop_args
{
  svn_string_t **value_p;
  svn_fs_node_t *node;
  svn_string_t *propname;
  apr_pool_t *pool;
};


static svn_error_t *
txn_body_get_node_prop (void *baton,
			DB_TXN *db_txn)
{
  struct get_node_prop_args *args = baton;
  dag_node_t *dag_node;
  skel_t *proplist, *prop;

  SVN_ERR (check_for_clone (&dag_node, args->node, db_txn, args->pool));
  SVN_ERR (svn_fs__dag_get_proplist (&proplist, dag_node, db_txn, args->pool));
  
  /* Search the proplist for a property with the right name.  */
  for (prop = proplist->children; prop; prop = prop->next->next)
    {
      skel_t *name = prop;
      skel_t *value = prop->next;

      if (name->len == args->propname->len
	  && ! memcmp (name->data, args->propname->data, name->len))
	{
	  *(args->value_p) = svn_string_ncreate (value->data, value->len,
						 args->pool);
	  return 0;
	}
    }

  *(args->value_p) = 0;
  return 0;
}


svn_error_t *
svn_fs_get_node_prop (svn_string_t **value_p,
		      svn_fs_node_t *node,
		      svn_string_t *propname,
		      apr_pool_t *pool)
{
  struct get_node_prop_args args;
  svn_string_t *value;

  args.value_p  = &value;
  args.node     = node;
  args.propname = propname;
  args.pool     = pool;

  SVN_ERR (svn_fs__retry_txn (node->fs, txn_body_get_node_prop, &args));

  *value_p = value;
  return 0;
}


svn_error_t *
svn_fs_get_node_proplist (apr_hash_t **table_p,
			  svn_fs_node_t *node,
			  apr_pool_t *pool)
{
  abort ();
}


struct change_node_prop_args {
  svn_fs_node_t *node;
  svn_string_t *name;
  svn_string_t *value;
  apr_pool_t *pool;
};


static svn_error_t *
txn_body_change_node_prop (void *baton,
			   DB_TXN *db_txn)
{
  struct change_node_prop_args *args = baton;
  dag_node_t *dag_node;
  skel_t *proplist, *prop;

  SVN_ERR (make_clone (&dag_node, args->node, db_txn, args->pool));
  SVN_ERR (svn_fs__dag_get_proplist (&proplist, dag_node, db_txn, args->pool));
  
  /* Delete the skel, either replacing or adding the given property.  */
  for (prop = proplist->children; prop; prop = prop->next->next)
    {
      skel_t *name = prop;
      skel_t *value = prop->next;

      /* We've found an existing entry for this property. 
	 Replace the value.  */
      if (name->len == args->name->len
	  && ! memcmp (name->data, args->name->data, name->len))
	{
	  value->data = args->value->data;
	  value->len = args->value->len;

	  break;
	}
    }

  /* This property doesn't appear in the property list; add it to the
     beginning.  */
  if (! prop)
    {
      svn_fs__prepend (svn_fs__mem_atom (args->value->data,
					 args->value->len,
					 args->pool),
		       proplist);
      svn_fs__prepend (svn_fs__mem_atom (args->name->data,
					 args->name->len,
					 args->pool),
		       proplist);
    }

  SVN_ERR (svn_fs__dag_set_proplist (dag_node, proplist, db_txn, args->pool));

  return 0;
}


svn_error_t *
svn_fs_change_node_prop (svn_fs_node_t *node,
			 svn_string_t *name,
			 svn_string_t *value,
			 apr_pool_t *pool)
{
  struct change_node_prop_args args;

  args.node  = node;
  args.name  = name;
  args.value = value;
  args.pool  = pool;

  SVN_ERR (svn_fs__retry_txn (node->fs, txn_body_change_node_prop, &args));

  return 0;
}


svn_error_t *
svn_fs_merge (const char **conflict_p,
	      svn_fs_node_t *source,
	      svn_fs_node_t *target,
	      svn_fs_node_t *ancestor,
	      apr_pool_t *pool)
{
  abort ();
}



/* Directories.  */


/* Starting from PARENT, traverse PATH, and set *NODE_P to a newly
   opened svn_fs_node_t for whatever we find there.  Do all this as
   part of the Berkeley DB transaction DB_TXN.  Allocate the new node,
   and do any necessary temporary allocation, in POOL.  */
static svn_error_t *
traverse_path (svn_fs_node_t **node_p,
	       svn_fs_node_t *parent,
	       dag_node_t *parent_dag,
	       const char *path,
	       DB_TXN *db_txn,
	       apr_pool_t *pool)
{
  abort ();
}


svn_error_t *
svn_fs_open_node (svn_fs_node_t **child_p,
		  svn_fs_node_t *parent,
		  const char *path,
		  apr_pool_t *pool)
{
  abort ();
}


svn_error_t *
svn_fs_dir_entries (apr_hash_t **table_p,
		    svn_fs_node_t *dir,
		    apr_pool_t *pool)
{
  abort ();
}


svn_error_t *
svn_fs_make_dir (svn_fs_node_t *parent,
		 char *path,
		 apr_pool_t *pool)
{
  abort ();
}
			      

svn_error_t *
svn_fs_delete (svn_fs_node_t *parent,
	       const char *path,
	       apr_pool_t *pool)
{
  abort ();
}


svn_error_t *
svn_fs_rename (svn_fs_node_t *old_parent,
	       const char *old_path,
	       svn_fs_node_t *new_parent,
	       const char *new_path,
	       apr_pool_t *pool)
{
  abort ();
}


svn_error_t *
svn_fs_copy (svn_fs_node_t *parent,
	     const char *path,
	     svn_fs_node_t *child,
	     apr_pool_t *pool)
{
  abort ();
}


