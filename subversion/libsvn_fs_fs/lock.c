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
#include "svn_path.h"
#include "svn_fs.h"
#include "svn_hash.h"
#include "svn_time.h"

#include "apr_uuid.h"
#include "apr_file_io.h"

#include "lock.h"
#include "tree.h"
#include "err.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/* Names of special lock directories in the fs_fs filesystem. */
#define LOCK_ROOT_DIR "locks"
#define LOCK_LOCK_DIR "locks"
#define LOCK_TOKEN_DIR "lock-tokens"

/* Names of hash keys used to store a lock for writing to disk. */
#define PATH_KEY "path"
#define TOKEN_KEY "token"
#define OWNER_KEY "owner"
#define CREATION_DATE_KEY "creation_date"
#define EXPIRATION_DATE_KEY "expiration_date"
#define COMMENT_KEY "comment"


static char *
abs_path_to_lock_file (svn_fs_t *fs,
                       const char *rel_path,
                       apr_pool_t *pool)
{
  char *abs_path;
  abs_path = svn_path_join_many (pool, fs->path, LOCK_ROOT_DIR, 
                                 LOCK_LOCK_DIR, rel_path, NULL);
  return abs_path;
}


static char *
abs_path_to_lock_token_file (svn_fs_t *fs,
                             const char *token,
                             apr_pool_t *pool)
{
  char *abs_path;
  abs_path = svn_path_join_many (pool, fs->path, LOCK_ROOT_DIR, 
                                 LOCK_TOKEN_DIR, token, NULL);
  return abs_path;
}


static void
hash_store (apr_hash_t *hash,
            const char *key,
            const char *value,
            apr_pool_t *pool)
{
  svn_string_t *str;
  if (!key || !value)
    return;

  str = svn_string_create (value, pool);
  apr_hash_set (hash, key, APR_HASH_KEY_STRING, str);
}


static const char *
hash_fetch (apr_hash_t *hash,
            const char *key,
            apr_pool_t *pool)
{
  svn_string_t *str;

  str = apr_hash_get (hash, key, APR_HASH_KEY_STRING);
  if (str)
    return str->data;
  return NULL;
}


/* Store the lock in the OS level filesystem in a tree under
   repos/db/locks/locks that reflects the location of lock->path in
   the repository. */
static svn_error_t *
write_lock_to_file (svn_fs_t *fs,
                    svn_lock_t *lock,
                    apr_pool_t *pool)
{
  apr_hash_t *hash;
  apr_file_t *fd;
  svn_stream_t *stream;
  apr_status_t status = APR_SUCCESS;
  char *abs_path;

  char *dir;
  /* ###file and pathnames will be limited by the native filesystem's
     encoding--could that pose a problem? */
  abs_path = abs_path_to_lock_file (fs, lock->path, pool);

  /* Make sure that the directory exists before we create the lock file. */
  dir = svn_path_dirname (abs_path, pool);
  status = apr_dir_make_recursive (dir, APR_OS_DEFAULT, pool);
  
  if (status)
    return svn_error_wrap_apr (status, _("Can't create lock directory '%s'"),
                               dir);

  /* Create our hash and load it up. */
  hash = apr_hash_make (pool);

  hash_store (hash, PATH_KEY, lock->path, pool); 
  hash_store (hash, TOKEN_KEY, lock->token, pool); 
  hash_store (hash, OWNER_KEY, lock->owner, pool); 
  hash_store (hash, COMMENT_KEY, lock->comment, pool); 

  hash_store (hash, CREATION_DATE_KEY, 
              svn_time_to_cstring(lock->creation_date, pool), pool);
  hash_store (hash, EXPIRATION_DATE_KEY, 
              svn_time_to_cstring(lock->expiration_date, pool), pool);


  status = apr_file_open (&fd, abs_path, APR_WRITE | APR_CREATE, 
                          APR_OS_DEFAULT, pool);
  if (status && !APR_STATUS_IS_ENOENT (status))
    return svn_error_wrap_apr (status, _("Can't open '%s' to write lock"),
                               abs_path);

  stream = svn_stream_from_aprfile (fd, pool);

  SVN_ERR_W (svn_hash_write2 (hash, stream, SVN_HASH_TERMINATOR, pool),
             apr_psprintf (pool,
                           _("Cannot write lock hash to '%s'"),
                           svn_path_local_style (abs_path, pool)));

  return SVN_NO_ERROR;
}

/* Store the lock token in the OS level filesystem in a file under
   repos/db/locks/lock-tokens.  The file's name is lock->token, and
   the file's contents is lock->path. */
