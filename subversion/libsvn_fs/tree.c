/* tree.c : tree-like filesystem, built on DAG filesystem
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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
#include <assert.h>
#include "svn_error.h"
#include "svn_fs.h"
#include "skel.h"
#include "id.h"
#include "fs.h"
#include "err.h"
#include "trail.h"
#include "txn-table.h"
#include "rev-table.h"
#include "txn.h"
#include "dag.h"
#include "tree.h"



/* The root structure.  */

typedef enum root_kind_t {
  unspecified_root = 0,
  revision_root,
  transaction_root,
  id_root
} root_kind_t;

struct svn_fs_root_t
{

  /* What filesystem does this root belong to?  */
  svn_fs_t *fs;

  /* All data belonging to this root is allocated in this pool.
     Destroying this pool will correctly free all resources the root
     holds.  */
  apr_pool_t *pool;

  /* What kind of root is this?  */
  root_kind_t kind;

  /* For transaction roots (i.e., KIND == transaction_root), the name of
     that transaction, allocated in POOL.  */
  const char *txn;

  /* For revision roots (i.e., KIND == revision_root), the number of
     that revision.  */
  svn_revnum_t rev;

  /* For revision roots, this is a dag node for the revision's root
     directory.  For transaction roots, we open the root directory
     afresh every time, since the root may have been cloned, or
     the transaction may have disappeared altogether.  */
  dag_node_t *root_dir;
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

  return root;
}


/* Construct a root object referring to the root of REVISION in FS,
   whose root directory is ROOT_DIR.  Create the new root in POOL.  */
static svn_fs_root_t *
make_revision_root (svn_fs_t *fs,
                    svn_revnum_t rev,
                    dag_node_t *root_dir,
                    apr_pool_t *pool)
{
  svn_fs_root_t *root = make_root (fs, pool);
  root->kind = revision_root;
  root->rev = rev;
  root->root_dir = root_dir;

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
  root->kind = transaction_root;
  root->txn = apr_pstrdup (root->pool, txn);

  return root;
}



/* Constructing nice error messages for roots.  */

/* Return a detailed `file not found' error message for PATH in ROOT.  */
static svn_error_t *
not_found (svn_fs_root_t *root, const char *path)
{
  svn_fs_t *fs = root->fs;

  if (root->kind == transaction_root)
    return
      svn_error_createf
      (SVN_ERR_FS_NOT_FOUND, 0, 0, fs->pool,
       "file not found: filesystem `%s', transaction `%s', path `%s'",
       fs->env_path, root->txn, path);
  else if (root->kind == revision_root)
    return
      svn_error_createf
      (SVN_ERR_FS_NOT_FOUND, 0, 0, fs->pool,
       "file not found: filesystem `%s', revision `%ld', path `%s'",
       fs->env_path, root->rev, path);
  else
    abort ();
}


/* Return a detailed `file already exists' message for PATH in ROOT.  */
static svn_error_t *
already_exists (svn_fs_root_t *root, const char *path)
{
  svn_fs_t *fs = root->fs;

  if (root->kind == transaction_root)
    return
      svn_error_createf
      (SVN_ERR_FS_ALREADY_EXISTS, 0, 0, fs->pool,
       "file already exists: filesystem `%s', transaction `%s', path `%s'",
       fs->env_path, root->txn, path);
  else if (root->kind == revision_root)
    return
      svn_error_createf
      (SVN_ERR_FS_ALREADY_EXISTS, 0, 0, fs->pool,
       "file already exists: filesystem `%s', revision `%ld', path `%s'",
       fs->env_path, root->rev, path);
  else
    abort ();
}


static svn_error_t *
not_txn (svn_fs_root_t *root)
{
  return svn_error_create
    (SVN_ERR_FS_NOT_TXN_ROOT, 0, NULL, root->pool,
     "root object must be a transaction root");
}



/* Getting dag nodes for roots.  */


/* Set *NODE_P to a freshly opened dag node referring to the root
   directory of ROOT, as part of TRAIL.  */
static svn_error_t *
root_node (dag_node_t **node_p,
           svn_fs_root_t *root,
           trail_t *trail)
{
  if (root->kind == revision_root)
    {
      /* It's a revision root, so we already have its root directory
         opened.  */
      *node_p = svn_fs__dag_dup (root->root_dir, trail);
      return SVN_NO_ERROR;
    }
  else if (root->kind == transaction_root)
    {
      /* It's a transaction root.  Open a fresh copy.  */
      return svn_fs__dag_txn_root (node_p, root->fs, root->txn, trail);
    }
  else
    abort ();
}


/* Set *NODE_P to a mutable root directory for ROOT, cloning if
   necessary, as part of TRAIL.  ROOT must be a transaction root.  Use
   ERROR_PATH in error messages.  */
static svn_error_t *
mutable_root_node (dag_node_t **node_p,
                   svn_fs_root_t *root,
                   const char *error_path,
                   trail_t *trail)
{
  if (root->kind == transaction_root)
    return svn_fs__dag_clone_root (node_p, root->fs, root->txn, trail);
  else
    /* If it's not a transaction root, we can't change its contents.  */
    return svn_fs__err_not_mutable (root->fs, root->rev, error_path);
}



/* Simple root operations.  */

void
svn_fs_close_root (svn_fs_root_t *root)
{
  svn_pool_destroy (root->pool);
}


svn_fs_t *
svn_fs_root_fs (svn_fs_root_t *root)
{
  return root->fs;
}


int
svn_fs_is_txn_root (svn_fs_root_t *root)
{
  return root->kind == transaction_root;
}


int
svn_fs_is_revision_root (svn_fs_root_t *root)
{
  return root->kind == revision_root;
}


int
svn_fs_is_id_root (svn_fs_root_t *root)
{
  return root->kind == id_root;
}


const char *
svn_fs_txn_root_name (svn_fs_root_t *root,
                      apr_pool_t *pool)
{
  if (root->kind == transaction_root)
    return apr_pstrdup (pool, root->txn);
  else
    return 0;
}


svn_revnum_t
svn_fs_revision_root_revision (svn_fs_root_t *root)
{
  if (root->kind == revision_root)
    return root->rev;
  else
    return -1;
}



/* Traversing directory paths.  */


/* A linked list representing the path from a node up to a root
   directory.  We use this for cloning, and for operations that need
   to deal with both a node and its parent directory.  For example, a
   `delete' operation needs to know that the node actually exists, but
   also needs to change the parent directory.  */
typedef struct parent_path_t
{
  
  /* A node along the path.  This could be the final node, one of its
     parents, or the root.  Every parent path ends with an element for
     the root directory.  */
  dag_node_t *node;

  /* The name NODE has in its parent directory.  This is zero for the
     root directory, which (obviously) has no name in its parent.  */
  char *entry;

  /* The parent of NODE, or zero if NODE is the root directory.  */
  struct parent_path_t *parent;
  
} parent_path_t;


/* Allocate a new parent_path_t node from POOL, referring to NODE,
   ENTRY, and PARENT.  */
static parent_path_t *
make_parent_path (dag_node_t *node,
                  char *entry,
                  parent_path_t *parent,
                  apr_pool_t *pool)
{
  parent_path_t *parent_path = apr_pcalloc (pool, sizeof (*parent_path));

  parent_path->node = node;
  parent_path->entry = entry;
  parent_path->parent = parent;

  return parent_path;
}


/* Return a null-terminated copy of the first component of PATH,
   allocated in POOL.  If path is empty, or consists entirely of
   slashes, return the empty string.

   If the component is followed by one or more slashes, we set *NEXT_P
   to point after the slashes.  If the component ends PATH, we set
   *NEXT_P to zero.  This means:
   - If *NEXT_P is zero, then the component ends the PATH, and there
     are no trailing slashes in the path.
   - If *NEXT_P points at PATH's terminating null character, then
     the component returned was the last, and PATH ends with one or more
     slash characters.
   - Otherwise, *NEXT_P points to the beginning of the next component
     of PATH.  You can pass this value to next_entry_name to extract
     the next component.  */

static char *
next_entry_name (const char **next_p,
                 const char *path,
                 apr_pool_t *pool)
{
  const char *end;

  /* Find the end of the current component.  */
  end = strchr (path, '/');

  if (! end)
    {
      /* The path contains only one component, with no trailing
         slashes.  */
      *next_p = 0;
      return apr_pstrdup (pool, path);
    }
  else
    {
      /* There's a slash after the first component.  Skip over an arbitrary
         number of slashes to find the next one.  */
      const char *next = end;
      while (*next == '/')
        next++;
      *next_p = next;
      return apr_pstrndup (pool, path, end - path);
    }
}


/* Flags for open_path.  */
typedef enum open_path_flags_t {

  /* The last component of the PATH need not exist.  (All parent
     directories must exist, as usual.)  If the last component doesn't
     exist, simply leave the `node' member of the bottom parent_path
     component zero.  */
  open_path_last_optional = 1,

} open_path_flags_t;


