/* dag.h : DAG-like interface filesystem, private to libsvn_fs
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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

#include "trail.h"

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


/* Initializing a filesystem.  */


/* Given a filesystem FS, which contains all the necessary tables,
   create the initial revision 0, and the initial root directory.  */
svn_error_t *svn_fs__dag_init_fs (svn_fs_t *fs);



/* Generic DAG node stuff.  */

typedef struct dag_node_t dag_node_t;


/* Fill *NODE with a dag_node_t representing node revision ID in FS,
   allocating in TRAIL->pool.  */
svn_error_t *
svn_fs__dag_get_node (dag_node_t **node,
                      svn_fs_t *fs,
                      const svn_fs_id_t *id,
                      trail_t *trail);


/* Return a new dag_node_t object referring to the same node as NODE,
   allocated in TRAIL->pool.  If you're trying to build a structure in
   TRAIL->pool that wants to refer to dag nodes that may have been
   allocated elsewhere, you can call this function, and avoid
   inter-pool pointers.  */
dag_node_t *svn_fs__dag_dup (dag_node_t *node,
                             trail_t *trail);


/* Return the filesystem containing NODE.  */
svn_fs_t *svn_fs__dag_get_fs (dag_node_t *node);


/* Set *REV to NODE's revision number, as part of TRAIL.  If NODE has
   never been committed as part of a revision, set *REV to
   SVN_INVALID_REVNUM.  */
svn_error_t *svn_fs__dag_get_revision (svn_revnum_t *rev,
                                       dag_node_t *node,
                                       trail_t *trail);


/* Return the node revision ID of NODE.  The value returned is shared
   with NODE, and will be deallocated when NODE is.  */
const svn_fs_id_t *svn_fs__dag_get_id (dag_node_t *node);


/* Set *ID_P to the node revision ID of NODE's immediate predecessor,
   or NULL if NODE has no predecessor, as part of TRAIL.  The returned
   ID will be allocated in TRAIL->pool.  */
svn_error_t *svn_fs__dag_get_predecessor_id (const svn_fs_id_t **id_p,
                                             dag_node_t *node,
                                             trail_t *trail);


/* Set *COUNT to the number of predecessors NODE has (recursively), or
   -1 if not known, as part of TRAIL.  */
svn_error_t *svn_fs__dag_get_predecessor_count (int *count,
                                                dag_node_t *node,
                                                trail_t *trail);


/* Callback function type for svn_fs__dag_walk_predecessors() */
typedef svn_error_t *(*svn_fs__dag_pred_func_t) (void *baton,
                                                 dag_node_t *node,
                                                 int *done,
                                                 trail_t *trail);

/* Walk over NODE's predecessor list, calling CALLBACK (with its
   associated BATON) for each predecessor until the callback returns
   an error (in which case, return that error) or until it sets its
   DONE flag.  When the predecessor walk reaches a node with no
   predecessor, it will call the CALLBACK one final time with a NULL
   `node' argument to indicate that the predecessor walk is now
   complete.

   Do all this as part of TRAIL.  */
svn_error_t *svn_fs__dag_walk_predecessors (dag_node_t *node,
                                            svn_fs__dag_pred_func_t callback,
                                            void *baton,
                                            trail_t *trail);

/* Return non-zero IFF NODE is currently mutable under Subversion
   transaction TXN_ID.  */
int svn_fs__dag_check_mutable (dag_node_t *node,
                               const char *txn_id);

/* Return the node kind of NODE. */
svn_node_kind_t svn_fs__dag_node_kind (dag_node_t *node);

/* Return true iff NODE is a file/directory. */
int svn_fs__dag_is_file (dag_node_t *node);
int svn_fs__dag_is_directory (dag_node_t *node);


/* Set *PROPLIST_P to a PROPLIST hash representing the entire property
   list of NODE, as part of TRAIL.  The hash has const char * names
   (the property names) and svn_string_t * values (the property values).

   If properties do not exist on NODE, *PROPLIST_P will be set to NULL.

   The returned property list is allocated in TRAIL->pool.  */
svn_error_t *svn_fs__dag_get_proplist (apr_hash_t **proplist_p,
                                       dag_node_t *node,
                                       trail_t *trail);

/* Set the property list of NODE to PROPLIST, as part of TRAIL.  The
   node being changed must be mutable.  TXN_ID is the Subversion
   transaction under which this occurs.  */
