/*
 * cache-test.c -- test the in-memory cache
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

#include <stdio.h>
#include <string.h>
#include <apr_general.h>
#include <apr_lib.h>
#include <apr_time.h>

#include "svn_pools.h"

#include "private/svn_cache.h"
#include "svn_private_config.h"

#include "../svn_test.h"

/* Create memcached cache if configured */
static svn_error_t *
create_memcache(svn_memcache_t **memcache,
                const svn_test_opts_t *opts,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  svn_config_t *config = NULL;
  if (opts->config_file)
    {
      SVN_ERR(svn_config_read3(&config, opts->config_file,
                               TRUE, FALSE, FALSE, scratch_pool));
    }
  else if (opts->memcached_server)
    {
      SVN_ERR(svn_config_create2(&config, FALSE, FALSE, scratch_pool));

      svn_config_set(config, SVN_CACHE_CONFIG_CATEGORY_MEMCACHED_SERVERS,
                     "key" /* some value; ignored*/,
                     opts->memcached_server);
    }

  if (config)
    {
      SVN_ERR(svn_cache__make_memcache_from_config(memcache, config,
                                                   result_pool, scratch_pool));
    }
  else
    *memcache = NULL;

  return SVN_NO_ERROR;
}

/* Implements svn_cache__serialize_func_t */
static svn_error_t *
serialize_revnum(void **data,
                 apr_size_t *data_len,
                 void *in,
                 apr_pool_t *pool)
{
  *data_len = sizeof(svn_revnum_t);
  *data = apr_pmemdup(pool, in, *data_len);

  return SVN_NO_ERROR;
}


/* Implements svn_cache__deserialize_func_t */
static svn_error_t *
deserialize_revnum(void **out,
                   void *data,
                   apr_size_t data_len,
                   apr_pool_t *pool)
{
  const svn_revnum_t *in_rev = (const svn_revnum_t *) data;
  svn_revnum_t *out_rev;

  if (data_len != sizeof(*in_rev))
    return svn_error_create(SVN_ERR_REVNUM_PARSE_FAILURE, NULL,
                            _("Bad size for revision number in cache"));
  out_rev = apr_palloc(pool, sizeof(*out_rev));
  *out_rev = *in_rev;
  *out = out_rev;
  return SVN_NO_ERROR;
}

static svn_error_t *
basic_cache_test(svn_cache__t *cache,
                 svn_boolean_t size_is_one,
                 apr_pool_t *pool)
{
  svn_boolean_t found;
  svn_revnum_t twenty = 20, thirty = 30, *answer;
  apr_pool_t *subpool;

  /* We use a subpool for all calls in this test and aggressively
   * clear it, to try to find any bugs where the cached values aren't
   * actually saved away in the cache's pools. */
  subpool = svn_pool_create(pool);

  SVN_ERR(svn_cache__get((void **) &answer, &found, cache, "twenty", subpool));
  if (found)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "cache found an entry that wasn't there");
  svn_pool_clear(subpool);

  SVN_ERR(svn_cache__set(cache, "twenty", &twenty, subpool));
  svn_pool_clear(subpool);

  SVN_ERR(svn_cache__get((void **) &answer, &found, cache, "twenty", subpool));
  if (! found)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "cache failed to find entry for 'twenty'");
  if (*answer != 20)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "expected 20 but found '%ld'", *answer);
  svn_pool_clear(subpool);

  SVN_ERR(svn_cache__set(cache, "thirty", &thirty, subpool));
  svn_pool_clear(subpool);

  SVN_ERR(svn_cache__get((void **) &answer, &found, cache, "thirty", subpool));
  if (! found)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "cache failed to find entry for 'thirty'");
  if (*answer != 30)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "expected 30 but found '%ld'", *answer);

  if (size_is_one)
    {
      SVN_ERR(svn_cache__get((void **) &answer, &found, cache, "twenty", subpool));
      if (found)
        return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                                "cache found entry for 'twenty' that should have "
                                "expired");
    }
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_inprocess_cache_basic(apr_pool_t *pool)
{
  svn_cache__t *cache;

  /* Create a cache with just one entry. */
  SVN_ERR(svn_cache__create_inprocess(&cache,
                                      serialize_revnum,
                                      deserialize_revnum,
                                      APR_HASH_KEY_STRING,
                                      1,
                                      1,
                                      TRUE,
                                      "",
                                      pool));

  return basic_cache_test(cache, TRUE, pool);
}

