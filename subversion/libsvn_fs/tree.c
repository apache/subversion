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
#include "tree.h"



/* The root structure.  */

struct svn_fs_root_t
{

  /* What filesystem does this root belong to?  */
  svn_fs_t *fs;

  /* All data belonging to this root is allocated in this pool.
     Destroying this pool will correctly free all resources the root
     holds.  */
  apr_pool_t *pool;

  /* For transaction roots, the name of that transaction, allocated in
     POOL.  For revision roots, this is zero.  */
  const char *txn;

  /* For revision roots, the number of that revision.  For transaction
     roots, it's -1.  */
  svn_revnum_t rev;

};



/* Creating root objects.  */


/* Construct a new root object in FS, allocated from POOL.  */
static svn_fs_root_t *
make_root (svn_fs_t *fs,
           apr_pool_t *pool)
{
  /* We create a subpool for each root object to allow us to implement
     svn_fs_close_root.  */
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_fs_root_t *root = apr_pcalloc (subpool, sizeof (*root));

  root->fs = fs;
  root->pool = pool;
  root->rev = -1;

  return root;
}


/* Construct a root object referring to the root of REVISION in FS.
   Create the new root in POOL.  */
static svn_fs_root_t *
make_revision_root (svn_fs_t *fs,
                    svn_revnum_t rev,
                    apr_pool_t *pool)
{
  svn_fs_root_t *root = make_root (fs, pool);
  root->rev = rev;

  return root;
}


/* Construct a root object referring to the root of the transaction
   named TXN in FS.  Create the new root in POOL.  */
static svn_fs_root_t *
make_txn_root (svn_fs_t *fs,
               const char *txn,
               apr_pool_t *pool)
{
  svn_fs_root_t *root = make_root (fs, pool);
  root->txn = apr_pstrdup (root->pool, txn);

  return root;
}



/* Simple root operations.  */

void
svn_fs_close_root (svn_fs_root_t *root)
{
  apr_pool_destroy (root->pool);
}


svn_fs_t *
svn_fs_root_fs (svn_fs_root_t *root)
{
  return root->fs;
}


int
svn_fs_is_txn_root (svn_fs_root_t *root)
{
  return !! root->txn;
}


int
svn_fs_is_revision_root (svn_fs_root_t *root)
{
  return root->rev != -1;
}


const char *
svn_fs_txn_root_name (svn_fs_root_t *root,
                      apr_pool_t *pool)
{
  if (root->txn)
    return apr_pstrdup (pool, root->txn);
  else
    return 0;
}


svn_revnum_t
svn_fs_revision_root_revision (svn_fs_root_t *root)
{
  return root->rev;
}



/* Traversing directory paths.  */


/* Open the node identified by PATH under ROOT, as part of TRAIL. Set
   *CHILD_P to the new node, *PARENT_P to its parent, *NAME_P to
   *CHILD_P's name in *PARENT_P, all allocated in TRAIL->pool. PATH is
   a slash-separated directory path with.  If PATH is empty, *PARENT_P
   will be NULL, *CHILD_P will be ROOT and *NAME_P will be empty. */
static svn_error_t *
open_path (dag_node_t **child_p,
                       dag_node_t **parent_p,
                       const char **name_p,
                       dag_node_t *root,
                       const char *path,
                       trail_t *trail)
{
#if 0
  dag_node_t *child = root;
  dag_node_t *parent = NULL;
  char *mutable_path = apr_pstrdup (trail->pool, path);
  const char *name;

  while (svn_path_first_component(&name, &mutable_path,
                                  svn_path_repos_style))
    {
      parent = child;
      SVN_ERR (svn_fs__dag_open (&child, parent, name, trail));
    }

  *child_p = child;
  *parent_p = parent;
  *name_p = name;
#else
  abort ();
#endif
  return SVN_NO_ERROR;
}



/* Generic node operations.  */

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
svn_fs_node_prop (svn_string_t **value_p,
                  svn_fs_root_t *root,
                  const char *path,
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
svn_fs_node_proplist (apr_hash_t **table_p,
                      svn_fs_root_t *root,
                      const char *path,
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
svn_fs_change_node_prop (svn_fs_root_t *root,
                         const char *path,
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
              svn_fs_root_t *source_root,
              const char *source_path,
              svn_fs_root_t *target_root,
              const char *target_path,
              svn_fs_root_t *ancestor_root,
              const char *ancestor_path,
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


static svn_error_t *
traverse_to_parent (svn_fs_node_t **parent_p,
                    const char **entry_name,
                    svn_fs_node_t *root,
                    const char *path,
                    trail_t *trail)
{
  abort ();
}


svn_error_t *
svn_fs_dir_entries (apr_hash_t **table_p,
                    svn_fs_root_t *root,
                    const char *path,
                    apr_pool_t *pool)
{
  abort ();
}


svn_error_t *
svn_fs_make_dir (svn_fs_root_t *root,
                 const char *path,
                 apr_pool_t *pool)
{
  abort ();
}
                              

struct delete_args
{
  svn_fs_root_t *root;
  const char *path;
};


static svn_error_t *
txn_body_delete (void *baton,
                 trail_t *trail)
{
  struct delete_args *args = baton;
  const char *entry_name;
  dag_node_t *parent;
  dag_node_t *child;

  SVN_ERR (svn_fs__dag_open_path (&child, &parent, &entry_name,
                                  args->root, args->path, trail));
  svn_fs__dag_close (child);

  /* We can't remove the root of the filesystem. */
  if (parent == NULL)
    return svn_err_create (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
                           "attemptng to delete the root directory");

  /* We have a bug here: we'll happily delete non-empty directories,
     if they're shared with the base revision.  */
  SVN_ERR (make_clone (parent, trail));
  SVN_ERR (svn_fs__dag_delete (parent, entry_name, trail));

  return 0;
}


svn_error_t *
svn_fs_delete (svn_fs_root_t *root,
               const char *path,
               apr_pool_t *pool)
{
  struct delete_args args;

  SVN_ERR (check_node_mutable (parent));

  args.parent = parent;
  args.path   = path;
  return svn_fs__retry_txn (parent->fs, txn_body_delete, &args, pool);
}


svn_error_t *
svn_fs_delete_tree (svn_fs_root_t *root,
                    const char *path,
                    apr_pool_t *pool)
{
  abort ();
}


svn_error_t *
svn_fs_rename (svn_fs_root_t *root,
               const char *from,
               const char *to,
               apr_pool_t *pool)
{
  abort ();
  /* [[Don't forget to record renames in the `clones' table.]]  */
}


svn_error_t *
svn_fs_copy (svn_fs_root_t *from_root,
             const char *from_path,
             svn_fs_root_t *to_root,
             const char *to_path,
             apr_pool_t *pool)
{
  abort ();
}



/* Creating transaction and revision root nodes.  */


svn_error_t *
svn_fs__txn_root_node (svn_fs_root_t **root_p,
                       svn_fs_t *fs,
                       const char *txn,
                       trail_t *trail)
{
  svn_fs_id_t *root_id, *base_root_id;
  dag_node_t *dag_root;
  svn_fs_root_t *root;

  SVN_ERR (svn_fs__get_txn (&root_id, &base_root_id, fs, txn, trail));
  SVN_ERR (svn_fs__dag_txn_node (&dag_root, fs, txn, root_id, trail));

  root = new_root_object (fs, trail->pool);
  root->txn = apr_pstrdup (root->pool, txn);
  root->is_cloned = ! svn_fs_id_eq (root_id, base_root_id);
  root->dag_node = dag_root;

  *root_p = root;
  return 0;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
