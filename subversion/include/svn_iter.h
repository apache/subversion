/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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
 * @file svn_iter.h
 * @brief The Subversion Iteration drivers helper routines
 *
 */

#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_pools.h"


/** Callback function for use with svn_iter_apr_hash().
 * Use @a pool for temporary allocation, it's cleared between invocations.
 *
 * @a key, @a klen and @a val are the values normally retrieved with
 * apr_hash_this().
 *
 * @a baton is the baton passed into svn_iter_apr_hash().
 *
 * @since New in 1.5.
 */
typedef svn_error_t *(*svn_iter_apr_hash_cb_t)(void *baton,
                                               const void *key,
                                               apr_ssize_t klen,
                                               void *val, apr_pool_t *pool);

/** Iterate over the elements in @a hash, calling @a func for each one until
 * there are no more elements or @a func returns an error.
 *
 * Uses @a pool for temporary allocations.
 *
 * On return - if @a func returns no errors - @a *completed will be set
 * to @c TRUE.
 *
 * If @a func returns an error other than @c SVN_ERR_ITER_BREAK, that
 * error is returned.  When @a func returns @c SVN_ERR_ITER_BREAK,
 * iteration is interrupted, but no error is returned and @a *completed is
 * set to @c FALSE.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_iter_apr_hash(svn_boolean_t *completed,
                  apr_hash_t *hash,
                  svn_iter_apr_hash_cb_t func,
                  void *baton,
                  apr_pool_t *pool);

/** Iteration callback used in conjuction with svn_iter_apr_array().
 *
 * Use @a pool for temporary allocation, it's cleared between invocations.
 *
 * @a baton is the baton passed to svn_iter_apr_array().  @a item
 * is a pointer to the item written to the array with the APR_ARRAY_PUSH()
 * macro.
 *
 * @since New in 1.5.
 */
typedef svn_error_t *(*svn_iter_apr_array_cb_t)(void *baton,
                                                void *item,
                                                apr_pool_t *pool);

/** Iterate over the elements in @a array calling @a func for each one until
 * there are no more elements or @a func returns an error.
 *
 * Uses @a pool for temporary allocations.
 *
 * On return - if @a func returns no errors - @a *completed will be set
 * to @c TRUE.
 *
 * If @a func returns an error other than @c SVN_ERR_ITER_BREAK, that
 * error is returned.  When @a func returns @c SVN_ERR_ITER_BREAK,
 * iteration is interrupted, but no error is returned and @a *completed is
 * set to @c FALSE.
 *
 * @since New in 1.5.
 */
svn_error_t *
svn_iter_apr_array(svn_boolean_t *completed,
                   const apr_array_header_t *array,
                   svn_iter_apr_array_cb_t func,
                   void *baton,
                   apr_pool_t *pool);


/** Internal routine used by svn_iter_break() macro.
 */
svn_error_t *
svn_iter__break(void);

/** Helper macro to break looping in svn_iter_apr_array() and
 * svn_iter_apr_hash() driven loops.
 *
 * @note The error is just a means of communicating between
 *       driver and callback.  There is no need for it to exist
 *       past the lifetime of the iterpool.
 *
 * @since New in 1.5.
 */
#define svn_iter_break(pool) return svn_iter__break()
