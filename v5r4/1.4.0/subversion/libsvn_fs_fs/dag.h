/* dag.h : DAG-like interface filesystem, private to libsvn_fs
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

#ifndef SVN_LIBSVN_FS_DAG_H
#define SVN_LIBSVN_FS_DAG_H

#include "svn_fs.h"
#include "svn_delta.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* The interface in this file provides all the essential filesystem
   operations, but exposes the filesystem's DAG structure.  This makes
   it simpler to implement than the public interface, since a client
   of this interface has to understand and cope with shared structure
   directly as it appears in the database.  However, it's still a
   self-consistent set of invariants to maintain, making it
   (hopefully) a useful interface boundary.

   In other words:

   - The dag_node_t interface exposes the internal DAG structure of
     the filesystem, while the svn_fs.h interface does any cloning
     necessary to make the filesystem look like a tree.

   - The dag_node_t interface exposes the existence of copy nodes,
     whereas the svn_fs.h handles them transparently.

   - dag_node_t's must be explicitly cloned, whereas the svn_fs.h
     operations make clones implicitly.

   - Callers of the dag_node_t interface use Berkeley DB transactions
     to ensure consistency between operations, while callers of the
     svn_fs.h interface use Subversion transactions.  */


/* Generic DAG node stuff.  */

typedef struct dag_node_t dag_node_t;


/* Fill *NODE with a dag_node_t representing node revision ID in FS,
   allocating in POOL.  */
svn_error_t *
svn_fs_fs__dag_get_node(dag_node_t **node,
                        svn_fs_t *fs,
                        const svn_fs_id_t *id,
                        apr_pool_t *pool);


/* Return a new dag_node_t object referring to the same node as NODE,
   allocated in POOL.  If you're trying to build a structure in a
   pool that wants to refer to dag nodes that may have been allocated
   elsewhere, you can call this function and avoid inter-pool pointers. */
dag_node_t *svn_fs_fs__dag_dup(dag_node_t *node,
                               apr_pool_t *pool);


/* Return the filesystem containing NODE.  */
svn_fs_t *svn_fs_fs__dag_get_fs(dag_node_t *node);


/* Set *REV to NODE's revision number, allocating in POOL.  If NODE
   has never been committed as part of a revision, set *REV to
   SVN_INVALID_REVNUM.  */
svn_error_t *svn_fs_fs__dag_get_revision(svn_revnum_t *rev,
                                         dag_node_t *node,
                                         apr_pool_t *pool);


/* Return the node revision ID of NODE.  The value returned is shared
   with NODE, and will be deallocated when NODE is.  */
const svn_fs_id_t *svn_fs_fs__dag_get_id(dag_node_t *node);


/* Return the created path of NODE.  The value returned is shared
   with NODE, and will be deallocated when NODE is.  */
const char *svn_fs_fs__dag_get_created_path(dag_node_t *node);


/* Set *ID_P to the node revision ID of NODE's immediate predecessor,
   or NULL if NODE has no predecessor, allocating from POOL. */
svn_error_t *svn_fs_fs__dag_get_predecessor_id(const svn_fs_id_t **id_p,
                                               dag_node_t *node,
                                               apr_pool_t *pool);


/* Set *COUNT to the number of predecessors NODE has (recursively), or
   -1 if not known, allocating from POOL.  */
svn_error_t *svn_fs_fs__dag_get_predecessor_count(int *count,
                                                  dag_node_t *node,
                                                  apr_pool_t *pool);


/* Return non-zero IFF NODE is currently mutable under Subversion
   transaction TXN_ID.  */
svn_boolean_t svn_fs_fs__dag_check_mutable(dag_node_t *node,
                                           const char *txn_id);

/* Return the node kind of NODE. */
svn_node_kind_t svn_fs_fs__dag_node_kind(dag_node_t *node);

/* Set *PROPLIST_P to a PROPLIST hash representing the entire property
   list of NODE, allocating from POOL.  The hash has const char *
   names (the property names) and svn_string_t * values (the property
   values).

   If properties do not exist on NODE, *PROPLIST_P will be set to
   NULL. */
svn_error_t *svn_fs_fs__dag_get_proplist(apr_hash_t **proplist_p,
                                         dag_node_t *node,
                                         apr_pool_t *pool);

/* Set the property list of NODE to PROPLIST, allocating from POOL.
   The node being changed must be mutable.  TXN_ID is the Subversion
   transaction under which this occurs.  */
