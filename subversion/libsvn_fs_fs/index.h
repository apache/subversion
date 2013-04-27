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
#define SVN_FS_FS__ITEM_TYPE_UNUSED     0  /* file section not used */
#define SVN_FS_FS__ITEM_TYPE_FILE_REP   1  /* item is a file representation */
#define SVN_FS_FS__ITEM_TYPE_DIR_REP    2  /* item is a directory rep. */
#define SVN_FS_FS__ITEM_TYPE_FILE_PROPS 3  /* item is a file property rep. */
#define SVN_FS_FS__ITEM_TYPE_DIR_PROPS  4  /* item is a directory prop rep */
#define SVN_FS_FS__ITEM_TYPE_NODEREV    5  /* item is a noderev */
#define SVN_FS_FS__ITEM_TYPE_CHANGES    6  /* item is a changed paths list */

#define SVN_FS_FS__ITEM_TYPE_ANY_REP    7  /* item is any representation.
                                              Only used in pre-format7. */

#define SVN_FS_FS__ITEM_TYPE_CHANGES_CONT  8  /* item is a changes container */
#define SVN_FS_FS__ITEM_TYPE_NODEREVS_CONT 9  /* item is a noderevs container */

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

  /* Number of items in this block / container.  Their list can be found
   * in *ITEMS.  0 for unused sections.  1 for non-container items,
   * > 1 for containers. */
  apr_uint32_t item_count;

  /* List of items in that block / container */
  svn_fs_fs__id_part_t *items;
} svn_fs_fs__p2l_entry_t;

/* Return a (deep) copy of ENTRY, allocated in POOL.
 */
svn_fs_fs__p2l_entry_t *
svn_fs_fs__p2l_entry_dup(svn_fs_fs__p2l_entry_t *entry,
                         apr_pool_t *pool);

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

/* Add a new mapping, ITEM_INDEX to the (OFFSET, SUB_ITEM) pair, to log-to-
 * phys index file in PROTO_INDEX.  Please note that mappings may be added
 * in any order but duplicate entries for the same ITEM_INDEX, SUB_ITEM
 * are not supported.  Not all possible index values need to be used.
 * (OFFSET, SUB_ITEM) may be (-1, 0) to mark 'invalid' item indexes but
 * that is already implied for all item indexes not explicitly given a
 * mapping.
 * 
 * Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__l2p_proto_index_add_entry(apr_file_t *proto_index,
                                     apr_off_t offset,
                                     apr_uint32_t sub_item,
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
 * in the rep file containing REVISION.  Return the array in *ENTRIES,
 * elements being of type svn_fs_fs__p2l_entry_t.
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

/* Use the phys-to-log mapping files in FS to return the entry for the
 * container or single item starting at global OFFSET in the rep file
 * containing REVISION in *ENTRY.  Sets *ENTRY to NULL if no item starts
 * at exactly that offset.  Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__p2l_entry_lookup(svn_fs_fs__p2l_entry_t **entry,
                            svn_fs_t *fs,
                            svn_revnum_t revision,
                            apr_off_t offset,
                            apr_pool_t *pool);

/* Use the phys-to-log mapping files in FS to return the svn_fs_fs__id_part_t
 * for the SUB_ITEM of the container starting at global OFFSET in the rep /
 * pack file containing REVISION in *ITEM.  Sets *ITEM to NULL if no element
 * starts at exactly that offset or if it contains no more than SUB_ITEM
 * sub-items.  Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__p2l_item_lookup(svn_fs_fs__id_part_t **item,
                           svn_fs_t *fs,
                           svn_revnum_t revision,
                           apr_off_t offset,
                           apr_uint32_t sub_item,
                           apr_pool_t *pool);

/* Use the log-to-phys mapping files in FS to find the packed / non-packed /
 * proto-rev file offset and container sub-item of either (REVISION,
 * ITEM_INDEX) or (TXN_ID, ITEM_INDEX).  *SUB_ITEM will be 0 for non-
 * container items.  For committed revision, TXN_ID must be NULL.  For
 * format 6 and older repositories, we simply map the revision local offset
 * given as ITEM_INDEX to the actual file offset (when packed).
 * Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__item_offset(apr_off_t *offset,
                       apr_uint32_t *sub_item,
                       svn_fs_t *fs,
                       svn_revnum_t revision,
                       const svn_fs_fs__id_part_t *txn_id,
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

/* In *OFFSET, return the first OFFSET in the pack / rev file containing
 * REVISION in FS not covered by the log-to-phys index.
 * Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__p2l_get_max_offset(apr_off_t *offset,
                              svn_fs_t *fs,
                              svn_revnum_t revision,
                              apr_pool_t *pool);

/* Serialization and caching interface
 */

/* We use this key type to address individual pages from both index types.
 */
typedef struct svn_fs_fs__page_cache_key_t
{
  /* in l2p: this is the revision of the items being mapped
     in p2l: this is the start revision identifying the pack / rev file */
  svn_revnum_t revision;

  /* if TRUE, this is the index to a pack file
   */
  svn_boolean_t is_packed;

  /* in l2p: page number within the revision
   * in p2l: page number with the rev / pack file
   */
  apr_uint64_t page;
} svn_fs_fs__page_cache_key_t;

/*
 * Implements svn_cache__serialize_func_t for l2p_header_t objects.
 */
svn_error_t *
svn_fs_fs__serialize_l2p_header(void **data,
                                apr_size_t *data_len,
                                void *in,
                                apr_pool_t *pool);

/*
 * Implements svn_cache__deserialize_func_t for l2p_header_t objects.
 */
svn_error_t *
svn_fs_fs__deserialize_l2p_header(void **out,
                                  void *data,
                                  apr_size_t data_len,
                                  apr_pool_t *pool);

/*
 * Implements svn_cache__serialize_func_t for l2p_page_t objects.
 */
svn_error_t *
svn_fs_fs__serialize_l2p_page(void **data,
                              apr_size_t *data_len,
                              void *in,
                              apr_pool_t *pool);

/*
 * Implements svn_cache__deserialize_func_t for l2p_page_t objects.
 */
svn_error_t *
svn_fs_fs__deserialize_l2p_page(void **out,
                                void *data,
                                apr_size_t data_len,
                                apr_pool_t *pool);

/*
 * Implements svn_cache__serialize_func_t for p2l_header_t objects.
 */
svn_error_t *
svn_fs_fs__serialize_p2l_header(void **data,
                                apr_size_t *data_len,
                                void *in,
                                apr_pool_t *pool);

/*
 * Implements svn_cache__deserialize_func_t for p2l_header_t objects.
 */
svn_error_t *
svn_fs_fs__deserialize_p2l_header(void **out,
                                  void *data,
                                  apr_size_t data_len,
                                  apr_pool_t *pool);

/*
 * Implements svn_cache__serialize_func_t for apr_array_header_t objects
 * with elements of type svn_fs_fs__p2l_entry_t.
 */
svn_error_t *
svn_fs_fs__serialize_p2l_page(void **data,
                              apr_size_t *data_len,
                              void *in,
                              apr_pool_t *pool);

/*
 * Implements svn_cache__deserialize_func_t for apr_array_header_t objects
 * with elements of type svn_fs_fs__p2l_entry_t.
 */
svn_error_t *
svn_fs_fs__deserialize_p2l_page(void **out,
                                void *data,
                                apr_size_t data_len,
                                apr_pool_t *pool);

#endif
