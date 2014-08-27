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

#include "private/svn_fs_fs_private.h"

#include "fs.h"

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

/* Add a new mapping, ITEM_INDEX to the OFFSET, to log-to-phys index file
 * in PROTO_INDEX.  Please note that mappings may be added in any order
 * but duplicate entries for the same ITEM_INDEX are not supported.
 * Not all possible index values need to be used.  OFFSET may be -1 to
 * mark 'invalid' item indexes but that is already implied for all item
 * indexes not explicitly given a mapping.
 * 
 * Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__l2p_proto_index_add_entry(apr_file_t *proto_index,
                                     apr_off_t offset,
                                     apr_uint64_t item_index,
                                     apr_pool_t *pool);

/* Use the proto index file stored at PROTO_FILE_NAME, construct the final
 * log-to-phys index and append it to INDEX_FILE.  The first revision will
 * be REVISION, entries to the next revision will be assigned to REVISION+1
 * and so forth.  Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__l2p_index_append(svn_fs_t *fs,
                            apr_file_t *index_file,
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

/* Set *NEXT_OFFSET to the first offset behind the last entry in the
 * phys-to-log proto index file PROTO_INDEX.  This will be 0 for empty
 * index files.  Use POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__p2l_proto_index_next_offset(apr_off_t *next_offset,
                                       apr_file_t *proto_index,
                                       apr_pool_t *pool);

/* Use the proto index file stored at PROTO_FILE_NAME, construct the final
 * phys-to-log index and append it to INDEX_FILE.  Entries without a valid
 * revision will be assigned to the REVISION given here.
 * Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__p2l_index_append(svn_fs_t *fs,
                            apr_file_t *index_file,
                            const char *proto_file_name,
                            svn_revnum_t revision,
                            apr_pool_t *pool);

/* Use the phys-to-log mapping files in FS to return the entry for the
 * item starting at global OFFSET in the rep file containing REVISION in
 * *ENTRY.  Sets *ENTRY to NULL if no item starts at exactly that offset.
 * REV_FILE determines whether to access single rev or pack file data.
 * If that is not available anymore (neither in cache nor on disk),
 * return an error.  Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__p2l_entry_lookup(svn_fs_fs__p2l_entry_t **entry,
                            svn_fs_t *fs,
                            svn_fs_fs__revision_file_t *rev_file,
                            svn_revnum_t revision,
                            apr_off_t offset,
                            apr_pool_t *pool);

/* For ITEM_INDEX within REV in FS, return the position in the respective
 * rev or pack file in *ABSOLUTE_POSITION.  If TXN_ID is not NULL, return
 * the file offset within that transaction and REV should be given as
 * SVN_INVALID_REVNUM in that case.
 *
 * REV_FILE determines whether to access single rev or pack file data.
 * If that is not available anymore (neither in cache nor on disk), re-open
 * the rev / pack file and retry to open the index file.  For anything but
 * committed log addressed revisions, REV_FILE may be NULL.
 * Use POOL for allocations.
 */
svn_error_t *
svn_fs_fs__item_offset(apr_off_t *absolute_position,
                       svn_fs_t *fs,
                       svn_fs_fs__revision_file_t *rev_file,
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

/* Serialization and caching interface
 */

/* We use this key type to address individual pages from both index types.
 */
typedef struct svn_fs_fs__page_cache_key_t
{
  /* in l2p: this is the revision of the items being mapped
     in p2l: this is the start revision identifying the pack / rev file */
  apr_uint32_t revision;

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
