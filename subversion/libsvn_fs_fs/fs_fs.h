/* fs_fs.h : interface to the native filesystem layer
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_FS__FS_FS_H
#define SVN_LIBSVN_FS__FS_FS_H

#include "fs.h"

/* Open the fsfs filesystem pointed to by PATH and associate it with
   filesystem object FS.  Use POOL for temporary allocations.

   ### Some parts of *FS must have been initialized beforehand; some parts
       (including FS->path) are initialized by this function. */
svn_error_t *svn_fs_fs__open(svn_fs_t *fs,
                             const char *path,
                             apr_pool_t *pool);

/* Upgrade the fsfs filesystem FS.  Use POOL for temporary allocations. */
svn_error_t *svn_fs_fs__upgrade(svn_fs_t *fs,
                                apr_pool_t *pool);

/* Verify the fsfs filesystem FS.  Use POOL for temporary allocations. */
svn_error_t *svn_fs_fs__verify(svn_fs_t *fs,
                               svn_cancel_func_t cancel_func,
                               void *cancel_baton,
                               svn_revnum_t start,
                               svn_revnum_t end,
                               apr_pool_t *pool);

/* Set *YOUNGEST to the youngest revision in filesystem FS.  Do any
   temporary allocation in POOL. */
svn_error_t *svn_fs_fs__youngest_rev(svn_revnum_t *youngest,
                                     svn_fs_t *fs,
                                     apr_pool_t *pool);

/* For revision REV in fileysystem FS, open the revision (or packed rev)
   file and seek to the start of the revision.  Return it in *FILE, and
   use POOL for allocations. */
svn_error_t *
svn_fs_fs__open_pack_or_rev_file(apr_file_t **file,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 apr_pool_t *pool);

/* Assert that revision REV exists in fileysystem FS.  If it doesn't, return
   an error.  Use POOL for allocations. */
svn_error_t *
svn_fs_fs__ensure_revision_exists(svn_revnum_t rev,
                                  svn_fs_t *fs,
                                  apr_pool_t *pool);

/* Return an error iff REV does not exist in FS. */
svn_error_t *
svn_fs_fs__revision_exists(svn_revnum_t rev,
                           svn_fs_t *fs,
                           apr_pool_t *pool);

/* Set *PROPLIST to be an apr_hash_t containing the property list of
   revision REV as seen in filesystem FS.  Use POOL for temporary
   allocations. */
svn_error_t *svn_fs_fs__revision_proplist(apr_hash_t **proplist,
                                          svn_fs_t *fs,
                                          svn_revnum_t rev,
                                          apr_pool_t *pool);

/* Set *LENGTH to the be fulltext length of the node revision
   specified by NODEREV.  Use POOL for temporary allocations. */
svn_error_t *svn_fs_fs__file_length(svn_filesize_t *length,
                                    node_revision_t *noderev,
                                    apr_pool_t *pool);

/* Return TRUE if the representation keys in A and B both point to the
   same representation, else return FALSE. */
svn_boolean_t svn_fs_fs__noderev_same_rep_key(representation_t *a,
                                              representation_t *b);


/* Return a copy of the representation REP allocated from POOL. */
representation_t *svn_fs_fs__rep_copy(representation_t *rep,
                                      apr_pool_t *pool);


/* Return the recorded checksum of type KIND for the text representation
   of NODREV into CHECKSUM, allocating from POOL.  If no stored checksum is
   available, put all NULL into CHECKSUM. */
svn_error_t *svn_fs_fs__file_checksum(svn_checksum_t **checksum,
                                      node_revision_t *noderev,
                                      svn_checksum_kind_t kind,
                                      apr_pool_t *pool);

/* Return whether or not the given FS supports mergeinfo metadata. */
svn_boolean_t svn_fs_fs__fs_supports_mergeinfo(svn_fs_t *fs);

/* Create a fs_fs fileysystem referenced by FS at path PATH.  Get any
   temporary allocations from POOL.

   ### Some parts of *FS must have been initialized beforehand; some parts
       (including FS->path) are initialized by this function. */
svn_error_t *svn_fs_fs__create(svn_fs_t *fs,
                               const char *path,
                               apr_pool_t *pool);

/* Set the uuid of repository FS to UUID, if UUID is not NULL;
   otherwise, set the uuid of FS to a newly generated UUID.  Perform
   temporary allocations in POOL. */
svn_error_t *svn_fs_fs__set_uuid(svn_fs_t *fs,
                                 const char *uuid,
                                 apr_pool_t *pool);

