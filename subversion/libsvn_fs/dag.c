/* dag.c : DAG-like interface filesystem, private to libsvn_fs
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

#include <string.h>
#include <assert.h>

#include "svn_pools.h"
#include "svn_path.h"
#include "svn_time.h"
#include "svn_error.h"
#include "svn_fs.h"

#include "dag.h"
#include "err.h"
#include "fs.h"
#include "key-gen.h"
#include "node-rev.h"
#include "trail.h"
#include "id.h"
#include "reps-strings.h"
#include "revs-txns.h"

#include "util/fs_skels.h"

#include "bdb/txn-table.h"
#include "bdb/rev-table.h"
#include "bdb/nodes-table.h"
#include "bdb/copies-table.h"
#include "bdb/reps-table.h"
#include "bdb/strings-table.h"


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
     This is allocated either in this node's POOL, if the node is
     immutable, or in some trail's pool, if the node is mutable.  For
     mutable nodes, this must be reset to zero as soon as the trail in
     which we read it is completed.  Otherwise, we will end up with
     out-of-date content here.

     If you're willing to respect all the rules above, you can munge
     this yourself, but you're probably better off just calling
     `get_node_revision' and `set_node_revision', which take care of
     things for you.  */
  svn_fs__node_revision_t *node_revision;
};



/* Trivial helper/accessor functions. */
svn_node_kind_t svn_fs__dag_node_kind (dag_node_t *node)
{
  return node->kind;
}


int 
svn_fs__dag_is_file (dag_node_t *node)
{
  return (node->kind == svn_node_file);
}


int 
svn_fs__dag_is_directory (dag_node_t *node)
{
  return (node->kind == svn_node_dir);
}


const svn_fs_id_t *
svn_fs__dag_get_id (dag_node_t *node)
{
  return node->id;
}


svn_fs_t *
svn_fs__dag_get_fs (dag_node_t *node)
{
  return node->fs;
}


/* Clear NODE's cache of its node revision.  */
static void
uncache_node_revision (void *baton)
{
  dag_node_t *node = baton;
  node->node_revision = NULL;
}


/* Dup NODEREV and all associated data into POOL */
static svn_fs__node_revision_t *
copy_node_revision (svn_fs__node_revision_t *noderev,
                    apr_pool_t *pool)
{
  svn_fs__node_revision_t *nr = apr_pcalloc (pool, sizeof (*nr));
  nr->kind = noderev->kind;
  if (noderev->predecessor_id)
    nr->predecessor_id = svn_fs__id_copy (noderev->predecessor_id, pool);
  if (noderev->prop_key)
    nr->prop_key = apr_pstrdup (pool, noderev->prop_key);
  if (noderev->data_key)
    nr->data_key = apr_pstrdup (pool, noderev->data_key);
  if (noderev->edit_key)
    nr->edit_key = apr_pstrdup (pool, noderev->edit_key);

  return nr;
}


/* Set NODE's node revision cache to NODEREV, as part of TRAIL.
   NODEREV must be allocated in TRAIL->pool.  */
static void
cache_node_revision (dag_node_t *node,
                     svn_fs__node_revision_t *noderev,
                     trail_t *trail)
{
  /* ### todo: For now, we will always throw away the node revision at trail
     completion.  We used to keep it around longer because we used to
     be reading in the full file contents here, but that's no longer
     the case.  */

#if 0
  if (node_rev_is_mutable (noderev))
    {
#endif /* 0 */

      /* Mutable nodes might have other processes change their
         contents, so we must throw away this node revision once the
         trail is complete.  */
      svn_fs__record_completion (trail, uncache_node_revision, node);
      node->node_revision = noderev;

#if 0
    }
  else
    {
      /* For immutable nodes, we can cache the contents permanently,
         but we need to copy them over into the node's own pool.  */
      node->node_revision = copy_node_revision (noderev, node->pool);
    }
#endif /* 0 */
}
                     

/* Set *NODEREV_P to the cached node-revision for NODE, as part of
   TRAIL.  If NODE is immutable, the node-revision is allocated in
   NODE->pool.  If NODE is mutable, the node-revision is allocated in
   TRAIL->pool, and the cache will be cleared as soon as TRAIL
   completes.

   If you plan to change the contents of NODE, be careful!  We're
   handing you a pointer directly to our cached node-revision, not
   your own copy.  If you change it as part of some operation, but
   then some Berkeley DB function deadlocks or gets an error, you'll
   need to back out your changes, or else the cache will reflect
   changes that never got committed.  It's probably best not to change
   the structure at all.  */
static svn_error_t *
get_node_revision (svn_fs__node_revision_t **noderev_p,
                   dag_node_t *node,
                   trail_t *trail)
{
  svn_fs__node_revision_t *noderev;

  /* If we've already got a copy, there's no need to read it in.  */
  if (! node->node_revision)
    {
      /* Read it in, and cache it.  */
      SVN_ERR (svn_fs__get_node_revision (&noderev, node->fs, 
                                          node->id, trail));
      cache_node_revision (node, noderev, trail);
    }
          
  /* Now NODE->node_revision is set.  */
  *noderev_p = node->node_revision;
  return SVN_NO_ERROR;
}


/* Set the NODE-REVISION of NODE to NODEREV as part of TRAIL, and keep
   NODE's cache up to date.  NODEREV must be allocated in TRAIL->pool.  */
static svn_error_t *
set_node_revision (dag_node_t *node,
                   svn_fs__node_revision_t *noderev,
                   trail_t *trail)
{
  /* Write it out.  */
  SVN_ERR (svn_fs__put_node_revision (node->fs, node->id, noderev, trail));

  /* Since the write succeeded, update the cache.  */
  cache_node_revision (node, noderev, trail);

  return SVN_NO_ERROR;
}


int svn_fs__dag_check_mutable (dag_node_t *node, 
                               const char *txn_id)
{
  return (! strcmp (svn_fs__id_txn_id (svn_fs__dag_get_id (node)), txn_id));
}


