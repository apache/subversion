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
#include "svn_utf.h"

#include "apr_uuid.h"
#include "apr_file_io.h"
#include "apr_file_info.h"

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



static svn_error_t *
merge_paths (char **result,
             const char *p1,
             const char *p2,
             apr_pool_t *pool)
{
  apr_status_t status;
  const char *p2_rel = p2;
  if (*p2 == '/')
    p2_rel = p2 + 1;

  status = apr_filepath_merge (result, p1, p2_rel, APR_FILEPATH_NATIVE, pool);
  if (status)
    return svn_error_wrap_apr (status, _("Can't merge paths '%s' and '%s'"),
                               p1, p2);
  return SVN_NO_ERROR;
}

static svn_error_t *
abs_path_to_lock_file (char **abs_path,
                       svn_fs_t *fs,
                       const char *rel_path,
                       apr_pool_t *pool)
{
  SVN_ERR (merge_paths (abs_path, fs->path, LOCK_ROOT_DIR, pool));
  SVN_ERR (merge_paths (abs_path, *abs_path, LOCK_LOCK_DIR, pool));
  SVN_ERR (merge_paths (abs_path, *abs_path, (char *)rel_path, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
base_path_to_lock_file (char **base_path,
                        svn_fs_t *fs,
                        apr_pool_t *pool)
{
  SVN_ERR (merge_paths (base_path, fs->path, LOCK_ROOT_DIR, pool));
  SVN_ERR (merge_paths (base_path, *base_path, LOCK_LOCK_DIR, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
abs_path_to_lock_token_file (char **abs_path,
                             svn_fs_t *fs,
                             const char *token,
                             apr_pool_t *pool)
{
  SVN_ERR (merge_paths (abs_path, fs->path, LOCK_ROOT_DIR, pool));
  SVN_ERR (merge_paths (abs_path, *abs_path, LOCK_TOKEN_DIR, pool));
  SVN_ERR (merge_paths (abs_path, *abs_path, (char *)token, pool));

  return SVN_NO_ERROR;
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
  SVN_ERR (abs_path_to_lock_file (&abs_path, fs, lock->path, pool));

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

  SVN_ERR (abs_path_to_lock_token_file (&abs_path, fs, lock->token, pool));

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
  SVN_ERR (abs_path_to_lock_file (&abs_path, fs, lock->path, pool));
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
  SVN_ERR (abs_path_to_lock_token_file (&abs_path, fs, lock->token, pool));
  SVN_ERR (svn_io_remove_file (abs_path, pool));

  return SVN_NO_ERROR;
}


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

  SVN_ERR (svn_fs_fs__generate_token (&(lock->token), fs, pool));
  
  lock->path = apr_pstrdup (pool, path);
  lock->owner = apr_pstrdup (pool, owner);
  lock->comment = apr_pstrdup (pool, comment);
  /* ### this function should take a 'comment' argument!  */
  lock->creation_date = apr_time_now();

  if (timeout)
    lock->expiration_date = lock->creation_date + apr_time_from_sec(timeout);

  *lock_p = lock;
  return SVN_NO_ERROR;
}

static svn_error_t *
read_path_from_lock_token_file(svn_fs_t *fs, 
                               char **path_p,
                               const char *token, 
                               apr_pool_t *pool)
{
  char *abs_path;
  const char *abs_path_utf8;
  apr_status_t status;
  apr_finfo_t finfo;
  svn_stringbuf_t *buf;

  SVN_ERR (abs_path_to_lock_token_file (&abs_path, fs, token, pool));

  status = apr_stat (&finfo, abs_path, APR_FINFO_TYPE, pool);

  /* If token file doesn't exist, then there's no lock, so return
     immediately. */
  if (APR_STATUS_IS_ENOENT (status))
    {
      *path_p = NULL;
      return svn_fs_fs__err_bad_lock_token (fs, token);
    }      

  SVN_ERR (svn_utf_cstring_to_utf8 (&abs_path_utf8, abs_path, pool));
  
  SVN_ERR (svn_stringbuf_from_file (&buf, abs_path_utf8, pool));

  *path_p = buf->data;         
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

  SVN_ERR (abs_path_to_lock_file(&abs_path, fs, path, pool));

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
get_lock_from_path (svn_lock_t **lock_p,
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


static svn_error_t *
get_lock_from_path_helper (svn_fs_t *fs,
                           svn_lock_t **lock_p,
                           const char *path,
                           apr_pool_t *pool)
{
  svn_lock_t *lock;
  svn_error_t *err;
  
  err = get_lock_from_path (&lock, fs, path, pool);

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
                 svn_revnum_t current_rev,
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

  /* Is the caller attempting to lock an out-of-date working file? */
  if (SVN_IS_VALID_REVNUM(current_rev))
    {
      svn_revnum_t created_rev;
      SVN_ERR (svn_fs_fs__node_created_rev (&created_rev, root, path, pool));

      /* SVN_INVALID_REVNUM means the path doesn't exist.  So
         apparently somebody is trying to lock something in their
         working copy, but somebody else has deleted the thing
         from HEAD.  That counts as being 'out of date'. */     
      if (! SVN_IS_VALID_REVNUM(created_rev))
        return svn_error_createf (SVN_ERR_FS_OUT_OF_DATE, NULL,
                                  "Path '%s' doesn't exist in HEAD revision.",
                                  path);

      if (current_rev < created_rev)
        return svn_error_createf (SVN_ERR_FS_OUT_OF_DATE, NULL,
                                  "Lock failed: newer version of '%s' exists.",
                                  path);
    }

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
  SVN_ERR (generate_new_lock (&new_lock, fs, path, fs->access_ctx->username,
                              comment, timeout, pool));
  SVN_ERR (save_lock (fs, new_lock, pool));
  *lock_p = new_lock;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__attach_lock (svn_lock_t *lock,
                        svn_fs_t *fs,
                        svn_boolean_t force,
                        svn_revnum_t current_rev,
                        apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_lock_t *existing_lock;
  svn_fs_root_t *root;
  svn_revnum_t youngest;

  SVN_ERR (svn_fs_fs__check_fs (fs));

  SVN_ERR (svn_fs_youngest_rev (&youngest, fs, pool));

  SVN_ERR (svn_fs_revision_root (&root, fs, youngest, pool));

  SVN_ERR (svn_fs_fs__check_path (&kind, root, lock->path, pool));

  /* Until we implement directory locks someday, we only allow locks
     on files or non-existent paths. */
  if (kind == svn_node_dir)
    return svn_fs_fs__err_not_file (fs, lock->path);

  /* There better be a username in the incoming lock. */
  if (! lock->owner)
    {
      if (!fs->access_ctx || !fs->access_ctx->username)
        return svn_fs_fs__err_no_user (fs);
      else
        lock->owner = fs->access_ctx->username;
    }

  /* Is the caller attempting to lock an out-of-date working file? */
  if (SVN_IS_VALID_REVNUM(current_rev))
    {
      svn_revnum_t created_rev;
      SVN_ERR (svn_fs_fs__node_created_rev (&created_rev, root, lock->path, pool));

      /* SVN_INVALID_REVNUM means the path doesn't exist.  So
         apparently somebody is trying to lock something in their
         working copy, but somebody else has deleted the thing
         from HEAD.  That counts as being 'out of date'. */     
      if (! SVN_IS_VALID_REVNUM(created_rev))
        return svn_error_createf (SVN_ERR_FS_OUT_OF_DATE, NULL,
                                  "Path '%s' doesn't exist in HEAD revision.",
                                  lock->path);

      if (current_rev < created_rev)
        return svn_error_createf (SVN_ERR_FS_OUT_OF_DATE, NULL,
                                  "Lock failed: newer version of '%s' exists.",
                                  lock->path);
    }

  /* Try and get a lock from lock->path */ 
  SVN_ERR (get_lock_from_path_helper (fs, &existing_lock, lock->path, pool));

  if (existing_lock)
    {
      if (! force)
        {
          /* Sorry, the path is already locked. */
          return svn_fs_fs__err_path_locked (fs, existing_lock);
        }
      else
        {
          /* Force was passed, so lock is being stolen. Destroy the
             existing lock. */
          SVN_ERR (delete_lock (fs, existing_lock, pool));
        }          
    }

  save_lock (fs, lock, pool);

  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_fs__generate_token (const char **token,
                           svn_fs_t *fs,
                           apr_pool_t *pool)
{
  /* ### Notice that 'fs' is currently unused.  But perhaps someday,
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


/* Static helper func.

   If TOKEN points to a lock in FS, set *LOCK to an svn_lock_t which
   represents the lock, allocated in POOL.  If TOKEN doesn't point to
   a lock, return SVN_ERR_FS_BAD_LOCK_TOKEN.  If TOKEN points to a
   lock that has expired, then return SVN_ERR_FS_LOCK_EXPIRED.
*/
static svn_error_t *
get_lock_from_token (svn_lock_t **lock_p,
                     svn_fs_t *fs,
                     const char *token,
                     apr_pool_t *pool)
{
  svn_lock_t *lock;
  char *path;

  /* Read lock token from disk */
  SVN_ERR (read_path_from_lock_token_file(fs, &path, token, pool));

  SVN_ERR (get_lock_from_path (&lock, fs, path, pool));

  /* Get lock back, check for null */
  if (!lock)
    return svn_fs_fs__err_no_such_lock (fs, path);
  *lock_p = lock;
  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_fs__unlock (svn_fs_t *fs,
                   const char *path,
                   const char *token,
                   svn_boolean_t force,
                   apr_pool_t *pool)
{
  svn_lock_t *existing_lock;

  /* Sanity check:  we don't want to lookup a NULL path. */
  if (! token)
    return svn_fs_fs__err_bad_lock_token (fs, "null");
  
  /* This could return SVN_ERR_FS_BAD_LOCK_TOKEN or
     SVN_ERR_FS_LOCK_EXPIRED. */
  SVN_ERR (get_lock_from_token(&existing_lock, fs, token, pool));
  
  /* Sanity check:  the incoming path should match existing_lock->path. */
  if (strcmp(path, existing_lock->path) != 0)
    return svn_fs_fs__err_no_such_lock (fs, existing_lock->path);

  /* Unless breaking the lock, there better be a username attached to the
     fs. */
  if (!force && (!fs->access_ctx || !fs->access_ctx->username))
    return svn_fs_fs__err_no_user (fs);

  /* And that username better be the same as the lock's owner. */
  if (!force
      && strcmp(fs->access_ctx->username, existing_lock->owner) != 0)
    return svn_fs_fs__err_lock_owner_mismatch (fs,
                                               fs->access_ctx->username,
                                               existing_lock->owner);
  
  /* Remove lock and lock token files. */
  SVN_ERR (delete_lock (fs, existing_lock, pool));

  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs_fs__get_lock_from_path (svn_lock_t **lock_p,
                               svn_fs_t *fs,
                               const char *path,
                               apr_pool_t *pool)
{
  SVN_ERR (get_lock_from_path_helper (fs, lock_p, path, pool));
  return SVN_NO_ERROR;
}



struct dir_walker_baton
{
  svn_fs_t *fs;
  apr_hash_t *locks;
};


static svn_error_t *
locks_dir_walker (void *baton,
                  const char *path,
                  const apr_finfo_t *finfo,
                  apr_pool_t *pool)
{
  char *base_path;
  const char *rel_path;
  svn_lock_t *lock;
  struct dir_walker_baton *dir_baton;
  dir_baton = baton;
  
  /* Skip directories. */
  if (finfo->filetype == APR_DIR)
    return SVN_NO_ERROR;

  /* Get the repository-relative path for the lock. */
  SVN_ERR (base_path_to_lock_file (&base_path, dir_baton->fs, pool));
  rel_path = path + strlen (base_path);

  /* Get lock */
  SVN_ERR (svn_fs_fs__get_lock_from_path (&lock, dir_baton->fs, 
                                          rel_path, pool));

  /* Stuff lock in hash, keyed on lock->path */
  apr_hash_set (dir_baton->locks, lock->path, APR_HASH_KEY_STRING, lock);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__get_locks (apr_hash_t **locks,
                      svn_fs_t *fs,
                      const char *path,
                      apr_pool_t *pool)
{
  char *abs_path;
  struct dir_walker_baton baton;
  apr_finfo_t finfo;
  apr_status_t status;

  /* Make the hash that we'll return. */
  *locks = apr_hash_make(pool);

  /* Compose the absolute/rel path to PATH */
  SVN_ERR (abs_path_to_lock_file (&abs_path, fs, path, pool));

  status = apr_stat (&finfo, abs_path, APR_FINFO_TYPE, pool);

  /* If base dir doesn't exist, then we don't have any locks. */
  if (APR_STATUS_IS_ENOENT (status))
      return SVN_NO_ERROR;

  baton.fs = fs;
  baton.locks = *locks;

  SVN_ERR (svn_io_dir_walk (abs_path, APR_FINFO_TYPE, locks_dir_walker,
                            &baton, pool));

  return SVN_NO_ERROR;
}

/* Utility function:  verify that a lock can be used.

   If no username is attached to the FS, return SVN_ERR_FS_NO_USER.

   If the FS username doesn't match LOCK's owner, return
   SVN_ERR_FS_LOCK_OWNER_MISMATCH.

   If FS hasn't been supplied with a matching lock-token for LOCK,
   return SVN_ERR_FS_BAD_LOCK_TOKEN.

   Otherwise return SVN_NO_ERROR.

   ###It pains me that I had to copy and paste this and verify_locks()
      from libsvn_base. -Fitz
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


/* Utility function: verify that an entire hash of LOCKS can all be used.

   Loop over hash, call svn_fs__verify_lock() on each lock, throw any
   of the three specific errors when an usuable lock is encountered.
   If all locks are usable, return SVN_NO_ERROR.
 */
static svn_error_t *
verify_locks (svn_fs_t *fs,
              apr_hash_t *locks,
              apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first (pool, locks); hi; hi = apr_hash_next (hi))
    {
      void *lock;

      apr_hash_this (hi, NULL, NULL, &lock);
      SVN_ERR (verify_lock (fs, lock, pool));
    }

  return SVN_NO_ERROR;
}


/* The main routine for lock enforcement, used throughout libsvn_fs_fs. */
svn_error_t *
svn_fs_fs__allow_locked_operation (const char *path,
                                   svn_node_kind_t kind,
                                   svn_fs_t *fs,
                                   svn_boolean_t recurse,
                                   apr_pool_t *pool)
{
  if (kind == svn_node_dir)
    {
      if (recurse)
        {
          apr_hash_t *locks;
          
          /* Discover all locks at or below the path. */
          SVN_ERR (svn_fs_fs__get_locks (&locks, fs, path, pool));
          
          /* Easy out. */
          if (apr_hash_count (locks) == 0)
            return SVN_NO_ERROR;
          
          /* Some number of locks exist below path; are we allowed to
             change them? */
          return verify_locks (fs, locks, pool); 
        }
      /* If this function is called on a directory non-recursively,
         then just return--directory locking isn't supported, so a
         directory can't be locked. */
      return SVN_NO_ERROR;
    }

  /* We're either checking a file, or checking a dir non-recursively: */
    {
      svn_lock_t *lock;

      /* Discover any lock attached to the path. */
      SVN_ERR (get_lock_from_path_helper (fs, &lock, path, pool));

      /* Easy out. */
      if (! lock)
        return SVN_NO_ERROR;

      /* The path is locked;  are we allowed to change it? */
      return verify_lock (fs, lock, pool);
    }
}

