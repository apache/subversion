/* dag.c : DAG-like interface filesystem, private to libsvn_fs
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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
#include "svn_time.h"
#include "svn_error.h"
#include "svn_md5.h"
#include "svn_fs.h"
#include "svn_props.h"

#include "dag.h"
#include "err.h"
#include "fs.h"
#include "key-gen.h"
#include "node-rev.h"
#include "trail.h"
#include "reps-strings.h"
#include "revs-txns.h"
#include "id.h"

#include "util/fs_skels.h"

#include "bdb/txn-table.h"
#include "bdb/rev-table.h"
#include "bdb/nodes-table.h"
#include "bdb/copies-table.h"
#include "bdb/reps-table.h"
#include "bdb/strings-table.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"


/* Initializing a filesystem.  */

struct dag_node_t
{
  /*** NOTE: Keeping in-memory representations of disk data that can
       be changed by other accessors is a nasty business.  Such
       representations are basically a cache with some pretty complex
       invalidation rules.  For example, the "node revision"
       associated with a DAG node ID can look completely different to
       a process that has modified that information as part of a
       Berkeley DB transaction than it does to some other process.
       That said, there are some aspects of a "node revision" which
       never change, like its 'id' or 'kind'.  Our best bet is to
       limit ourselves to exposing outside of this interface only
       those immutable aspects of a DAG node representation.  ***/

  /* The filesystem this dag node came from. */
  svn_fs_t *fs;

  /* The node revision ID for this dag node. */
  svn_fs_id_t *id;

  /* The node's type (file, dir, etc.) */
  svn_node_kind_t kind;

  /* the path at which this node was created. */
  const char *created_path;
};



/* Trivial helper/accessor functions. */
svn_node_kind_t svn_fs_base__dag_node_kind(dag_node_t *node)
{
  return node->kind;
}


const svn_fs_id_t *
svn_fs_base__dag_get_id(dag_node_t *node)
{
  return node->id;
}


const char *
svn_fs_base__dag_get_created_path(dag_node_t *node)
{
  return node->created_path;
}


svn_fs_t *
svn_fs_base__dag_get_fs(dag_node_t *node)
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
    nr->predecessor_id = svn_fs_base__id_copy(noderev->predecessor_id, pool);
  nr->predecessor_count = noderev->predecessor_count;
  if (noderev->prop_key)
    nr->prop_key = apr_pstrdup(pool, noderev->prop_key);
  if (noderev->data_key)
    nr->data_key = apr_pstrdup(pool, noderev->data_key);
  if (noderev->edit_key)
    nr->edit_key = apr_pstrdup(pool, noderev->edit_key);
  if (noderev->created_path)
    nr->created_path = apr_pstrdup(pool, noderev->created_path);
  return nr;
}


svn_boolean_t svn_fs_base__dag_check_mutable(dag_node_t *node,
                                             const char *txn_id)
{
  return (strcmp(svn_fs_base__id_txn_id(svn_fs_base__dag_get_id(node)),
                 txn_id) == 0);
}


svn_error_t *
svn_fs_base__dag_get_node(dag_node_t **node,
                          svn_fs_t *fs,
                          const svn_fs_id_t *id,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  dag_node_t *new_node;
  node_revision_t *noderev;

  /* Construct the node. */
  new_node = apr_pcalloc(pool, sizeof(*new_node));
  new_node->fs = fs;
  new_node->id = svn_fs_base__id_copy(id, pool);

  /* Grab the contents so we can cache some of the immutable parts of it. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, id, trail, pool));

  /* Initialize the KIND and CREATED_PATH attributes */
  new_node->kind = noderev->kind;
  new_node->created_path = noderev->created_path;

  /* Return a fresh new node */
  *node = new_node;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_get_revision(svn_revnum_t *rev,
                              dag_node_t *node,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  /* Use the txn ID from the NODE's id to look up the transaction and
     get its revision number.  */
  return svn_fs_base__txn_get_revision
    (rev, svn_fs_base__dag_get_fs(node),
     svn_fs_base__id_txn_id(svn_fs_base__dag_get_id(node)), trail, pool);
}


svn_error_t *
svn_fs_base__dag_get_predecessor_id(const svn_fs_id_t **id_p,
                                    dag_node_t *node,
                                    trail_t *trail,
                                    apr_pool_t *pool)
{
  node_revision_t *noderev;

  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, node->fs, node->id, 
                                        trail, pool));
  *id_p = noderev->predecessor_id;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_get_predecessor_count(int *count,
                                       dag_node_t *node,
                                       trail_t *trail,
                                       apr_pool_t *pool)
{
  node_revision_t *noderev;

  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, node->fs, node->id, 
                                        trail, pool));
  *count = noderev->predecessor_count;
  return SVN_NO_ERROR;
}