svn_error_t *
svn_fs__dag_get_node (dag_node_t **node,
                      svn_fs_t *fs,
                      const svn_fs_id_t *id,
                      trail_t *trail)
{
  dag_node_t *new_node;
  svn_fs__node_revision_t *noderev;

  /* Construct the node. */
  new_node = apr_pcalloc (trail->pool, sizeof (*new_node));
  new_node->fs = fs;
  new_node->id = svn_fs__id_copy (id, trail->pool); 
  new_node->pool = trail->pool;

  /* Grab the contents so we can inspect the node's kind. */
  SVN_ERR (get_node_revision (&noderev, new_node, trail));

  /* Initialize the KIND attribute */
  new_node->kind = noderev->kind;
  
  /* Return a fresh new node */
  *node = new_node;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_get_revision (svn_revnum_t *rev,
                          dag_node_t *node,
                          trail_t *trail)
{
  svn_fs__transaction_t *txn;

  /* Get the txn ID from the node revision ID. */
  const char *txn_id = svn_fs__id_txn_id (svn_fs__dag_get_id (node));
  
  /* Use the txn ID to look up the transaction.  */
  SVN_ERR (svn_fs__get_txn (&txn, svn_fs__dag_get_fs (node), txn_id, trail));

  /* If the transaction has been committed, we can return the revision
     number, else we return the invalid revision number. */
  *rev = txn->revision;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_get_predecessor_id (const svn_fs_id_t **id_p,
                                dag_node_t *node,
                                trail_t *trail)
{
  svn_fs__node_revision_t *noderev;
  
  SVN_ERR (get_node_revision (&noderev, node, trail));
  *id_p = noderev->predecessor_id;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_walk_predecessors (dag_node_t *node,
                               svn_fs__dag_pred_func_t callback,
                               void *baton,
                               trail_t *trail)
{
  svn_fs_t *fs = svn_fs__dag_get_fs (node);
  dag_node_t *this_node;
  int done = 0;

  this_node = node;
  while ((! done) && this_node)
    {
      svn_fs__node_revision_t *noderev;

      /* Get the node revision for THIS_NODE so we can examine its
         predecessor id.  */
      SVN_ERR (get_node_revision (&noderev, this_node, trail));

      /* If THIS_NODE has a predecessor, replace THIS_NODE with the
         precessor, else set it to NULL.  */
      if (noderev->predecessor_id)
        SVN_ERR (svn_fs__dag_get_node (&this_node, fs, 
                                       noderev->predecessor_id, trail));
      else
        this_node = NULL;

      /* Now call the user-supplied callback with our predecessor
         node. */
      if (callback)
        SVN_ERR (callback (baton, this_node, &done, trail));
    }

  return SVN_NO_ERROR;
}


/* Trail body for svn_fs__dag_init_fs. */
static svn_error_t *
txn_body_dag_init_fs (void *fs_baton, trail_t *trail)
{
  svn_fs__node_revision_t noderev;
  svn_fs__revision_t revision;
  svn_revnum_t rev = SVN_INVALID_REVNUM;
  svn_fs_t *fs = fs_baton;
  svn_string_t date;
  const char *txn_id;
  const char *copy_id;
  svn_fs_id_t *root_id = svn_fs_parse_id ("0.0.0", 5, trail->pool);
  
  /* Create empty root directory with node revision 0.0.0. */
  memset (&noderev, 0, sizeof (noderev));
  noderev.kind = svn_node_dir;
  SVN_ERR (svn_fs__put_node_revision (fs, root_id, &noderev, trail));

  /* Create a new transaction (better have an id of "0") */
  SVN_ERR (svn_fs__create_txn (&txn_id, fs, root_id, trail));
  if (strcmp (txn_id, "0"))
    return svn_error_createf 
      (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
       "initial transaction id not `0' in filesystem `%s'", fs->path);

  /* Create a default copy (better have an id of "0") */
  SVN_ERR (svn_fs__reserve_copy_id (&copy_id, fs, trail));
  if (strcmp (copy_id, "0"))
    return svn_error_createf 
      (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
       "initial copy id not `0' in filesystem `%s'", fs->path);
  SVN_ERR (svn_fs__create_copy (copy_id, fs, NULL, SVN_INVALID_REVNUM, 
                                root_id, trail));

  /* Link it into filesystem revision 0. */
  revision.id = root_id;
  revision.proplist = NULL;
  revision.txn = txn_id;
  SVN_ERR (svn_fs__put_rev (&rev, fs, &revision, trail));
  if (rev != 0)
    return svn_error_createf (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
                              "initial revision number is not `0'"
                              " in filesystem `%s'", fs->path);

  /* Promote our transaction to a "committed" transaction. */
  SVN_ERR (svn_fs__commit_txn (fs, txn_id, rev, trail));

  /* Set a date on revision 0. */
  date.data = svn_time_to_nts (apr_time_now(), trail->pool);
  date.len = strlen (date.data);
  return svn_fs__set_rev_prop (fs, 0, SVN_PROP_REVISION_DATE, &date, trail);
}


svn_error_t *
svn_fs__dag_init_fs (svn_fs_t *fs)
{
  return svn_fs__retry_txn (fs, txn_body_dag_init_fs, fs, fs->pool);
}



/*** Directory node functions ***/

/* Some of these are helpers for functions outside this section. */

/* Given directory NODEREV in FS, set *ENTRIES_P to its entries list
   hash, as part of TRAIL, or to NULL if NODEREV has no entries.
   The entries list will be allocated in TRAIL->pool.  If NODEREV is
   not a directory, return the error SVN_ERR_FS_NOT_DIRECTORY. */
static svn_error_t *
get_dir_entries (apr_hash_t **entries_p,
                 svn_fs_t *fs,
                 svn_fs__node_revision_t *noderev,
                 trail_t *trail)
{
  apr_hash_t *entries = apr_hash_make (trail->pool);
  apr_hash_index_t *hi;
  svn_string_t entries_raw;
  skel_t *entries_skel;

  /* Error if this is not a directory. */
  if (noderev->kind != svn_node_dir)
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, 0, NULL, trail->pool,
       "Attempted to create entry in non-directory parent");

  /* If there's a DATA-KEY, there might be entries to fetch. */
  if (noderev->data_key)
    {
      /* Now we have a rep, follow through to get the entries. */
      SVN_ERR (svn_fs__rep_contents (&entries_raw, fs, 
                                     noderev->data_key, trail));
      entries_skel = svn_fs__parse_skel ((char *) entries_raw.data, 
                                         entries_raw.len, trail->pool);

      /* Were there entries?  Make a hash from them. */
      if (entries_skel)
        SVN_ERR (svn_fs__parse_entries_skel (&entries, entries_skel, 
                                             trail->pool));
    }

  /* No hash?  No problem.  */
  *entries_p = NULL;
  if (! entries)
    return SVN_NO_ERROR;

  /* Else, convert the hash from a name->id mapping to a name->dirent one.  */
  *entries_p = apr_hash_make (trail->pool);
  for (hi = apr_hash_first (trail->pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_fs_dirent_t *dirent = apr_palloc (trail->pool, sizeof (*dirent));
      
      /* KEY will be the entry name in ancestor, VAL the id.  */
      apr_hash_this (hi, &key, &klen, &val);
      dirent->name = (char *) key;
      dirent->id = val;
      apr_hash_set (*entries_p, key, klen, (void *) dirent);
    }

  /* Return our findings. */
  return SVN_NO_ERROR;
}


/* Set *ID_P to the node-id for entry NAME in PARENT, as part of
   TRAIL.  If no such entry, set *ID_P to NULL but do not error.  The
   entry is allocated in TRAIL->pool or in the same pool as PARENT;
   the caller should copy if it cares.  */
static svn_error_t *
dir_entry_id_from_node (const svn_fs_id_t **id_p, 
                        dag_node_t *parent,
                        const char *name,
                        trail_t *trail)
{
  apr_hash_t *entries;
  svn_fs_dirent_t *dirent;

  SVN_ERR (svn_fs__dag_dir_entries (&entries, parent, trail));
  if (entries)
    dirent = apr_hash_get (entries, name, APR_HASH_KEY_STRING);
  else
    dirent = NULL;
    
  *id_p = dirent ? dirent->id : NULL;
  return SVN_NO_ERROR;
}


/* Add or set in PARENT a directory entry NAME pointing to ID.
   Allocations are done in TRAIL.

   Assumptions:
   - PARENT is a mutable directory.
   - ID does not refer to an ancestor of parent
   - NAME is a single path component
*/
static svn_error_t *
set_entry (dag_node_t *parent,
           const char *name,
           const svn_fs_id_t *id,
           const char *txn_id,
           trail_t *trail)
{
  svn_fs__node_revision_t *parent_noderev;
  const char *rep_key, *mutable_rep_key;
  apr_hash_t *entries = NULL;
  svn_stream_t *wstream;
  apr_size_t len;
  svn_string_t raw_entries;
  svn_stringbuf_t *raw_entries_buf;
  skel_t *entries_skel;
  svn_fs_t *fs = svn_fs__dag_get_fs (parent);

  /* Get the parent's node-revision. */
  SVN_ERR (get_node_revision (&parent_noderev, parent, trail));
  rep_key = parent_noderev->data_key;
  SVN_ERR (svn_fs__get_mutable_rep (&mutable_rep_key, rep_key,
                                    fs, txn_id, trail));

  /* If the parent node already pointed at a mutable representation,
     we don't need to do anything.  But if it didn't, either because
     the parent didn't refer to any rep yet or because it referred to
     an immutable one, we must make the parent refer to the mutable
     rep we just created. */
  if (! svn_fs__same_keys (rep_key, mutable_rep_key))
    {
      svn_fs__node_revision_t *new_noderev = 
        copy_node_revision (parent_noderev, trail->pool);
      new_noderev->data_key = mutable_rep_key;
      SVN_ERR (set_node_revision (parent, new_noderev, trail));
    }

  /* If the new representation inherited nothing, start a new entries
     list for it.  Else, go read its existing entries list. */
  if (rep_key)
    {
      SVN_ERR (svn_fs__rep_contents (&raw_entries, fs, 
                                     mutable_rep_key, trail));
      entries_skel = svn_fs__parse_skel ((char *) raw_entries.data, 
                                         raw_entries.len, trail->pool);
      if (entries_skel)
        SVN_ERR (svn_fs__parse_entries_skel (&entries, entries_skel, 
                                             trail->pool));
    }

  /* If we still have no ENTRIES hash, make one here.  */
  if (! entries)
    entries = apr_hash_make (trail->pool);

  /* Now, add our new entry to the entries list. */
  apr_hash_set (entries, name, APR_HASH_KEY_STRING, (void *) id);

  /* Finally, replace the old entries list with the new one. */
  SVN_ERR (svn_fs__unparse_entries_skel (&entries_skel, entries, trail->pool));
  raw_entries_buf = svn_fs__unparse_skel (entries_skel, trail->pool);
  SVN_ERR (svn_fs__rep_contents_clear (fs, mutable_rep_key, txn_id, trail));
  wstream = svn_fs__rep_contents_write_stream (fs, mutable_rep_key, txn_id, 
                                               trail, trail->pool);
  len = raw_entries_buf->len;
  svn_stream_write (wstream, raw_entries_buf->data, &len);
  return SVN_NO_ERROR;
}


/* Make a new entry named NAME in PARENT, as part of TRAIL.  If IS_DIR
   is true, then the node revision the new entry points to will be a
   directory, else it will be a file.  The new node will be allocated
   in TRAIL->pool.  PARENT must be mutable, and must not have an entry
   named NAME.  */
static svn_error_t *
make_entry (dag_node_t **child_p,
            dag_node_t *parent,
            const char *name,
            svn_boolean_t is_dir,
            const char *txn_id,
            trail_t *trail)
{
  const svn_fs_id_t *new_node_id;
  svn_fs__node_revision_t new_noderev;

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component (name))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, 0, NULL, trail->pool,
       "Attempted to create a node with an illegal name `%s'", name);

  /* Make sure that parent is a directory */
  if (! svn_fs__dag_is_directory (parent))
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, 0, NULL, trail->pool,
       "Attempted to create entry in non-directory parent");
    
  /* Check that the parent is mutable. */
  if (! svn_fs__dag_check_mutable (parent, txn_id))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
       "Attempted to clone child of non-mutable node");

  /* Check that parent does not already have an entry named NAME. */
  SVN_ERR (dir_entry_id_from_node (&new_node_id, parent, name, trail));
  if (new_node_id)
    return svn_error_createf 
      (SVN_ERR_FS_ALREADY_EXISTS, 0, NULL, trail->pool,
       "Attempted to create entry that already exists");

  /* Create the new node's NODE-REVISION */
  memset (&new_noderev, 0, sizeof (new_noderev));
  new_noderev.kind = is_dir ? svn_node_dir : svn_node_file;
  SVN_ERR (svn_fs__create_node (&new_node_id, svn_fs__dag_get_fs (parent),
                                &new_noderev, txn_id, trail));

  /* Create a new dag_node_t for our new node */
  SVN_ERR (svn_fs__dag_get_node (child_p, svn_fs__dag_get_fs (parent),
                                 new_node_id, trail));

  /* We can safely call set_entry because we already know that
     PARENT is mutable, and we just created CHILD, so we know it has
     no ancestors (therefore, PARENT cannot be an ancestor of CHILD) */
  SVN_ERR (set_entry (parent, name, svn_fs__dag_get_id (*child_p), 
                      txn_id, trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_dir_entries (apr_hash_t **entries,
                         dag_node_t *node,
                         trail_t *trail)
{
  svn_fs__node_revision_t *noderev;

  SVN_ERR (get_node_revision (&noderev, node, trail));
  return get_dir_entries (entries, svn_fs__dag_get_fs (node), noderev, trail);
}


svn_error_t *
svn_fs__dag_set_entry (dag_node_t *node,
                       const char *entry_name,
                       const svn_fs_id_t *id,
                       const char *txn_id,
                       trail_t *trail)
{
  /* Check it's a directory. */
  if (! svn_fs__dag_is_directory (node))
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, 0, NULL, trail->pool,
       "Attempted to set entry in non-directory node.");
  
  /* Check it's mutable. */
  if (! svn_fs__dag_check_mutable (node, txn_id))
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, 0, NULL, trail->pool,
       "Attempted to set entry in immutable node.");

  return set_entry (node, entry_name, id, txn_id, trail);
}