svn_error_t *svn_fs_fs__dag_set_proplist(dag_node_t *node,
                                         apr_hash_t *proplist,
                                         const char *txn_id,
                                         apr_pool_t *pool);



/* Revision and transaction roots.  */


/* Open the root of revision REV of filesystem FS, allocating from
   POOL.  Set *NODE_P to the new node.  */
svn_error_t *svn_fs_fs__dag_revision_root(dag_node_t **node_p,
                                          svn_fs_t *fs,
                                          svn_revnum_t rev,
                                          apr_pool_t *pool);


/* Set *NODE_P to the root of transaction TXN_ID in FS, allocating
   from POOL.

   Note that the root node of TXN_ID is not necessarily mutable.  If
   no changes have been made in the transaction, then it may share its
   root directory with its base revision.  To get a mutable root node
   for a transaction, call svn_fs_fs__dag_clone_root.  */
svn_error_t *svn_fs_fs__dag_txn_root(dag_node_t **node_p,
                                     svn_fs_t *fs,
                                     const char *txn_id,
                                     apr_pool_t *pool);


/* Set *NODE_P to the base root of transaction TXN_ID in FS,
   allocating from POOL.  Allocate the node in TRAIL->pool.  */
svn_error_t *svn_fs_fs__dag_txn_base_root(dag_node_t **node_p,
                                          svn_fs_t *fs,
                                          const char *txn_id,
                                          apr_pool_t *pool);


/* Clone the root directory of TXN_ID in FS, and update the
   `transactions' table entry to point to it, unless this has been
   done already.  In either case, set *ROOT_P to a reference to the
   root directory clone.  Allocate *ROOT_P in POOL.  */
svn_error_t *svn_fs_fs__dag_clone_root(dag_node_t **root_p,
                                       svn_fs_t *fs,
                                       const char *txn_id,
                                       apr_pool_t *pool);



/* Directories.  */


/* Open the node named NAME in the directory PARENT.  Set *CHILD_P to
   the new node, allocated in POOL.  NAME must be a single path
   component; it cannot be a slash-separated directory path.  */
svn_error_t *svn_fs_fs__dag_open(dag_node_t **child_p,
                                 dag_node_t *parent,
                                 const char *name,
                                 apr_pool_t *pool);


/* Set *ENTRIES_P to a hash table of NODE's entries.  The keys of the
   table are entry names, and the values are svn_fs_dirent_t's.  Use
   POOL for temporary allocations.

   The returned table is *not* allocated in POOL, and becomes invalid
   on the next call to this function or svn_fs_fs__rep_contents_dir.
   If the caller needs the table to live longer, it should copy the
   hash using svn_fs_fs__copy_dir_entries. */
svn_error_t *svn_fs_fs__dag_dir_entries(apr_hash_t **entries_p,
                                        dag_node_t *node,
                                        apr_pool_t *pool);


/* Set ENTRY_NAME in NODE to point to ID (with kind KIND), allocating
   from POOL.  NODE must be a mutable directory.  ID can refer to a
   mutable or immutable node.  If ENTRY_NAME does not exist, it will
   be created.  TXN_ID is the Subversion transaction under which this
   occurs.*/
svn_error_t *svn_fs_fs__dag_set_entry(dag_node_t *node,
                                      const char *entry_name,
                                      const svn_fs_id_t *id,
                                      svn_node_kind_t kind,
                                      const char *txn_id,
                                      apr_pool_t *pool);


/* Make a new mutable clone of the node named NAME in PARENT, and
   adjust PARENT's directory entry to point to it, unless NAME in
   PARENT already refers to a mutable node.  In either case, set
   *CHILD_P to a reference to the new node, allocated in POOL.  PARENT
   must be mutable.  NAME must be a single path component; it cannot
   be a slash-separated directory path.  PARENT_PATH must be the
   canonicalized absolute path of the parent directory.

   COPY_ID, if non-NULL, is a key into the `copies' table, and
   indicates that this new node is being created as the result of a
   copy operation, and specifically which operation that was.

   PATH is the canonicalized absolute path at which this node is being
   created.

   TXN_ID is the Subversion transaction under which this occurs.  */
svn_error_t *svn_fs_fs__dag_clone_child(dag_node_t **child_p,
                                        dag_node_t *parent,
                                        const char *parent_path,
                                        const char *name,
                                        const char *copy_id,
                                        const char *txn_id,
                                        svn_boolean_t is_parent_copyroot,
                                        apr_pool_t *pool);


