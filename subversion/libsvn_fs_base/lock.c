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
  const char **lock_token_p;
  const char *path;
};


static svn_error_t *
txn_body_get_lock_token_from_path (void *baton, trail_t *trail)
{
  struct lock_token_get_args *args = baton;
  return svn_fs_bdb__lock_get (args->lock_token_p, trail->fs,
                               args->path, trail);
}


svn_error_t *
svn_fs_base__get_lock_from_path (svn_lock_t **lock,
                                 svn_fs_t *fs,
                                 const char *path,
                                 apr_pool_t *pool)
{
  struct lock_token_get_args args;
  svn_error_t *err;
  const char *token;
  base_fs_data_t *bfd = fs->fsap_data;

  SVN_ERR (svn_fs_base__check_fs (fs));
  
  /* First convert the path into a token. */
  args.path = path;
  args.lock_token_p = &token;  
  SVN_ERR (svn_fs_base__retry_txn (fs, txn_body_get_lock_token_from_path,
                                   &args, pool));
  
  /* Then convert the token into an svn_lock_t */
  SVN_ERR (svn_fs_base__get_lock_from_token (lock, fs, token, pool));
  
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
  struct lock_get args;
  svn_error_t *err;
  base_fs_data_t *bfd = fs->fsap_data;

  SVN_ERR (svn_fs_base__check_fs (fs));
  
  args.lock_token = token;
  args.lock_p = lock;
  err = svn_fs_base__retry_txn (fs, txn_body_get_lock_from_token,
                                &args, pool);

  if (err && err->apr_err == SVN_ERR_FS_BAD_LOCK_TOKEN)
    *lock = NULL;
  else if (err)
    return err;
  
  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_base__get_locks (apr_hash_t **locks,
                        svn_fs_t *fs,
                        const char *path,
                        apr_pool_t *pool)
{
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0,
                           "Function not yet implemented.");
}

