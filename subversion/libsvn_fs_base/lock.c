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

#include "apr_uuid.h"

#include "lock.h"
#include "tree.h"
#include "err.h"
#include "bdb/locks-table.h"
#include "bdb/lock-tokens-table.h"
#include "../libsvn_fs/fs-loader.h"



/* Helper func:  create a new svn_lock_t, everything allocated in pool. */
static svn_error_t *
generate_new_lock (svn_lock_t **lock_p,
                   const char *path,
                   const char *owner,
                   long int timeout,
                   apr_pool_t *pool)
{
  apr_uuid_t uuid;
  char *uuid_str = apr_pcalloc (pool, APR_UUID_FORMATTED_LENGTH + 1);
  svn_lock_t *lock = apr_pcalloc (pool, sizeof(*lock));
  
  lock->path = apr_pstrdup (pool, path);
  
  lock->owner = apr_pstrdup (pool, owner);
  
  apr_uuid_get (&uuid);
  apr_uuid_format (uuid_str, &uuid);
  lock->token = uuid_str;

  lock->creation_date = apr_time_now();

  if (timeout)
    lock->expiration_date = lock->creation_date + apr_time_from_sec(timeout);

  *lock_p = lock;
  return SVN_NO_ERROR;
}


struct lock_args
{
  svn_lock_t **lock_p;
  const char *path;
  svn_boolean_t force;
  long int timeout;
  const char *current_token;
};


