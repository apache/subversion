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


/* The job of this layer is to take a filesystem with lots of node
   sharing going on --- the real DAG filesystem as it appears in the
   database --- and make it look and act like an ordinary tree
   filesystem, with no sharing.

   We do just-in-time cloning: you can walk from some unfinished
   transaction's root down into directories and files shared with
   committed revisions; as soon as you try to change something, the
   appropriate nodes get cloned (and parent directory entries updated)
   invisibly, behind your back.  Any other references you have to
   nodes that have been cloned by other changes, even made by other
   processes, are automatically updated to point to the right clones.  */


#include <stdlib.h>
#include <string.h>
#include "svn_fs.h"
#include "skel.h"
#include "id.h"
#include "fs.h"
#include "err.h"
#include "trail.h"
#include "clones-table.h"
#include "txn-table.h"
#include "dag.h"



/* The node structure.  */

typedef enum {
  kind_unset,
  kind_file,
  kind_directory
} kind_t;


/* The path of directory entry names from some node to the root of a
   revision.  The root directory's path is the null pointer.  This is
   the path of parent directory entries we need to update to clone a
   given DAG node.  */
struct clone_path_t {

  /* The path of the parent directory.  Zero if this is a child of the
     root directory.  */
  struct clone_path_t *parent;

  /* The name of this node in that parent directory.  */
  const char *name;

};


typedef struct clone_path_t clone_path_t;


/* There are essentially two kinds of nodes:

   - Nodes which we reached via the root of some committed revision
     ("revision nodes").  These are easy, since they never change.  We
     just hold a reference to the dag node, and that's that.

   - Nodes which we reached via the root of some unfinished Subversion
     transaction ("transaction nodes").  These are more complicated to
     handle, since nodes in a Subversion transaction can be cloned at
     any time, perhaps even by other processes.  We divide transaction
     nodes into three subclasses:

     - Nodes which haven't yet been cloned --- to the best of our
       knowledge.  Cloning could happen at any time.  ("uncloned
       transaction nodes".)

     - Nodes which we know have been cloned.  ("cloned transaction
       nodes".)

   Note that, since the Berkeley DB transaction in which a node is
   cloned may later be aborted, a node may change status from
   "cloned transaction node" back to an "uncloned transaction node".
   It's really not safe to use a given svn_fs_node_t within the
   context of two Berkeley DB transactions simultanously.  */

struct svn_fs_node_t
{

  /* What filesystem does this node belong to?  */
  svn_fs_t *fs;

  /* All data belonging to this node is allocated in this pool.  Destroying
     this pool will correctly free all resources the node holds.  */
  apr_pool_t *pool;

  /* What kind of node is this?  */
  kind_t kind;

  /* For transaction nodes, the name of that transaction, allocated in
     POOL.  For revision nodes, this is zero.  */
  const char *txn;

  /* For revision nodes, the number of that revision.  For transaction
     nodes, it's -1.  */
  svn_revnum_t rev;

  /* For transaction nodes, non-zero if the node has been cloned.  */
  int is_cloned;

  /* For uncloned transaction nodes, this is the path from the node to
     the root of the transaction's base revision --- the nodes we need
     to clone to change this one.

     If we turn a node from uncloned to cloned as part of some
     Berkeley DB transaction, this continues to be valid at least
     until that transaction has been successfully committed.  */
  clone_path_t *clone_path;

  /* The DAG node itself.  Well, our best guess, anyway:
     - For revision nodes and cloned transaction nodes, this is
       reliably the correct DAG node reference.
     - For uncloned transaction nodes, this field is untrustworthy,
       since the node could be cloned at any moment, perhaps even via
       operations on other nodes within the same Berkeley DB
       transaction.  Instead of using this field, you should use the
       `txn' and `clone_path' members to look up this node in the
       `clones' table, and go by what you find there.
     The `check_for_clone' and `make_clone' functions handle all this
     logic correctly, so you may want to simply call those functions
     instead of using this field directly.  */
  dag_node_t *dag_node;