svn_error_t *svn_fs__dag_set_proplist (dag_node_t *node,
                                       apr_hash_t *proplist,
                                       const char *txn_id, 
                                       trail_t *trail);



/* Revision and transaction roots.  */


/* Open the root of revision REV of filesystem FS, as part of TRAIL.
   Set *NODE_P to the new node.  Allocate the node in TRAIL->pool.  */
svn_error_t *svn_fs__dag_revision_root (dag_node_t **node_p,
                                        svn_fs_t *fs,
                                        svn_revnum_t rev,
                                        trail_t *trail);


/* Set *NODE_P to the root of transaction TXN_ID in FS, as part
   of TRAIL.  Allocate the node in TRAIL->pool.

   Note that the root node of TXN_ID is not necessarily mutable.  If no
   changes have been made in the transaction, then it may share its
   root directory with its base revision.  To get a mutable root node
   for a transaction, call svn_fs__dag_clone_root.  */
svn_error_t *svn_fs__dag_txn_root (dag_node_t **node_p,
                                   svn_fs_t *fs,
                                   const char *txn_id,
                                   trail_t *trail);


/* Set *NODE_P to the base root of transaction TXN_ID in FS, as part
   of TRAIL.  Allocate the node in TRAIL->pool.  */
svn_error_t *svn_fs__dag_txn_base_root (dag_node_t **node_p,
                                        svn_fs_t *fs,
                                        const char *txn_id,
                                        trail_t *trail);


/* Clone the root directory of TXN_ID in FS, and update the
   `transactions' table entry to point to it, unless this has been
   done already.  In either case, set *ROOT_P to a reference to the
   root directory clone.  Do all this as part of TRAIL, and allocate
   *ROOT_P in TRAIL->pool.  */
svn_error_t *svn_fs__dag_clone_root (dag_node_t **root_p,
                                     svn_fs_t *fs,
                                     const char *txn_id,
                                     trail_t *trail);


/* Commit the transaction TXN_ID in FS, as part of TRAIL.  Store the
   new revision number in *NEW_REV.  This entails:
   - marking the tree of mutable nodes at TXN_ID's root as immutable,
     and marking all their contents as stable
   - creating a new revision, with TXN_ID's root as its root directory
   - promoting TXN_ID to a "committed" transaction.

   Beware!  This does not make sure that TXN_ID is based on the very
   latest revision in FS.  If the caller doesn't take care of this,
   you may lose people's work!

   Do any necessary temporary allocation in a subpool of TRAIL->pool.
   Consume temporary space at most proportional to the maximum depth
   of SVN_TXN's tree of mutable nodes.  */
svn_error_t *svn_fs__dag_commit_txn (svn_revnum_t *new_rev,
                                     svn_fs_t *fs,
                                     const char *txn_id,
                                     trail_t *trail);



/* Directories.  */


/* Open the node named NAME in the directory PARENT, as part of TRAIL.
   Set *CHILD_P to the new node, allocated in TRAIL->pool.  NAME must be a
   single path component; it cannot be a slash-separated directory
   path.  */
svn_error_t *svn_fs__dag_open (dag_node_t **child_p,
                               dag_node_t *parent,
                               const char *name,
                               trail_t *trail);


/* Set *ENTRIES_P to a hash table of NODE's entries, as part of TRAIL,
   or NULL if NODE has no entries.  The keys of the table are entry
   names, and the values are svn_fs_dirent_t's.

   The returned table is allocated in *either* TRAIL->pool or the pool
   NODE was allocated in, at this function's discretion; the caller
   must finish using it while both of those remain live.  If the
   caller needs the table to live longer, it should copy the hash.  */
svn_error_t *svn_fs__dag_dir_entries (apr_hash_t **entries_p,
                                      dag_node_t *node,
                                      trail_t *trail);


/* Set ENTRY_NAME in NODE to point to ID, as part of TRAIL.  NODE must
   be a mutable directory.  ID can refer to a mutable or immutable
   node.  If ENTRY_NAME does not exist, it will be created.  TXN_ID is
   the Subversion transaction under which this occurs.*/
svn_error_t *svn_fs__dag_set_entry (dag_node_t *node,
                                    const char *entry_name,
                                    const svn_fs_id_t *id,
                                    const char *txn_id, 
                                    trail_t *trail);