/*** Proplists. ***/

svn_error_t *
svn_fs__dag_get_proplist (apr_hash_t **proplist_p,
                          dag_node_t *node,
                          trail_t *trail)
{
  svn_fs__node_revision_t *noderev;
  apr_hash_t *proplist = NULL;
  svn_string_t raw_proplist;
  skel_t *proplist_skel;
  
  /* Go get a fresh NODE-REVISION for this node. */
  SVN_ERR (get_node_revision (&noderev, node, trail));

  /* Get property key (returning early if there isn't one) . */
  if (! noderev->prop_key)
    {
      *proplist_p = NULL;
      return SVN_NO_ERROR;
    }

  /* Get the string associated with the property rep, parsing it as a
     skel, and then attempt to parse *that* into a property hash.  */
  SVN_ERR (svn_fs__rep_contents (&raw_proplist, svn_fs__dag_get_fs (node), 
                                 noderev->prop_key, trail));
  proplist_skel = svn_fs__parse_skel ((char *) raw_proplist.data, 
                                      raw_proplist.len, trail->pool);
  if (proplist_skel)
    SVN_ERR (svn_fs__parse_proplist_skel (&proplist, proplist_skel, 
                                          trail->pool));

  *proplist_p = proplist;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_set_proplist (dag_node_t *node,
                          apr_hash_t *proplist,
                          const char *txn_id,
                          trail_t *trail)
{
  svn_fs__node_revision_t *noderev;
  const char *rep_key, *mutable_rep_key;
  svn_fs_t *fs = svn_fs__dag_get_fs (node);
  
  /* Sanity check: this node better be mutable! */
  if (! svn_fs__dag_check_mutable (node, txn_id))
    {
      svn_string_t *idstr = svn_fs_unparse_id (node->id, node->pool);
      return svn_error_createf 
        (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
         "Can't set_proplist on *immutable* node-revision %s", idstr->data);
    }

  /* Go get a fresh NODE-REVISION for this node. */
  SVN_ERR (get_node_revision (&noderev, node, trail));
  rep_key = noderev->prop_key;

  /* Get a mutable version of this rep (updating the node revision if
     this isn't a NOOP)  */
  SVN_ERR (svn_fs__get_mutable_rep (&mutable_rep_key, rep_key,
                                    fs, txn_id, trail));
  if (! svn_fs__same_keys (mutable_rep_key, rep_key))
    {
      noderev->prop_key = mutable_rep_key;
      SVN_ERR (svn_fs__put_node_revision (fs, node->id, noderev, trail));
    }

  /* Replace the old property list with the new one. */
  {
    svn_stream_t *wstream;
    apr_size_t len;
    skel_t *proplist_skel;
    svn_stringbuf_t *raw_proplist_buf;

    SVN_ERR (svn_fs__unparse_proplist_skel (&proplist_skel, proplist, 
                                            trail->pool));
    raw_proplist_buf = svn_fs__unparse_skel (proplist_skel, trail->pool);
    wstream = svn_fs__rep_contents_write_stream (fs, mutable_rep_key, txn_id,
                                                 trail, trail->pool);
    SVN_ERR (svn_fs__rep_contents_clear (fs, mutable_rep_key, txn_id, trail));
    len = raw_proplist_buf->len;
    SVN_ERR (svn_stream_write (wstream, raw_proplist_buf->data, &len));
  }

  return SVN_NO_ERROR;
}



/*** Roots. ***/

svn_error_t *
svn_fs__dag_revision_root (dag_node_t **node_p,
                           svn_fs_t *fs,
                           svn_revnum_t rev,
                           trail_t *trail)
{
  const svn_fs_id_t *root_id;

  SVN_ERR (svn_fs__rev_get_root (&root_id, fs, rev, trail));
  return svn_fs__dag_get_node (node_p, fs, root_id, trail);
}


svn_error_t *
svn_fs__dag_txn_root (dag_node_t **node_p,
                      svn_fs_t *fs,
                      const char *txn_id,
                      trail_t *trail)
{
  const svn_fs_id_t *root_id, *ignored;
  
  SVN_ERR (svn_fs__get_txn_ids (&root_id, &ignored, fs, txn_id, trail));
  return svn_fs__dag_get_node (node_p, fs, root_id, trail);
}


svn_error_t *
svn_fs__dag_txn_base_root (dag_node_t **node_p,
                           svn_fs_t *fs,
                           const char *txn_id,
                           trail_t *trail)
{
  const svn_fs_id_t *base_root_id, *ignored;
  
  SVN_ERR (svn_fs__get_txn_ids (&ignored, &base_root_id, fs, txn_id, trail));
  return svn_fs__dag_get_node (node_p, fs, base_root_id, trail);
}


svn_error_t *
svn_fs__dag_clone_child (dag_node_t **child_p,
                         dag_node_t *parent,
                         const char *name,
                         const char *copy_id,
                         const char *txn_id,
                         trail_t *trail)
{
  dag_node_t *cur_entry; /* parent's current entry named NAME */
  const svn_fs_id_t *new_node_id; /* node id we'll put into NEW_NODE */
  svn_fs_t *fs = svn_fs__dag_get_fs (parent);

  /* First check that the parent is mutable. */
  if (! svn_fs__dag_check_mutable (parent, txn_id))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
       "Attempted to clone child of non-mutable node");

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component (name))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, 0, NULL, trail->pool,
       "Attempted to make a child clone with an illegal name `%s'", name);

  /* Find the node named NAME in PARENT's entries list if it exists. */
  SVN_ERR (svn_fs__dag_open (&cur_entry, parent, name, trail));

  /* Check for mutability in the node we found.  If it's mutable, we
     don't need to clone it. */
  if (svn_fs__dag_check_mutable (cur_entry, txn_id))
    {
      /* This has already been cloned */
      new_node_id = cur_entry->id;
    }
  else
    {
      svn_fs__node_revision_t *noderev;
      
      /* Go get a fresh NODE-REVISION for current child node. */
      SVN_ERR (get_node_revision (&noderev, cur_entry, trail));
      
      /* Do the clone thingy here. */
      noderev->predecessor_id = svn_fs__id_copy (cur_entry->id, trail->pool);
      SVN_ERR (svn_fs__create_successor (&new_node_id, fs, cur_entry->id, 
                                         noderev, copy_id, txn_id, trail));
      
      /* Replace the ID in the parent's ENTRY list with the ID which
         refers to the mutable clone of this child. */
      SVN_ERR (set_entry (parent, name, new_node_id, txn_id, trail));
    }

  /* Initialize the youngster. */
  return svn_fs__dag_get_node (child_p, fs, new_node_id, trail);
}