/* Trail body for svn_fs_base__dag_init_fs. */
static svn_error_t *
txn_body_dag_init_fs(void *baton, 
                     trail_t *trail)
{
  node_revision_t noderev;
  revision_t revision;
  svn_revnum_t rev = SVN_INVALID_REVNUM;
  svn_fs_t *fs = trail->fs;
  svn_string_t date;
  const char *txn_id;
  const char *copy_id;
  svn_fs_id_t *root_id = svn_fs_base__id_create("0", "0", "0", trail->pool);

  /* Create empty root directory with node revision 0.0.0. */
  memset(&noderev, 0, sizeof(noderev));
  noderev.kind = svn_node_dir;
  noderev.created_path = "/";
  SVN_ERR(svn_fs_bdb__put_node_revision(fs, root_id, &noderev, 
                                        trail, trail->pool));

  /* Create a new transaction (better have an id of "0") */
  SVN_ERR(svn_fs_bdb__create_txn(&txn_id, fs, root_id, trail, trail->pool));
  if (strcmp(txn_id, "0"))
    return svn_error_createf
      (SVN_ERR_FS_CORRUPT, 0,
       _("Corrupt DB: initial transaction id not '0' in filesystem '%s'"),
       fs->path);

  /* Create a default copy (better have an id of "0") */
  SVN_ERR(svn_fs_bdb__reserve_copy_id(&copy_id, fs, trail, trail->pool));
  if (strcmp(copy_id, "0"))
    return svn_error_createf
      (SVN_ERR_FS_CORRUPT, 0,
       _("Corrupt DB: initial copy id not '0' in filesystem '%s'"), fs->path);
  SVN_ERR(svn_fs_bdb__create_copy(fs, copy_id, NULL, NULL, root_id,
                                  copy_kind_real, trail, trail->pool));

  /* Link it into filesystem revision 0. */
  revision.txn_id = txn_id;
  SVN_ERR(svn_fs_bdb__put_rev(&rev, fs, &revision, trail, trail->pool));
  if (rev != 0)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, 0,
                             _("Corrupt DB: initial revision number "
                               "is not '0' in filesystem '%s'"), fs->path);

  /* Promote our transaction to a "committed" transaction. */
  SVN_ERR(svn_fs_base__txn_make_committed(fs, txn_id, rev, 
                                          trail, trail->pool));

  /* Set a date on revision 0. */
  date.data = svn_time_to_cstring(apr_time_now(), trail->pool);
  date.len = strlen(date.data);
  return svn_fs_base__set_rev_prop(fs, 0, SVN_PROP_REVISION_DATE, &date,
                                   trail, trail->pool);
}


svn_error_t *
svn_fs_base__dag_init_fs(svn_fs_t *fs)
{
  return svn_fs_base__retry_txn(fs, txn_body_dag_init_fs, NULL, fs->pool);
}



/*** Directory node functions ***/

/* Some of these are helpers for functions outside this section. */

/* Given directory NODEREV in FS, set *ENTRIES_P to its entries list
   hash, as part of TRAIL, or to NULL if NODEREV has no entries.  The
   entries list will be allocated in POOL, and the entries in
   that list will not have interesting value in their 'kind' fields.
   If NODEREV is not a directory, return the error SVN_ERR_FS_NOT_DIRECTORY. */
static svn_error_t *
get_dir_entries(apr_hash_t **entries_p,
                svn_fs_t *fs,
                node_revision_t *noderev,
                trail_t *trail,
                apr_pool_t *pool)
{
  apr_hash_t *entries = apr_hash_make(pool);
  apr_hash_index_t *hi;
  svn_string_t entries_raw;
  skel_t *entries_skel;

  /* Error if this is not a directory. */
  if (noderev->kind != svn_node_dir)
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, NULL,
       _("Attempted to create entry in non-directory parent"));

  /* If there's a DATA-KEY, there might be entries to fetch. */
  if (noderev->data_key)
    {
      /* Now we have a rep, follow through to get the entries. */
      SVN_ERR(svn_fs_base__rep_contents(&entries_raw, fs, noderev->data_key, 
                                        trail, pool));
      entries_skel = svn_fs_base__parse_skel(entries_raw.data,
                                             entries_raw.len, pool);

      /* Were there entries?  Make a hash from them. */
      if (entries_skel)
        SVN_ERR(svn_fs_base__parse_entries_skel(&entries, entries_skel,
                                                pool));
    }

  /* No hash?  No problem.  */
  *entries_p = NULL;
  if (! entries)
    return SVN_NO_ERROR;

  /* Else, convert the hash from a name->id mapping to a name->dirent one.  */
  *entries_p = apr_hash_make(pool);
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_fs_dirent_t *dirent = apr_palloc(pool, sizeof(*dirent));

      /* KEY will be the entry name in ancestor, VAL the id.  */
      apr_hash_this(hi, &key, &klen, &val);
      dirent->name = key;
      dirent->id = val;
      dirent->kind = svn_node_unknown;
      apr_hash_set(*entries_p, key, klen, dirent);
    }

  /* Return our findings. */
  return SVN_NO_ERROR;
}


/* Set *ID_P to the node-id for entry NAME in PARENT, as part of
   TRAIL.  If no such entry, set *ID_P to NULL but do not error.  The
   entry is allocated in POOL or in the same pool as PARENT;
   the caller should copy if it cares.  */