/* Open the node identified by PATH in ROOT, as part of TRAIL.  Set
   *PARENT_PATH_P to a path from the node up to ROOT, allocated in
   TRAIL->pool.  The resulting *PARENT_PATH_P value is guaranteed to
   contain at least one element, for the root directory.

   If FLAGS & open_path_last_optional is zero, return an error if the
   node PATH refers to does not exist.  If it is non-zero, require all
   the parent directories to exist as normal, but if the final path
   component doesn't exist, simply return a path whose bottom `node'
   member is zero.  This option is useful for callers that create new
   nodes --- we find the parent directory for them, and tell them
   whether the entry exists already.  

   If ROOT is an id root, then PATH is the unparsed form of an
   svn_fs_id_t; set (*PARENT_PATH)->node to the node identified by
   PATH, and (*PARENT_PATH)->parent to null.  In this case, FLAGS &
   open_path_last_optional must be zero or an assertion failure
   results.  */ 
static svn_error_t *
open_path (parent_path_t **parent_path_p,
           svn_fs_root_t *root,
           const char *path,
           int flags,
           trail_t *trail)
{
  svn_fs_t *fs = root->fs;
  apr_pool_t *pool = trail->pool;

  /* The directory we're currently looking at.  */
  dag_node_t *here;

  /* The path from HERE up to the root.  */
  parent_path_t *parent_path;
  
  /* The portion of PATH we haven't traversed yet.  */
  const char *rest = path;

  if (svn_fs_is_id_root (root))
    {
      dag_node_t *node;
      svn_fs_id_t *id = svn_fs_parse_id (path, strlen (path), pool);

      /* Unwise callers pay at the door. */
      assert ((flags & open_path_last_optional) == 0);

      /* This won't detect a string that's a node id but not a node
         revision id, but such an id would fail later on anyway. */
      if (! id)
        return svn_error_createf
          (SVN_ERR_FS_NOT_ID, 0, NULL, pool,
           "`%s' is not a node revision ID", path);

      SVN_ERR (svn_fs__dag_get_node (&node, root->fs, id, trail));
      parent_path = make_parent_path (node, 0, 0, pool);
    }
  else
    {
      SVN_ERR (root_node (&here, root, trail));
      parent_path = make_parent_path (here, 0, 0, pool);
      
      /* Whenever we are at the top of this loop:
         - HERE is our current directory,
         - REST is the path we're going to find in HERE, and 
         - PARENT_PATH includes HERE and all its parents.  */
      for (;;)
        {
          const char *next;
          char *entry;
          dag_node_t *child;
          
          /* Parse out the next entry from the path.  */
          entry = next_entry_name (&next, rest, pool);
          
          if (*entry == '\0')
            /* Given the behavior of next_entry_name, this happens when
               the path either starts or ends with a slash.  In either
               case, we stay put: the current directory stays the same,
               and we add nothing to the parent path.  */
            child = here;
          else
            {
              /* If we found a directory entry, follow it.  */
              svn_error_t *svn_err = svn_fs__dag_open (&child, here,
                                                       entry, trail);
              
              /* "file not found" requires special handling.  */
              if (svn_err && svn_err->apr_err == SVN_ERR_FS_NOT_FOUND)
                {
                  /* If this was the last path component, and the caller
                     said it was optional, then don't return an error;
                     just put a zero node pointer in the path.  */
                  if ((flags & open_path_last_optional)
                      && (! next || *next == '\0'))
                    {
                      parent_path = make_parent_path (0, entry,
                                                      parent_path, pool);
                      break;
                    }
                  else
                    /* Build a better error message than svn_fs__dag_open
                       can provide, giving the root and full path name.  */
                    return not_found (root, path);
                }
              
              /* Other errors we return normally.  */
              SVN_ERR (svn_err);
              
              parent_path = make_parent_path (child, entry, parent_path, pool);
            }
          
          /* Are we finished traversing the path?  */
          if (! next)
            break;
          
          /* The path isn't finished yet; we'd better be in a directory.  */
          if (! svn_fs__dag_is_directory (child))
            return svn_fs__err_not_directory (fs, path);
          
          rest = next;
          here = child;
        }
    }

  *parent_path_p = parent_path;
  return SVN_NO_ERROR;
}


/* Make the node referred to by PARENT_PATH mutable, if it isn't
   already, as part of TRAIL.  ROOT must be the root from which
   PARENT_PATH descends.  Clone any parent directories as needed.
   Adjust the dag nodes in PARENT_PATH to refer to the clones.  Use
   ERROR_PATH in error messages.  */
static svn_error_t *
make_path_mutable (svn_fs_root_t *root,
                   parent_path_t *parent_path,
                   const char *error_path,
                   trail_t *trail)
{
  dag_node_t *clone;

  {
    svn_boolean_t is_mutable;

    /* Is the node mutable already?  */
    SVN_ERR (svn_fs__dag_check_mutable (&is_mutable,
                                        parent_path->node, trail));
    if (is_mutable)
      return SVN_NO_ERROR;
  }

  /* Are we trying to clone the root, or somebody's child node?  */
  if (parent_path->parent)
    {
      /* We're trying to clone somebody's child. 
         Make sure our parent is mutable.  */
      SVN_ERR (make_path_mutable (root, parent_path->parent, error_path,
                                  trail));
      
      /* Now make this node mutable.  */
      SVN_ERR (svn_fs__dag_clone_child (&clone,
                                        parent_path->parent->node,
                                        parent_path->entry,
                                        trail));
    }
  else
    /* We're trying to clone the root directory.  */
    SVN_ERR (mutable_root_node (&clone, root, error_path, trail));

  /* Update the PARENT_PATH link to refer to the clone.  */
  parent_path->node = clone;
  return SVN_NO_ERROR;
}


/* Open the node identified by PATH in ROOT, as part of TRAIL.  Set
   *DAG_NODE_P to the node we find, allocated in TRAIL->pool.  Return
   an error if this node doesn't exist. */
static svn_error_t *
get_dag (dag_node_t **dag_node_p,
         svn_fs_root_t *root,
         const char *path,
         trail_t *trail)
{
  parent_path_t *parent_path;

  /* Call open_path with no flags, as we want this to return an error
     if the node for which we are searching doesn't exist. */
  SVN_ERR (open_path (&parent_path, root, path, 0, trail));
  *dag_node_p = parent_path->node;

  return SVN_NO_ERROR;
}


/* Open the mutable node identified by PATH in ROOT, as part of TRAIL.
   If the node isn't already mutable, it will be made so, with parent
   directories cloned as needed.  If the node doesn't exist, an error
   will be returned.  Otherwise, set *DAG_NODE_P to the resulting
   node, allocated in TRAIL->pool.  */
static svn_error_t *
get_dag_mutable (dag_node_t **dag_node_p,
                 svn_fs_root_t *root,
                 const char *path,
                 trail_t *trail)
{
  parent_path_t *parent_path;

  /* Call open_path with no flags, as we want this to return an error
     if the node for which we are searching doesn't exist. */
  SVN_ERR (open_path (&parent_path, root, path, 0, trail));
  SVN_ERR (make_path_mutable (root, parent_path, path, trail));
  *dag_node_p = parent_path->node;

  return SVN_NO_ERROR;
}



/* Generic node operations.  */


struct node_id_args {
  svn_fs_id_t **id_p;
  svn_fs_root_t *root;
  const char *path;
};


