/* tree.c : tree-like filesystem, built on DAG filesystem
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
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_hash.h"
#include "svn_sorts.h"
#include "skel.h"
#include "id.h"
#include "fs.h"
#include "err.h"
#include "trail.h"
#include "txn-table.h"
#include "rev-table.h"
#include "nodes-table.h"
#include "node-rev.h"
#include "txn.h"
#include "dag.h"
#include "tree.h"
#include "proplist.h"



/* ### I believe this constant will become internal to reps-strings.c.
   ### see the comment in window_consumer() for more information. */

/* ### the comment also seems to need tweaking: the log file stuff
   ### is no longer an issue... */
/* Data written to the filesystem through the svn_fs_apply_textdelta()
   interface is cached in memory until the end of the data stream, or
   until a size trigger is hit.  Define that trigger here (in bytes).
   Setting the value to 0 will result in no filesystem buffering at
   all.  The value only really matters when dealing with file contents
   bigger than the value itself.  Above that point, large values here
   allow the filesystem to buffer more data in memory before flushing
   to the database, which increases memory usage but greatly decreases
   the amount of disk access (and log-file generation) in database.
   Smaller values will limit your overall memory consumption, but can
   drastically hurt throughput by necessitating more write operations
   to the database (which also generates more log-files).  */
#define SVN_FS_WRITE_BUFFER_SIZE   512000


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
       "file not found: transaction `%s', path `%s'",
       root->txn, path);
  else if (root->kind == revision_root)
    return
      svn_error_createf
      (SVN_ERR_FS_NOT_FOUND, 0, 0, fs->pool,
       "file not found: revision `%ld', path `%s'",
       root->rev, path);
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
       fs->path, root->txn, path);
  else if (root->kind == revision_root)
    return
      svn_error_createf
      (SVN_ERR_FS_ALREADY_EXISTS, 0, 0, fs->pool,
       "file already exists: filesystem `%s', revision `%ld', path `%s'",
       fs->path, root->rev, path);
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

                  svn_error_clear_all (svn_err);

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
  *args->id_p = svn_fs__id_copy (svn_fs__dag_get_id (node), trail->pool);

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


struct node_created_rev_args {
  svn_revnum_t revision;
  svn_fs_root_t *root;
  const char *path;
};