static svn_error_t *
test_memcache_basic(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_cache__t *cache;
  svn_memcache_t *memcache = NULL;
  const char *prefix = apr_psprintf(pool,
                                    "test_memcache_basic-%" APR_TIME_T_FMT,
                                    apr_time_now());

  SVN_ERR(create_memcache(&memcache, opts, pool, pool));
  if (! memcache)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "not configured to use memcached");


  /* Create a memcache-based cache. */
  SVN_ERR(svn_cache__create_memcache(&cache,
                                    memcache,
                                    serialize_revnum,
                                    deserialize_revnum,
                                    APR_HASH_KEY_STRING,
                                    prefix,
                                    pool));

  return basic_cache_test(cache, FALSE, pool);
}

static svn_error_t *
test_membuffer_cache_basic(apr_pool_t *pool)
{
  svn_cache__t *cache;
  svn_membuffer_t *membuffer;

  SVN_ERR(svn_cache__membuffer_cache_create(&membuffer, 10*1024, 1, 0,
                                            TRUE, TRUE, pool));

  /* Create a cache with just one entry. */
  SVN_ERR(svn_cache__create_membuffer_cache(&cache,
                                            membuffer,
                                            serialize_revnum,
                                            deserialize_revnum,
                                            APR_HASH_KEY_STRING,
                                            "cache:",
                                            SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY,
                                            FALSE,
                                            FALSE,
                                            pool, pool));

  return basic_cache_test(cache, FALSE, pool);
}

/* Implements svn_cache__deserialize_func_t */
static svn_error_t *
raise_error_deserialize_func(void **out,
                             void *data,
                             apr_size_t data_len,
                             apr_pool_t *pool)
{
  return svn_error_create(APR_EGENERAL, NULL, NULL);
}

/* Implements svn_cache__partial_getter_func_t */
static svn_error_t *
raise_error_partial_getter_func(void **out,
                                const void *data,
                                apr_size_t data_len,
                                void *baton,
                                apr_pool_t *result_pool)
{
  return svn_error_create(APR_EGENERAL, NULL, NULL);
}

/* Implements svn_cache__partial_setter_func_t */
static svn_error_t *
raise_error_partial_setter_func(void **data,
                                apr_size_t *data_len,
                                void *baton,
                                apr_pool_t *result_pool)
{
  return svn_error_create(APR_EGENERAL, NULL, NULL);
}

