/*
 * cache-memcache.c: memcached caching for Subversion
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#include <assert.h>

#include "svn_private_config.h"

#if SVN_HAVE_MEMCACHE

#include <apr_memcache.h>

#include "svn_pools.h"
#include "svn_base64.h"
#include "svn_path.h"

#include "cache.h"

/* A note on thread safety:

   The apr_memcache_t object does its own mutex handling, and nothing
   else in memcache_t is ever modified, so this implementation should
   be fully thread-safe.
*/

/* The (internal) cache object. */
typedef struct {
  /* The memcached server set we're using. */
  apr_memcache_t *memcache;

  /* A prefix used to differentiate our data from any other data in
   * the memcached (URI-encoded). */
  const char *prefix;

  /* The size of the key: either a fixed number of bytes or
   * APR_HASH_KEY_STRING. */
  apr_ssize_t klen;


  /* Used to marshal values in and out of the cache. */
  svn_cache_serialize_func_t *serialize_func;
  svn_cache_deserialize_func_t *deserialize_func;
} memcache_t;


/* Returns a memcache key for the given key KEY for CACHE, allocated
   in POOL. */
const char *
build_key(memcache_t *cache,
          const void *raw_key,
          apr_pool_t *pool)
{
  const char *encoded_suffix;

  if (cache->klen == APR_HASH_KEY_STRING)
    encoded_suffix = svn_path_uri_encode(raw_key, pool);
  else
    {
      const svn_string_t *raw = svn_string_ncreate(raw_key, cache->klen, pool);
      const svn_string_t *encoded = svn_base64_encode_string2(raw, FALSE,
                                                              pool);
      encoded_suffix = encoded->data;
    }

  return apr_pstrcat(pool, "SVN:", cache->prefix, ":", encoded_suffix, NULL);
}