static svn_error_t *
dir_entry_id_from_node(const svn_fs_id_t **id_p,
                       dag_node_t *parent,
                       const char *name,
                       trail_t *trail,
                       apr_pool_t *pool)
{
  apr_hash_t *entries;
  svn_fs_dirent_t *dirent;

  SVN_ERR(svn_fs_base__dag_dir_entries(&entries, parent, trail, pool));
  if (entries)
    dirent = apr_hash_get(entries, name, APR_HASH_KEY_STRING);
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
set_entry(dag_node_t *parent,
          const char *name,
          const svn_fs_id_t *id,
          const char *txn_id,
          trail_t *trail,
          apr_pool_t *pool)
{
  node_revision_t *parent_noderev;
  const char *rep_key, *mutable_rep_key;
  apr_hash_t *entries = NULL;
  svn_stream_t *wstream;
  apr_size_t len;
  svn_string_t raw_entries;
  svn_stringbuf_t *raw_entries_buf;
  skel_t *entries_skel;
  svn_fs_t *fs = svn_fs_base__dag_get_fs(parent);

  /* Get the parent's node-revision. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&parent_noderev, fs, parent->id, 
                                        trail, pool));
  rep_key = parent_noderev->data_key;
  SVN_ERR(svn_fs_base__get_mutable_rep(&mutable_rep_key, rep_key,
                                       fs, txn_id, trail, pool));

  /* If the parent node already pointed at a mutable representation,
     we don't need to do anything.  But if it didn't, either because
     the parent didn't refer to any rep yet or because it referred to
     an immutable one, we must make the parent refer to the mutable
     rep we just created. */
  if (! svn_fs_base__same_keys(rep_key, mutable_rep_key))
    {
      node_revision_t *new_noderev = copy_node_revision(parent_noderev, pool);
      new_noderev->data_key = mutable_rep_key;
      SVN_ERR(svn_fs_bdb__put_node_revision(fs, parent->id, new_noderev, 
                                            trail, pool));
    }

  /* If the new representation inherited nothing, start a new entries
     list for it.  Else, go read its existing entries list. */
  if (rep_key)
    {
      SVN_ERR(svn_fs_base__rep_contents(&raw_entries, fs, rep_key, 
                                        trail, pool));
      entries_skel = svn_fs_base__parse_skel(raw_entries.data,
                                             raw_entries.len, pool);
      if (entries_skel)
        SVN_ERR(svn_fs_base__parse_entries_skel(&entries, entries_skel,
                                                pool));
    }

  /* If we still have no ENTRIES hash, make one here.  */
  if (! entries)
    entries = apr_hash_make(pool);

  /* Now, add our new entry to the entries list. */
  apr_hash_set(entries, name, APR_HASH_KEY_STRING, id);

  /* Finally, replace the old entries list with the new one. */
  SVN_ERR(svn_fs_base__unparse_entries_skel(&entries_skel, entries,
                                            pool));
  raw_entries_buf = svn_fs_base__unparse_skel(entries_skel, pool);
  SVN_ERR(svn_fs_base__rep_contents_write_stream(&wstream, fs,
                                                 mutable_rep_key, txn_id,
                                                 TRUE, trail, pool));
  len = raw_entries_buf->len;
  SVN_ERR(svn_stream_write(wstream, raw_entries_buf->data, &len));
  SVN_ERR(svn_stream_close(wstream));
  return SVN_NO_ERROR;
}


/* Make a new entry named NAME in PARENT, as part of TRAIL.  If IS_DIR
   is true, then the node revision the new entry points to will be a
   directory, else it will be a file.  The new node will be allocated
   in POOL.  PARENT must be mutable, and must not have an entry
   named NAME.  */
static svn_error_t *
make_entry(dag_node_t **child_p,
           dag_node_t *parent,
           const char *parent_path,
           const char *name,
           svn_boolean_t is_dir,
           const char *txn_id,
           trail_t *trail,
           apr_pool_t *pool)
{
  const svn_fs_id_t *new_node_id;
  node_revision_t new_noderev;

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
  if (! svn_fs_base__dag_check_mutable(parent, txn_id))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to clone child of non-mutable node"));

  /* Check that parent does not already have an entry named NAME. */
  SVN_ERR(dir_entry_id_from_node(&new_node_id, parent, name, trail, pool));
  if (new_node_id)
    return svn_error_createf
      (SVN_ERR_FS_ALREADY_EXISTS, NULL,
       _("Attempted to create entry that already exists"));

  /* Create the new node's NODE-REVISION */
  memset(&new_noderev, 0, sizeof(new_noderev));
  new_noderev.kind = is_dir ? svn_node_dir : svn_node_file;
  new_noderev.created_path = svn_path_join(parent_path, name, pool);
  SVN_ERR(svn_fs_base__create_node
          (&new_node_id, svn_fs_base__dag_get_fs(parent), &new_noderev,
           svn_fs_base__id_copy_id(svn_fs_base__dag_get_id(parent)),
           txn_id, trail, pool));

  /* Create a new dag_node_t for our new node */
  SVN_ERR(svn_fs_base__dag_get_node(child_p,
                                    svn_fs_base__dag_get_fs(parent),
                                    new_node_id, trail, pool));

  /* We can safely call set_entry because we already know that
     PARENT is mutable, and we just created CHILD, so we know it has
     no ancestors (therefore, PARENT cannot be an ancestor of CHILD) */
  SVN_ERR(set_entry(parent, name, svn_fs_base__dag_get_id(*child_p),
                    txn_id, trail, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_dir_entries(apr_hash_t **entries,
                             dag_node_t *node,
                             trail_t *trail,
                             apr_pool_t *pool)
{
  node_revision_t *noderev;
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, node->fs, node->id, 
                                        trail, pool));
  return get_dir_entries(entries, node->fs, noderev, trail, pool);
}


svn_error_t *
svn_fs_base__dag_set_entry(dag_node_t *node,
                           const char *entry_name,
                           const svn_fs_id_t *id,
                           const char *txn_id,
                           trail_t *trail,
                           apr_pool_t *pool)
{
  /* Check it's a directory. */
  if (node->kind != svn_node_dir)
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, NULL,
       _("Attempted to set entry in non-directory node"));

  /* Check it's mutable. */
  if (! svn_fs_base__dag_check_mutable(node, txn_id))
    return svn_error_create
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to set entry in immutable node"));

  return set_entry(node, entry_name, id, txn_id, trail, pool);
}



/*** Proplists. ***/

svn_error_t *
svn_fs_base__dag_get_proplist(apr_hash_t **proplist_p,
                              dag_node_t *node,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  node_revision_t *noderev;
  apr_hash_t *proplist = NULL;
  svn_string_t raw_proplist;
  skel_t *proplist_skel;

  /* Go get a fresh NODE-REVISION for this node. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, node->fs, node->id, 
                                        trail, pool));

  /* Get property key (returning early if there isn't one) . */
  if (! noderev->prop_key)
    {
      *proplist_p = NULL;
      return SVN_NO_ERROR;
    }

  /* Get the string associated with the property rep, parsing it as a
     skel, and then attempt to parse *that* into a property hash.  */
  SVN_ERR(svn_fs_base__rep_contents(&raw_proplist,
                                    svn_fs_base__dag_get_fs(node),
                                    noderev->prop_key, trail, pool));
  proplist_skel = svn_fs_base__parse_skel(raw_proplist.data, 
                                          raw_proplist.len, pool);
  if (proplist_skel)
    SVN_ERR(svn_fs_base__parse_proplist_skel(&proplist, 
                                             proplist_skel, pool));

  *proplist_p = proplist;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_set_proplist(dag_node_t *node,
                              apr_hash_t *proplist,
                              const char *txn_id,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  node_revision_t *noderev;
  const char *rep_key, *mutable_rep_key;
  svn_fs_t *fs = svn_fs_base__dag_get_fs(node);

  /* Sanity check: this node better be mutable! */
  if (! svn_fs_base__dag_check_mutable(node, txn_id))
    {
      svn_string_t *idstr = svn_fs_base__id_unparse(node->id, pool);
      return svn_error_createf
        (SVN_ERR_FS_NOT_MUTABLE, NULL,
         _("Can't set proplist on *immutable* node-revision %s"), idstr->data);
    }

  /* Go get a fresh NODE-REVISION for this node. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, node->id, 
                                        trail, pool));
  rep_key = noderev->prop_key;

  /* Get a mutable version of this rep (updating the node revision if
     this isn't a NOOP)  */
  SVN_ERR(svn_fs_base__get_mutable_rep(&mutable_rep_key, rep_key,
                                       fs, txn_id, trail, pool));
  if (! svn_fs_base__same_keys(mutable_rep_key, rep_key))
    {
      noderev->prop_key = mutable_rep_key;
      SVN_ERR(svn_fs_bdb__put_node_revision(fs, node->id, noderev, 
                                            trail, pool));
    }

  /* Replace the old property list with the new one. */
  {
    svn_stream_t *wstream;
    apr_size_t len;
    skel_t *proplist_skel;
    svn_stringbuf_t *raw_proplist_buf;

    SVN_ERR(svn_fs_base__unparse_proplist_skel(&proplist_skel, 
                                               proplist, pool));
    raw_proplist_buf = svn_fs_base__unparse_skel(proplist_skel, pool);
    SVN_ERR(svn_fs_base__rep_contents_write_stream(&wstream, fs,
                                                   mutable_rep_key, txn_id,
                                                   TRUE, trail, pool));
    len = raw_proplist_buf->len;
    SVN_ERR(svn_stream_write(wstream, raw_proplist_buf->data, &len));
    SVN_ERR(svn_stream_close(wstream));
  }

  return SVN_NO_ERROR;
}



/*** Roots. ***/

svn_error_t *
svn_fs_base__dag_revision_root(dag_node_t **node_p,
                               svn_fs_t *fs,
                               svn_revnum_t rev,
                               trail_t *trail,
                               apr_pool_t *pool)
{
  const svn_fs_id_t *root_id;

  SVN_ERR(svn_fs_base__rev_get_root(&root_id, fs, rev, trail, pool));
  return svn_fs_base__dag_get_node(node_p, fs, root_id, trail, pool);
}


svn_error_t *
svn_fs_base__dag_txn_root(dag_node_t **node_p,
                          svn_fs_t *fs,
                          const char *txn_id,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  const svn_fs_id_t *root_id, *ignored;

  SVN_ERR(svn_fs_base__get_txn_ids(&root_id, &ignored, fs, txn_id, 
                                   trail, pool));
  return svn_fs_base__dag_get_node(node_p, fs, root_id, trail, pool);
}


svn_error_t *
svn_fs_base__dag_txn_base_root(dag_node_t **node_p,
                               svn_fs_t *fs,
                               const char *txn_id,
                               trail_t *trail,
                               apr_pool_t *pool)
{
  const svn_fs_id_t *base_root_id, *ignored;

  SVN_ERR(svn_fs_base__get_txn_ids(&ignored, &base_root_id, fs, txn_id,
                                   trail, pool));
  return svn_fs_base__dag_get_node(node_p, fs, base_root_id, trail, pool);
}


svn_error_t *
svn_fs_base__dag_clone_child(dag_node_t **child_p,
                             dag_node_t *parent,
                             const char *parent_path,
                             const char *name,
                             const char *copy_id,
                             const char *txn_id,
                             trail_t *trail,
                             apr_pool_t *pool)
{
  dag_node_t *cur_entry; /* parent's current entry named NAME */
  const svn_fs_id_t *new_node_id; /* node id we'll put into NEW_NODE */
  svn_fs_t *fs = svn_fs_base__dag_get_fs(parent);

  /* First check that the parent is mutable. */
  if (! svn_fs_base__dag_check_mutable(parent, txn_id))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to clone child of non-mutable node"));

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       _("Attempted to make a child clone with an illegal name '%s'"), name);

  /* Find the node named NAME in PARENT's entries list if it exists. */
  SVN_ERR(svn_fs_base__dag_open(&cur_entry, parent, name, trail, pool));

  /* Check for mutability in the node we found.  If it's mutable, we
     don't need to clone it. */
  if (svn_fs_base__dag_check_mutable(cur_entry, txn_id))
    {
      /* This has already been cloned */
      new_node_id = cur_entry->id;
    }
  else
    {
      node_revision_t *noderev;

      /* Go get a fresh NODE-REVISION for current child node. */
      SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, cur_entry->id, 
                                            trail, pool));

      /* Do the clone thingy here. */
      noderev->predecessor_id = cur_entry->id;
      if (noderev->predecessor_count != -1)
        noderev->predecessor_count++;
      noderev->created_path = svn_path_join(parent_path, name, pool);
      SVN_ERR(svn_fs_base__create_successor(&new_node_id, fs, cur_entry->id,
                                            noderev, copy_id, txn_id,
                                            trail, pool));

      /* Replace the ID in the parent's ENTRY list with the ID which
         refers to the mutable clone of this child. */
      SVN_ERR(set_entry(parent, name, new_node_id, txn_id, trail, pool));
    }

  /* Initialize the youngster. */
  return svn_fs_base__dag_get_node(child_p, fs, new_node_id, trail, pool);
}