static svn_error_t *
test_membuffer_serializer_error_handling(apr_pool_t *pool)
{
  svn_cache__t *cache;
  svn_membuffer_t *membuffer;
  svn_revnum_t twenty = 20;
  svn_boolean_t found;
  void *val;

  SVN_ERR(svn_cache__membuffer_cache_create(&membuffer, 10*1024, 1, 0,
                                            TRUE, TRUE, pool));

  /* Create a cache with just one entry. */
  SVN_ERR(svn_cache__create_membuffer_cache(&cache,
                                            membuffer,
                                            serialize_revnum,
                                            raise_error_deserialize_func,
                                            APR_HASH_KEY_STRING,
                                            "cache:",
                                            SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY,
                                            FALSE,
                                            FALSE,
                                            pool, pool));

  SVN_ERR(svn_cache__set(cache, "twenty", &twenty, pool));

  /* Test retrieving data from cache using full getter that
     always raises an error. */
  SVN_TEST_ASSERT_ERROR(
    svn_cache__get(&val, &found, cache, "twenty", pool),
    APR_EGENERAL);

  /* Test retrieving data from cache using partial getter that
     always raises an error. */
  SVN_TEST_ASSERT_ERROR(
    svn_cache__get_partial(&val, &found, cache, "twenty",
                           raise_error_partial_getter_func,
                           NULL, pool),
    APR_EGENERAL);

  /* Create a new cache. */
  SVN_ERR(svn_cache__membuffer_cache_create(&membuffer, 10*1024, 1, 0,
                                            TRUE, TRUE, pool));
  SVN_ERR(svn_cache__create_membuffer_cache(&cache,
                                            membuffer,
                                            serialize_revnum,
                                            deserialize_revnum,
                                            APR_HASH_KEY_STRING,
                                            "cache:",
                                            SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY,
                                            FALSE,
                                            FALSE,
                                            pool, pool));

  /* Store one entry in cache. */
  SVN_ERR(svn_cache__set(cache, "twenty", &twenty, pool));

  /* Test setting data in cache using partial setter that
     always raises an error. */
  SVN_TEST_ASSERT_ERROR(
    svn_cache__set_partial(cache, "twenty",
                           raise_error_partial_setter_func,
                           NULL, pool),
    APR_EGENERAL);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_memcache_long_key(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  svn_cache__t *cache;
  svn_memcache_t *memcache = NULL;
  svn_revnum_t fifty = 50, *answer;
  svn_boolean_t found = FALSE;
  const char *prefix = apr_psprintf(pool,
                                    "test_memcache_long_key-%" APR_TIME_T_FMT,
                                    apr_time_now());
  static const char *long_key =
    "0123456789" "0123456789" "0123456789" "0123456789" "0123456789" /* 50 */
    "0123456789" "0123456789" "0123456789" "0123456789" "0123456789" /* 100 */
    "0123456789" "0123456789" "0123456789" "0123456789" "0123456789" /* 150 */
    "0123456789" "0123456789" "0123456789" "0123456789" "0123456789" /* 200 */
    "0123456789" "0123456789" "0123456789" "0123456789" "0123456789" /* 250 */
    "0123456789" "0123456789" "0123456789" "0123456789" "0123456789" /* 300 */
    ;

  SVN_ERR(create_memcache(&memcache, opts, pool, pool));

  if (! memcache)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "not configured to use memcached");


  /* Create a memcache-based cache. */
  SVN_ERR(svn_cache__create_memcache(&cache,
                                    memcache,
                                    serialize_revnum,
                                    deserialize_revnum,
                                    APR_HASH_KEY_STRING,
                                    prefix,
                                    pool));

  SVN_ERR(svn_cache__set(cache, long_key, &fifty, pool));
  SVN_ERR(svn_cache__get((void **) &answer, &found, cache, long_key, pool));

  if (! found)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "cache failed to find entry for 'fifty'");
  if (*answer != 50)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "expected 50 but found '%ld'", *answer);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_membuffer_cache_clearing(apr_pool_t *pool)
{
  svn_cache__t *cache;
  svn_membuffer_t *membuffer;
  svn_boolean_t found;
  svn_revnum_t *value;
  svn_revnum_t valueA = 12345;
  svn_revnum_t valueB = 67890;

  /* Create a simple cache for strings, keyed by strings. */
  SVN_ERR(svn_cache__membuffer_cache_create(&membuffer, 10*1024, 1, 0,
                                            TRUE, TRUE, pool));
  SVN_ERR(svn_cache__create_membuffer_cache(&cache,
                                            membuffer,
                                            serialize_revnum,
                                            deserialize_revnum,
                                            APR_HASH_KEY_STRING,
                                            "cache:",
                                            SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY,
                                            FALSE,
                                            FALSE,
                                            pool, pool));

  /* Initially, the cache is empty. */
  SVN_ERR(svn_cache__get((void **) &value, &found, cache, "key A", pool));
  SVN_TEST_ASSERT(!found);
  SVN_ERR(svn_cache__get((void **) &value, &found, cache, "key B", pool));
  SVN_TEST_ASSERT(!found);
  SVN_ERR(svn_cache__get((void **) &value, &found, cache, "key C", pool));
  SVN_TEST_ASSERT(!found);

  /* Add entries. */
  SVN_ERR(svn_cache__set(cache, "key A", &valueA, pool));
  SVN_ERR(svn_cache__set(cache, "key B", &valueB, pool));

  /* Added entries should be cached (too small to get evicted already). */
  SVN_ERR(svn_cache__get((void **) &value, &found, cache, "key A", pool));
  SVN_TEST_ASSERT(found);
  SVN_TEST_ASSERT(*value == valueA);
  SVN_ERR(svn_cache__get((void **) &value, &found, cache, "key B", pool));
  SVN_TEST_ASSERT(found);
  SVN_TEST_ASSERT(*value == valueB);
  SVN_ERR(svn_cache__get((void **) &value, &found, cache, "key C", pool));
  SVN_TEST_ASSERT(!found);

  /* Clear the cache. */
  SVN_ERR(svn_cache__membuffer_clear(membuffer));

  /* The cache is empty again. */
  SVN_ERR(svn_cache__get((void **) &value, &found, cache, "key A", pool));
  SVN_TEST_ASSERT(!found);
  SVN_ERR(svn_cache__get((void **) &value, &found, cache, "key B", pool));
  SVN_TEST_ASSERT(!found);
  SVN_ERR(svn_cache__get((void **) &value, &found, cache, "key C", pool));
  SVN_TEST_ASSERT(!found);

  /* But still functional: */
  SVN_ERR(svn_cache__set(cache, "key B", &valueB, pool));
  SVN_ERR(svn_cache__has_key(&found, cache, "key A", pool));
  SVN_TEST_ASSERT(!found);
  SVN_ERR(svn_cache__has_key(&found, cache, "key B", pool));
  SVN_TEST_ASSERT(found);
  SVN_ERR(svn_cache__has_key(&found, cache, "key C", pool));
  SVN_TEST_ASSERT(!found);

  return SVN_NO_ERROR;
}

