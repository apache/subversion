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




#include <assert.h>

#include "svn_checksum.h"
#include "svn_config.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_repos.h"

#include "private/svn_atomic.h"
#include "private/svn_mutex.h"
#include "private/svn_subr_private.h"
#include "private/svn_repos_private.h"

#include "svn_private_config.h"


/* A reference counting wrapper around a parsed svn_config_t* instance.  All
 * data in CFG is expanded (to make it thread-safe) and considered read-only.
 */
typedef struct config_ref_t
{
  /* reference to the parent container */
  svn_config_pool__t *config_pool;

  /* UUID of the configuration contents.
   * This is a SHA1 checksum of the parsed textual representation of CFG. */
  svn_checksum_t *key;

  /* Parsed and expanded configuration */
  svn_config_t *cfg;

  /* private pool. This instance and its other members got allocated in it.
   * Will be destroyed when this instance is cleaned up. */
  apr_pool_t *pool;

  /* Number of references to this data struct */
  volatile svn_atomic_t ref_count;
} config_ref_t;


/* Data structure used to short-circuit the repository access for configs
 * read via URL.  After reading such a config successfully, we store key
 * repository information here and will validate it without actually opening
 * the repository.
 *
 * As this is only an optimization and may create many entries in
 * svn_config_pool__t's IN_REPO_HASH_POOL index, we clean them up once in
 * a while.
 */
typedef struct in_repo_config_t
{
  /* URL used to open the configuration */
  const char *url;

  /* Path of the repository that contained URL */
  const char *repo_root;

  /* Head revision of that repository when last read */
  svn_revnum_t revision;

  /* Contents checksum of the file stored under URL@REVISION */
  svn_checksum_t *key;
} in_repo_config_t;


/* Core data structure.  All access to it must be serialized using MUTEX.
 *
 * CONFIGS maps a SHA1 checksum of the config text to the config_ref_t with
 * the parsed configuration in it.
 *
 * To speed up URL@HEAD lookups, we maintain IN_REPO_CONFIGS as a secondary
 * hash index.  It maps URLs as provided by the caller onto in_repo_config_t
 * instances.  If that is still up-to-date, a further lookup into CONFIG
 * may yield the desired configuration without the need to actually open
 * the respective repository.
 *
 * Unused configurations that are kept in the IN_REPO_CONFIGS hash and may
 * be cleaned up when the hash is about to grow.  We use various pools to
 * ensure that we can release unused memory for each data structure:
 *
 * - every config_ref_t uses its own pool
 *   (gets destroyed when config is removed from cache)
 * - CONFIGS_HASH_POOL is used for configs only
 * - IN_REPO_HASH_POOL is used for IN_REPO_CONFIGS and the in_repo_config_t
 *   structure in it
 *
 * References handed out to callers must remain valid until released.  In
 * that case, cleaning up the config pool will only set READY_FOR_CLEANUP
 * and the last reference being returned will actually trigger the
 * destruction.
 */
struct svn_config_pool__t
{
  /* serialization object for all non-atomic data in this struct */
  svn_mutex__t *mutex;

  /* set to TRUE when pool passed to svn_config_pool__create() gets cleaned
   * up.  When set, the last config reference released must also destroy
   * this config pool object. */
  volatile svn_atomic_t ready_for_cleanup;

  /* SHA1 -> config_ref_t* mapping */
  apr_hash_t *configs;

  /* number of entries in CONFIGS with a reference count > 0 */
  volatile svn_atomic_t used_config_count;

  /* URL -> in_repo_config_t* mapping.
   * This is only a partial index and will get cleared regularly. */
  apr_hash_t *in_repo_configs;

  /* the root pool owning this structure */
  apr_pool_t *root_pool;

  /* allocate the CONFIGS index here */
  apr_pool_t *configs_hash_pool;

  /* allocate the IN_REPO_CONFIGS index and in_repo_config_t here */
  apr_pool_t *in_repo_hash_pool;
};


