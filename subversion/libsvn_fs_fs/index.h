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
#include "rev_file.h"

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

  /* modified FNV-1a checksum.  0 if unknown checksum */
  apr_uint32_t fnv1_checksum;

  /* item in that block */
  svn_fs_fs__id_part_t item;
} svn_fs_fs__p2l_entry_t;

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

#endif
