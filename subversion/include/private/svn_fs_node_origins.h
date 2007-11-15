/*
 * svn_fs_node_origin.h: Declarations for APIs of libsvn_fs_util to
 * be consumed by only fs_* libs; access to the node origin index.
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

#ifndef SVN_FS_NODE_ORIGIN_H
#define SVN_FS_NODE_ORIGIN_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* The node origin table is a cache of immutable data to assist
 * svn_fs_node_origin_rev.  Because both FS backends implement
 * svn_fs_id_t as a structure where objects on the same line of
 * history have a "Node ID" in common, we can cache responses to
 * svn_fs_node_origin_rev based on the "Node ID". */


/* Update the node origin index for FS based on the hash
   NODE_ORIGIN_FOR_PATHS, which maps from const char * "Node IDs" to
   const svn_fs_id_t * node-rev-ids.  Returns an error if any cache
   entry exists with a different value; pre-existing entries with the
   same value are ignored.  Use POOL for any temporary allocations.
 */
svn_error_t *
svn_fs__set_node_origins(svn_fs_t *fs,
                         apr_hash_t *node_origins,
                         apr_pool_t *pool);

/* Set *ORIGIN_ID to the node revision ID from which the history of
   all nodes in FS whose "Node ID" is NODE_ID springs, as determined
   by a look in the index.  Use POOL for allocations.

   If there is no entry for NODE_ID in the cache, return
   SVN_ERR_FS_NO_SUCH_NODE_ORIGIN. */
svn_error_t *
svn_fs__get_node_origin(const svn_fs_id_t **origin_id,
                        svn_fs_t *fs,
                        const char *node_id,
                        apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_FS_NODE_ORIGIN_H */
