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
#include "svn_private_config.h"

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
                   svn_fs_t *fs,
                   const char *path,
                   const char *owner,
                   const char *comment,
                   long int timeout,
                   apr_pool_t *pool)
{
  svn_lock_t *lock = apr_pcalloc (pool, sizeof (*lock));

  SVN_ERR (svn_fs_base__generate_token (&(lock->token), fs, pool));
  
  lock->path = apr_pstrdup (pool, path);
  lock->owner = apr_pstrdup (pool, owner);
  lock->comment = apr_pstrdup (pool, comment);
  lock->creation_date = apr_time_now();

  if (timeout)
    lock->expiration_date = lock->creation_date + apr_time_from_sec(timeout);

  *lock_p = lock;
  return SVN_NO_ERROR;
}


/* Add LOCK and its associated LOCK_TOKEN (associated with PATH) as
   part of TRAIL. */
static svn_error_t *
add_lock_and_token (svn_lock_t *lock,
                    const char *lock_token,
                    const char *path,
                    trail_t *trail)
{
  SVN_ERR (svn_fs_bdb__lock_add (trail->fs, lock_token, lock, 
                                 trail, trail->pool));
  SVN_ERR (svn_fs_bdb__lock_token_add (trail->fs, path, lock_token, 
                                       trail, trail->pool));
  return SVN_NO_ERROR;
}


/* Delete LOCK_TOKEN and its corresponding lock (associated with PATH,
   whose KIND is supplied), as part of TRAIL. */
static svn_error_t *
delete_lock_and_token (const char *lock_token,
                       const char *path,
                       trail_t *trail)
{
  SVN_ERR (svn_fs_bdb__lock_delete (trail->fs, lock_token, 
                                    trail, trail->pool));
  SVN_ERR (svn_fs_bdb__lock_token_delete (trail->fs, path,
                                          trail, trail->pool));
  return SVN_NO_ERROR;
}


struct lock_args
{
  svn_lock_t **lock_p;
  const char *path;
  const char *comment;
  svn_boolean_t force;
  long int timeout;
  svn_revnum_t current_rev;
};


