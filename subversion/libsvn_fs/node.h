/* node.h : interface to node functions, private to libsvn_fs
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

#ifndef SVN_LIBSVN_FS_NODE_H
#define SVN_LIBSVN_FS_NODE_H

#include "db.h"
#include "svn_fs.h"
#include "skel.h"



/* Operations on nodes.  */

/* Open the node identified by ID in FS, and set *NODE_P to point to
   it, as part of the Berkeley DB transaction DB_TXN.  */
svn_error_t *svn_fs__open_node_by_id (svn_fs_node_t **node_p,
				      svn_fs_t *fs,
				      svn_fs_id_t *id,
				      DB_TXN *db_txn);


/* Create and open an entirely new, mutable node in the filesystem FS,
   whose NODE-REVISION skel is SKEL, and set *NODE_P to point to it, as
   part of the Berkeley DB transaction DB_TXN.  SKEL must have a
   well-formed HEADER, with the "mutable" flag set.  Use POOL as
   NODE's pool; it should be a subpool of FS.

   After this call, the node table manager assumes that the new node's
   contents will change frequently.  */
svn_error_t *svn_fs__create_node (svn_fs_node_t **node_p,
				  svn_fs_t *fs,
				  skel_t *skel,
				  DB_TXN *db_txn,
				  apr_pool_t *pool);


/* Create and open a mutable node which is an immediate successor of
   OLD, and set *NEW_P to point to it.  Do this as part of the
   Berkeley DB transaction DB_TXN, and the Subversion transaction
   whose ID is SVN_TXN_ID.

   After this call, the node table manager assumes that the new node's
   contents will change frequently.  */
svn_error_t *svn_fs__create_successor (svn_fs_node_t **new_p,
				       svn_fs_node_t *old,
				       char *svn_txn_id,
				       DB_TXN *db_txn);


/* Open a new reference to NODE.  The returned node will remain open
   after NODE is closed, and can be closed without closing NODE.  */
svn_fs_node_t *svn_fs__reopen_node (svn_fs_node_t *node);


/* Return the filesystem NODE lives in.  */
svn_fs_t *svn_fs__node_fs (svn_fs_node_t *node);


/* Return the ID of NODE.  The result is live for as long as NODE is.  */
svn_fs_id_t *svn_fs__node_id (svn_fs_node_t *node);


<<<<<<< node.h
/* Return the transaction ID of NODE, if it is mutable.  The result
   is live for as long as NODE is.  */
char *svn_fs__node_txn_id (svn_fs_node_t *node);


/* Set *SKEL_P to the NODE-VERSION skel for NODE, as part of the
=======
/* Set *SKEL_P to the NODE-REVISION skel for NODE, as part of the
>>>>>>> 1.5
   Berkeley DB transaction DB_TXN.  *SKEL_P is guaranteed to be a list
   at least one element long, whose first element is a well-formed
   HEADER skel.

   If NODE is mutable, the skel, and the data it points into, are
   allocated in POOL.  If NODE is immutable, the skel is owned by the
   node object, and the caller must not change it.  */
svn_error_t *svn_fs__get_node_revision (skel_t **skel_p,
                                        svn_fs_node_t *node,
                                        DB_TXN *db_txn,
                                        apr_pool_t *pool);


/* Store SKEL as the NODE-REVISION skel for NODE, as part of the
   Berkeley DB transaction DB_TXN.

   After this call, the node table manager assumes that NODE's
   contents will change frequently.  */
svn_error_t *svn_fs__put_node_revision (svn_fs_node_t *node,
                                        skel_t *skel,
                                        DB_TXN *db_txn);


/* Indicate that the contents of NODE are expected to be stable.  This
   suggests to the node table manager that it would be effective to
   represent other nodes' contents as deltas against NODE's contents,
   if it so desired.  */
svn_error_t *svn_fs__stable_node (svn_fs_node_t *node);



/* Operations on HEADER skels.  */


/* Build a new HEADER skel in POOL whose kind is the string KIND.
   If TXN_ID is non-zero, include a "mutable" flag in the header
   with the given transaction ID.  */
skel_t *svn_fs__new_header (char *kind, char *txn_id, apr_pool_t *pool);


#endif /* SVN_LIBSVN_FS_NODE_H */
