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

#include "svn_fs.h"
#include "private/svn_fs_private.h"
#include "private/svn_cache.h"

#include "svn_pools.h"

/* The cache settings as a process-wide singleton.
 */
static svn_fs_cache_config_t cache_settings =
  {
    /* default configuration:
     */
    0x1000000,   /* 16 MB for caches */
    16,          /* up to 16 files kept open */
    TRUE,        /* cache fulltexts */
    FALSE,       /* don't cache text deltas */
    FALSE        /* assume multi-threaded operation */
  };

/* Get the current FSFS cache configuration. */
const svn_fs_cache_config_t *
svn_fs_get_cache_config(void)
{
  return &cache_settings;
}

/* Access the process-global (singleton) membuffer cache. The first call
 * will automatically allocate the cache using the current cache config.
 * NULL will be returned if the desired cache size is 0.
 */
svn_membuffer_t *
svn_fs__get_global_membuffer_cache(void)
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
           (apr_size_t)cache_size,
           (apr_size_t)cache_size / 16,
           ! svn_fs_get_cache_config()->single_threaded,
           pool);
    }

  return cache;
}

void 
svn_fs_set_cache_config(const svn_fs_cache_config_t *settings)
{
  cache_settings = *settings;

  /* Allocate global membuffer cache as a side-effect.
   * Only the first call will actually take affect. */
  svn_fs__get_global_membuffer_cache();
}