svn_error_t *
svn_fs__dag_clone_root (dag_node_t **root_p,
                        svn_fs_t *fs,
                        const char *txn_id,
                        trail_t *trail)
{
  const svn_fs_id_t *base_root_id, *root_id;
  svn_fs__node_revision_t *noderev;
  
  /* Get the node ID's of the root directories of the transaction and
     its base revision.  */
  SVN_ERR (svn_fs__get_txn_ids (&root_id, &base_root_id, fs, txn_id, trail));

  /* Oh, give me a clone...
     (If they're the same, we haven't cloned the transaction's root
     directory yet.)  */
  if (svn_fs__id_eq (root_id, base_root_id)) 
    {
      /* Of my own flesh and bone...
         (Get the NODE-REVISION for the base node, and then write
         it back out as the clone.) */
      SVN_ERR (svn_fs__get_node_revision (&noderev, fs, base_root_id, trail));

      /* Store it. */
      /* ### todo: Does it even makes sense to have a different copy id for
         the root node?  That is, does this function need a copy_id
         passed in?  */
      noderev->predecessor_id = svn_fs__id_copy (base_root_id, trail->pool);
      SVN_ERR (svn_fs__create_successor (&root_id, fs, base_root_id, 
                                         noderev, 
                                         svn_fs__id_copy_id (base_root_id),
                                         txn_id, trail));

      /* ... And when it is grown
       *      Then my own little clone
       *        Will be of the opposite sex!
       */
      SVN_ERR (svn_fs__set_txn_root (fs, txn_id, root_id, trail));
    }

  /* One way or another, root_id now identifies a cloned root node. */
  SVN_ERR (svn_fs__dag_get_node (root_p, fs, root_id, trail));

  /*
   * (Sung to the tune of "Home, Home on the Range", with thanks to
   * Randall Garrett and Isaac Asimov.)
   */

  return SVN_NO_ERROR;
}


