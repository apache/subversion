/* fs_fs.h : interface to the native filesystem layer
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

#ifndef SVN_LIBSVN_FS__FS_FS_H
#define SVN_LIBSVN_FS__FS_FS_H

/* Open the fs_fs filesystem pointed to by PATH and associate it with
   filesystem object FS.  Use POOL for temporary allocations. */
svn_error_t *svn_fs__fs_open (svn_fs_t *fs,
                              const char *path,
                              apr_pool_t *pool);

/* Set *NODEREV_P to the node-revision for the node ID in FS.  Do any
   allocations in POOL. */
svn_error_t *svn_fs__fs_get_node_revision (svn_fs__node_revision_t **noderev_p,
                                           svn_fs_t *fs,
                                           const svn_fs_id_t *id,
                                           apr_pool_t *pool);

/* Store NODEREV as the node-revision for the node whose id is ID in
   FS.  Do any necessary temporary allocation in POOL. */
svn_error_t *svn_fs__fs_put_node_revision (svn_fs_t *fs,
                                           const svn_fs_id_t *id,
                                           svn_fs__node_revision_t *noderev,
                                           apr_pool_t *pool);

/* Set *YOUNGEST to the youngest revision in filesystem FS.  Do any
   temporary allocation in POOL. */
svn_error_t *svn_fs__fs_youngest_revision (svn_revnum_t *youngest,
                                           svn_fs_t *fs,
                                           apr_pool_t *pool);

/* Set *ROOT_ID to the node-id for the root of revision REV in
   filesystem FS.  Do any allocations in POOL. */
svn_error_t *svn_fs__fs_rev_get_root (svn_fs_id_t **root_id,
                                      svn_fs_t *fs,
                                      svn_revnum_t rev,
                                      apr_pool_t *pool);

/* Set *ENTRIES to an apr_hash_t of dirent structs that contain the
   directory entries of node-revision NODEREV in filesystem FS.  Use
   POOL for temporary allocations. */
svn_error_t *svn_fs__fs_rep_contents_dir (apr_hash_t **entries,
                                          svn_fs_t *fs,
                                          svn_fs__node_revision_t *noderev,
                                          apr_pool_t *pool);

/* Set *CONTENTS to be a readable svn_stream_t that receives the text
   representation of node-revision NODEREV as seen in filesystem FS.
   Use POOL for temporary allocations. */
svn_error_t *svn_fs__fs_get_contents (svn_stream_t **contents,
                                      svn_fs_t *fs,
                                      svn_fs__node_revision_t *noderev,
                                      apr_pool_t *pool);

/* Set *PROPLIST to be an apr_hash_t containing the property list of
   node-revision NODEREV as seen in filesystem FS.  Use POOL for
   temporary allocations. */
svn_error_t *svn_fs__fs_get_proplist (apr_hash_t **proplist,
                                      svn_fs_t *fs,
                                      svn_fs__node_revision_t *noderev,
                                      apr_pool_t *pool);

/* Set the revision property list of revision REV in filesystem FS to
   PROPLIST.  Use POOL for temporary allocations. */
svn_error_t *svn_fs__fs_set_revision_proplist (svn_fs_t *fs,
                                               svn_revnum_t rev,
                                               apr_hash_t *proplist,
                                               apr_pool_t *pool);

/* Set *PROPLIST to be an apr_hash_t containing the property list of
   revision REV as seen in filesystem FS.  Use POOL for temporary
   allocations. */
svn_error_t *svn_fs__fs_revision_proplist (apr_hash_t **proplist,
                                           svn_fs_t *fs,
                                           svn_revnum_t rev,
                                           apr_pool_t *pool);

/* Set *LENGTH to the be fulltext length of the node revision
   specified by NODEREV.  Use POOL for temporary allocations. */
svn_error_t *svn_fs__fs_file_length (svn_filesize_t *length,
                                     svn_fs__node_revision_t *noderev,
                                     apr_pool_t *pool);

/* Return TRUE if the representation keys in A and B both point to the
   same representation, else return FALSE. */
svn_boolean_t svn_fs__fs_noderev_same_rep_key (svn_fs__representation_t *a,
                                               svn_fs__representation_t *b);


/* Return a copy of the representation REP allocated from POOL. */
svn_fs__representation_t *svn_fs__fs_rep_copy (svn_fs__representation_t *rep,
                                               apr_pool_t *pool);


/* Return the record MD5 checksum of the text representation of NODREV
   into DIGEST, allocating from POOL.  If no stored checksum is
   available, put all 0's into DIGEST. */
