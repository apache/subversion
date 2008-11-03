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
svn_cache__set_error_handler(svn_cache__t *cache,
                             svn_cache__error_handler_t handler,
                             void *baton,
                             apr_pool_t *pool)
{
  cache->error_handler = handler;
  cache->error_baton = baton;
  return SVN_NO_ERROR;
}


/* Give the error handler callback a chance to replace or ignore the
   error. */
static svn_error_t *
handle_error(const svn_cache__t *cache,
             svn_error_t *err,
             apr_pool_t *pool)
{
  if (err && cache->error_handler)
    err = (cache->error_handler)(err, cache->error_baton, pool);
  return err;
}


svn_error_t *
svn_cache__get(void **value_p,
               svn_boolean_t *found,
               const svn_cache__t *cache,
               const void *key,
               apr_pool_t *pool)
{
  /* In case any errors happen and are quelched, make sure we start
     out with FOUND set to false. */
  *found = FALSE;
  return handle_error(cache,
                      (cache->vtable->get)(value_p,
                                           found,
                                           cache->cache_internal,
                                           key,
                                           pool),
                      pool);
}

svn_error_t *
svn_cache__set(svn_cache__t *cache,
               const void *key,
               void *value,
               apr_pool_t *pool)
{
  return handle_error(cache,
                      (cache->vtable->set)(cache->cache_internal,
                                           key,
                                           value,
                                           pool),
                      pool);
}


svn_error_t *
svn_cache__iter(svn_boolean_t *completed,
                const svn_cache__t *cache,
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