/* Delete the directory entry named NAME from PARENT, as part of
   TRAIL.  PARENT must be mutable.  NAME must be a single path
   component.  If REQUIRE_EMPTY is true and the node being deleted is
   a directory, it must be empty.  

   If return SVN_ERR_FS_NO_SUCH_ENTRY, then there is no entry NAME in
   PARENT.  */
static svn_error_t *
delete_entry (dag_node_t *parent,
              const char *name,
              svn_boolean_t require_empty,
              const char *txn_id,
              trail_t *trail)
{
  svn_fs__node_revision_t *parent_noderev;
  const char *rep_key, *mutable_rep_key;
  apr_hash_t *entries = NULL;
  skel_t *entries_skel;
  svn_fs_t *fs = parent->fs;
  svn_string_t str;
  svn_fs_id_t *id = NULL;
  dag_node_t *node; 

  /* Make sure parent is a directory. */
  if (! svn_fs__dag_is_directory (parent))
    return svn_error_createf
      (SVN_ERR_FS_NOT_DIRECTORY, 0, NULL, parent->pool,
       "Attempted to delete entry `%s' from *non*-directory node.", name);    

  /* Make sure parent is mutable. */
  if (! svn_fs__dag_check_mutable (parent, txn_id))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, parent->pool,
       "Attempted to delete entry `%s' from immutable directory node.", name);

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component (name))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, 0, NULL, trail->pool,
       "Attempted to delete a node with an illegal name `%s'", name);

  /* Get a fresh NODE-REVISION for the parent node. */
  SVN_ERR (get_node_revision (&parent_noderev, parent, trail));

  /* Get the key for the parent's entries list (data) representation. */
  rep_key = parent_noderev->data_key;

  /* No REP_KEY means no representation, and no representation means
     no data, and no data means no entries...there's nothing here to
     delete! */
  if (! rep_key)
    return svn_error_createf 
      (SVN_ERR_FS_NO_SUCH_ENTRY, 0, NULL, trail->pool,
       "Delete failed--directory has no entry `%s'", name);

  /* Ensure we have a key to a mutable representation of the entries
     list.  We'll have to update the NODE-REVISION if it points to an
     immutable version.  */
  SVN_ERR (svn_fs__get_mutable_rep (&mutable_rep_key, rep_key,
                                    fs, txn_id, trail));
  if (! svn_fs__same_keys (mutable_rep_key, rep_key))
    {
      svn_fs__node_revision_t *new_noderev =
        copy_node_revision (parent_noderev, trail->pool);
      new_noderev->data_key = mutable_rep_key;
      SVN_ERR (set_node_revision (parent, new_noderev, trail));
    }

  /* Read the representation, then use it to get the string that holds
     the entries list.  Parse that list into a skel, and parse *that*
     into a hash. */
  SVN_ERR (svn_fs__rep_contents (&str, fs, mutable_rep_key, trail));
  entries_skel = svn_fs__parse_skel ((char *) str.data, str.len, trail->pool);
  if (entries_skel)
    SVN_ERR (svn_fs__parse_entries_skel (&entries, entries_skel, trail->pool));
  
  /* Find NAME in the ENTRIES skel.  */
  if (entries)
    id = apr_hash_get (entries, name, APR_HASH_KEY_STRING);

  /* If we never found ID in ENTRIES (perhaps because there are no
     ENTRIES, perhaps because ID just isn't in the existing ENTRIES
     ... it doesn't matter), return an error.  */
  if (! id)
    return svn_error_createf 
      (SVN_ERR_FS_NO_SUCH_ENTRY, 0, NULL, trail->pool,
       "Delete failed--directory has no entry `%s'", name);

  /* Use the ID of this ENTRY to get the entry's node.  If the node we
     get is a directory, make sure it meets up to our emptiness
     standards (as determined by REQUIRE_EMPTY).  */
  SVN_ERR (svn_fs__dag_get_node (&node, svn_fs__dag_get_fs (parent), 
                                 id, trail));
  if (svn_fs__dag_is_directory (node))
    {
      apr_hash_t *entries_here;
      SVN_ERR (svn_fs__dag_dir_entries (&entries_here, node, trail));
      if (require_empty && entries_here && apr_hash_count (entries_here))
        return svn_error_createf
          (SVN_ERR_DIR_NOT_EMPTY, 0, NULL, parent->pool,
           "Attempt to delete non-empty directory `%s'.", name);
    }

  /* If mutable, remove it and any mutable children from db. */
  SVN_ERR (svn_fs__dag_delete_if_mutable (parent->fs, id, txn_id, trail));
        
  /* Remove this entry from its parent's entries list. */
  apr_hash_set (entries, name, APR_HASH_KEY_STRING, NULL);

  /* Replace the old entries list with the new one. */
  {
    svn_stream_t *ws;
    svn_stringbuf_t *unparsed_entries;
    apr_size_t len;
    
    SVN_ERR (svn_fs__unparse_entries_skel (&entries_skel, entries, 
                                           trail->pool));
    unparsed_entries = svn_fs__unparse_skel (entries_skel, trail->pool);
    SVN_ERR (svn_fs__rep_contents_clear (fs, mutable_rep_key, txn_id, trail));
    ws = svn_fs__rep_contents_write_stream (fs, mutable_rep_key, txn_id,
                                            trail, trail->pool);
    len = unparsed_entries->len;
    SVN_ERR (svn_stream_write (ws, unparsed_entries->data, &len));
  }
    
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_delete (dag_node_t *parent,
                    const char *name,
                    const char *txn_id,
                    trail_t *trail)
{
  return delete_entry (parent, name, TRUE, txn_id, trail);
}


