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


svn_error_t *
svn_fs_base__unlock (svn_fs_t *fs,
                     const char *token,
                     svn_boolean_t force,
                     apr_pool_t *pool)
{
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0,
                           "Function not yet implemented.");
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
  return svn_fs_bdb__lock_get (args->lock_p, trail->fs,
                               lock_token, trail);
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
  return svn_fs_bdb__lock_get (args->lock_p, trail->fs,
                               args->lock_token, trail);
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
  SVN_ERR (svn_fs_bdb__lock_tokens_get (args->locks_p, trail->fs,
                                        args->path, trail));

  /* strikerXXX: TODO, resolve tokens to svn_lock_t */

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
