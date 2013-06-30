/* index.c indexing support for FSFS support
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

#include <assert.h>

#include "svn_io.h"
#include "svn_pools.h"
#include "svn_sorts.h"

#include "index.h"
#include "util.h"
#include "pack.h"

#include "private/svn_subr_private.h"
#include "private/svn_temp_serializer.h"

#include "svn_private_config.h"
#include "temp_serializer.h"

#include "../libsvn_fs/fs-loader.h"

/* maximum length of a uint64 in an 7/8b encoding */
#define ENCODED_INT_LENGTH 10


svn_error_t *
svn_fs_fs__item_offset(apr_off_t *offset,
                       apr_uint32_t *sub_item,
                       svn_fs_t *fs,
                       svn_revnum_t revision,
                       const svn_fs_fs__id_part_t *txn_id,
                       apr_uint64_t item_index,
                       apr_pool_t *pool)
{
  /* older fsfs formats don't have containers */
  *sub_item = 0;

  /* older fsfs formats use the manifest file to re-map the offsets */
  *offset = (apr_off_t)item_index;
  if (!txn_id && is_packed_rev(fs, revision))
    {
      apr_off_t rev_offset;

      SVN_ERR(svn_fs_fs__get_packed_offset(&rev_offset, fs, revision,
                                            pool));
      *offset += rev_offset;
    }

  return SVN_NO_ERROR;
}