svn_error_t *
svn_fs__dag_delete_tree (dag_node_t *parent,
                         const char *name,
                         const char *txn_id,
                         trail_t *trail)
{
  return delete_entry (parent, name, FALSE, txn_id, trail);
}


svn_error_t *
svn_fs__dag_delete_if_mutable (svn_fs_t *fs,
                               const svn_fs_id_t *id,
                               const char *txn_id,
                               trail_t *trail)
{
  dag_node_t *node;
  svn_fs__node_revision_t *noderev;

  SVN_ERR (svn_fs__dag_get_node (&node, fs, id, trail));

  /* If immutable, do nothing and return immediately. */
  if (! svn_fs__dag_check_mutable (node, txn_id))
    return SVN_NO_ERROR;

  /* Else it's mutable.  Recurse on directories... */
  if (svn_fs__dag_is_directory (node))
    {
      apr_hash_t *entries;
      apr_hash_index_t *hi;
      apr_pool_t *pool = trail->pool;

      /* Loop over hash entries */
      SVN_ERR (svn_fs__dag_dir_entries (&entries, node, trail));
      if (entries)
        {
          for (hi = apr_hash_first (pool, entries); 
               hi; 
               hi = apr_hash_next (hi))
            {
              void *val;
              svn_fs_dirent_t *dirent;
              
              apr_hash_this (hi, NULL, NULL, &val);
              dirent = val;
              SVN_ERR (svn_fs__dag_delete_if_mutable (fs, dirent->id, 
                                                      txn_id, trail));
            }
        }
    }

  /* ... then delete the node itself, after deleting any mutable
     representations and strings it points to. */

  /* Get a fresh node-revision. */
  SVN_ERR (svn_fs__get_node_revision (&noderev, fs, id, trail));

  /* Delete any mutable property representation. */
  if (noderev->prop_key)
    SVN_ERR (svn_fs__delete_rep_if_mutable (fs, noderev->prop_key,
                                            txn_id, trail));
  
  /* Delete any mutable data representation. */
  if (noderev->data_key)
    SVN_ERR (svn_fs__delete_rep_if_mutable (fs, noderev->data_key, 
                                            txn_id, trail));

  /* Delete any mutable edit representation (files only). */
  if (noderev->edit_key)
    SVN_ERR (svn_fs__delete_rep_if_mutable (fs, noderev->edit_key, 
                                            txn_id, trail));

  /* Delete the node revision itself. */
  SVN_ERR (svn_fs__delete_node_revision (fs, id, trail));
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_make_file (dag_node_t **child_p,
                       dag_node_t *parent,
                       const char *name,
                       const char *txn_id, 
                       trail_t *trail)
{
  /* Call our little helper function */
  return make_entry (child_p, parent, name, FALSE, txn_id, trail);
}


svn_error_t *
svn_fs__dag_make_dir (dag_node_t **child_p,
                      dag_node_t *parent,
                      const char *name,
                      const char *txn_id, 
                      trail_t *trail)
{
  /* Call our little helper function */
  return make_entry (child_p, parent, name, TRUE, txn_id, trail);
}


/* ### somebody todo: figure out why this *reaaaaaaally* exists.  It
   has no callers (though kfogel has some speculation about a possible
   use (see tree.c:merge) */
svn_error_t *
svn_fs__dag_link (dag_node_t *parent,
                  dag_node_t *child,
                  const char *name,
                  const char *txn_id, 
                  trail_t *trail)
{
  const svn_fs_id_t *entry_id;

  /* Make sure that parent is a directory */
  if (! svn_fs__dag_is_directory (parent))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_DIRECTORY, 0, NULL, trail->pool,
       "Attempted to create entry in non-directory parent");
    
  /* Make sure parent is mutable */
  if (! svn_fs__dag_check_mutable (parent, txn_id))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
       "Can't add a link from an immutable parent");

  /* Make sure child is IMmutable */
  if (svn_fs__dag_check_mutable (child, txn_id))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
       "Can't add a link to a mutable child");

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component (name))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, 0, NULL, trail->pool,
       "Attempted to link to a node with an illegal name `%s'", name);

  /* Verify that this parent node does not already have an entry named NAME. */
  SVN_ERR (dir_entry_id_from_node (&entry_id, parent, name, trail));
  if (entry_id)
    return svn_error_createf 
      (SVN_ERR_FS_ALREADY_EXISTS, 0, NULL, trail->pool,
       "Attempted to create entry that already exists");

  /* We can safely call set_entry because we already know that PARENT
     is mutable, and we know that CHILD is immutable (since every
     parent of a mutable node is mutable itself, we know that CHILD
     can't be equal to, or a parent of, PARENT).  */
  return set_entry (parent, name, svn_fs__dag_get_id (child), txn_id, trail);
}


svn_error_t *
svn_fs__dag_get_contents (svn_stream_t **contents,
                          dag_node_t *file,
                          apr_pool_t *pool,
                          trail_t *trail)
{ 
  svn_fs__node_revision_t *noderev;

  /* Make sure our node is a file. */
  if (! svn_fs__dag_is_file (file))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_FILE, 0, NULL, trail->pool,
       "Attempted to get textual contents of a *non*-file node.");
  
  /* Go get a fresh node-revision for FILE. */
  SVN_ERR (get_node_revision (&noderev, file, trail));

  /* Our job is to _return_ a stream on the file's contents, so the
     stream has to be trail-independent.  Here, we pass NULL to tell
     the stream that we're not providing it a trail that lives across
     reads.  This means the stream will do each read in a one-off,
     temporary trail.  */
  *contents = svn_fs__rep_contents_read_stream (file->fs, noderev->data_key,
                                                0, NULL, pool);

  /* Note that we're not registering any `close' func, because there's
     nothing to cleanup outside of our trail.  When the trail is
     freed, the stream/baton will be too. */ 

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_file_length (apr_size_t *length,
                         dag_node_t *file,
                         trail_t *trail)
{ 
  svn_fs__node_revision_t *noderev;

  /* Make sure our node is a file. */
  if (! svn_fs__dag_is_file (file))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_FILE, 0, NULL, trail->pool,
       "Attempted to get length of a *non*-file node.");

  /* Go get a fresh node-revision for FILE, and . */
  SVN_ERR (get_node_revision (&noderev, file, trail));
  if (noderev->data_key)
    SVN_ERR (svn_fs__rep_contents_size (length, file->fs,
                                        noderev->data_key, trail));
  else
    *length = 0;

  return SVN_NO_ERROR;
}