/* Callback for svn_config_enumerate2: Continue to next value. */
static svn_boolean_t
expand_value(const char *name,
             const char *value,
             void *baton,
             apr_pool_t *pool)
{
  return TRUE;
}

/* Callback for svn_config_enumerate_sections2:
 * Enumerate and implicitly expand all values in this section.
 */
static svn_boolean_t
expand_values_in_section(const char *name,
                         void *baton,
                         apr_pool_t *pool)
{
  svn_config_t *cfg = baton;
  svn_config_enumerate2(cfg, name, expand_value, NULL, pool);

  return TRUE;
}

/* Expand all values in all sections of CONFIG.
 */
static void
expand_all_values(config_ref_t *config)
{
  svn_config_enumerate_sections2(config->cfg, expand_values_in_section,
                                 config->cfg, config->pool);
}

/* Destructor function for the whole config pool.
 */
static apr_status_t
destroy_config_pool(svn_config_pool__t *config_pool)
{
  svn_mutex__lock(config_pool->mutex);

  /* there should be no outstanding references to any config in this pool */
  assert(svn_atomic_read(&config_pool->used_config_count) == 0);

  /* make future attempts to access this pool cause definitive segfaults */
  config_pool->configs = NULL;
  config_pool->in_repo_configs = NULL;

  /* This is the actual point of destruction. */
  /* Destroying the pool will also release the lock. */
  svn_pool_destroy(config_pool->root_pool);

  return APR_SUCCESS;
}

/* Pool cleanup function for the whole config pool.  Actual destruction will
 * be deferred until no configurations are left in use.
 */
static apr_status_t
config_pool_cleanup(void *baton)
{
  svn_config_pool__t *config_pool = baton;

  svn_atomic_set(&config_pool->ready_for_cleanup, TRUE);
  if (svn_atomic_read(&config_pool->used_config_count) == 0)
    {
      /* Attempts to get a configuration from a pool that whose cleanup has
       * already started is illegal.
       * So, used_config_count must not increase again.
       */
      destroy_config_pool(config_pool);
    }

  return APR_SUCCESS;
}

/* Cleanup function called when a config_ref_t gets released.
 */
static apr_status_t
config_ref_cleanup(void *baton)
{
  config_ref_t *config = baton;
  svn_config_pool__t *config_pool = config->config_pool;

  /* Maintain reference counters and handle object cleanup */
  if (   svn_atomic_dec(&config->ref_count) == 1
      && svn_atomic_dec(&config_pool->used_config_count) == 1
      && svn_atomic_read(&config_pool->ready_for_cleanup) == TRUE)
    {
      /* There cannot be any future references to a config in this pool.
       * So, we are the last one and need to finally clean it up.
       */
      destroy_config_pool(config_pool);
    }

  return APR_SUCCESS;
}

/* Return an automatic reference to the CFG member in CONFIG that will be
 * released when POOL gets cleaned up.
 */
static svn_config_t *
return_config_ref(config_ref_t *config,
                  apr_pool_t *pool)
{
  if (svn_atomic_inc(&config->ref_count) == 0)
    svn_atomic_inc(&config->config_pool->used_config_count);
  apr_pool_cleanup_register(pool, config, config_ref_cleanup,
                            apr_pool_cleanup_null);
  return config->cfg;
}

/* Set *CFG to the configuration with a parsed textual matching CHECKSUM.
 * Set *CFG to NULL if no such config can be found in CONFIG_POOL.
 * 
 * RESULT_POOL determines the lifetime of the returned reference.
 *
 * Requires external serialization on CONFIG_POOL.
 */
static svn_error_t *
config_by_checksum(svn_config_t **cfg,
                   svn_config_pool__t *config_pool,
                   svn_checksum_t *checksum,
                   apr_pool_t *result_pool)
{
  config_ref_t *config_ref = apr_hash_get(config_pool->configs,
                                          checksum->digest,
                                          svn_checksum_size(checksum));
  *cfg = config_ref ? return_config_ref(config_ref, result_pool) : NULL;

  return SVN_NO_ERROR;
}