/* Implements svn_iter_apr_hash_cb_t. */
static svn_error_t *
null_cache_iter_func(void *baton,
                     const void *key,
                     apr_ssize_t klen,
                     void *val,
                     apr_pool_t *pool)
{
  /* shall never be called */
  return svn_error_create(SVN_ERR_TEST_FAILED, NULL, "should not be called");
}

static svn_error_t *
test_null_cache(apr_pool_t *pool)
{
  svn_boolean_t found, done;
  int *data = NULL;
  svn_cache__info_t info;

  svn_cache__t *cache;
  SVN_ERR(svn_cache__create_null(&cache, "test-dummy", pool));

  /* Can't cache anything. */
  SVN_TEST_ASSERT(svn_cache__is_cachable(cache, 0) == FALSE);
  SVN_TEST_ASSERT(svn_cache__is_cachable(cache, 1) == FALSE);

  /* No point in adding data. */
  SVN_ERR(svn_cache__set(cache, "data", &data, pool));
  SVN_ERR(svn_cache__get((void **)&data, &found, cache, "data", pool));
  SVN_TEST_ASSERT(found == FALSE);

  SVN_ERR(svn_cache__has_key(&found, cache, "data", pool));
  SVN_TEST_ASSERT(found == FALSE);

  /* Iteration "works" but is a no-op. */
  SVN_ERR(svn_cache__iter(&done, cache, null_cache_iter_func, NULL, pool));
  SVN_TEST_ASSERT(done);

  /* It shall know its name. */
  SVN_ERR(svn_cache__get_info(cache, &info, TRUE, pool));
  SVN_TEST_STRING_ASSERT(info.id, "test-dummy");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_membuffer_unaligned_string_keys(apr_pool_t *pool)
{
  svn_cache__t *cache;
  svn_membuffer_t *membuffer;
  svn_revnum_t fifty = 50;
  svn_revnum_t *answer;
  svn_boolean_t found = FALSE;

  /* Allocate explicitly to have aligned string and this add one
   * to have unaligned string.*/
  const char *aligned_key = apr_pstrdup(pool, "fifty");
  const char *unaligned_key = apr_pstrdup(pool, "_fifty") + 1;
  const char *unaligned_prefix = apr_pstrdup(pool, "_cache:") + 1;

  SVN_ERR(svn_cache__membuffer_cache_create(&membuffer, 10*1024, 1, 0,
                                            TRUE, TRUE, pool));

  /* Create a cache with just one entry. */
  SVN_ERR(svn_cache__create_membuffer_cache(
            &cache, membuffer, serialize_revnum, deserialize_revnum,
            APR_HASH_KEY_STRING, unaligned_prefix,
            SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY, FALSE, FALSE,
            pool, pool));

  SVN_ERR(svn_cache__set(cache, unaligned_key, &fifty, pool));
  SVN_ERR(svn_cache__get((void **) &answer, &found, cache, unaligned_key,
                         pool));

  if (! found)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "cache failed to find entry for 'fifty'");
  if (*answer != 50)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "expected 50 but found '%ld'", *answer);

  /* Make sure that we get proper result when providing aligned key*/
  SVN_ERR(svn_cache__get((void **) &answer, &found, cache, aligned_key,
                         pool));

  if (! found)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "cache failed to find entry for 'fifty'");
  if (*answer != 50)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "expected 50 but found '%ld'", *answer);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_membuffer_unaligned_fixed_keys(apr_pool_t *pool)
{
  svn_cache__t *cache;
  svn_membuffer_t *membuffer;
  svn_revnum_t fifty = 50;
  svn_revnum_t *answer;
  svn_boolean_t found = FALSE;

  /* Allocate explicitly to have aligned string and this add one
   * to have unaligned key.*/
  const char *aligned_key = apr_pstrdup(pool, "12345678");
  const char *unaligned_key = apr_pstrdup(pool, "_12345678") + 1;
  const char *unaligned_prefix = apr_pstrdup(pool, "_cache:") + 1;

  SVN_ERR(svn_cache__membuffer_cache_create(&membuffer, 10*1024, 1, 0,
                                            TRUE, TRUE, pool));

  /* Create a cache with just one entry. */
  SVN_ERR(svn_cache__create_membuffer_cache(
            &cache, membuffer, serialize_revnum, deserialize_revnum,
            8 /* klen*/,
            unaligned_prefix,
            SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY, FALSE, FALSE,
            pool, pool));

  SVN_ERR(svn_cache__set(cache, unaligned_key, &fifty, pool));
  SVN_ERR(svn_cache__get((void **) &answer, &found, cache, unaligned_key,
                         pool));

  if (! found)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "cache failed to find entry for '12345678' (unaligned)");
  if (*answer != 50)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "expected 50 but found '%ld'", *answer);

  /* Make sure that we get proper result when providing aligned key*/
  SVN_ERR(svn_cache__get((void **) &answer, &found, cache, aligned_key,
                         pool));

  if (! found)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "cache failed to find entry for '12345678' (aligned)");
  if (*answer != 50)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "expected 50 but found '%ld'", *answer);

  return SVN_NO_ERROR;
}


/* The test table.  */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_inprocess_cache_basic,
                   "basic inprocess svn_cache test"),
    SVN_TEST_OPTS_PASS(test_memcache_basic,
                       "basic memcache svn_cache test"),
    SVN_TEST_OPTS_PASS(test_memcache_long_key,
                       "memcache svn_cache with very long keys"),
    SVN_TEST_PASS2(test_membuffer_cache_basic,
                   "basic membuffer svn_cache test"),
    SVN_TEST_PASS2(test_membuffer_serializer_error_handling,
                   "test for error handling in membuffer svn_cache"),
    SVN_TEST_PASS2(test_membuffer_cache_clearing,
                   "test clearing a membuffer svn_cache"),
    SVN_TEST_PASS2(test_null_cache,
                   "basic null svn_cache test"),
    SVN_TEST_PASS2(test_membuffer_unaligned_string_keys,
                   "test membuffer cache with unaligned string keys"),
    SVN_TEST_PASS2(test_membuffer_unaligned_fixed_keys,
                   "test membuffer cache with unaligned fixed keys"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