svn_error_t *
svn_fs__dag_get_edit_stream (svn_stream_t **contents,
                             dag_node_t *file,
                             apr_pool_t *pool,
                             const char *txn_id,
                             trail_t *trail)
{
  svn_fs_t *fs = file->fs;   /* just for nicer indentation */
  svn_fs__node_revision_t *noderev;
  const char *mutable_rep_key;
  svn_stream_t *ws;

  /* Make sure our node is a file. */
  if (! svn_fs__dag_is_file (file))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_FILE, 0, NULL, trail->pool,
       "Attempted to set textual contents of a *non*-file node.");
  
  /* Make sure our node is mutable. */
  if (! svn_fs__dag_check_mutable (file, txn_id))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
       "Attempted to set textual contents of an immutable node.");

  /* Get the node revision. */
  SVN_ERR (get_node_revision (&noderev, file, trail));

  /* If this node already has an EDIT-DATA-KEY, destroy the data
     associated with that key.  */
  if (noderev->edit_key)
    SVN_ERR (svn_fs__delete_rep_if_mutable (fs, noderev->edit_key,
                                            txn_id, trail));

  /* Now, let's ensure that we have a new EDIT-DATA-KEY available for
     use. */
  SVN_ERR (svn_fs__get_mutable_rep (&mutable_rep_key, NULL, fs,
                                    txn_id, trail));
  
  /* We made a new rep, so update the node revision. */
  noderev->edit_key = mutable_rep_key;
  SVN_ERR (svn_fs__put_node_revision (fs, file->id, noderev, trail));

  /* Return a writable stream with which to set new contents. */
  ws = svn_fs__rep_contents_write_stream (fs, mutable_rep_key, txn_id,
                                          NULL, pool);
  *contents = ws;

  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs__dag_finalize_edits (dag_node_t *file,
                            const char *txn_id, 
                            trail_t *trail)
{
  svn_fs_t *fs = file->fs;   /* just for nicer indentation */
  svn_fs__node_revision_t *noderev;
  const char *old_data_key;
  
  /* Make sure our node is a file. */
  if (! svn_fs__dag_is_file (file))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_FILE, 0, NULL, trail->pool,
       "Attempted to set textual contents of a *non*-file node.");
  
  /* Make sure our node is mutable. */
  if (! svn_fs__dag_check_mutable (file, txn_id))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
       "Attempted to set textual contents of an immutable node.");

  /* Get the node revision. */
  SVN_ERR (get_node_revision (&noderev, file, trail));

  /* If this node has no EDIT-DATA-KEY, this is a no-op. */
  if (! noderev->edit_key)
    return SVN_NO_ERROR;

  /* Now, we want to delete the old representation and replace it with
     the new.  Of course, we don't actually delete anything until
     everything is being properly referred to by the node-revision
     skel. */
  old_data_key = noderev->data_key;
  noderev->data_key = noderev->edit_key;
  noderev->edit_key = NULL;
  SVN_ERR (svn_fs__put_node_revision (fs, file->id, noderev, trail));
  
  /* Only *now* can we safely destroy the old representation (if it
     even existed in the first place). */
  if (old_data_key)
    SVN_ERR (svn_fs__delete_rep_if_mutable (fs, old_data_key, txn_id, trail));

  return SVN_NO_ERROR;
}



dag_node_t *
svn_fs__dag_dup (dag_node_t *node,
                 trail_t *trail)
{
  /* Allocate our new node. */
  dag_node_t *new_node = apr_pcalloc (trail->pool, sizeof (*new_node));

  new_node->fs = node->fs;
  new_node->pool = trail->pool;
  new_node->id = svn_fs__id_copy (node->id, node->pool);
  new_node->kind = node->kind;

  /* Leave new_node->node_revision zero for now, so it'll get read in.
     We can get fancy and duplicate node's cache later.  */

  return new_node;
}


svn_error_t *
svn_fs__dag_open (dag_node_t **child_p,
                  dag_node_t *parent,
                  const char *name,
                  trail_t *trail)
{
  const svn_fs_id_t *node_id;

  /* Ensure that NAME exists in PARENT's entry list. */
  SVN_ERR (dir_entry_id_from_node (&node_id, parent, name, trail));
  if (! node_id)
    return svn_error_createf 
      (SVN_ERR_FS_NOT_FOUND, 0, NULL, trail->pool,
       "Attempted to open non-existant child node \"%s\"", name);
  
  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component (name))
    return svn_error_createf 
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, 0, NULL, trail->pool,
       "Attempted to open node with an illegal name `%s'", name);

  /* Now get the node that was requested. */
  return svn_fs__dag_get_node (child_p, svn_fs__dag_get_fs (parent),
                               node_id, trail);
}


