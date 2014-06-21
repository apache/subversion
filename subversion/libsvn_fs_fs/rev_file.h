/* rev_file.h --- revision file and index access data structure
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

#ifndef SVN_LIBSVN_FS__REV_FILE_H
#define SVN_LIBSVN_FS__REV_FILE_H

#include "svn_fs.h"
#include "id.h"

/* In format 7, index files must be read in sync with the respective
 * revision / pack file.  I.e. we must use packed index files for packed
 * rev files and unpacked ones for non-packed rev files.  So, the whole
 * point is to open them with matching "is packed" setting in case some
 * background pack process was run.
 */

/* Opaque index stream type.
 */
typedef struct svn_fs_fs__packed_number_stream_t
  svn_fs_fs__packed_number_stream_t;

/* All files and associated properties for START_REVISION.
 */
typedef struct svn_fs_fs__revision_file_t
{
  /* first (potentially only) revision in the rev / pack file.
   * SVN_INVALID_REVNUM for txn proto-rev files. */
  svn_revnum_t start_revision;

  /* the revision was packed when the first file / stream got opened */
  svn_boolean_t is_packed;

  /* rev / pack file or NULL if not opened, yet */
  apr_file_t *file;

  /* stream based on FILE and not NULL exactly when FILE is not NULL */
  svn_stream_t *stream;

  /* the opened P2L index or NULL.  Always NULL for txns. */
  svn_fs_fs__packed_number_stream_t *p2l_stream;

  /* the opened L2P index or NULL.  Always NULL for txns. */
  svn_fs_fs__packed_number_stream_t *l2p_stream;

  /* pool containing this object */
  apr_pool_t *pool;
} svn_fs_fs__revision_file_t;

/* Open the correct revision file for REV.  If the filesystem FS has
 * been packed, *FILE will be set to the packed file; otherwise, set *FILE
 * to the revision file for REV.  Return SVN_ERR_FS_NO_SUCH_REVISION if the
 * file doesn't exist.  Use POOL for allocations. */
svn_error_t *
svn_fs_fs__open_pack_or_rev_file(svn_fs_fs__revision_file_t **file,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 apr_pool_t *pool);

/* Close previous files as well as streams in FILE (if open) and open the
 * rev / pack file for REVISION in FS.  This is useful when a pack operation
 * made the current files outdated or no longer available and the caller
 * wants to keep the same revision file data structure.
 */
svn_error_t *
svn_fs_fs__reopen_revision_file(svn_fs_fs__revision_file_t *file,
                                svn_fs_t *fs,
                                svn_revnum_t revision);

/* Open the proto-rev file of transaction TXN_ID in FS and return it in *FILE.
 * Use POOL for allocations. */
svn_error_t *
svn_fs_fs__open_proto_rev_file(svn_fs_fs__revision_file_t **file,
                               svn_fs_t *fs,
                               const svn_fs_fs__id_part_t *txn_id,
                               apr_pool_t *pool);

/* Close all files and streams in FILE.
 */
svn_error_t *
svn_fs_fs__close_revision_file(svn_fs_fs__revision_file_t *file);

#endif