static svn_error_t *
txn_body_lock (void *baton, trail_t *trail)
{
  struct lock_args *args = baton;
  svn_node_kind_t kind = svn_node_file;
  svn_lock_t *existing_lock;
  const char *fs_username;
  svn_lock_t *new_lock;

  SVN_ERR (svn_fs_base__get_path_kind (&kind, args->path, trail, trail->pool));

  /* Until we implement directory locks someday, we only allow locks
     on files or non-existent paths. */
  if (kind == svn_node_dir)
    return svn_fs_base__err_not_file (trail->fs, args->path);

  /* While our locking implementation easily supports the locking of
     nonexistent paths, we deliberately choose not to allow such madness. */
  if (kind == svn_node_none)
    return svn_error_createf (SVN_ERR_FS_NOT_FOUND, NULL,
                              "Path '%s' doesn't exist in HEAD revision",
                              args->path);

  /* There better be a username attached to the fs. */
  if (!trail->fs->access_ctx || !trail->fs->access_ctx->username)
    return svn_fs_base__err_no_user (trail->fs);
  else
    fs_username = trail->fs->access_ctx->username; /* for convenience */

  /* Is the caller attempting to lock an out-of-date working file? */
  if (SVN_IS_VALID_REVNUM(args->current_rev))
    {
      svn_revnum_t created_rev;
      SVN_ERR (svn_fs_base__get_path_created_rev (&created_rev, args->path,
                                                  trail, trail->pool));

      /* SVN_INVALID_REVNUM means the path doesn't exist.  So
         apparently somebody is trying to lock something in their
         working copy, but somebody else has deleted the thing
         from HEAD.  That counts as being 'out of date'. */     
      if (! SVN_IS_VALID_REVNUM(created_rev))
        return svn_error_createf (SVN_ERR_FS_OUT_OF_DATE, NULL,
                                  "Path '%s' doesn't exist in HEAD revision",
                                  args->path);

      if (args->current_rev < created_rev)
        return svn_error_createf (SVN_ERR_FS_OUT_OF_DATE, NULL,
                                  "Lock failed: newer version of '%s' exists",
                                  args->path);
    }

  /* Is the path already locked?   

     Note that this next function call will automatically ignore any
     errors about {the path not existing as a key, the path's token
     not existing as a key, the lock just having been expired}.  And
     that's totally fine.  Any of these three errors are perfectly
     acceptable to ignore; it means that the path is now free and
     clear for locking, because the bdb funcs just cleared out both
     of the tables for us.   */
  SVN_ERR (svn_fs_base__get_lock_helper (&existing_lock, args->path, 
                                         trail, trail->pool));
  if (existing_lock)
    {
      if (! args->force)
        {
          /* Sorry, the path is already locked. */
          return svn_fs_base__err_path_locked (trail->fs, existing_lock);
        }
      else
        {
          /* Force was passed, so fs_username is "stealing" the
             lock from lock->owner.  Destroy the existing lock. */
          SVN_ERR (delete_lock_and_token (existing_lock->token,
                                          existing_lock->path, trail));
        }          
    }

  /* Create a new lock, and add it to the tables. */    
  SVN_ERR (generate_new_lock (&new_lock, trail->fs, args->path, fs_username,
                              args->comment, args->timeout, trail->pool));
  SVN_ERR (add_lock_and_token (new_lock, new_lock->token, 
                               args->path, trail));
  *(args->lock_p) = new_lock;

  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_base__lock (svn_lock_t **lock,
                   svn_fs_t *fs,
                   const char *path,
                   const char *comment,
                   svn_boolean_t force,
                   long int timeout,
                   svn_revnum_t current_rev,
                   apr_pool_t *pool)
{
  struct lock_args args;

  SVN_ERR (svn_fs_base__check_fs (fs));

  args.lock_p = lock;
  args.path = svn_fs_base__canonicalize_abspath (path, pool);
  args.comment = comment;
  args.force =  force;
  args.timeout = timeout;
  args.current_rev = current_rev;

  return svn_fs_base__retry_txn (fs, txn_body_lock, &args, pool);

}



struct attach_lock_args
{
  svn_lock_t *lock;
  svn_boolean_t force;
  svn_revnum_t current_rev;
};


static svn_error_t *
txn_body_attach_lock (void *baton, trail_t *trail)
{
  struct attach_lock_args *args = baton;
  svn_lock_t *lock = args->lock;
  svn_node_kind_t kind = svn_node_file;
  svn_lock_t *existing_lock;

  /* Dup the lock so we can canonicalize it's 'path' member. */
  lock = svn_lock_dup (lock, trail->pool);
  lock->path = svn_fs_base__canonicalize_abspath (lock->path, trail->pool);
  
  /* Until we implement directory locks someday, we only allow locks
     on files or non-existent paths. */
  SVN_ERR (svn_fs_base__get_path_kind (&kind, lock->path, trail, trail->pool));
  if (kind == svn_node_dir)
    return svn_fs_base__err_not_file (trail->fs, lock->path);

  /* While our locking implementation easily supports the locking of
     nonexistent paths, we deliberately choose not to allow such madness. */
  if (kind == svn_node_none)
    return svn_error_createf (SVN_ERR_FS_NOT_FOUND, NULL,
                              "Path '%s' doesn't exist in HEAD revision",
                              lock->path);

  /* There better be a username in the incoming lock. */
  if (! lock->owner)
    {
      if (!trail->fs->access_ctx || !trail->fs->access_ctx->username)
        return svn_fs_base__err_no_user (trail->fs);
      else
        lock->owner = trail->fs->access_ctx->username;
    }

  /* Is the caller attempting to lock an out-of-date working file? */
  if (SVN_IS_VALID_REVNUM(args->current_rev))
    {
      svn_revnum_t created_rev;
      SVN_ERR (svn_fs_base__get_path_created_rev (&created_rev, lock->path,
                                                  trail, trail->pool));

      /* SVN_INVALID_REVNUM means the path doesn't exist.  So
         apparently somebody is trying to lock something in their
         working copy, but somebody else has deleted the thing
         from HEAD.  That counts as being 'out of date'. */     
      if (! SVN_IS_VALID_REVNUM(created_rev))
        return svn_error_createf (SVN_ERR_FS_OUT_OF_DATE, NULL,
                                  "Path '%s' doesn't exist in HEAD revision",
                                  lock->path);

      if (args->current_rev < created_rev)
        return svn_error_createf (SVN_ERR_FS_OUT_OF_DATE, NULL,
                                  "Lock failed: newer version of '%s' exists.",
                                  lock->path);
    }

  /* Is the path already locked? */
  SVN_ERR (svn_fs_base__get_lock_helper (&existing_lock, lock->path, 
                                         trail, trail->pool));
  if (existing_lock)
    {
      if (! args->force)
        {
          /* Sorry, the path is already locked. */
          return svn_fs_base__err_path_locked (trail->fs, existing_lock);
        }
      else  /* forcibly locking anyway */
        {
          /* Force was passed, so fs_username is "stealing" the
             lock from lock->owner.  Destroy the existing lock. */
          SVN_ERR (delete_lock_and_token (existing_lock->token,
                                          existing_lock->path, trail));
        }
    }

  /* Write the incoming lock into our tables. */
  SVN_ERR (add_lock_and_token (lock, lock->token, lock->path, trail));
  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_base__attach_lock (svn_lock_t *lock,
                          svn_fs_t *fs,
                          svn_boolean_t force,
                          svn_revnum_t current_rev,
                          apr_pool_t *pool)
{
  struct attach_lock_args args;

  SVN_ERR (svn_fs_base__check_fs (fs));

  args.lock = lock;
  args.force = force;
  args.current_rev = current_rev;

  return svn_fs_base__retry_txn (fs, txn_body_attach_lock, &args, pool);
}



svn_error_t *
svn_fs_base__generate_token (const char **token,
                             svn_fs_t *fs,
                             apr_pool_t *pool)
{
  /* Notice that 'fs' is currently unused.  But perhaps someday,
     we'll want to use the fs UUID + some incremented number?  */
  apr_uuid_t uuid;
  char *uuid_str = apr_pcalloc (pool, APR_UUID_FORMATTED_LENGTH + 1);

  apr_uuid_get (&uuid);
  apr_uuid_format (uuid_str, &uuid);

  /* For now, we generate a URI that matches the DAV RFC.  We could
     change this to some other URI schema someday, if we wish. */
  *token = apr_pstrcat (pool, "opaquelocktoken:", uuid_str, NULL);
  return SVN_NO_ERROR;
}



struct unlock_args
{
  const char *path;
  const char *token;
  svn_boolean_t force;
};


static svn_error_t *
txn_body_unlock (void *baton, trail_t *trail)
{
  struct unlock_args *args = baton;
  const char *lock_token;
  svn_lock_t *lock;

  /* This could return SVN_ERR_FS_BAD_LOCK_TOKEN or SVN_ERR_FS_LOCK_EXPIRED. */
  SVN_ERR (svn_fs_bdb__lock_token_get (&lock_token, trail->fs, args->path,
                                       trail, trail->pool));

  /* If not breaking the lock, we need to do some more checking. */
  if (!args->force)
    {
      /* Sanity check: The lock token must exist, and must match. */
      if (args->token == NULL)
        return svn_fs_base__err_no_lock_token (trail->fs, args->path);
      else if (strcmp (lock_token, args->token) != 0)
        return svn_fs_base__err_no_such_lock (trail->fs, args->path);

      SVN_ERR (svn_fs_bdb__lock_get (&lock, trail->fs, lock_token, 
                                     trail, trail->pool));

      /* There better be a username attached to the fs. */
      if (!trail->fs->access_ctx || !trail->fs->access_ctx->username)
        return svn_fs_base__err_no_user (trail->fs);

      /* And that username better be the same as the lock's owner. */
      if (strcmp(trail->fs->access_ctx->username, lock->owner) != 0)
        return svn_fs_base__err_lock_owner_mismatch
          (trail->fs,
           trail->fs->access_ctx->username,
           lock->owner);
    }

  /* Remove a row from each of the locking tables. */
  SVN_ERR (delete_lock_and_token (lock_token, args->path, trail));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__unlock (svn_fs_t *fs,
                     const char *path,
                     const char *token,
                     svn_boolean_t force,
                     apr_pool_t *pool)
{
  struct unlock_args args;

  SVN_ERR (svn_fs_base__check_fs (fs));

  args.path = svn_fs_base__canonicalize_abspath (path, pool);
  args.token = token;
  args.force = force;
  return svn_fs_base__retry_txn (fs, txn_body_unlock, &args, pool);
}


svn_error_t *
svn_fs_base__get_lock_helper (svn_lock_t **lock_p,
                              const char *path,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  const char *lock_token;
  svn_error_t *err;
  
  err = svn_fs_bdb__lock_token_get (&lock_token, trail->fs, path,
                                    trail, pool);

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
  err = svn_fs_bdb__lock_get (lock_p, trail->fs, lock_token, trail, pool);
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
txn_body_get_lock (void *baton, trail_t *trail)
{
  struct lock_token_get_args *args = baton;
  return svn_fs_base__get_lock_helper (args->lock_p, args->path, 
                                       trail, trail->pool);
}


svn_error_t *
svn_fs_base__get_lock (svn_lock_t **lock,
                       svn_fs_t *fs,
                       const char *path,
                       apr_pool_t *pool)
{
  struct lock_token_get_args args;
  svn_error_t *err;

  SVN_ERR (svn_fs_base__check_fs (fs));
  
  args.path = svn_fs_base__canonicalize_abspath (path, pool);
  args.lock_p = lock;  
  return svn_fs_base__retry_txn (fs, txn_body_get_lock, &args, pool);
  return err;
}


struct locks_get_args
{
  const char *path;
  svn_fs_get_locks_callback_t get_locks_func;
  void *get_locks_baton;
};


static svn_error_t *
txn_body_get_locks (void *baton, trail_t *trail)
{
  struct locks_get_args *args = baton;
  return svn_fs_bdb__locks_get (trail->fs, args->path,
                                args->get_locks_func, args->get_locks_baton,
                                trail, trail->pool);
}


svn_error_t *
svn_fs_base__get_locks (svn_fs_t *fs,
                        const char *path,
                        svn_fs_get_locks_callback_t get_locks_func,
                        void *get_locks_baton,
                        apr_pool_t *pool)
{
  struct locks_get_args args;

  SVN_ERR (svn_fs_base__check_fs (fs));
  args.path = svn_fs_base__canonicalize_abspath (path, pool);
  args.get_locks_func = get_locks_func;
  args.get_locks_baton = get_locks_baton;
  return svn_fs_base__retry_txn (fs, txn_body_get_locks, &args, pool);
}



/* Utility function:  verify that a lock can be used.

   If no username is attached to the FS, return SVN_ERR_FS_NO_USER.

   If the FS username doesn't match LOCK's owner, return
   SVN_ERR_FS_LOCK_OWNER_MISMATCH.

   If FS hasn't been supplied with a matching lock-token for LOCK,
   return SVN_ERR_FS_BAD_LOCK_TOKEN.

   Otherwise return SVN_NO_ERROR.
 */
static svn_error_t *
verify_lock (svn_fs_t *fs,
             svn_lock_t *lock,
             apr_pool_t *pool)
{
  if ((! fs->access_ctx) || (! fs->access_ctx->username))
    return svn_error_createf 
      (SVN_ERR_FS_NO_USER, NULL,
       _("Cannot verify lock on path '%s'; no username available"),
       lock->path);
  
  else if (strcmp (fs->access_ctx->username, lock->owner) != 0)
    return svn_error_createf 
      (SVN_ERR_FS_LOCK_OWNER_MISMATCH, NULL,
       _("User %s does not own lock on path '%s' (currently locked by %s)"),
       fs->access_ctx->username, lock->path, lock->owner);

  else if (apr_hash_get (fs->access_ctx->lock_tokens, lock->token,
                         APR_HASH_KEY_STRING) == NULL)
    return svn_error_createf 
      (SVN_ERR_FS_BAD_LOCK_TOKEN, NULL,
       _("Cannot verify lock on path '%s'; no matching lock-token available"),
       lock->path);
    
  return SVN_NO_ERROR;
}


/* This implements the svn_fs_get_locks_callback_t interface, where
   BATON is just an svn_fs_t object. */
static svn_error_t *
get_locks_callback (void *baton, 
                    svn_lock_t *lock, 
                    apr_pool_t *pool)
{
  return verify_lock (baton, lock, pool);
}


/* The main routine for lock enforcement, used throughout libsvn_fs_base. */
svn_error_t *
svn_fs_base__allow_locked_operation (const char *path,
                                     svn_boolean_t recurse,
                                     trail_t *trail,
                                     apr_pool_t *pool)
{
  if (recurse)
    {
      /* Discover all locks at or below the path. */
      SVN_ERR (svn_fs_bdb__locks_get (trail->fs, path, get_locks_callback, 
                                      trail->fs, trail, pool));
    }
  else
    {
      svn_lock_t *lock;

      /* Discover any lock attached to the path. */
      SVN_ERR (svn_fs_base__get_lock_helper (&lock, path, trail, pool));
      if (lock)
        SVN_ERR (verify_lock (trail->fs, lock, pool));
    }
  return SVN_NO_ERROR;
}