static svn_error_t *
txn_body_node_created_rev (void *baton, trail_t *trail)
{
  struct node_created_rev_args *args = baton;
  dag_node_t *node;

  SVN_ERR (get_dag (&node, args->root, args->path, trail));
  SVN_ERR (svn_fs__dag_get_revision (&(args->revision), node, trail));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_node_created_rev (svn_revnum_t *revision,
                         svn_fs_root_t *root,
                         const char *path,
                         apr_pool_t *pool)
{
  struct node_created_rev_args args;

  args.revision = SVN_INVALID_REVNUM;
  args.root = root;
  args.path = path;
  SVN_ERR (svn_fs__retry_txn 
           (root->fs, txn_body_node_created_rev, &args, pool));
  *revision = args.revision;
  return SVN_NO_ERROR;
}


struct node_kind_args {
  svn_fs_root_t *root;
  const char *path;

  svn_node_kind_t kind; /* OUT parameter */
};


static svn_error_t *
txn_body_node_kind (void *baton, trail_t *trail)
{
  struct node_kind_args *args = baton;
  dag_node_t *node;

  SVN_ERR (get_dag (&node, args->root, args->path, trail));
  args->kind = svn_fs__dag_node_kind (node);
  
  return SVN_NO_ERROR;
}

svn_node_kind_t svn_fs_check_path (svn_fs_root_t *root,
                                   const char *path,
                                   apr_pool_t *pool)
{
  struct node_kind_args args;
  svn_error_t *err;

  args.root = root;
  args.path = path;

  err = svn_fs__retry_txn (root->fs, txn_body_node_kind, &args, pool);
  if (err)
    return svn_node_none;

  return args.kind;
}

svn_error_t *
svn_fs_is_dir (int *is_dir,
               svn_fs_root_t *root,
               const char *path,
               apr_pool_t *pool)
{
  struct node_kind_args args;

  args.root = root;
  args.path = path;

  SVN_ERR (svn_fs__retry_txn (root->fs, txn_body_node_kind, &args, pool));
  *is_dir = (args.kind == svn_node_dir);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_is_file (int *is_file,
                svn_fs_root_t *root,
                const char *path,
                apr_pool_t *pool)
{
  struct node_kind_args args;

  args.root = root;
  args.path = path;

  SVN_ERR (svn_fs__retry_txn (root->fs, txn_body_node_kind, &args, pool));
  *is_file = (args.kind == svn_node_file);

  return SVN_NO_ERROR;
}


struct is_different_args
{
  int is_different;
  svn_fs_root_t *root1;
  const char *path1;
  svn_fs_root_t *root2;
  const char *path2;
};


static svn_error_t *
txn_body_is_different (void *baton, trail_t *trail)
{
  struct is_different_args *args = baton;
  dag_node_t *node1, *node2;
  svn_node_kind_t kind;
  int props_differ, contents_differ;

  /* Assume the paths *are* different. */
  args->is_different = 1;

  /* Get the node revisions for these paths. */
  SVN_ERR (get_dag (&node1, args->root1, args->path1, trail));
  SVN_ERR (get_dag (&node2, args->root2, args->path2, trail));

  /* If they have the same node-ID, they're the same! */
  if (svn_fs__dag_get_id (node1) == svn_fs__dag_get_id (node2))
    {
      args->is_different = 0;
      return SVN_NO_ERROR;
    }

  /* If their kinds differ, they differ. */
  if ((kind = svn_fs__dag_node_kind (node1)) != svn_fs__dag_node_kind (node2))
    return SVN_NO_ERROR;

  /* Now call our internal differencing checker thingamabob.  This
     will only compare representation IDs, though, so it only tells us
     when things have the same contents, really, not when they have
     different contents. 

     ### todo: Finish svn_fs__things_different to do the full content
     comparison it was intended to do.  */
  SVN_ERR (svn_fs__things_different (&props_differ,
                                     &contents_differ,
                                     node1, node2, trail));

  if (! (props_differ || contents_differ))
    {
      args->is_different = 0;
      return SVN_NO_ERROR;
    }
      
  return SVN_NO_ERROR;
}


/* Note:  it is acceptable for this function to call back into
   public FS API interfaces because it does not itself use trails.  */
svn_error_t *
svn_fs_is_different (int *is_different,
                     svn_fs_root_t *root1,
                     const char *path1,
                     svn_fs_root_t *root2,
                     const char *path2,
                     apr_pool_t *pool)
{
  struct is_different_args args;

  if ((svn_fs_root_fs (root1)) != (svn_fs_root_fs (root2)))
    return svn_error_create
      (SVN_ERR_FS_GENERAL, 0, NULL, pool,
       "Asking is different in two different filesystems.");

  args.root1 = root1;
  args.path1 = path1;
  args.root2 = root2;
  args.path2 = path2;

  SVN_ERR (svn_fs__retry_txn (root1->fs, txn_body_is_different, &args, pool));
  *is_different = args.is_different;
  return SVN_NO_ERROR;
}


struct node_prop_args
{
  svn_string_t **value_p;
  svn_fs_root_t *root;
  const char *path;
  const char *propname;
};


static svn_error_t *
txn_body_node_prop (void *baton,
                    trail_t *trail)
{
  struct node_prop_args *args = baton;
  dag_node_t *node;
  skel_t *proplist;

  SVN_ERR (get_dag (&node, args->root, args->path, trail));
  SVN_ERR (svn_fs__dag_get_proplist (&proplist, node, trail));

  /* Return the results of the generic property getting function. */
  return svn_fs__get_prop (args->value_p,
                           proplist,
                           args->propname,
                           trail->pool);
}


svn_error_t *
svn_fs_node_prop (svn_string_t **value_p,
                  svn_fs_root_t *root,
                  const char *path,
                  const char *propname,
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
  skel_t *proplist;

  SVN_ERR (open_path (&parent_path, args->root, args->path, 0, trail));
  SVN_ERR (svn_fs__dag_get_proplist (&proplist, parent_path->node, trail));

  /* Return the results of the generic property hash getting function. */
  return svn_fs__make_prop_hash (args->table_p,
                                 proplist,
                                 trail->pool);
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
  const char *name;
  const svn_string_t *value;
};


static svn_error_t *
txn_body_change_node_prop (void *baton,
                           trail_t *trail)
{
  struct change_node_prop_args *args = baton;
  parent_path_t *parent_path;
  skel_t *proplist;

  SVN_ERR (open_path (&parent_path, args->root, args->path, 0, trail));
  SVN_ERR (make_path_mutable (args->root, parent_path, args->path, trail));
  SVN_ERR (svn_fs__dag_get_proplist (&proplist, parent_path->node, trail));
  SVN_ERR (svn_fs__set_prop (proplist, args->name, args->value, trail->pool));
  SVN_ERR (svn_fs__dag_set_proplist (parent_path->node, proplist, trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_change_node_prop (svn_fs_root_t *root,
                         const char *path,
                         const char *name,
                         const svn_string_t *value,
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


struct things_changed_args
{
  int *changed_p;
  svn_fs_root_t *root1;
  svn_fs_root_t *root2;
  const char *path1;
  const char *path2;
  apr_pool_t *pool;
};


static svn_error_t *
txn_body_props_changed (void *baton, trail_t *trail)
{
  struct things_changed_args *args = baton;
  parent_path_t *parent_path_1, *parent_path_2;

  SVN_ERR (open_path (&parent_path_1, args->root1, args->path1, 0, trail));
  SVN_ERR (open_path (&parent_path_2, args->root2, args->path2, 0, trail));

  SVN_ERR (svn_fs__things_different (args->changed_p,
                                     NULL,
                                     parent_path_1->node,
                                     parent_path_2->node,
                                     trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_props_changed (int *changed_p,
                      svn_fs_root_t *root1,
                      const char *path1,
                      svn_fs_root_t *root2,
                      const char *path2,
                      apr_pool_t *pool)
{
  struct things_changed_args args;
  
  /* Check that roots are in the same fs. */
  if ((svn_fs_root_fs (root1)) != (svn_fs_root_fs (root2)))
    return svn_error_create
      (SVN_ERR_FS_GENERAL, 0, NULL, pool,
       "Asking props changed in two different filesystems.");
  
  args.root1      = root1;
  args.root2      = root2;
  args.path1      = path1;
  args.path2      = path2;
  args.changed_p  = changed_p;
  args.pool       = pool;

  SVN_ERR (svn_fs__retry_txn (root1->fs, txn_body_props_changed,
                              &args, pool));

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


/* String slap-together-er for path extension purposes. */
static char *
path_append (const char *dir, const char *entry, apr_pool_t *pool)
{
  return apr_psprintf (pool, "%s/%s", dir, entry);
}


/* Merge changes between ANCESTOR and SOURCE into TARGET, as part of
 * TRAIL.  ANCESTOR and TARGET must be distinct node revisions.
 * TARGET_PATH should correspond to TARGET's full path in its
 * filesystem, and is used for reporting conflict location.
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
       const char *target_path,
       dag_node_t *target,
       dag_node_t *source,
       dag_node_t *ancestor,
       trail_t *trail)
{
  const svn_fs_id_t *source_id, *target_id, *ancestor_id;
  apr_hash_t *s_entries, *t_entries, *a_entries;
  apr_hash_index_t *hi;
  svn_fs_t *fs;

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
  if (svn_fs__id_eq (ancestor_id, target_id))
    {
      svn_stringbuf_t *id_str = svn_fs_unparse_id (target_id, trail->pool);
      return svn_error_createf
        (SVN_ERR_FS_GENERAL, 0, NULL, trail->pool,
         "Bad merge call -- target `%s' has id `%s', same as ancestor.",
         target_path, id_str->data);
    }

  *conflict_p = NULL;

  /* Base cases:
   * Either no change made in source, or same change as made in target.
   * Both mean nothing to merge here.
   */
  if (svn_fs__id_eq (ancestor_id, source_id)
      || (svn_fs__id_eq (source_id, target_id)))
    return SVN_NO_ERROR;

  /* Else proceed, knowing all three are distinct node revisions.
   *
   * How to merge from this point: 
   *
   * if (not all 3 are directories)
   *   {
   *     early exit with conflict;
   *   }
   *
   * // Property changes may only be made to up-to-date
   * // directories, because once the client commits the prop
   * // change, it bumps the directory's revision, and therefore
   * // must be able to depend on there being no other changes to
   * // that directory in the repository.
   * if (target's property list differs from ancestor's)
   *    conflict;
   *
   * for (each entry E in ancestor)
   *   {
   *     if (E exists in target and source)
   *       {
   *         if (source entry points to different id than E)
   *           {
   *             if (target entry points to same id as ancestor E)
   *               change target to point to same id as source entry;
   *             else if ((target entry id different from source)
   *                      && (target entry not descended from source))
   *               {
   *                 if (not all 3 entries point to directories)
   *                   {
   *                     early exit with conflict;
   *                   }
   *
   *                 // We know they are different directories, so...
   *                 recursively merge;
   *               }
   *             // Else target entry same as source entry, or is
   *             // descendant of source entry; either way, leave it.
   *           }
   *       }
   *     else if (E exists in source but not target)
   *       add same entry to target, pointing to source entry's id;
   *     else if (E exists in target but not source)
   *       {
   *         if (E points the same node rev in target and ancestor)
   *            delete E from target;
   *         else // E points to different node revs in target & ancestor
   *           {
   *             conflict;
   *           }
   *       }
   *     else
   *       {
   *         // E exists in neither target nor source, so it's a
   *         // double delete -- do nothing, since E is already
   *         // absent from target.  ### kff todo: but it would be
   *         // nice to handle the rename case better.  How?
   *       }
   *   }
   *
   * // This next loop is over those entries in source that were
   * // not already covered in the loop over ancestor above.
   * for (each remaining entry E in source)
   *   {
   *     if (E does not exist in target)
   *       add it to target, based on source;
   *     else if (E exists in target but different id than E in source)
   *       conflict;
   *   }
   *         
   *     // All entries in ancestor and source are accounted for.
   *     // Remaining entries in target should be left as-is.
   *   }
   *
   */

  if ((! svn_fs__dag_is_directory (source))
      || (! svn_fs__dag_is_directory (target))
      || (! svn_fs__dag_is_directory (ancestor)))
    {
      *conflict_p = apr_pstrdup (trail->pool, target_path);
      return svn_error_createf (SVN_ERR_FS_CONFLICT, 0, NULL, trail->pool,
                                "conflict at \"%s\"", target_path);
    }

      
  /* Possible early merge failure: if target and ancestor have
     different property lists, then the merge should fail.
     Propchanges can *only* be committed on an up-to-date directory.

     ### TODO: Please see issue #418 about the inelegance of this. */
  {
    skel_t *tgt_skel, *anc_skel;

    /* Convert dag_nodes into id's, and id's into skels. */
    SVN_ERR (svn_fs__get_node_revision (&tgt_skel, fs, target_id, trail));
    SVN_ERR (svn_fs__get_node_revision (&anc_skel, fs, ancestor_id, trail));
        
    /* Now compare the prop-keys of the skels.  Note that just because
       the keys are different -doesn't- mean the proplists have
       different contents.  But merge() isn't concerned with contents;
       it doesn't do a brute-force comparison on textual contents, so
       it won't do that here either.  Checking to see if the propkey
       atoms are `equal' is enough. */
    if (! svn_fs__skels_are_equal (SVN_FS__NR_PROP_KEY(tgt_skel),
                                   SVN_FS__NR_PROP_KEY(anc_skel)))
      {
        *conflict_p = apr_pstrdup (trail->pool, target_path);
        return svn_error_createf (SVN_ERR_FS_CONFLICT, 0, NULL, trail->pool,
                                  "conflict at \"%s\"", *conflict_p);
      }
  }

  SVN_ERR (svn_fs__dag_dir_entries_hash (&s_entries, source, trail));
  SVN_ERR (svn_fs__dag_dir_entries_hash (&t_entries, target, trail));
  SVN_ERR (svn_fs__dag_dir_entries_hash (&a_entries, ancestor, trail));

  /* for each entry E in a_entries... */
  for (hi = apr_hash_first (trail->pool, a_entries); 
       hi; 
       hi = apr_hash_next (hi))
    {
      svn_fs_dirent_t *s_entry, *t_entry, *a_entry;

      const void *key;
      void *val;
      apr_ssize_t klen;
          
      /* KEY will be the entry name in ancestor, VAL the dirent */
      apr_hash_this (hi, &key, &klen, &val);
      a_entry = val;
          
      /* E exists in target and source (as well as ancestor) */
      if ((s_entry = apr_hash_get (s_entries, key, klen))
          && (t_entry = apr_hash_get (t_entries, key, klen)))
        {
          /* If source entry has changed since ancestor entry... */
          if (! svn_fs__id_eq (a_entry->id, s_entry->id))
            {
              /* ... and if target entry has not changed,
                 - OR - if target descends from ancestor, and source
                 descends from target... */
              if ( (svn_fs__id_eq (a_entry->id, t_entry->id))
                   || ( (svn_fs__id_is_ancestor(a_entry->id, t_entry->id)) 
                        && (svn_fs__id_is_ancestor(t_entry->id,
                                                   s_entry->id))) )
                {
                  /* ### kff todo: what about svn_fs__dag_link()
                     instead of svn_fs__dag_set_entry()?  The cycle
                     protection guaranteed by the former would be
                     guaranteed "for free" anyway, if this function
                     demanded that SOURCE and ANCESTOR always be
                     immutable nodes.  But we don't demand that,
                     although it happens to be true of our only caller
                     right now, since merges are only done as part of
                     commits. */

                  /* ... target takes source. */
                  svn_boolean_t is_mutable;
                  SVN_ERR (svn_fs__dag_check_mutable (&is_mutable,
                                                      target, trail));
                  if (! is_mutable)
                    return svn_error_createf
                      (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
                       "unexpected immutable node at \"%s\"", target_path);

                  SVN_ERR (svn_fs__dag_set_entry
                           (target, t_entry->name, s_entry->id, trail));
                }
              /* or if target entry is different from both and
                 unrelated to source, and all three entries are dirs... */
              else if (! svn_fs__id_is_ancestor (s_entry->id, t_entry->id))
                {
                  dag_node_t *s_ent_node, *t_ent_node, *a_ent_node;
                  const char *new_tpath;
                      
                  SVN_ERR (svn_fs__dag_get_node (&s_ent_node, fs,
                                                 s_entry->id, trail));
                  SVN_ERR (svn_fs__dag_get_node (&t_ent_node, fs,
                                                 t_entry->id, trail));
                  SVN_ERR (svn_fs__dag_get_node (&a_ent_node, fs,
                                                 a_entry->id, trail));
                      
                  if ((! svn_fs__dag_is_directory (s_ent_node))
                      || (! svn_fs__dag_is_directory (t_ent_node))
                      || (! svn_fs__dag_is_directory (a_ent_node)))
                    {
                      /* Not all of these entries is a directory. Conflict. */
                      *conflict_p = path_append (target_path,
                                                 a_entry->name,
                                                 trail->pool);
                      return svn_error_createf
                        (SVN_ERR_FS_CONFLICT, 0, NULL, trail->pool,
                         "conflict at \"%s\"", *conflict_p);
                    }

                  /* ... just recurse. */
                  new_tpath = path_append (target_path, t_entry->name,
                                           trail->pool);
                  SVN_ERR (merge (conflict_p, new_tpath,
                                  t_ent_node, s_ent_node, a_ent_node,
                                  trail));
                  /* ### kff todo: 

                     As Jim mentioned on the phone, there's a
                     history-tracking problem here.  After we do the
                     merge into target, target has absorbed the
                     history between ancestor and source, but there is
                     no record of this absorbtion having happened.
                     For example, when generating a log message for
                     target, you'd want to include all the changes
                     between ancestor and source.

                     In the general case, this is the same genetic
                     merge problem that we'll have to deal with when
                     we do full ancestry tracking.  (Hello,
                     changesets.)

                     But the most common particular case is that
                     target is an immediate descendant of ancestor,
                     and source is also a descendant of ancestor.
                     That is:

                        svn_fs_id_distance (ancestor, target) == 1
                        svn_fs_id_distance (ancestor, source) >= 1
                             
                     In such cases, we can record the successful merge
                     for free, by making t_entry->name point to a node
                     id that is a successor of s_entry->id.  This is
                     safe because

                     - all the history from time-zero to ancestor is
                     preserved

                     - all the history from ancestor to source is now
                     preserved

                     - the single historical step from ancestor to
                     target is preserved as a single step from source
                     to target, which is an accurate reflection of the
                     post-merge situation anyway.

                     Note that this trick should be used after any
                     call to merge(), not just the recursive call
                     above.  That means the transaction root should be
                     re-ID'd after the merge.  (And come to think of
                     it, we're already resetting the transaction's
                     base root to source, so setting the txn's mutable
                     root to a successor of source has a certain
                     inevitability, n'est ce pas?) */

                  /* Per the comment above, if target is an immediate
                     descendant of ancestor, and source is also a
                     descendant of ancestor, we need to re-ID target
                     with a successor ID of source. */
                  if ((svn_fs_id_distance (a_entry->id,
                                           t_entry->id) == 1)
                      && (svn_fs_id_distance (a_entry->id,
                                              s_entry->id) >= 1))
                    {
                      svn_fs_id_t *successor;
                      skel_t *node_rev;

                      /* Get a successor id. */
                      SVN_ERR (svn_fs__new_successor_id 
                               (&successor, fs, s_entry->id, trail));
                              
                      /* Copy the target node to the new successor id. */
                      SVN_ERR (svn_fs__get_node_revision 
                               (&node_rev, fs, t_entry->id, trail));
                      SVN_ERR (svn_fs__put_node_revision 
                               (fs, successor, node_rev, trail));
                              
                      /* Update t_entry's parent with the new id. */
                      SVN_ERR (svn_fs__dag_set_entry 
                               (target, t_entry->name, 
                                successor, trail));

                      /* Now, delete the old node revision. */
                      SVN_ERR (svn_fs__delete_nodes_entry 
                               (fs, t_entry->id, trail));
                    }
                }
              /* Else target entry has changed since ancestor entry,
                 but it changed either to source entry or to a
                 successor of source entry, so we need do nothing. */
            }
        }
      /* E exists in source but not target */
      else if ((s_entry = apr_hash_get (s_entries, key, klen))
               && (! apr_hash_get (t_entries, key, klen)))
        {
          /* If E changed between ancestor and source, then that
             conflicts with E's having been removed from target. */
          if (! svn_fs__id_eq (a_entry->id, s_entry->id))
            {
              *conflict_p = path_append (target_path, a_entry->name,
                                         trail->pool);
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
          if (svn_fs__id_eq (t_entry->id, a_entry->id))
            {
              /* If E is same in target as ancestor, then it has not
                 changed, and the deletion in source should be
                 honored. */
              svn_boolean_t is_mutable;
              SVN_ERR (svn_fs__dag_check_mutable (&is_mutable,
                                                  target, trail));
              if (! is_mutable)
                return svn_error_createf
                  (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
                   "unexpected immutable node at \"%s\"", target_path);
              
              SVN_ERR (svn_fs__dag_delete_tree (target, t_entry->name, trail));

              /* Seems cleanest to remove it from the target entries
                 hash now, even though no code would break if we
                 didn't.  It feels more robust if t_entries reflects
                 the state of the target at all times. */
              apr_hash_set (t_entries, key, klen, NULL);
            }
          else if (svn_fs_id_distance (t_entry->id, a_entry->id) != -1)
            {
              /* E is an attempt to modify ancestor, so it's a
                 conflict with the deletion of E in source.  If E
                 were unrelated to ancestor, it would not be an
                 attempt to modify ancestor (it might just be a copy
                 or rename of something unrelated), in which case we
                 wouldn't conflict.  It's because E is *related* to
                 the ancestor that we conflict here.

                 ### TODO: see issue #418 about this inelegance. */
              *conflict_p = path_append (target_path, t_entry->name,
                                         trail->pool);
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
          
      /* We've taken care of any possible implications E could have.
         Remove it from source_entries, so it's easy later to loop
         over all the source entries that didn't exist in
         ancestor_entries. */
      apr_hash_set (s_entries, key, klen, NULL);
    }
      
  /* For each entry E in source but not in ancestor */
  for (hi = apr_hash_first (trail->pool, s_entries); 
       hi; 
       hi = apr_hash_next (hi))
    {
      svn_fs_dirent_t *s_entry, *t_entry;
      const void *key;
      void *val;
      apr_ssize_t klen;
          
      apr_hash_this (hi, &key, &klen, &val);
      s_entry = val;
      t_entry = apr_hash_get (t_entries, key, klen);

      /* E does not exist in target */
      if (! t_entry)
        {
          /* target takes source */
          svn_boolean_t is_mutable;
          SVN_ERR (svn_fs__dag_check_mutable (&is_mutable,
                                              target, trail));
          if (! is_mutable)
            return svn_error_createf
              (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
               "unexpected immutable node at \"%s\"", target_path);
              
          SVN_ERR (svn_fs__dag_set_entry
                   (target, s_entry->name, s_entry->id, trail));
        }
      /* E exists in target but is different from E in source */
      else if (! svn_fs__id_is_ancestor (s_entry->id, t_entry->id))
        {
          *conflict_p = path_append (target_path, t_entry->name, trail->pool);
          return svn_error_createf
            (SVN_ERR_FS_CONFLICT, 0, NULL, trail->pool,
             "conflict at \"%s\"", *conflict_p);

          /* The remaining case would be: E exists in target and is
           * same as in source.  This implies a twin add, so target
           * just stays as is.  
           */
        }
    }
      
  /* All entries in ancestor and source have been accounted for.
   *
   * Any entry E in target that does not exist in ancestor or source
   * is a non-conflicting add, so we don't need to do anything about
   * it.  
   */

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


/* Merge changes between an ancestor and BATON->source_node into
   BATON->txn.  The ancestor is either BATON->ancestor_node, or if
   that is null, BATON->txn's base node.

   If the merge is successful, BATON->txn's base will become
   BATON->source_node, and its root node will have a new ID, a
   successor of BATON->source_node. */
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
  
  if (svn_fs__id_eq (svn_fs__dag_get_id (ancestor_node),
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
      const svn_fs_id_t *ancestor_id, *target_id;

      SVN_ERR (merge (&(args->conflict),
                      "",
                      txn_root_node,
                      source_node,
                      ancestor_node,
                      trail));

      ancestor_id = svn_fs__dag_get_id (ancestor_node);
      target_id = svn_fs__dag_get_id (txn_root_node);

      /* Re-ID the txn following a successful merge; see Dostoyevskian
         comment in merge() about this for the full explanation. */
      if ((svn_fs_id_distance (ancestor_id, target_id) == 1)
          && (svn_fs_id_distance (ancestor_id, source_id) >= 1))
        {
          svn_fs_id_t *successor;
          skel_t *node_rev;
          
          /* Get a successor id. */
          SVN_ERR (svn_fs__new_successor_id 
                   (&successor, fs, source_id, trail));
          
          /* Copy the target node to the new successor id. */
          SVN_ERR (svn_fs__get_node_revision 
                   (&node_rev, fs, target_id, trail));
          SVN_ERR (svn_fs__put_node_revision 
                   (fs, successor, node_rev, trail));
          
          /* Update the transaction with the new root id. */
          SVN_ERR (svn_fs__set_txn_root (fs, txn_name, successor, trail));

          /* Now, delete the old node revision. */
          SVN_ERR (svn_fs__delete_nodes_entry (fs, target_id, trail));
        }

      /* After the merge, txn's new "ancestor" is now really the node
         at source_id, so record that fact.  Think of this as
         ratcheting the txn forward in time, so it can't backslide and
         forget the merging work that's already been done. */
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
  if (! svn_fs__id_eq (y_rev_root_id, svn_fs__dag_get_id (txn_base_root_node)))
    {
      svn_stringbuf_t *id_str = svn_fs_unparse_id (y_rev_root_id, trail->pool);
      return svn_error_createf
        (SVN_ERR_TXN_OUT_OF_DATE, 0, NULL, trail->pool,
         "txn `%s' out of date w.r.t. revision `%s'", txn_name, id_str->data);
    }
  
  /* Else, commit the txn. */
  SVN_ERR (svn_fs__dag_commit_txn (&(args->new_rev), fs, txn_name, trail));

  return SVN_NO_ERROR;
}


/* Note:  it is acceptable for this function to call back into
   public FS API interfaces because it does not itself use trails.  */
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
  svn_fs_t *fs = svn_fs__txn_fs (txn);
  apr_pool_t *pool = svn_fs__txn_pool (txn);

  /* Initialize returned revision number to an invalid value. */
  *new_rev = SVN_INVALID_REVNUM;

  while (1729)
    {
      struct get_root_args get_root_args;
      struct merge_args merge_args;
      struct commit_args commit_args;
      svn_revnum_t youngish_rev;
      svn_fs_root_t *youngish_root;
      dag_node_t *youngish_root_node;

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
          else
            svn_error_clear_all (err);
        }
      else if (err)
        return err;
      else
        {
          svn_fs_root_t *root;
          svn_error_t *err2;

          *new_rev = commit_args.new_rev;

          /* Final step: after a successful commit of the transaction,
             deltify the new revision. */
          if ((! (err2 = svn_fs_revision_root (&root, fs, *new_rev, pool))))
            err2 = svn_fs_deltify (root, "/", TRUE, pool);
          
          return (err2 
                  ? svn_error_quick_wrap 
                      (err2, "Commit succeeded, deltification failed")
                  : SVN_NO_ERROR);
        }
    }

  return SVN_NO_ERROR;
}


/* Note:  it is acceptable for this function to call back into
   public FS API interfaces because it does not itself use trails.  */
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


/* If this returns SVN_ERR_FS_NO_SUCH_ENTRY, it means that the
   basename of PATH is missing from its parent, that is, the final
   target of the deletion is missing.  */
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
  svn_boolean_t preserve_history;
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
      /* Make sure the target node's parents are mutable.  */
      SVN_ERR (make_path_mutable (to_root, to_parent_path->parent, 
                                  to_path, trail));

      SVN_ERR (svn_fs__dag_copy (to_parent_path->parent->node,
                                 to_parent_path->entry,
                                 from_parent_path->node,
                                 args->preserve_history,
                                 svn_fs_revision_root_revision (from_root),
                                 from_path,
                                 trail));
    }
  else
    {
      /* See IZ Issue #436 */
      /* Copying from transaction roots not currently available.

         ### cmpilato todo someday: make this not so. :-) Note that
         when copying from mutable trees, you have to make sure that
         you aren't creating a cyclic graph filesystem, and a simple
         referencing operation won't cut it.  Currently, we should not
         be able to reach this clause, and the interface reports that
         this only works from immutable trees anyway, but JimB has
         stated that this requirement need not be necessary in the
         future. */

      abort ();
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

  args.from_root         = from_root;
  args.from_path         = from_path;
  args.to_root           = to_root;
  args.to_path           = to_path;
  args.preserve_history  = 1;

  return svn_fs__retry_txn (to_root->fs, txn_body_copy, &args, pool);
}


svn_error_t *
svn_fs_link (svn_fs_root_t *from_root,
             const char *from_path,
             svn_fs_root_t *to_root,
             const char *to_path,
             apr_pool_t *pool)
{
  struct copy_args args;

  if (! svn_fs_is_txn_root (to_root))
    return not_txn (to_root);

  args.from_root         = from_root;
  args.from_path         = from_path;
  args.to_root           = to_root;
  args.to_path           = to_path;
  args.preserve_history  = 0;

  return svn_fs__retry_txn (to_root->fs, txn_body_copy, &args, pool);
}


struct copied_from_args
{
  svn_fs_root_t *root;      /* Root for the node whose ancestry we seek. */
  const char *path;         /* Path for the node whose ancestry we seek. */

  svn_revnum_t result_rev;  /* Revision, if any, of the ancestor. */
  const char *result_path;  /* Path, if any, of the ancestor. */

  apr_pool_t *pool;         /* Allocate `result_path' here. */
};


static svn_error_t *
txn_body_copied_from (void *baton, trail_t *trail)
{
  struct copied_from_args *args = baton;
  parent_path_t *path_down;

  SVN_ERR (open_path (&path_down,
                      args->root,
                      args->path,
                      0,
                      trail));

  SVN_ERR (svn_fs__dag_copied_from (&(args->result_rev),
                                    &(args->result_path),
                                    path_down->node,
                                    trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_copied_from (svn_revnum_t *rev_p,
                    const char **path_p,
                    svn_fs_root_t *root,
                    const char *path,
                    apr_pool_t *pool)
{
  struct copied_from_args args;

  args.root = root;
  args.path = path;
  args.pool = pool;

  SVN_ERR (svn_fs__retry_txn (root->fs, txn_body_copied_from, &args, pool));

  *rev_p  = args.result_rev;
  *path_p = args.result_path;

  return SVN_NO_ERROR;
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
  apr_size_t length;     /* OUT parameter */
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
  return svn_fs__dag_file_length (&args->length, file, trail);
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
    
  /* The pool in which `file_stream' (below) is allocated. */
  apr_pool_t *pool;

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
                                     fb->pool,
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
  fb->pool = pool;

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

  /* The original file info */
  svn_fs_root_t *root;
  const char *path;
  
  /* Derived from the file info */
  dag_node_t *node;

  svn_stream_t *source_stream;
  svn_stream_t *target_stream;
  svn_stream_t *string_stream;
  svn_stringbuf_t *target_string;

  /* Pool used by db txns */
  apr_pool_t *pool;

} txdelta_baton_t;


/* A trail-ready wrapper around svn_fs__dag_finalize_edits. */
static svn_error_t *
txn_body_finalize_edits (void *baton, trail_t *trail)
{
  txdelta_baton_t *tb = (txdelta_baton_t *) baton;
  return svn_fs__dag_finalize_edits (tb->node, trail);
}


/* ### see comment in window_consumer() regarding this function. */

/* Helper function of generic type `svn_write_fn_t'.  Implements a
   writable stream which appends to an svn_stringbuf_t. */
static svn_error_t *
write_to_string (void *baton, const char *data, apr_size_t *len)
{
  txdelta_baton_t *tb = (txdelta_baton_t *) baton;


  svn_stringbuf_appendbytes (tb->target_string, data, *len);

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

  /* ### the write_to_string() callback for the txdelta's output stream
     ### should be doing all the flush determination logic, not here.
     ### in a drastic case, a window could generate a LOT more than the
     ### maximum buffer size. we want to flush to the underlying target
     ### stream much sooner (e.g. also in a streamy fashion). also, by
     ### moving this logic inside the stream, the stream becomes nice
     ### and encapsulated: it holds all the logic about buffering and
     ### flushing.
     ###
     ### further: I believe the buffering should be removed from tree.c
     ### the buffering should go into the target_stream itself, which
     ### is defined by reps-string.c. Specifically, I think the
     ### rep_write_contents() function will handle the buffering and
     ### the spill to the underlying DB. by locating it there, then
     ### anybody who gets a writable stream for FS content can take
     ### advantage of the buffering capability. this will be important
     ### when we export an FS API function for writing a fulltext into
     ### the FS, rather than forcing that fulltext thru apply_textdelta.
  */

  /* Check to see if we need to purge the portion of the contents that
     have been written thus far. */
  if ((! window) || (tb->target_string->len > SVN_FS_WRITE_BUFFER_SIZE))
    {
      apr_size_t len = tb->target_string->len;
      svn_stream_write (tb->target_stream,
                        tb->target_string->data,
                        &len);
      svn_stringbuf_set (tb->target_string, "");
    }

  /* Is the window NULL?  If so, we're done, and we need to tell the
     dag subsystem that we're finished with our edits. */
  if (! window)
    SVN_ERR (svn_fs__retry_txn (svn_fs_root_fs (tb->root),
                                txn_body_finalize_edits, tb, tb->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
txn_body_apply_textdelta (void *baton, trail_t *trail)
{
  txdelta_baton_t *tb = (txdelta_baton_t *) baton;
  parent_path_t *parent_path;

  /* Call open_path with no flags, as we want this to return an error
     if the node for which we are searching doesn't exist. */
  SVN_ERR (open_path (&parent_path, tb->root, tb->path, 0, trail));

  /* Now, make sure this path is mutable. */
  SVN_ERR (make_path_mutable (tb->root, parent_path, tb->path, trail));
  tb->node = parent_path->node;

  /* Make a readable "source" stream out of the current contents of
     ROOT/PATH; obviously, this must done in the context of a db_txn.
     The stream is returned in tb->source_stream. */
  SVN_ERR (svn_fs__dag_get_contents (&(tb->source_stream),
                                     tb->node,
                                     tb->pool,
                                     trail));

  /* Make a writable "target" stream */
  SVN_ERR (svn_fs__dag_get_edit_stream (&(tb->target_stream),
                                        tb->node,
                                        tb->pool,
                                        trail));

  /* Make a writable "string" stream which writes data to
     tb->target_string. */
  tb->target_string = svn_stringbuf_create ("", tb->pool);
  tb->string_stream = svn_stream_create (tb, tb->pool);
  svn_stream_set_write (tb->string_stream, write_to_string);

  /* Finally, create a custom window handler that uses our two
     streams. */
  svn_txdelta_apply (tb->source_stream,
                     tb->string_stream,
                     tb->pool,
                     &(tb->interpreter),
                     &(tb->interpreter_baton));


  return SVN_NO_ERROR;

}

svn_error_t *
svn_fs_apply_textdelta (svn_txdelta_window_handler_t *contents_p,
                        void **contents_baton_p,
                        svn_fs_root_t *root,
                        const char *path,
                        apr_pool_t *pool)
{
  txdelta_baton_t *tb = apr_pcalloc (pool, sizeof(*tb));

  tb->root = root;
  tb->path = path;
  tb->pool = pool;

  /* See IZ Issue #438 */
  SVN_ERR (svn_fs__retry_txn (svn_fs_root_fs (root),
                              txn_body_apply_textdelta, tb, pool));
  
  *contents_p = window_consumer;
  *contents_baton_p = tb;
  return SVN_NO_ERROR;
}

/* --- End machinery for svn_fs_apply_textdelta() ---  */


/* Note: we're sharing the `things_changed_args' struct with
   svn_fs_props_changed(). */

static svn_error_t *
txn_body_contents_changed (void *baton, trail_t *trail)
{
  struct things_changed_args *args = baton;
  parent_path_t *parent_path_1, *parent_path_2;

  SVN_ERR (open_path (&parent_path_1, args->root1, args->path1, 0, trail));
  SVN_ERR (open_path (&parent_path_2, args->root2, args->path2, 0, trail));

  SVN_ERR (svn_fs__things_different (NULL,
                                     args->changed_p,
                                     parent_path_1->node,
                                     parent_path_2->node,
                                     trail));

  return SVN_NO_ERROR;
}


/* Note:  it is acceptable for this function to call back into
   public FS API interfaces because it does not itself use trails.  */
svn_error_t *
svn_fs_contents_changed (int *changed_p,
                         svn_fs_root_t *root1,
                         const char *path1,
                         svn_fs_root_t *root2,
                         const char *path2,
                         apr_pool_t *pool)
{
  struct things_changed_args args;

  /* Check that roots are in the same fs. */
  if ((svn_fs_root_fs (root1)) != (svn_fs_root_fs (root2)))
    return svn_error_create
      (SVN_ERR_FS_GENERAL, 0, NULL, pool,
       "Asking props changed in two different filesystems.");
  
  /* Check that both paths are files. */
  {
    int is_file;

    SVN_ERR (svn_fs_is_file (&is_file, root1, path1, pool));
    if (! is_file)
      return svn_error_createf
        (SVN_ERR_FS_GENERAL, 0, NULL, pool, "`%s' is not a file.", path1);
      
    SVN_ERR (svn_fs_is_file (&is_file, root2, path2, pool));
    if (! is_file)
      return svn_error_createf
        (SVN_ERR_FS_GENERAL, 0, NULL, pool, "`%s' is not a file.", path2);
  }

  args.root1      = root1;
  args.root2      = root2;
  args.path1      = path1;
  args.path2      = path2;
  args.changed_p  = changed_p;
  args.pool       = pool;

  SVN_ERR (svn_fs__retry_txn (root1->fs, txn_body_contents_changed,
                              &args, pool));

  return SVN_NO_ERROR;
}



/* Public interface to computing file text deltas.  */

/* Note:  it is acceptable for this function to call back into
   public FS API interfaces because it does not itself use trails.  */
svn_error_t *
svn_fs_get_file_delta_stream (svn_txdelta_stream_t **stream_p,
                              svn_fs_root_t *source_root,
                              const char *source_path,
                              svn_fs_root_t *target_root,
                              const char *target_path,
                              apr_pool_t *pool)
{
  svn_stream_t *source, *target;
  svn_txdelta_stream_t *delta_stream;

  /* Get read functions for the source file contents.  */
  if (source_root && source_path)
    SVN_ERR (svn_fs_file_contents (&source, source_root, source_path, pool));
  else
    source = svn_stream_empty (pool);

  /* Get read functions for the target file contents.  */
  SVN_ERR (svn_fs_file_contents (&target, target_root, target_path, pool));

  /* Create a delta stream that turns the ancestor into the target.  */
  svn_txdelta (&delta_stream, source, target, pool);

  *stream_p = delta_stream;
  return SVN_NO_ERROR;
}




/* Determining the revisions in which a given path was changed. */

struct revisions_changed_args
{
  apr_array_header_t **revs;
  svn_fs_t *fs;
  apr_array_header_t *ids;
  apr_pool_t *pool;
};

static svn_error_t *
txn_body_revisions_changed (void *baton, trail_t *trail)
{
  struct revisions_changed_args *args = baton;
  apr_array_header_t *array;
  int i;
  apr_pool_t *subpool = svn_pool_create(args->pool);
  svn_revnum_t prev_rev;

  /* Allocate an array for holding revision numbers. */
  array = apr_array_make (subpool, 4, sizeof (svn_revnum_t));

  /* Check the ID for each path */
  for (i = 0; i < args->ids->nelts; i++)
    {
      svn_fs_id_t *tmp_id = APR_ARRAY_IDX (args->ids, i, svn_fs_id_t *);

      /* Loop, from ID, through its predecessors, until it ceases to
         exist.  */
      do
        {
          svn_revnum_t revision;
          dag_node_t *node;
          int len = svn_fs__id_length (tmp_id);

          /* Get the dag node for this id. */
          SVN_ERR (svn_fs__dag_get_node (&node, args->fs, tmp_id, trail));

          /* Now get the revision from the dag. */
          SVN_ERR (svn_fs__dag_get_revision (&revision, node, trail));

          (*((svn_revnum_t *) apr_array_push (array))) = revision;

          /* Hack up TMP_ID so that it represents its own predecessor.
             Node IDs come in pairs, terminated by a trailing -1. So
             we process a pair until the ID gets down to zero, then
             mock up the -1 so we'll process the previous one.  */
        tmp_id->digits[len - 1]--;
        if (tmp_id->digits[len - 1] == 0)
          tmp_id->digits[len - 2] = -1;
        }
      while (tmp_id->digits[0] != -1);
    }

  /* Now sort the array */
  qsort (array->elts, array->nelts, array->elt_size, 
         svn_sort_compare_revisions);

  /* Now build the return array, removing duplicates along the way. */
  *(args->revs) = apr_array_make (args->pool, 4, sizeof (svn_revnum_t));
  prev_rev = SVN_INVALID_REVNUM;
  for (i = 0; i < array->nelts; i++)
    {
      if (APR_ARRAY_IDX (array, i, svn_revnum_t) != prev_rev)
        (*((svn_revnum_t *) apr_array_push (*(args->revs)))) =
            APR_ARRAY_IDX (array, i, svn_revnum_t);
      prev_rev = APR_ARRAY_IDX (array, i, svn_revnum_t);
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_revisions_changed (apr_array_header_t **revs,
                          svn_fs_root_t *root,
                          const apr_array_header_t *paths,
                          apr_pool_t *pool)
{
  struct revisions_changed_args args;
  svn_fs_t *fs = svn_fs_root_fs (root);
  int i;
  svn_fs_id_t *tmp_id;
  const char *this_path;
  apr_pool_t *subpool = svn_pool_create (pool);

  /* Populate the baton. */
  args.revs = revs;
  args.fs = fs;
  args.pool = pool;
  args.ids = apr_array_make (subpool, 1, sizeof (svn_fs_id_t *));

  /* Get the node-id for each PATH under ROOT. */
  for (i = 0; i < paths->nelts; i++)
    {
      this_path = APR_ARRAY_IDX(paths, i, const char *);
      SVN_ERR (svn_fs_node_id (&tmp_id, root, this_path, subpool));
      *((svn_fs_id_t **) apr_array_push (args.ids)) 
        = svn_fs__id_copy (tmp_id, subpool);
    }

  /* Call the real function under the umbrella of a trail. */
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_revisions_changed, &args, subpool));

  /* Destroy all memory used, except the revisions array */
  svn_pool_destroy(subpool);

  /* Return the array. */
  return SVN_NO_ERROR;
}




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
  SVN_ERR (svn_fs__get_txn_ids (&root_id, &base_root_id, fs, 
                                svn_txn_id, trail));

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


svn_error_t *svn_fs_txn_path_is_id (int *matches,
                                    svn_fs_root_t *txn_root,
                                    const char *path,
                                    const svn_fs_id_t *id,
                                    apr_pool_t *pool)
{
  svn_fs_id_t *res_id;

  /* get the ID of PATH within the TXN */
  SVN_ERR_W (svn_fs_node_id (&res_id, txn_root, path, pool),
             "could not fetch the node ID of the path within the"
             "transaction tree.");

  /* Assume the path/id match, unless we find out otherwise. */
  *matches = 1;

  if (!svn_fs__id_eq(res_id, id))
    {
      svn_boolean_t okay = FALSE;

      /* The provided ID does not match the ID of the node in the
         transaction tree. However, there is a special exception case:

         If the nodes are directories, and the transaction's node is
         an immediate child of the requested node, then we're okay.
         The issue is that we generate a child for the directory when
         it goes from an immutable node in a revision root, into a
         mutable node in the transaction root. We should not consider
         this a change, and there would be no way for the client to
         check out the transaction root's directory node anyway.

         Files do not have this behavior because they are not spuriously
         placed into the transaction root (directories are because of
         the bubble up algorithm). The only time a file will shift is
         when a true change is being made, and we will see that when the
         caller makes it happen.
      */
      if (svn_fs__id_is_parent (id, res_id))
        {
          int is_dir;
          svn_error_t *err;

          err = svn_fs_is_dir (&is_dir, txn_root, path, pool);
          if (err == NULL)
            {
              okay = is_dir;
            }
          else
            {
              /* ignore the error and go with the "not matching" msg */
              /* ### this needs to get changed... */
              /* svn_error_clear_all (err); */
            }
        }

      if (!okay)
        {
          *matches = 0;
        }
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
