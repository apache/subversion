/* util.c --- utility functions for FSFS repo access
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

#include "svn_dirent_uri.h"
#include "private/svn_string_private.h"

#include "fs_fs.h"
#include "util.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

svn_boolean_t
svn_fs_fs__is_packed_rev(svn_fs_t *fs,
                         svn_revnum_t rev)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  return (rev < ffd->min_unpacked_rev);
}

const char *
svn_fs_fs__path_rev_packed(svn_fs_t *fs,
                           svn_revnum_t rev,
                           const char *kind,
                           apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  assert(ffd->max_files_per_dir);
  assert(svn_fs_fs__is_packed_rev(fs, rev));

  return svn_dirent_join_many(pool, fs->path, PATH_REVS_DIR,
                              apr_psprintf(pool,
                                           "%ld" PATH_EXT_PACKED_SHARD,
                                           rev / ffd->max_files_per_dir),
                              kind, NULL);
}

const char *
svn_fs_fs__path_min_unpacked_rev(svn_fs_t *fs,
                                 apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_MIN_UNPACKED_REV, pool);
}

svn_error_t *
svn_fs_fs__read_min_unpacked_rev(svn_revnum_t *min_unpacked_rev,
                                 svn_fs_t *fs,
                                 apr_pool_t *pool)
{
  char buf[80];
  apr_file_t *file;
  apr_size_t len;

  SVN_ERR(svn_io_file_open(&file,
                           svn_fs_fs__path_min_unpacked_rev(fs, pool),
                           APR_READ | APR_BUFFERED,
                           APR_OS_DEFAULT,
                           pool));
  len = sizeof(buf);
  SVN_ERR(svn_io_read_length_line(file, buf, &len, pool));
  SVN_ERR(svn_io_file_close(file, pool));

  *min_unpacked_rev = SVN_STR_TO_REV(buf);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__write_revnum_file(svn_fs_t *fs,
                             svn_revnum_t revnum,
                             apr_pool_t *scratch_pool)
{
  const char *final_path;
  char buf[SVN_INT64_BUFFER_SIZE];
  apr_size_t len = svn__i64toa(buf, revnum);
  buf[len] = '\n';

  final_path = svn_fs_fs__path_min_unpacked_rev(fs, scratch_pool);

  SVN_ERR(svn_io_write_atomic(final_path, buf, len + 1,
                              final_path /* copy_perms */, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__read_number_from_stream(apr_int64_t *result,
                                   svn_boolean_t *hit_eof,
                                   svn_stream_t *stream,
                                   apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *sb;
  svn_boolean_t eof;
  svn_error_t *err;

  SVN_ERR(svn_stream_readline(stream, &sb, "\n", &eof, scratch_pool));
  if (hit_eof)
    *hit_eof = eof;
  else
    if (eof)
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL, _("Unexpected EOF"));

  if (!eof)
    {
      err = svn_cstring_atoi64(result, sb->data);
      if (err)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, err,
                                 _("Number '%s' invalid or too large"),
                                 sb->data);
    }

  return SVN_NO_ERROR;
}