/* Delete the directory entry named NAME from PARENT, allocating from
   POOL.  PARENT must be mutable.  NAME must be a single path
   component; it cannot be a slash-separated directory path.  If the
   node being deleted is a mutable directory, remove all mutable nodes
   reachable from it.  TXN_ID is the Subversion transaction under
   which this occurs.

   If return SVN_ERR_FS_NO_SUCH_ENTRY, then there is no entry NAME in
   PARENT.  */
svn_error_t *svn_fs_fs__dag_delete(dag_node_t *parent,
                                   const char *name,
                                   const char *txn_id,
                                   apr_pool_t *pool);


/* Delete the node revision assigned to node ID from FS's `nodes'
   table, allocating from POOL.  Also delete any mutable
   representations and strings associated with that node revision.  ID
   may refer to a file or directory, which must be mutable.  TXN_ID is
   the Subversion transaction under which this occurs.

   NOTE: If ID represents a directory, and that directory has mutable
   children, you risk orphaning those children by leaving them
   dangling, disconnected from all DAG trees.  It is assumed that
   callers of this interface know what in the world they are doing.  */
svn_error_t *svn_fs_fs__dag_remove_node(svn_fs_t *fs,
                                        const svn_fs_id_t *id,
                                        const char *txn_id,
                                        apr_pool_t *pool);


/* Delete all mutable node revisions reachable from node ID, including
   ID itself, from FS's `nodes' table, allocating from POOL.  Also
   delete any mutable representations and strings associated with that
   node revision.  ID may refer to a file or directory, which may be
   mutable or immutable.  TXN_ID is the Subversion transaction under
   which this occurs.  */
svn_error_t *svn_fs_fs__dag_delete_if_mutable(svn_fs_t *fs,
                                              const svn_fs_id_t *id,
                                              const char *txn_id,
                                              apr_pool_t *pool);


/* Create a new mutable directory named NAME in PARENT.  Set *CHILD_P
   to a reference to the new node, allocated in POOL.  The new
   directory has no contents, and no properties.  PARENT must be
   mutable.  NAME must be a single path component; it cannot be a
   slash-separated directory path.  PARENT_PATH must be the
   canonicalized absolute path of the parent directory.  PARENT must
   not currently have an entry named NAME.  Do any temporary
   allocation in POOL.  TXN_ID is the Subversion transaction under
   which this occurs.  */
svn_error_t *svn_fs_fs__dag_make_dir(dag_node_t **child_p,
                                     dag_node_t *parent,
                                     const char *parent_path,
                                     const char *name,
                                     const char *txn_id,
                                     apr_pool_t *pool);



/* Files.  */


/* Set *CONTENTS to a readable generic stream which yields the
   contents of FILE.  Allocate the stream in POOL.

   If FILE is not a file, return SVN_ERR_FS_NOT_FILE.  */
svn_error_t *svn_fs_fs__dag_get_contents(svn_stream_t **contents,
                                         dag_node_t *file,
                                         apr_pool_t *pool);


/* Set *STREAM_P to a delta stream that will turn the contents of SOURCE into
   the contents of TARGET, allocated in POOL.  If SOURCE is null, the empty
   string will be used. */
svn_error_t *
svn_fs_fs__dag_get_file_delta_stream(svn_txdelta_stream_t **stream_p,
                                     dag_node_t *source,
                                     dag_node_t *target,
                                     apr_pool_t *pool);

/* Return a generic writable stream in *CONTENTS with which to set the
   contents of FILE.  Allocate the stream in POOL.  TXN_ID is the
   Subversion transaction under which this occurs.

   Any previous edits on the file will be deleted, and a new edit
   stream will be constructed.  */
svn_error_t *svn_fs_fs__dag_get_edit_stream(svn_stream_t **contents,
                                            dag_node_t *file,
                                            const char *txn_id,
                                            apr_pool_t *pool);


/* Signify the completion of edits to FILE made using the stream
   returned by svn_fs_fs__dag_get_edit_stream, allocating from POOL.
   TXN_ID is the Subversion transaction under which this occurs.

   If CHECKSUM is non-null, it must match the checksum for FILE's
   contents (note: this is not recalculated, the recorded checksum is
   used), else the error SVN_ERR_CHECKSUM_MISMATCH is returned.

   This operation is a no-op if no edits are present.  */
