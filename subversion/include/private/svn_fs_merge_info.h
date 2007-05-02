/*
 * svn_fs_merge_info.h: Declarations for the APIs of libsvn_fs_util to
 * be consumed by only fs_* libs.
 *
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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

#ifndef SVN_FS_MERGE_INFO_H
#define SVN_FS_MERGE_INFO_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* The name of the sqlite merge info database. */
#define SVN_FS_MERGE_INFO__DB_NAME "mergeinfo.db"

/* Create the merge info index under PATH.  Use POOL for any temporary
   allocations. */
svn_error_t *
svn_fs_merge_info__create_index(const char *path, apr_pool_t *pool);

/* Update the merge info index according to the changes made in
   transaction TXN for revision NEW_REV.  MERGEINFO is the merge
   information for the transaction, or NULL if there was no merge info
   recorded for that transaction.  Use POOL for any temporary
   allocations.

   NOTE: Even if there is no merge info, this function should be
   called to make sure there is no stray merge info for this revision
   left from a previous failed commit.  */
svn_error_t *
svn_fs_merge_info__update_index(svn_fs_txn_t *txn, 
                                svn_revnum_t new_rev,
                                apr_hash_t *mergeinfo, 
                                apr_pool_t *pool);

/* Get the merge info for the set of PATHS (an array of
   absolute-in-the-fs paths) under ROOT and return it in *MERGEINFO,
   mapping char * paths to char * strings with mergeinfo, allocated in
   POOL.  If INCLUDE_PARENTS is TRUE elide for mergeinfo.  If a path
   has no mergeinfo, just return no info for that path.  Return an
   error if the mergeinfo store does not exist or doesn't use the
   'mergeinfo' schema.  */
svn_error_t *
svn_fs_merge_info__get_merge_info(apr_hash_t **mergeinfo,
                                  svn_fs_root_t *root,
                                  const apr_array_header_t *paths,
                                  svn_boolean_t include_parents,
                                  apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_FS_MERGE_INFO_H */
