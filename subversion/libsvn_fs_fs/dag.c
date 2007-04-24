/* dag.c : DAG-like interface filesystem, private to libsvn_fs
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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

#include "svn_path.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_props.h"
#include "svn_pools.h"
#include "svn_md5.h"

#include "dag.h"
#include "err.h"
#include "fs.h"
#include "key-gen.h"
#include "fs_fs.h"
#include "id.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"


/* Initializing a filesystem.  */

struct dag_node_t
{
  /* The filesystem this dag node came from. */
  svn_fs_t *fs;

  /* The pool in which this dag_node_t was allocated.  Unlike
     filesystem and root pools, this is not a private pool for this
     structure!  The caller may have allocated other objects of their
     own in it.  */
  apr_pool_t *pool;

  /* The node revision ID for this dag node, allocated in POOL.  */
  svn_fs_id_t *id;

  /* The node's type (file, dir, etc.) */
  svn_node_kind_t kind;

  /* The node's NODE-REVISION, or NULL if we haven't read it in yet.
     This is allocated in this node's POOL.

     If you're willing to respect all the rules above, you can munge
     this yourself, but you're probably better off just calling
     `get_node_revision' and `set_node_revision', which take care of
     things for you.  */
  node_revision_t *node_revision;

  /* the path at which this node was created. */
  const char *created_path;
};



/* Trivial helper/accessor functions. */
svn_node_kind_t svn_fs_fs__dag_node_kind(dag_node_t *node)
{
  return node->kind;
}


const svn_fs_id_t *
svn_fs_fs__dag_get_id(dag_node_t *node)
{
  return node->id;
}


const char *
svn_fs_fs__dag_get_created_path(dag_node_t *node)
{
  return node->created_path;
}


svn_fs_t *
svn_fs_fs__dag_get_fs(dag_node_t *node)
{
  return node->fs;
}


/* Dup NODEREV and all associated data into POOL */
static node_revision_t *
copy_node_revision(node_revision_t *noderev,
                   apr_pool_t *pool)
{
  node_revision_t *nr = apr_pcalloc(pool, sizeof(*nr));
  nr->kind = noderev->kind;
  if (noderev->predecessor_id)
    nr->predecessor_id = svn_fs_fs__id_copy(noderev->predecessor_id, pool);
  nr->predecessor_count = noderev->predecessor_count;
  if (noderev->copyfrom_path)
    nr->copyfrom_path = apr_pstrdup(pool, noderev->copyfrom_path);
  nr->copyfrom_rev = noderev->copyfrom_rev;
  nr->copyroot_path = apr_pstrdup(pool, noderev->copyroot_path);
  nr->copyroot_rev = noderev->copyroot_rev;
  nr->predecessor_count = noderev->predecessor_count;
  nr->data_rep = svn_fs_fs__rep_copy(noderev->data_rep, pool);
  nr->prop_rep = svn_fs_fs__rep_copy(noderev->prop_rep, pool);
  
  if (noderev->created_path)
    nr->created_path = apr_pstrdup(pool, noderev->created_path);
  return nr;
}


/* Set *NODEREV_P to the cached node-revision for NODE.  If NODE is
   immutable, the node-revision is allocated in NODE->pool.  If NODE
   is mutable, the node-revision is allocated in POOL.

   If you plan to change the contents of NODE, be careful!  We're
   handing you a pointer directly to our cached node-revision, not
   your own copy.  If you change it as part of some operation, but
   then some Berkeley DB function deadlocks or gets an error, you'll
   need to back out your changes, or else the cache will reflect
   changes that never got committed.  It's probably best not to change
   the structure at all.  */
static svn_error_t *
get_node_revision(node_revision_t **noderev_p,
                  dag_node_t *node,
                  apr_pool_t *pool)
{
  node_revision_t *noderev;

  /* If we've already got a copy, there's no need to read it in.  */
  if (! node->node_revision)
    {
      SVN_ERR(svn_fs_fs__get_node_revision(&noderev, node->fs,
                                           node->id, pool));
      node->node_revision = noderev;
    }
          
  /* Now NODE->node_revision is set.  */
  *noderev_p = node->node_revision;
  return SVN_NO_ERROR;
}


