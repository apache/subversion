/* tree.c : tree-like filesystem, built on DAG filesystem
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
#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_md5.h"
#include "svn_fs.h"
#include "svn_sorts.h"
#include "fs.h"
#include "err.h"
#include "trail.h"
#include "node-rev.h"
#include "key-gen.h"
#include "dag.h"
#include "tree.h"
#include "lock.h"
#include "revs-txns.h"
#include "id.h"
#include "bdb/txn-table.h"
#include "bdb/rev-table.h"
#include "bdb/nodes-table.h"
#include "bdb/changes-table.h"
#include "bdb/copies-table.h"
#include "../libsvn_fs/fs-loader.h"


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
#define SVN_FS_WRITE_BUFFER_SIZE          512000

/* The maximum number of cache items to maintain in the node cache. */
#define SVN_FS_NODE_CACHE_MAX_KEYS        32



/* The root structure.  */

/* Structure for svn_fs_root_t's node_cache hash values. */
struct dag_node_cache_t
{
  dag_node_t *node; /* NODE to be cached. */
  int idx;          /* Index into the keys array for this cache item's key. */
  apr_pool_t *pool; /* Pool in which NODE is allocated. */
};


typedef struct
{

  /* For revision roots, this is a dag node for the revision's root
     directory.  For transaction roots, we open the root directory
     afresh every time, since the root may have been cloned, or
     the transaction may have disappeared altogether.  */
  dag_node_t *root_dir;

  /* Cache structures, for mapping const char * PATH to const
     struct dag_node_cache_t * structures.

     ### Currently this is only used for revision roots.  To be safe
     for transaction roots, you must have the guarantee that there is
     never more than a single transaction root per Subversion
     transaction ever open at a given time -- having two roots open to
     the same Subversion transaction would be a request for pain.
     Also, you have to ensure that if a 'make_path_mutable()' fails for
     any reason, you don't leave cached nodes for the portion of that
     function that succeeded.  In other words, this cache must never,
     ever, lie. */
  apr_hash_t *node_cache;
  const char *node_cache_keys[SVN_FS_NODE_CACHE_MAX_KEYS];
  int node_cache_idx;
} base_root_data_t;


static svn_fs_root_t *make_revision_root(svn_fs_t *fs, svn_revnum_t rev,
                                         dag_node_t *root_dir,
                                         apr_pool_t *pool);

static svn_fs_root_t *make_txn_root(svn_fs_t *fs, const char *txn,
                                    apr_uint32_t flags, apr_pool_t *pool);


/*** Node Caching in the Roots. ***/

/* Return NODE for PATH from ROOT's node cache, or NULL if the node
   isn't cached. */
static dag_node_t *
dag_node_cache_get(svn_fs_root_t *root,
                   const char *path,
                   apr_pool_t *pool)
{
  base_root_data_t *brd = root->fsap_data;
  struct dag_node_cache_t *cache_item;

  /* Assert valid input. */
  assert(*path == '/');

  /* Only allow revision roots. */
  if (root->is_txn_root)
    return NULL;

  /* Look in the cache for our desired item. */
  cache_item = apr_hash_get(brd->node_cache, path, APR_HASH_KEY_STRING);
  if (cache_item)
    return svn_fs_base__dag_dup(cache_item->node, pool);

  return NULL;
}


/* Add the NODE for PATH to ROOT's node cache.  Callers should *NOT*
   call this unless they are adding a currently un-cached item to the
   cache, or are replacing the NODE for PATH with a new (different)
   one. */
static void
dag_node_cache_set(svn_fs_root_t *root,
                   const char *path,
                   dag_node_t *node)
{
  base_root_data_t *brd = root->fsap_data;
  const char *cache_path;
  apr_pool_t *cache_pool;
  struct dag_node_cache_t *cache_item;
  int num_keys = apr_hash_count(brd->node_cache);

  /* What?  No POOL passed to this function?

     To ensure that our cache values live as long as the svn_fs_root_t
     in which they are ultimately stored, and to allow us to free()
     them individually without harming the rest, they are each
     allocated from a subpool of ROOT's pool.  We'll keep one subpool
     around for each cache slot -- as we start expiring stuff
     to make room for more entries, we'll re-use the expired thing's
     pool. */

  /* Assert valid input and state. */
  assert(*path == '/');
  assert((brd->node_cache_idx <= num_keys)
         && (num_keys <= SVN_FS_NODE_CACHE_MAX_KEYS));

  /* Only allow revision roots. */
  if (root->is_txn_root)
    return;

  /* Special case: the caller wants us to replace an existing cached
     node with a new one.  If the callers aren't mindless, this should
     only happen when a node is made mutable under a transaction
     root, and that only happens once under that root.  So, we'll be a
     little bit sloppy here, and count on callers doing the right
     thing. */
  cache_item = apr_hash_get(brd->node_cache, path, APR_HASH_KEY_STRING);
  if (cache_item)
    {
      /* ### This section is somehow broken.  I don't know how, but it
         ### is.  And I don't want to spend any more time on it.  So,
         ### callers, use only revision root and don't try to update
         ### an already-cached thing.  -- cmpilato */
      abort();

#if 0
      int cache_index = cache_item->idx;
      cache_path = brd->node_cache_keys[cache_index];
      cache_pool = cache_item->pool;
      cache_item->node = svn_fs_base__dag_dup(node, cache_pool);

      /* Now, move the cache key reference to the end of the keys in
         the keys array (unless it's already at the end).  ### Yes,
         it's a memmove(), but we're not talking about pages of memory
         here. */
      if (cache_index != (num_keys - 1))
        {
          int move_num = SVN_FS_NODE_CACHE_MAX_KEYS - cache_index - 1;
          memmove(brd->node_cache_keys + cache_index,
                  brd->node_cache_keys + cache_index + 1,
                  move_num * sizeof(const char *));
          cache_index = num_keys - 1;
          brd->node_cache_keys[cache_index] = cache_path;
        }

      /* Advance the cache pointers. */
      cache_item->idx = cache_index;
      brd->node_cache_idx = (cache_index + 1) % SVN_FS_NODE_CACHE_MAX_KEYS;
      return;
#endif
    }

  /* We're adding a new cache item.  First, see if we have room for it
     (otherwise, make some room). */
  if (apr_hash_count(brd->node_cache) == SVN_FS_NODE_CACHE_MAX_KEYS)
    {
      /* No room.  Expire the oldest thing. */
      cache_path = brd->node_cache_keys[brd->node_cache_idx];
      cache_item = apr_hash_get(brd->node_cache, cache_path,
                                APR_HASH_KEY_STRING);
      apr_hash_set(brd->node_cache, cache_path, APR_HASH_KEY_STRING, NULL);
      cache_pool = cache_item->pool;
      svn_pool_clear(cache_pool);
    }
  else
    {
      cache_pool = svn_pool_create(root->pool);
    }

  /* Make the cache item, allocated in its own pool. */
  cache_item = apr_palloc(cache_pool, sizeof(*cache_item));
  cache_item->node = svn_fs_base__dag_dup(node, cache_pool);
  cache_item->idx = brd->node_cache_idx;
  cache_item->pool = cache_pool;

  /* Now add it to the cache. */
  cache_path = apr_pstrdup(cache_pool, path);
  apr_hash_set(brd->node_cache, cache_path, APR_HASH_KEY_STRING, cache_item);
  brd->node_cache_keys[brd->node_cache_idx] = cache_path;

  /* Advance the cache pointer. */
  brd->node_cache_idx = (brd->node_cache_idx + 1) % SVN_FS_NODE_CACHE_MAX_KEYS;
}




/* Creating transaction and revision root nodes.  */

struct txn_root_args
{
  svn_fs_root_t **root_p;
  svn_fs_txn_t *txn;
};


static svn_error_t *
txn_body_txn_root(void *baton,
                  trail_t *trail)
{
  struct txn_root_args *args = baton;
  svn_fs_root_t **root_p = args->root_p;
  svn_fs_txn_t *txn = args->txn;
  svn_fs_t *fs = txn->fs;
  const char *svn_txn_id = txn->id;
  const svn_fs_id_t *root_id, *base_root_id;
  svn_fs_root_t *root;
  apr_hash_t *txnprops;
  apr_uint32_t flags = 0;

  /* Verify that the transaction actually exists.  */
  SVN_ERR(svn_fs_base__get_txn_ids(&root_id, &base_root_id, fs,
                                   svn_txn_id, trail, trail->pool));

  /* Look for special txn props that represent the 'flags' behavior of
     the transaction. */
  SVN_ERR(svn_fs_base__txn_proplist_in_trail(&txnprops, svn_txn_id, trail));
  if (txnprops)
    {
      if (apr_hash_get(txnprops, SVN_FS_PROP_TXN_CHECK_OOD,
                       APR_HASH_KEY_STRING))
        flags |= SVN_FS_TXN_CHECK_OOD;

      if (apr_hash_get(txnprops, SVN_FS_PROP_TXN_CHECK_LOCKS,
                       APR_HASH_KEY_STRING))
        flags |= SVN_FS_TXN_CHECK_LOCKS;
    }

  root = make_txn_root(fs, svn_txn_id, flags, trail->pool);

  *root_p = root;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__txn_root(svn_fs_root_t **root_p,
                      svn_fs_txn_t *txn,
                      apr_pool_t *pool)
{
  svn_fs_root_t *root;
  struct txn_root_args args;

  args.root_p = &root;
  args.txn = txn;
  SVN_ERR(svn_fs_base__retry_txn(txn->fs, txn_body_txn_root, &args, pool));

  *root_p = root;
  return SVN_NO_ERROR;
}


struct revision_root_args
{
  svn_fs_root_t **root_p;
  svn_revnum_t rev;
};


static svn_error_t *
txn_body_revision_root(void *baton,
                       trail_t *trail)
{
  struct revision_root_args *args = baton;
  dag_node_t *root_dir;
  svn_fs_root_t *root;

  SVN_ERR(svn_fs_base__dag_revision_root(&root_dir, trail->fs, args->rev,
                                         trail, trail->pool));
  root = make_revision_root(trail->fs, args->rev, root_dir, trail->pool);

  *args->root_p = root;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__revision_root(svn_fs_root_t **root_p,
                           svn_fs_t *fs,
                           svn_revnum_t rev,
                           apr_pool_t *pool)
{
  struct revision_root_args args;
  svn_fs_root_t *root;

  SVN_ERR(svn_fs_base__check_fs(fs));

  args.root_p = &root;
  args.rev = rev;
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_revision_root, &args, pool));

  *root_p = root;
  return SVN_NO_ERROR;
}



/* Constructing nice error messages for roots.  */

/* Build an SVN_ERR_FS_NOT_FOUND error, with a detailed error text,
   for PATH in ROOT. */
#define NOT_FOUND(r, p) (                                \
  r->is_txn_root ?                                       \
    svn_error_createf                                    \
      (SVN_ERR_FS_NOT_FOUND, 0,                          \
       _("File not found: transaction '%s', path '%s'"), \
       r->txn, p)                                        \
  :                                                      \
    svn_error_createf                                    \
      (SVN_ERR_FS_NOT_FOUND, 0,                          \
       _("File not found: revision %ld, path '%s'"),     \
       r->rev, p)                                        \
  )


/* Build a detailed `file already exists' message for PATH in ROOT.  */
#define ALREADY_EXISTS(r, p) (                                                 \
  r->is_txn_root ?                                                             \
    svn_error_createf                                                          \
      (SVN_ERR_FS_ALREADY_EXISTS, 0,                                           \
       _("File already exists: filesystem '%s', transaction '%s', path '%s'"), \
       r->fs->path, r->txn, p)                                                 \
  :                                                                            \
    svn_error_createf                                                          \
      (SVN_ERR_FS_ALREADY_EXISTS, 0,                                           \
       _("File already exists: filesystem '%s', revision %ld, path '%s'"),     \
       r->fs->path, r->rev, p)                                                 \
  )


#define NOT_TXN(r)                                 \
  svn_error_create                                 \
    (SVN_ERR_FS_NOT_TXN_ROOT, NULL,                \
     _("Root object must be a transaction root"))



/* Getting dag nodes for roots.  */


/* Set *NODE_P to a freshly opened dag node referring to the root
   directory of ROOT, as part of TRAIL.  */
static svn_error_t *
root_node(dag_node_t **node_p,
          svn_fs_root_t *root,
          trail_t *trail,
          apr_pool_t *pool)
{
  base_root_data_t *brd = root->fsap_data;

  if (! root->is_txn_root)
    {
      /* It's a revision root, so we already have its root directory
         opened.  */
      *node_p = svn_fs_base__dag_dup(brd->root_dir, pool);
      return SVN_NO_ERROR;
    }
  else
    {
      /* It's a transaction root.  Open a fresh copy.  */
      return svn_fs_base__dag_txn_root(node_p, root->fs, root->txn, 
                                       trail, pool);
    }
}


/* Set *NODE_P to a mutable root directory for ROOT, cloning if
   necessary, as part of TRAIL.  ROOT must be a transaction root.  Use
   ERROR_PATH in error messages.  */
static svn_error_t *
mutable_root_node(dag_node_t **node_p,
                  svn_fs_root_t *root,
                  const char *error_path,
                  trail_t *trail,
                  apr_pool_t *pool)
{
  if (root->is_txn_root)
    return svn_fs_base__dag_clone_root(node_p, root->fs, root->txn, 
                                       trail, pool);
  else
    /* If it's not a transaction root, we can't change its contents.  */
    return svn_fs_base__err_not_mutable(root->fs, root->rev, error_path);
}



/* Traversing directory paths.  */

typedef enum copy_id_inherit_t
{
  copy_id_inherit_unknown = 0,
  copy_id_inherit_self,
  copy_id_inherit_parent,
  copy_id_inherit_new

} copy_id_inherit_t;

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

  /* The copy ID inheritence style. */
  copy_id_inherit_t copy_inherit;

  /* If copy ID inheritence style is copy_id_inherit_new, this is the
     path which should be implicitly copied; otherwise, this is NULL. */
  const char *copy_src_path;

} parent_path_t;


static const char *
parent_path_path(parent_path_t *parent_path,
                 apr_pool_t *pool)
{
  const char *path_so_far = "/";
  if (parent_path->parent)
    path_so_far = parent_path_path(parent_path->parent, pool);
  return parent_path->entry
    ? svn_path_join(path_so_far, parent_path->entry, pool)
         : path_so_far;
}


/* Choose a copy ID inheritance method *INHERIT_P to be used in the
   event that immutable node CHILD in FS needs to be made mutable.  If
   the inheritance method is copy_id_inherit_new, also return a
   *COPY_SRC_PATH on which to base the new copy ID (else return NULL
   for that path).  CHILD must have a parent (it cannot be the root
   node).  TXN_ID is the transaction in which these items might be
   mutable.  */
static svn_error_t *
get_copy_inheritance(copy_id_inherit_t *inherit_p,
                     const char **copy_src_path,
                     svn_fs_t *fs,
                     parent_path_t *child,
                     const char *txn_id,
                     trail_t *trail,
                     apr_pool_t *pool)
{
  const svn_fs_id_t *child_id, *parent_id;
  const char *child_copy_id, *parent_copy_id;
  const char *id_path = NULL;

  /* Make some assertions about the function input. */
  assert(child && child->parent && txn_id);

  /* Initialize our return variables (default: self-inheritance). */
  *inherit_p = copy_id_inherit_self;
  *copy_src_path = NULL;

  /* Initialize some convenience variables. */
  child_id = svn_fs_base__dag_get_id(child->node);
  parent_id = svn_fs_base__dag_get_id(child->parent->node);
  child_copy_id = svn_fs_base__id_copy_id(child_id);
  parent_copy_id = svn_fs_base__id_copy_id(parent_id);

  /* Easy out: if this child is already mutable, we have nothing to do. */
  if (svn_fs_base__key_compare(svn_fs_base__id_txn_id(child_id), txn_id) == 0)
    return SVN_NO_ERROR;

  /* If the child and its parent are on the same branch, then the
     child will inherit the copy ID of its parent when made mutable.
     This is trivially detectable when the child and its parent have
     the same copy ID.  But that's not the sole indicator of
     same-branchness.  It might be the case that the parent was the
     result of a copy, but the child has not yet been cloned for
     mutability since that copy.  Detection of this latter case
     basically means making sure the copy IDs don't differ for some
     other reason, such as that the child was the direct target of the
     copy whose ID it has.  There is a special case here, too -- if
     the child's copy ID is the special ID "0", it can't have been the
     target of any copy, and therefore must be on the same branch as
     its parent.  */
  if ((strcmp(child_copy_id, "0") == 0)
      || (svn_fs_base__key_compare(child_copy_id, parent_copy_id) == 0))
    {
      *inherit_p = copy_id_inherit_parent;
      return SVN_NO_ERROR;
    }
  else
    {
      copy_t *copy;
      SVN_ERR(svn_fs_bdb__get_copy(&copy, fs, child_copy_id, trail, pool));
      if (svn_fs_base__id_compare(copy->dst_noderev_id, child_id) == -1)
        {
          *inherit_p = copy_id_inherit_parent;
          return SVN_NO_ERROR;
        }
    }

  /* If we get here, the child and its parent are not on speaking
     terms -- there will be no parental inheritence handed down in
     *this* generation. */

  /* If the child was created at a different path than the one we are
     expecting its clone to live, one of its parents must have been
     created via a copy since the child was created.  The child isn't
     on the same branch as its parent (we caught those cases early);
     it can't keep its current copy ID because there's been an
     affecting copy (its clone won't be on the same branch as the
     child is).  That leaves only one course of action -- to assign
     the child a brand new "soft" copy ID. */
  id_path = svn_fs_base__dag_get_created_path(child->node);
  if (strcmp(id_path, parent_path_path(child, pool)) != 0)
    {
      *inherit_p = copy_id_inherit_new;
      *copy_src_path = id_path;
      return SVN_NO_ERROR;
    }

  /* The node gets to keep its own ID. */
  return SVN_NO_ERROR;
}


