/*
 * blncache.c: DAV baseline information cache.
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

#include <apr_pools.h>

#include "svn_dirent_uri.h"
#include "svn_types.h"
#include "svn_pools.h"

#include "blncache.h"

#define MAX_CACHE_SIZE 1000

typedef struct baseline_info_t
{
  const char *bc_url;
  svn_revnum_t revsion;
} baseline_info_t;

struct svn_ra_serf__blncache_t
{
  /* Pool to store cached data. */
  apr_pool_t *cache_pool;

  /** A hash containing as keys svn_revnum_t of baseline; the values are
   * baseline collection URL.
   */
  apr_hash_t *revnum_to_bc;

  /** A hash containing as keys baseline URL; the values are
   * (baseline_info_t *) structures.
   */
  apr_hash_t *baseline_info;
};

static baseline_info_t *
baseline_info_make(const char *bc_url,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  baseline_info_t *result = apr_palloc(pool, sizeof(*result));

  result->bc_url = apr_pstrdup(pool, bc_url);
  result->revsion = revision;
  
  return result;
}

svn_error_t *
svn_ra_serf__blncache_create(svn_ra_serf__blncache_t **blncache_p,
                             apr_pool_t *pool)
{
    svn_ra_serf__blncache_t *blncache = apr_pcalloc(pool, sizeof(*blncache));

    /* Create subpool for cached data. It will be cleared if we reach maximum
     * cache size.*/
    blncache->cache_pool = svn_pool_create(pool);
    blncache->revnum_to_bc = apr_hash_make(blncache->cache_pool);
    blncache->baseline_info = apr_hash_make(blncache->cache_pool);

    *blncache_p = blncache;

    return SVN_NO_ERROR;
}

/* Helper to copy key and associate value with key in hash table.*/
static void
hash_set_copy(apr_hash_t *hash,
              const void *key,
              apr_ssize_t klen,
              const void *val)
{
    apr_pool_t *pool = apr_hash_pool_get(hash);
    const void *key_copy;

    if (klen == APR_HASH_KEY_STRING)
      {
        key_copy = apr_pstrdup(pool, (const char*) key);
      }
    else
      {
        key_copy = apr_pmemdup(pool, key, klen);
      }

    apr_hash_set(hash, key_copy, klen, val);
}

static void
recycle_cache_if_needed(svn_ra_serf__blncache_t *blncache)
{
  apr_size_t cache_size = 0;

  cache_size += apr_hash_count(blncache->baseline_info);
  cache_size += apr_hash_count(blncache->revnum_to_bc);

  if (cache_size > MAX_CACHE_SIZE)
  {
    /* Clear cache pool and create new hash tables. */
    svn_pool_clear(blncache->cache_pool);
    blncache->revnum_to_bc = apr_hash_make(blncache->cache_pool);
    blncache->baseline_info = apr_hash_make(blncache->cache_pool);
  }
}

svn_error_t *
svn_ra_serf__blncache_set(svn_ra_serf__blncache_t *blncache,
                          const char *baseline_url,
                          svn_revnum_t revision,
                          const char *bc_url,
                          apr_pool_t *pool)
{
  if (bc_url && SVN_IS_VALID_REVNUM(revision))
    {
      recycle_cache_if_needed(blncache);

      hash_set_copy(blncache->revnum_to_bc, &revision, sizeof(revision),
                    apr_pstrdup(blncache->cache_pool, bc_url));

      if (baseline_url)
        {
          hash_set_copy(blncache->baseline_info, baseline_url,
                        APR_HASH_KEY_STRING,
                        baseline_info_make(bc_url, revision,
                                           blncache->cache_pool));
      }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__blncache_get_bc_url(const char **bc_url_p,
                                 svn_ra_serf__blncache_t *blncache,
                                 svn_revnum_t revnum,
                                 apr_pool_t *pool)
{
  const char *value;

  value = apr_hash_get(blncache->revnum_to_bc, &revnum, sizeof(revnum));

  if (value)
    {
      /* Copy baseline collection URL to result pool. */
      *bc_url_p = apr_pstrdup(pool, value);
    }
  else
    {
      *bc_url_p = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__blncache_get_baseline_info(const char **bc_url_p,
                                        svn_revnum_t *revision_p,
                                        svn_ra_serf__blncache_t *blncache,
                                        const char *baseline_url,
                                        apr_pool_t *pool)
{
    baseline_info_t *info;

    info = apr_hash_get(blncache->baseline_info, baseline_url,
                        APR_HASH_KEY_STRING);

    if (info)
      {
        /* Copy baseline collection URL to result pool. */
        *bc_url_p = apr_pstrdup(pool, info->bc_url);
        *revision_p = info->revsion;
      }
    else
      {
        *bc_url_p = NULL;
        *revision_p = SVN_INVALID_REVNUM;
      }

    return SVN_NO_ERROR;
}