/* Make a new mutable clone of the node named NAME in PARENT, and
   adjust PARENT's directory entry to point to it, as part of TRAIL,
   unless NAME in PARENT already refers to a mutable node.  In either
   case, set *CHILD_P to a reference to the new node, allocated in
   TRAIL->pool.  PARENT must be mutable.  NAME must be a single path
   component; it cannot be a slash-separated directory path.  

   COPY_ID, if non-NULL, is a key into the `copies' table, and
   indicates that this new node is being created as the result of a
   copy operation, and specifically which operation that was.  

   TXN_ID is the Subversion transaction under which this occurs.  */
svn_error_t *svn_fs__dag_clone_child (dag_node_t **child_p,
                                      dag_node_t *parent,
                                      const char *name,
                                      const char *copy_id,
                                      const char *txn_id, 
                                      trail_t *trail);


/* Create a link to CHILD in PARENT named NAME, as part of TRAIL.
   PARENT must be mutable.  CHILD must be immutable.  NAME must be a
   single path component; it cannot be a slash-separated directory
   path.  TXN_ID is the Subversion transaction under which this
   occurs.

   Note that it is impossible to use this function to create cyclic
   directory structures.  Since PARENT is mutable, and every parent of
   a mutable node is mutable itself, and CHILD is immutable, we know
   that CHILD can't be equal to, or a parent of, PARENT.  */
svn_error_t *svn_fs__dag_link (dag_node_t *parent,
                               dag_node_t *child,
                               const char *name,
                               const char *txn_id, 
                               trail_t *trail);


/* Delete the directory entry named NAME from PARENT, as part of
   TRAIL.  PARENT must be mutable.  NAME must be a single path
   component; it cannot be a slash-separated directory path.  If the
   node being deleted is a directory, it must be empty.  TXN_ID is the
   Subversion transaction under which this occurs.

   If return SVN_ERR_FS_NO_SUCH_ENTRY, then there is no entry NAME in
   PARENT.  */
svn_error_t *svn_fs__dag_delete (dag_node_t *parent,
                                 const char *name,
                                 const char *txn_id, 
                                 trail_t *trail);


/* Delete the directory entry named NAME from PARENT, as part of
   TRAIL.  PARENT must be mutable.  NAME must be a single path
   component; it cannot be a slash-separated directory path.  If the
   node being deleted is a mutable directory, remove all mutable nodes
   reachable from it.  TXN_ID is the Subversion transaction under
   which this occurs.

   If return SVN_ERR_FS_NO_SUCH_ENTRY, then there is no entry NAME in
   PARENT.  */
svn_error_t *svn_fs__dag_delete_tree (dag_node_t *parent,
                                      const char *name,
                                      const char *txn_id,
                                      trail_t *trail);


/* Delete all mutable node revisions reachable from node ID, including
   ID itself, from FS's `nodes' table, as part of TRAIL.  ID may refer
   to a file or directory, which may be mutable or immutable.  TXN_ID
   is the Subversion transaction under which this occurs.  */
svn_error_t *svn_fs__dag_delete_if_mutable (svn_fs_t *fs,
                                            const svn_fs_id_t *id,
                                            const char *txn_id,
                                            trail_t *trail);


/* Create a new mutable directory named NAME in PARENT, as part of
   TRAIL.  Set *CHILD_P to a reference to the new node, allocated in
   TRAIL->pool.  The new directory has no contents, and no properties.
   PARENT must be mutable.  NAME must be a single path component; it
   cannot be a slash-separated directory path.  PARENT must not
   currently have an entry named NAME.  Do any temporary allocation in
   TRAIL->pool.  TXN_ID is the Subversion transaction under which this
   occurs.  */
svn_error_t *svn_fs__dag_make_dir (dag_node_t **child_p,
                                   dag_node_t *parent,
                                   const char *name,
                                   const char *txn_id,
                                   trail_t *trail);



/* Files.  */


/* Set *CONTENTS to a readable generic stream which yields the
   contents of FILE, as part of TRAIL.  Allocate the stream in POOL,
   which may or may not be TRAIL->pool.

   If FILE is not a file, return SVN_ERR_FS_NOT_FILE.  */
svn_error_t *svn_fs__dag_get_contents (svn_stream_t **contents,
                                       dag_node_t *file,
                                       apr_pool_t *pool,
                                       trail_t *trail);


/* Return a generic writable stream in *CONTENTS with which to set the
   contents of FILE as part of TRAIL.  Allocate the stream in POOL,
   which may or may not be TRAIL->pool.  TXN_ID is the Subversion
   transaction under which this occurs.

   Any previous edits on the file will be deleted, and a new edit
   stream will be constructed.  */
