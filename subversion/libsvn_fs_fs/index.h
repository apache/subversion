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

#endif