/* Set *PATH to the path of REV in FS, whether in a pack file or not.
   Allocate *PATH in POOL.

   Note: If the caller does not have the write lock on FS, then the path is
   not guaranteed to be correct or to remain correct after the function
   returns, because the revision might become packed before or after this
   call.  If a file exists at that path, then it is correct; if not, then
   the caller should call update_min_unpacked_rev() and re-try once. */
svn_error_t *
svn_fs_fs__path_rev_absolute(const char **path,
                             svn_fs_t *fs,
                             svn_revnum_t rev,
                             apr_pool_t *pool);

/* Return the path to the 'current' file in FS.
   Perform allocation in POOL. */
const char *
svn_fs_fs__path_current(svn_fs_t *fs, apr_pool_t *pool);

/* Read the format number and maximum number of files per directory
   from PATH and return them in *PFORMAT and *MAX_FILES_PER_DIR
   respectively.

   *MAX_FILES_PER_DIR is obtained from the 'layout' format option, and
   will be set to zero if a linear scheme should be used.

   Use POOL for temporary allocation. */
svn_error_t *
svn_fs_fs__write_format(svn_fs_t *fs, apr_pool_t *pool);

/* Find the value of the property named PROPNAME in transaction TXN.
   Return the contents in *VALUE_P.  The contents will be allocated
   from POOL. */
svn_error_t *svn_fs_fs__revision_prop(svn_string_t **value_p, svn_fs_t *fs,
                                      svn_revnum_t rev,
                                      const char *propname,
                                      apr_pool_t *pool);

/* Change, add, or delete a property on a revision REV in filesystem
   FS.  NAME gives the name of the property, and value, if non-NULL,
   gives the new contents of the property.  If value is NULL, then the
   property will be deleted.  If OLD_VALUE_P is not NULL, do nothing unless the
   preexisting value is *OLD_VALUE_P.  Do any temporary allocation in POOL.  */
svn_error_t *svn_fs_fs__change_rev_prop(svn_fs_t *fs, svn_revnum_t rev,
                                        const char *name,
                                        const svn_string_t *const *old_value_p,
                                        const svn_string_t *value,
                                        apr_pool_t *pool);

/* If directory PATH does not exist, create it and give it the same
   permissions as FS_PATH.*/
svn_error_t *svn_fs_fs__ensure_dir_exists(const char *path,
                                          const char *fs_path,
                                          apr_pool_t *pool);

/* Update the node origin index for FS, recording the mapping from
   NODE_ID to NODE_REV_ID.  Use POOL for any temporary allocations.

   Because this is just an "optional" cache, this function does not
   return an error if the underlying storage is readonly; it still
   returns an error for other error conditions.
 */
svn_error_t *
svn_fs_fs__set_node_origin(svn_fs_t *fs,
                           const char *node_id,
                           const svn_fs_id_t *node_rev_id,
                           apr_pool_t *pool);

/* Set *ORIGIN_ID to the node revision ID from which the history of
   all nodes in FS whose "Node ID" is NODE_ID springs, as determined
   by a look in the index.  ORIGIN_ID needs to be parsed in an
   FS-backend-specific way.  Use POOL for allocations.

   If there is no entry for NODE_ID in the cache, return NULL
   in *ORIGIN_ID. */
svn_error_t *
svn_fs_fs__get_node_origin(const svn_fs_id_t **origin_id,
                           svn_fs_t *fs,
                           const char *node_id,
                           apr_pool_t *pool);


/* Initialize all session-local caches in FS according to the global
   cache settings. Use POOL for allocations.

   Please note that it is permissible for this function to set some
   or all of these caches to NULL, regardless of any setting. */
svn_error_t *
svn_fs_fs__initialize_caches(svn_fs_t *fs, apr_pool_t *pool);

/* Initialize all transaction-local caches in FS according to the global
   cache settings and make TXN_ID part of their key space. Use POOL for
   allocations.

   Please note that it is permissible for this function to set some or all
   of these caches to NULL, regardless of any setting. */
svn_error_t *
svn_fs_fs__initialize_txn_caches(svn_fs_t *fs,
                                 const char *txn_id,
                                 apr_pool_t *pool);

/* Resets the svn_cache__t structures local to the current transaction in FS.
   Calling it more than once per txn or from outside any txn is allowed. */
void
svn_fs_fs__reset_txn_caches(svn_fs_t *fs);

#endif