  /* For a transaction node that got cloned as part of an as-of-yet
     uncommitted trail, this is a reference to the original node.
     Outside of that situation, this is zero.  */
  dag_node_t *original_dag_node;

};



/* Internal node operations.  */


#if 0
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


/* Create a clone path referring to the directory entry named NAME in
   the directory given by PARENT.  Allocate the new node in POOL.  */
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
#endif



/* Clone tracking.  */


/* Return a string representing the path from the root directory
   represented by PATH, allocated in POOL.  */
static char *
clone_path_to_string (clone_path_t *path,
		      apr_pool_t *pool)
{
  clone_path_t *p;
  int path_len;
  char *string, *end;

  /* How long is the string form of the path going to be?  */
  path_len = 0;
  for (p = path; p; p = p->parent)
    path_len += strlen (p->name) + 1;

  string = apr_palloc (pool, path_len + 1);
  end = string + path_len;
  for (p = path; p->name; p = p->parent)
    {
      int name_len = strlen (p->name);
      end -= name_len;
      memcpy (end, p->name, name_len);
      *--end = '/';
    }

  if (string != end)
    abort ();

  return string;
}


/* Undo function for set_clone.  */
static void
undo_set_clone (void *baton)
{
  svn_fs_node_t *node = baton;

  node->dag_node = node->original_dag_node;
  node->original_dag_node = 0;
  node->is_cloned = 0;
}


/* Set NODE's DAG_NODE member to CLONE, set its `is_cloned' flag, and
   prepare to undo all this if TRAIL is aborted.  */
static void
set_clone (svn_fs_node_t *node,
	   dag_node_t *clone,
	   trail_t *trail)
{
  /* If we're setting dag_node, the original_dag_node should always be
     zero.  */
  if (node->original_dag_node)
    abort ();

  /* Stash away the original node, and drop in the new node.  */
  node->original_dag_node = node->dag_node;
  node->dag_node = clone;

  /* "Stand back, Gertrude!  You've been cloned!"  */
  node->is_cloned = 1;

  /* If TRAIL is aborted, undo this change.  */
  svn_fs__record_undo (trail, undo_set_clone, node);
}


/* If NODE has been cloned in TRAIL, update NODE's DAG node reference
   to refer to the clone.  */
static svn_error_t *
check_for_clone (svn_fs_node_t *node,
		 trail_t *trail)
{
  /* If we do find a clone of NODE, we cache that fact, so that
     subsequent calls can return without doing any I/O.

     However, if we find that NODE has *not* yet been cloned, we can't
     cache that fact --- make_clone could be applied to some child of
     NODE, or someone might clone some other node object referring to
     the same node.  This means that, for uncloned nodes, every call
     to check_for_clone goes to disk for its answer.  It would be nice
     to remove that necessity somehow.  */

  if (! node->txn)
    {
      /* We reached this node from the root of a revision, so there
	 will never be a clone of this node.  */
      return 0;
    }

  if (node->is_cloned)
    {
      /* We've already cloned this node, and updated its `dag_node'
         member.  */
      return 0;
    }

  /* Are we looking for the clone of the transaction's root directory?  */
  if (! node->clone_path)
    {
      /* Find the transaction's current root directory.  */
      const svn_fs_id_t *node_id = svn_fs__dag_get_id (node->dag_node);
      svn_fs_id_t *root_id, *base_root_id;

      svn_fs__get_txn (&root_id, &base_root_id, node->txn, trail);

      /* If NODE refers to the transaction's current root, we're up to
         date.  */
      if (svn_fs_id_eq (node_id, root_id))
	return 0;

      /* If NODE refers to the transaction's uncloned root, which is
	 different from the current root, update it to point to the
	 current root.  */
      else if (svn_fs_id_eq (node_id, base_root_id))
	{
	  dag_node_t *cloned_root;

	  SVN_ERR (svn_fs__dag_txn_node (&cloned_root,
					 node->fs, node->txn, root_id,
					 trail));
	  set_clone (node, cloned_root, trail);
	  return 0;
	}

      /* Otherwise, NODE has no clone path, but it's referring to
	 neither the old nor the new directory.  Freak out.  */
      else
	abort ();
    }

  {
    /* We're looking for a clone of some random subdirectory.  Check
       the `clones' table.  */
    char *base_path = clone_path_to_string (node->clone_path, trail->pool);
    skel_t *clone_info;
    skel_t *clone_id_skel;

    SVN_ERR (svn_fs__check_clone (&clone_info, node->fs, node->txn, base_path,
				  trail));
    if (clone_info
	&& svn_fs__is_cloned (&clone_id_skel, clone_info))
      {
	/* The node has been cloned, and clone_id_skel contains the
	   clone's ID.  */
	svn_fs_id_t *clone_id = svn_fs_parse_id (clone_id_skel->data,
						 clone_id_skel->len,
						 trail->pool);
	dag_node_t *clone;

	if (! clone_id)
	  return svn_fs__err_corrupt_clone (node->fs, node->txn, base_path);

	SVN_ERR (svn_fs__dag_txn_node (&clone, node->fs, node->txn, clone_id,
				       trail));

	set_clone (node, clone, trail);
	return 0;
      }

    /* The node has not yet been cloned in this transaction, so the
       reference to the original is still accurate.  */
    return 0;
  }
}


