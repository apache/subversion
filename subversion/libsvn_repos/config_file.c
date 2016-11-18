/*
 * config_file.c :  efficiently read config files from disk or repo
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




#include "svn_checksum.h"
#include "svn_path.h"
#include "svn_pools.h"

#include "private/svn_subr_private.h"
#include "private/svn_repos_private.h"

#include "config_file.h"

#include "svn_private_config.h"



typedef struct config_access_t
{
  /* The last repository that we found the requested URL in.  May be NULL. */
  svn_repos_t *repos;

  /* Owning pool of this structure and is private to this structure.
   * All objects with the lifetime of this access object will be allocated
   * from this pool. */
  apr_pool_t *pool;
} config_access_t;



/* A stream object that gives access to a representation's content but
 * delays accessing the repository data until the stream is first used.
 * IOW, the stream object is cheap as long as it is not accessed.
 */
typedef struct presentation_stream_baton_t
{
  svn_fs_root_t *root;
  const char *fs_path;
  apr_pool_t *pool;
  svn_stream_t *inner;
} presentation_stream_baton_t;

static svn_error_t *
auto_open_inner_stream(presentation_stream_baton_t *b)
{
  if (!b->inner)
    {
      svn_filesize_t length;
      svn_stream_t *stream;
      svn_stringbuf_t *contents;

      SVN_ERR(svn_fs_file_length(&length, b->root, b->fs_path, b->pool));
      SVN_ERR(svn_fs_file_contents(&stream, b->root, b->fs_path, b->pool));
      SVN_ERR(svn_stringbuf_from_stream(&contents, stream,
                                        (apr_size_t)length, b->pool));
      b->inner = svn_stream_from_stringbuf(contents, b->pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
read_handler_rep(void *baton, char *buffer, apr_size_t *len)
{
  presentation_stream_baton_t *b = baton;
  SVN_ERR(auto_open_inner_stream(b));

  return svn_error_trace(svn_stream_read2(b->inner, buffer, len));
}

static svn_error_t *
mark_handler_rep(void *baton, svn_stream_mark_t **mark, apr_pool_t *pool)
{
  presentation_stream_baton_t *b = baton;
  SVN_ERR(auto_open_inner_stream(b));

  return svn_error_trace(svn_stream_mark(b->inner, mark, pool));
}

static svn_error_t *
seek_handler_rep(void *baton, const svn_stream_mark_t *mark)
{
  presentation_stream_baton_t *b = baton;
  SVN_ERR(auto_open_inner_stream(b));

  return svn_error_trace(svn_stream_seek(b->inner, mark));
}

static svn_error_t *
skip_handler_rep(void *baton, apr_size_t len)
{
  presentation_stream_baton_t *b = baton;
  SVN_ERR(auto_open_inner_stream(b));

  return svn_error_trace(svn_stream_skip(b->inner, len));
}

static svn_error_t *
data_available_handler_rep(void *baton, svn_boolean_t *data_available)
{
  presentation_stream_baton_t *b = baton;
  SVN_ERR(auto_open_inner_stream(b));

  return svn_error_trace(svn_stream_data_available(b->inner, data_available));
}

static svn_error_t *
readline_handler_rep(void *baton,
                        svn_stringbuf_t **stringbuf,
                        const char *eol,
                        svn_boolean_t *eof,
                        apr_pool_t *pool)
{
  presentation_stream_baton_t *b = baton;
  SVN_ERR(auto_open_inner_stream(b));

  return svn_error_trace(svn_stream_readline(b->inner, stringbuf, eol, eof,
                                             pool));
}

/* Return a lazy access stream for FS_PATH under ROOT, allocated in POOL. */
static svn_stream_t *
representation_stream(svn_fs_root_t *root,
                      const char *fs_path,
                      apr_pool_t *pool)
{
  svn_stream_t *stream;
  presentation_stream_baton_t *baton;

  baton = apr_pcalloc(pool, sizeof(*baton));
  baton->root = root;
  baton->fs_path = fs_path;
  baton->pool = pool;

  stream = svn_stream_create(baton, pool);
  svn_stream_set_read2(stream, read_handler_rep, read_handler_rep);
  svn_stream_set_mark(stream, mark_handler_rep);
  svn_stream_set_seek(stream, seek_handler_rep);
  svn_stream_set_skip(stream, skip_handler_rep);
  svn_stream_set_data_available(stream, data_available_handler_rep);
  svn_stream_set_readline(stream, readline_handler_rep);
  return stream;
}

/* Open the in-repository file at URL, return its content checksum in
 * *CHECKSUM and the content itself through *STREAM.  Allocate those with
 * the lifetime of ACCESS and use SCRATCH_POOL for temporaries.
 *
 * Error out when the file does not exist but MUST_EXIST is set.
 */
static svn_error_t *
get_repos_config(svn_stream_t **stream,
                 svn_checksum_t **checksum,
                 config_access_t *access,
                 const char *url,
                 svn_boolean_t must_exist,
                 apr_pool_t *scratch_pool)
{
  svn_fs_t *fs;
  svn_fs_root_t *root;
  svn_revnum_t youngest_rev;
  svn_node_kind_t node_kind;
  const char *dirent;
  const char *fs_path;
  const char *repos_root_dirent;

  SVN_ERR(svn_uri_get_dirent_from_file_url(&dirent, url, access->pool));

  /* Maybe we can use the repos hint instance instead of creating a
   * new one. */
  if (access->repos)
    {
      repos_root_dirent = svn_repos_path(access->repos, scratch_pool);
      if (!svn_dirent_is_absolute(repos_root_dirent))
        SVN_ERR(svn_dirent_get_absolute(&repos_root_dirent,
                                        repos_root_dirent,
                                        scratch_pool));

      if (!svn_dirent_is_ancestor(repos_root_dirent, dirent))
        access->repos = NULL;
    }

  /* Open repos if no suitable repos is available. */
  if (!access->repos)
    {
      /* Search for a repository in the full path. */
      repos_root_dirent = svn_repos_find_root_path(dirent, scratch_pool);

      /* Attempt to open a repository at repos_root_dirent. */
      SVN_ERR(svn_repos_open3(&access->repos, repos_root_dirent, NULL,
                              access->pool, scratch_pool));
    }

  fs_path = &dirent[strlen(repos_root_dirent)];

  /* Get the filesystem. */
  fs = svn_repos_fs(access->repos);

  /* Find HEAD and the revision root */
  SVN_ERR(svn_fs_youngest_rev(&youngest_rev, fs, scratch_pool));
  SVN_ERR(svn_fs_revision_root(&root, fs, youngest_rev, access->pool));

  /* Special case: non-existent paths are handled as "empty" contents. */
  SVN_ERR(svn_fs_check_path(&node_kind, root, fs_path, scratch_pool));
  if (node_kind == svn_node_none && !must_exist)
    {
      *stream = svn_stream_empty(access->pool);
      SVN_ERR(svn_checksum(checksum, svn_checksum_md5, "", 0, access->pool));
      return SVN_NO_ERROR;
    }

  /* Fetch checksum and see whether we already have a matching config */
  SVN_ERR(svn_fs_file_checksum(checksum, svn_checksum_md5, root, fs_path,
                               TRUE, access->pool));

  *stream = representation_stream(root, fs_path, access->pool);

  return SVN_NO_ERROR;
}

/* Open the file at PATH, return its content checksum in CHECKSUM and the
 * content itself through *STREAM.  Allocate those with the lifetime of
 * ACCESS.
 */
static svn_error_t *
get_file_config(svn_stream_t **stream,
                svn_checksum_t **checksum,
                config_access_t *access,
                const char *path)
{
  svn_stringbuf_t *contents;
  SVN_ERR(svn_stringbuf_from_file2(&contents, path, access->pool));

  /* calculate MD5 over the whole file contents */
  SVN_ERR(svn_checksum(checksum, svn_checksum_md5,
                       contents->data, contents->len, access->pool));
  *stream = svn_stream_from_stringbuf(contents, access->pool);

  return SVN_NO_ERROR;
}

config_access_t *
svn_repos__create_config_access(svn_repos_t *repos_hint,
                                apr_pool_t *result_pool)
{
  apr_pool_t *pool = svn_pool_create(result_pool);
  config_access_t *result = apr_pcalloc(pool, sizeof(*result));

  result->repos = repos_hint;
  result->pool = pool;

  return result;
}

void 
svn_repos__destroy_config_access(config_access_t *access)
{
  svn_pool_destroy(access->pool);
}

svn_error_t *
svn_repos__get_config(svn_stream_t **stream,
                      svn_checksum_t **checksum,
                      config_access_t *access,
                      const char *path,
                      svn_boolean_t must_exist,
                      apr_pool_t *scratch_pool)
{
  if (svn_path_is_url(path))
    SVN_ERR(get_repos_config(stream, checksum, access, path, must_exist,
                             scratch_pool));
  else
    SVN_ERR(get_file_config(stream, checksum, access, path));

  return SVN_NO_ERROR;
}
