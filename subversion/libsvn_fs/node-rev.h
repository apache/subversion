/* node-rev.h : interface to node revision retrieval and storage
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

#ifndef SVN_LIBSVN_FS_NODE_REV_H
#define SVN_LIBSVN_FS_NODE_REV_H

#include "db.h"
#include "svn_fs.h"
#include "trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Functions. ***/

/* Create an entirely new, mutable node in the filesystem FS, whose
   NODE-REVISION is NODEREVL, as part of TRAIL.  Set *ID_P to the new
   node revision's ID.  Use TRAIL->pool for any temporary allocation.

   After this call, the node table manager assumes that the new node's
   contents will change frequently.  */
svn_error_t *svn_fs__create_node (const svn_fs_id_t **id_p,
                                  svn_fs_t *fs,
                                  svn_fs__node_revision_t *noderev,
                                  trail_t *trail);

/* Create a node revision in FS which is an immediate successor of
   OLD_ID, whose contents are NEW_NR, as part of TRAIL.  Set *NEW_ID_P
   to the new node revision's ID.  Use TRAIL->pool for any temporary
   allocation.

   After this call, the deltification code assumes that the new node's
   contents will change frequently, and will avoid representing other
   nodes as deltas against this node's contents.  */
svn_error_t *svn_fs__create_successor (const svn_fs_id_t **new_id_p,
                                       svn_fs_t *fs,
                                       const svn_fs_id_t *old_id,
                                       svn_fs__node_revision_t *new_nr,
                                       trail_t *trail);


/* Indicate that the contents of the node ID in FS are expected to be
   stable now, as part of TRAIL.  This suggests to the deltification
   code that it could be effective to represent other nodes' contents
   as deltas against this node's contents.  This does not change the
   contents of the node.

   Do any necessary temporary allocation in TRAIL->pool.  */
svn_error_t *svn_fs__stable_node (svn_fs_t *fs,
                                  svn_fs_id_t *id,
                                  trail_t *trail);


/* Delete node revision ID from FS's `nodes' table, as part of TRAIL.
   WARNING: This does not check that the node revision is mutable!
   Callers should do that check themselves.  */
svn_error_t *svn_fs__delete_node_revision (svn_fs_t *fs,
                                           const svn_fs_id_t *id,
                                           trail_t *trail);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_NODE_REV_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
