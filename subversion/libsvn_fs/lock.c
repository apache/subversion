/*
 * lock.c:  shared code to manipulate examine locks.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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


#include <apr_hash.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_fs.h"

#include "fs-loader.h"


svn_error_t *
svn_fs__verify_lock (svn_boolean_t *may_use_lock,
                     svn_fs_t *fs,
                     svn_lock_t *lock,
                     apr_pool_t *pool)
{
  *may_use_lock = FALSE;

  if (!fs->access_ctx
      || fs->access_ctx->username != lock->owner)
    return SVN_NO_ERROR;

  *may_use_lock = apr_hash_get (fs->access_ctx->lock_tokens,
                                lock->token,
                                APR_HASH_KEY_STRING) != NULL;
  return SVN_NO_ERROR;
}




svn_error_t *
svn_fs__verify_locks (svn_boolean_t *may_use_locks,
                      svn_fs_t *fs,
                      apr_hash_t *locks,
                      apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  *may_use_locks = TRUE;

  for (hi = apr_hash_first (pool, locks);
       hi && *may_use_locks;
       hi = apr_hash_next (hi))
    {
      void *lock;

      apr_hash_this (hi, NULL, NULL, &lock);
      SVN_ERR (svn_fs__verify_lock (may_use_locks, fs, lock, pool));
    }

  return SVN_NO_ERROR;
}
