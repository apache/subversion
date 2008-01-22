/* node-origins-table.h : internal interface to ops on `node-origins' table
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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

#ifndef SVN_LIBSVN_FS_NODE_ORIGINS_TABLE_H
#define SVN_LIBSVN_FS_NODE_ORIGINS_TABLE_H

#include "svn_fs.h"
#include "svn_error.h"
#include "../trail.h"
#include "../fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Open a `node-origins' table in ENV.  If CREATE is non-zero, create
   one if it doesn't exist.  Set *NODE_ORIGINS_P to the new table.
   Return a Berkeley DB error code.  */
int svn_fs_bdb__open_node_origins_table(DB **node_origins_p,
                                        DB_ENV *env,
                                        svn_boolean_t create);

/* Set *ORIGIN_ID to the node revision ID from which the history of
   all nodes in FS whose node ID is NODE_ID springs, as determined by
   a look in the `node-origins' table.  Do this as part of TRAIL.  Use
   POOL for allocations.

   If no such node revision ID is stored for NODE_ID, return
   SVN_ERR_FS_NO_SUCH_NODE_ORIGIN.  */
svn_error_t *svn_fs_bdb__get_node_origin(const svn_fs_id_t **origin_id,
                                         svn_fs_t *fs,
                                         const char *node_id,
                                         trail_t *trail,
                                         apr_pool_t *pool);

/* Store in the `node-origins' table a mapping of NODE_ID to original
   node revision ID ORIGIN_ID for FS.  Do this as part of TRAIL.  Use
   POOL for temporary allocations.  */
svn_error_t *svn_fs_bdb__set_node_origin(svn_fs_t *fs,
                                         const char *node_id,
                                         const svn_fs_id_t *origin_id,
                                         trail_t *trail,
                                         apr_pool_t *pool);

/* Delete from the `node-origins' table the record for NODE_ID in FS.
   Do this as part of TRAIL.  Use POOL for temporary allocations.  */
svn_error_t *svn_fs_bdb__delete_node_origin(svn_fs_t *fs,
                                            const char *node_id,
                                            trail_t *trail,
                                            apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_NODE_ORIGINS_TABLE_H */
