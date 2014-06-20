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

#include "svn_private_config.h"

#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_temp_serializer.h"

#include "index.h"
#include "pack.h"
#include "temp_serializer.h"
#include "util.h"
#include "fs_fs.h"

#include "../libsvn_fs/fs-loader.h"

svn_error_t *
svn_fs_fs__item_offset(apr_off_t *absolute_position,
                       svn_fs_t *fs,
                       svn_fs_fs__revision_file_t *rev_file,
                       svn_revnum_t revision,
                       const svn_fs_fs__id_part_t *txn_id,
                       apr_uint64_t item_index,
                       apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  if (txn_id)
    {
        /* for data in txns, item_index *is* the offset */
        *absolute_position = item_index;
    }
  else if (rev_file->is_packed)
    {
      /* pack file with physical addressing */
      apr_off_t rev_offset;
      SVN_ERR(svn_fs_fs__get_packed_offset(&rev_offset, fs, revision, pool));
      *absolute_position = rev_offset + item_index;
    }
  else
    {
      /* for non-packed revs with physical addressing,
         item_index *is* the offset */
      *absolute_position = item_index;
    }

  return svn_error_trace(err);
}