static svn_error_t *
write_lock_token_to_file (svn_fs_t *fs,
                          svn_lock_t *lock,
                          apr_pool_t *pool)
{
  apr_status_t status;
  apr_file_t *fd;
  char *abs_path, *dir;

  abs_path = abs_path_to_lock_token_file (fs, lock->path, pool);

  /* Make sure that the directory exists before we create the lock file. */
  dir = svn_path_dirname (abs_path, pool);

  status = apr_dir_make_recursive (dir, APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_wrap_apr (status, 
                               _("Can't create lock token directory '%s'"),
                               dir);

  status = apr_file_open (&fd, abs_path, APR_WRITE | APR_CREATE,
                          APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_wrap_apr (status, _("Can't open '%s' to write lock token"),
                               abs_path);

  /* The token file contains only the relative path to the lock file. */
  SVN_ERR (svn_io_file_write_full (fd, lock->path, 
                                   strlen (lock->path), NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
save_lock (svn_fs_t *fs,
           svn_lock_t *lock, 
           apr_pool_t *pool)
{

  SVN_ERR (write_lock_to_file (fs, lock, pool));

  SVN_ERR (write_lock_token_to_file (fs, lock, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
delete_lock (svn_fs_t *fs, 
             svn_lock_t *lock,
             apr_pool_t *pool)
{
  apr_status_t status = APR_SUCCESS;
  char *abs_path;

  /* Delete lock from locks area */
  abs_path = abs_path_to_lock_file (fs, lock->path, pool);
  SVN_ERR (svn_io_remove_file (abs_path, pool));

  /* Prune directories on the way back */
  while (status == APR_SUCCESS)
    {
      abs_path = svn_path_dirname (abs_path, pool);
      /* If this fails, status will != APR_SUCCESS, so we drop out of
         the loop. */
      status = apr_dir_remove (abs_path, pool);
    }

  /* Delete lock from tokens area */
  abs_path = abs_path_to_lock_token_file (fs, lock->path, pool);
  SVN_ERR (svn_io_remove_file (abs_path, pool));

  return SVN_NO_ERROR;
}


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
  svn_lock_t *lock = apr_pcalloc (pool, sizeof (*lock));
  
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


static svn_error_t *
read_lock_from_file (svn_lock_t **lock_p,
                     svn_fs_t *fs,
                     const char *path,
                     apr_pool_t *pool)
{
  svn_lock_t *lock;
  apr_hash_t *hash;
  apr_file_t *fd;
  svn_stream_t *stream;
  apr_status_t status;
  char *abs_path;
  const char *val;
  apr_finfo_t finfo;

  abs_path = abs_path_to_lock_file(fs, path, pool);

  status = apr_stat (&finfo, abs_path, APR_FINFO_TYPE, pool);
  /* If file doesn't exist, then there's no lock, so return immediately. */
  if (APR_STATUS_IS_ENOENT (status))
    {
      *lock_p = NULL;
      return svn_fs_fs__err_no_such_lock (fs, path);
    }      

  /* ###Is this necessary? */ 
  if (status  && !APR_STATUS_IS_ENOENT (status))
    return svn_error_wrap_apr (status, _("Can't stat '%s'"), abs_path);

  status = apr_file_open (&fd, abs_path, APR_READ, APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_wrap_apr (status, _("Can't open '%s' to read lock"),
                               abs_path);

  hash = apr_hash_make (pool);

  stream = svn_stream_from_aprfile(fd, pool);
  SVN_ERR_W (svn_hash_read2 (hash, stream, SVN_HASH_TERMINATOR, pool),
             apr_psprintf (pool, _("Can't parse '%s'"), abs_path));

  /* Create our lock and load it up. */
  lock = apr_palloc (pool, sizeof (*lock));

  val = hash_fetch (hash, PATH_KEY, pool);
  if (val)
    lock->path = val;

  val = hash_fetch (hash, TOKEN_KEY, pool);
  if (val)
    lock->token = val;

  val = hash_fetch (hash, OWNER_KEY, pool);
  if (val)
    lock->owner = val;

  val = hash_fetch (hash, COMMENT_KEY, pool);
  if (val)
    lock->comment = val;

  val = hash_fetch (hash, CREATION_DATE_KEY, pool);
  if (val)
    svn_time_from_cstring (&(lock->creation_date), val, pool);

  val = hash_fetch (hash, EXPIRATION_DATE_KEY, pool);
  if (val)
    svn_time_from_cstring (&(lock->expiration_date), val, pool);

  *lock_p = lock;

  return SVN_NO_ERROR;
}


static svn_error_t *
get_lock_from_path_helper (svn_fs_t *fs,
                           svn_lock_t **lock_p,
                           const char *path,
                           apr_pool_t *pool)
{
  svn_lock_t *lock;
  svn_error_t *err;
  
  err = svn_fs_fs__get_lock_from_path (&lock, fs, path, pool);

  /* We've deliberately decided that this function doesn't tell the
     caller *why* the lock is unavailable.  */
  if (err && ((err->apr_err == SVN_ERR_FS_NO_SUCH_LOCK)
              || (err->apr_err == SVN_ERR_FS_LOCK_EXPIRED)))
    {
      svn_error_clear (err);
      *lock_p = NULL;
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR (err);

  *lock_p = lock;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__lock (svn_lock_t **lock_p,
                 svn_fs_t *fs,
                 const char *path,
                 const char *comment,
                 svn_boolean_t force,
                 long int timeout,
                 apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_lock_t *existing_lock;
  svn_lock_t *new_lock;
  svn_fs_root_t *root;
  svn_revnum_t youngest;

  SVN_ERR (svn_fs_fs__check_fs (fs));

  SVN_ERR (svn_fs_youngest_rev (&youngest, fs, pool));

  SVN_ERR (svn_fs_revision_root (&root, fs, youngest, pool));

  SVN_ERR (svn_fs_fs__check_path (&kind, root, path, pool));

  /* Until we implement directory locks someday, we only allow locks
     on files or non-existent paths. */
  if (kind == svn_node_dir)
    return svn_fs_fs__err_not_file (fs, path);

  /* We need to have a username attached to the fs. */
  if (!fs->access_ctx || !fs->access_ctx->username)
    return svn_fs_fs__err_no_user (fs);

  /* Is the path already locked?   

     Note that this next function call will automatically ignore any
     errors about {the path not existing as a key, the path's token
     not existing as a key, the lock just having been expired}.  And
     that's totally fine.  Any of these three errors are perfectly
     acceptable to ignore; it means that the path is now free and
     clear for locking, because the fsfs funcs just cleared out both
     of the tables for us.   */
  SVN_ERR (get_lock_from_path_helper (fs, &existing_lock, path, pool));
  if (existing_lock)
    {
      if (! force)
        {
          /* Sorry, the path is already locked. */
          return svn_fs_fs__err_path_locked (fs, existing_lock);
        }
      else
        {
          /* Force was passed, so fs_username is "stealing" the
             lock from lock->owner.  Destroy the existing lock. */
          SVN_ERR (delete_lock (fs, existing_lock, pool));
        }          
    }

  /* Create a new lock, and add it to the tables. */    
  SVN_ERR (generate_new_lock (&new_lock, path, fs->access_ctx->username,
                              timeout, pool));
  SVN_ERR (save_lock (fs, new_lock, pool));
  *lock_p = new_lock;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__attach_lock (svn_lock_t *lock,
                        svn_fs_t *fs,
                        apr_pool_t *pool)
{
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0,
                           "Function not yet implemented.");
}


svn_error_t *
svn_fs_fs__unlock (svn_fs_t *fs,
                   const char *token,
                   svn_boolean_t force,
                   apr_pool_t *pool)
{
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0,
                           "Function not yet implemented.");
}



svn_error_t *
svn_fs_fs__get_lock_from_path (svn_lock_t **lock_p,
                               svn_fs_t *fs,
                               const char *path,
                               apr_pool_t *pool)
{
  svn_lock_t *lock;
    
  SVN_ERR (read_lock_from_file (&lock, fs, path, pool));

  /* Possibly auto-expire the lock. */
  if (lock->expiration_date 
      && (apr_time_now() > lock->expiration_date))
    {
      SVN_ERR (delete_lock (fs, lock, pool));
      *lock_p = NULL;
      return svn_fs_fs__err_lock_expired (fs, lock->token); 
    }

  *lock_p = lock;
  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_fs__get_lock_from_token (svn_lock_t **lock,
                                svn_fs_t *fs,
                                const char *token,
                                apr_pool_t *pool)
{
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0,
                           "Function not yet implemented.");
}



svn_error_t *
svn_fs_fs__get_locks (apr_hash_t **locks,
                      svn_fs_t *fs,
                      const char *path,
                      apr_pool_t *pool)
{
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0,
                           "Function not yet implemented.");
}