/* Clone the node indicated by PATH in SVN_TXN in FS, as part of
   TRAIL.  Clone parents as necessary.  Set *CLONE_P to a dag_node_t
   for the clone we make (or find); allocate *CLONE_P in TRAIL->pool.

   This is the fabled "bubble up" function.  */
static svn_error_t *
clone_path (dag_node_t **clone_p,
	    svn_fs_t *fs,
	    const char *svn_txn,
	    clone_path_t *path,
	    trail_t *trail)
{
  /* Are we trying to clone a subdirectory, or the root directory?  */
  if (path)
    {
      /* We're trying to clone a subdirectory, not the root.  Has this
         node already been cloned?  */
      char *base_path = clone_path_to_string (path, trail->pool);
      skel_t *clone_info;
      skel_t *clone_id_skel, *parent_clone_id_skel, *entry_name_skel;

      /* Check the `clones' table for an entry for this path.  */
      SVN_ERR (svn_fs__check_clone (&clone_info, fs, svn_txn, base_path,
				    trail));

      if (! clone_info)
	{
	  /* According to the `clones' table, this node hasn't been
	     cloned yet.  Recursively clone the parent, then clone
	     the node itself.  */
	  dag_node_t *parent_clone, *child_clone;
	    
	  SVN_ERR (clone_path (&parent_clone, fs, svn_txn, path->parent,
			       trail));
	  SVN_ERR (svn_fs__dag_clone_child (&child_clone, parent_clone,
					    path->name, trail));
	  svn_fs__dag_close (parent_clone);
	  SVN_ERR (svn_fs__record_clone (fs, svn_txn, base_path,
					 svn_fs__dag_get_id (child_clone),
					 trail));

	  *clone_p = child_clone;
	  return 0;
	}
      else if (svn_fs__is_cloned (&clone_id_skel, clone_info))
	{
	  svn_fs_id_t *clone_id = svn_fs_parse_id (clone_id_skel->data,
						   clone_id_skel->len,
						   trail->pool);
	  dag_node_t *clone;

	  if (! clone_id)
	    return svn_fs__err_corrupt_clone (fs, svn_txn, base_path);

	  SVN_ERR (svn_fs__dag_txn_node (&clone, fs, svn_txn, clone_id,
					 trail));

	  *clone_p = clone;
	  return 0;
	}
      else if (svn_fs__is_renamed (&parent_clone_id_skel, &entry_name_skel,
				   clone_info))
	{
	  svn_fs_id_t *parent_clone_id
	    = svn_fs_parse_id (parent_clone_id_skel->data,
			       parent_clone_id_skel->len,
			       trail->pool);
	  dag_node_t *parent_clone, *child_clone;

	  if (! parent_clone_id)
	    return svn_fs__err_corrupt_clone (fs, svn_txn, base_path);

	  SVN_ERR (svn_fs__dag_txn_node (&parent_clone, fs, svn_txn,
					 parent_clone_id,
					 trail));
	  SVN_ERR (svn_fs__dag_clone_child (&child_clone, parent_clone,
					    path->name, trail));
	  svn_fs__dag_close (parent_clone);
	  SVN_ERR (svn_fs__record_clone (fs, svn_txn, base_path,
					 svn_fs__dag_get_id (child_clone),
					 trail));
	    
	  *clone_p = child_clone;
	  return 0;
	}
      else
	abort ();
    }
  else
    {
      /* We're trying to clone the root directory.  */
      dag_node_t *root_clone;

      SVN_ERR (svn_fs__dag_clone_root (&root_clone, fs, svn_txn,
				       trail));

      /* There's no need to make an entry in the `clones' table for
	 this, since we use the `transactions' entry itself to record
	 root directory clones, and svn_fs__dag_clone_root updates
	 that.  */

      *clone_p = root_clone;
      return 0;
    }
}
	    

