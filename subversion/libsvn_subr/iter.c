/* iter.c : iteration drivers
 *
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
 */


#include "svn_iter.h"
#include "svn_pools.h"

#include "svn_error_codes.h"

static svn_error_t internal_break_error =
  {
    SVN_ERR_ITER_BREAK, /* APR status */
    NULL, /* message */
    NULL, /* child error */
    NULL, /* pool */
    __FILE__, /* file name */
    __LINE__ /* line number */
  };

svn_error_t *
svn_iter_apr_hash(svn_boolean_t *completed,
                  apr_hash_t *hash,
                  svn_iter_apr_hash_cb_t func,
                  void *baton,
                  apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, hash);
       ! err && hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      apr_ssize_t len;

      svn_pool_clear(iterpool);

      apr_hash_this(hi, &key, &len, &val);
      err = (*func)(baton, key, len, val, iterpool);
    }

  if (completed)
    *completed = ! err;

  if (err && err->apr_err == SVN_ERR_ITER_BREAK)
    {
      if (err != &internal_break_error)
        /* Errors - except those created by svn_iter_break() -
           need to be cleared when not further propagated. */
        svn_error_clear(err);

      err = SVN_NO_ERROR;
    }

  /* Clear iterpool, because callers may clear the error but have no way
     to clear the iterpool with potentially lots of allocated memory */
  svn_pool_destroy(iterpool);

  return err;
}

svn_error_t *
svn_iter_apr_array(svn_boolean_t *completed,
                   const apr_array_header_t *array,
                   svn_iter_apr_array_cb_t func,
                   void *baton,
                   apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i;

  for (i = 0; (! err) && i < array->nelts; ++i)
    {
      void *item = array->elts + array->elt_size*i;

      svn_pool_clear(iterpool);

      err = (*func)(baton, item, pool);
    }

  if (completed)
    *completed = ! err;

  if (err && err->apr_err == SVN_ERR_ITER_BREAK)
    {
      if (err != &internal_break_error)
        /* Errors - except those created by svn_iter_break() -
           need to be cleared when not further propagated. */
        svn_error_clear(err);

      err = SVN_NO_ERROR;
    }

  /* Clear iterpool, because callers may clear the error but have no way
     to clear the iterpool with potentially lots of allocated memory */
  svn_pool_destroy(iterpool);

  return err;
}

svn_error_t *
svn_iter__break(void)
{
  return &internal_break_error;
}
