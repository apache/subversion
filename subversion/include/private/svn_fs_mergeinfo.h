/*
 * svn_fs_mergeinfo.h: Declarations for the APIs of libsvn_fs_util to
 * be consumed by only fs_* libs.
 *
 * ====================================================================
 * Copyright (c) 2006-2007 CollabNet.  All rights reserved.
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

#ifndef SVN_FS_MERGEINFO_H
#define SVN_FS_MERGEINFO_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* The functions declared here are the API between the two FS
   libraries and the mergeinfo index.  Currently, the index is stored
   in a sqlite database (which is created using a function in
   private/svn_fs_sqlite.h); to implement a different backend, just
   change these three functions.
 */


/* Update the mergeinfo index according to the changes made in
   transaction TXN for revision NEW_REV.  MERGEINFO_FOR_PATHS is the
   mergeinfo for each path changed in the transaction (a mapping of
   const char * -> svn_string_t *), or NULL if there was no mergeinfo
   recorded for that transaction.  Use POOL for any temporary allocations.

   NOTE: Even if there is no mergeinfo, this function should be
   called to make sure there is no stray mergeinfo for this revision
   left from a previous failed commit.  */
svn_error_t *
svn_fs_mergeinfo__update_index(svn_fs_txn_t *txn,
                               svn_revnum_t new_rev,
                               apr_hash_t *mergeinfo_for_paths,
                               apr_pool_t *pool);

/* Get the mergeinfo for the set of PATHS (an array of
   absolute-in-the-fs paths) under ROOT and return it in *MERGEINFO,
   mapping char * paths to char * strings with mergeinfo, allocated in
   POOL.  INHERIT indicates whether to get explicit, explicit or inherited,
   or only inherited mergeinfo for PATHS.  If a path has no mergeinfo,
   just return no info for that path.  Return an error if the mergeinfo
   store does not exist or doesn't use the 'mergeinfo' schema.  */
svn_error_t *
svn_fs_mergeinfo__get_mergeinfo(apr_hash_t **mergeinfo,
                                 svn_fs_root_t *root,
                                 const apr_array_header_t *paths,
                                 svn_mergeinfo_inheritance_t inherit,
                                 apr_pool_t *pool);

/* Get the combined mergeinfo for the tree under each one of PATHS
   (an array of absolute-in-the-fs paths) under ROOT, and return it
   in *MERGEINFO, mapping char * paths to mergeinfo hashs.  The resulting
   mergeinfo also includes elided mergeinfo for each one of PATHS.  This
   function conforms to the get_mergeinfo_for_tree() interface.  */
svn_error_t *
svn_fs_mergeinfo__get_mergeinfo_for_tree(
  apr_hash_t **mergeinfo,
  svn_fs_root_t *root,
  const apr_array_header_t *paths,
  svn_fs_mergeinfo_filter_func_t filter_func,
  void *filter_func_baton,
  apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_FS_MERGEINFO_H */