/* Re-allocate CONFIGS in CONFIG_POOL and remove all unused configurations
 * to minimize memory consumption.
 *
 * Requires external serialization on CONFIG_POOL.
 */
static void
remove_unused_configs(svn_config_pool__t *config_pool)
{
  apr_pool_t *new_pool = svn_pool_create(config_pool->root_pool);
  apr_hash_t *new_hash = svn_hash__make(new_pool);

  apr_hash_index_t *hi;
  for (hi = apr_hash_first(config_pool->configs_hash_pool,
                           config_pool->configs);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      config_ref_t *config_ref = svn__apr_hash_index_val(hi);
      if (config_ref->ref_count == 0)
        svn_pool_destroy(config_ref->pool);
      else
        apr_hash_set(new_hash, config_ref->key->digest,
                     svn_checksum_size(config_ref->key), config_ref);
    }

  svn_pool_destroy(config_pool->configs_hash_pool);
  config_pool->configs = new_hash;
  config_pool->configs_hash_pool = new_pool;
}

/* Cache config_ref* in CONFIG_POOL and return a reference to it in *CFG.
 * RESULT_POOL determines the lifetime of that reference.
 *
 * Requires external serialization on CONFIG_POOL.
 */
static svn_error_t *
config_add(svn_config_t **cfg,
           svn_config_pool__t *config_pool,
           config_ref_t *config_ref,
           apr_pool_t *result_pool)
{
  config_ref_t *config = apr_hash_get(config_ref->config_pool->configs,
                                      config_ref->key->digest,
                                      svn_checksum_size(config_ref->key));
  if (config)
    {
      /* entry already exists (e.g. race condition)
       * Destroy the new one and return a reference to the existing one
       * because the existing one may already have references on it.
       */
      svn_pool_destroy(config_ref->pool);
      config_ref = config;
    }
  else
    {
      /* Release unused configurations if there are relatively frequent. */
      if (  config_pool->used_config_count * 2 + 4
          < apr_hash_count(config_pool->configs))
        {
          remove_unused_configs(config_pool);
        }

      /* add new index entry */
      apr_hash_set(config_ref->config_pool->configs,
                   config_ref->key->digest,
                   svn_checksum_size(config_ref->key),
                   config_ref);
    }

  *cfg = return_config_ref(config_ref, result_pool);

  return SVN_NO_ERROR;
}

/* Set *CFG to the configuration passed in as text in CONTENTS.  If no such
 * configuration exists in CONFIG_POOL, yet, parse CONTENTS and cache the
 * result.
 * 
 * RESULT_POOL determines the lifetime of the returned reference and 
 * SCRATCH_POOL is being used for temporary allocations.
 */
