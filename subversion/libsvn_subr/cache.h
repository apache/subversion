/*
 * cache.h: cache vtable interface
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

#ifndef SVN_LIBSVN_SUBR_CACHE_H
#define SVN_LIBSVN_SUBR_CACHE_H

#include "private/svn_cache.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct {
  svn_error_t *(*get)(void **value,
                      svn_boolean_t *found,
                      void *cache_implementation,
                      const void *key,
                      apr_pool_t *pool);

  svn_error_t *(*set)(void *cache_implementation,
                      const void *key,
                      void *value,
                      apr_pool_t *pool);

  svn_error_t *(*iter)(svn_boolean_t *completed,
                       void *cache_implementation,
                       svn_iter_apr_hash_cb_t func,
                       void *baton,
                       apr_pool_t *pool);
} svn_cache__vtable_t;

struct svn_cache__t {
  const svn_cache__vtable_t *vtable;
  svn_cache__error_handler_t error_handler;
  void *error_baton;
  void *cache_internal;
};


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_SUBR_CACHE_H */
