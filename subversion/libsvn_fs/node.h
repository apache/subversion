/* node.h : interface to node functions, private to libsvn_fs
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */

#ifndef SVN_LIBSVN_FS_NODE_H
#define SVN_LIBSVN_FS_NODE_H

#include "db.h"
#include "svn_fs.h"
#include "skel.h"



/* Creating and opening the Berkeley DB `nodes' table.  */


/* Create a new `nodes' table for the new filesystem FS.  FS->env must
   already be open; this sets FS->nodes.  */
svn_error_t *svn_fs__create_nodes (svn_fs_t *fs);


/* Open the existing `nodes' table for the filesystem FS.  FS->env
   must already be open; this sets FS->nodes.  */
svn_error_t *svn_fs__open_nodes (svn_fs_t *fs);



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
   well-formed HEADER, with the "mutable" flag set.

   After this call, the node table manager assumes that the new node's
   contents will change frequently.  */
svn_error_t *svn_fs__create_node (svn_fs_node_t **node_p,
				  svn_fs_t *fs,
				  skel_t *skel,
				  DB_TXN *db_txn);


/* Create and open a mutable node which is an immediate successor of
   OLD, and set *NEW_P to point to it.  Do this as part of the
   Berkeley DB transaction DB_TXN, and the Subversion transaction
   SVN_TXN.

   After this call, the node table manager assumes that the new node's
   contents will change frequently.  */
svn_error_t *svn_fs__create_successor (svn_fs_node_t **new_p,
				       svn_fs_node_t *old,
				       svn_fs_txn_t *svn_txn,
				       DB_TXN *db_txn);


/* Open a new reference to NODE.  The returned node will remain open
   after NODE is closed, and can be closed without closing NODE.  */
svn_fs_node_t *svn_fs__reopen_node (svn_fs_node_t *node);


/* Return the filesystem NODE lives in.  */
svn_fs_t *svn_fs__node_fs (svn_fs_node_t *node);


/* Return the ID of NODE.  The result is live for as long as NODE is.  */
svn_fs_id_t *svn_fs__node_id (svn_fs_node_t *node);


/* Set *SKEL_P to the NODE-REVISION skel for NODE, as part of the
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


#endif /* SVN_LIBSVN_FS_NODE_H */