static svn_error_t *
auto_parse(svn_config_t **cfg,
           svn_config_pool__t *config_pool,
           svn_stringbuf_t *contents,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
{
  svn_checksum_t *checksum;
  config_ref_t *config_ref;
  apr_pool_t *cfg_pool;

  /* calculate SHA1 over the whole file contents */
  SVN_ERR(svn_stream_close
              (svn_stream_checksummed2
                  (svn_stream_from_stringbuf(contents, scratch_pool),
                   &checksum, NULL, svn_checksum_sha1, TRUE, scratch_pool)));

  /* return reference to suitable config object if that already exists */
  *cfg = NULL;
  SVN_MUTEX__WITH_LOCK(config_pool->mutex,
                       config_by_checksum(cfg, config_pool, checksum,
                                          result_pool));

  if (*cfg)
    return SVN_NO_ERROR;

  /* create a pool for the new config object and parse the data into it  */

  /* the following is thread-safe because the allocator is thread-safe */
  cfg_pool = svn_pool_create(config_pool->root_pool);

  config_ref = apr_pcalloc(cfg_pool, sizeof(*config_ref));
  config_ref->config_pool = config_pool;
  config_ref->key = svn_checksum_dup(checksum, cfg_pool);
  config_ref->pool = cfg_pool;
  config_ref->ref_count = 0;

  SVN_ERR(svn_config_parse(&config_ref->cfg,
                           svn_stream_from_stringbuf(contents, scratch_pool),
                           TRUE, TRUE, cfg_pool));

  /* make sure r/o access to config data will not modify the internal state */
  expand_all_values(config_ref);

  /* add config in pool, handle loads races and return the right config */
  SVN_MUTEX__WITH_LOCK(config_pool->mutex,
                       config_add(cfg, config_pool, config_ref, result_pool));

  return SVN_NO_ERROR;
}

/* Set *CFG to the configuration stored in URL@HEAD and cache it in 
 * CONFIG_POOL.
 * 
 * RESULT_POOL determines the lifetime of the returned reference and 
 * SCRATCH_POOL is being used for temporary allocations.
 *
 * Requires external serialization on CONFIG_POOL.
 */
static svn_error_t *
add_checksum(svn_config_pool__t *config_pool,
             const char *url,
             const char *repos_root,
             svn_revnum_t revision,
             svn_checksum_t *checksum)
{
  apr_size_t path_len = strlen(url);
  apr_pool_t *pool = config_pool->in_repo_hash_pool;
  in_repo_config_t *config = apr_hash_get(config_pool->in_repo_configs,
                                            url, path_len);
  if (config)
    {
      /* update the existing entry */
      memcpy((void *)config->key->digest, checksum->digest,
             svn_checksum_size(checksum));
      config->revision = revision;

      /* duplicate the string only if necessary */
      if (strcmp(config->repo_root, repos_root))
        config->repo_root = apr_pstrdup(pool, repos_root);
    }
  else
    {
      /* insert a new entry.
       * Limit memory consumption by cyclically clearing pool and hash. */
      if (2 * apr_hash_count(config_pool->configs)
          < apr_hash_count(config_pool->in_repo_configs))
        {
          svn_pool_clear(pool);
          config_pool->in_repo_configs = svn_hash__make(pool);
        }

      /* construct the new entry */
      config = apr_pcalloc(pool, sizeof(*config));
      config->key = svn_checksum_dup(checksum, pool);
      config->url = apr_pstrmemdup(pool, url, path_len);
      config->repo_root = apr_pstrdup(pool, repos_root);
      config->revision = revision;

      /* add to index */
      apr_hash_set(config_pool->in_repo_configs, url, path_len, config);
    }

  return SVN_NO_ERROR;
}

/* Set *CFG to the configuration stored in URL@HEAD and cache it in 
 * CONFIG_POOL.
 * 
 * RESULT_POOL determines the lifetime of the returned reference and 
 * SCRATCH_POOL is being used for temporary allocations.
 */
static svn_error_t *
find_repos_config(svn_config_t **cfg,
                  svn_config_pool__t *config_pool,
                  const char *url,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_repos_t *repos;
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

  /* Search for a repository in the full path. */
  repos_root_dirent = svn_repos_find_root_path(dirent, scratch_pool);
    return SVN_NO_ERROR;

  /* Attempt to open a repository at repos_root_dirent. */
  SVN_ERR(svn_repos_open2(&repos, repos_root_dirent, NULL, scratch_pool));

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
    SVN_MUTEX__WITH_LOCK(config_pool->mutex,
                         config_by_checksum(cfg, config_pool, checksum,
                                            result_pool));

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
      SVN_ERR(auto_parse(cfg, config_pool, contents, result_pool,
                         scratch_pool));
    }

  /* store the (path,rev) -> checksum mapping as well */
  if (*cfg)
    SVN_MUTEX__WITH_LOCK(config_pool->mutex,
                         add_checksum(config_pool, url, repos_root_dirent,
                                      youngest_rev, checksum));

  return SVN_NO_ERROR;
}

