/* caching.c : in-memory caching
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

#include "fs.h"
#include "fs_fs.h"
#include "id.h"
#include "dag.h"
#include "temp_serializer.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_config.h"
#include "svn_pools.h"

#include "svn_private_config.h"

/*** Dup/serialize/deserialize functions. ***/

/* Return a memcache in *MEMCACHE_P for FS if it's configured to use
   memcached, or NULL otherwise.  Also, sets *FAIL_STOP to a boolean
   indicating whether cache errors should be returned to the caller or
   just passed to the FS warning handler.  Use FS->pool for allocating
   the memcache, and POOL for temporary allocations. */
static svn_error_t *
read_config(svn_memcache_t **memcache_p,
            svn_boolean_t *fail_stop,
            svn_fs_t *fs,
            apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  SVN_ERR(svn_cache__make_memcache_from_config(memcache_p, ffd->config,
                                              fs->pool));
  return svn_config_get_bool(ffd->config, fail_stop,
                             CONFIG_SECTION_CACHES, CONFIG_OPTION_FAIL_STOP,
                             FALSE);
}


/* Implements svn_cache__error_handler_t */
static svn_error_t *
warn_on_cache_errors(svn_error_t *err,
                     void *baton,
                     apr_pool_t *pool)
{
  svn_fs_t *fs = baton;
  (fs->warning)(fs->warning_baton, err);
  svn_error_clear(err);
  return SVN_NO_ERROR;
}

/* The cache settings as a process-wide singleton.
 */
static svn_fs_fs__cache_config_t cache_settings =
  {
    /* default configuration:
     */
    0x8000000,   /* 128 MB for caches */
    16,          /* up to 16 files kept open */
    FALSE,       /* don't cache fulltexts */
    FALSE,       /* don't cache text deltas */
    FALSE        /* assume multi-threaded operation */
  };

/* Get the current FSFS cache configuration. */
const svn_fs_fs__cache_config_t *
svn_fs_fs__get_cache_config(void)
{
  return &cache_settings;
}

/* Access the process-global (singleton) membuffer cache. The first call
 * will automatically allocate the cache using the current cache config.
 * NULL will be returned if the desired cache size is 0.
 */
static svn_membuffer_t *
get_global_membuffer_cache(void)
{
  static svn_membuffer_t *cache = NULL;

  apr_uint64_t cache_size = cache_settings.cache_size;
  if (!cache && cache_size)
    {
      /* auto-allocate cache*/
      apr_allocator_t *allocator = NULL;
      apr_pool_t *pool = NULL;

      if (apr_allocator_create(&allocator))
        return NULL;

      /* Ensure that we free partially allocated data if we run OOM
       * before the cache is complete: If the cache cannot be allocated
       * in its full size, the create() function will clear the pool
       * explicitly. The allocator will make sure that any memory no
       * longer used by the pool will actually be returned to the OS.
       */
      apr_allocator_max_free_set(allocator, 1);
      pool = svn_pool_create_ex(NULL, allocator);

      svn_cache__membuffer_cache_create
          (&cache,
           cache_size,
           cache_size / 16,
           ! svn_fs_fs__get_cache_config()->single_threaded,
           pool);
    }

  return cache;
}

/* Access the process-global (singleton) open file handle cache. The first
 * call will automatically allocate the cache using the current cache config.
 * Even for file handle limit of 0, a cache object will be returned.
 */
static svn_file_handle_cache_t *
get_global_file_handle_cache(void)
{
  static svn_file_handle_cache_t *cache = NULL;

  if (!cache)
    svn_file_handle_cache__create_cache(&cache,
                                        cache_settings.file_handle_count,
                                        !cache_settings.single_threaded,
                                        svn_pool_create(NULL));

  return cache;
}

void 
svn_fs_fs__set_cache_config(const svn_fs_fs__cache_config_t *settings)
{
  cache_settings = *settings;

  /* Allocate global membuffer cache as a side-effect.
   * Only the first call will actually take affect. */
  get_global_membuffer_cache();

  /* Same for the file handle cache. */
  get_global_file_handle_cache();
}