svn_error_t *svn_fs__dag_get_edit_stream (svn_stream_t **contents,
                                          dag_node_t *file,
                                          apr_pool_t *pool,
                                          const char *txn_id, 
                                          trail_t *trail);


/* Signify the completion of edits to FILE made using the stream
   returned by svn_fs__dag_get_edit_stream, as part of TRAIL.  TXN_ID
   is the Subversion transaction under which this occurs.

   This operation is a no-op if no edits are present.  */
svn_error_t *svn_fs__dag_finalize_edits (dag_node_t *file,
                                         const char *txn_id, 
                                         trail_t *trail);


/* Set *LENGTH to the length of the contents of FILE, as part of TRAIL. */
svn_error_t *svn_fs__dag_file_length (apr_size_t *length,
                                      dag_node_t *file,
                                      trail_t *trail);


/* Create a new mutable file named NAME in PARENT, as part of TRAIL.
   Set *CHILD_P to a reference to the new node, allocated in
   TRAIL->pool.  The new file's contents are the empty string, and it
   has no properties.  PARENT must be mutable.  NAME must be a single
   path component; it cannot be a slash-separated directory path.
   TXN_ID is the Subversion transaction under which this occurs.  */
svn_error_t *svn_fs__dag_make_file (dag_node_t **child_p,
                                    dag_node_t *parent,
                                    const char *name,
                                    const char *txn_id,
                                    trail_t *trail);



/* Copies */

/* Make ENTRY in TO_NODE be a copy of FROM_NODE, as part of TRAIL.
   TO_NODE must be mutable.  TXN_ID is the Subversion transaction
   under which this occurs.

   If PRESERVE_HISTORY is true, the new node will record that it was
   copied from FROM_PATH in FROM_REV; therefore, FROM_NODE should be
   the node found at FROM_PATH in FROM_REV, although this is not
   checked.  

   If PRESERVE_HISTORY is false, FROM_PATH and FROM_REV are ignored.  */
svn_error_t *svn_fs__dag_copy (dag_node_t *to_node,
                               const char *entry,
                               dag_node_t *from_node,
                               svn_boolean_t preserve_history,
                               svn_revnum_t from_rev,
                               const char *from_path,
                               const char *txn_id, 
                               trail_t *trail);


/* If NODE was copied from some other node, set *REV_P and *PATH_P to
   the revision and path of the other node, as part of TRAIL.
   Allocate *PATH_P in TRAIL->pool.

   Else if NODE is not a copy, set *REV_P to SVN_INVALID_REVNUM and
   *PATH_P to null.  */
svn_error_t *svn_fs__dag_copied_from (svn_revnum_t *rev_p,
                                      const char **path_p,
                                      dag_node_t *node,
                                      trail_t *trail);



/* Deltification */

/* Change TARGET's representation to be a delta against SOURCE, as
   part of TRAIL.  If TARGET or SOURCE does not exist, do nothing and
   return success.  If PROPS_ONLY is non-zero, only the node property
   portion of TARGET will be deltified.  

   WARNING WARNING WARNING: Do *NOT* call this with a mutable SOURCE
   node.  Things will go *very* sour if you deltify TARGET against a
   node that might just disappear from the filesystem in the (near)
   future.  */
svn_error_t *svn_fs__dag_deltify (dag_node_t *target,
                                  dag_node_t *source,
                                  int props_only,
                                  trail_t *trail);


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
svn_error_t *svn_fs__things_different (int *props_changed,
                                       int *contents_changed,
                                       dag_node_t *node1,
                                       dag_node_t *node2,
                                       trail_t *trail);


/* Set *IS_ANCESTOR to non-zero IFF NODE1 is an ancestor of NODE2.
   Perform this test under TRAIL.  */
svn_error_t *svn_fs__dag_is_ancestor (int *is_ancestor,
                                      dag_node_t *node1,
                                      dag_node_t *node2,
                                      trail_t *trail);


/* Set *IS_PARENT to non-zero IFF NODE1 is the parent of NODE2.
   Perform this test under TRAIL.  */
svn_error_t *svn_fs__dag_is_parent (int *is_ancestor,
                                    dag_node_t *node1,
                                    dag_node_t *node2,
                                    trail_t *trail);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_DAG_H */
