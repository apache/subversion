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

svn_error_t *
svn_fs_fs__l2p_proto_index_open(apr_file_t **proto_index,
                                const char *file_name,
                                apr_pool_t *pool);

svn_error_t *
svn_fs_fs__l2p_proto_index_add_revision(apr_file_t *proto_index,
                                        apr_pool_t *pool);

svn_error_t *
svn_fs_fs__l2p_proto_index_add_entry(apr_file_t *proto_index,
                                     apr_off_t offset,
                                     apr_uint64_t item_index,
                                     apr_pool_t *pool);

svn_error_t *
svn_fs_fs__l2p_index_create(apr_file_t *proto_index,
                            svn_fs_t *fs,
                            svn_revnum_t revision,
                            apr_pool_t *pool);

svn_error_t *
svn_fs_fs__l2p_index_lookup(apr_off_t *offset,
                            svn_fs_t *fs,
                            svn_revnum_t revision,
                            apr_uint64_t item_index,
                            apr_pool_t *pool);

typedef struct svn_fs_fs__p2l_entry_t
{
  apr_off_t offset;
  apr_off_t size;
  unsigned type;
  svn_revnum_t revision;
  apr_uint64_t item_index;
} svn_fs_fs__p2l_entry_t;

svn_error_t *
svn_fs_fs__p2l_proto_index_open(apr_file_t **proto_index,
                                const char *file_name,
                                apr_pool_t *pool);

svn_error_t *
svn_fs_fs__p2l_proto_index_add_entry(apr_file_t *proto_index,
                                     svn_fs_fs__p2l_entry_t *entry,
                                     apr_pool_t *pool);

svn_error_t *
svn_fs_fs__p2l_index_create(apr_file_t *proto_index,
                            svn_fs_t *fs,
                            svn_revnum_t revision,
                            apr_pool_t *pool);

svn_error_t *
svn_fs_fs__p2l_index_lookup(apr_array_header_t **entries,
                            svn_fs_t *fs,
                            svn_revnum_t revision,
                            apr_off_t offset,
                            apr_pool_t *pool);

svn_error_t *
svn_fs_fs__item_offset(apr_off_t *offset,
                       svn_fs_t *fs,
                       svn_revnum_t revision,
                       apr_uint64_t item_index,
                       apr_pool_t *pool);

#endif