svn_error_t *
svn_fs_fs__initialize_caches(svn_fs_t *fs,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *prefix = apr_pstrcat(pool,
                                   "fsfs:", ffd->uuid,
                                   "/", fs->path, ":",
                                   NULL);
  svn_memcache_t *memcache;
  svn_boolean_t no_handler;

  SVN_ERR(read_config(&memcache, &no_handler, fs, pool));

  /* Make the cache for revision roots.  For the vast majority of
   * commands, this is only going to contain a few entries (svnadmin
   * dump/verify is an exception here), so to reduce overhead let's
   * try to keep it to just one page.  I estimate each entry has about
   * 72 bytes of overhead (svn_revnum_t key, svn_fs_id_t +
   * id_private_t + 3 strings for value, and the cache_entry); the
   * default pool size is 8192, so about a hundred should fit
   * comfortably. */
  if (get_global_membuffer_cache())
    SVN_ERR(svn_cache__create_membuffer_cache(&(ffd->rev_root_id_cache),
                                              get_global_membuffer_cache(),
                                              svn_fs_fs__serialize_id,
                                              svn_fs_fs__deserialize_id,
                                              sizeof(svn_revnum_t),
                                              apr_pstrcat(pool, prefix, "RRI",
                                                          NULL),
                                              fs->pool));
  else
    SVN_ERR(svn_cache__create_inprocess(&(ffd->rev_root_id_cache),
                                        svn_fs_fs__serialize_id,
                                        svn_fs_fs__deserialize_id,
                                        sizeof(svn_revnum_t),
                                        1, 100, FALSE, fs->pool));
  if (! no_handler)
    SVN_ERR(svn_cache__set_error_handler(ffd->rev_root_id_cache,
                                         warn_on_cache_errors, fs, pool));


  /* Rough estimate: revision DAG nodes have size around 320 bytes, so
   * let's put 16 on a page. */
  if (get_global_membuffer_cache())
    SVN_ERR(svn_cache__create_membuffer_cache(&(ffd->rev_node_cache),
                                              get_global_membuffer_cache(),
                                              svn_fs_fs__dag_serialize,
                                              svn_fs_fs__dag_deserialize,
                                              APR_HASH_KEY_STRING,
                                              apr_pstrcat(pool, prefix, "DAG",
                                                          NULL),
                                              fs->pool));
  else
    SVN_ERR(svn_cache__create_inprocess(&(ffd->rev_node_cache),
                                        svn_fs_fs__dag_serialize,
                                        svn_fs_fs__dag_deserialize,
                                        APR_HASH_KEY_STRING,
                                        1024, 16, FALSE, fs->pool));
  if (! no_handler)
    SVN_ERR(svn_cache__set_error_handler(ffd->rev_node_cache,
                                         warn_on_cache_errors, fs, pool));


  /* Very rough estimate: 1K per directory. */
  if (get_global_membuffer_cache())
    SVN_ERR(svn_cache__create_membuffer_cache(&(ffd->dir_cache),
                                              get_global_membuffer_cache(),
                                              svn_fs_fs__dir_entries_serialize,
                                              svn_fs_fs__dir_entries_deserialize,
                                              APR_HASH_KEY_STRING,
                                              apr_pstrcat(pool, prefix, "DIR",
                                                          NULL),
                                              fs->pool));
  else
    SVN_ERR(svn_cache__create_inprocess(&(ffd->dir_cache),
                                        svn_fs_fs__dir_entries_serialize,
                                        svn_fs_fs__dir_entries_deserialize,
                                        APR_HASH_KEY_STRING,
                                        1024, 8, FALSE, fs->pool));

  if (! no_handler)
    SVN_ERR(svn_cache__set_error_handler(ffd->dir_cache,
                                         warn_on_cache_errors, fs, pool));

  /* Only 16 bytes per entry (a revision number + the corresponding offset).
     Since we want ~8k pages, that means 512 entries per page. */
  if (get_global_membuffer_cache())
    SVN_ERR(svn_cache__create_membuffer_cache(&(ffd->packed_offset_cache),
                                              get_global_membuffer_cache(),
                                              svn_fs_fs__serialize_manifest,
                                              svn_fs_fs__deserialize_manifest,
                                              sizeof(svn_revnum_t),
                                              apr_pstrcat(pool, prefix, "PACK-MANIFEST",
                                                          NULL),
                                              fs->pool));
  else
    SVN_ERR(svn_cache__create_inprocess(&(ffd->packed_offset_cache),
                                        svn_fs_fs__serialize_manifest,
                                        svn_fs_fs__deserialize_manifest,
                                        sizeof(svn_revnum_t),
                                        32, 1, FALSE, fs->pool));

  if (! no_handler)
    SVN_ERR(svn_cache__set_error_handler(ffd->packed_offset_cache,
                                         warn_on_cache_errors, fs, pool));

  /* initialize fulltext cache as configured */
  if (memcache)
    {
      SVN_ERR(svn_cache__create_memcache(&(ffd->fulltext_cache),
                                         memcache,
                                         /* Values are svn_string_t */
                                         NULL, NULL,
                                         APR_HASH_KEY_STRING,
                                         apr_pstrcat(pool, prefix, "TEXT",
                                                     NULL),
                                         fs->pool));
    }
  else if (get_global_membuffer_cache() && 
           svn_fs_fs__get_cache_config()->cache_fulltexts)
    {
      SVN_ERR(svn_cache__create_membuffer_cache(&(ffd->fulltext_cache),
                                                get_global_membuffer_cache(),
                                                /* Values are svn_string_t */
                                                NULL, NULL,
                                                APR_HASH_KEY_STRING,
                                                apr_pstrcat(pool, prefix, "TEXT",
                                                            NULL),
                                                fs->pool));
    }
  else
    {
      ffd->fulltext_cache = NULL;
    }

  if (ffd->fulltext_cache && ! no_handler)
    SVN_ERR(svn_cache__set_error_handler(ffd->fulltext_cache,
            warn_on_cache_errors, fs, pool));

  /* initialize file handle cache as configured */
  ffd->file_handle_cache = get_global_file_handle_cache();

  /* if enabled, enable the txdelta window cache */
  if (get_global_membuffer_cache() &&
      svn_fs_fs__get_cache_config()->cache_txdeltas)
    {
      SVN_ERR(svn_cache__create_membuffer_cache
                (&(ffd->txdelta_window_cache),
                 get_global_membuffer_cache(),
                 svn_fs_fs__serialize_txdelta_window,
                 svn_fs_fs__deserialize_txdelta_window,
                 APR_HASH_KEY_STRING,
                 apr_pstrcat(pool, prefix, "TXDELTA_WINDOW", NULL),
                 fs->pool));
    }
  else
    {
      ffd->txdelta_window_cache = NULL;
    }

  if (ffd->txdelta_window_cache && ! no_handler)
    SVN_ERR(svn_cache__set_error_handler(ffd->txdelta_window_cache,
                                         warn_on_cache_errors, fs, pool));

  return SVN_NO_ERROR;
}
