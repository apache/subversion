/* rep.h --- interface to storing and retrieving NODE-VERSION skels
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

#ifndef SVN_LIBSVN_FS_REP_H
#define SVN_LIBSVN_FS_REP_H

#include "fs.h"
#include "skel.h"


/* Creating and opening the Berkeley DB `nodes' table.  */


/* Create a new `nodes' table for the new filesystem FS.  FS->env must
   already be open; this sets FS->nodes.  */
svn_error_t *svn_fs__create_nodes (svn_fs_t *fs);


/* Open the existing `nodes' table for the filesystem FS.  FS->env
   must already be open; this sets FS->nodes.  */
svn_error_t *svn_fs__open_nodes (svn_fs_t *fs);



/* Storing and retrieving NODE-REVISION skels.  */

/* Set *SKEL_P to the NODE-REVISION skel for the node ID in FS, as
   part of the Berkeley DB transaction DB_TXN.  Allocate the skel, and
   do any other temporary allocation in POOL.  */
svn_error_t *svn_fs__get_node_revision (skel_t **skel_p,
					svn_fs_t *fs,
                                        DB_TXN *db_txn,
                                        const svn_fs_id_t *id,
                                        apr_pool_t *pool);


/* Store SKEL as the NODE-REVISION skel for the node revision whose id
   is ID in FS, as part of the Berkeley DB transaction DB_TXN.  Do any
   necessary temporary allocation in POOL.

   After this call, the node table manager assumes that NODE's
   contents will change frequently.  */
svn_error_t *svn_fs__put_node_revision (svn_fs_t *fs,
                                        DB_TXN *db_txn,
					const svn_fs_id_t *id,
                                        skel_t *skel,
					apr_pool_t *pool);


/* Indicate that the contents of the node ID in FS are expected to be
   stable.  This suggests to the node table manager that it would be
   effective to represent other nodes' contents as deltas against this
   node's contents, if it so desired.

   Do any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs__stable_node (svn_fs_t *fs,
				  svn_fs_id_t *id,
				  apr_pool_t *pool);



/* Creating nodes, and new revisions of existing nodes.  */
				  

/* Create an entirely new, mutable node in the filesystem FS, whose
   NODE-REVISION skel is SKEL, as part of the Berkeley DB transaction
   DB_TXN.  Set *ID_P to the new node revision's ID.  Use POOL for any
   temporary allocation.

   After this call, the node table manager assumes that the new node's
   contents will change frequently.  */
svn_error_t *svn_fs__create_node (svn_fs_id_t **id_p,
				  svn_fs_t *fs,
				  DB_TXN *db_txn,
				  skel_t *skel,
				  apr_pool_t *pool);


/* Create a mutable node in FS which is an immediate successor of
   OLD_ID, whose contents are NEW_SKEL, as part of the Berkeley DB
   transaction DB_TXN.  Set *NEW_ID_P to the new node revision's ID.
   Use POOL for any temporary allocation.

   After this call, the node table manager assumes that the new node's
   contents will change frequently.  */
svn_error_t *svn_fs__create_successor (svn_fs_id_t **new_id_p,
				       svn_fs_t *fs,
				       DB_TXN *db_txn,
				       svn_fs_id_t *old_id,
				       skel_t *new_skel,
				       apr_pool_t *pool);


#endif /* SVN_LIBSVN_FS_REP_H */