static svn_error_t *
txn_body_node_id (void *baton, trail_t *trail)
{
  struct node_id_args *args = baton;
  dag_node_t *node;

  SVN_ERR (get_dag (&node, args->root, args->path, trail));
  *args->id_p = svn_fs_copy_id (svn_fs__dag_get_id (node), trail->pool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_node_id (svn_fs_id_t **id_p,
                svn_fs_root_t *root,
                const char *path,
                apr_pool_t *pool)
{
  svn_fs_id_t *id;
  struct node_id_args args;

  args.id_p = &id;
  args.root = root;
  args.path = path;

  SVN_ERR (svn_fs__retry_txn (root->fs, txn_body_node_id, &args, pool));
  *id_p = id;
  return SVN_NO_ERROR;
}

struct is_kind_args {
  int (*query)(dag_node_t *node);
  svn_fs_root_t *root;
  const char *path;

  int result;   /* OUT parameter */
};


static svn_error_t *
txn_body_is_kind (void *baton, trail_t *trail)
{
  struct is_kind_args *args = baton;
  dag_node_t *node;

  SVN_ERR (get_dag (&node, args->root, args->path, trail));
  args->result = (*args->query) (node);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_is_dir (int *is_dir,
               svn_fs_root_t *root,
               const char *path,
               apr_pool_t *pool)
{
  struct is_kind_args args;

  args.query = svn_fs__dag_is_directory;
  args.root = root;
  args.path = path;

  SVN_ERR (svn_fs__retry_txn (root->fs, txn_body_is_kind, &args, pool));
  *is_dir = args.result;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_is_file (int *is_file,
                svn_fs_root_t *root,
                const char *path,
                apr_pool_t *pool)
{
  struct is_kind_args args;

  args.query = svn_fs__dag_is_file;
  args.root = root;
  args.path = path;

  SVN_ERR (svn_fs__retry_txn (root->fs, txn_body_is_kind, &args, pool));
  *is_file = args.result;

  return SVN_NO_ERROR;
}


/* Helper for svn_fs_is_different.  Return KIND of node at ROOT:PATH. */
static svn_error_t *
get_node_kind (enum svn_node_kind *kind,
               svn_fs_root_t *root,
               const char *path,
               apr_pool_t *pool)
{
  int is_dir, is_file;

  SVN_ERR (svn_fs_is_dir (&is_dir, root, path, pool));
  if (is_dir)
    *kind = svn_node_dir;
  else
    {
      SVN_ERR (svn_fs_is_file (&is_file, root, path, pool));
      if (is_file)
        *kind = svn_node_file;
      else
        return 
          svn_error_createf 
          (SVN_ERR_FS_GENERAL, 0, NULL, pool,
           "Node for `%s' seems to be neither file nor dir.", path);
    }
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_is_different (int *is_different,
                     svn_fs_root_t *root1,
                     const char *path1,
                     svn_fs_root_t *root2,
                     const char *path2,
                     apr_pool_t *pool)
{

  svn_fs_id_t *id1, *id2;
  enum svn_node_kind kind1, kind2;

  /* Easy check:  are they the same ID? */
  SVN_ERR (svn_fs_node_id (&id1, root1, path1, pool));
  SVN_ERR (svn_fs_node_id (&id2, root2, path2, pool));
  if (svn_fs_id_eq (id1, id2))
    {
      *is_different = FALSE;
      return SVN_NO_ERROR;
    }

  /* Easy check:  are they different node types? */
  SVN_ERR (get_node_kind (&kind1, root1, path1, pool));
  SVN_ERR (get_node_kind (&kind2, root2, path2, pool));
  
  if (kind1 != kind2)
    {
      *is_different = TRUE;
      return SVN_NO_ERROR;
    }

  /* If they're both files... */
  if (kind1 == svn_node_file)
    {
      /* ... then they can't be the same, because the IDs are different. */
      *is_different = TRUE;
      return SVN_NO_ERROR;
    }

  /* If they're both dirs... */
  if (kind1 == svn_node_dir)
    {
      /* ... compare dirent hashes.  (steal code from
         libsvn_wc/props.c?).  For each dirent match, check to see if
         the dirent node-rev-ids have any relationship to one
         another. */








    }

  return SVN_NO_ERROR;
}







struct node_prop_args
{
  svn_string_t **value_p;
  svn_fs_root_t *root;
  const char *path;
  svn_string_t *propname;
};


static svn_error_t *
txn_body_node_prop (void *baton,
                    trail_t *trail)
{
  struct node_prop_args *args = baton;
  dag_node_t *node;
  skel_t *proplist, *prop;

  SVN_ERR (get_dag (&node, args->root, args->path, trail));
  SVN_ERR (svn_fs__dag_get_proplist (&proplist, node, trail));
  
  /* Search the proplist for a property with the right name.  */
  for (prop = proplist->children; prop; prop = prop->next->next)
    {
      skel_t *name = prop;
      skel_t *value = prop->next;

      if (svn_fs__atom_matches_string (name, args->propname))
        {
          *(args->value_p) = svn_string_ncreate (value->data, value->len,
                                                 trail->pool);
          return SVN_NO_ERROR;
        }
    }

  *(args->value_p) = 0;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_node_prop (svn_string_t **value_p,
                  svn_fs_root_t *root,
                  const char *path,
                  svn_string_t *propname,
                  apr_pool_t *pool)
{
  struct node_prop_args args;
  svn_string_t *value;

  args.value_p  = &value;
  args.root     = root;
  args.path     = path;
  args.propname = propname;
  SVN_ERR (svn_fs__retry_txn (root->fs, txn_body_node_prop, &args, pool));

  *value_p = value;
  return SVN_NO_ERROR;
}


struct node_proplist_args {
  apr_hash_t **table_p;
  svn_fs_root_t *root;
  const char *path;
};


static svn_error_t *
txn_body_node_proplist (void *baton, trail_t *trail)
{
  struct node_proplist_args *args = baton;

  parent_path_t *parent_path;
  apr_hash_t *table;
  skel_t *proplist, *prop;

  SVN_ERR (open_path (&parent_path, args->root, args->path, 0, trail));
  SVN_ERR (svn_fs__dag_get_proplist (&proplist, parent_path->node, trail));

  /* Build a hash table from the property list.  */
  table = apr_hash_make (trail->pool);
  for (prop = proplist->children; prop; prop = prop->next->next)
    {
      skel_t *name = prop;
      skel_t *value = prop->next;

      apr_hash_set (table, name->data, name->len,
                    svn_string_ncreate (value->data, value->len,
                                        trail->pool));
    }

  *args->table_p = table;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_node_proplist (apr_hash_t **table_p,
                      svn_fs_root_t *root,
                      const char *path,
                      apr_pool_t *pool)
{
  apr_hash_t *table;
  struct node_proplist_args args;

  args.table_p = &table;
  args.root = root;
  args.path = path;

  SVN_ERR (svn_fs__retry_txn (root->fs, txn_body_node_proplist, &args, pool));

  *table_p = table;
  return SVN_NO_ERROR;
}


struct change_node_prop_args {
  svn_fs_root_t *root;
  const char *path;
  svn_string_t *name;
  svn_string_t *value;
};


static svn_error_t *
txn_body_change_node_prop (void *baton,
                           trail_t *trail)
{
  struct change_node_prop_args *args = baton;
  parent_path_t *parent_path;
  skel_t *proplist, *prop;
  skel_t *prev = NULL;

  SVN_ERR (open_path (&parent_path, args->root, args->path, 0, trail));
  SVN_ERR (make_path_mutable (args->root, parent_path, args->path, trail));
  SVN_ERR (svn_fs__dag_get_proplist (&proplist, parent_path->node, trail));
  
  /* Delete the skel, either replacing or adding the given property.  */
  for (prop = proplist->children; prop; prop = prop->next->next)
    {
      skel_t *name = prop;
      skel_t *value = prop->next;

      if (svn_fs__atom_matches_string (name, args->name))
        {
          /* We've found an existing entry for this property.  */
          if (! args->value)
            {
              /* If our new value for this is NULL, we'll remove the
                 property altogether by effectively routing our linked
                 list of properties around the current property
                 name/value pair. */
              if (prev)
                {
                  /* If this isn't the first pair in the list, this
                     can be done by setting the previous value's next
                     pointer to the name of the following property
                     pair, if one exists, or zero if we are removing
                     the last name/value pair currently in the
                     list. */
                  if (prop->next)
                    prev->next->next = prop->next->next;
                  else
                    prev->next->next = 0;
                }
              else
                {
                  /* If, however, this is the first item in the list,
                     we'll set the children pointer of the PROPLIST
                     skel to the following name/value pair, if one
                     exists, or zero if we're removing the only
                     property pair in the list. */
                  if (prop->next)
                    proplist->children = prop->next->next;
                  else
                    proplist->children = 0;
                }
            }
          else
            {
              value->data = args->value->data;
              value->len = args->value->len;
            }

          /* Regardless of what we changed, we're done editing the
             list now that we've acted on the property we found. */
          break;
        }
      /* Squirrel away a pointer to this property name/value pair, as
         we may need this in the next iteration of this loop. */
      prev = prop;
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

  SVN_ERR (svn_fs__dag_set_proplist (parent_path->node, proplist, trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_change_node_prop (svn_fs_root_t *root,
                         const char *path,
                         svn_string_t *name,
                         svn_string_t *value,
                         apr_pool_t *pool)
{
  struct change_node_prop_args args;

  if (! svn_fs_is_txn_root (root))
    return not_txn (root);

  args.root  = root;
  args.path  = path;
  args.name  = name;
  args.value = value;
  SVN_ERR (svn_fs__retry_txn (root->fs, txn_body_change_node_prop, &args,
                              pool));

  return SVN_NO_ERROR;
}



/* Merges and commits. */
 
struct get_root_args
{
  svn_fs_root_t *root;
  dag_node_t *node;
};


/* Set ARGS->node to the root node of ARGS->root.  */
static svn_error_t *
txn_body_get_root (void *baton, trail_t *trail)
{
  struct get_root_args *args = baton;
  SVN_ERR (get_dag (&(args->node), args->root, "", trail));
  return SVN_NO_ERROR;
}


/* Merge changes between ANCESTOR and SOURCE into TARGET (the node
 * represented by TARGET_ROOT and TARGET_PATH), as part of TRAIL.
 * ANCESTOR and TARGET must be distinct node revisions.
 *
 * SOURCE, TARGET, and ANCESTOR are generally directories; this
 * function recursively merges the directories' contents.  If any are
 * files, this function simply returns an error whenever SOURCE,
 * TARGET, and ANCESTOR are all distinct node revisions.
 *
 * If there are differences between ANCESTOR and SOURCE that conflict
 * with changes between ANCESTOR and TARGET, this function returns an
 * SVN_ERR_FS_CONFLICT error, and sets *CONFLICT_P to the name of the
 * conflicting node in TARGET, with TARGET_PATH prepended as a path.
 *
 * If there are no conflicting differences, *CONFLICT_P is set to
 * null. 
 *
 * Do any necessary temporary allocation in TRAIL->pool.
 */
static svn_error_t *
merge (const char **conflict_p,
       svn_fs_root_t *target_root,
       const char *target_path,
       dag_node_t *source,
       dag_node_t *ancestor,
       trail_t *trail)
{
  const svn_fs_id_t *source_id, *target_id, *ancestor_id;
  svn_fs_t *fs;
  dag_node_t *target;

  SVN_ERR (get_dag (&target, target_root, target_path, trail));

  /* Make sure everyone comes from the same filesystem. */
  fs = svn_fs__dag_get_fs (ancestor);
  if ((fs != svn_fs__dag_get_fs (source))
      || (fs != svn_fs__dag_get_fs (target)))
    {
      return svn_error_create
        (SVN_ERR_FS_CORRUPT, 0, NULL, trail->pool,
         "Bad merge -- ancestor, source, and target not all in same fs");
    }

  /* We have the same fs, now check it. */
  SVN_ERR (svn_fs__check_fs (fs));

  source_id   = svn_fs__dag_get_id (source);
  target_id   = svn_fs__dag_get_id (target);
  ancestor_id = svn_fs__dag_get_id (ancestor);

  /* It's improper to call this function with ancestor == target. */
  if (svn_fs_id_eq (ancestor_id, target_id))
    {
      svn_string_t *id_str = svn_fs_unparse_id (target_id, trail->pool);
      return svn_error_createf
        (SVN_ERR_FS_CORRUPT, 0, NULL, trail->pool,
         "Bad merge call -- target `%s' has id `%s', same as ancestor.",
         target_path, id_str->data);
    }

  *conflict_p = NULL;

  /* Base cases:
   * Either no change made in source, or same change as made in target.
   * Both mean nothing to merge here.
   */
  if (svn_fs_id_eq (ancestor_id, source_id)
      || (svn_fs_id_eq (source_id, target_id)))
    return SVN_NO_ERROR;

  /* Else proceed, knowing all three are distinct node revisions.
   *
   * How to merge from this point: 
   *
   * if (all 3 are directories)
   *   {
   *     for (each entry E in ancestor)
   *       {
   *         if (E exists in target and source)
   *           {
   *             if (source entry points to different id than E)
   *               {
   *                 if (target entry points to same id as ancestor E)
   *                   change target to point to same id as source entry;
   *                 else if (target entry id different from source entry id)
   *                   {
   *                     if (all 3 entries point to directories)
   *                       // We know they are different directories, so...
   *                       recursively merge;
   *                     else
   *                       conflict;
   *                   }
   *                 // Else target entry same as source entry; do nothing.
   *               }
   *           }
   *         else if (E exists in source but not target)
   *           add same entry to target, pointing to source entry's id;
   *         else if (E exists in target but not source)
   *           {
   *             if (E points the same node rev in target and ancestor)
   *                delete E from target;
   *             else // E points to different node revs in target & ancestor
   *               {
   *                 conflict;
   *               }
   *           }
   *         else
   *           {
   *             // E exists in neither target nor source, so it's a
   *             // double delete -- do nothing, since E is already
   *             // absent from target.  ### kff todo: but it would be
   *             // nice to handle the rename case better.  How?
   *           }
   *       }
   *
   *     // This next loop is over those entries in source that were
   *     // not already covered in the loop over ancestor above.
   *     for (each remaining entry E in source)
   *       {
   *         if (E does not exist in target)
   *           add it to target, based on source;
   *         else if (E exists in target but different id than E in source)
   *           conflict;
   *       }
   *         
   *     // All entries in ancestor and source are accounted for.
   *     // Remaining entries in target should be left as-is.
   *   }
   * else   // not all directories, and distinct node revisions
   *   conflict;
   *
   */

  if (svn_fs__dag_is_directory (source)
      && svn_fs__dag_is_directory (target)
      && svn_fs__dag_is_directory (ancestor))
    {
      apr_hash_t *s_entries, *t_entries, *a_entries;
      apr_hash_index_t *hi;
      
      SVN_ERR (svn_fs__dag_dir_entries_hash (&s_entries, source, trail));
      SVN_ERR (svn_fs__dag_dir_entries_hash (&t_entries, target, trail));
      SVN_ERR (svn_fs__dag_dir_entries_hash (&a_entries, ancestor, trail));

      /* for each entry E in a_entries... */
      for (hi = apr_hash_first (a_entries); hi; hi = apr_hash_next (hi))
        {
          svn_fs_dirent_t *s_entry, *t_entry, *a_entry;

          const void *key;
          void *val;
          apr_size_t klen;
          
          /* KEY will be the entry name in ancestor, VAL the dirent */
          apr_hash_this (hi, &key, &klen, &val);
          a_entry = val;
          
          /* E exists in target and source (as well as ancestor) */
          if ((s_entry = apr_hash_get (s_entries, key, klen))
              && (t_entry = apr_hash_get (t_entries, key, klen)))
            {
              /* If source entry has changed since ancestor entry... */
              if (! svn_fs_id_eq (a_entry->id, s_entry->id))
                {
                  /* ... and if target entry has not changed, then... */
                  if (svn_fs_id_eq (a_entry->id, t_entry->id))
                    {
                      /* ### kff todo: what about svn_fs__dag_link()
                         instead of svn_fs__dag_set_entry()?  The
                         cycle protection guaranteed by the former
                         would be guaranteed "for free" anyway, if
                         this function demanded that SOURCE and
                         ANCESTOR always be immutable nodes.  But we
                         don't demand that, although it happens to be
                         true of our only caller right now, since
                         merges are only done as part of commits. */

                      /* ... target takes source. */
                      dag_node_t *tnode;
                      SVN_ERR (get_dag_mutable (&tnode, target_root,
                                                target_path, trail));
                      SVN_ERR (svn_fs__dag_set_entry
                               (tnode, t_entry->name, s_entry->id, trail));
                    }
                  /* or if target entry is different from both, then... */
                  else if (! svn_fs_id_is_ancestor (s_entry->id, t_entry->id))
                    {
                      dag_node_t *s_ent_node, *t_ent_node, *a_ent_node;
                      
                      SVN_ERR (svn_fs__dag_get_node (&s_ent_node, fs,
                                                     s_entry->id, trail));
                      SVN_ERR (svn_fs__dag_get_node (&t_ent_node, fs,
                                                     t_entry->id, trail));
                      SVN_ERR (svn_fs__dag_get_node (&a_ent_node, fs,
                                                     a_entry->id, trail));
                      
                      /* ... and all three entries are dirs, then... */
                      if (svn_fs__dag_is_directory (s_ent_node)
                          && svn_fs__dag_is_directory (t_ent_node)
                          && svn_fs__dag_is_directory (a_ent_node))
                        {
                          /* ... just recurse. */

                          /* ### kff todo: would be nice to use an
                             abstracted, char * based path function
                             here. */
                          const char *new_tpath
                            = apr_psprintf (trail->pool, "%s/%s",
                                            target_path, t_entry->name);

                          SVN_ERR (merge (conflict_p,
                                          target_root,
                                          new_tpath,
                                          s_ent_node,
                                          a_ent_node,
                                          trail));
                          /* ### kff todo:
                             As Jim mentioned on the phone, there's a
                             history-tracking problem here.  After we
                             do the merge into target, target has
                             absorbed the history between ancestor and
                             source, but there is no record of this
                             absorbtion having happened.  For example,
                             when generating a log message for target,
                             you'd want to include all the changes
                             between ancestor and source.

                             In the general case, this is the same
                             genetic merge problem that we'll have to
                             deal with when we do full ancestry
                             tracking.  (Hello, changesets.)

                             But the most common particular case is
                             that target is an immediate descendant of
                             ancestor, and source is also a descendant
                             of ancestor.  That is:

                                svn_fs_id_distance (ancestor, target) == 1
                                svn_fs_id_distance (ancestor, source) >= 1
                             
                             In such cases, we can record the
                             successful merge for free, by making
                             t_entry->name point to a node id that is
                             a successor of s_entry->id.  This is safe
                             because

                                - all the history from time-zero to
                                  ancestor is preserved

                                - all the history from ancestor to
                                  source is now preserved

                                - the single historical step from
                                  ancestor to target is preserved as a
                                  single step from source to target,
                                  which is an accurate reflection of
                                  the post-merge situation anyway.

                             Note that this trick should be used after
                             *any* call to merge(), not just the
                             recursive call above.  That means the
                             transaction root should be re-ID'd after
                             the merge.  (And come to think of it,
                             we're already resetting the transaction's
                             base root to source, so setting the txn's
                             mutable root to a successor of source has
                             a certain inevitability, n'est ce pas?) */
                        }
                      else  /* otherwise, they're not all dirs, so... */
                        {
                          /*... flag a conflict. */
                          
                          /* ### kff todo: abstract path creation func
                             here? */
                          *conflict_p = apr_psprintf (trail->pool,
                                                      "%s/%s",
                                                      target_path,
                                                      a_entry->name);
                          return svn_error_createf
                            (SVN_ERR_FS_CONFLICT, 0, NULL, trail->pool,
                             "conflict at \"%s\"", *conflict_p);
                        }
                    }
                  /* Else target entry has changed since ancestor
                     entry, but it changed to the same thing as source
                     entry, so we need do nothing. */
                }
            }
          /* E exists in source but not target */
          else if ((s_entry = apr_hash_get (s_entries, key, klen))
                   && (! apr_hash_get (t_entries, key, klen)))
            {
              /* If E changed between ancestor and source, then that
                 conflicts with E's having been removed from
                 target. */
              if (! svn_fs_id_eq (a_entry->id, s_entry->id))
                {
                  /* ### kff todo: abstract path creation func here? */
                  *conflict_p = apr_psprintf (trail->pool, "%s/%s",
                                              target_path, a_entry->name);
                  
                  return svn_error_createf
                    (SVN_ERR_FS_CONFLICT, 0, NULL, trail->pool,
                     "conflict at \"%s\"", *conflict_p);
                }

              /* Else if E did not change between ancestor and source,
                 then E's removal from target holds, so do nothing. */
            }
          /* E exists in target but not source */
          else if ((t_entry = apr_hash_get (t_entries, key, klen))
                   && (! apr_hash_get (s_entries, key, klen)))
            {
              if (svn_fs_id_eq (t_entry->id, a_entry->id))
                {
                  /* If E is same in target as ancestor, then it has
                     not changed, and the deletion in source should be
                     honored. */
                  dag_node_t *tnode;
                  SVN_ERR (get_dag_mutable (&tnode, target_root,
                                            target_path, trail));
                  SVN_ERR (svn_fs__dag_delete_tree (tnode, t_entry->name,
                                                    trail));

                  /* Seems cleanest to remove it from the target
                     entries hash now, even though no code would break
                     if we didn't.  It feels more robust if t_entries
                     reflects the state of the target at all times. */
                  apr_hash_set (t_entries, key, klen, NULL);
                }
              else
                {
                  /* Otherwise, E is different in target than in
                     ancestor, so it's a conflict with the deletion of
                     E in source. */

                  /* ### kff todo: abstract path creation func here? */
                  *conflict_p = apr_psprintf (trail->pool, "%s/%s",
                                              target_path, t_entry->name);

                  return svn_error_createf
                    (SVN_ERR_FS_CONFLICT, 0, NULL, trail->pool,
                     "conflict at \"%s\"", *conflict_p);
                }
            }
          /* E exists in neither target nor source */
          else
            {
              /* It's a double delete, so do nothing.
                 ### kff todo: what about the rename case? */
            }
          
          /* We've taken care of any possible implications E could
             have.  Remove it from source_entries, so it's easy
             later to loop over all the source entries that didn't
             exist in ancestor_entries. */
          apr_hash_set (s_entries, key, klen, NULL);
        }
      
      /* For each entry E in source but not in ancestor */
      for (hi = apr_hash_first (s_entries); hi; hi = apr_hash_next (hi))
        {
          svn_fs_dirent_t *s_entry, *t_entry;
          const void *key;
          void *val;
          apr_size_t klen;
          
          apr_hash_this (hi, &key, &klen, &val);
          s_entry = val;
          t_entry = apr_hash_get (t_entries, key, klen);

          /* E does not exist in target */
          if (! t_entry)
            {
              /* target takes source */
              dag_node_t *tnode;
              SVN_ERR (get_dag_mutable (&tnode, target_root,
                                        target_path, trail));
              SVN_ERR (svn_fs__dag_set_entry
                       (tnode, s_entry->name, s_entry->id, trail));
            }
          /* E exists in target but is different from E in source */
          else if (! svn_fs_id_is_ancestor (s_entry->id, t_entry->id))
            {
              /* ### kff todo: abstract path creation func here? */
              *conflict_p = apr_psprintf (trail->pool, "%s/%s",
                                          target_path, t_entry->name);
              
              return svn_error_createf
                (SVN_ERR_FS_CONFLICT, 0, NULL, trail->pool,
                 "conflict at \"%s\"", *conflict_p);

              /* The remaining case would be: E exists in target and
               * is same as in source.  This implies a twin add, so
               * target just stays as is.
               */
            }
        }
      
      /* All entries in ancestor and source have been accounted for.
       *
       * Any entry E in target that does not exist in ancestor or
       * source is a non-conflicting add, so we don't need to do
       * anything about it. 
       */
    }
  else  /* they are distinct node revisions, and not all directories */
    {
      *conflict_p = target_path;
      return svn_error_createf
        (SVN_ERR_FS_CONFLICT, 0, NULL, trail->pool,
         "conflict at \"%s\"", target_path);
    }

  return SVN_NO_ERROR;
}


struct merge_args
{
  /* The ancestor for the merge.  If this is null, then TXN's base is
     used as the ancestor for the merge. */
  dag_node_t *ancestor_node;

  /* This is the SOURCE node for the merge.  It may not be null. */
  dag_node_t *source_node;

  /* This is the TARGET of the merge.  It may not be null.  If
     ancestor_node above is null, then this txn's base is used as the
     ancestor for the merge. */
  svn_fs_txn_t *txn;

  /* If a conflict results, this is set to the path in the txn that
     conflicted, allocated in the pool of the trail in which the
     conflict was encountered. */
  const char *conflict;
};


/* Merge changes between ARGS->txn's base and ARGS->node into
   ARGS->txn's root.  If the merge is successful, ARGS->txn's base
   will become ARGS->node. */
static svn_error_t *
txn_body_merge (void *baton, trail_t *trail)
{
  struct merge_args *args = baton;
  dag_node_t *source_node, *txn_root_node, *ancestor_node;
  const svn_fs_id_t *source_id;
  svn_fs_t *fs = svn_fs__txn_fs (args->txn);
  const char *txn_name = svn_fs__txn_id (args->txn);

  source_node = args->source_node;
  ancestor_node = args->ancestor_node;
  source_id = svn_fs__dag_get_id (source_node);
  
  SVN_ERR (svn_fs__dag_txn_root (&txn_root_node, fs, txn_name, trail));

  if (ancestor_node == NULL)
    {
      SVN_ERR (svn_fs__dag_txn_base_root (&ancestor_node, fs,
                                          txn_name, trail));
    }
  
  if (svn_fs_id_eq (svn_fs__dag_get_id (ancestor_node),
                    svn_fs__dag_get_id (txn_root_node)))
    {
      /* If no changes have been made in TXN since its current base,
         then it can't conflict with any changes since that base.  So
         we just set *both* its base and root to source, making TXN
         in effect a repeat of source. */
      
      /* ### kff todo: this would, of course, be a mighty silly thing
         for the caller to do, and we might want to consider whether
         this response is really appropriate. */
      
      SVN_ERR (svn_fs__set_txn_base (fs, txn_name, source_id, trail));
      SVN_ERR (svn_fs__set_txn_root (fs, txn_name, source_id, trail));
    }
  else
    {
      svn_fs_root_t *target_root;
      SVN_ERR (svn_fs_txn_root (&target_root, args->txn, trail->pool));

      SVN_ERR (merge (&(args->conflict),
                      target_root,
                      "",
                      source_node,
                      ancestor_node,
                      trail));
      
      /* ### kff todo:
         See the comment immediately after the recursive call in
         merge().  The same thing applies here. */

      SVN_ERR (svn_fs__set_txn_base (fs, txn_name, source_id, trail));
    }
  
  return SVN_NO_ERROR;
}


struct commit_args
{
  svn_fs_txn_t *txn;
  svn_revnum_t new_rev;
};


/* Commit ARGS->txn, setting ARGS->new_rev to the resulting new
 * revision, if ARGS->txn is up-to-date w.r.t. the repository.
 *
 * Up-to-date means that ARGS->txn's base root is the same as the root
 * of the youngest revision.  If ARGS->txn is not up-to-date, the
 * error SVN_ERR_TXN_OUT_OF_DATE is returned, and the commit fails: no
 * new revision is created, and ARGS->new_rev is not touched.
 *
 * If the commit succeeds, ARGS->txn is destroyed.
 */
static svn_error_t *
txn_body_commit (void *baton, trail_t *trail)
{
  struct commit_args *args = baton;

  svn_fs_txn_t *txn = args->txn;
  svn_fs_t *fs = svn_fs__txn_fs (txn);
  const char *txn_name = svn_fs__txn_id (txn);

  svn_revnum_t youngest_rev;
  svn_fs_id_t *y_rev_root_id;
  dag_node_t *txn_base_root_node;

  /* Getting the youngest revision locks the revisions table until
     this trail is done. */
  SVN_ERR (svn_fs__youngest_rev (&youngest_rev, fs, trail));

  /* If the root of the youngest revision is the same as txn's base,
     then no further merging is necessary and we can commit. */
  SVN_ERR (svn_fs__rev_get_root (&y_rev_root_id, fs, youngest_rev, trail));
  SVN_ERR (svn_fs__dag_txn_base_root (&txn_base_root_node, fs, txn_name,
                                      trail));
  /* ### kff todo: it seems weird to grab the ID for one, and the node
     for the other.  We can certainly do the comparison we need, but
     it would be nice to grab the same type of information from the
     start, instead of having to transform one of them. */ 
  if (! svn_fs_id_eq (y_rev_root_id, svn_fs__dag_get_id (txn_base_root_node)))
    {
      svn_string_t *id_str = svn_fs_unparse_id (y_rev_root_id, trail->pool);
      return svn_error_createf
        (SVN_ERR_TXN_OUT_OF_DATE, 0, NULL, trail->pool,
         "txn `%s' out of date w.r.t. revision `%s'", txn_name, id_str->data);
    }
  
  /* Else, commit the txn. */
  SVN_ERR (svn_fs__dag_commit_txn (&(args->new_rev), fs, txn_name, trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_commit_txn (const char **conflict_p,
                   svn_revnum_t *new_rev, 
                   svn_fs_txn_t *txn)
{
  /* How do commits work in Subversion?
   *
   * When you're ready to commit, here's what you have:
   *
   *    1. A transaction, with a mutable tree hanging off it.
   *    2. A base revision, against which TXN_TREE was made.
   *    3. A latest revision, which may be newer than the base rev.
   *
   * The problem is that if latest != base, then one can't simply
   * attach the txn root as the root of the new revision, because that
   * would lose all the changes between base and latest.  It is also
   * not acceptable to insist that base == latest; in a busy
   * repository, commits happen too fast to insist that everyone keep
   * their entire tree up-to-date at all times.  Non-overlapping
   * changes should not interfere with each other.
   *
   * The solution is to merge the changes between base and latest into
   * the txn tree [see the function merge()].  The txn tree is the
   * only one of the three trees that is mutable, so it has to be the
   * one to adjust.
   *
   * You might have to adjust it more than once, if a new latest
   * revision gets committed while you were merging in the previous
   * one.  For example:
   *
   *    1. Jane starts txn T, based at revision 6.
   *    2. Someone commits (or already committed) revision 7.
   *    3. Jane's starts merging the changes between 6 and 7 into T.
   *    4. Meanwhile, someone commits revision 8.
   *    5. Jane finishes the 6-->7 merge.  T could now be committed
   *       against a latest revision of 7, if only that were still the
   *       latest.  Unfortunately, 8 is now the latest, so... 
   *    6. Jane starts merging the changes between 7 and 8 into T.
   *    7. Meanwhile, no one commits any new revisions.  Whew.
   *    8. Jane commits T, creating revision 9, whose tree is exactly
   *       T's tree, except immutable now.
   *
   * Lather, rinse, repeat.
   */

  svn_error_t *err;

  while (1729)
    {
      struct get_root_args get_root_args;
      struct merge_args merge_args;
      struct commit_args commit_args;
      svn_revnum_t youngish_rev;
      svn_fs_root_t *youngish_root;
      dag_node_t *youngish_root_node;
      svn_fs_t *fs = svn_fs__txn_fs (txn);
      apr_pool_t *pool = svn_fs__txn_pool (txn);

      /* Get the *current* youngest revision, in one short-lived
         Berkeley transaction.  (We don't want the revisions table
         locked while we do the main merge.)  We call it "youngish"
         because new revisions might get committed after we've
         obtained it. */

      SVN_ERR (svn_fs_youngest_rev (&youngish_rev, fs, pool));
      SVN_ERR (svn_fs_revision_root (&youngish_root, fs, youngish_rev, pool));

      /* Get the dag node for the youngest revision, also in one
         Berkeley transaction.  Later we'll use it as the SOURCE
         argument to a merge, and if the merge succeeds, this youngest
         root node will become the new base root for the svn txn that
         was the target of the merge (but note that the youngest rev
         may have changed by then -- that's why we're careful to get
         this root in its own bdb txn here). */
      get_root_args.root = youngish_root;
      SVN_ERR (svn_fs__retry_txn (fs, txn_body_get_root,
                                  &get_root_args, pool));
      youngish_root_node = get_root_args.node;
      
      /* Try to merge.  If the merge succeeds, the base root node of
         TARGET's txn will become the same as youngish_root_node, so
         any future merges will only be between that node and whatever
         the root node of the youngest rev is by then. */ 
      merge_args.ancestor_node = NULL;
      merge_args.source_node = youngish_root_node;
      merge_args.txn = txn;
      err = svn_fs__retry_txn (fs, txn_body_merge, &merge_args, pool);
      if (err)
        {
          if ((err->apr_err == SVN_ERR_FS_CONFLICT) && conflict_p)
            *conflict_p = merge_args.conflict;
          return err;
        }
      
      /* Try to commit. */
      commit_args.txn = txn;
      err = svn_fs__retry_txn (fs, txn_body_commit, &commit_args, pool);
      if (err && (err->apr_err == SVN_ERR_TXN_OUT_OF_DATE))
        {
          /* Did someone else finish committing a new revision while we
             were in mid-merge or mid-commit?  If so, we'll need to
             loop again to merge the new changes in, then try to
             commit again.  Or if that's not what happened, then just
             return the error. */

          svn_revnum_t youngest_rev;
          SVN_ERR (svn_fs_youngest_rev (&youngest_rev, fs, pool));
          if (youngest_rev == youngish_rev)
            return err;
        }
      else
        {
          *new_rev = commit_args.new_rev;
          break;
        }
    }

  return SVN_NO_ERROR;
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
  dag_node_t *source, *ancestor;
  struct get_root_args get_root_args;
  struct merge_args merge_args;
  svn_fs_txn_t *txn;
  svn_error_t *err;
  svn_fs_t *fs;

  if (! svn_fs_is_txn_root (target_root))
    return not_txn (target_root);

  /* Paranoia. */
  fs = svn_fs_root_fs (ancestor_root);
  if ((svn_fs_root_fs (source_root) != fs)
      || (svn_fs_root_fs (target_root) != fs))
    {
      return svn_error_create
        (SVN_ERR_FS_CORRUPT, 0, NULL, pool,
         "Bad merge -- ancestor, source, and target not all in same fs");
    }

  /* ### kff todo: is there any compelling reason to get the nodes in
     one db transaction?  Right now we don't; txn_body_get_root() gets
     one node at a time.  This will probably need to change:

     Jim Blandy <jimb@zwingli.cygnus.com> writes:
     > svn_fs_merge needs to be a single transaction, to protect it against
     > people deleting parents of nodes it's working on, etc.
  */

  /* Get the ancestor node. */
  get_root_args.root = ancestor_root;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_get_root, &get_root_args, pool));
  ancestor = get_root_args.node;

  /* Get the source node. */
  get_root_args.root = source_root;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_get_root, &get_root_args, pool));
  source = get_root_args.node;
  
  /* Open a txn for the txn root into which we're merging. */
  SVN_ERR (svn_fs_open_txn (&txn, fs,
                            svn_fs_txn_root_name (target_root, pool),
                            pool));

  /* Merge changes between ANCESTOR and SOURCE into TXN. */
  merge_args.source_node = source;
  merge_args.ancestor_node = ancestor;
  merge_args.txn = txn;
  err = svn_fs__retry_txn (fs, txn_body_merge, &merge_args, pool);
  if (err)
    {
      if ((err->apr_err == SVN_ERR_FS_CONFLICT) && conflict_p)
        *conflict_p = merge_args.conflict;
      return err;
    }

  return SVN_NO_ERROR;
}



/* Directories.  */


struct dir_entries_args
{
  apr_hash_t **table_p;
  svn_fs_root_t *root;
  const char *path;
};


static svn_error_t *
txn_body_dir_entries (void *baton,
                      trail_t *trail)
{
  struct dir_entries_args *args = baton;
  parent_path_t *parent_path;
  apr_hash_t *table;

  SVN_ERR (open_path (&parent_path, args->root, args->path, 0, trail));
  SVN_ERR (svn_fs__dag_dir_entries_hash (&table, parent_path->node, trail));

  *args->table_p = table;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_dir_entries (apr_hash_t **table_p,
                    svn_fs_root_t *root,
                    const char *path,
                    apr_pool_t *pool)
{
  struct dir_entries_args args;
  apr_hash_t *table;

  args.table_p = &table;
  args.root    = root;
  args.path    = path;
  SVN_ERR (svn_fs__retry_txn (root->fs, txn_body_dir_entries, &args, pool));
  
  *table_p = table;
  return SVN_NO_ERROR;
}



struct make_dir_args
{
  svn_fs_root_t *root;
  const char *path;
};


static svn_error_t *
txn_body_make_dir (void *baton,
                   trail_t *trail)
{
  struct make_dir_args *args = baton;
  svn_fs_root_t *root = args->root;
  const char *path = args->path;
  parent_path_t *parent_path;
  dag_node_t *sub_dir;
  
  SVN_ERR (open_path (&parent_path, root, path, open_path_last_optional,
                      trail));

  /* If there's already a sub-directory by that name, complain.  This
     also catches the case of trying to make a subdirectory named `/'.  */
  if (parent_path->node)
    return already_exists (root, path);

  /* Create the subdirectory.  */
  SVN_ERR (make_path_mutable (root, parent_path->parent, path, trail));
  SVN_ERR (svn_fs__dag_make_dir (&sub_dir,
                                 parent_path->parent->node, 
                                 parent_path->entry,
                                 trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_make_dir (svn_fs_root_t *root,
                 const char *path,
                 apr_pool_t *pool)
{
  struct make_dir_args args;

  if (! svn_fs_is_txn_root (root))
    return not_txn (root);

  args.root = root;
  args.path = path;
  return svn_fs__retry_txn (root->fs, txn_body_make_dir, &args, pool);
}
                              

struct delete_args
{
  svn_fs_root_t *root;
  const char *path;
  svn_boolean_t delete_tree;
};


static svn_error_t *
txn_body_delete (void *baton,
                 trail_t *trail)
{
  struct delete_args *args = baton;
  svn_fs_root_t *root = args->root;
  const char *path = args->path;
  parent_path_t *parent_path;

  SVN_ERR (open_path (&parent_path, root, path, 0, trail));

  if (! svn_fs_is_txn_root (root))
    return not_txn (root);

  /* We can't remove the root of the filesystem.  */
  if (! parent_path->parent)
    return svn_error_create (SVN_ERR_FS_ROOT_DIR, 0, NULL, trail->pool,
                             "the root directory cannot be deleted");

  /* Make the parent directory mutable.  */
  SVN_ERR (make_path_mutable (root, parent_path->parent, path, trail));

  if (args->delete_tree)
    {
      SVN_ERR (svn_fs__dag_delete_tree (parent_path->parent->node,
                                        parent_path->entry,
                                        trail));
    }
  else
    {
      SVN_ERR (svn_fs__dag_delete (parent_path->parent->node,
                                   parent_path->entry,
                                   trail));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_delete (svn_fs_root_t *root,
               const char *path,
               apr_pool_t *pool)
{
  struct delete_args args;

  args.root        = root;
  args.path        = path;
  args.delete_tree = FALSE;
  return svn_fs__retry_txn (root->fs, txn_body_delete, &args, pool);
}


svn_error_t *
svn_fs_delete_tree (svn_fs_root_t *root,
                    const char *path,
                    apr_pool_t *pool)
{
  struct delete_args args;

  args.root        = root;
  args.path        = path;
  args.delete_tree = TRUE;
  return svn_fs__retry_txn (root->fs, txn_body_delete, &args, pool);
}


svn_error_t *
svn_fs_rename (svn_fs_root_t *root,
               const char *from,
               const char *to,
               apr_pool_t *pool)
{
  abort ();
}


struct copy_args
{
  svn_fs_root_t *from_root;
  const char *from_path;
  svn_fs_root_t *to_root;
  const char *to_path;
};


static svn_error_t *
txn_body_copy (void *baton,
               trail_t *trail)
{
  struct copy_args *args = baton;
  svn_fs_root_t *from_root = args->from_root;
  const char *from_path = args->from_path;
  svn_fs_root_t *to_root = args->to_root;
  const char *to_path = args->to_path;
  parent_path_t *from_parent_path;
  parent_path_t *to_parent_path;

  if (! svn_fs_is_revision_root (from_root))
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, 
                             NULL, trail->pool,
                             "copy from mutable tree not currently supported");

  /* Build up the parent path from FROM_PATH, making sure that it
     exists in FROM_ROOT */
  SVN_ERR (open_path (&from_parent_path, from_root, from_path, 
                      0, trail));

  /* Build up the parent path from TO_PATH in TO_ROOT.  If the last
     component does not exist, it's not that big a deal.  We'll just
     make one there. */
  SVN_ERR (open_path (&to_parent_path, to_root, to_path, 
                      open_path_last_optional, trail));

  if (svn_fs_is_revision_root (from_root))
    {
      /* Copying a file or directory from a revision root.  This is a
         trivial referencing operation. */

      /* Make sure that this node's parents are mutable.  */
      SVN_ERR (make_path_mutable (to_root, to_parent_path->parent, 
                                  to_path, trail));

      /* Now, set the entry with this name in the parent to the id of
         the node we are copying.  If the destination parent has no
         entry of this name, we'll just create one. */
      SVN_ERR (svn_fs__dag_set_entry 
               (to_parent_path->parent->node,
                to_parent_path->entry,
                (svn_fs_id_t *)svn_fs__dag_get_id (from_parent_path->node),
                trail));
    }
  else
    {
      /* Copying from transaction roots not currently available.
         cmpilato todo someday: make this not so. :-)  Note that when
         copying from mutable trees, you have to make sure that you
         aren't creating a cyclic graph filesystem, and a simple
         referencing operation won't cut it.   Currently, we should
         not be able to reach this clause.  This is okay, however,
         since the interface reports that this only works from
         immutable trees, but JimB has stated that this requirement
         need not be necessary in the future. */
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_copy (svn_fs_root_t *from_root,
             const char *from_path,
             svn_fs_root_t *to_root,
             const char *to_path,
             apr_pool_t *pool)
{
  struct copy_args args;

  if (! svn_fs_is_txn_root (to_root))
    return not_txn (to_root);

  args.from_root = from_root;
  args.from_path = from_path;
  args.to_root   = to_root;
  args.to_path   = to_path;
  return svn_fs__retry_txn (to_root->fs, txn_body_copy, &args, pool);
}



/* Files.  */


struct make_file_args
{
  svn_fs_root_t *root;
  const char *path;
};


static svn_error_t *
txn_body_make_file (void *baton,
                    trail_t *trail)
{
  struct make_file_args *args = baton;
  svn_fs_root_t *root = args->root;
  const char *path = args->path;
  parent_path_t *parent_path;
  dag_node_t *child;
  
  SVN_ERR (open_path (&parent_path, root, path, open_path_last_optional,
                      trail));

  /* If there's already a file by that name, complain.
     This also catches the case of trying to make a file named `/'.  */
  if (parent_path->node)
    return already_exists (root, path);

  /* Create the file.  */
  SVN_ERR (make_path_mutable (root, parent_path->parent, path, trail));
  SVN_ERR (svn_fs__dag_make_file (&child,
                                  parent_path->parent->node, 
                                  parent_path->entry,
                                  trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_make_file (svn_fs_root_t *root,
                  const char *path,
                  apr_pool_t *pool)
{
  struct make_file_args args;

  args.root = root;
  args.path = path;
  return svn_fs__retry_txn (root->fs, txn_body_make_file, &args, pool);
}



struct file_length_args
{
  svn_fs_root_t *root;
  const char *path;

  apr_off_t length;     /* OUT parameter */
};

static svn_error_t *
txn_body_file_length (void *baton,
                      trail_t *trail)
{
  struct file_length_args *args = baton;
  dag_node_t *file;
  
  /* First create a dag_node_t from the root/path pair. */
  SVN_ERR (get_dag (&file, args->root, args->path, trail));

  /* Now fetch its length */
  return svn_fs__dag_file_length(&args->length, file, trail);
}

svn_error_t *
svn_fs_file_length (apr_off_t *length_p,
                    svn_fs_root_t *root,
                    const char *path,
                    apr_pool_t *pool)
{
  struct file_length_args args;

  args.root = root;
  args.path = path;
  SVN_ERR (svn_fs__retry_txn (root->fs, txn_body_file_length, &args, pool));

  *length_p = args.length;
  return SVN_NO_ERROR;
}


/* --- Machinery for svn_fs_file_contents() ---  */


/* Local baton type for txn_body_get_file_contents. */
typedef struct file_contents_baton_t
{
  /* The file we want to read. */
  svn_fs_root_t *root;
  const char *path;

  /* The dag_node that will be made from the above. */
  dag_node_t *node;
    
  /* The readable file stream that will be made from the
     dag_node. (And returned to the caller.) */
  svn_stream_t *file_stream;

} file_contents_baton_t;


/* Main body of svn_fs_file_contents;  converts a root/path pair into
   a readable file stream (in the context of a db txn). */
static svn_error_t *
txn_body_get_file_contents (void *baton, trail_t *trail)
{
  file_contents_baton_t *fb = (file_contents_baton_t *) baton;

  /* First create a dag_node_t from the root/path pair. */
  SVN_ERR (get_dag (&(fb->node), fb->root, fb->path, trail));
  
  /* Then create a readable stream from the dag_node_t. */
  SVN_ERR (svn_fs__dag_get_contents (&(fb->file_stream),
                                     fb->node,
                                     trail));
  return SVN_NO_ERROR;
}     



svn_error_t *
svn_fs_file_contents (svn_stream_t **contents,
                      svn_fs_root_t *root,
                      const char *path,
                      apr_pool_t *pool)
{
  file_contents_baton_t *fb = apr_pcalloc (pool, sizeof(*fb));
  fb->root = root;
  fb->path = path;

  /* Create the readable stream in the context of a db txn.  */
  SVN_ERR (svn_fs__retry_txn (svn_fs_root_fs (root),
                              txn_body_get_file_contents, fb, pool));
  
  *contents = fb->file_stream;
  return SVN_NO_ERROR;
}

/* --- End machinery for svn_fs_file_contents() ---  */



/* --- Machinery for svn_fs_apply_textdelta() ---  */


/* Local baton type for all the helper functions below. */
typedef struct txdelta_baton_t
{
  /* This is the custom-built window consumer given to us by the delta
     library;  it uniquely knows how to read data from our designated
     "source" stream, interpret the window, and write data to our
     designated "target" stream (in this case, our repos file.) */
  svn_txdelta_window_handler_t interpreter;
  void *interpreter_baton;

  /* This string holds the entire "growing" target in memory.  Yes,
     this is bad!  Someday we'll tell berkeley db to operate directly
     on substrings of table values. */
  svn_string_t *target_string;

  /* Information about the file into which we will eventually dump
     target_string:  */

  /* The original file info */
  svn_fs_root_t *root;
  const char *path;
  
  /* Derived from the file info */
  dag_node_t *node;
  svn_stream_t *source_stream;

  /* Pool used by db txns */
  apr_pool_t *pool;

} txdelta_baton_t;



/* Helper function of generic type `svn_write_fn_t'.  Implements a
   writable stream which appends to an svn_string_t. */
static svn_error_t *
write_to_string (void *baton, const char *data, apr_size_t *len)
{
  txdelta_baton_t *tb = (txdelta_baton_t *) baton;
  
  svn_string_appendbytes (tb->target_string, data, *len);

  return SVN_NO_ERROR;
}


/* Helper function: takes a txdelta_baton_t and converts its file
   information into a readable generic stream. */
static svn_error_t *
txn_body_get_mutable_source_stream (void *baton, trail_t *trail)
{
  txdelta_baton_t *tb = (txdelta_baton_t *) baton;
  parent_path_t *parent_path;

  /* Call open_path with no flags, as we want this to return an error
     if the node for which we are searching doesn't exist. */
  SVN_ERR (open_path (&parent_path, tb->root, tb->path, 0, trail));

  /* Now, make sure this path is mutable. */
  SVN_ERR (make_path_mutable (tb->root, parent_path, tb->path, trail));
  tb->node = parent_path->node;

  /* If we get here without error, then the path to the file must
     exist.  Now convert the dag_node into a generic readable
     stream. */
  SVN_ERR (svn_fs__dag_get_contents (&(tb->source_stream),
                                     tb->node,
                                     trail));
  return SVN_NO_ERROR;
}


/* Helper function:  flush baton->target_string to disk as new file
   contents. */
static svn_error_t *
txn_body_write_target_string (void *baton, trail_t *trail)
{
  txdelta_baton_t *tb = (txdelta_baton_t *) baton;

  SVN_ERR (svn_fs__dag_set_contents (tb->node,
                                     tb->target_string,
                                     trail));

  return SVN_NO_ERROR;
}


/* The main window handler returned by svn_fs_apply_textdelta. */
static svn_error_t *
window_consumer (svn_txdelta_window_t *window, void *baton)
{
  txdelta_baton_t *tb = (txdelta_baton_t *) baton;

  /* Send the window right through to the custom window interpreter.
     In theory, the interpreter will then write more data to
     cb->target_string. */
  SVN_ERR (tb->interpreter (window, tb->interpreter_baton));

  /* Is the window NULL?  If so, we're done;  time to dump our target
     string to disk.  */
  if (! window)
    SVN_ERR (svn_fs__retry_txn (svn_fs_root_fs (tb->root),
                                txn_body_write_target_string, tb,
                                tb->pool));
  
  return SVN_NO_ERROR;
}




svn_error_t *
svn_fs_apply_textdelta (svn_txdelta_window_handler_t *contents_p,
                        void **contents_baton_p,
                        svn_fs_root_t *root,
                        const char *path,
                        apr_pool_t *pool)
{
  svn_stream_t *target_stream;
  txdelta_baton_t *tb = apr_pcalloc (pool, sizeof(*tb));
  tb->root = root;
  tb->path = path;
  tb->pool = pool;
  tb->target_string = svn_string_create ("", pool);
 
  /* Make a readable "source" stream out of the current contents of
     ROOT/PATH; obviously, this must done in the context of a
     db_txn.  The stream is returned in tb->source_stream. */
  SVN_ERR (svn_fs__retry_txn (svn_fs_root_fs (root),
                              txn_body_get_mutable_source_stream, tb, pool));

  /* Make a writable "target" stream which writes data to
     tb->target_string. */
  target_stream = svn_stream_create (tb, pool);
  svn_stream_set_write (target_stream, write_to_string);

  /* Finally, create a custom window handler that uses our two streams. */
  svn_txdelta_apply (tb->source_stream,
                     target_stream,
                     pool,
                     &(tb->interpreter),
                     &(tb->interpreter_baton));
  
  *contents_p = window_consumer;
  *contents_baton_p = tb;
  return SVN_NO_ERROR;
}

/* --- End machinery for svn_fs_apply_textdelta() ---  */




/* Creating transaction and revision root nodes.  */


struct txn_root_args
{
  svn_fs_root_t **root_p;
  svn_fs_txn_t *txn;
};


static svn_error_t *
txn_body_txn_root (void *baton,
                   trail_t *trail)
{
  struct txn_root_args *args = baton;
  svn_fs_root_t **root_p = args->root_p;
  svn_fs_txn_t *txn = args->txn;
  svn_fs_t *fs = svn_fs_txn_fs (txn);
  const char *svn_txn_id = svn_fs__txn_id (txn);
  svn_fs_id_t *root_id, *base_root_id;
  svn_fs_root_t *root;

  /* Verify that the transaction actually exists.  */
  SVN_ERR (svn_fs__get_txn (&root_id, &base_root_id, fs, svn_txn_id, trail));

  root = make_txn_root (fs, svn_txn_id, trail->pool);

  *root_p = root;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_txn_root (svn_fs_root_t **root_p,
                 svn_fs_txn_t *txn,
                 apr_pool_t *pool)
{
  svn_fs_root_t *root;
  struct txn_root_args args;

  args.root_p = &root;
  args.txn    = txn;
  SVN_ERR (svn_fs__retry_txn (svn_fs_txn_fs (txn), txn_body_txn_root,
                              &args, pool));

  *root_p = root;
  return SVN_NO_ERROR;
}


struct revision_root_args
{
  svn_fs_root_t **root_p;
  svn_fs_t *fs;
  svn_revnum_t rev;
};


static svn_error_t *
txn_body_revision_root (void *baton,
                        trail_t *trail)
{
  struct revision_root_args *args = baton;
  dag_node_t *root_dir;
  svn_fs_root_t *root;

  SVN_ERR (svn_fs__dag_revision_root (&root_dir, args->fs, args->rev, trail));
  root = make_revision_root (args->fs, args->rev, root_dir, trail->pool);

  *args->root_p = root;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_revision_root (svn_fs_root_t **root_p,
                      svn_fs_t *fs,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  struct revision_root_args args;
  svn_fs_root_t *root;

  SVN_ERR (svn_fs__check_fs (fs));

  args.root_p = &root;
  args.fs     = fs;
  args.rev    = rev;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_revision_root, &args, pool));

  *root_p = root;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_id_root (svn_fs_root_t **root_p,
                svn_fs_t *fs,
                apr_pool_t *pool)
{
  svn_fs_root_t *root = make_root (fs, pool);

  root->kind = id_root;
  *root_p = root;

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
