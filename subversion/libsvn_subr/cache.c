/*
 * cache.c: cache interface for Subversion
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

#include "cache.h"

svn_error_t *
svn_cache_get(void **value_p,
              svn_boolean_t *found,
              svn_cache_t *cache,
              const void *key,
              apr_pool_t *pool)
{
  return (cache->vtable->get)(value_p,
                              found,
                              cache->cache_internal,
                              key,
                              pool);
}

svn_error_t *
svn_cache_set(svn_cache_t *cache,
              const void *key,
              void *value,
              apr_pool_t *pool)
{
  return (cache->vtable->set)(cache->cache_internal,
                              key,
                              value,
                              pool);
}



svn_error_t *
svn_cache_iter(svn_boolean_t *completed,
               svn_cache_t *cache,
               svn_iter_apr_hash_cb_t user_cb,
               void *user_baton,
               apr_pool_t *pool)
{
  return (cache->vtable->iter)(completed,
                               cache->cache_internal,
                               user_cb,
                               user_baton,
                               pool);
}