svn_error_t *svn_fs__fs_file_checksum (unsigned char digest[],
                                       svn_fs__node_revision_t *noderev,
                                       apr_pool_t *pool);

/* Find the paths which were changed in revision REV of filesystem FS
   and store them in *CHANGED_PATHS_P.  Get any temporary allocations
   from POOL. */
svn_error_t *svn_fs__fs_paths_changed (apr_hash_t **changed_paths_p,
                                       svn_fs_t *fs,
                                       svn_revnum_t rev,
                                       apr_pool_t *pool);

/* Create a new transaction in filesystem FS, based on revision REV,
   and store it in *TXN_P.  Allocate all necessary variables from
   POOL. */
svn_error_t *svn_fs__fs_begin_txn  (svn_fs_txn_t **txn_p,
                                    svn_fs_t *fs,
                                    svn_revnum_t rev,
                                    apr_pool_t *pool);

/* Set the transaction property NAME to the value VALUE in transaction
   TXN.  Perform temporary allocations from POOL. */
svn_error_t *svn_fs__fs_change_txn_prop (svn_fs_txn_t *txn,
                                         const char *name,
                                         const svn_string_t *value,
                                         apr_pool_t *pool);

/* Store a transaction record in *TXN_P for the transaction identified
   by TXN_ID in filesystem FS.  Allocate everything from POOL. */
svn_error_t *svn_fs__fs_get_txn (svn_fs__transaction_t **txn_p,
                                 svn_fs_t *fs,
                                 const char *txn_id,
                                 apr_pool_t *pool);

/* Create an entirely new mutable node in the filesystem FS, whose
   node-revision is NODEREV.  Set *ID_P to the new node revision's ID.
   Use POOL for any temporary allocation.  COPY_ID is the copy_id to
   use in the node revision ID.  TXN_ID is the Subversion transaction
   under which this occurs. */
svn_error_t *svn_fs__fs_create_node (const svn_fs_id_t **id_p,
                                     svn_fs_t *fs,
                                     svn_fs__node_revision_t *noderev,
                                     const char *copy_id,
                                     const char *txn_id,
                                     apr_pool_t *pool);

/* Remove all references to the transaction TXN_ID from filesystem FS.
   Temporary allocations are from POOL. */
svn_error_t *svn_fs__fs_purge_txn (svn_fs_t *fs,
                                   const char *txn_id,
                                   apr_pool_t *pool);

/* Add or set in filesystem FS, transaction TXN_ID, in directory
   PARENT_NODEREV a directory entry for NAME pointing to ID of type
   KIND.  Allocations are done in POOL. */
svn_error_t *svn_fs__fs_set_entry (svn_fs_t *fs,
                                   const char *txn_id,
                                   svn_fs__node_revision_t *parent_noderev,
                                   const char *name,
                                   const svn_fs_id_t *id,
                                   svn_node_kind_t kind,
                                   apr_pool_t *pool);

/* Add a change to the changes record for filesystem FS in transaction
   TXN_ID.  Mark path PATH, having node-id ID, as changed according to
   the type in CHANGE_KIND.  If the text representation was changed
   set TEXT_MOD to TRUE, and likewise for PROP_MOD.  Perform any
   temporary allocations from POOL. */
svn_error_t *svn_fs__fs_add_change (svn_fs_t *fs,
                                    const char *txn_id,
                                    const char *path,
                                    const svn_fs_id_t *id,
                                    svn_fs_path_change_kind_t change_kind,
                                    svn_boolean_t text_mod,
                                    svn_boolean_t prop_mod,
                                    apr_pool_t *pool);

/* Return a writable stream in *STREAM that allows storing the text
   representation of node-revision NODEREV in filesystem FS.
   Allocations are from POOL. */
svn_error_t *svn_fs__fs_set_contents (svn_stream_t **stream,
                                      svn_fs_t *fs,
                                      svn_fs__node_revision_t *noderev,
                                      apr_pool_t *pool);

/* Create a node revision in FS which is an immediate successor of
   OLD_ID, whose contents are NEW_NR.  Set *NEW_ID_P to the new node
   revision's ID.  Use POOL for any temporary allocation.

   COPY_ID, if non-NULL, is a key into the `copies' table, and
   indicates that this new node is being created as the result of a
   copy operation, and specifically which operation that was.

   TXN_ID is the Subversion transaction under which this occurs.

   After this call, the deltification code assumes that the new node's
   contents will change frequently, and will avoid representing other
   nodes as deltas against this node's contents.  */
svn_error_t *svn_fs__fs_create_successor (const svn_fs_id_t **new_id_p,
                                          svn_fs_t *fs,
                                          const svn_fs_id_t *old_idp,
                                          svn_fs__node_revision_t *new_noderev,
                                          const char *copy_id,
                                          const char *txn_id,
                                          apr_pool_t *pool);