svn_boolean_t svn_fs_fs__dag_check_mutable(dag_node_t *node, 
                                           const char *txn_id)
{
  return (svn_fs_fs__id_txn_id(svn_fs_fs__dag_get_id(node)) != NULL);
}


svn_error_t *
svn_fs_fs__dag_get_node(dag_node_t **node,
                        svn_fs_t *fs,
                        const svn_fs_id_t *id,
                        apr_pool_t *pool)
{
  dag_node_t *new_node;
  node_revision_t *noderev;

  /* Construct the node. */
  new_node = apr_pcalloc(pool, sizeof(*new_node));
  new_node->fs = fs;
  new_node->id = svn_fs_fs__id_copy(id, pool); 
  new_node->pool = pool;

  /* Grab the contents so we can inspect the node's kind and created path. */
  SVN_ERR(get_node_revision(&noderev, new_node, pool));

  /* Initialize the KIND and CREATED_PATH attributes */
  new_node->kind = noderev->kind;
  new_node->created_path = apr_pstrdup(pool, noderev->created_path);

  /* Return a fresh new node */
  *node = new_node;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_get_revision(svn_revnum_t *rev,
                            dag_node_t *node,
                            apr_pool_t *pool)
{
  /* Look up the committed revision from the Node-ID. */
  *rev = svn_fs_fs__id_rev(node->id);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_get_predecessor_id(const svn_fs_id_t **id_p,
                                  dag_node_t *node,
                                  apr_pool_t *pool)
{
  node_revision_t *noderev;
  
  SVN_ERR(get_node_revision(&noderev, node, pool));
  *id_p = noderev->predecessor_id;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_get_predecessor_count(int *count,
                                     dag_node_t *node,
                                     apr_pool_t *pool)
{
  node_revision_t *noderev;
  
  SVN_ERR(get_node_revision(&noderev, node, pool));
  *count = noderev->predecessor_count;
  return SVN_NO_ERROR;
}



/*** Directory node functions ***/

/* Some of these are helpers for functions outside this section. */

/* Set *ID_P to the node-id for entry NAME in PARENT.  If no such
   entry, set *ID_P to NULL but do not error.  The entry is allocated
   in POOL or in the same pool as PARENT; the caller should copy if it
   cares.  */
static svn_error_t *
dir_entry_id_from_node(const svn_fs_id_t **id_p, 
                       dag_node_t *parent,
                       const char *name,
                       apr_pool_t *pool)
{
  apr_hash_t *entries;
  svn_fs_dirent_t *dirent;

  SVN_ERR(svn_fs_fs__dag_dir_entries(&entries, parent, pool));
  if (entries)
    dirent = apr_hash_get(entries, name, APR_HASH_KEY_STRING);
  else
    dirent = NULL;
    
  *id_p = dirent ? dirent->id : NULL;
  return SVN_NO_ERROR;
}


/* Add or set in PARENT a directory entry NAME pointing to ID.
   Allocations are done in POOL.

   Assumptions:
   - PARENT is a mutable directory.
   - ID does not refer to an ancestor of parent
   - NAME is a single path component
*/
static svn_error_t *
set_entry(dag_node_t *parent,
          const char *name,
          const svn_fs_id_t *id,
          svn_node_kind_t kind,
          const char *txn_id,
          apr_pool_t *pool)
{
  node_revision_t *parent_noderev;

  /* Get the parent's node-revision. */
  SVN_ERR(get_node_revision(&parent_noderev, parent, pool));

  /* Set the new entry. */
  SVN_ERR(svn_fs_fs__set_entry(parent->fs, txn_id, parent_noderev, name, id,
                               kind, pool));
  
  return SVN_NO_ERROR;
}


/* Make a new entry named NAME in PARENT.  If IS_DIR is true, then the
   node revision the new entry points to will be a directory, else it
   will be a file.  The new node will be allocated in POOL.  PARENT
   must be mutable, and must not have an entry named NAME.  */
static svn_error_t *
make_entry(dag_node_t **child_p,
           dag_node_t *parent,
           const char *parent_path,
           const char *name,
           svn_boolean_t is_dir,
           const char *txn_id,
           apr_pool_t *pool)
{
  const svn_fs_id_t *new_node_id;
  node_revision_t new_noderev, *parent_noderev;

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       _("Attempted to create a node with an illegal name '%s'"), name);

  /* Make sure that parent is a directory */
  if (parent->kind != svn_node_dir)
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, NULL,
       _("Attempted to create entry in non-directory parent"));

  /* Check that the parent is mutable. */
  if (! svn_fs_fs__dag_check_mutable(parent, txn_id))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to clone child of non-mutable node"));

  /* Create the new node's NODE-REVISION */
  memset(&new_noderev, 0, sizeof(new_noderev));
  new_noderev.kind = is_dir ? svn_node_dir : svn_node_file;
  new_noderev.created_path = svn_path_join(parent_path, name, pool);

  SVN_ERR(get_node_revision(&parent_noderev, parent, pool));
  new_noderev.copyroot_path = apr_pstrdup(pool,
                                          parent_noderev->copyroot_path);
  new_noderev.copyroot_rev = parent_noderev->copyroot_rev;
  new_noderev.copyfrom_rev = SVN_INVALID_REVNUM;
  new_noderev.copyfrom_path = NULL;
  
  SVN_ERR(svn_fs_fs__create_node
          (&new_node_id, svn_fs_fs__dag_get_fs(parent), &new_noderev,
           svn_fs_fs__id_copy_id(svn_fs_fs__dag_get_id(parent)),
           txn_id, pool));

  /* Create a new dag_node_t for our new node */
  SVN_ERR(svn_fs_fs__dag_get_node(child_p, svn_fs_fs__dag_get_fs(parent),
                                  new_node_id, pool));

  /* We can safely call set_entry because we already know that
     PARENT is mutable, and we just created CHILD, so we know it has
     no ancestors (therefore, PARENT cannot be an ancestor of CHILD) */
  SVN_ERR(set_entry(parent, name, svn_fs_fs__dag_get_id(*child_p),
                    new_noderev.kind, txn_id, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_dir_entries(apr_hash_t **entries,
                           dag_node_t *node,
                           apr_pool_t *pool)
{
  node_revision_t *noderev;

  SVN_ERR(get_node_revision(&noderev, node, pool));

  if (noderev->kind != svn_node_dir)
    return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                            _("Can't get entries of non-directory"));

  return svn_fs_fs__rep_contents_dir(entries, node->fs, noderev, pool);
}


svn_error_t *
svn_fs_fs__dag_set_entry(dag_node_t *node,
                         const char *entry_name,
                         const svn_fs_id_t *id,
                         svn_node_kind_t kind,
                         const char *txn_id,
                         apr_pool_t *pool)
{
  /* Check it's a directory. */
  if (node->kind != svn_node_dir)
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, NULL,
       _("Attempted to set entry in non-directory node"));
  
  /* Check it's mutable. */
  if (! svn_fs_fs__dag_check_mutable(node, txn_id))
    return svn_error_create
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to set entry in immutable node"));

  return set_entry(node, entry_name, id, kind, txn_id, pool);
}



/*** Proplists. ***/

svn_error_t *
svn_fs_fs__dag_get_proplist(apr_hash_t **proplist_p,
                            dag_node_t *node,
                            apr_pool_t *pool)
{
  node_revision_t *noderev;
  apr_hash_t *proplist = NULL;

  SVN_ERR(get_node_revision(&noderev, node, pool));

  SVN_ERR(svn_fs_fs__get_proplist(&proplist, node->fs,
                                  noderev, pool));

  *proplist_p = proplist;
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_set_proplist(dag_node_t *node,
                            apr_hash_t *proplist,
                            const char *txn_id,
                            apr_pool_t *pool)
{
  node_revision_t *noderev;

  /* Sanity check: this node better be mutable! */
  if (! svn_fs_fs__dag_check_mutable(node, txn_id))
    {
      svn_string_t *idstr = svn_fs_fs__id_unparse(node->id, node->pool);
      return svn_error_createf
        (SVN_ERR_FS_NOT_MUTABLE, NULL,
         "Can't set proplist on *immutable* node-revision %s",
         idstr->data);
    }

  /* Go get a fresh NODE-REVISION for this node. */
  SVN_ERR(get_node_revision(&noderev, node, pool));

  /* Set the new proplist. */
  SVN_ERR(svn_fs_fs__set_proplist(node->fs, noderev, proplist, pool));

  return SVN_NO_ERROR;
}



/*** Roots. ***/

svn_error_t *
svn_fs_fs__dag_revision_root(dag_node_t **node_p,
                             svn_fs_t *fs,
                             svn_revnum_t rev,
                             apr_pool_t *pool)
{
  svn_fs_id_t *root_id;

  SVN_ERR(svn_fs_fs__rev_get_root(&root_id, fs, rev, pool));
  return svn_fs_fs__dag_get_node(node_p, fs, root_id, pool);
}


svn_error_t *
svn_fs_fs__dag_txn_root(dag_node_t **node_p,
                        svn_fs_t *fs,
                        const char *txn_id,
                        apr_pool_t *pool)
{
  const svn_fs_id_t *root_id, *ignored;
  
  SVN_ERR(svn_fs_fs__get_txn_ids(&root_id, &ignored, fs, txn_id, pool));
  return svn_fs_fs__dag_get_node(node_p, fs, root_id, pool);
}


svn_error_t *
svn_fs_fs__dag_txn_base_root(dag_node_t **node_p,
                             svn_fs_t *fs,
                             const char *txn_id,
                             apr_pool_t *pool)
{
  const svn_fs_id_t *base_root_id, *ignored;
  
  SVN_ERR(svn_fs_fs__get_txn_ids(&ignored, &base_root_id, fs, txn_id, pool));
  return svn_fs_fs__dag_get_node(node_p, fs, base_root_id, pool);
}


svn_error_t *
svn_fs_fs__dag_clone_child(dag_node_t **child_p,
                           dag_node_t *parent,
                           const char *parent_path,
                           const char *name,
                           const char *copy_id,
                           const char *txn_id,
                           svn_boolean_t is_parent_copyroot,
                           apr_pool_t *pool)
{
  dag_node_t *cur_entry; /* parent's current entry named NAME */
  const svn_fs_id_t *new_node_id; /* node id we'll put into NEW_NODE */
  svn_fs_t *fs = svn_fs_fs__dag_get_fs(parent);

  /* First check that the parent is mutable. */
  if (! svn_fs_fs__dag_check_mutable(parent, txn_id))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       "Attempted to clone child of non-mutable node");

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       "Attempted to make a child clone with an illegal name '%s'", name);

  /* Find the node named NAME in PARENT's entries list if it exists. */
  SVN_ERR(svn_fs_fs__dag_open(&cur_entry, parent, name, pool));

  /* Check for mutability in the node we found.  If it's mutable, we
     don't need to clone it. */
  if (svn_fs_fs__dag_check_mutable(cur_entry, txn_id))
    {
      /* This has already been cloned */
      new_node_id = cur_entry->id;
    }
  else
    {
      node_revision_t *noderev, *parent_noderev;
      
      /* Go get a fresh NODE-REVISION for current child node. */
      SVN_ERR(get_node_revision(&noderev, cur_entry, pool));
      
      if (is_parent_copyroot)
        {
          SVN_ERR(get_node_revision(&parent_noderev, parent, pool));
          noderev->copyroot_rev = parent_noderev->copyroot_rev;
          noderev->copyroot_path = apr_pstrdup(pool,
                                               parent_noderev->copyroot_path);
        }
      
      noderev->copyfrom_path = NULL;
      noderev->copyfrom_rev = SVN_INVALID_REVNUM;
      
      noderev->predecessor_id = svn_fs_fs__id_copy(cur_entry->id, pool);
      if (noderev->predecessor_count != -1)
        noderev->predecessor_count++;
      noderev->created_path = svn_path_join(parent_path, name, pool);
      
      SVN_ERR(svn_fs_fs__create_successor(&new_node_id, fs, cur_entry->id, 
                                          noderev, copy_id, txn_id, pool));

      /* Replace the ID in the parent's ENTRY list with the ID which
         refers to the mutable clone of this child. */
      SVN_ERR(set_entry(parent, name, new_node_id, noderev->kind, txn_id,
                        pool));
    }

  /* Initialize the youngster. */
  return svn_fs_fs__dag_get_node(child_p, fs, new_node_id, pool);
}



svn_error_t *
svn_fs_fs__dag_clone_root(dag_node_t **root_p,
                          svn_fs_t *fs,
                          const char *txn_id,
                          apr_pool_t *pool)
{
  const svn_fs_id_t *base_root_id, *root_id;
  
  /* Get the node ID's of the root directories of the transaction and
     its base revision.  */
  SVN_ERR(svn_fs_fs__get_txn_ids(&root_id, &base_root_id, fs, txn_id, pool));

  /* Oh, give me a clone...
     (If they're the same, we haven't cloned the transaction's root
     directory yet.)  */
  if (svn_fs_fs__id_eq(root_id, base_root_id)) 
    {
      abort();
    }

  /* One way or another, root_id now identifies a cloned root node. */
  SVN_ERR(svn_fs_fs__dag_get_node(root_p, fs, root_id, pool));

  /*
   * (Sung to the tune of "Home, Home on the Range", with thanks to
   * Randall Garrett and Isaac Asimov.)
   */

  return SVN_NO_ERROR;
}


/* Delete the directory entry named NAME from PARENT, allocating from
   POOL.  PARENT must be mutable.  NAME must be a single path
   component.  If REQUIRE_EMPTY is true and the node being deleted is
   a directory, it must be empty.

   If return SVN_ERR_FS_NO_SUCH_ENTRY, then there is no entry NAME in
   PARENT.  */
svn_error_t *
svn_fs_fs__dag_delete(dag_node_t *parent,
                      const char *name,
                      const char *txn_id,
                      apr_pool_t *pool)
{
  node_revision_t *parent_noderev;
  apr_hash_t *entries;
  svn_fs_t *fs = parent->fs;
  svn_fs_dirent_t *dirent;
  svn_fs_id_t *id;
  dag_node_t *node;

  /* Make sure parent is a directory. */
  if (parent->kind != svn_node_dir)
    return svn_error_createf
      (SVN_ERR_FS_NOT_DIRECTORY, NULL,
       "Attempted to delete entry '%s' from *non*-directory node", name);

  /* Make sure parent is mutable. */
  if (! svn_fs_fs__dag_check_mutable(parent, txn_id))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       "Attempted to delete entry '%s' from immutable directory node", name);

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       "Attempted to delete a node with an illegal name '%s'", name);

  /* Get a fresh NODE-REVISION for the parent node. */
  SVN_ERR(get_node_revision(&parent_noderev, parent, pool));

  /* Get a dirent hash for this directory. */
  SVN_ERR(svn_fs_fs__rep_contents_dir(&entries, fs, parent_noderev, pool));

  /* Find name in the ENTRIES hash. */
  dirent = apr_hash_get(entries, name, APR_HASH_KEY_STRING);

  /* If we never found ID in ENTRIES (perhaps because there are no
     ENTRIES, perhaps because ID just isn't in the existing ENTRIES
     ... it doesn't matter), return an error.  */
  if (! dirent)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_ENTRY, NULL,
       "Delete failed--directory has no entry '%s'", name);

  /* Stash a copy of the ID, since dirent will become invalid during
     svn_fs_fs__dag_delete_if_mutable. */
  id = svn_fs_fs__id_copy(dirent->id, pool);
  
  /* Use the ID to get the entry's node.  */
  SVN_ERR(svn_fs_fs__dag_get_node(&node, svn_fs_fs__dag_get_fs(parent), id,
                                  pool));

  /* If mutable, remove it and any mutable children from db. */
  SVN_ERR(svn_fs_fs__dag_delete_if_mutable(parent->fs, id, txn_id, pool));

  /* Remove this entry from its parent's entries list. */
  SVN_ERR(svn_fs_fs__set_entry(parent->fs, txn_id, parent_noderev, name,
                               NULL, svn_node_unknown, pool));
    
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_remove_node(svn_fs_t *fs,
                           const svn_fs_id_t *id,
                           const char *txn_id,
                           apr_pool_t *pool)
{
  dag_node_t *node;

  /* Fetch the node. */
  SVN_ERR(svn_fs_fs__dag_get_node(&node, fs, id, pool));

  /* If immutable, do nothing and return immediately. */
  if (! svn_fs_fs__dag_check_mutable(node, txn_id))
    return svn_error_createf(SVN_ERR_FS_NOT_MUTABLE, NULL,
                             "Attempted removal of immutable node");

  /* Delete the node revision. */
  SVN_ERR(svn_fs_fs__delete_node_revision(fs, id, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_delete_if_mutable(svn_fs_t *fs,
                                 const svn_fs_id_t *id,
                                 const char *txn_id,
                                 apr_pool_t *pool)
{
  dag_node_t *node;

  /* Get the node. */
  SVN_ERR(svn_fs_fs__dag_get_node(&node, fs, id, pool));

  /* If immutable, do nothing and return immediately. */
  if (! svn_fs_fs__dag_check_mutable(node, txn_id))
    return SVN_NO_ERROR;

  /* Else it's mutable.  Recurse on directories... */
  if (node->kind == svn_node_dir)
    {
      apr_hash_t *entries;
      apr_hash_index_t *hi;

      /* Loop over hash entries */
      SVN_ERR(svn_fs_fs__dag_dir_entries(&entries, node, pool));
      entries = svn_fs_fs__copy_dir_entries(entries, pool);
      if (entries)
        {
          for (hi = apr_hash_first(pool, entries);
               hi;
               hi = apr_hash_next(hi))
            {
              void *val;
              svn_fs_dirent_t *dirent;

              apr_hash_this(hi, NULL, NULL, &val);
              dirent = val;
              SVN_ERR(svn_fs_fs__dag_delete_if_mutable(fs, dirent->id,
                                                       txn_id, pool));
            }
        }
    }

  /* ... then delete the node itself, after deleting any mutable
     representations and strings it points to. */
  SVN_ERR(svn_fs_fs__dag_remove_node(fs, id, txn_id, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__dag_make_file(dag_node_t **child_p,
                         dag_node_t *parent,
                         const char *parent_path,
                         const char *name,
                         const char *txn_id, 
                         apr_pool_t *pool)
{
  /* Call our little helper function */
  return make_entry(child_p, parent, parent_path, name, FALSE, txn_id, pool);
}


svn_error_t *
svn_fs_fs__dag_make_dir(dag_node_t **child_p,
                        dag_node_t *parent,
                        const char *parent_path,
                        const char *name,
                        const char *txn_id, 
                        apr_pool_t *pool)
{
  /* Call our little helper function */
  return make_entry(child_p, parent, parent_path, name, TRUE, txn_id, pool);
}


svn_error_t *
svn_fs_fs__dag_get_contents(svn_stream_t **contents_p,
                            dag_node_t *file,
                            apr_pool_t *pool)
{ 
  node_revision_t *noderev;
  svn_stream_t *contents;

  /* Make sure our node is a file. */
  if (file->kind != svn_node_file)
    return svn_error_createf 
      (SVN_ERR_FS_NOT_FILE, NULL,
       "Attempted to get textual contents of a *non*-file node");
  
  /* Go get a fresh node-revision for FILE. */
  SVN_ERR(get_node_revision(&noderev, file, pool));

  /* Get a stream to the contents. */
  SVN_ERR(svn_fs_fs__get_contents(&contents, file->fs,
                                  noderev, pool));

  *contents_p = contents;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_get_file_delta_stream(svn_txdelta_stream_t **stream_p,
                                     dag_node_t *source,
                                     dag_node_t *target,
                                     apr_pool_t *pool)
{
  node_revision_t *src_noderev;
  node_revision_t *tgt_noderev;

  /* Make sure our nodes are files. */
  if ((source && source->kind != svn_node_file)
      || target->kind != svn_node_file)
    return svn_error_createf 
      (SVN_ERR_FS_NOT_FILE, NULL,
       "Attempted to get textual contents of a *non*-file node");
  
  /* Go get fresh node-revisions for the nodes. */
  if (source)
    SVN_ERR(get_node_revision(&src_noderev, source, pool));
  else
    src_noderev = NULL;
  SVN_ERR(get_node_revision(&tgt_noderev, target, pool));

  /* Get the delta stream. */
  SVN_ERR(svn_fs_fs__get_file_delta_stream(stream_p, target->fs,
                                           src_noderev, tgt_noderev, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_file_length(svn_filesize_t *length,
                           dag_node_t *file,
                           apr_pool_t *pool)
{ 
  node_revision_t *noderev;

  /* Make sure our node is a file. */
  if (file->kind != svn_node_file)
    return svn_error_createf 
      (SVN_ERR_FS_NOT_FILE, NULL,
       "Attempted to get length of a *non*-file node");

  /* Go get a fresh node-revision for FILE, and . */
  SVN_ERR(get_node_revision(&noderev, file, pool));

  SVN_ERR(svn_fs_fs__file_length(length, noderev, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_file_checksum(unsigned char digest[],
                             dag_node_t *file,
                             apr_pool_t *pool)
{ 
  node_revision_t *noderev;

  if (file->kind != svn_node_file)
    return svn_error_createf 
      (SVN_ERR_FS_NOT_FILE, NULL,
       "Attempted to get checksum of a *non*-file node");

  SVN_ERR(get_node_revision(&noderev, file, pool));

  SVN_ERR(svn_fs_fs__file_checksum(digest, noderev, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dag_get_edit_stream(svn_stream_t **contents,
                               dag_node_t *file,
                               const char *txn_id,
                               apr_pool_t *pool)
{
  node_revision_t *noderev;
  svn_stream_t *ws;

  /* Make sure our node is a file. */
  if (file->kind != svn_node_file)
    return svn_error_createf 
      (SVN_ERR_FS_NOT_FILE, NULL,
       "Attempted to set textual contents of a *non*-file node");
  
  /* Make sure our node is mutable. */
  if (! svn_fs_fs__dag_check_mutable(file, txn_id))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       "Attempted to set textual contents of an immutable node");

  /* Get the node revision. */
  SVN_ERR(get_node_revision(&noderev, file, pool));

  SVN_ERR(svn_fs_fs__set_contents(&ws, file->fs, noderev, pool));

  *contents = ws;
  
  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_fs__dag_finalize_edits(dag_node_t *file,
                              const char *checksum,
                              const char *txn_id, 
                              apr_pool_t *pool)
{
  unsigned char digest[APR_MD5_DIGESTSIZE];
  const char *hex;

  if (checksum)
    {
      SVN_ERR(svn_fs_fs__dag_file_checksum(digest, file, pool));
      hex = svn_md5_digest_to_cstring(digest, pool);
      if (hex && strcmp(checksum, hex) != 0)
        return svn_error_createf(SVN_ERR_CHECKSUM_MISMATCH, NULL,
                                 _("Checksum mismatch, file '%s':\n"
                                   "   expected:  %s\n"
                                   "     actual:  %s\n"),
                                 file->created_path, checksum, hex);
    }

  return SVN_NO_ERROR;
}


dag_node_t *
svn_fs_fs__dag_dup(dag_node_t *node,
                   apr_pool_t *pool)
{
  /* Allocate our new node. */
  dag_node_t *new_node = apr_pcalloc(pool, sizeof(*new_node));

  new_node->fs = node->fs;
  new_node->pool = pool;
  new_node->id = svn_fs_fs__id_copy(node->id, pool);
  new_node->kind = node->kind;
  new_node->created_path = apr_pstrdup(pool, node->created_path);

  /* Leave new_node->node_revision zero for now, so it'll get read in.
     We can get fancy and duplicate node's cache later.  */

  return new_node;
}


svn_error_t *
svn_fs_fs__dag_open(dag_node_t **child_p,
                    dag_node_t *parent,
                    const char *name,
                    apr_pool_t *pool)
{
  const svn_fs_id_t *node_id;

  /* Ensure that NAME exists in PARENT's entry list. */
  SVN_ERR(dir_entry_id_from_node(&node_id, parent, name, pool));
  if (! node_id)
    return svn_error_createf 
      (SVN_ERR_FS_NOT_FOUND, NULL,
       "Attempted to open non-existent child node '%s'", name);
  
  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       "Attempted to open node with an illegal name '%s'", name);

  /* Now get the node that was requested. */
  return svn_fs_fs__dag_get_node(child_p, svn_fs_fs__dag_get_fs(parent),
                                 node_id, pool);
}


svn_error_t *
svn_fs_fs__dag_copy(dag_node_t *to_node,
                    const char *entry,
                    dag_node_t *from_node,
                    svn_boolean_t preserve_history,
                    svn_revnum_t from_rev,
                    const char *from_path,
                    const char *txn_id, 
                    apr_pool_t *pool)
{
  const svn_fs_id_t *id;
  
  if (preserve_history)
    {
      node_revision_t *from_noderev, *to_noderev;
      const char *copy_id;
      const svn_fs_id_t *src_id = svn_fs_fs__dag_get_id(from_node);
      svn_fs_t *fs = svn_fs_fs__dag_get_fs(from_node);
      
      /* Make a copy of the original node revision. */
      SVN_ERR(get_node_revision(&from_noderev, from_node, pool));
      to_noderev = copy_node_revision(from_noderev, pool);

      /* Reserve a copy ID for this new copy. */
      SVN_ERR(svn_fs_fs__reserve_copy_id(&copy_id, fs, txn_id, pool));

      /* Create a successor with its predecessor pointing at the copy
         source. */
      to_noderev->predecessor_id = svn_fs_fs__id_copy(src_id, pool);
      if (to_noderev->predecessor_count != -1)
        to_noderev->predecessor_count++;
      to_noderev->created_path =
        svn_path_join(svn_fs_fs__dag_get_created_path(to_node), entry,
                      pool);
      to_noderev->copyfrom_path = apr_pstrdup(pool, from_path);
      to_noderev->copyfrom_rev = from_rev;

      /* Set the copyroot equal to our own id. */
      to_noderev->copyroot_path = NULL;

      SVN_ERR(svn_fs_fs__create_successor(&id, fs, src_id, to_noderev,
                                          copy_id, txn_id, pool));

    }
  else  /* don't preserve history */
    {
      id = svn_fs_fs__dag_get_id(from_node);
    }
      
  /* Set the entry in to_node to the new id. */
  SVN_ERR(svn_fs_fs__dag_set_entry(to_node, entry, id, from_node->kind,
                                   txn_id, pool));

  return SVN_NO_ERROR;
}



/*** Comparison. ***/

svn_error_t *
svn_fs_fs__things_different(svn_boolean_t *props_changed,
                            svn_boolean_t *contents_changed,
                            dag_node_t *node1,
                            dag_node_t *node2,
                            apr_pool_t *pool)
{
  node_revision_t *noderev1, *noderev2;

  /* If we have no place to store our results, don't bother doing
     anything. */
  if (! props_changed && ! contents_changed)
    return SVN_NO_ERROR;

  /* The node revision skels for these two nodes. */
  SVN_ERR(get_node_revision(&noderev1, node1, pool));
  SVN_ERR(get_node_revision(&noderev2, node2, pool));

  /* Compare property keys. */
  if (props_changed != NULL)
    *props_changed = (! svn_fs_fs__noderev_same_rep_key(noderev1->prop_rep,
                                                        noderev2->prop_rep));

  /* Compare contents keys. */
  if (contents_changed != NULL)
    *contents_changed = 
      (! svn_fs_fs__noderev_same_rep_key(noderev1->data_rep,
                                         noderev2->data_rep));
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__dag_get_copyroot(svn_revnum_t *rev,
                            const char **path,
                            dag_node_t *node,
                            apr_pool_t *pool)
{
  node_revision_t *noderev;
  
  /* Go get a fresh node-revision for FILE. */
  SVN_ERR(get_node_revision(&noderev, node, pool));

  *rev = noderev->copyroot_rev;
  *path = noderev->copyroot_path;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__dag_get_copyfrom_rev(svn_revnum_t *rev,
                                dag_node_t *node,
                                apr_pool_t *pool)
{
  node_revision_t *noderev;
  
  /* Go get a fresh node-revision for FILE. */
  SVN_ERR(get_node_revision(&noderev, node, pool));

  *rev = noderev->copyfrom_rev;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__dag_get_copyfrom_path(const char **path,
                                 dag_node_t *node,
                                 apr_pool_t *pool)
{
  node_revision_t *noderev;
  
  /* Go get a fresh node-revision for FILE. */
  SVN_ERR(get_node_revision(&noderev, node, pool));

  *path = noderev->copyfrom_path;
  
  return SVN_NO_ERROR;
}
