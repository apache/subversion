/* lock.c :  functions for manipulating filesystem locks.
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


#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"

#include "lock.h"
#include "err.h"
#include "bdb/locks-table.h"
#include "bdb/lock-tokens-table.h"
#include "../libsvn_fs/fs-loader.h"

svn_error_t *
svn_fs_base__lock (const char **token,
                   svn_fs_t *fs,
                   const char *path,
                   svn_boolean_t force,
                   long int timeout,
                   const char *current_token,
                   apr_pool_t *pool)
{
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0,
                           "Function not yet implemented.");
}


static svn_error_t *
txn_body_expire_lock (void *baton, trail_t *trail)
{
  svn_lock_t *lock = baton;

  SVN_ERR (svn_fs_bdb__lock_token_delete (trail->fs, lock->token, trail));
  return svn_fs_bdb__lock_delete (trail->fs, lock->path, trail);
}

svn_error_t *
svn_fs_base__unlock (svn_fs_t *fs,
                     const char *token,
                     svn_boolean_t force,
                     apr_pool_t *pool)
{
  svn_lock_t *lock;

  SVN_ERR (svn_fs_base__get_lock_from_token (&lock, fs, token, pool));

  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0,
                           "Function not yet implemented.");

  if (!fs->access_ctx || !fs->access_ctx->username)
    {
      /* TODO: Return specific error noting that a username has to be set
       * TODO: for unlock to function.
       */
    }

  if (!force && strcmp(fs->access_ctx->username, lock->owner) != 0)
    {
      /* TODO: Return specific error noting that username must
       * TODO: be owner or the force flag should be set.
       */
    }

  return svn_fs_base__retry_txn (fs, txn_body_expire_lock,
                                 lock, pool);
}


static svn_error_t *
check_lock_expired (svn_lock_t *lock, trail_t *trail)
{
  if (lock->expiration_date && lock->expiration_date < apr_time_now())
    {
      return SVN_NO_ERROR;
    }

  SVN_ERR (txn_body_expire_lock (lock, trail));
  
  return svn_error_create (SVN_ERR_FS_LOCK_EXPIRED, 0, "Lock expired.");
}


struct lock_token_get_args
{
  svn_lock_t **lock_p;
  const char *path;
};


static svn_error_t *
txn_body_get_lock_from_path (void *baton, trail_t *trail)
{
  const char *lock_token;
  struct lock_token_get_args *args = baton;
  
  SVN_ERR (svn_fs_bdb__lock_token_get (&lock_token, trail->fs,
                                       args->path, trail));
  SVN_ERR (svn_fs_bdb__lock_get (args->lock_p, trail->fs,
                                 lock_token, trail));

  return check_lock_expired (*args->lock_p, trail);
}


svn_error_t *
svn_fs_base__get_lock_from_path (svn_lock_t **lock,
                                 svn_fs_t *fs,
                                 const char *path,
                                 apr_pool_t *pool)
{
  struct lock_token_get_args args;

  SVN_ERR (svn_fs_base__check_fs (fs));
  
  args.path = path;
  args.lock_p = lock;  
  SVN_ERR (svn_fs_base__retry_txn (fs, txn_body_get_lock_from_path,
                                   &args, pool));
  
  return SVN_NO_ERROR;
}


struct lock_get_args
{
  svn_lock_t **lock_p;
  const char *lock_token;
};


static svn_error_t *
txn_body_get_lock_from_token (void *baton, trail_t *trail)
{
  struct lock_get_args *args = baton;
  
  SVN_ERR (svn_fs_bdb__lock_get (args->lock_p, trail->fs,
                                 args->lock_token, trail));
  
  return check_lock_expired (*args->lock_p, trail);
}


svn_error_t *
svn_fs_base__get_lock_from_token (svn_lock_t **lock,
                                  svn_fs_t *fs,
                                  const char *token,
                                  apr_pool_t *pool)
{
  struct lock_get_args args;
  svn_error_t *err;

  SVN_ERR (svn_fs_base__check_fs (fs));
  
  args.lock_token = token;
  args.lock_p = lock;
  err = svn_fs_base__retry_txn (fs, txn_body_get_lock_from_token,
                                &args, pool);

  if (err && err->apr_err == SVN_ERR_FS_BAD_LOCK_TOKEN)
    {
      *lock = NULL;
      return SVN_NO_ERROR;
    }

  return err;
}


struct locks_get_args
{
  apr_hash_t **locks_p;
  const char *path;
};


static svn_error_t *
txn_body_get_locks (void *baton, trail_t *trail)
{
  struct locks_get_args *args = baton;
  apr_hash_index_t *hi;

  SVN_ERR (svn_fs_bdb__lock_tokens_get (args->locks_p, trail->fs,
                                        args->path, trail));

  hi = apr_hash_first (trail->pool, *args->locks_p);
  while (hi)
    {
      const void *key;
      apr_ssize_t keylen;
      void *token;
      svn_lock_t *lock;
      svn_error_t *err;

      apr_hash_this (hi, &key, &keylen, &token);
      hi = apr_hash_next (hi);

      SVN_ERR (svn_fs_bdb__lock_get (&lock, trail->fs, token, trail));
      err = check_lock_expired (lock, trail);

      /* If the lock has expired, simply remove it from the hash */
      if (err && err->apr_err == SVN_ERR_FS_LOCK_EXPIRED)
        {
          svn_error_clear (err);
          lock = NULL;
        }
      else
        SVN_ERR (err);

      apr_hash_set (*args->locks_p, key, keylen, lock);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__get_locks (apr_hash_t **locks,
                        svn_fs_t *fs,
                        const char *path,
                        apr_pool_t *pool)
{
  struct locks_get_args args;

  SVN_ERR (svn_fs_base__check_fs (fs));
  
  args.locks_p = locks;
  args.path = path;
  return svn_fs_base__retry_txn (fs, txn_body_get_locks, &args, pool);
}