/* Write a new property list PROPLIST for node-revision NODEREV in
   filesystem FS.  Perform any temporary allocations in POOL. */
svn_error_t *svn_fs__fs_set_proplist (svn_fs_t *fs,
                                      svn_fs__node_revision_t *noderev,
                                      apr_hash_t *proplist,
                                      apr_pool_t *pool);

/* Commit the transaction TXN in filesystem FS and return it's new
   revision number in *REV.  If the transaction is out of date, return
   the error SVN_ERR_FS_TXN_OUT_OF_DATE.  Use POOL for temporary
   allocations. */
svn_error_t *svn_fs__fs_commit (svn_revnum_t *new_rev_p,
                                svn_fs_t *fs,
                                svn_fs_txn_t *txn,
                                apr_pool_t *pool);

/* Return the next available copy_id in *COPY_ID for the transaction
   TXN_ID in filesystem FS.  Allocate space in POOL. */
svn_error_t *svn_fs__fs_reserve_copy_id (const char **copy_id,
                                         svn_fs_t *fs,
                                         const char *txn_id,
                                         apr_pool_t *pool);

/* Create a fs_fs fileysystem referenced by FS at path PATH.  Get any
   temporary allocations from POOL. */
svn_error_t *svn_fs__fs_create (svn_fs_t *fs,
                                const char *path,
                                apr_pool_t *pool);

/* Store the uuid of the repository FS in *UUID.  Allocate space in
   POOL. */
svn_error_t *svn_fs__fs_get_uuid (const char **uuid,
                                  svn_fs_t *fs,
                                  apr_pool_t *pool);

/* Set the uuid of repository FS to UUID.  Perform temporary
   allocations in POOL. */
svn_error_t *svn_fs__fs_set_uuid (svn_fs_t *fs,
                                  const char *uuid,
                                  apr_pool_t *pool);

/* Write out the zeroth revision for filesystem FS. */
svn_error_t *svn_fs__fs_write_revision_zero (svn_fs_t *fs);

/* Following are defines that specify the textual elements of the
   native filesystem directories and revision files. */

/* Names of special files in the fs_fs filesystem. */
#define SVN_FS_FS__UUID              "uuid"     /* Contains UUID */
#define SVN_FS_FS__CURRENT           "current"  /* Youngest revision */
#define SVN_FS_FS__LOCK_FILE         "write-lock" /* Revision lock file */

#define SVN_FS_FS__REVS_DIR          "revs"     /* Directory of revisions */
#define SVN_FS_FS__REVPROPS_DIR      "revprops" /* Directory of revprops */
#define SVN_FS_FS__TXNS_DIR          "transactions"
#define SVN_FS_FS__CHANGES           "changes"
#define SVN_FS_FS__TXNS_EXT          ".txn"
#define SVN_FS_FS__TXNS_PROPS        "props"
#define SVN_FS_FS__NEXT_IDS          "next-ids"
#define SVN_FS_FS__CHILDREN_EXT      ".children"
#define SVN_FS_FS__REV               "rev"

/* Headers used to describe node-revision in the revision file. */
#define SVN_FS_FS__NODE_ID           "id"       
#define SVN_FS_FS__KIND              "type"     
#define SVN_FS_FS__COUNT             "count"    
#define SVN_FS_FS__PROPS             "props"    
#define SVN_FS_FS__TEXT              "text"      
#define SVN_FS_FS__CPATH             "cpath"
#define SVN_FS_FS__PRED              "pred"
#define SVN_FS_FS__COPYFROM          "copyfrom"
#define SVN_FS_FS__COPYROOT          "copyroot"

/* Kinds that a change can be. */
#define SVN_FS_FS__ACTION_MODIFY     "modify"
#define SVN_FS_FS__ACTION_ADD        "add"
#define SVN_FS_FS__ACTION_DELETE     "delete"
#define SVN_FS_FS__ACTION_REPLACE    "replace"
#define SVN_FS_FS__ACTION_RESET      "reset"

/* True and False flags. */
#define SVN_FS_FS__TRUE              "true"
#define SVN_FS_FS__FALSE             "false"

/* Kinds that a node-rev can be. */
#define SVN_FS_FS__FILE              "file"   /* node-rev kind for file */
#define SVN_FS_FS__DIR               "dir"    /* node-rev kind for directory */

#define SVN_FS_FS__SOFT              "soft"
#define SVN_FS_FS__HARD              "hard"

#define SVN_FS_FS__THIS "this"

#endif
