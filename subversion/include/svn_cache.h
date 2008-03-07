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


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/**
 * @defgroup svn_cache_support In-memory caching
 * @{
 */

/** A function type for copying an object @a in into a different pool @pool
 *  and returning the result in @a *out. */
typedef svn_error_t *(svn_cache_dup_func_t)(void **out,
                                            void *in,
                                            apr_pool_t *pool);

/** Opaque type for an in-memory cache. */
typedef struct svn_cache_t svn_cache_t;

/**
 * Creates a new cache in @a *cache_p.  This cache will use @a pool
 * for all of its storage needs.  The elements in the cache will be
 * indexed by keys of length @a klen, which may be APR_HASH_KEY_STRING
 * if they are strings.  Cached values will be copied in and out of
 * the cache using @a dup.
 *
 * The cache stores up to @a pages * @a items_per_page items at a
 * time.  The exact cache invalidation strategy is not defined here,
 * but in general, a lower value for @a items_per_page means more
 * memory overhead for the same number of items, but a higher value
 * for @a items_per_page means more items are cleared at once.  Both
 * @a pages and @a items_per_page must be positive (though they both
 * may certainly be 1).
 *
 * Note that NULL is a legitimate value for cache entries (and @a dup
 * will not be called on it).
 */
svn_error_t *
svn_cache_create(svn_cache_t **cache_p,
                 svn_cache_dup_func_t dup,
                 apr_ssize_t klen,
                 int pages,
                 int items_per_page,
                 apr_pool_t *pool);

/**
 * Fetches a value indexed by @a key from @a cache into @a *value,
 * setting @a found to TRUE iff it is in the cache.  The value is
 * copied into @a pool using the copy function provided to the cache's
 * constructor.
 */
svn_error_t *
svn_cache_get(void **value,
              svn_boolean_t *found,
              svn_cache_t *cache,
              void *key,
              apr_pool_t *pool);

/**
 * Stores the value @value under the key @a key in @a cache.  @a pool
 * is used only for temporary allocations.  The cache makes copies of
 * @a key and @a value if necessary (that is, @a key and @a value may
 * have shorter lifetimes than the cache).
 *
 * If there is already a value for @a key, this will replace it; bear
 * in mind that in some circumstances this may leak memory (that is,
 * the cache's copy of the previous value may not be immediately
 * cleared).
 */
svn_error_t *
svn_cache_set(svn_cache_t *cache,
              void *key,
              void *value,
              apr_pool_t *pool);
/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CACHE_H */
