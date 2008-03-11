/*
 * cache.c: in-memory caching for Subversion
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

#include "svn_cache.h"

svn_error_t *
svn_cache_create(svn_cache_t **cache_p,
                 svn_cache_dup_func_t dup,
                 apr_ssize_t klen,
                 int pages,
                 int items_per_page,
                 svn_boolean_t thread_safe,
                 apr_pool_t *pool)
{
  /* ### TODO: implement */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_cache_get(void **value,
              svn_boolean_t *found,
              svn_cache_t *cache,
              const void *key,
              apr_pool_t *pool)
{
  /* ### TODO: implement */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_cache_set(svn_cache_t *cache,
              const void *key,
              void *value,
              apr_pool_t *pool)
{
  /* ### TODO: implement */
  return SVN_NO_ERROR;
}