svn_error_t *
svn_fs__dag_copy (dag_node_t *to_node,
                  const char *entry,
                  dag_node_t *from_node,
                  svn_boolean_t preserve_history,
                  svn_revnum_t from_rev,
                  const char *from_path,
                  const char *txn_id, 
                  trail_t *trail)
{
  const svn_fs_id_t *id;
  
  if (preserve_history)
    {
      svn_fs__node_revision_t *from_noderev, *to_noderev;
      const char *copy_id;
      svn_fs_t *fs = svn_fs__dag_get_fs (from_node);
      const svn_fs_id_t *src_id = svn_fs__dag_get_id (from_node);

      /* Make a copy of the original node revision. */
      SVN_ERR (get_node_revision (&from_noderev, from_node, trail));
      to_noderev = copy_node_revision (from_noderev, trail->pool);
      
      /* Reserve a copy ID for this new copy. */
      SVN_ERR (svn_fs__reserve_copy_id (&copy_id, fs, trail));

      /* Create a successor with its predecessor pointing at the copy
         source. */
      to_noderev->predecessor_id = svn_fs__id_copy (src_id, trail->pool);
      SVN_ERR (svn_fs__create_successor (&id, fs, src_id, to_noderev,
                                         copy_id, txn_id, trail));

      /* Now that we've done the copy, we need to add the information
         about the copy to the `copies' table, using the COPY_ID we
         reserved above.  */
      SVN_ERR (svn_fs__create_copy 
               (copy_id, fs, 
                svn_fs__canonicalize_abspath (from_path, trail->pool), 
                from_rev, id, trail));

      /* Finally, add the COPY_ID to the transaction's list of copies
         so that, if this transaction is aborted, the `copies' table
         entry we added above will be cleaned up. */
      SVN_ERR (svn_fs__add_txn_copy (fs, txn_id, copy_id, trail));
    }
  else  /* don't preserve history */
    {
      id = svn_fs__dag_get_id (from_node);
    }
      
  /* Set the entry in to_node to the new id. */
  SVN_ERR (svn_fs__dag_set_entry (to_node, entry, id, txn_id, trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_copied_from (svn_revnum_t *rev_p,
                         const char **path_p,
                         dag_node_t *node,
                         trail_t *trail)
{
  svn_fs__node_revision_t *noderev;
  const svn_fs_id_t *id = svn_fs__dag_get_id (node), *pred_id;
  
  /* Initialize the return values to mean "not a copy". */
  *rev_p = SVN_INVALID_REVNUM;
  *path_p = NULL;
  
  SVN_ERR (get_node_revision (&noderev, node, trail));
  if ((pred_id = noderev->predecessor_id))
    {
      const char *id_copy_id = svn_fs__id_copy_id (id);
      const char *pred_copy_id = svn_fs__id_copy_id (pred_id);
      
      /* If NODE's copy id differs from that of its predecessor... */
      if (strcmp (id_copy_id, pred_copy_id))
        {
          /* ... then NODE was either the target of a copy operation,
             a copied subtree item.  We examine the actual copy record
             to determine which is the case.  */
          svn_fs__copy_t *copy;
          SVN_ERR (svn_fs__get_copy (&copy, svn_fs__dag_get_fs (node),
                                     id_copy_id, trail));
          if (svn_fs__id_eq (copy->dst_noderev_id, id))
            {
              *rev_p = copy->src_revision;
              *path_p = copy->src_path;
            }
        }
    }
  
  return SVN_NO_ERROR;
}



/*** Deltification ***/

svn_error_t *
svn_fs__dag_deltify (dag_node_t *target,
                     dag_node_t *source,
                     int props_only,
                     trail_t *trail)
{
  svn_fs__node_revision_t *source_nr, *target_nr;
  svn_fs_t *fs = svn_fs__dag_get_fs (target);

  /* Get node revisions for the two nodes.  */
  SVN_ERR (get_node_revision (&target_nr, target, trail));
  SVN_ERR (get_node_revision (&source_nr, source, trail));

  /* If TARGET and SOURCE both have properties, and are not sharing a
     property key, deltify TARGET's properties.  */
  if (target_nr->prop_key 
      && source_nr->prop_key
      && (strcmp (target_nr->prop_key, source_nr->prop_key)))
    SVN_ERR (svn_fs__rep_deltify (fs, target_nr->prop_key, 
                                  source_nr->prop_key, trail));

  /* If we are not only attending to properties, and if TARGET and
     SOURCE both have data, and are not sharing a data key, deltify
     TARGET's data.  */
  if ((! props_only)
      && target_nr->data_key 
      && source_nr->data_key 
      && (strcmp (target_nr->data_key, source_nr->data_key)))     
   SVN_ERR (svn_fs__rep_deltify (fs, target_nr->data_key, 
                                 source_nr->data_key, trail));

  return SVN_NO_ERROR;
}




/*** Committing ***/

svn_error_t *
svn_fs__dag_commit_txn (svn_revnum_t *new_rev,
                        svn_fs_t *fs,
                        const char *txn_id,
                        trail_t *trail)
{
  dag_node_t *root;
  svn_fs__revision_t revision;
  svn_fs__transaction_t *transaction;
  svn_string_t date;

  SVN_ERR (svn_fs__dag_txn_root (&root, fs, txn_id, trail));

  /* Add new revision entry to `revisions' table, copying the
     transaction's property list.  */
  SVN_ERR (svn_fs__get_txn (&transaction, fs, txn_id, trail));
  revision.id = root->id;
  revision.proplist = transaction->proplist;
  revision.txn = txn_id;
  if (new_rev)
    *new_rev = SVN_INVALID_REVNUM;
  SVN_ERR (svn_fs__put_rev (new_rev, fs, &revision, trail));

  /* Set a date on the commit.  We wait until now to fetch the date,
     so it's definitely newer than any previous revision's date. */
  date.data = svn_time_to_nts (apr_time_now(), trail->pool);
  date.len = strlen (date.data);
  SVN_ERR (svn_fs__set_rev_prop (fs, *new_rev, SVN_PROP_REVISION_DATE, 
                                 &date, trail));

  /* Promote the unfinished transaction to a committed one. */
  SVN_ERR (svn_fs__commit_txn (fs, txn_id, *new_rev, trail));

  return SVN_NO_ERROR;
}



/*** Comparison. ***/

svn_error_t *
svn_fs__things_different (int *props_changed,
                          int *contents_changed,
                          dag_node_t *node1,
                          dag_node_t *node2,
                          trail_t *trail)
{
  svn_fs__node_revision_t *noderev1, *noderev2;

  /* If we have no place to store our results, don't bother doing
     anything. */
  if (! props_changed && ! contents_changed)
    return SVN_NO_ERROR;

  /* The the node revision skels for these two nodes. */
  SVN_ERR (get_node_revision (&noderev1, node1, trail));
  SVN_ERR (get_node_revision (&noderev2, node2, trail));

  /* Compare property keys. */
  if (props_changed != NULL)
    *props_changed = (! svn_fs__same_keys (noderev1->prop_key, 
                                           noderev2->prop_key));

  /* Compare contents keys. */
  if (contents_changed != NULL)
    *contents_changed = (! svn_fs__same_keys (noderev1->data_key, 
                                              noderev2->data_key));
  
  return SVN_NO_ERROR;
}



struct is_ancestor_baton
{
  const svn_fs_id_t *node1_id;
  int is_ancestor;
  int need_parent; /* non-zero if we only care about parenthood, not
                      full ancestry */
};


static svn_error_t *
is_ancestor_callback (void *baton,
                      dag_node_t *node,
                      int *done,
                      trail_t *trail)
{
  struct is_ancestor_baton *b = baton;

  /* If there is no NODE, then this is the last call, and we didn't
     find an ancestor.  But if there is ... */
  if (node)
    {
      /* ... compare NODE's ID with the ID we're looking for. */
      if (svn_fs__id_eq (b->node1_id, svn_fs__dag_get_id (node)))
        b->is_ancestor = 1;

      /* Now, if we only are interested in parenthood, we don't care
         to look any further than this. */
      if (b->need_parent)
        *done = 1;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__dag_is_ancestor (int *is_ancestor,
                         dag_node_t *node1,
                         dag_node_t *node2,
                         trail_t *trail)
{
  struct is_ancestor_baton baton;
  const svn_fs_id_t 
    *id1 = svn_fs__dag_get_id (node1),
    *id2 = svn_fs__dag_get_id (node2);

  /* Pessimism. */
  *is_ancestor = 0;

  /* Ancestry holds relatedness as a prerequisite. */
  if (! svn_fs_check_related (id1, id2))
    return SVN_NO_ERROR;

  baton.is_ancestor = 0;
  baton.need_parent = 0;
  baton.node1_id = id1;

  SVN_ERR (svn_fs__dag_walk_predecessors (node2, is_ancestor_callback,
                                          &baton, trail));
  if (baton.is_ancestor)
    *is_ancestor = 1;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_is_parent (int *is_parent,
                       dag_node_t *node1,
                       dag_node_t *node2,
                       trail_t *trail)
{
  struct is_ancestor_baton baton;
  const svn_fs_id_t 
    *id1 = svn_fs__dag_get_id (node1),
    *id2 = svn_fs__dag_get_id (node2);

  /* Pessimism. */
  *is_parent = 0;

  /* Parentry holds relatedness as a prerequisite. */
  if (! svn_fs_check_related (id1, id2))
    return SVN_NO_ERROR;

  baton.is_ancestor = 0;
  baton.need_parent = 1;
  baton.node1_id = id1;

  SVN_ERR (svn_fs__dag_walk_predecessors (node2, is_ancestor_callback,
                                          &baton, trail));
  if (baton.is_ancestor)
    *is_parent = 1;

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
