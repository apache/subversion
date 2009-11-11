/*
 * cache.c: cache interface for Subversion
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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

