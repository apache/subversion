/* revprops.h --- everything needed to handle revprops in FSX
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

#ifndef SVN_LIBSVN_FS_X_REVPROPS_H
#define SVN_LIBSVN_FS_X_REVPROPS_H

#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Auto-create / replace the revprop generation file in FS with its
 * initial contents.  In any case, FS will not hold an open handle to
 * it after this function succeeds.
 *
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_fs_x__reset_revprop_generation_file(svn_fs_t *fs,
                                        apr_pool_t *scratch_pool);

/* Read the revprops for revision REV in FS and return them in *PROPLIST_P.
 * If BYPASS_CACHE is set, don't consult the disks but always read from disk.
 *
 * Allocate the *PROPLIST_P in RESULT_POOL and use SCRATCH_POOL for temporary
 * allocations.
 */
svn_error_t *
svn_fs_x__get_revision_proplist(apr_hash_t **proplist_p,
                                svn_fs_t *fs,
                                svn_revnum_t rev,
                                svn_boolean_t bypass_cache,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool);

/* Set the revision property list of revision REV in filesystem FS to
   PROPLIST.  Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_fs_x__set_revision_proplist(svn_fs_t *fs,
                                svn_revnum_t rev,
                                apr_hash_t *proplist,
                                apr_pool_t *scratch_pool);


/* Return TRUE, if for REVISION in FS, we can find the revprop pack file.
 * Use SCRATCH_POOL for temporary allocations.
 * Set *MISSING, if the reason is a missing manifest or pack file.
 */
svn_boolean_t
svn_fs_x__packed_revprop_available(svn_boolean_t *missing,
                                   svn_fs_t *fs,
                                   svn_revnum_t revision,
                                   apr_pool_t *scratch_pool);


/****** Packing FSX shards *********/

/* For the revprop SHARD at SHARD_PATH with exactly MAX_FILES_PER_DIR
 * revprop files in it, create a packed shared at PACK_FILE_DIR in
 * filesystem FS.
 *
 * COMPRESSION_LEVEL defines how well the resulting pack file shall be
 * compressed or whether is shall be compressed at all.  Individual pack
 * file containing more than one revision will be limited to a size of
 * MAX_PACK_SIZE bytes before compression.
 *
 * CANCEL_FUNC and CANCEL_BATON are used in the usual way.  Temporary
 * allocations are done in SCRATCH_POOL.
 */
svn_error_t *
svn_fs_x__pack_revprops_shard(svn_fs_t *fs,
                              const char *pack_file_dir,
                              const char *shard_path,
                              apr_int64_t shard,
                              int max_files_per_dir,
                              apr_off_t max_pack_size,
                              int compression_level,
                              svn_cancel_func_t cancel_func,
                              void *cancel_baton,
                              apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_X_REVPROPS_H */