svn_error_t *svn_fs_fs__dag_finalize_edits(dag_node_t *file,
                                           const char *checksum,
                                           const char *txn_id,
                                           apr_pool_t *pool);


/* Set *LENGTH to the length of the contents of FILE, allocating from
   POOL. */
svn_error_t *svn_fs_fs__dag_file_length(svn_filesize_t *length,
                                        dag_node_t *file,
                                        apr_pool_t *pool);

/* Put the recorded MD5 checksum of FILE into DIGEST, allocating from
 * POOL.  DIGEST must point to APR_MD5_DIGESTSIZE bytes of storage.
 *
 * If no stored checksum is available, do not calculate the checksum,
 * just put all 0's into DIGEST.
 */
svn_error_t *
svn_fs_fs__dag_file_checksum(unsigned char digest[],
                             dag_node_t *file,
                             apr_pool_t *pool);

/* Create a new mutable file named NAME in PARENT.  Set *CHILD_P to a
   reference to the new node, allocated in POOL.  The new file's
   contents are the empty string, and it has no properties.  PARENT
   must be mutable.  NAME must be a single path component; it cannot
   be a slash-separated directory path.  PARENT_PATH must be the
   canonicalized absolute path of the parent directory.  TXN_ID is the
   Subversion transaction under which this occurs.  */
svn_error_t *svn_fs_fs__dag_make_file(dag_node_t **child_p,
                                      dag_node_t *parent,
                                      const char *parent_path,
                                      const char *name,
                                      const char *txn_id,
                                      apr_pool_t *pool);



/* Copies */

/* Make ENTRY in TO_NODE be a copy of FROM_NODE, allocating from POOL.
   TO_NODE must be mutable.  TXN_ID is the Subversion transaction
   under which this occurs.

   If PRESERVE_HISTORY is true, the new node will record that it was
   copied from FROM_PATH in FROM_REV; therefore, FROM_NODE should be
   the node found at FROM_PATH in FROM_REV, although this is not
   checked.  FROM_PATH should be canonicalized before being passed
   here.

   If PRESERVE_HISTORY is false, FROM_PATH and FROM_REV are ignored.  */
svn_error_t *svn_fs_fs__dag_copy(dag_node_t *to_node,
                                 const char *entry,
                                 dag_node_t *from_node,
                                 svn_boolean_t preserve_history,
                                 svn_revnum_t from_rev,
                                 const char *from_path,
                                 const char *txn_id,
                                 apr_pool_t *pool);


/* Comparison */

/* Find out what is the same between two nodes.

   If PROPS_CHANGED is non-null, set *PROPS_CHANGED to 1 if the two
   nodes have different property lists, or to 0 if same.

   If CONTENTS_CHANGED is non-null, set *CONTENTS_CHANGED to 1 if the
   two nodes have different contents, or to 0 if same.  For files,
   file contents are compared; for directories, the entries lists are
   compared.  If one is a file and the other is a directory, the one's
   contents will be compared to the other's entries list.  (Not
   terribly useful, I suppose, but that's the caller's business.)

   ### todo: This function only compares rep keys at the moment.  This
   may leave us with a slight chance of a false positive, though I
   don't really see how that would happen in practice.  Nevertheless,
   it should probably be fixed.  */
svn_error_t *svn_fs_fs__things_different(svn_boolean_t *props_changed,
                                         svn_boolean_t *contents_changed,
                                         dag_node_t *node1,
                                         dag_node_t *node2,
                                         apr_pool_t *pool);


/* Set *NODE_ID to the node-id of the coyproot of node NODE, or NULL
   if no copyroot exists.  Get any temporary allocations from POOL. */
svn_error_t *svn_fs_fs__dag_get_copyroot(svn_revnum_t *rev,
                                         const char **path,
                                         dag_node_t *node,
                                         apr_pool_t *pool);

/* Set *REV to the copyfrom revision associated with NODE.  Get any
   temporary allocations from POOL. */
svn_error_t *svn_fs_fs__dag_get_copyfrom_rev(svn_revnum_t *rev,
                                             dag_node_t *node,
                                             apr_pool_t *pool);

/* Set *PATH to the copyfrom path associated with NODE.  Get any
   temporary allocations from POOL. */
svn_error_t *svn_fs_fs__dag_get_copyfrom_path(const char **path,
                                              dag_node_t *node,
                                              apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_DAG_H */
