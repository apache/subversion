/* util.h --- utility functions for FSFS repo access
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

#ifndef SVN_LIBSVN_FS__UTIL_H
#define SVN_LIBSVN_FS__UTIL_H

#include "svn_fs.h"
#include "id.h"

/* Return TRUE is REV is packed in FS, FALSE otherwise. */
svn_boolean_t
svn_fs_fs__is_packed_rev(svn_fs_t *fs,
                         svn_revnum_t rev);

/* Return the path of the pack-related file that for revision REV in FS.
 * KIND specifies the file name base, e.g. "manifest" or "pack".
 * The result will be allocated in POOL.
 */
const char *
svn_fs_fs__path_rev_packed(svn_fs_t *fs,
                           svn_revnum_t rev,
                           const char *kind,
                           apr_pool_t *pool);

/* Return the path of the file storing the oldest non-packed revision in FS.
 * The result will be allocated in POOL.
 */
const char *
svn_fs_fs__path_min_unpacked_rev(svn_fs_t *fs,
                                 apr_pool_t *pool);

/* Set *MIN_UNPACKED_REV to the integer value read from the file returned
 * by #svn_fs_fs__path_min_unpacked_rev() for FS.
 * Use POOL for temporary allocations.
 */
svn_error_t *
svn_fs_fs__read_min_unpacked_rev(svn_revnum_t *min_unpacked_rev,
                                 svn_fs_t *fs,
                                 apr_pool_t *pool);

/* Write a file FILENAME in directory FS_PATH, containing a single line
 * with the number REVNUM in ASCII decimal.  Move the file into place
 * atomically, overwriting any existing file.
 *
 * Similar to write_current(). */
svn_error_t *
svn_fs_fs__write_revnum_file(svn_fs_t *fs,
                             svn_revnum_t revnum,
                             apr_pool_t *scratch_pool);

/* Reads a line from STREAM and converts it to a 64 bit integer to be
 * returned in *RESULT.  If we encounter eof, set *HIT_EOF and leave
 * *RESULT unchanged.  If HIT_EOF is NULL, EOF causes an "corrupt FS"
 * error return.
 * SCRATCH_POOL is used for temporary allocations.
 */
svn_error_t *
svn_fs_fs__read_number_from_stream(apr_int64_t *result,
                                   svn_boolean_t *hit_eof,
                                   svn_stream_t *stream,
                                   apr_pool_t *scratch_pool);

#endif