/* Set *CFG to the configuration cached in CONFIG_POOL for URL.  If no
 * suitable config has been cached or if it is potentially outdated, set
 * *CFG to NULL.
 *
 * RESULT_POOL determines the lifetime of the returned reference and 
 * SCRATCH_POOL is being used for temporary allocations.
 *
 * Requires external serialization on CONFIG_POOL.
 */
static svn_error_t *
config_by_url(svn_config_t **cfg,
              svn_config_pool__t *config_pool,
              const char *url,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  config_ref_t *config_ref = NULL;
  svn_stringbuf_t *contents;
  apr_int64_t current;

  /* hash lookup url -> sha1 -> config */
  in_repo_config_t *config = apr_hash_get(config_pool->in_repo_configs,
                                          url, APR_HASH_KEY_STRING);
  *cfg = 0;
  if (config)
    config_ref = apr_hash_get(config_pool->configs,
                              config->key->digest,
                              svn_checksum_size(config->key));
  if (!config_ref)
    return SVN_NO_ERROR;

  /* found *some* configuration. 
   * Verify that it is still current.  Will fail for BDB repos. */
  err = svn_stringbuf_from_file2(&contents, 
                                 svn_dirent_join(config->repo_root,
                                                 "db/current", scratch_pool),
                                 scratch_pool);
  if (!err)
    err = svn_cstring_atoi64(&current, contents->data);

  if (err)
    svn_error_clear(err);
  else if (current == config->revision)
    *cfg = return_config_ref(config_ref, result_pool);

  return SVN_NO_ERROR;
}

/* API implementation */

svn_error_t *
svn_config_pool__create(svn_config_pool__t **config_pool,
                        apr_pool_t *pool)
{
  /* our allocator must be thread-safe */
  apr_pool_t *root_pool
    = apr_allocator_owner_get(svn_pool_create_allocator(TRUE));

  /* construct the config pool in our private ROOT_POOL to survive POOL
   * cleanup and to prevent threading issues with the allocator */
  svn_config_pool__t *result = apr_pcalloc(pool, sizeof(*result));
  SVN_ERR(svn_mutex__init(&result->mutex, TRUE, root_pool));

  result->root_pool = root_pool;
  result->configs_hash_pool = svn_pool_create(root_pool);
  result->in_repo_hash_pool = svn_pool_create(root_pool);
  result->configs = svn_hash__make(result->configs_hash_pool);
  result->in_repo_configs = svn_hash__make(result->in_repo_hash_pool);
  result->ready_for_cleanup = FALSE;

  /* make sure we clean up nicely */
  apr_pool_cleanup_register(pool, result, config_pool_cleanup,
                            apr_pool_cleanup_null);

  *config_pool = result;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_config_pool__get(svn_config_t **cfg,
                     svn_config_pool__t *config_pool,
                     const char *path,
                     apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  apr_pool_t *scratch_pool = svn_pool_create(pool);

  if (svn_path_is_url(path))
    {
      /* Read config file from repository.
       * Attempt a quick lookup first. */
      SVN_MUTEX__WITH_LOCK(config_pool->mutex,
                           config_by_url(cfg, config_pool, path, pool,
                                         scratch_pool));
      if (cfg)
        return SVN_NO_ERROR;

      /* Read and cache the configuration.  This may fail. */
      err = find_repos_config(cfg, config_pool, path, pool, scratch_pool);
      if (err || !*cfg)
        {
          /* let the standard implementation handle all the difficult cases */
          svn_error_clear(err);
          err = svn_repos__retrieve_config(cfg, path, TRUE, pool);
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
          err = svn_config_read3(cfg, path, TRUE, TRUE, TRUE, pool);
        }
      else
        {
          /* parsing and caching will always succeed */
          err = auto_parse(cfg, config_pool, contents, pool, scratch_pool);
        }
    }

  svn_pool_destroy(scratch_pool);

  return err;
}