svn_error_t *
svn_fs_base__dag_clone_root(dag_node_t **root_p,
                            svn_fs_t *fs,
                            const char *txn_id,
                            trail_t *trail,
                            apr_pool_t *pool)
{
  const svn_fs_id_t *base_root_id, *root_id;
  node_revision_t *noderev;

  /* Get the node ID's of the root directories of the transaction and
     its base revision.  */
  SVN_ERR(svn_fs_base__get_txn_ids(&root_id, &base_root_id, fs, txn_id,
                                   trail, pool));

  /* Oh, give me a clone...
     (If they're the same, we haven't cloned the transaction's root
     directory yet.)  */
  if (svn_fs_base__id_eq(root_id, base_root_id))
    {
      const char *base_copy_id = svn_fs_base__id_copy_id(base_root_id);

      /* Of my own flesh and bone...
         (Get the NODE-REVISION for the base node, and then write
         it back out as the clone.) */
      SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, base_root_id,
                                            trail, pool));

      /* Store it. */
      /* ### todo: Does it even makes sense to have a different copy id for
         the root node?  That is, does this function need a copy_id
         passed in?  */
      noderev->predecessor_id = svn_fs_base__id_copy(base_root_id, pool);
      if (noderev->predecessor_count != -1)
        noderev->predecessor_count++;
      SVN_ERR(svn_fs_base__create_successor(&root_id, fs, base_root_id,
                                            noderev, base_copy_id,
                                            txn_id, trail, pool));

      /* ... And when it is grown
       *      Then my own little clone
       *        Will be of the opposite sex!
       */
      SVN_ERR(svn_fs_base__set_txn_root(fs, txn_id, root_id, trail, pool));
    }

  /* One way or another, root_id now identifies a cloned root node. */
  SVN_ERR(svn_fs_base__dag_get_node(root_p, fs, root_id, trail, pool));

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
svn_error_t *
svn_fs_base__dag_delete(dag_node_t *parent,
                        const char *name,
                        const char *txn_id,
                        trail_t *trail,
                        apr_pool_t *pool)
{
  node_revision_t *parent_noderev;
  const char *rep_key, *mutable_rep_key;
  apr_hash_t *entries = NULL;
  skel_t *entries_skel;
  svn_fs_t *fs = parent->fs;
  svn_string_t str;
  svn_fs_id_t *id = NULL;
  dag_node_t *node;

  /* Make sure parent is a directory. */
  if (parent->kind != svn_node_dir)
    return svn_error_createf
      (SVN_ERR_FS_NOT_DIRECTORY, NULL,
       _("Attempted to delete entry '%s' from *non*-directory node"), name);

  /* Make sure parent is mutable. */
  if (! svn_fs_base__dag_check_mutable(parent, txn_id))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to delete entry '%s' from immutable directory node"),
       name);

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       _("Attempted to delete a node with an illegal name '%s'"), name);

  /* Get a fresh NODE-REVISION for the parent node. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&parent_noderev, fs, parent->id, 
                                        trail, pool));

  /* Get the key for the parent's entries list (data) representation. */
  rep_key = parent_noderev->data_key;

  /* No REP_KEY means no representation, and no representation means
     no data, and no data means no entries...there's nothing here to
     delete! */
  if (! rep_key)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_ENTRY, NULL,
       _("Delete failed: directory has no entry '%s'"), name);

  /* Ensure we have a key to a mutable representation of the entries
     list.  We'll have to update the NODE-REVISION if it points to an
     immutable version.  */
  SVN_ERR(svn_fs_base__get_mutable_rep(&mutable_rep_key, rep_key,
                                       fs, txn_id, trail, pool));
  if (! svn_fs_base__same_keys(mutable_rep_key, rep_key))
    {
      parent_noderev->data_key = mutable_rep_key;
      SVN_ERR(svn_fs_bdb__put_node_revision(fs, parent->id, parent_noderev, 
                                            trail, pool));
    }

  /* Read the representation, then use it to get the string that holds
     the entries list.  Parse that list into a skel, and parse *that*
     into a hash. */

  SVN_ERR(svn_fs_base__rep_contents(&str, fs, rep_key, trail, pool));
  entries_skel = svn_fs_base__parse_skel(str.data, str.len, pool);
  if (entries_skel)
    SVN_ERR(svn_fs_base__parse_entries_skel(&entries, entries_skel, pool));

  /* Find NAME in the ENTRIES skel.  */
  if (entries)
    id = apr_hash_get(entries, name, APR_HASH_KEY_STRING);

  /* If we never found ID in ENTRIES (perhaps because there are no
     ENTRIES, perhaps because ID just isn't in the existing ENTRIES
     ... it doesn't matter), return an error.  */
  if (! id)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_ENTRY, NULL,
       _("Delete failed: directory has no entry '%s'"), name);

  /* Use the ID of this ENTRY to get the entry's node.  */
  SVN_ERR(svn_fs_base__dag_get_node(&node, svn_fs_base__dag_get_fs(parent),
                                    id, trail, pool));

  /* If mutable, remove it and any mutable children from db. */
  SVN_ERR(svn_fs_base__dag_delete_if_mutable(parent->fs, id, txn_id, 
                                             trail, pool));

  /* Remove this entry from its parent's entries list. */
  apr_hash_set(entries, name, APR_HASH_KEY_STRING, NULL);

  /* Replace the old entries list with the new one. */
  {
    svn_stream_t *ws;
    svn_stringbuf_t *unparsed_entries;
    apr_size_t len;

    SVN_ERR(svn_fs_base__unparse_entries_skel(&entries_skel, entries, pool));
    unparsed_entries = svn_fs_base__unparse_skel(entries_skel, pool);
    SVN_ERR(svn_fs_base__rep_contents_write_stream(&ws, fs, mutable_rep_key,
                                                   txn_id, TRUE, trail,
                                                   pool));
    len = unparsed_entries->len;
    SVN_ERR(svn_stream_write(ws, unparsed_entries->data, &len));
    SVN_ERR(svn_stream_close(ws));
  }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_remove_node(svn_fs_t *fs,
                             const svn_fs_id_t *id,
                             const char *txn_id,
                             trail_t *trail,
                             apr_pool_t *pool)
{
  dag_node_t *node;
  node_revision_t *noderev;

  /* Fetch the node. */
  SVN_ERR(svn_fs_base__dag_get_node(&node, fs, id, trail, pool));

  /* If immutable, do nothing and return immediately. */
  if (! svn_fs_base__dag_check_mutable(node, txn_id))
    return svn_error_createf(SVN_ERR_FS_NOT_MUTABLE, NULL,
                             _("Attempted removal of immutable node"));

  /* Get a fresh node-revision. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, id, trail, pool));

  /* Delete any mutable property representation. */
  if (noderev->prop_key)
    SVN_ERR(svn_fs_base__delete_rep_if_mutable(fs, noderev->prop_key,
                                               txn_id, trail, pool));

  /* Delete any mutable data representation. */
  if (noderev->data_key)
    SVN_ERR(svn_fs_base__delete_rep_if_mutable(fs, noderev->data_key,
                                               txn_id, trail, pool));

  /* Delete any mutable edit representation (files only). */
  if (noderev->edit_key)
    SVN_ERR(svn_fs_base__delete_rep_if_mutable(fs, noderev->edit_key,
                                               txn_id, trail, pool));

  /* Delete the node revision itself. */
  SVN_ERR(svn_fs_base__delete_node_revision(fs, id, trail, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_delete_if_mutable(svn_fs_t *fs,
                                   const svn_fs_id_t *id,
                                   const char *txn_id,
                                   trail_t *trail,
                                   apr_pool_t *pool)
{
  dag_node_t *node;

  /* Get the node. */
  SVN_ERR(svn_fs_base__dag_get_node(&node, fs, id, trail, pool));

  /* If immutable, do nothing and return immediately. */
  if (! svn_fs_base__dag_check_mutable(node, txn_id))
    return SVN_NO_ERROR;

  /* Else it's mutable.  Recurse on directories... */
  if (node->kind == svn_node_dir)
    {
      apr_hash_t *entries;
      apr_hash_index_t *hi;

      /* Loop over hash entries */
      SVN_ERR(svn_fs_base__dag_dir_entries(&entries, node, trail, pool));
      if (entries)
        {
          apr_pool_t *subpool = svn_pool_create(pool);
          for (hi = apr_hash_first(pool, entries);
               hi;
               hi = apr_hash_next(hi))
            {
              void *val;
              svn_fs_dirent_t *dirent;

              apr_hash_this(hi, NULL, NULL, &val);
              dirent = val;
              SVN_ERR(svn_fs_base__dag_delete_if_mutable(fs, dirent->id,
                                                         txn_id, trail, 
                                                         subpool));
            }
        }
    }

  /* ... then delete the node itself, after deleting any mutable
     representations and strings it points to. */
  SVN_ERR(svn_fs_base__dag_remove_node(fs, id, txn_id, trail, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_make_file(dag_node_t **child_p,
                           dag_node_t *parent,
                           const char *parent_path,
                           const char *name,
                           const char *txn_id,
                           trail_t *trail,
                           apr_pool_t *pool)
{
  /* Call our little helper function */
  return make_entry(child_p, parent, parent_path, name, FALSE, 
                    txn_id, trail, pool);
}


svn_error_t *
svn_fs_base__dag_make_dir(dag_node_t **child_p,
                          dag_node_t *parent,
                          const char *parent_path,
                          const char *name,
                          const char *txn_id,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  /* Call our little helper function */
  return make_entry(child_p, parent, parent_path, name, TRUE, 
                    txn_id, trail, pool);
}


svn_error_t *
svn_fs_base__dag_get_contents(svn_stream_t **contents,
                              dag_node_t *file,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  node_revision_t *noderev;

  /* Make sure our node is a file. */
  if (file->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       _("Attempted to get textual contents of a *non*-file node"));

  /* Go get a fresh node-revision for FILE. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, file->fs, file->id, 
                                        trail, pool));

  /* Our job is to _return_ a stream on the file's contents, so the
     stream has to be trail-independent.  Here, we pass NULL to tell
     the stream that we're not providing it a trail that lives across
     reads.  This means the stream will do each read in a one-off,
     temporary trail.  */
  SVN_ERR(svn_fs_base__rep_contents_read_stream(contents, file->fs,
                                                noderev->data_key,
                                                FALSE, trail, pool));

  /* Note that we're not registering any `close' func, because there's
     nothing to cleanup outside of our trail.  When the trail is
     freed, the stream/baton will be too. */

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_file_length(svn_filesize_t *length,
                             dag_node_t *file,
                             trail_t *trail,
                             apr_pool_t *pool)
{
  node_revision_t *noderev;

  /* Make sure our node is a file. */
  if (file->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       _("Attempted to get length of a *non*-file node"));

  /* Go get a fresh node-revision for FILE, and . */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, file->fs, file->id, 
                                        trail, pool));
  if (noderev->data_key)
    SVN_ERR(svn_fs_base__rep_contents_size(length, file->fs, 
                                           noderev->data_key, trail, pool));
  else
    *length = 0;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_file_checksum(unsigned char digest[],
                               dag_node_t *file,
                               trail_t *trail,
                               apr_pool_t *pool)
{
  node_revision_t *noderev;

  if (file->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       _("Attempted to get checksum of a *non*-file node"));

  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, file->fs, file->id, 
                                        trail, pool));
  if (noderev->data_key)
    SVN_ERR(svn_fs_base__rep_contents_checksum(digest, file->fs,
                                               noderev->data_key, 
                                               trail, pool));
  else
    memset(digest, 0, APR_MD5_DIGESTSIZE);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__dag_get_edit_stream(svn_stream_t **contents,
                                 dag_node_t *file,
                                 const char *txn_id,
                                 trail_t *trail,
                                 apr_pool_t *pool)
{
  svn_fs_t *fs = file->fs;   /* just for nicer indentation */
  node_revision_t *noderev;
  const char *mutable_rep_key;
  svn_stream_t *ws;

  /* Make sure our node is a file. */
  if (file->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       _("Attempted to set textual contents of a *non*-file node"));

  /* Make sure our node is mutable. */
  if (! svn_fs_base__dag_check_mutable(file, txn_id))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to set textual contents of an immutable node"));

  /* Get the node revision. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, file->id, 
                                        trail, pool));

  /* If this node already has an EDIT-DATA-KEY, destroy the data
     associated with that key.  */
  if (noderev->edit_key)
    SVN_ERR(svn_fs_base__delete_rep_if_mutable(fs, noderev->edit_key,
                                               txn_id, trail, pool));

  /* Now, let's ensure that we have a new EDIT-DATA-KEY available for
     use. */
  SVN_ERR(svn_fs_base__get_mutable_rep(&mutable_rep_key, NULL, fs,
                                       txn_id, trail, pool));

  /* We made a new rep, so update the node revision. */
  noderev->edit_key = mutable_rep_key;
  SVN_ERR(svn_fs_bdb__put_node_revision(fs, file->id, noderev, 
                                        trail, pool));

  /* Return a writable stream with which to set new contents. */
  SVN_ERR(svn_fs_base__rep_contents_write_stream(&ws, fs, mutable_rep_key,
                                                 txn_id, FALSE, trail,
                                                 pool));
  *contents = ws;

  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_base__dag_finalize_edits(dag_node_t *file,
                                const char *checksum,
                                const char *txn_id,
                                trail_t *trail,
                                apr_pool_t *pool)
{
  svn_fs_t *fs = file->fs;   /* just for nicer indentation */
  node_revision_t *noderev;
  const char *old_data_key;

  /* Make sure our node is a file. */
  if (file->kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL,
       _("Attempted to set textual contents of a *non*-file node"));

  /* Make sure our node is mutable. */
  if (! svn_fs_base__dag_check_mutable(file, txn_id))
    return svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, NULL,
       _("Attempted to set textual contents of an immutable node"));

  /* Get the node revision. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, file->id, 
                                        trail, pool));

  /* If this node has no EDIT-DATA-KEY, this is a no-op. */
  if (! noderev->edit_key)
    return SVN_NO_ERROR;

  if (checksum)
    {
      unsigned char digest[APR_MD5_DIGESTSIZE];
      const char *hex;

      SVN_ERR(svn_fs_base__rep_contents_checksum
              (digest, fs, noderev->edit_key, trail, pool));

      hex = svn_md5_digest_to_cstring_display(digest, pool);
      if (strcmp(checksum, hex) != 0)
        return svn_error_createf
          (SVN_ERR_CHECKSUM_MISMATCH,
           NULL,
           _("Checksum mismatch, rep '%s':\n"
             "   expected:  %s\n"
             "     actual:  %s\n"),
           noderev->edit_key, checksum, hex);
    }

  /* Now, we want to delete the old representation and replace it with
     the new.  Of course, we don't actually delete anything until
     everything is being properly referred to by the node-revision
     skel. */
  old_data_key = noderev->data_key;
  noderev->data_key = noderev->edit_key;
  noderev->edit_key = NULL;
  SVN_ERR(svn_fs_bdb__put_node_revision(fs, file->id, noderev, trail, pool));

  /* Only *now* can we safely destroy the old representation (if it
     even existed in the first place). */
  if (old_data_key)
    SVN_ERR(svn_fs_base__delete_rep_if_mutable(fs, old_data_key, txn_id,
                                               trail, pool));

  return SVN_NO_ERROR;
}



dag_node_t *
svn_fs_base__dag_dup(dag_node_t *node,
                     apr_pool_t *pool)
{
  /* Allocate our new node. */
  dag_node_t *new_node = apr_pcalloc(pool, sizeof(*new_node));

  new_node->fs = node->fs;
  new_node->id = svn_fs_base__id_copy(node->id, pool);
  new_node->kind = node->kind;
  new_node->created_path = apr_pstrdup(pool, node->created_path);
  return new_node;
}


svn_error_t *
svn_fs_base__dag_open(dag_node_t **child_p,
                      dag_node_t *parent,
                      const char *name,
                      trail_t *trail,
                      apr_pool_t *pool)
{
  const svn_fs_id_t *node_id;

  /* Ensure that NAME exists in PARENT's entry list. */
  SVN_ERR(dir_entry_id_from_node(&node_id, parent, name, trail, pool));
  if (! node_id)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FOUND, NULL,
       _("Attempted to open non-existent child node '%s'"), name);

  /* Make sure that NAME is a single path component. */
  if (! svn_path_is_single_path_component(name))
    return svn_error_createf
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, NULL,
       _("Attempted to open node with an illegal name '%s'"), name);

  /* Now get the node that was requested. */
  return svn_fs_base__dag_get_node(child_p, svn_fs_base__dag_get_fs(parent),
                                   node_id, trail, pool);
}


svn_error_t *
svn_fs_base__dag_copy(dag_node_t *to_node,
                      const char *entry,
                      dag_node_t *from_node,
                      svn_boolean_t preserve_history,
                      svn_revnum_t from_rev,
                      const char *from_path,
                      const char *txn_id,
                      trail_t *trail,
                      apr_pool_t *pool)
{
  const svn_fs_id_t *id;

  if (preserve_history)
    {
      node_revision_t *noderev;
      const char *copy_id;
      svn_fs_t *fs = svn_fs_base__dag_get_fs(from_node);
      const svn_fs_id_t *src_id = svn_fs_base__dag_get_id(from_node);
      const char *from_txn_id = NULL;

      /* Make a copy of the original node revision. */
      SVN_ERR(svn_fs_bdb__get_node_revision(&noderev, fs, from_node->id, 
                                            trail, pool));

      /* Reserve a copy ID for this new copy. */
      SVN_ERR(svn_fs_bdb__reserve_copy_id(&copy_id, fs, trail, pool));

      /* Create a successor with its predecessor pointing at the copy
         source. */
      noderev->predecessor_id = svn_fs_base__id_copy(src_id, pool);
      if (noderev->predecessor_count != -1)
        noderev->predecessor_count++;
      noderev->created_path = svn_path_join 
        (svn_fs_base__dag_get_created_path(to_node), entry, pool);
      SVN_ERR(svn_fs_base__create_successor(&id, fs, src_id, noderev,
                                            copy_id, txn_id, trail, pool));

      /* Translate FROM_REV into a transaction ID. */
      SVN_ERR(svn_fs_base__rev_get_txn_id(&from_txn_id, fs, from_rev,
                                          trail, pool));

      /* Now that we've done the copy, we need to add the information
         about the copy to the `copies' table, using the COPY_ID we
         reserved above.  */
      SVN_ERR(svn_fs_bdb__create_copy
              (fs, copy_id,
               svn_fs_base__canonicalize_abspath(from_path, pool),
               from_txn_id, id, copy_kind_real, trail, pool));

      /* Finally, add the COPY_ID to the transaction's list of copies
         so that, if this transaction is aborted, the `copies' table
         entry we added above will be cleaned up. */
      SVN_ERR(svn_fs_base__add_txn_copy(fs, txn_id, copy_id, trail, pool));
    }
  else  /* don't preserve history */
    {
      id = svn_fs_base__dag_get_id(from_node);
    }

  /* Set the entry in to_node to the new id. */
  SVN_ERR(svn_fs_base__dag_set_entry(to_node, entry, id, txn_id, 
                                     trail, pool));

  return SVN_NO_ERROR;
}



/*** Deltification ***/

svn_error_t *
svn_fs_base__dag_deltify(dag_node_t *target,
                         dag_node_t *source,
                         svn_boolean_t props_only,
                         trail_t *trail,
                         apr_pool_t *pool)
{
  node_revision_t *source_nr, *target_nr;
  svn_fs_t *fs = svn_fs_base__dag_get_fs(target);

  /* Get node revisions for the two nodes.  */
  SVN_ERR(svn_fs_bdb__get_node_revision(&target_nr, fs, target->id, 
                                        trail, pool));
  SVN_ERR(svn_fs_bdb__get_node_revision(&source_nr, fs, source->id, 
                                        trail, pool));

  /* If TARGET and SOURCE both have properties, and are not sharing a
     property key, deltify TARGET's properties.  */
  if (target_nr->prop_key
      && source_nr->prop_key
      && (strcmp(target_nr->prop_key, source_nr->prop_key)))
    SVN_ERR(svn_fs_base__rep_deltify(fs, target_nr->prop_key,
                                     source_nr->prop_key, trail, pool));

  /* If we are not only attending to properties, and if TARGET and
     SOURCE both have data, and are not sharing a data key, deltify
     TARGET's data.  */
  if ((! props_only)
      && target_nr->data_key
      && source_nr->data_key
      && (strcmp(target_nr->data_key, source_nr->data_key)))
    SVN_ERR(svn_fs_base__rep_deltify(fs, target_nr->data_key,
                                     source_nr->data_key, trail, pool));

  return SVN_NO_ERROR;
}




/*** Committing ***/

svn_error_t *
svn_fs_base__dag_commit_txn(svn_revnum_t *new_rev,
                            svn_fs_t *fs,
                            const char *txn_id,
                            trail_t *trail,
                            apr_pool_t *pool)
{
  revision_t revision;
  svn_string_t date;
  apr_hash_t *txnprops;

  /* Remove any temporary transaction properties initially created by
     begin_txn().  */
  SVN_ERR(svn_fs_base__txn_proplist_in_trail(&txnprops, txn_id, trail));
  if (txnprops)
    {
      if (apr_hash_get(txnprops, SVN_FS_PROP_TXN_CHECK_OOD,
                       APR_HASH_KEY_STRING))
        SVN_ERR(svn_fs_base__set_txn_prop 
                (fs, txn_id, SVN_FS_PROP_TXN_CHECK_OOD, NULL, trail, pool));

      if (apr_hash_get(txnprops, SVN_FS_PROP_TXN_CHECK_LOCKS,
                       APR_HASH_KEY_STRING))
        SVN_ERR(svn_fs_base__set_txn_prop 
                (fs, txn_id, SVN_FS_PROP_TXN_CHECK_LOCKS, NULL, trail, pool));
    }

  /* Add new revision entry to `revisions' table. */
  revision.txn_id = txn_id;
  *new_rev = SVN_INVALID_REVNUM;
  SVN_ERR(svn_fs_bdb__put_rev(new_rev, fs, &revision, trail, pool));

  /* Promote the unfinished transaction to a committed one. */
  SVN_ERR(svn_fs_base__txn_make_committed(fs, txn_id, *new_rev, 
                                          trail, pool));

  /* Set a date on the commit.  We wait until now to fetch the date,
     so it's definitely newer than any previous revision's date. */
  date.data = svn_time_to_cstring(apr_time_now(), pool);
  date.len = strlen(date.data);
  SVN_ERR(svn_fs_base__set_rev_prop(fs, *new_rev, SVN_PROP_REVISION_DATE,
                                    &date, trail, pool));

  return SVN_NO_ERROR;
}



/*** Comparison. ***/

svn_error_t *
svn_fs_base__things_different(svn_boolean_t *props_changed,
                              svn_boolean_t *contents_changed,
                              dag_node_t *node1,
                              dag_node_t *node2,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  node_revision_t *noderev1, *noderev2;

  /* If we have no place to store our results, don't bother doing
     anything. */
  if (! props_changed && ! contents_changed)
    return SVN_NO_ERROR;

  /* The the node revision skels for these two nodes. */
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev1, node1->fs, node1->id, 
                                        trail, pool));
  SVN_ERR(svn_fs_bdb__get_node_revision(&noderev2, node2->fs, node2->id, 
                                        trail, pool));

  /* Compare property keys. */
  if (props_changed != NULL)
    *props_changed = (! svn_fs_base__same_keys(noderev1->prop_key,
                                               noderev2->prop_key));

  /* Compare contents keys. */
  if (contents_changed != NULL)
    *contents_changed = (! svn_fs_base__same_keys(noderev1->data_key,
                                                  noderev2->data_key));

  return SVN_NO_ERROR;
}