svn_error_t *
memcache_get(void **value_p,
             svn_boolean_t *found,
             void *cache_void,
             const void *key,
             apr_pool_t *pool)
{
  memcache_t *cache = cache_void;
  apr_status_t apr_err;
  char *data;
  const char *mc_key;
  apr_size_t data_len;
  apr_pool_t *subpool = svn_pool_create(pool);

  mc_key = build_key(cache, key, subpool);

  apr_err = apr_memcache_getp(cache->memcache,
                              subpool,
                              mc_key,
                              &data,
                              &data_len,
                              NULL /* ignore flags */);
  if (apr_err == APR_NOTFOUND)
    {
      *found = FALSE;
      return SVN_NO_ERROR;
    }
  else if (apr_err != APR_SUCCESS || !data)
    return svn_error_wrap_apr(apr_err,
                              _("Unknown memcached error while reading"));

  /* We found it! */
  SVN_ERR((cache->deserialize_func)(value_p, data, data_len, pool));
  *found = TRUE;

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


svn_error_t *
memcache_set(void *cache_void,
             const void *key,
             void *value,
             apr_pool_t *pool)
{
  memcache_t *cache = cache_void;
  apr_pool_t *subpool = svn_pool_create(pool);
  char *data;
  const char *mc_key = build_key(cache, key, subpool);
  apr_size_t data_len;
  apr_status_t apr_err;

  SVN_ERR((cache->serialize_func)(&data, &data_len, value, subpool));

  apr_err = apr_memcache_set(cache->memcache, mc_key, data, data_len, 0, 0);

  /* ### Maybe write failures should be ignored (but logged)? */
  if (apr_err != APR_SUCCESS)
    return svn_error_wrap_apr(apr_err,
                              _("Unknown memcached error while writing"));

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


svn_error_t *
memcache_iter(svn_boolean_t *completed,
              void *cache_void,
              svn_iter_apr_hash_cb_t user_cb,
              void *user_baton,
              apr_pool_t *pool)
{
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                          _("Can't iterate a memcached cache."));
}

static svn_cache__vtable_t memcache_vtable = {
  memcache_get,
  memcache_set,
  memcache_iter
};

svn_error_t *
svn_cache_create_memcache(svn_cache_t **cache_p,
                          apr_memcache_t *memcache,
                          svn_cache_serialize_func_t *serialize_func,
                          svn_cache_deserialize_func_t *deserialize_func,
                          apr_ssize_t klen,
                          const char *prefix,
                          apr_pool_t *pool)
{
  svn_cache_t *wrapper = apr_pcalloc(pool, sizeof(*wrapper));
  memcache_t *cache = apr_pcalloc(pool, sizeof(*cache));

  cache->serialize_func = serialize_func;
  cache->deserialize_func = deserialize_func;
  cache->klen = klen;
  cache->prefix = svn_path_uri_encode(prefix, pool);
  cache->memcache = memcache;

  wrapper->vtable = &memcache_vtable;
  wrapper->cache_internal = cache;
  wrapper->error_handler = wrapper->error_baton = NULL;

  *cache_p = wrapper;
  return SVN_NO_ERROR;
}


/*** Creating apr_memcache_t from svn_config_t. ***/

/* Baton for add_memcache_server. */
struct ams_baton {
  apr_memcache_t *memcache;
  apr_pool_t *memcache_pool;
  svn_error_t *err;
};

/* Implements svn_config_enumerator2_t. */
static svn_boolean_t
add_memcache_server(const char *name,
                    const char *value,
                    void *baton,
                    apr_pool_t *pool)
{
  struct ams_baton *b = baton;
  char *host, *scope;
  apr_port_t port;
  apr_status_t apr_err;
  apr_memcache_server_t *server;

  apr_err = apr_parse_addr_port(&host, &scope, &port,
                                value, pool);
  if (apr_err != APR_SUCCESS)
    {
      b->err = svn_error_wrap_apr(apr_err,
                                  _("Error parsing memcache server '%s'"),
                                  name);
      return FALSE;
    }

  if (scope)
    {
      b->err = svn_error_createf(SVN_ERR_BAD_SERVER_SPECIFICATION, NULL,
                                  _("Scope not allowed in memcache server "
                                    "'%s'"),
                                  name);
      return FALSE;
    }
  if (!host || !port)
    {
      b->err = svn_error_createf(SVN_ERR_BAD_SERVER_SPECIFICATION, NULL,
                                  _("Must specify host and port for memcache "
                                    "server '%s'"),
                                  name);
      return FALSE;
    }

  /* Note: the four numbers here are only relevant when an
     apr_memcache_t is being shared by multiple threads. */
  apr_err = apr_memcache_server_create(b->memcache_pool,
                                       host,
                                       port,
                                       0,  /* min connections */
                                       5,  /* soft max connections */
                                       10, /* hard max connections */
                                       50, /* connection time to live (secs) */
                                       &server);
  if (apr_err != APR_SUCCESS)
    {
      b->err = svn_error_wrap_apr(apr_err,
                                  _("Unknown error creating memcache server"));
      return FALSE;
    }

  apr_err = apr_memcache_add_server(b->memcache, server);
  if (apr_err != APR_SUCCESS)
    {
      b->err = svn_error_wrap_apr(apr_err,
                                  _("Unknown error adding server to memcache"));
      return FALSE;
    }

  return TRUE;
}

/* Implements svn_config_enumerator2_t.  Just used for the
   entry-counting return value of svn_config_enumerate2. */
static svn_boolean_t
nop_enumerator(const char *name,
               const char *value,
               void *baton,
               apr_pool_t *pool)
{
  return TRUE;
}

svn_error_t *
svn_cache_make_memcache_from_config(apr_memcache_t **memcache_p,
                                    svn_config_t *config,
                                    apr_pool_t *pool)
{
  apr_memcache_t *memcache;
  apr_status_t apr_err;
  apr_uint16_t server_count;
  struct ams_baton b;
  apr_pool_t *subpool = svn_pool_create(pool);

  server_count =
    svn_config_enumerate2(config,
                          SVN_CACHE_CONFIG_CATEGORY_MEMCACHED_SERVERS,
                          nop_enumerator, NULL, subpool);

  if (server_count == 0)
    {
      *memcache_p = NULL;
      svn_pool_destroy(subpool);
      return SVN_NO_ERROR;
    }

  apr_err = apr_memcache_create(pool,
                                server_count,
                                0, /* flags */
                                &memcache);
  if (apr_err != APR_SUCCESS)
    return svn_error_wrap_apr(apr_err,
                              _("Unknown error creating apr_memcache_t"));

  b.memcache = memcache;
  b.memcache_pool = pool;
  b.err = SVN_NO_ERROR;
  svn_config_enumerate2(config,
                        SVN_CACHE_CONFIG_CATEGORY_MEMCACHED_SERVERS,
                        add_memcache_server, &b,
                        subpool);

  if (b.err)
    return b.err;

  *memcache_p = memcache;

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;

}

#endif