static svn_error_t *
txn_body_lock (void *baton, trail_t *trail)
{
  struct lock_args *args = baton;
  svn_node_kind_t kind = svn_node_file;
  svn_lock_t *existing_lock;
  const char *fs_username;

  SVN_ERR (svn_fs_base__get_path_kind (&kind, args->path, trail));

  /* Until we implement directory locks someday, we only allow locks
     on files or non-existent paths. */
  if (kind == svn_node_dir)
    return svn_fs_base__err_not_file (trail->fs, args->path);
  if (kind == svn_node_none)
    kind = svn_node_file;    /* pretend, so the name can be reserved */

  /* There better be a username attached to the fs. */
  if (!trail->fs->access_ctx || !trail->fs->access_ctx->username)
    return svn_fs_base__err_no_user (trail->fs);
  else
    fs_username = trail->fs->access_ctx->username; /* for convenience */

  /* Is the path already locked?   

     Note that this next function call will automatically ignore any
     errors about {the path not existing as a key, the path's token
     not existing as a key, the lock just having been expired}.  And
     that's totally fine.  Any of these three errors are perfectly
     acceptable to ignore; it means that the path is now free and
     clear for locking, because the bdb funcs just cleared out both
     of the tables for us.   */
  SVN_ERR (svn_fs_base__get_lock_from_path_helper (&existing_lock,
                                                   args->path, kind, trail));
  if (existing_lock)
    {
      if (strcmp(existing_lock->owner, fs_username) != 0)
        {
          if (! args->force)
            {
              /* You can't steal someone else's lock, silly. */
              return svn_fs_base__err_path_locked (trail->fs, existing_lock);
            }
          else
            {
              /* Force was passed, so fs_username is "stealing" the
                 lock from lock->owner.  Destroy the existing lock,
                 and create a new one. */
              SVN_ERR (svn_fs_bdb__lock_delete (trail->fs,
                                                existing_lock->token, trail));
              SVN_ERR (svn_fs_bdb__lock_token_delete (trail->fs,
                                                      existing_lock->path,
                                                      kind, trail));
              goto make_new_lock;
            }
          
        }
      else
        {
          /* So the fs_username owns the existing lock:  we interpret
             that as a 'refresh' request. */

          if ((! args->current_token)
              || (strcmp(args->current_token, existing_lock->token) != 0))
            {
              /* Whoops, you may own the existing lock, but you gotta
                 show me the right token for it. */
              return svn_fs_base__err_bad_lock_token (trail->fs,
                                                      existing_lock->token);
            }
          else
            {
              /* Destroy the existing lock, and create a new one. */
              SVN_ERR (svn_fs_bdb__lock_delete (trail->fs,
                                                existing_lock->token, trail));
              SVN_ERR (svn_fs_bdb__lock_token_delete (trail->fs,
                                                      existing_lock->path,
                                                      kind, trail));
              goto make_new_lock;
            }
        }
    }
  else
    {
      /* There's no existing lock at all, so make a new one. */
      goto make_new_lock;
    }

 make_new_lock:
  {
    svn_lock_t *new_lock;
    
    SVN_ERR (generate_new_lock (&new_lock, args->path, fs_username,
                                args->timeout, trail->pool));
    SVN_ERR (svn_fs_bdb__lock_add (trail->fs, new_lock->token,
                                   new_lock, trail));
    SVN_ERR (svn_fs_bdb__lock_token_add (trail->fs, args->path, kind,
                                         new_lock->token, trail));
    *(args->lock_p) = new_lock;
  }

  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_base__lock (svn_lock_t **lock,
                   svn_fs_t *fs,
                   const char *path,
                   svn_boolean_t force,
                   long int timeout,
                   const char *current_token,
                   apr_pool_t *pool)
{
  struct lock_args args;

  SVN_ERR (svn_fs_base__check_fs (fs));

  args.lock_p = lock;
  args.path = path;
  args.force =  force;
  args.timeout = timeout;
  args.current_token = current_token;

  return svn_fs_base__retry_txn (fs, txn_body_lock, &args, pool);

}


struct unlock_args
{
  const char *token;
  svn_boolean_t force;
};


static svn_error_t *
txn_body_unlock (void *baton, trail_t *trail)
{
  struct unlock_args *args = baton;
  svn_node_kind_t kind = svn_node_none;
  svn_lock_t *lock;

  /* Sanity check:  we don't want to pass a NULL key to BDB lookup. */
  if (! args->token)
    return svn_fs_base__err_bad_lock_token (trail->fs, "null");

  /* This could return SVN_ERR_FS_BAD_LOCK_TOKEN or SVN_ERR_FS_LOCK_EXPIRED. */
  SVN_ERR (svn_fs_bdb__lock_get (&lock, trail->fs, args->token, trail));
  
  /* There better be a username attached to the fs. */
  if (!trail->fs->access_ctx || !trail->fs->access_ctx->username)
    return svn_fs_base__err_no_user (trail->fs);

  /* And that username better be the same as the lock's owner. */
  if (!args->force
      && strcmp(trail->fs->access_ctx->username, lock->owner) != 0)
    return svn_fs_base__err_lock_owner_mismatch (trail->fs,
             trail->fs->access_ctx->username,
             lock->owner);

  /* Remove a row from each of the locking tables. */
  SVN_ERR (svn_fs_bdb__lock_delete (trail->fs, lock->token, trail));
  SVN_ERR (svn_fs_base__get_path_kind (&kind, lock->path, trail));
  return svn_fs_bdb__lock_token_delete (trail->fs, lock->path, kind, trail);
}


svn_error_t *
svn_fs_base__unlock (svn_fs_t *fs,
                     const char *token,
                     svn_boolean_t force,
                     apr_pool_t *pool)
{
  struct unlock_args args;

  SVN_ERR (svn_fs_base__check_fs (fs));

  args.token = token;
  args.force = force;
  return svn_fs_base__retry_txn (fs, txn_body_unlock, &args, pool);
}


svn_error_t *
svn_fs_base__get_lock_from_path_helper (svn_lock_t **lock_p,
                                        const char *path,
                                        const svn_node_kind_t kind,
                                        trail_t *trail)
{
  const char *lock_token;
  svn_error_t *err;
  
  err = svn_fs_bdb__lock_token_get (&lock_token, trail->fs, path, kind, trail);

  /* We've deliberately decided that this function doesn't tell the
     caller *why* the lock is unavailable.  */
  if (err && ((err->apr_err == SVN_ERR_FS_NO_SUCH_LOCK)
              || (err->apr_err == SVN_ERR_FS_LOCK_EXPIRED)
              || (err->apr_err == SVN_ERR_FS_BAD_LOCK_TOKEN)))
    {
      svn_error_clear (err);
      *lock_p = NULL;
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR (err);

  /* Same situation here.  */
  err = svn_fs_bdb__lock_get (lock_p, trail->fs, lock_token, trail);
  if (err && ((err->apr_err == SVN_ERR_FS_LOCK_EXPIRED)
              || (err->apr_err == SVN_ERR_FS_BAD_LOCK_TOKEN)))
    {
      svn_error_clear (err);
      *lock_p = NULL;
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR (err);

  return err;
}


struct lock_token_get_args
{
  svn_lock_t **lock_p;
  const char *path;
};


static svn_error_t *
txn_body_get_lock_from_path (void *baton, trail_t *trail)
{
  struct lock_token_get_args *args = baton;
  svn_node_kind_t kind = svn_node_none;

  SVN_ERR (svn_fs_base__get_path_kind (&kind, args->path, trail));

  return svn_fs_base__get_lock_from_path_helper (args->lock_p,
                                                 args->path, kind, trail);
}


svn_error_t *
svn_fs_base__get_lock_from_path (svn_lock_t **lock,
                                 svn_fs_t *fs,
                                 const char *path,
                                 apr_pool_t *pool)
{
  struct lock_token_get_args args;
  svn_error_t *err;

  SVN_ERR (svn_fs_base__check_fs (fs));
  
  args.path = path;
  args.lock_p = lock;  
  return svn_fs_base__retry_txn (fs, txn_body_get_lock_from_path,
                                 &args, pool);
  return err;
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

  SVN_ERR (svn_fs_base__check_fs (fs));
  
  args.lock_token = token;
  args.lock_p = lock;
  return svn_fs_base__retry_txn (fs, txn_body_get_lock_from_token,
                                 &args, pool);
}



svn_error_t *
svn_fs_base__get_locks_helper (apr_hash_t **locks_p,
                               const char *path,
                               const svn_node_kind_t kind,
                               trail_t *trail)
{
  return svn_fs_bdb__locks_get (locks_p, trail->fs, path, kind, trail);
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
  svn_node_kind_t kind = svn_node_none;

  SVN_ERR (svn_fs_base__get_path_kind (&kind, args->path, trail));

  return svn_fs_base__get_locks_helper (args->locks_p, args->path,
                                        kind, trail);
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



svn_error_t *
svn_fs_base__allow_locked_operation (const char *path,
                                     svn_node_kind_t kind,
                                     svn_boolean_t recurse,
                                     trail_t *trail)
{
  if (kind == svn_node_dir && recurse)
    {
      apr_hash_t *locks;
      
      /* Discover all locks at or below the path. */
      SVN_ERR (svn_fs_base__get_locks_helper (&locks, path, kind, trail));

      /* Easy out. */
      if (apr_hash_count (locks) == 0)
          return SVN_NO_ERROR;

      /* Some number of locks exist below path; are we allowed to
         change them? */
      return svn_fs__verify_locks (trail->fs, locks, trail->pool);      
    }

  /* We're either checking a file, or checking a dir non-recursively: */
    {
      svn_lock_t *lock;

      /* Discover any lock attached to the path. */
      SVN_ERR (svn_fs_base__get_lock_from_path_helper (&lock, path,
                                                       kind, trail));

      /* Easy out. */
      if (! lock)
        return SVN_NO_ERROR;

      /* The path is locked;  are we allowed to change it? */
      return svn_fs__verify_lock (trail->fs, lock, trail->pool);
    }
}
