/* dag.h : DAG-like interface filesystem, private to libsvn_fs
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

#ifndef SVN_LIBSVN_FS_DAG_H
#define SVN_LIBSVN_FS_DAG_H

#include "svn_fs.h"
#include "db.h"
#include "skel.h"

/* The interface in this file provides all the essential filesystem
   operations, but exposes the filesystem's DAG structure.  This makes
   it simpler to implement than the public interface, since the client
   has to understand and cope with shared structure directly as it
   appears in the database.  However, it's still a self-consistent set
   of invariants to maintain, making it (hopefully) a useful interface
   boundary.

   The fundamental differences between this interface and the public
   interface are:
   - exposes DAG structure,
   - identifies node revisions by ID, not by an opaque data type,
   - cloning is explicit --- no clone tracking, and
   - based on Berkeley DB transactions, not SVN transactions.  */



/* References to nodes in DAG filesystems.  */

typedef struct dag_node_t dag_node_t;


/* Open the root of revision REV of filesystem FS, as part of the
   Berkeley DB transaction DB_TXN.  Set *NODE_P to the new node.
   Allocate the node in POOL.  */
svn_error_t *svn_fs__dag_revision_root (dag_node_t **node_p,
					svn_fs_t *fs,
					svn_revnum_t rev,
					DB_TXN *db_txn,
					apr_pool_t *pool);


/* Open the root of the transaction named TXN in FS, as part of the
   Berkeley DB transaction DB_TXN; set *NODE_P to the new node.
   Allocate the node in POOL.  */
svn_error_t *svn_fs__dag_txn_root (dag_node_t **node_p,
				   svn_fs_t *fs,
				   const char *txn,
				   DB_TXN *db_txn,
				   apr_pool_t *pool);


/* Close NODE.  */
void svn_fs__dag_close (dag_node_t *node); 



/* Generic node operations.  */


/* Return the ID of NODE.  The value returned is shared with NODE, and
   will be deallocated when NODE is.  */
const svn_fs_id_t *svn_fs__dag_get_id (dag_node_t *node);


/* Set *PROPLIST_P to a PROPLIST skel representing the entire property
   list of NODE, as part of the Berkeley DB transaction DB_TXN.
   Allocate the skel in POOL.  */
svn_error_t *svn_fs__dag_get_proplist (skel_t **proplist_p,
				       dag_node_t *node,
				       DB_TXN *db_txn,
				       apr_pool_t *pool);


/* Set the property list of NODE to PROPLIST, as part of Berkeley DB
   transaction DB_TXN.  The node being changed must be mutable.  Do
   any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs__dag_set_proplist (dag_node_t *node,
				       skel_t *proplist,
				       DB_TXN *db_txn,
				       apr_pool_t *pool);


/* Mark NODE as immutable.  All nodes are created mutable; this call
   indicates that the node's contents will no longer change.  The
   filesystem might elect to store NODE in a more compact form, or to
   store other nodes as deltas relative to NODE.  Do any necessary
   temporary allocation in POOL.  */
svn_error_t *svn_fs__dag_stabilize (dag_node_t *node,
				    apr_pool_t *pool);



/* Directories.  */


/* Open the node named NAME in the directory PARENT, as part of the
   Berkeley DB transaction DB_TXN.  Set *CHILD_P to the new node,
   allocated in POOL.  NAME must be a single path component; it cannot
   be a slash-separated directory path.  */
svn_error_t *svn_fs__dag_open (dag_node_t **child_p,
			       dag_node_t *parent,
			       const char *name,
			       DB_TXN *db_txn,
			       apr_pool_t *pool);


/* Create a link to CHILD in PARENT named NAME, as part of the
   Berkeley DB transaction DB_TXN.  PARENT must be mutable.  NAME must
   be a single path component; it cannot be a slash-separated
   directory path.  Do all temporary allocation in POOL.  */
svn_error_t *svn_fs__dag_link (dag_node_t *parent,
			       dag_node_t *child,
			       const char *name,
			       DB_TXN *db_txn,
			       apr_pool_t *pool);


/* Delete the directory entry named NAME from PARENT, as part of the
   Berkeley DB transaction DB_TXN.  PARENT must be mutable.  NAME must
   be a single path component; it cannot be a slash-separated
   directory path.  Do all temporary allocation in POOL.  */
svn_error_t *svn_fs__dag_delete (dag_node_t *parent,
				 const char *name,
				 DB_TXN *db_txn,
				 apr_pool_t *pool);


/* Create a new directory named NAME in PARENT, as part of the
   Berkeley DB transaction DB_TXN.  Set *CHILD_P to a reference to the
   new node, allocated in POOL.  The new directory has no contents,
   and no properties.  PARENT must be mutable.  NAME must be a single
   path component; it cannot be a slash-separated directory path.  Do
   any temporary allocation in POOL.  */
svn_error_t *svn_fs__dag_make_dir (dag_node_t **child_p,
				   dag_node_t *parent,
				   const char *name,
				   DB_TXN *db_txn,
				   apr_pool_t *pool);



/* Files.  */


/* Set *CONTENTS_FN_P and *CONTENTS_BATON_P to a `read'-like function
   and baton which return the contents of FILE, as part of the
   Berkeley DB transaction DB_TXN.  Allocate the BATON in POOL.  */
svn_error_t *svn_fs__dag_get_contents (dag_node_t *file,
				       DB_TXN *db_txn,
				       apr_pool_t *pool);


/* Set the contents of FILE to CONTENTS, as part of the Berkeley DB
   transaction DB_TXN.  (Yes, this interface will need to be revised
   to handle large files; let's get things working first.)  Do all
   temporary allocation in POOL.  */
svn_error_t *svn_fs__dag_set_contents (dag_node_t *file,
				       svn_string_t *contents,
				       DB_TXN *db_txn,
				       apr_pool_t *pool);



#endif /* SVN_LIBSVN_FS_DAG_H */
