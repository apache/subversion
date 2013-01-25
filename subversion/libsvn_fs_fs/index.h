/* index.h : interface to FSFS indexing functionality
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

#ifndef SVN_LIBSVN_FS__INDEX_H
#define SVN_LIBSVN_FS__INDEX_H

#include "fs.h"

/* Per-defined item index values.  They are used to identify empty or
 * mandatory items.
 */
#define SVN_FS_FS__ITEM_INDEX_UNUSED     0  /* invalid / reserved value */
#define SVN_FS_FS__ITEM_INDEX_CHANGES    1  /* list of changed paths */
#define SVN_FS_FS__ITEM_INDEX_ROOT_NODE  2  /* the root noderev */
#define SVN_FS_FS__ITEM_INDEX_FIRST_USER 3  /* first noderev to be freely
                                               assigned */

/* Data / item types as stored in the phys-to-log index.
 */
#define SVN_FS_FS__ITEM_TYPE_UNUSED  0  /* file section not used */
#define SVN_FS_FS__ITEM_TYPE_REP     1  /* item is a representation */
#define SVN_FS_FS__ITEM_TYPE_NODEREV 2  /* item is a noderev */
#define SVN_FS_FS__ITEM_TYPE_CHANGES 3  /* item is a changed paths list */

/* (user visible) entry in the phys-to-log index.  It describes a section
 * of some packed / non-packed rev file as containing a specific item.
 * There must be no overlapping / conflicting entries.
 */
typedef struct svn_fs_fs__p2l_entry_t
{
  /* offset of the first byte that belongs to the item */
  apr_off_t offset;
  
  /* length of the item in bytes */
  apr_off_t size;

  /* type of the item (see SVN_FS_FS__ITEM_TYPE_*) defines */
  unsigned type;

  /* revision that the item belongs to */
  svn_revnum_t revision;

  /* logical index of the item within that revision */
  apr_uint64_t item_index;
} svn_fs_fs__p2l_entry_t;

/* Open / create a log-to-phys index file with the full file path name
 * FILE_NAME.  Return the open file in *PROTO_INDEX and use POOL for
 * allocations.
 */
svn_error_t *
svn_fs_fs__l2p_proto_index_open(apr_file_t **proto_index,
                                const char *file_name,
                                apr_pool_t *pool);

/* Call this function before adding entries for the next revision to the
 * log-to-phys index file in PROTO_INDEX.  Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__l2p_proto_index_add_revision(apr_file_t *proto_index,
                                        apr_pool_t *pool);

/* Add a new mapping, ITEM_INDEX to OFFSET, to log-to-phys index file in
 * PROTO_INDEX.  Please note that mappings may be added in any order but
 * duplicate entries for the same ITEM_INDEX are not supported.  Not all
 * possible index values need to be used.  OFFSET may be -1 to mark
 * 'invalid' item indexes but that is already implied for all item indexes
 * not explicitly given a mapping.
 * 
 * Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__l2p_proto_index_add_entry(apr_file_t *proto_index,
                                     apr_off_t offset,
                                     apr_uint64_t item_index,
                                     apr_pool_t *pool);

/* Use the proto index file stored at PROTO_FILE_NAME and construct the
 * final log-to-phys index file at FILE_NAME.  The first revision will
 * be REVISION, entries to the next revision will be assigned to REVISION+1
 * and so forth.  Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__l2p_index_create(svn_fs_t *fs,
                            const char *file_name,
                            const char *proto_file_name,
                            svn_revnum_t revision,
                            apr_pool_t *pool);

/* Open / create a phys-to-log index file with the full file path name
 * FILE_NAME.  Return the open file in *PROTO_INDEX and use POOL for
 * allocations.
 */
svn_error_t *
svn_fs_fs__p2l_proto_index_open(apr_file_t **proto_index,
                                const char *file_name,
                                apr_pool_t *pool);

/* Add a new mapping ENTRY to the phys-to-log index file in PROTO_INDEX.
 * The entries must be added in ascending offset order and must not leave
 * intermittent ranges uncovered.  The revision value in ENTRY may be
 * SVN_INVALID_REVISION.  Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__p2l_proto_index_add_entry(apr_file_t *proto_index,
                                     svn_fs_fs__p2l_entry_t *entry,
                                     apr_pool_t *pool);

/* Use the proto index file stored at PROTO_FILE_NAME and construct the
 * final phys-to-log index file at FILE_NAME.  Entries without a valid
 * revision will be assigned to the REVISION given here.
 * Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__p2l_index_create(svn_fs_t *fs,
                            const char *file_name,
                            const char *proto_file_name,
                            svn_revnum_t revision,
                            apr_pool_t *pool);

/* Use the phys-to-log mapping files in FS to build a list of entries
 * that (partly) share in the same cluster as the item at global OFFSET
 * in the rep file containing REVISION.  Return the array in *ENTRIES.
 * Use POOL for allocations.
 *
 * Note that (only) the first and the last mapping may cross a cluster
 * boundary.
 */
svn_error_t *
svn_fs_fs__p2l_index_lookup(apr_array_header_t **entries,
                            svn_fs_t *fs,
                            svn_revnum_t revision,
                            apr_off_t offset,
                            apr_pool_t *pool);

/* Use the log-to-phys mapping files in FS to find the packed / non-packed /
 * proto-rev file offset of either (REVISION, ITEM_INDEX) or (TXN_ID,
 * ITEM_INDEX).  For committed revision, TXN_ID must be NULL.  For format 6
 * and older repositories, we simply map the revision local offset given
 * as ITEM_INDEX to the actual file offset (when packed).
 * Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__item_offset(apr_off_t *offset,
                       svn_fs_t *fs,
                       svn_revnum_t revision,
                       const char *txn_id,
                       apr_uint64_t item_index,
                       apr_pool_t *pool);

/* Use the log-to-phys indexes in FS to determine the maximum item indexes
 * assigned to revision START_REV to START_REV + COUNT - 1.  That is a
 * close upper limit to the actual number of items in the respective revs.
 * Return the results in *MAX_IDS,  allocated in POOL.
 */
svn_error_t *
svn_fs_fs__l2p_get_max_ids(apr_array_header_t **max_ids,
                           svn_fs_t *fs,
                           svn_revnum_t start_rev,
                           apr_size_t count,
                           apr_pool_t *pool);

#endif