/* Clone NODE in TRAIL, if it hasn't been cloned already.  After this
   call, NODE refers to the clone.  */
static svn_error_t *
make_clone (svn_fs_node_t *node,
	    trail_t *trail)
{
  dag_node_t *node_clone;

  if (! node->txn)
    return svn_fs__err_not_mutable (node->fs,
				    svn_fs__dag_get_id (node->dag_node));

  /* If the node has already been cloned, we're done.  */
  if (node->is_cloned)
    return 0;

  /* As far as we know, this node hasn't been cloned yet.
     Make/find the clone, cloning any parents as necessary.  */
  SVN_ERR (clone_path (&node_clone, node->fs, node->txn, node->clone_path,
		       trail));

  set_clone (node, node_clone, trail);
  return 0;
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
};


static svn_error_t *
txn_body_get_node_prop (void *baton,
			trail_t *trail)
{
  struct get_node_prop_args *args = baton;
  skel_t *proplist, *prop;

  SVN_ERR (check_for_clone (args->node, trail));
  SVN_ERR (svn_fs__dag_get_proplist (&proplist, args->node->dag_node, trail));
  
  /* Search the proplist for a property with the right name.  */
  for (prop = proplist->children; prop; prop = prop->next->next)
    {
      skel_t *name = prop;
      skel_t *value = prop->next;

      if (name->len == args->propname->len
	  && ! memcmp (name->data, args->propname->data, name->len))
	{
	  *(args->value_p) = svn_string_ncreate (value->data, value->len,
						 trail->pool);
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

  SVN_ERR (svn_fs__retry_txn (node->fs, txn_body_get_node_prop, &args, pool));

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
};


static svn_error_t *
txn_body_change_node_prop (void *baton,
			   trail_t *trail)
{
  struct change_node_prop_args *args = baton;
  skel_t *proplist, *prop;

  SVN_ERR (make_clone (args->node, trail));
  SVN_ERR (svn_fs__dag_get_proplist (&proplist, args->node->dag_node, trail));
  
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
					 trail->pool),
		       proplist);
      svn_fs__prepend (svn_fs__mem_atom (args->name->data,
					 args->name->len,
					 trail->pool),
		       proplist);
    }

  SVN_ERR (svn_fs__dag_set_proplist (args->node->dag_node, proplist, trail));

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

  SVN_ERR (svn_fs__retry_txn (node->fs, txn_body_change_node_prop, &args,
			      pool));

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


#if 0
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
#endif


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
  /* [[Don't forget to record renames in the `clones' table.]]  */
}


svn_error_t *
svn_fs_copy (svn_fs_node_t *parent,
	     const char *path,
	     svn_fs_node_t *child,
	     apr_pool_t *pool)
{
  abort ();
}
