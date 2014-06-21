/* rev_file.c --- revision file and index access functions
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

#include "rev_file.h"
#include "fs_fs.h"
#include "index.h"
#include "util.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

void
init_revision_file(svn_fs_fs__revision_file_t *file,
                   svn_fs_t *fs,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  file->is_packed = svn_fs_fs__is_packed_rev(fs, revision);
  file->start_revision = revision < ffd->min_unpacked_rev
                       ? revision - (revision % ffd->max_files_per_dir)
                       : revision;

  file->file = NULL;
  file->stream = NULL;
  file->p2l_stream = NULL;
  file->l2p_stream = NULL;
  file->pool = pool;
}

/* Core implementation of svn_fs_fs__open_pack_or_rev_file working on an
 * existing, initialized FILE structure.
 */
static svn_error_t *
open_pack_or_rev_file(svn_fs_fs__revision_file_t *file,
                      svn_fs_t *fs,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_error_t *err;
  svn_boolean_t retry = FALSE;

  do
    {
      const char *path = svn_fs_fs__path_rev_absolute(fs, rev, pool);
      apr_file_t *apr_file;

      /* open the revision file in buffered r/o mode */
      err = svn_io_file_open(&apr_file, path,
                             APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool);
      if (!err)
        {
          file->file = apr_file;
          file->stream = svn_stream_from_aprfile2(apr_file, TRUE, pool);
          file->is_packed = svn_fs_fs__is_packed_rev(fs, rev);

          return SVN_NO_ERROR;
        }

      if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        {
          if (ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT)
            {
              /* Could not open the file. This may happen if the
               * file once existed but got packed later. */
              svn_error_clear(err);

              /* if that was our 2nd attempt, leave it at that. */
              if (retry)
                return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                                         _("No such revision %ld"), rev);

              /* We failed for the first time. Refresh cache & retry. */
              SVN_ERR(svn_fs_fs__update_min_unpacked_rev(fs, pool));

              retry = TRUE;
            }
          else
            {
              svn_error_clear(err);
              return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                                       _("No such revision %ld"), rev);
            }
        }
      else
        {
          retry = FALSE;
        }
    }
  while (retry);

  return svn_error_trace(err);
}

svn_error_t *
svn_fs_fs__open_pack_or_rev_file(svn_fs_fs__revision_file_t **file,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 apr_pool_t *pool)
{
  *file = apr_palloc(pool, sizeof(**file));
  init_revision_file(*file, fs, rev, pool);

  return svn_error_trace(open_pack_or_rev_file(*file, fs, rev, pool));
}

svn_error_t *
svn_fs_fs__reopen_revision_file(svn_fs_fs__revision_file_t *file,
                                svn_fs_t *fs,
                                svn_revnum_t rev)
{
  if (file->file)
    svn_fs_fs__close_revision_file(file);

  return svn_error_trace(open_pack_or_rev_file(file, fs, rev, file->pool));
}

svn_error_t *
svn_fs_fs__open_proto_rev_file(svn_fs_fs__revision_file_t **file,
                               svn_fs_t *fs,
                               const svn_fs_fs__id_part_t *txn_id,
                               apr_pool_t *pool)
{
  apr_file_t *apr_file;
  SVN_ERR(svn_io_file_open(&apr_file,
                           svn_fs_fs__path_txn_proto_rev(fs, txn_id, pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool));

  *file = apr_pcalloc(pool, sizeof(**file));
  (*file)->file = apr_file;
  (*file)->is_packed = FALSE;
  (*file)->start_revision = SVN_INVALID_REVNUM;
  (*file)->stream = svn_stream_from_aprfile2(apr_file, TRUE, pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__close_revision_file(svn_fs_fs__revision_file_t *file)
{
  if (file->stream)
    SVN_ERR(svn_stream_close(file->stream));
  if (file->file)
    SVN_ERR(svn_io_file_close(file->file, file->pool));

  SVN_ERR(svn_fs_fs__packed_stream_close(file->l2p_stream));
  SVN_ERR(svn_fs_fs__packed_stream_close(file->p2l_stream));

  file->file = NULL;
  file->stream = NULL;
  file->l2p_stream = NULL;
  file->p2l_stream = NULL;

  return SVN_NO_ERROR;
}
