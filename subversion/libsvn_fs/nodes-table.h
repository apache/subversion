/* nodes-table.h : interface to `nodes' table
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

#ifndef SVN_LIBSVN_FS_NODES_TABLE_H
#define SVN_LIBSVN_FS_NODES_TABLE_H

#include "db.h"
#include "svn_fs.h"
#include "skel.h"
#include "trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Creating and opening the `nodes' table.  */


/* Open a `nodes' table in ENV.  If CREATE is non-zero, create
   one if it doesn't exist.  Set *NODES_P to the new table.  
   Return a Berkeley DB error code.  */
int svn_fs__open_nodes_table (DB **nodes_p,
                              DB_ENV *env,
                              int create);


/* Check FS's `nodes' table to find an unused node number, and set
   *ID_P to the ID of the first revision of an entirely new node in
   FS, as part of TRAIL.  Allocate the new ID, and do all temporary
   allocation, in TRAIL->pool.  */
svn_error_t *svn_fs__new_node_id (svn_fs_id_t **id_p,
                                  svn_fs_t *fs,
                                  trail_t *trail);


/* Delete node revision ID from FS's `nodes' table, as part of TRAIL.
   WARNING: This does not check that the node revision is mutable!
   Callers should do that check themselves.

   todo: Jim and Karl are both not sure whether it would be better for
   this to check mutability or not.  On the one hand, having the
   lowest level do that check would seem intuitively good.  On the
   other hand, we'll need a way to delete even immutable nodes someday
   -- for example, someone accidentally commits NDA-protected data to
   a public repository and wants to remove it.  Thoughts?  */
svn_error_t *svn_fs__delete_nodes_entry (svn_fs_t *fs,
                                         const svn_fs_id_t *id,
                                         trail_t *trail);


/* Set *SUCCESSOR_P to the ID of an immediate successor to node
   revision ID in FS that does not exist yet, as part of TRAIL.
   Allocate *SUCCESSOR_P in TRAIL->pool.

   If ID is the youngest revision of its node, then the successor is
   simply ID with its rightmost revision number increased; otherwise,
   the successor is a new branch from ID.  */
svn_error_t *svn_fs__new_successor_id (svn_fs_id_t **successor_p,
                                       svn_fs_t *fs,
                                       const svn_fs_id_t *id,
                                       trail_t *trail);


/* Set *SKEL_P to the NODE-REVISION skel for the node ID in FS, as
   part of TRAIL.  Allocate the skel, and do any other temporary
   allocation, in TRAIL->pool.

   This function guarantees that SKEL is a well-formed NODE-REVISION
   skel.  */
svn_error_t *svn_fs__get_node_revision (skel_t **skel_p,
                                        svn_fs_t *fs,
                                        const svn_fs_id_t *id,
                                        trail_t *trail);


/* Store SKEL as the NODE-REVISION skel for the node revision whose id
   is ID in FS, as part of TRAIL.  Do any necessary temporary
   allocation in TRAIL->pool.

   This function checks that SKEL is a well-formed NODE-REVISION skel.

   After this call, the node table manager assumes that NODE's
   contents will change frequently.  */
svn_error_t *svn_fs__put_node_revision (svn_fs_t *fs,
                                        const svn_fs_id_t *id,
                                        skel_t *skel,
                                        trail_t *trail);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_NODES_TABLE_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