/* Allocate a new parent_path_t node from POOL, referring to NODE,
   ENTRY, PARENT, and COPY_ID.  */
static parent_path_t *
make_parent_path(dag_node_t *node,
                 char *entry,
                 parent_path_t *parent,
                 apr_pool_t *pool)
{
  parent_path_t *parent_path = apr_pcalloc(pool, sizeof(*parent_path));
  parent_path->node = node;
  parent_path->entry = entry;
  parent_path->parent = parent;
  parent_path->copy_inherit = copy_id_inherit_unknown;
  parent_path->copy_src_path = NULL;
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
next_entry_name(const char **next_p,
                const char *path,
                apr_pool_t *pool)
{
  const char *end;

  /* Find the end of the current component.  */
  end = strchr(path, '/');

  if (! end)
    {
      /* The path contains only one component, with no trailing
         slashes.  */
      *next_p = 0;
      return apr_pstrdup(pool, path);
    }
  else
    {
      /* There's a slash after the first component.  Skip over an arbitrary
         number of slashes to find the next one.  */
      const char *next = end;
      while (*next == '/')
        next++;
      *next_p = next;
      return apr_pstrndup(pool, path, end - path);
    }
}


/* Flags for open_path.  */
typedef enum open_path_flags_t {

  /* The last component of the PATH need not exist.  (All parent
     directories must exist, as usual.)  If the last component doesn't
     exist, simply leave the `node' member of the bottom parent_path
     component zero.  */
  open_path_last_optional = 1

} open_path_flags_t;


/* Open the node identified by PATH in ROOT, as part of TRAIL.  Set
   *PARENT_PATH_P to a path from the node up to ROOT, allocated in
   TRAIL->pool.  The resulting *PARENT_PATH_P value is guaranteed to
   contain at least one element, for the root directory.

   If resulting *PARENT_PATH_P will eventually be made mutable and
   modified, or if copy ID inheritance information is otherwise
   needed, TXN_ID should be the ID of the mutability transaction.  If
   TXN_ID is NULL, no copy ID in heritance information will be
   calculated for the *PARENT_PATH_P chain.

   If FLAGS & open_path_last_optional is zero, return the error
   SVN_ERR_FS_NOT_FOUND if the node PATH refers to does not exist.  If
   non-zero, require all the parent directories to exist as normal,
   but if the final path component doesn't exist, simply return a path
   whose bottom `node' member is zero.  This option is useful for
   callers that create new nodes --- we find the parent directory for
   them, and tell them whether the entry exists already.

   NOTE: Public interfaces which only *read* from the filesystem
   should not call this function directly, but should instead use
   get_dag().
*/
static svn_error_t *
open_path(parent_path_t **parent_path_p,
          svn_fs_root_t *root,
          const char *path,
          int flags,
          const char *txn_id,
          trail_t *trail,
          apr_pool_t *pool)
{
  svn_fs_t *fs = root->fs;
  const svn_fs_id_t *id;
  dag_node_t *here; /* The directory we're currently looking at.  */
  parent_path_t *parent_path; /* The path from HERE up to the root.  */
  const char *rest; /* The portion of PATH we haven't traversed yet.  */
  const char *canon_path = svn_fs_base__canonicalize_abspath(path, pool);
  const char *path_so_far = "/";

  /* Make a parent_path item for the root node, using its own current
     copy id.  */
  SVN_ERR(root_node(&here, root, trail, pool));
  id = svn_fs_base__dag_get_id(here);
  parent_path = make_parent_path(here, 0, 0, pool);
  parent_path->copy_inherit = copy_id_inherit_self;

  rest = canon_path + 1; /* skip the leading '/', it saves in iteration */

  /* Whenever we are at the top of this loop:
     - HERE is our current directory,
     - ID is the node revision ID of HERE,
     - REST is the path we're going to find in HERE, and
     - PARENT_PATH includes HERE and all its parents.  */
  for (;;)
    {
      const char *next;
      char *entry;
      dag_node_t *child;

      /* Parse out the next entry from the path.  */
      entry = next_entry_name(&next, rest, pool);

      /* Calculate the path traversed thus far. */
      path_so_far = svn_path_join(path_so_far, entry, pool);

      if (*entry == '\0')
        {
          /* Given the behavior of next_entry_name, this happens when
             the path either starts or ends with a slash.  In either
             case, we stay put: the current directory stays the same,
             and we add nothing to the parent path.  */
          child = here;
        }
      else
        {
          copy_id_inherit_t inherit;
          const char *copy_path = NULL;
          svn_error_t *err = SVN_NO_ERROR;
          dag_node_t *cached_node;

          /* If we found a directory entry, follow it.  First, we
             check our node cache, and, failing that, we hit the DAG
             layer. */
          cached_node = dag_node_cache_get(root, path_so_far, pool);
          if (cached_node)
            child = cached_node;
          else
            err = svn_fs_base__dag_open(&child, here, entry, trail, pool);

          /* "file not found" requires special handling.  */
          if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
            {
              /* If this was the last path component, and the caller
                 said it was optional, then don't return an error;
                 just put a NULL node pointer in the path.  */

              svn_error_clear(err);

              if ((flags & open_path_last_optional)
                  && (! next || *next == '\0'))
                {
                  parent_path = make_parent_path(NULL, entry, parent_path,
                                                 pool);
                  break;
                }
              else
                {
                  /* Build a better error message than svn_fs_base__dag_open
                     can provide, giving the root and full path name.  */
                  return NOT_FOUND(root, path);
                }
            }

          /* Other errors we return normally.  */
          SVN_ERR(err);

          /* Now, make a parent_path item for CHILD. */
          parent_path = make_parent_path(child, entry, parent_path, pool);
          if (txn_id)
            {
              SVN_ERR(get_copy_inheritance(&inherit, &copy_path,
                                           fs, parent_path, txn_id, 
                                           trail, pool));
              parent_path->copy_inherit = inherit;
              parent_path->copy_src_path = apr_pstrdup(pool, copy_path);
            }

          /* Cache the node we found (if it wasn't already cached). */
          if (! cached_node)
            dag_node_cache_set(root, path_so_far, child);
        }

      /* Are we finished traversing the path?  */
      if (! next)
        break;

      /* The path isn't finished yet; we'd better be in a directory.  */
      if (svn_fs_base__dag_node_kind(child) != svn_node_dir)
        SVN_ERR_W(svn_fs_base__err_not_directory(fs, path_so_far),
                  apr_psprintf(pool, _("Failure opening '%s'"), path));

      rest = next;
      here = child;
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
make_path_mutable(svn_fs_root_t *root,
                  parent_path_t *parent_path,
                  const char *error_path,
                  trail_t *trail,
                  apr_pool_t *pool)
{
  dag_node_t *clone;
  const char *txn_id = root->txn;
  svn_fs_t *fs = root->fs;

  /* Is the node mutable already?  */
  if (svn_fs_base__dag_check_mutable(parent_path->node, txn_id))
    return SVN_NO_ERROR;

  /* Are we trying to clone the root, or somebody's child node?  */
  if (parent_path->parent)
    {
      const svn_fs_id_t *parent_id;
      const svn_fs_id_t *node_id = svn_fs_base__dag_get_id(parent_path->node);
      const char *copy_id = NULL;
      const char *copy_src_path = parent_path->copy_src_path;
      copy_id_inherit_t inherit = parent_path->copy_inherit;
      const char *clone_path;

      /* We're trying to clone somebody's child.  Make sure our parent
         is mutable.  */
      SVN_ERR(make_path_mutable(root, parent_path->parent,
                                error_path, trail, pool));

      switch (inherit)
        {
        case copy_id_inherit_parent:
          parent_id = svn_fs_base__dag_get_id(parent_path->parent->node);
          copy_id = svn_fs_base__id_copy_id(parent_id);
          break;

        case copy_id_inherit_new:
          SVN_ERR(svn_fs_bdb__reserve_copy_id(&copy_id, fs, trail, pool));
          break;

        case copy_id_inherit_self:
          copy_id = NULL;
          break;

        case copy_id_inherit_unknown:
        default:
          abort(); /* uh-oh -- somebody didn't calculate copy-ID
                      inheritance data. */
        }

      /* Now make this node mutable.  */
      clone_path = parent_path_path(parent_path->parent, pool);
      SVN_ERR(svn_fs_base__dag_clone_child(&clone,
                                           parent_path->parent->node,
                                           clone_path,
                                           parent_path->entry,
                                           copy_id, txn_id,
                                           trail, pool));

      /* If we just created a brand new copy ID, we need to store a
         `copies' table entry for it, as well as a notation in the
         transaction that should this transaction be terminated, our
         new copy needs to be removed. */
      if (inherit == copy_id_inherit_new)
        {
          const svn_fs_id_t *new_node_id = svn_fs_base__dag_get_id(clone);
          SVN_ERR(svn_fs_bdb__create_copy(fs, copy_id, copy_src_path,
                                          svn_fs_base__id_txn_id(node_id),
                                          new_node_id,
                                          copy_kind_soft, trail, pool));
          SVN_ERR(svn_fs_base__add_txn_copy(fs, txn_id, copy_id, 
                                            trail, pool));
        }
    }
  else
    {
      /* We're trying to clone the root directory.  */
      SVN_ERR(mutable_root_node(&clone, root, error_path, trail, pool));
    }

  /* Update the PARENT_PATH link to refer to the clone.  */
  parent_path->node = clone;

  return SVN_NO_ERROR;
}


/* Open the node identified by PATH in ROOT, as part of TRAIL.  Set
   *DAG_NODE_P to the node we find, allocated in TRAIL->pool.  Return
   the error SVN_ERR_FS_NOT_FOUND if this node doesn't exist. */
static svn_error_t *
get_dag(dag_node_t **dag_node_p,
        svn_fs_root_t *root,
        const char *path,
        trail_t *trail,
        apr_pool_t *pool)
{
  parent_path_t *parent_path;
  dag_node_t *node = NULL;

  /* Canonicalize the input PATH. */
  path = svn_fs_base__canonicalize_abspath(path, pool);

  /* If ROOT is a revision root, we'll look for the DAG in our cache. */
  node = dag_node_cache_get(root, path, pool);
  if (! node)
    {
      /* Call open_path with no flags, as we want this to return an error
         if the node for which we are searching doesn't exist. */
      SVN_ERR(open_path(&parent_path, root, path, 0, NULL, trail, pool));
      node = parent_path->node;

      /* No need to cache our find -- open_path() will do that for us. */
    }

  *dag_node_p = node;
  return SVN_NO_ERROR;
}



/* Populating the `changes' table. */

/* Add a change to the changes table in FS, keyed on transaction id
   TXN_ID, and indicated that a change of kind CHANGE_KIND occurred on
   PATH (whose node revision id is--or was, in the case of a
   deletion--NODEREV_ID), and optionally that TEXT_MODs or PROP_MODs
   occurred.  Do all this as part of TRAIL.  */
static svn_error_t *
add_change(svn_fs_t *fs,
           const char *txn_id,
           const char *path,
           const svn_fs_id_t *noderev_id,
           svn_fs_path_change_kind_t change_kind,
           svn_boolean_t text_mod,
           svn_boolean_t prop_mod,
           trail_t *trail,
           apr_pool_t *pool)
{
  change_t change;
  change.path = svn_fs_base__canonicalize_abspath(path, pool);
  change.noderev_id = noderev_id;
  change.kind = change_kind;
  change.text_mod = text_mod;
  change.prop_mod = prop_mod;
  return svn_fs_bdb__changes_add(fs, txn_id, &change, trail, pool);
}



/* Generic node operations.  */


struct node_id_args {
  const svn_fs_id_t **id_p;
  svn_fs_root_t *root;
  const char *path;
};


static svn_error_t *
txn_body_node_id(void *baton, trail_t *trail)
{
  struct node_id_args *args = baton;
  dag_node_t *node;

  SVN_ERR(get_dag(&node, args->root, args->path, trail, trail->pool));
  *args->id_p = svn_fs_base__id_copy(svn_fs_base__dag_get_id(node),
                                     trail->pool);

  return SVN_NO_ERROR;
}


static svn_error_t *
base_node_id(const svn_fs_id_t **id_p,
             svn_fs_root_t *root,
             const char *path,
             apr_pool_t *pool)
{
  base_root_data_t *brd = root->fsap_data;

  if (! root->is_txn_root
      && (path[0] == '\0' || ((path[0] == '/') && (path[1] == '\0'))))
    {
      /* Optimize the case where we don't need any db access at all.
         The root directory ("" or "/") node is stored in the
         svn_fs_root_t object, and never changes when it's a revision
         root, so we can just reach in and grab it directly. */
      *id_p = svn_fs_base__id_copy(svn_fs_base__dag_get_id(brd->root_dir),
                                   pool);
    }
  else
    {
      const svn_fs_id_t *id;
      struct node_id_args args;

      args.id_p = &id;
      args.root = root;
      args.path = path;

      SVN_ERR(svn_fs_base__retry_txn(root->fs, txn_body_node_id, &args,
                                     pool));
      *id_p = id;
    }
  return SVN_NO_ERROR;
}


struct node_created_rev_args {
  svn_revnum_t revision;
  svn_fs_root_t *root;
  const char *path;
};


static svn_error_t *
txn_body_node_created_rev(void *baton, trail_t *trail)
{
  struct node_created_rev_args *args = baton;
  dag_node_t *node;

  SVN_ERR(get_dag(&node, args->root, args->path, trail, trail->pool));
  SVN_ERR(svn_fs_base__dag_get_revision(&(args->revision), node, 
                                        trail, trail->pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
base_node_created_rev(svn_revnum_t *revision,
                      svn_fs_root_t *root,
                      const char *path,
                      apr_pool_t *pool)
{
  struct node_created_rev_args args;

  args.revision = SVN_INVALID_REVNUM;
  args.root = root;
  args.path = path;
  SVN_ERR(svn_fs_base__retry_txn
          (root->fs, txn_body_node_created_rev, &args, pool));
  *revision = args.revision;
  return SVN_NO_ERROR;
}


struct node_created_path_args {
  const char **created_path;
  svn_fs_root_t *root;
  const char *path;
};


static svn_error_t *
txn_body_node_created_path(void *baton, trail_t *trail)
{
  struct node_created_path_args *args = baton;
  dag_node_t *node;

  SVN_ERR(get_dag(&node, args->root, args->path, trail, trail->pool));
  *args->created_path = svn_fs_base__dag_get_created_path(node);
  return SVN_NO_ERROR;
}


static svn_error_t *
base_node_created_path(const char **created_path,
                       svn_fs_root_t *root,
                       const char *path,
                       apr_pool_t *pool)
{
  struct node_created_path_args args;

  args.created_path = created_path;
  args.root = root;
  args.path = path;
  SVN_ERR(svn_fs_base__retry_txn
          (root->fs, txn_body_node_created_path, &args, pool));
  return SVN_NO_ERROR;
}


struct node_kind_args {
  const svn_fs_id_t *id;
  svn_node_kind_t kind; /* OUT parameter */
};


static svn_error_t *
txn_body_node_kind(void *baton, trail_t *trail)
{
  struct node_kind_args *args = baton;
  dag_node_t *node;

  SVN_ERR(svn_fs_base__dag_get_node(&node, trail->fs, args->id, 
                                    trail, trail->pool));
  args->kind = svn_fs_base__dag_node_kind(node);

  return SVN_NO_ERROR;
}


static svn_error_t *
node_kind(svn_node_kind_t *kind_p,
          svn_fs_root_t *root,
          const char *path,
          apr_pool_t *pool)
{
  struct node_kind_args args;
  const svn_fs_id_t *node_id;

  /* Get the node id. */
  SVN_ERR(base_node_id(&node_id, root, path, pool));

  /* Use the node id to get the real kind. */
  args.id = node_id;
  SVN_ERR(svn_fs_base__retry_txn(root->fs, txn_body_node_kind, &args, pool));

  *kind_p = args.kind;
  return SVN_NO_ERROR;
}


static svn_error_t *
base_check_path(svn_node_kind_t *kind_p,
                svn_fs_root_t *root,
                const char *path,
                apr_pool_t *pool)
{
  svn_error_t *err = node_kind(kind_p, root, path, pool);
  if (err && (err->apr_err == SVN_ERR_FS_NOT_FOUND))
    {
      svn_error_clear(err);
      *kind_p = svn_node_none;
    }
  else if (err)
    {
      return err;
    }
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
txn_body_node_prop(void *baton,
                   trail_t *trail)
{
  struct node_prop_args *args = baton;
  dag_node_t *node;
  apr_hash_t *proplist;

  SVN_ERR(get_dag(&node, args->root, args->path, trail, trail->pool));
  SVN_ERR(svn_fs_base__dag_get_proplist(&proplist, node, 
                                        trail, trail->pool));
  *(args->value_p) = NULL;
  if (proplist)
    *(args->value_p) = apr_hash_get(proplist, args->propname,
                                    APR_HASH_KEY_STRING);
  return SVN_NO_ERROR;
}


static svn_error_t *
base_node_prop(svn_string_t **value_p,
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
  SVN_ERR(svn_fs_base__retry_txn(root->fs, txn_body_node_prop, &args, pool));

  *value_p = value;
  return SVN_NO_ERROR;
}


struct node_proplist_args {
  apr_hash_t **table_p;
  svn_fs_root_t *root;
  const char *path;
};


static svn_error_t *
txn_body_node_proplist(void *baton, trail_t *trail)
{
  struct node_proplist_args *args = baton;
  dag_node_t *node;
  apr_hash_t *proplist;

  SVN_ERR(get_dag(&node, args->root, args->path, trail, trail->pool));
  SVN_ERR(svn_fs_base__dag_get_proplist(&proplist, node, 
                                        trail, trail->pool));
  *args->table_p = proplist ? proplist : apr_hash_make(trail->pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
base_node_proplist(apr_hash_t **table_p,
                   svn_fs_root_t *root,
                   const char *path,
                   apr_pool_t *pool)
{
  apr_hash_t *table;
  struct node_proplist_args args;

  args.table_p = &table;
  args.root = root;
  args.path = path;

  SVN_ERR(svn_fs_base__retry_txn(root->fs, txn_body_node_proplist, &args,
                                 pool));

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
txn_body_change_node_prop(void *baton,
                          trail_t *trail)
{
  struct change_node_prop_args *args = baton;
  parent_path_t *parent_path;
  apr_hash_t *proplist;
  const char *txn_id = args->root->txn;

  SVN_ERR(open_path(&parent_path, args->root, args->path, 0, txn_id, 
                    trail, trail->pool));

  /* Check to see if path is locked; if so, check that we can use it.
     Notice that we're doing this non-recursively, regardless of node kind. */
  if (args->root->txn_flags & SVN_FS_TXN_CHECK_LOCKS)
    SVN_ERR(svn_fs_base__allow_locked_operation 
            (args->path, FALSE, trail, trail->pool));

  SVN_ERR(make_path_mutable(args->root, parent_path, args->path, 
                            trail, trail->pool));
  SVN_ERR(svn_fs_base__dag_get_proplist(&proplist, parent_path->node,
                                        trail, trail->pool));

  /* If there's no proplist, but we're just deleting a property, exit now. */
  if ((! proplist) && (! args->value))
    return SVN_NO_ERROR;

  /* Now, if there's no proplist, we know we need to make one. */
  if (! proplist)
    proplist = apr_hash_make(trail->pool);

  /* Set the property. */
  apr_hash_set(proplist, args->name, APR_HASH_KEY_STRING, args->value);

  /* Overwrite the node's proplist. */
  SVN_ERR(svn_fs_base__dag_set_proplist(parent_path->node, proplist,
                                        txn_id, trail, trail->pool));

  /* Make a record of this modification in the changes table. */
  SVN_ERR(add_change(args->root->fs, txn_id,
                     args->path, svn_fs_base__dag_get_id(parent_path->node),
                     svn_fs_path_change_modify, 0, 1, trail, trail->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
base_change_node_prop(svn_fs_root_t *root,
                      const char *path,
                      const char *name,
                      const svn_string_t *value,
                      apr_pool_t *pool)
{
  struct change_node_prop_args args;

  if (! root->is_txn_root)
    return NOT_TXN(root);

  args.root  = root;
  args.path  = path;
  args.name  = name;
  args.value = value;
  SVN_ERR(svn_fs_base__retry_txn(root->fs, txn_body_change_node_prop, &args,
                                 pool));

  return SVN_NO_ERROR;
}


struct things_changed_args
{
  svn_boolean_t *changed_p;
  svn_fs_root_t *root1;
  svn_fs_root_t *root2;
  const char *path1;
  const char *path2;
  apr_pool_t *pool;
};


static svn_error_t *
txn_body_props_changed(void *baton, trail_t *trail)
{
  struct things_changed_args *args = baton;
  dag_node_t *node1, *node2;

  SVN_ERR(get_dag(&node1, args->root1, args->path1, trail, trail->pool));
  SVN_ERR(get_dag(&node2, args->root2, args->path2, trail, trail->pool));
  SVN_ERR(svn_fs_base__things_different(args->changed_p, NULL,
                                        node1, node2, trail, trail->pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
base_props_changed(svn_boolean_t *changed_p,
                   svn_fs_root_t *root1,
                   const char *path1,
                   svn_fs_root_t *root2,
                   const char *path2,
                   apr_pool_t *pool)
{
  struct things_changed_args args;

  /* Check that roots are in the same fs. */
  if (root1->fs != root2->fs)
    return svn_error_create
      (SVN_ERR_FS_GENERAL, NULL,
       _("Cannot compare property value between two different filesystems"));

  args.root1      = root1;
  args.root2      = root2;
  args.path1      = path1;
  args.path2      = path2;
  args.changed_p  = changed_p;
  args.pool       = pool;

  SVN_ERR(svn_fs_base__retry_txn(root1->fs, txn_body_props_changed,
                                 &args, pool));

  return SVN_NO_ERROR;
}



/* Getting a directory's entries */


struct dir_entries_args
{
  apr_hash_t **table_p;
  svn_fs_root_t *root;
  const char *path;
};


static svn_error_t *
txn_body_dir_entries(void *baton,
                     trail_t *trail)
{
  struct dir_entries_args *args = baton;
  dag_node_t *node;
  apr_hash_t *entries;

  SVN_ERR(get_dag(&node, args->root, args->path, trail, trail->pool));

  /* Get the entries for PARENT_PATH. */
  SVN_ERR(svn_fs_base__dag_dir_entries(&entries, node, trail, trail->pool));

  /* Potentially initialize the return value to an empty hash. */
  *args->table_p = entries ? entries : apr_hash_make(trail->pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
base_dir_entries(apr_hash_t **table_p,
                 svn_fs_root_t *root,
                 const char *path,
                 apr_pool_t *pool)
{
  struct dir_entries_args args;
  apr_hash_t *table;
  svn_fs_t *fs = root->fs;

  args.table_p = &table;
  args.root    = root;
  args.path    = path;
  SVN_ERR(svn_fs_base__retry_txn(root->fs, txn_body_dir_entries, &args,
                                 pool));

  /* Add in the kind data. */
  if (table)
    {
      apr_hash_index_t *hi;
      apr_pool_t *subpool = svn_pool_create(pool);
      for (hi = apr_hash_first(subpool, table); hi; hi = apr_hash_next(hi))
        {
          svn_fs_dirent_t *entry;
          struct node_kind_args nk_args;
          void *val;

          /* KEY will be the entry name in ancestor (about which we
             simple don't care), VAL the dirent. */
          apr_hash_this(hi, NULL, NULL, &val);
          entry = val;
          nk_args.id = entry->id;
          SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_node_kind, &nk_args,
                                         pool));
          entry->kind = nk_args.kind;
        }
    }
  else
    {
      table = apr_hash_make(pool);
    }

  *table_p = table;
  return SVN_NO_ERROR;
}



/* Merges and commits. */


struct deltify_committed_args
{
  svn_fs_t *fs; /* the filesystem */
  svn_revnum_t rev; /* revision just committed */
  const char *txn_id; /* transaction just committed */
};


struct txn_deltify_args
{
  /* The target is what we're deltifying. */
  const svn_fs_id_t *tgt_id;

  /* The base is what we're deltifying against.  It's not necessarily
     the "next" revision of the node; skip deltas mean we sometimes
     deltify against a successor many generations away. */
  const svn_fs_id_t *base_id;

  /* We only deltify props for directories.
     ### Didn't we try removing this horrid little optimization once?
     ### What was the result?  I would have thought that skip deltas
     ### mean directory undeltification is cheap enough now. */
  svn_boolean_t is_dir;
};


static svn_error_t *
txn_body_txn_deltify(void *baton, trail_t *trail)
{
  struct txn_deltify_args *args = baton;
  dag_node_t *tgt_node, *base_node;

  SVN_ERR(svn_fs_base__dag_get_node(&tgt_node, trail->fs, args->tgt_id,
                                    trail, trail->pool));
  SVN_ERR(svn_fs_base__dag_get_node(&base_node, trail->fs, args->base_id,
                                    trail, trail->pool));
  SVN_ERR(svn_fs_base__dag_deltify(tgt_node, base_node, args->is_dir,
                                   trail, trail->pool));

  return SVN_NO_ERROR;
}


struct txn_pred_count_args
{
  const svn_fs_id_t *id;
  int pred_count;
};


static svn_error_t *
txn_body_pred_count(void *baton, trail_t *trail)
{
  node_revision_t *noderev;
  struct txn_pred_count_args *args = baton;

  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, trail->fs,
                                        args->id, trail, trail->pool));
  args->pred_count = noderev->predecessor_count;
  return SVN_NO_ERROR;
}


struct txn_pred_id_args
{
  const svn_fs_id_t *id;      /* The node id of for we want the predecessor. */
  const svn_fs_id_t *pred_id; /* The returned predecessor id. */
  apr_pool_t *pool;           /* The pool in which to allocate pred_id. */
};


static svn_error_t *
txn_body_pred_id(void *baton, trail_t *trail)
{
  node_revision_t *nr;
  struct txn_pred_id_args *args = baton;

  SVN_ERR(svn_fs_bdb__get_node_revision(&nr, trail->fs, args->id, 
                                        trail, trail->pool));
  if (nr->predecessor_id)
    args->pred_id = svn_fs_base__id_copy(nr->predecessor_id, args->pool);
  else
    args->pred_id = NULL;

  return SVN_NO_ERROR;
}


/* Deltify ID's predecessor iff ID is mutable under TXN_ID in FS.  If
   ID is a mutable directory, recurse.  Do this as part of TRAIL. */
static svn_error_t *
deltify_mutable(svn_fs_t *fs,
                svn_fs_root_t *root,
                const char *path,
                const char *txn_id,
                apr_pool_t *pool)
{
  const svn_fs_id_t *id;
  apr_hash_t *entries = NULL;
  svn_node_kind_t kind;
  struct txn_deltify_args td_args;

  /* Get the ID for PATH under ROOT. */
  SVN_ERR(base_node_id(&id, root, path, pool));

  /* Check for mutability.  Not mutable?  Go no further.  This is safe
     to do because for items in the tree to be mutable, their parent
     dirs must also be mutable.  Therefore, if a directory is not
     mutable under TXN_ID, its children cannot be.  */
  if (strcmp(svn_fs_base__id_txn_id(id), txn_id))
    return SVN_NO_ERROR;

  /* Is this a directory?  */
  SVN_ERR(base_check_path(&kind, root, path, pool));

  /* If this is a directory, read its entries.  */
  if (kind == svn_node_dir)
    SVN_ERR(base_dir_entries(&entries, root, path, pool));

  /* If there are entries, recurse on 'em.  */
  if (entries)
    {
      apr_pool_t *subpool = svn_pool_create(pool);
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
        {
          /* KEY will be the entry name, VAL the dirent (about
             which we really don't care) */
          const void *key;
          svn_pool_clear(subpool);
          apr_hash_this(hi, &key, NULL, NULL);
          SVN_ERR(deltify_mutable(fs, root,
                                  svn_path_join(path, key, subpool),
                                  txn_id, subpool));
        }

      svn_pool_destroy(subpool);
    }

  /* Finally, deltify old data against this node. */
  {
    /* Redeltify predecessor node-revisions of the one we added.  The
       idea is to require at most 2*lg(N) deltas to be applied to get
       to any node-revision in a chain of N predecessors.  We do this
       using a technique derived from skip lists:

          - Always redeltify the immediate parent

          - If the number of predecessors is divisible by 2,
              redeltify the revision two predecessors back

          - If the number of predecessors is divisible by 4,
              redeltify the revision four predecessors back

       ... and so on.

       That's the theory, anyway.  Unfortunately, if we strictly
       follow that theory we get a bunch of overhead up front and no
       great benefit until the number of predecessors gets large.  So,
       stop at redeltifying the parent if the number of predecessors
       is less than 32, and also skip the second level (redeltifying
       two predecessors back), since that doesn't help much.  Also,
       don't redeltify the oldest node-revision; it's potentially
       expensive and doesn't help retrieve any other revision.
       (Retrieving the oldest node-revision will still be fast, just
       not as blindingly so.)  */

    int pred_count, nlevels, lev, count;
    const svn_fs_id_t *pred_id;
    struct txn_pred_count_args tpc_args;
    apr_pool_t *subpools[2];
    int active_subpool = 0;

    tpc_args.id = id;
    SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_pred_count, &tpc_args,
                                   pool));
    pred_count = tpc_args.pred_count;

    /* If nothing to deltify, then we're done. */
    if (pred_count == 0)
      return SVN_NO_ERROR;

    /* Decide how many predecessors to redeltify.  To save overhead,
       don't redeltify anything but the immediate predecessor if there
       are less than 32 predecessors. */
    nlevels = 1;
    if (pred_count >= 32)
      {
        while (pred_count % 2 == 0)
          {
            pred_count /= 2;
            nlevels++;
          }

        /* Don't redeltify the oldest revision. */
        if (1 << (nlevels - 1) == pred_count)
          nlevels--;
      }

    /* Redeltify the desired number of predecessors. */
    count = 0;
    pred_id = id;

    /* We need to use two alternating pools because the id used in the
       call to txn_body_pred_id is allocated by the previous inner
       loop iteration.  If we would clear the pool each iteration we
       would free the previous result.  */
    subpools[0] = svn_pool_create(pool);
    subpools[1] = svn_pool_create(pool);
    for (lev = 0; lev < nlevels; lev++)
      {
        /* To save overhead, skip the second level (that is, never
           redeltify the node-revision two predecessors back). */
        if (lev == 1)
          continue;

        /* Note that COUNT is not reset between levels, and neither is
           PREDNODE; we just keep counting from where we were up to
           where we're supposed to get. */
        while (count < (1 << lev))
          {
            struct txn_pred_id_args tpi_args;

            active_subpool = !active_subpool;
            svn_pool_clear(subpools[active_subpool]);

            tpi_args.id = pred_id;
            tpi_args.pool = subpools[active_subpool];
            SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_pred_id, &tpi_args,
                                           subpools[active_subpool]));
            pred_id = tpi_args.pred_id;

            if (pred_id == NULL)
              return svn_error_create
                (SVN_ERR_FS_CORRUPT, 0,
                 _("Corrupt DB: faulty predecessor count"));

            count++;
          }

        /* Finally, do the deltification. */
        td_args.tgt_id = pred_id;
        td_args.base_id = id;
        td_args.is_dir = (kind == svn_node_dir);
        SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_txn_deltify, &td_args,
                                       subpools[active_subpool]));

      }
    svn_pool_destroy(subpools[0]);
    svn_pool_destroy(subpools[1]);
  }

  return SVN_NO_ERROR;
}


struct get_root_args
{
  svn_fs_root_t *root;
  dag_node_t *node;
};


/* Set ARGS->node to the root node of ARGS->root.  */
static svn_error_t *
txn_body_get_root(void *baton, trail_t *trail)
{
  struct get_root_args *args = baton;
  SVN_ERR(get_dag(&(args->node), args->root, "", trail, trail->pool));
  return SVN_NO_ERROR;
}



static svn_error_t *
update_ancestry(svn_fs_t *fs,
                const svn_fs_id_t *source_id,
                const svn_fs_id_t *target_id,
                const char *txn_id,
                const char *target_path,
                int source_pred_count,
                trail_t *trail,
                apr_pool_t *pool)
{
  node_revision_t *noderev;

  /* Set target's predecessor-id to source_id.  */
  if (strcmp(svn_fs_base__id_txn_id(target_id), txn_id))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Unexpected immutable node at '%s'"), target_path);
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, target_id, 
                                        trail, pool));
  noderev->predecessor_id = source_id;
  noderev->predecessor_count = source_pred_count;
  if (noderev->predecessor_count != -1)
    noderev->predecessor_count++;
  return svn_fs_bdb__put_node_revision(fs, target_id, noderev, trail, pool);
}


/* Set the contents of CONFLICT_PATH to PATH, and return an
   SVN_ERR_FS_CONFLICT error that indicates that there was a conflict
   at PATH.  Perform all allocations in POOL (except the allocation of
   CONFLICT_PATH, which should be handled outside this function).  */
static svn_error_t *
conflict_err(svn_stringbuf_t *conflict_path,
             const char *path)
{
  svn_stringbuf_set(conflict_path, path);
  return svn_error_createf(SVN_ERR_FS_CONFLICT, NULL,
                           _("Conflict at '%s'"), path);
}


/* Merge changes between ANCESTOR and SOURCE into TARGET as part of
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
 * SVN_ERR_FS_CONFLICT error, and updates CONFLICT_P to the name of the
 * conflicting node in TARGET, with TARGET_PATH prepended as a path.
 *
 * If there are no conflicting differences, CONFLICT_P is updated to
 * the empty string.
 *
 * CONFLICT_P must point to a valid svn_stringbuf_t.
 *
 * Do any necessary temporary allocation in POOL.
 */
static svn_error_t *
merge(svn_stringbuf_t *conflict_p,
      const char *target_path,
      dag_node_t *target,
      dag_node_t *source,
      dag_node_t *ancestor,
      const char *txn_id,
      trail_t *trail,
      apr_pool_t *pool)
{
  const svn_fs_id_t *source_id, *target_id, *ancestor_id;
  apr_hash_t *s_entries, *t_entries, *a_entries;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;
  svn_fs_t *fs;
  int pred_count;

  /* Make sure everyone comes from the same filesystem. */
  fs = svn_fs_base__dag_get_fs(ancestor);
  if ((fs != svn_fs_base__dag_get_fs(source))
      || (fs != svn_fs_base__dag_get_fs(target)))
    {
      return svn_error_create
        (SVN_ERR_FS_CORRUPT, NULL,
         _("Bad merge; ancestor, source, and target not all in same fs"));
    }

  /* We have the same fs, now check it. */
  SVN_ERR(svn_fs_base__check_fs(fs));

  source_id   = svn_fs_base__dag_get_id(source);
  target_id   = svn_fs_base__dag_get_id(target);
  ancestor_id = svn_fs_base__dag_get_id(ancestor);

  /* It's improper to call this function with ancestor == target. */
  if (svn_fs_base__id_eq(ancestor_id, target_id))
    {
      svn_string_t *id_str = svn_fs_base__id_unparse(target_id, pool);
      return svn_error_createf
        (SVN_ERR_FS_GENERAL, NULL,
         _("Bad merge; target '%s' has id '%s', same as ancestor"),
         target_path, id_str->data);
    }

  svn_stringbuf_setempty(conflict_p);

  /* Base cases:
   * Either no change made in source, or same change as made in target.
   * Both mean nothing to merge here.
   */
  if (svn_fs_base__id_eq(ancestor_id, source_id)
      || (svn_fs_base__id_eq(source_id, target_id)))
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
   * For each entry NAME in the directory ANCESTOR:
   *
   *   Let ANCESTOR-ENTRY, SOURCE-ENTRY, and TARGET-ENTRY be the IDs of
   *   the name within ANCESTOR, SOURCE, and TARGET respectively.
   *   (Possibly null if NAME does not exist in SOURCE or TARGET.)
   *
   *   If ANCESTOR-ENTRY == SOURCE-ENTRY, then:
   *     No changes were made to this entry while the transaction was in
   *     progress, so do nothing to the target.
   *
   *   Else if ANCESTOR-ENTRY == TARGET-ENTRY, then:
   *     A change was made to this entry while the transaction was in
   *     process, but the transaction did not touch this entry.  Replace
   *     TARGET-ENTRY with SOURCE-ENTRY.
   *
   *   Else:
   *     Changes were made to this entry both within the transaction and
   *     to the repository while the transaction was in progress.  They
   *     must be merged or declared to be in conflict.
   *
   *     If SOURCE-ENTRY and TARGET-ENTRY are both null, that's a
   *     double delete; flag a conflict.
   *
   *     If any of the three entries is of type file, declare a conflict.
   *
   *     If either SOURCE-ENTRY or TARGET-ENTRY is not a direct
   *     modification of ANCESTOR-ENTRY (determine by comparing the
   *     node-id fields), declare a conflict.  A replacement is
   *     incompatible with a modification or other replacement--even
   *     an identical replacement.
   *
   *     Direct modifications were made to the directory ANCESTOR-ENTRY
   *     in both SOURCE and TARGET.  Recursively merge these
   *     modifications.
   *
   * For each leftover entry NAME in the directory SOURCE:
   *
   *   If NAME exists in TARGET, declare a conflict.  Even if SOURCE and
   *   TARGET are adding exactly the same thing, two additions are not
   *   auto-mergeable with each other.
   *
   *   Add NAME to TARGET with the entry from SOURCE.
   *
   * Now that we are done merging the changes from SOURCE into the
   * directory TARGET, update TARGET's predecessor to be SOURCE.
   */

  if ((svn_fs_base__dag_node_kind(source) != svn_node_dir)
      || (svn_fs_base__dag_node_kind(target) != svn_node_dir)
      || (svn_fs_base__dag_node_kind(ancestor) != svn_node_dir))
    {
      return conflict_err(conflict_p, target_path);
    }


  /* Possible early merge failure: if target and ancestor have
     different property lists, then the merge should fail.
     Propchanges can *only* be committed on an up-to-date directory.
     ### TODO: see issue #418 about the inelegance of this. 

     Another possible, similar, early merge failure: if source and
     ancestor have different property lists (meaning someone else
     changed directory properties while our commit transaction was
     happening), the merge should fail.  See issue #2751.
  */
  {
    node_revision_t *tgt_nr, *anc_nr, *src_nr;

    /* Get node revisions for our id's. */
    SVN_ERR(svn_fs_bdb__get_node_revision(&tgt_nr, fs, target_id, 
                                          trail, pool));
    SVN_ERR(svn_fs_bdb__get_node_revision(&anc_nr, fs, ancestor_id, 
                                          trail, pool));
    SVN_ERR(svn_fs_bdb__get_node_revision(&src_nr, fs, source_id, 
                                          trail, pool));

    /* Now compare the prop-keys of the skels.  Note that just because
       the keys are different -doesn't- mean the proplists have
       different contents.  But merge() isn't concerned with contents;
       it doesn't do a brute-force comparison on textual contents, so
       it won't do that here either.  Checking to see if the propkey
       atoms are `equal' is enough. */
    if (! svn_fs_base__same_keys(tgt_nr->prop_key, anc_nr->prop_key))
      return conflict_err(conflict_p, target_path);
    if (! svn_fs_base__same_keys(src_nr->prop_key, anc_nr->prop_key))
      return conflict_err(conflict_p, target_path);
  }

  /* ### todo: it would be more efficient to simply check for a NULL
     entries hash where necessary below than to allocate an empty hash
     here, but another day, another day... */
  SVN_ERR(svn_fs_base__dag_dir_entries(&s_entries, source, trail, pool));
  if (! s_entries)
    s_entries = apr_hash_make(pool);
  SVN_ERR(svn_fs_base__dag_dir_entries(&t_entries, target, trail, pool));
  if (! t_entries)
    t_entries = apr_hash_make(pool);
  SVN_ERR(svn_fs_base__dag_dir_entries(&a_entries, ancestor, trail, pool));
  if (! a_entries)
    a_entries = apr_hash_make(pool);

  /* for each entry E in a_entries... */
  iterpool = svn_pool_create(pool);
  for (hi = apr_hash_first(pool, a_entries);
       hi;
       hi = apr_hash_next(hi))
    {
      svn_fs_dirent_t *s_entry, *t_entry, *a_entry;

      const void *key;
      void *val;
      apr_ssize_t klen;

      svn_pool_clear(iterpool);

      /* KEY will be the entry name in ancestor, VAL the dirent */
      apr_hash_this(hi, &key, &klen, &val);
      a_entry = val;

      s_entry = apr_hash_get(s_entries, key, klen);
      t_entry = apr_hash_get(t_entries, key, klen);

      /* No changes were made to this entry while the transaction was
         in progress, so do nothing to the target. */
      if (s_entry && svn_fs_base__id_eq(a_entry->id, s_entry->id))
        goto end;

      /* A change was made to this entry while the transaction was in
         process, but the transaction did not touch this entry. */
      else if (t_entry && svn_fs_base__id_eq(a_entry->id, t_entry->id))
        {
          if (s_entry)
            {
              SVN_ERR(svn_fs_base__dag_set_entry(target, key, s_entry->id,
                                                 txn_id, trail, iterpool));
            }
          else
            {
              SVN_ERR(svn_fs_base__dag_delete(target, key, txn_id, 
                                              trail, iterpool));
            }
        }

      /* Changes were made to this entry both within the transaction
         and to the repository while the transaction was in progress.
         They must be merged or declared to be in conflict. */
      else
        {
          dag_node_t *s_ent_node, *t_ent_node, *a_ent_node;
          const char *new_tpath;

          /* If SOURCE-ENTRY and TARGET-ENTRY are both null, that's a
             double delete; flag a conflict. */
          if (s_entry == NULL || t_entry == NULL)
            return conflict_err(conflict_p,
                                svn_path_join(target_path,
                                              a_entry->name,
                                              iterpool));

          /* If either SOURCE-ENTRY or TARGET-ENTRY is not a direct
             modification of ANCESTOR-ENTRY, declare a conflict. */
          if (strcmp(svn_fs_base__id_node_id(s_entry->id),
                     svn_fs_base__id_node_id(a_entry->id)) != 0
              || strcmp(svn_fs_base__id_copy_id(s_entry->id),
                        svn_fs_base__id_copy_id(a_entry->id)) != 0
              || strcmp(svn_fs_base__id_node_id(t_entry->id),
                        svn_fs_base__id_node_id(a_entry->id)) != 0
              || strcmp(svn_fs_base__id_copy_id(t_entry->id),
                        svn_fs_base__id_copy_id(a_entry->id)) != 0)
            return conflict_err(conflict_p,
                                svn_path_join(target_path,
                                              a_entry->name,
                                              iterpool));

          /* Fetch the nodes for our entries. */
          SVN_ERR(svn_fs_base__dag_get_node(&s_ent_node, fs,
                                            s_entry->id, trail, iterpool));
          SVN_ERR(svn_fs_base__dag_get_node(&t_ent_node, fs,
                                            t_entry->id, trail, iterpool));
          SVN_ERR(svn_fs_base__dag_get_node(&a_ent_node, fs,
                                            a_entry->id, trail, iterpool));

          /* If any of the three entries is of type file, flag a conflict. */
          if ((svn_fs_base__dag_node_kind(s_ent_node) == svn_node_file)
              || (svn_fs_base__dag_node_kind(t_ent_node) == svn_node_file)
              || (svn_fs_base__dag_node_kind(a_ent_node) == svn_node_file))
            return conflict_err(conflict_p,
                                svn_path_join(target_path,
                                              a_entry->name,
                                              iterpool));

          /* Direct modifications were made to the directory
             ANCESTOR-ENTRY in both SOURCE and TARGET.  Recursively
             merge these modifications. */
          new_tpath = svn_path_join(target_path, t_entry->name, iterpool);
          SVN_ERR(merge(conflict_p, new_tpath,
                        t_ent_node, s_ent_node, a_ent_node,
                        txn_id, trail, iterpool));
        }

      /* We've taken care of any possible implications E could have.
         Remove it from source_entries, so it's easy later to loop
         over all the source entries that didn't exist in
         ancestor_entries. */
    end:
      apr_hash_set(s_entries, key, klen, NULL);
    }

  /* For each entry E in source but not in ancestor */
  for (hi = apr_hash_first(pool, s_entries); 
       hi; 
       hi = apr_hash_next(hi))
    {
      svn_fs_dirent_t *s_entry, *t_entry;
      const void *key;
      void *val;
      apr_ssize_t klen;

      svn_pool_clear(iterpool);

      apr_hash_this(hi, &key, &klen, &val);
      s_entry = val;
      t_entry = apr_hash_get(t_entries, key, klen);

      /* If NAME exists in TARGET, declare a conflict. */
      if (t_entry)
        return conflict_err(conflict_p,
                            svn_path_join(target_path,
                                          t_entry->name,
                                          iterpool));

      SVN_ERR(svn_fs_base__dag_set_entry
              (target, s_entry->name, s_entry->id, txn_id, trail, iterpool));
    }
  svn_pool_destroy(iterpool);

  /* Now that TARGET has absorbed all of the history between ANCESTOR
     and SOURCE, we can update its predecessor to point to SOURCE.  */
  SVN_ERR(svn_fs_base__dag_get_predecessor_count(&pred_count, source, 
                                                 trail, pool));
  SVN_ERR(update_ancestry(fs, source_id, target_id, txn_id, target_path,
                          pred_count, trail, pool));

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

  /* If a conflict results, this is updated to the path in the txn that
     conflicted.  It must point to a valid svn_stringbuf_t before calling
     svn_fs_base__retry_txn, as this determines the pool used to allocate any
     required memory. */
  svn_stringbuf_t *conflict;
};


/* Merge changes between an ancestor and BATON->source_node into
   BATON->txn.  The ancestor is either BATON->ancestor_node, or if
   that is null, BATON->txn's base node.

   If the merge is successful, BATON->txn's base will become
   BATON->source_node, and its root node will have a new ID, a
   successor of BATON->source_node. */
static svn_error_t *
txn_body_merge(void *baton, trail_t *trail)
{
  struct merge_args *args = baton;
  dag_node_t *source_node, *txn_root_node, *ancestor_node;
  const svn_fs_id_t *source_id;
  svn_fs_t *fs = args->txn->fs;
  const char *txn_id = args->txn->id;

  source_node = args->source_node;
  ancestor_node = args->ancestor_node;
  source_id = svn_fs_base__dag_get_id(source_node);

  SVN_ERR(svn_fs_base__dag_txn_root(&txn_root_node, fs, txn_id, 
                                    trail, trail->pool));

  if (ancestor_node == NULL)
    {
      SVN_ERR(svn_fs_base__dag_txn_base_root(&ancestor_node, fs,
                                             txn_id, trail, trail->pool));
    }

  if (svn_fs_base__id_eq(svn_fs_base__dag_get_id(ancestor_node),
                         svn_fs_base__dag_get_id(txn_root_node)))
    {
      /* If no changes have been made in TXN since its current base,
         then it can't conflict with any changes since that base.  So
         we just set *both* its base and root to source, making TXN
         in effect a repeat of source. */

      /* ### kff todo: this would, of course, be a mighty silly thing
         for the caller to do, and we might want to consider whether
         this response is really appropriate. */

      SVN_ERR(svn_fs_base__set_txn_base(fs, txn_id, source_id, 
                                        trail, trail->pool));
      SVN_ERR(svn_fs_base__set_txn_root(fs, txn_id, source_id, 
                                        trail, trail->pool));
    }
  else
    {
      int pred_count;

      SVN_ERR(merge(args->conflict, "/", txn_root_node,
                    source_node, ancestor_node, txn_id, trail, trail->pool));

      SVN_ERR(svn_fs_base__dag_get_predecessor_count(&pred_count,
                                                     source_node, trail, 
                                                     trail->pool));

      /* After the merge, txn's new "ancestor" is now really the node
         at source_id, so record that fact.  Think of this as
         ratcheting the txn forward in time, so it can't backslide and
         forget the merging work that's already been done. */
      SVN_ERR(update_ancestry(fs, source_id,
                              svn_fs_base__dag_get_id(txn_root_node),
                              txn_id, "/", pred_count, trail, trail->pool));
      SVN_ERR(svn_fs_base__set_txn_base(fs, txn_id, source_id, 
                                        trail, trail->pool));
    }

  return SVN_NO_ERROR;
}


/* Verify that there are registered with TRAIL->fs all the locks
   necessary to permit all the changes associated with TXN_NAME. */
static svn_error_t *
verify_locks(const char *txn_name,
             trail_t *trail,
             apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *changes;
  apr_hash_index_t *hi;
  apr_array_header_t *changed_paths;
  svn_stringbuf_t *last_recursed = NULL;
  int i;

  /* Fetch the changes for this transaction. */
  SVN_ERR(svn_fs_bdb__changes_fetch(&changes, trail->fs, txn_name, 
                                    trail, pool));

  /* Make an array of the changed paths, and sort them depth-first-ily.  */
  changed_paths = apr_array_make(pool, apr_hash_count(changes) + 1, 
                                 sizeof(const char *));
  for (hi = apr_hash_first(pool, changes); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_hash_this(hi, &key, NULL, NULL);
      APR_ARRAY_PUSH(changed_paths, const char *) = key;
    }
  qsort(changed_paths->elts, changed_paths->nelts,
        changed_paths->elt_size, svn_sort_compare_paths);
  
  /* Now, traverse the array of changed paths, verify locks.  Note
     that if we need to do a recursive verification a path, we'll skip
     over children of that path when we get to them. */
  for (i = 0; i < changed_paths->nelts; i++)
    {
      const char *path;
      svn_fs_path_change_t *change;
      svn_boolean_t recurse = TRUE;

      svn_pool_clear(subpool);
      path = APR_ARRAY_IDX(changed_paths, i, const char *);

      /* If this path has already been verified as part of a recursive
         check of one of its parents, no need to do it again.  */
      if (last_recursed 
          && svn_path_is_child(last_recursed->data, path, subpool))
        continue;

      /* Fetch the change associated with our path.  */
      change = apr_hash_get(changes, path, APR_HASH_KEY_STRING);

      /* What does it mean to succeed at lock verification for a given
         path?  For an existing file or directory getting modified
         (text, props), it means we hold the lock on the file or
         directory.  For paths being added or removed, we need to hold
         the locks for that path and any children of that path.

         WHEW!  We have no reliable way to determine the node kind of
         deleted items, but fortunately we are going to do a recursive
         check on deleted paths regardless of their kind.  */
      if (change->change_kind == svn_fs_path_change_modify)
        recurse = FALSE;
      SVN_ERR(svn_fs_base__allow_locked_operation(path, recurse,
                                                  trail, subpool));

      /* If we just did a recursive check, remember the path we
         checked (so children can be skipped).  */
      if (recurse)
        {
          if (! last_recursed)
            last_recursed = svn_stringbuf_create(path, pool);
          else
            svn_stringbuf_set(last_recursed, path);
        }
    }
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


struct commit_args
{
  svn_fs_txn_t *txn;
  svn_revnum_t new_rev;
};


/* Commit ARGS->txn, setting ARGS->new_rev to the resulting new
 * revision, if ARGS->txn is up-to-date with respect to the repository.
 *
 * Up-to-date means that ARGS->txn's base root is the same as the root
 * of the youngest revision.  If ARGS->txn is not up-to-date, the
 * error SVN_ERR_FS_TXN_OUT_OF_DATE is returned, and the commit fails: no
 * new revision is created, and ARGS->new_rev is not touched.
 *
 * If the commit succeeds, ARGS->txn is destroyed.
 */
static svn_error_t *
txn_body_commit(void *baton, trail_t *trail)
{
  struct commit_args *args = baton;

  svn_fs_txn_t *txn = args->txn;
  svn_fs_t *fs = txn->fs;
  const char *txn_name = txn->id;

  svn_revnum_t youngest_rev;
  const svn_fs_id_t *y_rev_root_id;
  dag_node_t *txn_base_root_node;

  /* Getting the youngest revision locks the revisions table until
     this trail is done. */
  SVN_ERR(svn_fs_bdb__youngest_rev(&youngest_rev, fs, trail, trail->pool));

  /* If the root of the youngest revision is the same as txn's base,
     then no further merging is necessary and we can commit. */
  SVN_ERR(svn_fs_base__rev_get_root(&y_rev_root_id, fs, youngest_rev,
                                    trail, trail->pool));
  SVN_ERR(svn_fs_base__dag_txn_base_root(&txn_base_root_node, fs, txn_name,
                                         trail, trail->pool));
  /* ### kff todo: it seems weird to grab the ID for one, and the node
     for the other.  We can certainly do the comparison we need, but
     it would be nice to grab the same type of information from the
     start, instead of having to transform one of them. */
  if (! svn_fs_base__id_eq(y_rev_root_id,
                           svn_fs_base__dag_get_id(txn_base_root_node)))
    {
      svn_string_t *id_str = svn_fs_base__id_unparse(y_rev_root_id,
                                                     trail->pool);
      return svn_error_createf
        (SVN_ERR_FS_TXN_OUT_OF_DATE, NULL,
         _("Transaction '%s' out of date with respect to revision '%s'"),
         txn_name, id_str->data);
    }

  /* Locks may have been added (or stolen) between the calling of
     previous svn_fs.h functions and svn_fs_commit_txn(), so we need
     to re-examine every changed-path in the txn and re-verify all
     discovered locks. */
  SVN_ERR(verify_locks(txn_name, trail, trail->pool));

  /* Else, commit the txn. */
  SVN_ERR(svn_fs_base__dag_commit_txn(&(args->new_rev), fs, txn_name,
                                      trail, trail->pool));

  return SVN_NO_ERROR;
}


/* Note:  it is acceptable for this function to call back into
   top-level FS interfaces because it does not itself use trails.  */
svn_error_t *
svn_fs_base__commit_txn(const char **conflict_p,
                        svn_revnum_t *new_rev,
                        svn_fs_txn_t *txn,
                        apr_pool_t *pool)
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
  svn_fs_t *fs = txn->fs;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Initialize output params. */
  *new_rev = SVN_INVALID_REVNUM;
  if (conflict_p)
    *conflict_p = NULL;

  while (1729)
    {
      struct get_root_args get_root_args;
      struct merge_args merge_args;
      struct commit_args commit_args;
      svn_revnum_t youngish_rev;
      svn_fs_root_t *youngish_root;
      dag_node_t *youngish_root_node;

      svn_pool_clear(subpool);

      /* Get the *current* youngest revision, in one short-lived
         Berkeley transaction.  (We don't want the revisions table
         locked while we do the main merge.)  We call it "youngish"
         because new revisions might get committed after we've
         obtained it. */

      SVN_ERR(svn_fs_base__youngest_rev(&youngish_rev, fs, subpool));
      SVN_ERR(svn_fs_base__revision_root(&youngish_root, fs, youngish_rev,
                                         subpool));

      /* Get the dag node for the youngest revision, also in one
         Berkeley transaction.  Later we'll use it as the SOURCE
         argument to a merge, and if the merge succeeds, this youngest
         root node will become the new base root for the svn txn that
         was the target of the merge (but note that the youngest rev
         may have changed by then -- that's why we're careful to get
         this root in its own bdb txn here). */
      get_root_args.root = youngish_root;
      SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_get_root,
                                     &get_root_args, subpool));
      youngish_root_node = get_root_args.node;

      /* Try to merge.  If the merge succeeds, the base root node of
         TARGET's txn will become the same as youngish_root_node, so
         any future merges will only be between that node and whatever
         the root node of the youngest rev is by then. */
      merge_args.ancestor_node = NULL;
      merge_args.source_node = youngish_root_node;
      merge_args.txn = txn;
      merge_args.conflict = svn_stringbuf_create("", pool); /* use pool */
      err = svn_fs_base__retry_txn(fs, txn_body_merge, &merge_args, subpool);
      if (err)
        {
          if ((err->apr_err == SVN_ERR_FS_CONFLICT) && conflict_p)
            *conflict_p = merge_args.conflict->data;
          return err;
        }

      /* Try to commit. */
      commit_args.txn = txn;
      err = svn_fs_base__retry_txn(fs, txn_body_commit, &commit_args, 
                                   subpool);
      if (err && (err->apr_err == SVN_ERR_FS_TXN_OUT_OF_DATE))
        {
          /* Did someone else finish committing a new revision while we
             were in mid-merge or mid-commit?  If so, we'll need to
             loop again to merge the new changes in, then try to
             commit again.  Or if that's not what happened, then just
             return the error. */
          svn_revnum_t youngest_rev;
          svn_error_t *err2 = svn_fs_base__youngest_rev(&youngest_rev, fs,
                                                        subpool);
          if (err2)
            {
              svn_error_clear(err);
              return err2;  /* err2 is bad, it should not occur */
            }
          else if (youngest_rev == youngish_rev)
            return err;
          else
            svn_error_clear(err);
        }
      else if (err)
        {
          return err;
        }
      else
        {
          /* Set the return value -- our brand spankin' new revision! */
          *new_rev = commit_args.new_rev;
          break;
        }
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


/* Note:  it is acceptable for this function to call back into
   public FS API interfaces because it does not itself use trails.  */
static svn_error_t *
base_merge(const char **conflict_p,
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

  if (! target_root->is_txn_root)
    return NOT_TXN(target_root);

  /* Paranoia. */
  fs = ancestor_root->fs;
  if ((source_root->fs != fs) || (target_root->fs != fs))
    {
      return svn_error_create
        (SVN_ERR_FS_CORRUPT, NULL,
         _("Bad merge; ancestor, source, and target not all in same fs"));
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
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_get_root, &get_root_args,
                                 pool));
  ancestor = get_root_args.node;

  /* Get the source node. */
  get_root_args.root = source_root;
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_get_root, &get_root_args,
                                 pool));
  source = get_root_args.node;

  /* Open a txn for the txn root into which we're merging. */
  SVN_ERR(svn_fs_base__open_txn(&txn, fs, target_root->txn, pool));

  /* Merge changes between ANCESTOR and SOURCE into TXN. */
  merge_args.source_node = source;
  merge_args.ancestor_node = ancestor;
  merge_args.txn = txn;
  merge_args.conflict = svn_stringbuf_create("", pool);
  err = svn_fs_base__retry_txn(fs, txn_body_merge, &merge_args, pool);
  if (err)
    {
      if ((err->apr_err == SVN_ERR_FS_CONFLICT) && conflict_p)
        *conflict_p = merge_args.conflict->data;
      return err;
    }

  return SVN_NO_ERROR;
}


struct rev_get_txn_id_args
{
  const char **txn_id;
  svn_revnum_t revision;
};


static svn_error_t *
txn_body_rev_get_txn_id(void *baton, trail_t *trail)
{
  struct rev_get_txn_id_args *args = baton;
  return svn_fs_base__rev_get_txn_id(args->txn_id, trail->fs,
                                     args->revision, trail, trail->pool);
}


svn_error_t *
svn_fs_base__deltify(svn_fs_t *fs,
                     svn_revnum_t revision,
                     apr_pool_t *pool)
{
  svn_fs_root_t *root;
  const char *txn_id;
  struct rev_get_txn_id_args args;

  SVN_ERR(svn_fs_base__revision_root(&root, fs, revision, pool));

  args.txn_id = &txn_id;
  args.revision = revision;
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_rev_get_txn_id, &args, pool));

  return deltify_mutable(fs, root, "/", txn_id, pool);
}



/* Modifying directories */


struct make_dir_args
{
  svn_fs_root_t *root;
  const char *path;
};


static svn_error_t *
txn_body_make_dir(void *baton,
                  trail_t *trail)
{
  struct make_dir_args *args = baton;
  svn_fs_root_t *root = args->root;
  const char *path = args->path;
  parent_path_t *parent_path;
  dag_node_t *sub_dir;
  const char *txn_id = root->txn;

  SVN_ERR(open_path(&parent_path, root, path, open_path_last_optional,
                    txn_id, trail, trail->pool));

  /* If there's already a sub-directory by that name, complain.  This
     also catches the case of trying to make a subdirectory named `/'.  */
  if (parent_path->node)
    return ALREADY_EXISTS(root, path);

  /* Check to see if some lock is 'reserving' a file-path or dir-path
     at that location, or even some child-path;  if so, check that we
     can use it. */
  if (args->root->txn_flags & SVN_FS_TXN_CHECK_LOCKS)
    {
      SVN_ERR(svn_fs_base__allow_locked_operation(path, TRUE,
                                                  trail, trail->pool));
    }

  /* Create the subdirectory.  */
  SVN_ERR(make_path_mutable(root, parent_path->parent, path, 
                            trail, trail->pool));
  SVN_ERR(svn_fs_base__dag_make_dir(&sub_dir,
                                    parent_path->parent->node,
                                    parent_path_path(parent_path->parent,
                                                     trail->pool),
                                    parent_path->entry,
                                    txn_id,
                                    trail, trail->pool));

  /* Make a record of this modification in the changes table. */
  SVN_ERR(add_change(root->fs, txn_id, path,
                     svn_fs_base__dag_get_id(sub_dir),
                     svn_fs_path_change_add, 0, 0, trail, trail->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
base_make_dir(svn_fs_root_t *root,
              const char *path,
              apr_pool_t *pool)
{
  struct make_dir_args args;

  if (! root->is_txn_root)
    return NOT_TXN(root);

  args.root = root;
  args.path = path;
  return svn_fs_base__retry_txn(root->fs, txn_body_make_dir, &args, pool);
}


struct delete_args
{
  svn_fs_root_t *root;
  const char *path;
};


/* If this returns SVN_ERR_FS_NO_SUCH_ENTRY, it means that the
   basename of PATH is missing from its parent, that is, the final
   target of the deletion is missing.  */
static svn_error_t *
txn_body_delete(void *baton,
                trail_t *trail)
{
  struct delete_args *args = baton;
  svn_fs_root_t *root = args->root;
  const char *path = args->path;
  parent_path_t *parent_path;
  const char *txn_id = root->txn;

  if (! root->is_txn_root)
    return NOT_TXN(root);

  SVN_ERR(open_path(&parent_path, root, path, 0, txn_id, 
                    trail, trail->pool));

  /* We can't remove the root of the filesystem.  */
  if (! parent_path->parent)
    return svn_error_create(SVN_ERR_FS_ROOT_DIR, NULL,
                            _("The root directory cannot be deleted"));

  /* Check to see if path (or any child thereof) is locked; if so,
     check that we can use the existing lock(s). */
  if (root->txn_flags & SVN_FS_TXN_CHECK_LOCKS)
    {
      SVN_ERR(svn_fs_base__allow_locked_operation(path, TRUE,
                                                  trail, trail->pool));
    }
  
  /* Make the parent directory mutable, and do the deletion.  */
  SVN_ERR(make_path_mutable(root, parent_path->parent, path, 
                            trail, trail->pool));
  SVN_ERR(svn_fs_base__dag_delete(parent_path->parent->node,
                                  parent_path->entry,
                                  txn_id, trail, trail->pool));

  /* Make a record of this modification in the changes table. */
  SVN_ERR(add_change(root->fs, txn_id, path,
                     svn_fs_base__dag_get_id(parent_path->node),
                     svn_fs_path_change_delete, 0, 0, trail, trail->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
base_delete_node(svn_fs_root_t *root,
                 const char *path,
                 apr_pool_t *pool)
{
  struct delete_args args;

  args.root        = root;
  args.path        = path;
  return svn_fs_base__retry_txn(root->fs, txn_body_delete, &args, pool);
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
txn_body_copy(void *baton,
              trail_t *trail)
{
  struct copy_args *args = baton;
  svn_fs_root_t *from_root = args->from_root;
  const char *from_path = args->from_path;
  svn_fs_root_t *to_root = args->to_root;
  const char *to_path = args->to_path;
  dag_node_t *from_node;
  parent_path_t *to_parent_path;
  const char *txn_id = to_root->txn;

  /* Get the NODE for FROM_PATH in FROM_ROOT.*/
  SVN_ERR(get_dag(&from_node, from_root, from_path, trail, trail->pool));

  /* Build up the parent path from TO_PATH in TO_ROOT.  If the last
     component does not exist, it's not that big a deal.  We'll just
     make one there. */
  SVN_ERR(open_path(&to_parent_path, to_root, to_path,
                    open_path_last_optional, txn_id, trail, trail->pool));

  /* Check to see if to-path (or any child thereof) is locked, or at
     least 'reserved', whether it exists or not; if so, check that we
     can use the existing lock(s). */
  if (to_root->txn_flags & SVN_FS_TXN_CHECK_LOCKS)
    {
      SVN_ERR(svn_fs_base__allow_locked_operation(to_path, TRUE,
                                                  trail, trail->pool));
    }

  /* If the destination node already exists as the same node as the
     source (in other words, this operation would result in nothing
     happening at all), just do nothing an return successfully,
     proud that you saved yourself from a tiresome task. */
  if ((to_parent_path->node)
      && (svn_fs_base__id_compare(svn_fs_base__dag_get_id(from_node),
                                  svn_fs_base__dag_get_id
                                  (to_parent_path->node)) == 0))
    return SVN_NO_ERROR;

  if (! from_root->is_txn_root)
    {
      svn_fs_path_change_kind_t kind;
      dag_node_t *new_node;

      /* If TO_PATH already existed prior to the copy, note that this
         operation is a replacement, not an addition. */
      if (to_parent_path->node)
        kind = svn_fs_path_change_replace;
      else
        kind = svn_fs_path_change_add;

      /* Make sure the target node's parents are mutable.  */
      SVN_ERR(make_path_mutable(to_root, to_parent_path->parent,
                                to_path, trail, trail->pool));

      SVN_ERR(svn_fs_base__dag_copy(to_parent_path->parent->node,
                                    to_parent_path->entry,
                                    from_node,
                                    args->preserve_history,
                                    from_root->rev,
                                    from_path, txn_id, trail, trail->pool));

      /* Make a record of this modification in the changes table. */
      SVN_ERR(get_dag(&new_node, to_root, to_path, trail, trail->pool));
      SVN_ERR(add_change(to_root->fs, txn_id, to_path,
                         svn_fs_base__dag_get_id(new_node),
                         kind, 0, 0, trail, trail->pool));
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

      abort();
    }

  return SVN_NO_ERROR;
}


/* Set *SAME_P to TRUE if FS1 and FS2 have the same UUID, else set to FALSE.
   Use POOL for temporary allocation only.
   Note: this code is duplicated between libsvn_fs_fs and libsvn_fs_base. */
static svn_error_t *
fs_same_p(svn_boolean_t *same_p,
          svn_fs_t *fs1,
          svn_fs_t *fs2,
          apr_pool_t *pool)
{
  const char *uuid1;
  const char *uuid2;

  /* Random thought: if fetching UUIDs to compare filesystems is too
     expensive, one solution would be to cache the UUID in each fs
     object (copying the UUID into fs->pool, of course). */

  SVN_ERR(fs1->vtable->get_uuid(fs1, &uuid1, pool));
  SVN_ERR(fs2->vtable->get_uuid(fs2, &uuid2, pool));

  *same_p = ! strcmp(uuid1, uuid2);
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_helper(svn_fs_root_t *from_root,
            const char *from_path,
            svn_fs_root_t *to_root,
            const char *to_path,
            svn_boolean_t preserve_history,
            apr_pool_t *pool)
{
  struct copy_args args;
  svn_boolean_t same_p;

  /* Use an error check, not an assert, because even the caller cannot
     guarantee that a filesystem's UUID has not changed "on the fly". */
  SVN_ERR(fs_same_p(&same_p, from_root->fs, to_root->fs, pool));
  if (! same_p)
    return svn_error_createf
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("Cannot copy between two different filesystems ('%s' and '%s')"),
       from_root->fs->path, to_root->fs->path);

  if (! to_root->is_txn_root)
    return NOT_TXN(to_root);

  if (from_root->is_txn_root)
    return svn_error_create
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("Copy from mutable tree not currently supported"));

  args.from_root         = from_root;
  args.from_path         = from_path;
  args.to_root           = to_root;
  args.to_path           = to_path;
  args.preserve_history  = preserve_history;

  return svn_fs_base__retry_txn(to_root->fs, txn_body_copy, &args, pool);
}

static svn_error_t *
base_copy(svn_fs_root_t *from_root,
          const char *from_path,
          svn_fs_root_t *to_root,
          const char *to_path,
          apr_pool_t *pool)
{
  return copy_helper(from_root, from_path, to_root, to_path, TRUE, pool);
}


static svn_error_t *
base_revision_link(svn_fs_root_t *from_root,
                   svn_fs_root_t *to_root,
                   const char *path,
                   apr_pool_t *pool)
{
  return copy_helper(from_root, path, to_root, path, FALSE, pool);
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
txn_body_copied_from(void *baton, trail_t *trail)
{
  struct copied_from_args *args = baton;
  const svn_fs_id_t *node_id, *pred_id;
  dag_node_t *node;
  svn_fs_t *fs = args->root->fs;

  /* Clear the return variables. */
  args->result_path = NULL;
  args->result_rev = SVN_INVALID_REVNUM;

  /* Fetch the NODE in question. */
  SVN_ERR(get_dag(&node, args->root, args->path, trail, trail->pool));
  node_id = svn_fs_base__dag_get_id(node);

  /* Check the node's predecessor-ID.  If it doesn't have one, it
     isn't a copy. */
  SVN_ERR(svn_fs_base__dag_get_predecessor_id(&pred_id, node, 
                                              trail, trail->pool));
  if (! pred_id)
    return SVN_NO_ERROR;

  /* If NODE's copy-ID is the same as that of its predecessor... */
  if (svn_fs_base__key_compare(svn_fs_base__id_copy_id(node_id),
                               svn_fs_base__id_copy_id(pred_id)) != 0)
    {
      /* ... then NODE was either the target of a copy operation,
         a copied subtree item.  We examine the actual copy record
         to determine which is the case.  */
      copy_t *copy;
      SVN_ERR(svn_fs_bdb__get_copy(&copy, fs,
                                   svn_fs_base__id_copy_id(node_id),
                                   trail, trail->pool));
      if ((copy->kind == copy_kind_real)
          && svn_fs_base__id_eq(copy->dst_noderev_id, node_id))
        {
          args->result_path = copy->src_path;
          SVN_ERR(svn_fs_base__txn_get_revision(&(args->result_rev), fs,
                                                copy->src_txn_id, 
                                                trail, trail->pool));
        }
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
base_copied_from(svn_revnum_t *rev_p,
                 const char **path_p,
                 svn_fs_root_t *root,
                 const char *path,
                 apr_pool_t *pool)
{
  struct copied_from_args args;

  args.root = root;
  args.path = path;
  args.pool = pool;

  SVN_ERR(svn_fs_base__retry_txn(root->fs,
                                 txn_body_copied_from, &args, pool));

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
txn_body_make_file(void *baton,
                   trail_t *trail)
{
  struct make_file_args *args = baton;
  svn_fs_root_t *root = args->root;
  const char *path = args->path;
  parent_path_t *parent_path;
  dag_node_t *child;
  const char *txn_id = root->txn;

  SVN_ERR(open_path(&parent_path, root, path, open_path_last_optional,
                    txn_id, trail, trail->pool));

  /* If there's already a file by that name, complain.
     This also catches the case of trying to make a file named `/'.  */
  if (parent_path->node)
    return ALREADY_EXISTS(root, path);

  /* Check to see if some lock is 'reserving' a file-path or dir-path
     at that location, or even some child-path;  if so, check that we
     can use it. */
  if (args->root->txn_flags & SVN_FS_TXN_CHECK_LOCKS)
    {
      SVN_ERR(svn_fs_base__allow_locked_operation(path, TRUE,
                                                  trail, trail->pool));
    }

  /* Create the file.  */
  SVN_ERR(make_path_mutable(root, parent_path->parent, path, 
                            trail, trail->pool));
  SVN_ERR(svn_fs_base__dag_make_file(&child,
                                     parent_path->parent->node,
                                     parent_path_path(parent_path->parent,
                                                      trail->pool),
                                     parent_path->entry,
                                     txn_id,
                                     trail, trail->pool));

  /* Make a record of this modification in the changes table. */
  SVN_ERR(add_change(root->fs, txn_id, path, svn_fs_base__dag_get_id(child),
                     svn_fs_path_change_add, 0, 0, trail, trail->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
base_make_file(svn_fs_root_t *root,
               const char *path,
               apr_pool_t *pool)
{
  struct make_file_args args;

  args.root = root;
  args.path = path;
  return svn_fs_base__retry_txn(root->fs, txn_body_make_file, &args, pool);
}



struct file_length_args
{
  svn_fs_root_t *root;
  const char *path;
  svn_filesize_t length;       /* OUT parameter */
};

static svn_error_t *
txn_body_file_length(void *baton,
                     trail_t *trail)
{
  struct file_length_args *args = baton;
  dag_node_t *file;

  /* First create a dag_node_t from the root/path pair. */
  SVN_ERR(get_dag(&file, args->root, args->path, trail, trail->pool));

  /* Now fetch its length */
  return svn_fs_base__dag_file_length(&args->length, file, 
                                      trail, trail->pool);
}

static svn_error_t *
base_file_length(svn_filesize_t *length_p,
                 svn_fs_root_t *root,
                 const char *path,
                 apr_pool_t *pool)
{
  struct file_length_args args;

  args.root = root;
  args.path = path;
  SVN_ERR(svn_fs_base__retry_txn(root->fs, txn_body_file_length, &args,
                                 pool));

  *length_p = args.length;
  return SVN_NO_ERROR;
}


struct file_checksum_args
{
  svn_fs_root_t *root;
  const char *path;
  unsigned char *digest;  /* OUT parameter, APR_MD5_DIGESTSIZE bytes long */
};

static svn_error_t *
txn_body_file_checksum(void *baton,
                       trail_t *trail)
{
  struct file_checksum_args *args = baton;
  dag_node_t *file;

  SVN_ERR(get_dag(&file, args->root, args->path, trail, trail->pool));
  return svn_fs_base__dag_file_checksum(args->digest, file, 
                                        trail, trail->pool);
}

static svn_error_t *
base_file_md5_checksum(unsigned char digest[],
                       svn_fs_root_t *root,
                       const char *path,
                       apr_pool_t *pool)
{
  struct file_checksum_args args;

  args.root = root;
  args.path = path;
  args.digest = digest;
  SVN_ERR(svn_fs_base__retry_txn(root->fs, txn_body_file_checksum, &args,
                                 pool));

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
txn_body_get_file_contents(void *baton, trail_t *trail)
{
  file_contents_baton_t *fb = (file_contents_baton_t *) baton;

  /* First create a dag_node_t from the root/path pair. */
  SVN_ERR(get_dag(&(fb->node), fb->root, fb->path, trail, trail->pool));

  /* Then create a readable stream from the dag_node_t. */
  SVN_ERR(svn_fs_base__dag_get_contents(&(fb->file_stream),
                                        fb->node,
                                        trail, fb->pool));
  return SVN_NO_ERROR;
}



static svn_error_t *
base_file_contents(svn_stream_t **contents,
                   svn_fs_root_t *root,
                   const char *path,
                   apr_pool_t *pool)
{
  file_contents_baton_t *fb = apr_pcalloc(pool, sizeof(*fb));
  fb->root = root;
  fb->path = path;
  fb->pool = pool;

  /* Create the readable stream in the context of a db txn.  */
  SVN_ERR(svn_fs_base__retry_txn(root->fs, txn_body_get_file_contents,
                                 fb, pool));

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

  /* Hex MD5 digest for the base text against which a delta is to be
     applied, and for the resultant fulltext, respectively.  Either or
     both may be null, in which case ignored. */
  const char *base_checksum;
  const char *result_checksum;

  /* Pool used by db txns */
  apr_pool_t *pool;

} txdelta_baton_t;


/* A trail-ready wrapper around svn_fs_base__dag_finalize_edits.
 * This closes BATON->target_stream.
 *
 * Note: If you're confused about how this function relates to another
 * of similar name, think of it this way:
 *
 * svn_fs_apply_textdelta() ==> ... ==> txn_body_txdelta_finalize_edits()
 * svn_fs_apply_text()      ==> ... ==> txn_body_fulltext_finalize_edits()
 */
static svn_error_t *
txn_body_txdelta_finalize_edits(void *baton, trail_t *trail)
{
  txdelta_baton_t *tb = (txdelta_baton_t *) baton;
  return svn_fs_base__dag_finalize_edits(tb->node,
                                         tb->result_checksum,
                                         tb->root->txn,
                                         trail, trail->pool);
}


/* ### see comment in window_consumer() regarding this function. */

/* Helper function of generic type `svn_write_fn_t'.  Implements a
   writable stream which appends to an svn_stringbuf_t. */
static svn_error_t *
write_to_string(void *baton, const char *data, apr_size_t *len)
{
  txdelta_baton_t *tb = (txdelta_baton_t *) baton;
  svn_stringbuf_appendbytes(tb->target_string, data, *len);
  return SVN_NO_ERROR;
}



/* The main window handler returned by svn_fs_apply_textdelta. */
static svn_error_t *
window_consumer(svn_txdelta_window_t *window, void *baton)
{
  txdelta_baton_t *tb = (txdelta_baton_t *) baton;

  /* Send the window right through to the custom window interpreter.
     In theory, the interpreter will then write more data to
     cb->target_string. */
  SVN_ERR(tb->interpreter(window, tb->interpreter_baton));

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
      SVN_ERR(svn_stream_write(tb->target_stream,
                               tb->target_string->data,
                               &len));
      svn_stringbuf_set(tb->target_string, "");
    }

  /* Is the window NULL?  If so, we're done. */
  if (! window)
    {
      /* Close the internal-use stream.  ### This used to be inside of
         txn_body_fulltext_finalize_edits(), but that invoked a nested
         Berkeley DB transaction -- scandalous! */
      SVN_ERR(svn_stream_close(tb->target_stream));

      /* Tell the dag subsystem that we're finished with our edits. */
      SVN_ERR(svn_fs_base__retry_txn(tb->root->fs,
                                     txn_body_txdelta_finalize_edits, tb,
                                     tb->pool));
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
txn_body_apply_textdelta(void *baton, trail_t *trail)
{
  txdelta_baton_t *tb = (txdelta_baton_t *) baton;
  parent_path_t *parent_path;
  const char *txn_id = tb->root->txn;

  /* Call open_path with no flags, as we want this to return an error
     if the node for which we are searching doesn't exist. */
  SVN_ERR(open_path(&parent_path, tb->root, tb->path, 0, txn_id, 
                    trail, trail->pool));

  /* Check to see if path is locked;  if so, check that we can use it. */
  if (tb->root->txn_flags & SVN_FS_TXN_CHECK_LOCKS)
    SVN_ERR(svn_fs_base__allow_locked_operation(tb->path, FALSE, 
                                                trail, trail->pool));

  /* Now, make sure this path is mutable. */
  SVN_ERR(make_path_mutable(tb->root, parent_path, tb->path, 
                            trail, trail->pool));
  tb->node = parent_path->node;

  if (tb->base_checksum)
    {
      unsigned char digest[APR_MD5_DIGESTSIZE];
      const char *hex;

      /* Until we finalize the node, its data_key points to the old
         contents, in other words, the base text. */
      SVN_ERR(svn_fs_base__dag_file_checksum(digest, tb->node, 
                                             trail, trail->pool));
      hex = svn_md5_digest_to_cstring(digest, trail->pool);
      if (hex && (strcmp(tb->base_checksum, hex) != 0))
        return svn_error_createf
          (SVN_ERR_CHECKSUM_MISMATCH,
           NULL,
           _("Base checksum mismatch on '%s':\n"
             "   expected:  %s\n"
             "     actual:  %s\n"),
           tb->path, tb->base_checksum, hex);
    }

  /* Make a readable "source" stream out of the current contents of
     ROOT/PATH; obviously, this must done in the context of a db_txn.
     The stream is returned in tb->source_stream. */
  SVN_ERR(svn_fs_base__dag_get_contents(&(tb->source_stream),
                                        tb->node, trail, tb->pool));

  /* Make a writable "target" stream */
  SVN_ERR(svn_fs_base__dag_get_edit_stream(&(tb->target_stream), tb->node,
                                           txn_id, trail, tb->pool));

  /* Make a writable "string" stream which writes data to
     tb->target_string. */
  tb->target_string = svn_stringbuf_create("", tb->pool);
  tb->string_stream = svn_stream_create(tb, tb->pool);
  svn_stream_set_write(tb->string_stream, write_to_string);

  /* Now, create a custom window handler that uses our two streams. */
  svn_txdelta_apply(tb->source_stream,
                    tb->string_stream,
                    NULL,
                    tb->path,
                    tb->pool,
                    &(tb->interpreter),
                    &(tb->interpreter_baton));

  /* Make a record of this modification in the changes table. */
  SVN_ERR(add_change(tb->root->fs, txn_id, tb->path,
                     svn_fs_base__dag_get_id(tb->node),
                     svn_fs_path_change_modify, 1, 0, trail, trail->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
base_apply_textdelta(svn_txdelta_window_handler_t *contents_p,
                     void **contents_baton_p,
                     svn_fs_root_t *root,
                     const char *path,
                     const char *base_checksum,
                     const char *result_checksum,
                     apr_pool_t *pool)
{
  txdelta_baton_t *tb = apr_pcalloc(pool, sizeof(*tb));

  tb->root = root;
  tb->path = path;
  tb->pool = pool;

  if (base_checksum)
    tb->base_checksum = apr_pstrdup(pool, base_checksum);
  else
    tb->base_checksum = NULL;

  if (result_checksum)
    tb->result_checksum = apr_pstrdup(pool, result_checksum);
  else
    tb->result_checksum = NULL;

  SVN_ERR(svn_fs_base__retry_txn(root->fs, txn_body_apply_textdelta, tb,
                                 pool));

  *contents_p = window_consumer;
  *contents_baton_p = tb;
  return SVN_NO_ERROR;
}

/* --- End machinery for svn_fs_apply_textdelta() ---  */

/* --- Machinery for svn_fs_apply_text() ---  */

/* Baton for svn_fs_apply_text(). */
struct text_baton_t
{
  /* The original file info */
  svn_fs_root_t *root;
  const char *path;

  /* Derived from the file info */
  dag_node_t *node;

  /* The returned stream that will accept the file's new contents. */
  svn_stream_t *stream;

  /* The actual fs stream that the returned stream will write to. */
  svn_stream_t *file_stream;

  /* Hex MD5 digest for the final fulltext written to the file.  May
     be null, in which case ignored. */
  const char *result_checksum;

  /* Pool used by db txns */
  apr_pool_t *pool;
};


/* A trail-ready wrapper around svn_fs_base__dag_finalize_edits, but for
 * fulltext data, not text deltas.  Closes BATON->file_stream.
 *
 * Note: If you're confused about how this function relates to another
 * of similar name, think of it this way:
 *
 * svn_fs_apply_textdelta() ==> ... ==> txn_body_txdelta_finalize_edits()
 * svn_fs_apply_text()      ==> ... ==> txn_body_fulltext_finalize_edits()
 */
static svn_error_t *
txn_body_fulltext_finalize_edits(void *baton, trail_t *trail)
{
  struct text_baton_t *tb = baton;
  return svn_fs_base__dag_finalize_edits(tb->node,
                                         tb->result_checksum,
                                         tb->root->txn,
                                         trail, trail->pool);
}

/* Write function for the publically returned stream. */
static svn_error_t *
text_stream_writer(void *baton,
                   const char *data,
                   apr_size_t *len)
{
  struct text_baton_t *tb = baton;

  /* Psst, here's some data.  Pass it on to the -real- file stream. */
  return svn_stream_write(tb->file_stream, data, len);
}

/* Close function for the publically returned stream. */
static svn_error_t *
text_stream_closer(void *baton)
{
  struct text_baton_t *tb = baton;

  /* Close the internal-use stream.  ### This used to be inside of
     txn_body_fulltext_finalize_edits(), but that invoked a nested
     Berkeley DB transaction -- scandalous! */
  SVN_ERR(svn_stream_close(tb->file_stream));

  /* Need to tell fs that we're done sending text */
  SVN_ERR(svn_fs_base__retry_txn(tb->root->fs,
                                 txn_body_fulltext_finalize_edits, tb,
                                 tb->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
txn_body_apply_text(void *baton, trail_t *trail)
{
  struct text_baton_t *tb = baton;
  parent_path_t *parent_path;
  const char *txn_id = tb->root->txn;

  /* Call open_path with no flags, as we want this to return an error
     if the node for which we are searching doesn't exist. */
  SVN_ERR(open_path(&parent_path, tb->root, tb->path, 0, txn_id, 
                    trail, trail->pool));

  /* Check to see if path is locked;  if so, check that we can use it. */
  if (tb->root->txn_flags & SVN_FS_TXN_CHECK_LOCKS)
    SVN_ERR(svn_fs_base__allow_locked_operation(tb->path, FALSE, 
                                                trail, trail->pool));

  /* Now, make sure this path is mutable. */
  SVN_ERR(make_path_mutable(tb->root, parent_path, tb->path, 
                            trail, trail->pool));
  tb->node = parent_path->node;

  /* Make a writable stream for replacing the file's text. */
  SVN_ERR(svn_fs_base__dag_get_edit_stream(&(tb->file_stream), tb->node,
                                           txn_id, trail, tb->pool));

  /* Create a 'returnable' stream which writes to the file_stream. */
  tb->stream = svn_stream_create(tb, tb->pool);
  svn_stream_set_write(tb->stream, text_stream_writer);
  svn_stream_set_close(tb->stream, text_stream_closer);

  /* Make a record of this modification in the changes table. */
  SVN_ERR(add_change(tb->root->fs, txn_id, tb->path,
                     svn_fs_base__dag_get_id(tb->node),
                     svn_fs_path_change_modify, 1, 0, trail, trail->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
base_apply_text(svn_stream_t **contents_p,
                svn_fs_root_t *root,
                const char *path,
                const char *result_checksum,
                apr_pool_t *pool)
{
  struct text_baton_t *tb = apr_pcalloc(pool, sizeof(*tb));

  tb->root = root;
  tb->path = path;
  tb->pool = pool;

  if (result_checksum)
    tb->result_checksum = apr_pstrdup(pool, result_checksum);
  else
    tb->result_checksum = NULL;

  SVN_ERR(svn_fs_base__retry_txn(root->fs, txn_body_apply_text, tb, pool));

  *contents_p = tb->stream;
  return SVN_NO_ERROR;
}

/* --- End machinery for svn_fs_apply_text() ---  */


/* Note: we're sharing the `things_changed_args' struct with
   svn_fs_props_changed(). */

static svn_error_t *
txn_body_contents_changed(void *baton, trail_t *trail)
{
  struct things_changed_args *args = baton;
  dag_node_t *node1, *node2;

  SVN_ERR(get_dag(&node1, args->root1, args->path1, trail, trail->pool));
  SVN_ERR(get_dag(&node2, args->root2, args->path2, trail, trail->pool));
  SVN_ERR(svn_fs_base__things_different(NULL, args->changed_p,
                                        node1, node2, trail, trail->pool));
  return SVN_NO_ERROR;
}


/* Note:  it is acceptable for this function to call back into
   top-level interfaces because it does not itself use trails.  */
static svn_error_t *
base_contents_changed(svn_boolean_t *changed_p,
                      svn_fs_root_t *root1,
                      const char *path1,
                      svn_fs_root_t *root2,
                      const char *path2,
                      apr_pool_t *pool)
{
  struct things_changed_args args;

  /* Check that roots are in the same fs. */
  if (root1->fs != root2->fs)
    return svn_error_create
      (SVN_ERR_FS_GENERAL, NULL,
       _("Cannot compare file contents between two different filesystems"));

  /* Check that both paths are files. */
  {
    svn_node_kind_t kind;

    SVN_ERR(base_check_path(&kind, root1, path1, pool));
    if (kind != svn_node_file)
      return svn_error_createf
        (SVN_ERR_FS_GENERAL, NULL, _("'%s' is not a file"), path1);

    SVN_ERR(base_check_path(&kind, root2, path2, pool));
    if (kind != svn_node_file)
      return svn_error_createf
        (SVN_ERR_FS_GENERAL, NULL, _("'%s' is not a file"), path2);
  }

  args.root1      = root1;
  args.root2      = root2;
  args.path1      = path1;
  args.path2      = path2;
  args.changed_p  = changed_p;
  args.pool       = pool;

  SVN_ERR(svn_fs_base__retry_txn(root1->fs, txn_body_contents_changed,
                                 &args, pool));

  return SVN_NO_ERROR;
}



/* Public interface to computing file text deltas.  */

/* Note:  it is acceptable for this function to call back into
   public FS API interfaces because it does not itself use trails.  */
static svn_error_t *
base_get_file_delta_stream(svn_txdelta_stream_t **stream_p,
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
    SVN_ERR(base_file_contents(&source, source_root, source_path, pool));
  else
    source = svn_stream_empty(pool);

  /* Get read functions for the target file contents.  */
  SVN_ERR(base_file_contents(&target, target_root, target_path, pool));

  /* Create a delta stream that turns the ancestor into the target.  */
  svn_txdelta(&delta_stream, source, target, pool);

  *stream_p = delta_stream;
  return SVN_NO_ERROR;
}



/* Finding Changes */

struct paths_changed_args
{
  apr_hash_t *changes;
  svn_fs_root_t *root;
};


static svn_error_t *
txn_body_paths_changed(void *baton,
                       trail_t *trail)
{
  /* WARNING: This is called *without* the protection of a Berkeley DB
     transaction.  If you modify this function, keep that in mind. */

  struct paths_changed_args *args = baton;
  const char *txn_id;
  svn_fs_t *fs = args->root->fs;

  /* Get the transaction ID from ROOT. */
  if (! args->root->is_txn_root)
    SVN_ERR(svn_fs_base__rev_get_txn_id(&txn_id, fs, args->root->rev,
                                        trail, trail->pool));
  else
    txn_id = args->root->txn;

  return svn_fs_bdb__changes_fetch(&(args->changes), fs, txn_id, 
                                   trail, trail->pool);
}


static svn_error_t *
base_paths_changed(apr_hash_t **changed_paths_p,
                   svn_fs_root_t *root,
                   apr_pool_t *pool)
{
  struct paths_changed_args args;
  args.root = root;
  args.changes = NULL;
  SVN_ERR(svn_fs_base__retry(root->fs, txn_body_paths_changed, &args, pool));
  *changed_paths_p = args.changes;
  return SVN_NO_ERROR;
}



/* Our coolio opaque history object. */
typedef struct
{
  /* filesystem object */
  svn_fs_t *fs;

  /* path and revision of historical location */
  const char *path;
  svn_revnum_t revision;

  /* internal-use hints about where to resume the history search. */
  const char *path_hint;
  svn_revnum_t rev_hint;

  /* FALSE until the first call to svn_fs_history_prev(). */
  svn_boolean_t is_interesting;
} base_history_data_t;


static svn_fs_history_t *assemble_history(svn_fs_t *fs, const char *path,
                                          svn_revnum_t revision,
                                          svn_boolean_t is_interesting,
                                          const char *path_hint,
                                          svn_revnum_t rev_hint,
                                          apr_pool_t *pool);


static svn_error_t *
base_node_history(svn_fs_history_t **history_p,
                  svn_fs_root_t *root,
                  const char *path,
                  apr_pool_t *pool)
{
  svn_node_kind_t kind;

  /* We require a revision root. */
  if (root->is_txn_root)
    return svn_error_create(SVN_ERR_FS_NOT_REVISION_ROOT, NULL, NULL);

  /* And we require that the path exist in the root. */
  SVN_ERR(base_check_path(&kind, root, path, pool));
  if (kind == svn_node_none)
    return NOT_FOUND(root, path);

  /* Okay, all seems well.  Build our history object and return it. */
  *history_p = assemble_history(root->fs,
                                svn_fs_base__canonicalize_abspath(path,
                                                                  pool),
                                root->rev, FALSE, NULL,
                                SVN_INVALID_REVNUM, pool);
  return SVN_NO_ERROR;
}


/* Examine the PARENT_PATH structure chain to determine how copy IDs
   would be doled out in the event that PARENT_PATH was made mutable.
   Return the ID of the copy that last affected PARENT_PATH (and the
   COPY itself, if we've already fetched it).
*/
static svn_error_t *
examine_copy_inheritance(const char **copy_id,
                         copy_t **copy,
                         svn_fs_t *fs,
                         parent_path_t *parent_path,
                         trail_t *trail,
                         apr_pool_t *pool)
{
  /* The default response -- our current copy ID, and no fetched COPY. */
  *copy_id = svn_fs_base__id_copy_id
    (svn_fs_base__dag_get_id(parent_path->node));
  *copy = NULL;

  /* If we have no parent (we are looking at the root node), or if
     this node is supposed to inherit from itself, return that fact. */
  if (! parent_path->parent)
    return SVN_NO_ERROR;

  /* We could be a branch destination (which would answer our question
     altogether)!  But then, again, we might just have been modified
     in this revision, so all bets are off. */
  if (parent_path->copy_inherit == copy_id_inherit_self)
    {
      /* A copy ID of "0" means we've never been branched.  Therefore,
         there are no copies relevant to our history. */
      if (((*copy_id)[0] == '0') && ((*copy_id)[1] == '\0'))
        return SVN_NO_ERROR;

      /* Get the COPY record.  If it was a real copy (not an implicit
         one), we have our answer.  Otherwise, we fall through to the
         recursive case. */
      SVN_ERR(svn_fs_bdb__get_copy(copy, fs, *copy_id, trail, pool));
      if ((*copy)->kind != copy_kind_soft)
        return SVN_NO_ERROR;
    }

  /* Otherwise, our answer is dependent upon our parent. */
  return examine_copy_inheritance(copy_id, copy, fs,
                                  parent_path->parent, trail, pool);
}


struct history_prev_args
{
  svn_fs_history_t **prev_history_p;
  svn_fs_history_t *history;
  svn_boolean_t cross_copies;
  apr_pool_t *pool;
};


static svn_error_t *
txn_body_history_prev(void *baton, trail_t *trail)
{
  struct history_prev_args *args = baton;
  svn_fs_history_t **prev_history = args->prev_history_p;
  svn_fs_history_t *history = args->history;
  base_history_data_t *bhd = history->fsap_data;
  const char *commit_path, *src_path, *path = bhd->path;
  svn_revnum_t commit_rev, src_rev, dst_rev, revision = bhd->revision;
  apr_pool_t *retpool = args->pool;
  svn_fs_t *fs = bhd->fs;
  parent_path_t *parent_path;
  dag_node_t *node;
  svn_fs_root_t *root;
  const svn_fs_id_t *node_id;
  const char *end_copy_id = NULL;
  struct revision_root_args rr_args;
  svn_boolean_t reported = bhd->is_interesting;
  const char *txn_id;
  copy_t *copy = NULL;
  svn_boolean_t retry = FALSE;

  /* Initialize our return value. */
  *prev_history = NULL;

  /* If our last history report left us hints about where to pickup
     the chase, then our last report was on the destination of a
     copy.  If we are crossing copies, start from those locations,
     otherwise, we're all done here.  */
  if (bhd->path_hint && SVN_IS_VALID_REVNUM(bhd->rev_hint))
    {
      reported = FALSE;
      if (! args->cross_copies)
        return SVN_NO_ERROR;
      path = bhd->path_hint;
      revision = bhd->rev_hint;
    }

  /* Construct a ROOT for the current revision. */
  rr_args.root_p = &root;
  rr_args.rev = revision;
  SVN_ERR(txn_body_revision_root(&rr_args, trail));

  /* Open PATH/REVISION, and get its node and a bunch of other
     goodies.  */
  SVN_ERR(svn_fs_base__rev_get_txn_id(&txn_id, fs, revision, trail, 
                                      trail->pool));
  SVN_ERR(open_path(&parent_path, root, path, 0, txn_id, 
                    trail, trail->pool));
  node = parent_path->node;
  node_id = svn_fs_base__dag_get_id(node);
  commit_path = svn_fs_base__dag_get_created_path(node);
  SVN_ERR(svn_fs_base__dag_get_revision(&commit_rev, node, 
                                        trail, trail->pool));

  /* The Subversion filesystem is written in such a way that a given
     line of history may have at most one interesting history point
     per filesystem revision.  Either that node was edited (and
     possibly copied), or it was copied but not edited.  And a copy
     source cannot be from the same revision as its destination.  So,
     if our history revision matches its node's commit revision, we
     know that ... */
  if (revision == commit_rev)
    {
      if (! reported)
        {
          /* ... we either have not yet reported on this revision (and
             need now to do so) ... */
          *prev_history = assemble_history(fs,
                                           apr_pstrdup(retpool, commit_path),
                                           commit_rev, TRUE, NULL,
                                           SVN_INVALID_REVNUM, retpool);
          return SVN_NO_ERROR;
        }
      else
        {
          /* ... or we *have* reported on this revision, and must now
             progress toward this node's predecessor (unless there is
             no predecessor, in which case we're all done!). */
          const svn_fs_id_t *pred_id;

          SVN_ERR(svn_fs_base__dag_get_predecessor_id(&pred_id, node,
                                                      trail, trail->pool));
          if (! pred_id)
            return SVN_NO_ERROR;

          /* Replace NODE and friends with the information from its
             predecessor. */
          SVN_ERR(svn_fs_base__dag_get_node(&node, fs, pred_id, 
                                            trail, trail->pool));
          node_id = svn_fs_base__dag_get_id(node);
          commit_path = svn_fs_base__dag_get_created_path(node);
          SVN_ERR(svn_fs_base__dag_get_revision(&commit_rev, node, 
                                                trail, trail->pool));
        }
    }

  /* Calculate a possibly relevant copy ID. */
  SVN_ERR(examine_copy_inheritance(&end_copy_id, &copy, fs,
                                   parent_path, trail, trail->pool));

  /* Initialize some state variables. */
  src_path = NULL;
  src_rev = SVN_INVALID_REVNUM;
  dst_rev = SVN_INVALID_REVNUM;

  /* If our current copy ID (which is either the real copy ID of our
     node, or the last copy ID which would affect our node if it were
     to be made mutable) diffs at all from that of its predecessor
     (which is either a real predecessor, or is the node itself
     playing the predecessor role to an imaginary mutable successor),
     then we need to report a copy.  */
  if (svn_fs_base__key_compare(svn_fs_base__id_copy_id(node_id),
                               end_copy_id) != 0)
    {
      const char *remainder;
      dag_node_t *dst_node;
      const char *copy_dst;

      /* Get the COPY record if we haven't already fetched it. */
      if (! copy)
        SVN_ERR(svn_fs_bdb__get_copy(&copy, fs, end_copy_id, trail, 
                                     trail->pool));

      /* Figure out the destination path of the copy operation. */
      SVN_ERR(svn_fs_base__dag_get_node(&dst_node, fs,
                                        copy->dst_noderev_id, 
                                        trail, trail->pool));
      copy_dst = svn_fs_base__dag_get_created_path(dst_node);

      /* If our current path was the very destination of the copy,
         then our new current path will be the copy source.  If our
         current path was instead the *child* of the destination of
         the copy, then figure out its previous location by taking its
         path relative to the copy destination and appending that to
         the copy source.  Finally, if our current path doesn't meet
         one of these other criteria ... ### for now just fallback to
         the old copy hunt algorithm. */
      if (strcmp(path, copy_dst) == 0)
        remainder = "";
      else
        remainder = svn_path_is_child(copy_dst, path, trail->pool);

      if (remainder)
        {
          /* If we get here, then our current path is the destination
             of, or the child of the destination of, a copy.  Fill
             in the return values and get outta here.  */
          SVN_ERR(svn_fs_base__txn_get_revision
                  (&src_rev, fs, copy->src_txn_id, trail, trail->pool));
          SVN_ERR(svn_fs_base__txn_get_revision
                  (&dst_rev, fs,
                   svn_fs_base__id_txn_id(copy->dst_noderev_id), 
                   trail, trail->pool));
          src_path = svn_path_join(copy->src_path, remainder,
                                   trail->pool);
          if (copy->kind == copy_kind_soft)
            retry = TRUE;
        }
    }

  /* If we calculated a copy source path and revision, and the
     copy source revision doesn't pre-date a revision in which we
     *know* our node was modified, we'll make a 'copy-style' history
     object. */
  if (src_path && SVN_IS_VALID_REVNUM(src_rev) && (src_rev >= commit_rev))
    {
      /* It's possible for us to find a copy location that is the same
         as the history point we've just reported.  If that happens,
         we simply need to take another trip through this history
         search. */
      if ((dst_rev == revision) && reported)
        retry = TRUE;

      *prev_history = assemble_history(fs, apr_pstrdup(retpool, path),
                                       dst_rev, retry ? FALSE : TRUE,
                                       src_path, src_rev, retpool);
    }
  else
    {
      *prev_history = assemble_history(fs, apr_pstrdup(retpool, commit_path),
                                       commit_rev, TRUE, NULL,
                                       SVN_INVALID_REVNUM, retpool);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
base_history_prev(svn_fs_history_t **prev_history_p,
                  svn_fs_history_t *history,
                  svn_boolean_t cross_copies,
                  apr_pool_t *pool)
{
  svn_fs_history_t *prev_history = NULL;
  base_history_data_t *bhd = history->fsap_data;
  svn_fs_t *fs = bhd->fs;

  /* Special case: the root directory changes in every single
     revision, no exceptions.  And, the root can't be the target (or
     child of a target -- duh) of a copy.  So, if that's our path,
     then we need only decrement our revision by 1, and there you go. */
  if (strcmp(bhd->path, "/") == 0)
    {
      if (! bhd->is_interesting)
        prev_history = assemble_history(fs, "/", bhd->revision,
                                        1, NULL, SVN_INVALID_REVNUM, pool);
      else if (bhd->revision > 0)
        prev_history = assemble_history(fs, "/", bhd->revision - 1,
                                        1, NULL, SVN_INVALID_REVNUM, pool);
    }
  else
    {
      struct history_prev_args args;
      prev_history = history;

      while (1)
        {
          /* Get a trail, and get to work. */

          args.prev_history_p = &prev_history;
          args.history = prev_history;
          args.cross_copies = cross_copies;
          args.pool = pool;
          SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_history_prev, &args,
                                         pool));
          if (! prev_history)
            break;
          bhd = prev_history->fsap_data;
          if (bhd->is_interesting)
            break;
        }
    }

  *prev_history_p = prev_history;
  return SVN_NO_ERROR;
}


static svn_error_t *
base_history_location(const char **path,
                      svn_revnum_t *revision,
                      svn_fs_history_t *history,
                      apr_pool_t *pool)
{
  base_history_data_t *bhd = history->fsap_data;

  *path = apr_pstrdup(pool, bhd->path);
  *revision = bhd->revision;
  return SVN_NO_ERROR;
}


static history_vtable_t history_vtable = {
  base_history_prev,
  base_history_location
};



struct closest_copy_args
{
  svn_fs_root_t **root_p;
  const char **path_p;
  svn_fs_root_t *root;
  const char *path;
  apr_pool_t *pool;
};


static svn_error_t *
txn_body_closest_copy(void *baton, trail_t *trail)
{
  struct closest_copy_args *args = baton;
  svn_fs_root_t *root = args->root;
  const char *path = args->path;
  svn_fs_t *fs = root->fs;
  parent_path_t *parent_path;
  const svn_fs_id_t *node_id;
  const char *txn_id, *copy_id;
  copy_t *copy = NULL;
  svn_fs_root_t *copy_dst_root;
  dag_node_t *path_node_in_copy_dst, *copy_dst_node, *copy_dst_root_node;
  const char *copy_dst_path;
  svn_revnum_t copy_dst_rev, created_rev;
  svn_error_t *err;

  *(args->path_p) = NULL;
  *(args->root_p) = NULL;

  /* Get the transaction ID associated with our root. */
  if (root->is_txn_root)
    txn_id = root->txn;
  else
    SVN_ERR(svn_fs_base__rev_get_txn_id(&txn_id, fs, root->rev, 
                                        trail, trail->pool));

  /* Open PATH in ROOT -- it must exist. */
  SVN_ERR(open_path(&parent_path, root, path, 0, txn_id, 
                    trail, trail->pool));
  node_id = svn_fs_base__dag_get_id(parent_path->node);

  /* Now, examine the copy inheritance rules in play should our path
     be made mutable in the future (if it isn't already).  This will
     tell us about the youngest affecting copy.  */
  SVN_ERR(examine_copy_inheritance(&copy_id, &copy, fs, parent_path,
                                   trail, trail->pool));

  /* Easy out:  if the copy ID is 0, there's nothing of interest here. */
  if (((copy_id)[0] == '0') && ((copy_id)[1] == '\0'))
    return SVN_NO_ERROR;

  /* Fetch our copy if examine_copy_inheritance() didn't do it for us. */
  if (! copy)
    SVN_ERR(svn_fs_bdb__get_copy(&copy, fs, copy_id, trail, trail->pool));

  /* Figure out the destination path and revision of the copy operation. */
  SVN_ERR(svn_fs_base__dag_get_node(&copy_dst_node, fs, copy->dst_noderev_id,
                                    trail, trail->pool));
  copy_dst_path = svn_fs_base__dag_get_created_path(copy_dst_node);
  SVN_ERR(svn_fs_base__dag_get_revision(&copy_dst_rev, copy_dst_node, 
                                        trail, trail->pool));

  /* Turn that revision into a revision root. */
  SVN_ERR(svn_fs_base__dag_revision_root(&copy_dst_root_node, fs, 
                                         copy_dst_rev, trail, args->pool));
  copy_dst_root = make_revision_root(fs, copy_dst_rev, 
                                     copy_dst_root_node, args->pool);

  /* It is possible that this node was created from scratch at some
     revision between COPY_DST_REV and the transaction associated with
     our ROOT.  Make sure that PATH exists as of COPY_DST_REV and is
     related to this node-rev. */
  err = get_dag(&path_node_in_copy_dst, copy_dst_root, path, 
                trail, trail->pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
        {
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }
      return err;
    }
  if ((svn_fs_base__dag_node_kind(path_node_in_copy_dst) == svn_node_none)
      || (! (svn_fs_base__id_check_related 
             (node_id, svn_fs_base__dag_get_id(path_node_in_copy_dst)))))
    {
      return SVN_NO_ERROR;
    }

  /* One final check must be done here.  If you copy a directory and
     create a new entity somewhere beneath that directory in the same
     txn, then we can't claim that the copy affected the new entity.
     For example, if you do:

        copy dir1 dir2
        create dir2/new-thing
        commit
  
     then dir2/new-thing was not affected by the copy of dir1 to dir2.
     We detect this situation by asking if PATH@COPY_DST_REV's
     created-rev is COPY_DST_REV, and that node-revision has no
     predecessors, then there is no relevant closest copy.
  */
  SVN_ERR(svn_fs_base__dag_get_revision(&created_rev, path_node_in_copy_dst,
                                        trail, trail->pool));
  if (created_rev == copy_dst_rev)
    {
      const svn_fs_id_t *pred_id;
      SVN_ERR(svn_fs_base__dag_get_predecessor_id(&pred_id, 
                                                  path_node_in_copy_dst,
                                                  trail, trail->pool));
      if (! pred_id)
        return SVN_NO_ERROR;
    }

  *(args->path_p) = apr_pstrdup(args->pool, copy_dst_path);
  *(args->root_p) = copy_dst_root;

  return SVN_NO_ERROR;
}


static svn_error_t *
base_closest_copy(svn_fs_root_t **root_p,
                  const char **path_p,
                  svn_fs_root_t *root,
                  const char *path,
                  apr_pool_t *pool)
{
  struct closest_copy_args args;
  svn_fs_t *fs = root->fs;
  svn_fs_root_t *closest_root = NULL;
  const char *closest_path = NULL;

  args.root_p = &closest_root;
  args.path_p = &closest_path;
  args.root = root;
  args.path = path;
  args.pool = pool;
  SVN_ERR(svn_fs_base__retry_txn(fs, txn_body_closest_copy, &args, pool));
  *root_p = closest_root;
  *path_p = closest_path;
  return SVN_NO_ERROR;
}


/* Return a new history object (marked as "interesting") for PATH and
   REVISION, allocated in POOL, and with its members set to the values
   of the parameters provided.  Note that PATH and PATH_HINT are not
   duped into POOL -- it is the responsibility of the caller to ensure
   that this happens. */
static svn_fs_history_t *
assemble_history(svn_fs_t *fs,
                 const char *path,
                 svn_revnum_t revision,
                 svn_boolean_t is_interesting,
                 const char *path_hint,
                 svn_revnum_t rev_hint,
                 apr_pool_t *pool)
{
  svn_fs_history_t *history = apr_pcalloc(pool, sizeof(*history));
  base_history_data_t *bhd = apr_pcalloc(pool, sizeof(*bhd));
  bhd->path = path;
  bhd->revision = revision;
  bhd->is_interesting = is_interesting;
  bhd->path_hint = path_hint;
  bhd->rev_hint = rev_hint;
  bhd->fs = fs;
  history->vtable = &history_vtable;
  history->fsap_data = bhd;
  return history;
}



/* Creating root objects.  */


static root_vtable_t root_vtable = {
  base_paths_changed,
  base_check_path,
  base_node_history,
  base_node_id,
  base_node_created_rev,
  base_node_created_path,
  base_delete_node,
  base_copied_from,
  base_closest_copy,
  base_node_prop,
  base_node_proplist,
  base_change_node_prop,
  base_props_changed,
  base_dir_entries,
  base_make_dir,
  base_copy,
  base_revision_link,
  base_file_length,
  base_file_md5_checksum,
  base_file_contents,
  base_make_file,
  base_apply_textdelta,
  base_apply_text,
  base_contents_changed,
  base_get_file_delta_stream,
  base_merge
};


/* Construct a new root object in FS, allocated from POOL.  */
static svn_fs_root_t *
make_root(svn_fs_t *fs,
          apr_pool_t *pool)
{
  /* We create a subpool for each root object to allow us to implement
     svn_fs_close_root.  */
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_fs_root_t *root = apr_pcalloc(subpool, sizeof(*root));
  base_root_data_t *brd = apr_palloc(subpool, sizeof(*brd));

  root->fs = fs;
  root->pool = subpool;

  /* Init the node ID cache. */
  brd->node_cache = apr_hash_make(pool);
  brd->node_cache_idx = 0;
  root->vtable = &root_vtable;
  root->fsap_data = brd;

  return root;
}


/* Construct a root object referring to the root of REVISION in FS,
   whose root directory is ROOT_DIR.  Create the new root in POOL.  */
static svn_fs_root_t *
make_revision_root(svn_fs_t *fs,
                   svn_revnum_t rev,
                   dag_node_t *root_dir,
                   apr_pool_t *pool)
{
  svn_fs_root_t *root = make_root(fs, pool);
  base_root_data_t *brd = root->fsap_data;

  root->is_txn_root = FALSE;
  root->rev = rev;
  brd->root_dir = root_dir;

  return root;
}


/* Construct a root object referring to the root of the transaction
   named TXN in FS.  FLAGS represents the behavior of the transaction.
   Create the new root in POOL.  */
static svn_fs_root_t *
make_txn_root(svn_fs_t *fs,
              const char *txn,
              apr_uint32_t flags,
              apr_pool_t *pool)
{
  svn_fs_root_t *root = make_root(fs, pool);
  root->is_txn_root = TRUE;
  root->txn = apr_pstrdup(root->pool, txn);
  root->txn_flags = flags;

  return root;
}


svn_error_t *
svn_fs_base__get_path_kind(svn_node_kind_t *kind,
                           const char *path,
                           trail_t *trail,
                           apr_pool_t *pool)
{
  svn_revnum_t head_rev;
  svn_fs_root_t *root;
  dag_node_t *root_dir, *path_node;
  svn_error_t *err;

  /* Get HEAD revision, */
  SVN_ERR(svn_fs_bdb__youngest_rev(&head_rev, trail->fs, trail, pool));

  /* Then convert it into a root_t, */
  SVN_ERR(svn_fs_base__dag_revision_root(&root_dir, trail->fs, head_rev,
                                         trail, pool));
  root = make_revision_root(trail->fs, head_rev, root_dir, pool);

  /* And get the dag_node for path in the root_t. */
  err = get_dag(&path_node, root, path, trail, pool);
  if (err && (err->apr_err == SVN_ERR_FS_NOT_FOUND))
    {
      svn_error_clear(err);
      *kind = svn_node_none;
      return SVN_NO_ERROR;
    }
  else if (err)
    return err;

  *kind = svn_fs_base__dag_node_kind(path_node);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__get_path_created_rev(svn_revnum_t *rev,
                                  const char *path,
                                  trail_t *trail,
                                  apr_pool_t *pool)
{
  svn_revnum_t head_rev, created_rev;
  svn_fs_root_t *root;
  dag_node_t *root_dir, *path_node;
  svn_error_t *err;

  /* Get HEAD revision, */
  SVN_ERR(svn_fs_bdb__youngest_rev(&head_rev, trail->fs, trail, pool));

  /* Then convert it into a root_t, */
  SVN_ERR(svn_fs_base__dag_revision_root(&root_dir, trail->fs, head_rev,
                                         trail, pool));
  root = make_revision_root(trail->fs, head_rev, root_dir, pool);

  /* And get the dag_node for path in the root_t. */
  err = get_dag(&path_node, root, path, trail, pool);
  if (err && (err->apr_err == SVN_ERR_FS_NOT_FOUND))
    {
      svn_error_clear(err);
      *rev = SVN_INVALID_REVNUM;
      return SVN_NO_ERROR;
    }
  else if (err)
    return err;
  
  /* Find the created_rev of the dag_node. */
  SVN_ERR(svn_fs_base__dag_get_revision(&created_rev, path_node, 
                                        trail, pool));

  *rev = created_rev;
  return SVN_NO_ERROR;
}
