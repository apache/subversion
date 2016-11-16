/*
 * config_pool.c :  pool of configuration objects
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
#include "private/svn_object_pool.h"

#include "svn_private_config.h"


/* Return a memory buffer structure allocated in POOL and containing the
 * data from CHECKSUM.
 */
static svn_membuf_t *
checksum_as_key(svn_checksum_t *checksum,
                apr_pool_t *pool)
{
  svn_membuf_t *result = apr_pcalloc(pool, sizeof(*result));
  apr_size_t size = svn_checksum_size(checksum);

  svn_membuf__create(result, size, pool);
  result->size = size; /* exact length is required! */
  memcpy(result->data, checksum->digest, size);

  return result;
}

/* Set *CFG to the configuration passed in as text in CONTENTS and *KEY to
 * the corresponding object pool key.  If no such configuration exists in
 * CONFIG_POOL, yet, parse CONTENTS and cache the result.
 *
 * RESULT_POOL determines the lifetime of the returned reference and
 * SCRATCH_POOL is being used for temporary allocations.
 */
static svn_error_t *
auto_parse(svn_config_t **cfg,
           svn_membuf_t **key,
           svn_repos__config_pool_t *config_pool,
           svn_stringbuf_t *contents,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
{
  svn_checksum_t *checksum;
  svn_config_t *config;
  apr_pool_t *cfg_pool;

  /* calculate SHA1 over the whole file contents */
  SVN_ERR(svn_checksum(&checksum, svn_checksum_sha1,
                       contents->data, contents->len, scratch_pool));

  /* return reference to suitable config object if that already exists */
  *key = checksum_as_key(checksum, result_pool);
  SVN_ERR(svn_object_pool__lookup((void **)cfg, config_pool,
                                  *key, result_pool));
  if (*cfg)
    return SVN_NO_ERROR;

  /* create a pool for the new config object and parse the data into it  */
  cfg_pool = svn_object_pool__new_item_pool(config_pool);

  SVN_ERR(svn_config_parse(&config,
                           svn_stream_from_stringbuf(contents, scratch_pool),
                           FALSE, FALSE, cfg_pool));

  /* switch config data to r/o mode to guarantee thread-safe access */
  svn_config__set_read_only(config, cfg_pool);

  /* add config in pool, handle loads races and return the right config */
  SVN_ERR(svn_object_pool__insert((void **)cfg, config_pool, *key, config,
                                  cfg_pool, result_pool));

  return SVN_NO_ERROR;
}

/* Set *CFG to the configuration stored in URL@HEAD and cache it in
 * CONFIG_POOL.  If PREFERRED_REPOS is given, use that if it also matches
 * URL.
 *
 * RESULT_POOL determines the lifetime of the returned reference and
 * SCRATCH_POOL is being used for temporary allocations.
 */
static svn_error_t *
find_repos_config(svn_config_t **cfg,
                  svn_membuf_t **key,
                  svn_repos__config_pool_t *config_pool,
                  const char *url,
                  svn_repos_t *preferred_repos,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_repos_t *repos = NULL;
  svn_fs_t *fs;
  svn_fs_root_t *root;
  svn_revnum_t youngest_rev;
  svn_node_kind_t node_kind;
  const char *dirent;
  svn_stream_t *stream;
  const char *fs_path;
  const char *repos_root_dirent;
  svn_checksum_t *checksum;
  svn_stringbuf_t *contents;

  *cfg = NULL;
  SVN_ERR(svn_uri_get_dirent_from_file_url(&dirent, url, scratch_pool));

  /* maybe we can use the preferred repos instance instead of creating a
   * new one */
  if (preferred_repos)
    {
      repos_root_dirent = svn_repos_path(preferred_repos, scratch_pool);
      if (!svn_dirent_is_absolute(repos_root_dirent))
        SVN_ERR(svn_dirent_get_absolute(&repos_root_dirent,
                                        repos_root_dirent,
                                        scratch_pool));

      if (svn_dirent_is_ancestor(repos_root_dirent, dirent))
        repos = preferred_repos;
    }

  /* open repos if no suitable preferred repos was provided. */
  if (!repos)
    {
      /* Search for a repository in the full path. */
      repos_root_dirent = svn_repos_find_root_path(dirent, scratch_pool);

      /* Attempt to open a repository at repos_root_dirent. */
      SVN_ERR(svn_repos_open3(&repos, repos_root_dirent, NULL,
                              scratch_pool, scratch_pool));
    }

  fs_path = &dirent[strlen(repos_root_dirent)];

  /* Get the filesystem. */
  fs = svn_repos_fs(repos);

  /* Find HEAD and the revision root */
  SVN_ERR(svn_fs_youngest_rev(&youngest_rev, fs, scratch_pool));
  SVN_ERR(svn_fs_revision_root(&root, fs, youngest_rev, scratch_pool));

  /* Fetch checksum and see whether we already have a matching config */
  SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_sha1, root, fs_path,
                               FALSE, scratch_pool));
  if (checksum)
    {
      *key = checksum_as_key(checksum, scratch_pool);
      SVN_ERR(svn_object_pool__lookup((void **)cfg, config_pool,
                                      *key, result_pool));
    }

  /* not parsed, yet? */
  if (!*cfg)
    {
      svn_filesize_t length;

      /* fetch the file contents */
      SVN_ERR(svn_fs_check_path(&node_kind, root, fs_path, scratch_pool));
      if (node_kind != svn_node_file)
        return SVN_NO_ERROR;

      SVN_ERR(svn_fs_file_length(&length, root, fs_path, scratch_pool));
      SVN_ERR(svn_fs_file_contents(&stream, root, fs_path, scratch_pool));
      SVN_ERR(svn_stringbuf_from_stream(&contents, stream,
                                        (apr_size_t)length, scratch_pool));

      /* handle it like ordinary file contents and cache it */
      SVN_ERR(auto_parse(cfg, key, config_pool, contents,
                         result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* API implementation */

svn_error_t *
svn_repos__config_pool_create(svn_repos__config_pool_t **config_pool,
                              svn_boolean_t thread_safe,
                              apr_pool_t *pool)
{
  return svn_error_trace(svn_object_pool__create(config_pool,
                                                 thread_safe, pool));
}

svn_error_t *
svn_repos__config_pool_get(svn_config_t **cfg,
                           svn_membuf_t **key,
                           svn_repos__config_pool_t *config_pool,
                           const char *path,
                           svn_boolean_t must_exist,
                           svn_repos_t *preferred_repos,
                           apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  apr_pool_t *scratch_pool = svn_pool_create(pool);

  /* make sure we always have a *KEY object */
  svn_membuf_t *local_key = NULL;
  if (key == NULL)
    key = &local_key;
  else
    *key = NULL;

  if (svn_path_is_url(path))
    {
      /* Read and cache the configuration.  This may fail. */
      err = find_repos_config(cfg, key, config_pool, path,
                              preferred_repos, pool, scratch_pool);
      if (err || !*cfg)
        {
          /* let the standard implementation handle all the difficult cases */
          svn_error_clear(err);
          err = svn_repos__retrieve_config(cfg, path, must_exist, FALSE,
                                           pool, scratch_pool);
        }
    }
  else
    {
      /* Outside of repo file.  Read it. */
      svn_stringbuf_t *contents;
      err = svn_stringbuf_from_file2(&contents, path, scratch_pool);
      if (err)
        {
          /* let the standard implementation handle all the difficult cases */
          svn_error_clear(err);
          err = svn_config_read3(cfg, path, must_exist, FALSE, FALSE, pool);
        }
      else
        {
          /* parsing and caching will always succeed */
          err = auto_parse(cfg, key, config_pool, contents,
                           pool, scratch_pool);
        }
    }

  svn_pool_destroy(scratch_pool);

  /* we need to duplicate the root structure as it contains temp. buffers */
  if (*cfg)
    *cfg = svn_config__shallow_copy(*cfg, pool);

  return svn_error_trace(err);
}
