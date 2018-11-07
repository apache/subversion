/* util.c --- git filesystem utilities
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

#include "svn_fs.h"
#include "svn_sorts.h"
#include "svn_version.h"
#include "svn_pools.h"

#include "svn_private_config.h"

#include "../libsvn_fs/fs-loader.h"

#include "fs_git.h"

typedef struct git_fs_blob_stream_t
{
  apr_pool_t *cleanup_pool;
  git_odb *odb;
  git_odb_stream *odb_stream;
  const char *data;
  apr_size_t data_left;
} git_fs_blob_stream_t;

static apr_status_t
blob_stream_cleanup(void *baton)
{
  git_fs_blob_stream_t *bs = baton;

  git_odb_stream_free(bs->odb_stream);
  git_odb_free(bs->odb);
  return SVN_NO_ERROR;
}

static svn_error_t *blob_stream_read(void *baton,
                                     char *buffer,
                                     apr_size_t *len)
{
  git_fs_blob_stream_t *bs = baton;

  if (bs->data)
    {
      if (bs->data_left)
        {
          *len = MIN(*len, bs->data_left);
          memcpy(buffer, bs->data, *len);
          bs->data_left -= *len;
          bs->data += *len;

          if (!bs->data_left)
            {
              /* Releases file data! */
              svn_pool_destroy(bs->cleanup_pool);
              bs->cleanup_pool = NULL;
            }
        }
      else
        *len = 0;

      return SVN_NO_ERROR;
    }
  else
  {
    GIT2_ERR(
      git_odb_stream_read(bs->odb_stream, buffer, *len));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *blob_stream_close(void *baton)
{
  git_fs_blob_stream_t *bs = baton;

  if (bs->cleanup_pool)
    {
      if (bs->data)
        apr_pool_destroy(bs->cleanup_pool);
      else
        apr_pool_cleanup_run(bs->cleanup_pool, bs, blob_stream_cleanup);
      bs->cleanup_pool = NULL;
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_git__get_blob_stream(svn_stream_t **stream,
                            svn_fs_t *fs,
                            const git_oid *oid,
                            apr_pool_t *result_pool)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;
  git_odb *odb;
#if 0
  git_odb_stream *odb_stream;
#endif
  git_fs_blob_stream_t *blob_stream;
  int git_err;

  GIT2_ERR(git_repository_odb(&odb, fgf->repos));

  /* ### Somehow libgit2 assumes that we should just keep everything
     in RAM. There is not a single ODB backend in the libgit2 source that
     support streaming reads (yet). */
#if 0
  git_err = git_odb_open_rstream(&odb_stream, odb, oid);
  if (git_err)
#endif
    {
      git_odb_object *ob;
      apr_pool_t *subpool;
      apr_size_t size;
      char *data;

#if 0
      giterr_clear();
#endif
      /* libgit2 doesn' promise that this works :(
         (Somehow they don't want to support files that don't
          fit in memory) */

      git_err = git_odb_read(&ob, odb, oid);
      if (git_err)
        {
          git_odb_free(odb);
          return svn_git__wrap_git_error();
        }

      subpool = svn_pool_create(result_pool);

      size = git_odb_object_size(ob);
      data = apr_pmemdup(subpool, git_odb_object_data(ob), size);

      git_odb_object_free(ob);

      blob_stream = apr_pcalloc(result_pool, sizeof(*blob_stream));
      blob_stream->cleanup_pool = subpool;
      blob_stream->data = data;
      blob_stream->data_left = size;

      git_odb_free(odb);
      odb = NULL;
    }
#if 0
  else
    {
      blob_stream = apr_pcalloc(result_pool, sizeof(*blob_stream));
      blob_stream->cleanup_pool = result_pool;
      blob_stream->odb = odb;
      blob_stream->odb_stream = odb_stream;
    }
#endif

  *stream = svn_stream_create(blob_stream, result_pool);
  svn_stream_set_read2(*stream, blob_stream_read, blob_stream_read);
  svn_stream_set_close(*stream, blob_stream_close);

  apr_pool_cleanup_register(result_pool, blob_stream, blob_stream_cleanup,
                            apr_pool_cleanup_null);

  return SVN_NO_ERROR;
}

