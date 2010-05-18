/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_cache.h
 * @brief In-memory cache implementation.
 */


#ifndef SVN_CACHE_H
#define SVN_CACHE_H

#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_iter.h"
#include "svn_config.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/**
 * @defgroup svn_cache__support In-memory caching
 * @{
 */

/**
 * A function type for copying an object @a in into a different pool @a pool
 *  and returning the result in @a *out.
 *
 * @since New in 1.6.
*/
typedef svn_error_t *(*svn_cache__dup_func_t)(void **out,
                                              void *in,
                                              apr_pool_t *pool);

/**
 * A function type for deserializing an object @a *out from the string
 * @a data of length @a data_len in the pool @a pool.
*/
typedef svn_error_t *(*svn_cache__deserialize_func_t)(void **out,
                                                      const char *data,
                                                      apr_size_t data_len,
                                                      apr_pool_t *pool);

/**
 * A function type for serializing an object @a in into bytes.  The
 * function should allocate the serialized value in @a pool, set @a
 * *data to the serialized value, and set *data_len to its length.
*/
typedef svn_error_t *(*svn_cache__serialize_func_t)(char **data,
                                                    apr_size_t *data_len,
                                                    void *in,
                                                    apr_pool_t *pool);

/**
 * A function type for transforming or ignoring errors.  @a pool may
 * be used for temporary allocations.
 */
typedef svn_error_t *(*svn_cache__error_handler_t)(svn_error_t *err,
                                                   void *baton,
                                                   apr_pool_t *pool);

/**
 * A wrapper around apr_memcache_t, provided essentially so that the
 * Subversion public API doesn't depend on whether or not you have
 * access to the APR memcache libraries.
 */
typedef struct svn_memcache_t svn_memcache_t;

/**
 * Opaque type for an in-memory cache.
 */
typedef struct svn_cache__t svn_cache__t;

/**
 * Creates a new cache in @a *cache_p.  This cache will use @a pool
 * for all of its storage needs.  The elements in the cache will be
 * indexed by keys of length @a klen, which may be APR_HASH_KEY_STRING
 * if they are strings.  Cached values will be copied in and out of
 * the cache using @a dup_func.
 *
 * The cache stores up to @a pages * @a items_per_page items at a
 * time.  The exact cache invalidation strategy is not defined here,
 * but in general, a lower value for @a items_per_page means more
 * memory overhead for the same number of items, but a higher value
 * for @a items_per_page means more items are cleared at once.  Both
 * @a pages and @a items_per_page must be positive (though they both
 * may certainly be 1).
 *
 * If @a thread_safe is true, and APR is compiled with threads, all
 * accesses to the cache will be protected with a mutex.
 *
 * Note that NULL is a legitimate value for cache entries (and @a dup_func
 * will not be called on it).
 *
 * It is not safe for @a dup_func to interact with the cache itself.
 */
svn_error_t *
svn_cache__create_inprocess(svn_cache__t **cache_p,
                            svn_cache__dup_func_t dup_func,
                            apr_ssize_t klen,
                            apr_int64_t pages,
                            apr_int64_t items_per_page,
                            svn_boolean_t thread_safe,
                            apr_pool_t *pool);
/**
 * Creates a new cache in @a *cache_p, communicating to a memcached
 * process via @a memcache.  The elements in the cache will be indexed
 * by keys of length @a klen, which may be APR_HASH_KEY_STRING if they
 * are strings.  Values will be serialized for memcached using @a
 * serialize_func and deserialized using @a deserialize_func.  Because
 * the same memcached server may cache many different kinds of values,
 * @a prefix should be specified to differentiate this cache from
 * other caches.  @a *cache_p will be allocated in @a pool.
 *
 * If @a deserialize_func is NULL, then the data is returned as an
 * svn_string_t; if @a serialize_func is NULL, then the data is
 * assumed to be an svn_stringbuf_t.
 *
 * These caches are always thread safe.
 *
 * These caches do not support svn_cache__iter.
 *
 * If Subversion was not built with apr_memcache support, always
 * raises SVN_ERR_NO_APR_MEMCACHE.
 */
svn_error_t *
svn_cache__create_memcache(svn_cache__t **cache_p,
                           svn_memcache_t *memcache,
                           svn_cache__serialize_func_t serialize_func,
                           svn_cache__deserialize_func_t deserialize_func,
                           apr_ssize_t klen,
                           const char *prefix,
                           apr_pool_t *pool);

/**
 * Given @a config, returns an APR memcached interface in @a
 * *memcache_p allocated in @a pool if @a config contains entries in
 * the SVN_CACHE_CONFIG_CATEGORY_MEMCACHED_SERVERS section describing
 * memcached servers; otherwise, sets @a *memcache_p to NULL.
 *
 * If Subversion was not built with apr_memcache_support, then raises
 * SVN_ERR_NO_APR_MEMCACHE if and only if @a config is configured to
 * use memcache.
 */
svn_error_t *
svn_cache__make_memcache_from_config(svn_memcache_t **memcache_p,
                                     svn_config_t *config,
                                     apr_pool_t *pool);

/**
 * Sets @a handler to be @a cache's error handling routine.  If any
 * error is returned from a call to svn_cache__get or svn_cache__set, @a
 * handler will be called with @a baton and the error, and the
 * original function will return whatever error @a handler returns
 * instead (possibly SVN_NO_ERROR); @a handler will receive the pool
 * passed to the svn_cache_* function.  @a pool is used for temporary
 * allocations.
 */
svn_error_t *
svn_cache__set_error_handler(svn_cache__t *cache,
                             svn_cache__error_handler_t handler,
                             void *baton,
                             apr_pool_t *pool);


#define SVN_CACHE_CONFIG_CATEGORY_MEMCACHED_SERVERS "memcached-servers"

/**
 * Fetches a value indexed by @a key from @a cache into @a *value,
 * setting @a *found to TRUE iff it is in the cache and FALSE if it is
 * not found.  The value is copied into @a pool using the copy
 * function provided to the cache's constructor.
 */
svn_error_t *
svn_cache__get(void **value,
               svn_boolean_t *found,
               const svn_cache__t *cache,
               const void *key,
               apr_pool_t *pool);

/**
 * Stores the value @a value under the key @a key in @a cache.  @a pool
 * is used only for temporary allocations.  The cache makes copies of
 * @a key and @a value if necessary (that is, @a key and @a value may
 * have shorter lifetimes than the cache).
 *
 * If there is already a value for @a key, this will replace it.  Bear
 * in mind that in some circumstances this may leak memory (that is,
 * the cache's copy of the previous value may not be immediately
 * cleared); it is only guaranteed to not leak for caches created with
 * @a items_per_page equal to 1.
 */
svn_error_t *
svn_cache__set(svn_cache__t *cache,
               const void *key,
               void *value,
               apr_pool_t *pool);

/**
 * Iterates over the elements currently in @a cache, calling @a func
 * for each one until there are no more elements or @a func returns an
 * error.  Uses @a pool for temporary allocations.
 *
 * If @a completed is not NULL, then on return - if @a func returns no
 * errors - @a *completed will be set to @c TRUE.
 *
 * If @a func returns an error other than @c SVN_ERR_ITER_BREAK, that
 * error is returned.  When @a func returns @c SVN_ERR_ITER_BREAK,
 * iteration is interrupted, but no error is returned and @a
 * *completed is set to @c FALSE.  (The error handler set by
 * svn_cache__set_error_handler is not used for svn_cache__iter.)
 *
 * It is not legal to perform any other cache operations on @a cache
 * inside @a func.
 *
 * svn_cache__iter is not supported by all cache implementations; see
 * the svn_cache__create_* function for details.
 */
svn_error_t *
svn_cache__iter(svn_boolean_t *completed,
                const svn_cache__t *cache,
                svn_iter_apr_hash_cb_t func,
                void *baton,
                apr_pool_t *pool);
/** @} */


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CACHE_H */
