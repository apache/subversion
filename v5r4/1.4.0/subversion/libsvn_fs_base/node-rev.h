/* node-rev.h : interface to node revision retrieval and storage
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

#ifndef SVN_LIBSVN_FS_NODE_REV_H
#define SVN_LIBSVN_FS_NODE_REV_H

#define APU_WANT_DB
#include <apu_want.h>

#include "svn_fs.h"
#include "trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Functions. ***/

/* Create an entirely new, mutable node in the filesystem FS, whose
   NODE-REVISION is NODEREV, as part of TRAIL.  Set *ID_P to the new
   node revision's ID.  Use POOL for any temporary allocation.

   COPY_ID is the copy_id to use in the node revision ID returned in
   *ID_P.

   TXN_ID is the Subversion transaction under which this occurs.

   After this call, the node table manager assumes that the new node's
   contents will change frequently.  */
svn_error_t *svn_fs_base__create_node(const svn_fs_id_t **id_p,
                                      svn_fs_t *fs,
                                      node_revision_t *noderev,
                                      const char *copy_id,
                                      const char *txn_id,
                                      trail_t *trail,
                                      apr_pool_t *pool);

/* Create a node revision in FS which is an immediate successor of
   OLD_ID, whose contents are NEW_NR, as part of TRAIL.  Set *NEW_ID_P
   to the new node revision's ID.  Use POOL for any temporary
   allocation.

   COPY_ID, if non-NULL, is a key into the `copies' table, and
   indicates that this new node is being created as the result of a
   copy operation, and specifically which operation that was.

   TXN_ID is the Subversion transaction under which this occurs.

   After this call, the deltification code assumes that the new node's
   contents will change frequently, and will avoid representing other
   nodes as deltas against this node's contents.  */
svn_error_t *svn_fs_base__create_successor(const svn_fs_id_t **new_id_p,
                                           svn_fs_t *fs,
                                           const svn_fs_id_t *old_id,
                                           node_revision_t *new_nr,
                                           const char *copy_id,
                                           const char *txn_id,
                                           trail_t *trail,
                                           apr_pool_t *pool);


/* Delete node revision ID from FS's `nodes' table, as part of TRAIL.
   WARNING: This does not check that the node revision is mutable!
   Callers should do that check themselves.  */
svn_error_t *svn_fs_base__delete_node_revision(svn_fs_t *fs,
                                               const svn_fs_id_t *id,
                                               trail_t *trail,
                                               apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_NODE_REV_H */
