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
#include "svn_md5.h"

#include "apr_uuid.h"
#include "apr_file_io.h"
#include "apr_file_info.h"
#include "apr_md5.h"

#include "lock.h"
#include "tree.h"
#include "err.h"
#include "fs_fs.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/* Names of special lock directories in the fs_fs filesystem. */
#define LOCK_ROOT_DIR "locks"

/* Names of hash keys used to store a lock for writing to disk. */
#define PATH_KEY "path"
#define TOKEN_KEY "token"
#define OWNER_KEY "owner"
#define CREATION_DATE_KEY "creation_date"
#define EXPIRATION_DATE_KEY "expiration_date"
#define COMMENT_KEY "comment"


/* Join P1 and P2 with directory separators to create RESULT. */
static svn_error_t *
merge_paths (const char **result,
             const char *p1,
             const char *p2,
             apr_pool_t *pool)
{
  apr_status_t status;
  const char *p2_rel = p2;
  char *tmp;
  if (*p2 == '/')
    p2_rel = p2 + 1;

  status = apr_filepath_merge (&tmp, p1, p2_rel, APR_FILEPATH_NATIVE, pool);
  if (status)
    return svn_error_wrap_apr (status, _("Can't merge paths '%s' and '%s'"),
                               p1, p2);
  *result = tmp;
  return SVN_NO_ERROR;
}

/* Set ABS_PATH to the absolute path to the lock file or lock entries
   file for which DIGEST is the hashed repository relative path. */


/* Where DIGEST is the MD5 hash of the path to the lock file or lock
   entries file in FS, set ABS_PATH to the absolute path to file. */
static svn_error_t *
abs_path_to_lock_digest_file (const char **abs_path,
                              svn_fs_t *fs,
                              const char *digest,
                              apr_pool_t *pool)
{
  SVN_ERR (merge_paths (abs_path, fs->path, LOCK_ROOT_DIR, pool));
  /* ###TODO create a 1 or 2 char subdir to spread the love across
     many directories */
  SVN_ERR (merge_paths (abs_path, *abs_path, digest, pool));
  
  return SVN_NO_ERROR;
}

/* Return the MD5 hash of STR. */
static const char *
make_digest (const char *str,
             apr_pool_t *pool)
{
  unsigned char digest[APR_MD5_DIGESTSIZE];

  apr_md5 (digest, str, strlen(str));
  return svn_md5_digest_to_cstring (digest, pool);
}

/* Set ABS_PATH to the absolute path to REL_PATH, where REL_PATH is
   the path to the lock file or lock entries file in FS. */
static svn_error_t *
abs_path_to_lock_file (const char **abs_path,
                       svn_fs_t *fs,
                       const char *rel_path,
                       apr_pool_t *pool)
{
  const char *digest_cstring;

  SVN_ERR (merge_paths (abs_path, fs->path, LOCK_ROOT_DIR, pool));

  digest_cstring = make_digest (rel_path, pool);

  /* ###TODO create a 1 or 2 char subdir to spread the love across
     many directories */
  SVN_ERR (merge_paths (abs_path, *abs_path, digest_cstring, pool));

  return SVN_NO_ERROR;
}

/* ###Shouldn't abs_path_to_lock_file and base_path_to_lock_file be
   using this function? */
/* ###TODO Rename this to base_lock_dir or somesuch. */
/* Set BASE_PATH to the directory in FS where lock files (and lock
   entries files) are stored. */
static svn_error_t *
base_path_to_lock_file (const char **base_path,
                        svn_fs_t *fs,
                        apr_pool_t *pool)
{
  SVN_ERR (merge_paths (base_path, fs->path, LOCK_ROOT_DIR, pool));
  /* ###TODO create a 1 or 2 char subdir to spread the love across
     many directories (and take the hash as an optional arg. */

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

/* Write each hash in ENTRIES to path, one hash per line. */
static svn_error_t *
write_entries_file (apr_hash_t *entries,
                    const char *path, 
                    apr_pool_t *pool)
{
  apr_file_t *fd;
  apr_hash_index_t *hi;
  const char *digest, *tmp_path;
  char *content;

  SVN_ERR (svn_io_open_unique_file
           (&fd, &tmp_path, path, ".tmp", FALSE, pool));

  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi)) 
    {
      const void *key;
      apr_hash_this(hi, &key, NULL, NULL);
      digest = key;
      content = apr_pstrcat (pool, digest, "\n", NULL);
      SVN_ERR (svn_io_file_write_full (fd, content, strlen(content), 
                                       NULL, pool));
    }
  
  SVN_ERR (svn_io_file_close (fd, pool));
  SVN_ERR (svn_io_file_rename (tmp_path, path, pool));

  return SVN_NO_ERROR;
}

/* Create ENTRIES and read lock entries file at PATH, adding each
   child hash as a key as a char * in ENTRIES.  The value should be
   ignored.  If EXISTING_FD is non-NULL, read entries from EXISTING_FD
   instead of PATH. */
static svn_error_t *
read_entries_file (apr_hash_t **entries,
                   const char *path, 
                   apr_file_t *existing_fd,
                   apr_pool_t *pool)
{
  svn_error_t *err;
  apr_file_t *fd;
  apr_size_t buf_len = (APR_MD5_DIGESTSIZE * 2) + 1; /* Add room for "\n". */

  *entries = apr_hash_make (pool);

  if (!existing_fd) /* Only open PATH if fd is NULL. */
    {
      err = svn_io_file_open (&fd, path, APR_READ, APR_OS_DEFAULT, pool);
      if (err && APR_STATUS_IS_ENOENT (err->apr_err))
        {
          /* If we didn't find the file, then just return an empty
             entries file. */
          svn_error_clear (err);
          return SVN_NO_ERROR;
        }
      if (err)
        return err;
    }
  else
    fd = existing_fd;

  while (1729)
    {
      apr_size_t nbytes = buf_len;
      char *buf = apr_palloc (pool, buf_len);

      err = svn_io_file_read_full (fd, buf, nbytes, &nbytes, pool);

      if (err && APR_STATUS_IS_EOF (err->apr_err))
        {
          svn_error_clear (err);
          break;
        }
      if (err)
        return err;

      buf[buf_len - 1] = '\0'; /* Strip '\n' off the end. */
      apr_hash_set (*entries, buf,
                    APR_HASH_KEY_STRING, &(buf[0])); /* Grab a byte for val.*/

    }
  /* Only close fd if we weren't passed an existing (already open) fd. */
  if (!existing_fd)
    SVN_ERR (svn_io_file_close (fd, pool));
  
  return SVN_NO_ERROR;
}

/* If PATH does not have a leading slash, prepend one and return the
   new string.  Else, just return PATH. */ 
static const char * 
repository_abs_path(const char *path,
                    apr_pool_t *pool)
{
  const char *new_path = path;

  if (path[0] != '/')
    new_path = apr_pstrcat (pool, "/", path, NULL);
  return new_path;
}



/* Set the filesystem permissions on PATH to the same as those on the
   file for the initial revision (0) of FS. */
static svn_error_t *
fix_path_perms (const char *path,
                svn_fs_t *fs, 
                apr_pool_t *pool)
{
  svn_revnum_t revnum = 0;
  const char *ref_path = svn_fs_fs__path_rev (fs, revnum, pool);

  SVN_ERR (svn_fs_fs__dup_perms (path, ref_path, pool));
  return SVN_NO_ERROR;
}



/* If ABS_PATH exists, add DIGEST_STR to it iff it's not already in
   it.  Else, create ABS_PATH with DIGEST_STR as its only member.  If
   a new file is created, CREATED_NEW_FILE is set to TRUE. */
static svn_error_t *
add_hash_to_entries_file (const char *abs_path,
                          svn_fs_t *fs,
                          const char *digest_str,
                          svn_boolean_t *created_new_file,
                          apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_file_t *fd;

  /* Try to open the existing entries file. */
  SVN_ERR (read_entries_file(&entries, abs_path, NULL, pool));

  if (apr_hash_count (entries)) /* We have an entries file. */
    {
      /* Append the MD5 hash of the next component to entries it's not
         already in there. */
      if (apr_hash_get (entries, digest_str, APR_HASH_KEY_STRING) == NULL)
        {
          apr_hash_set (entries, digest_str,
                        APR_HASH_KEY_STRING, &(digest_str)[0]);
          write_entries_file (entries, (char *)abs_path, pool);
        }
      created_new_file = FALSE;
    }
  else /* Entries file DOES NOT exist. */
    {
      char *content;

      content = apr_pstrcat (pool, digest_str, "\n", NULL);

      SVN_ERR (svn_io_file_open (&fd, abs_path, APR_WRITE | APR_CREATE, 
                                 APR_OS_DEFAULT, pool));
      SVN_ERR (svn_io_file_write_full (fd, content, strlen (content), 
                                       NULL, pool));
      SVN_ERR (svn_io_file_close (fd, pool));
      SVN_ERR (fix_path_perms (abs_path, fs, pool));
      *created_new_file = TRUE;
    }

  return SVN_NO_ERROR;
}

/* Create a directory at PATH with the same permissions as
   REF_PATH. */
static svn_error_t *
make_dir (const char *path,
          const char *ref_path,
          apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_io_dir_make (path, APR_OS_DEFAULT, pool);

  /* If no error, then we successfully created the directory--tweak
     the perms.*/
  if (!err)
    SVN_ERR (svn_fs_fs__dup_perms (path, ref_path, pool));

  /* Any error other than EEXIST is a real error. */ 
  if (err && !APR_STATUS_IS_EEXIST (err->apr_err))
    return err;

  /* If EEXIST, then clear the error */
  if (err && APR_STATUS_IS_EEXIST (err->apr_err))
    svn_error_clear (err);

  return SVN_NO_ERROR;
}


/* Join all items in COMPONENTS with directory separators to create
   PATH. */
static svn_error_t *
merge_array_components (const char **path,
                        apr_array_header_t *components,
                        apr_pool_t *pool)
{
  int i;
  *path = "/";

  for (i = 0; i < components->nelts; i++)
    {
      char *component;
      component = APR_ARRAY_IDX (components, i, char *);
      SVN_ERR (merge_paths (path, *path, component, pool));
    }
  return SVN_NO_ERROR;
}


/* Store the lock in the OS level filesystem under
   repos/db/locks/locks in a file named by composing the MD5 hash of
   lock->path. */
static svn_error_t *
write_lock_to_file (svn_fs_t *fs,
                    svn_lock_t *lock,
                    apr_pool_t *pool)
{
  apr_hash_t *hash;
  apr_file_t *fd;
  svn_stream_t *stream;
  apr_array_header_t *nodes;
  const char *abs_path, *node_name;
  const char *digest_str, *path, *parent_path, *child_path;

  /* Make sure that the base dir exists. */
  SVN_ERR (base_path_to_lock_file (&abs_path, fs, pool));
  
  SVN_ERR (make_dir (abs_path, fs->path, pool));

  path = repository_abs_path (lock->path, pool);
  nodes = svn_path_decompose (path, pool);

  /* Make sure that each parent directory has an entries file on disk,
     and that the entries file contains an entry for its child. */

  do  /* Iterate in reverse. */ 
    {
      svn_boolean_t created_new_file = FALSE;

      SVN_ERR (merge_array_components (&child_path, nodes, pool));
      digest_str = make_digest (child_path, pool);

      /* Compose the path to the parent entries file. */
      node_name = apr_array_pop (nodes);
      SVN_ERR (merge_array_components (&parent_path, nodes, pool));

      SVN_ERR (abs_path_to_lock_file (&abs_path, fs, parent_path, pool));

      /* Make sure we don't put the root hash in the root dir. */
      if ((strcmp (child_path, "/") == 0)
          && (strcmp (parent_path, "/") == 0))
        break;

      SVN_ERR (add_hash_to_entries_file (abs_path, fs, digest_str, 
                                         &created_new_file, pool));
      if (!created_new_file) /* We just added our entry to an existing dir. */
        break;
    }
  while (node_name);

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

  digest_str = make_digest (path, pool);

  SVN_ERR (abs_path_to_lock_file (&abs_path, fs, path, pool));

  SVN_ERR (svn_io_file_open (&fd, abs_path, APR_WRITE | APR_CREATE, 
                          APR_OS_DEFAULT, pool));
  
  stream = svn_stream_from_aprfile (fd, pool);
  
  SVN_ERR_W (svn_hash_write2 (hash, stream, SVN_HASH_TERMINATOR, pool),
             apr_psprintf (pool,
                           _("Cannot write lock hash to '%s'"),
                           svn_path_local_style (abs_path, pool)));
  SVN_ERR (svn_io_file_close (fd, pool));
  SVN_ERR (fix_path_perms (abs_path, fs, pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
delete_lock (svn_fs_t *fs, 
             svn_lock_t *lock,
             apr_pool_t *pool)
{
  apr_array_header_t *nodes;
  const char *abs_path, *node;
  const char *digest_str, *path, *child_path, *parent_path;

  path = repository_abs_path (lock->path, pool);
  nodes = svn_path_decompose (path, pool);

  do  /* Iterate in reverse. */ 
    {
      apr_hash_t *entries;

      SVN_ERR (merge_array_components (&child_path, nodes, pool));
      digest_str = make_digest (child_path, pool);

      /* Compose the path to the parent entries file. */
      node = apr_array_pop (nodes);
      SVN_ERR (merge_array_components (&parent_path, nodes, pool));

      /* Stop when we get to the root. */
      if ((strcmp (child_path, "/") == 0)
          && (strcmp (parent_path, "/") == 0))
        break;

      /* Remove child hash from entries, deleting the entries file if
         we've removed the last hash. */
      SVN_ERR (abs_path_to_lock_file (&abs_path, fs, parent_path, pool));
      SVN_ERR (read_entries_file (&entries, abs_path, NULL, pool));
      apr_hash_set (entries, digest_str,
                    APR_HASH_KEY_STRING, NULL); /* Delete from hash.*/

      if (apr_hash_count (entries) == 0) /* We just removed the last entry. */
        SVN_ERR (svn_io_remove_file (abs_path, pool));
      else
        {
          SVN_ERR (write_entries_file (entries, abs_path, pool));
          break; /* We're done deleting entries files. */
        }
    }
  while (node);

  digest_str = make_digest (path, pool);
  
  SVN_ERR (abs_path_to_lock_file (&abs_path, fs, path, pool));
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
  lock->creation_date = apr_time_now();

  if (timeout)
    lock->expiration_date = lock->creation_date + apr_time_from_sec(timeout);

  *lock_p = lock;
  return SVN_NO_ERROR;
}


/* Read lock from file in FS at ABS_PATH into LOCK_P.  If open
   EXISTING_FD is non-NULL, use EXISTING_FD instead of opening a new
   file. */
static svn_error_t *
read_lock_from_abs_path (svn_lock_t **lock_p,
                         svn_fs_t *fs,
                         const char *abs_path,
                         apr_file_t *existing_fd,
                         apr_pool_t *pool)
{
  svn_lock_t *lock;
  apr_hash_t *hash;
  svn_stream_t *stream;
  apr_status_t status;
  apr_file_t *fd;
  const char *val;
  apr_finfo_t finfo;

  status = apr_stat (&finfo, abs_path, APR_FINFO_TYPE, pool);
  /* If file doesn't exist, then there's no lock, so return immediately. */
  if (APR_STATUS_IS_ENOENT (status))
    {
      *lock_p = NULL;
      return svn_fs_fs__err_no_such_lock (fs, abs_path);
    }      

  if (status  && !APR_STATUS_IS_ENOENT (status))
    return svn_error_wrap_apr (status, _("Can't stat '%s'"), abs_path);

  /* Only open the file if we haven't been passed an apr_file_t. */
  if (!existing_fd)
    SVN_ERR (svn_io_file_open (&fd, abs_path, APR_READ, APR_OS_DEFAULT, pool));
  else
    fd = existing_fd;

  hash = apr_hash_make (pool);

  stream = svn_stream_from_aprfile(fd, pool);
  SVN_ERR_W (svn_hash_read2 (hash, stream, SVN_HASH_TERMINATOR, pool),
             apr_psprintf (pool, _("Can't parse '%s'"), abs_path));

  if (!existing_fd)
    SVN_ERR (svn_io_file_close (fd, pool));

  /* Create our lock and load it up. */
  lock = apr_palloc (pool, sizeof (*lock));

  val = hash_fetch (hash, PATH_KEY, pool);
  if (!val)
    return svn_fs_fs__err_invalid_lockfile (fs, PATH_KEY, abs_path);
  lock->path = val;

  val = hash_fetch (hash, TOKEN_KEY, pool);
  if (!val)
    return svn_fs_fs__err_invalid_lockfile (fs, TOKEN_KEY, abs_path);
  lock->token = val;

  val = hash_fetch (hash, OWNER_KEY, pool);
  if (!val)
    return svn_fs_fs__err_invalid_lockfile (fs, OWNER_KEY, abs_path);
  lock->owner = val;

  val = hash_fetch (hash, COMMENT_KEY, pool);
  if (val)
    lock->comment = val;
  else /* Comment optional. */
    lock->comment = NULL;

  val = hash_fetch (hash, CREATION_DATE_KEY, pool);
  if (!val)
    return svn_fs_fs__err_invalid_lockfile (fs, CREATION_DATE_KEY, abs_path);
  svn_time_from_cstring (&(lock->creation_date), val, pool);

  val = hash_fetch (hash, EXPIRATION_DATE_KEY, pool);
  if (!val || val == 0) /* No expiration date. */
    lock->expiration_date = 0;
  else
    svn_time_from_cstring (&(lock->expiration_date), val, pool);

  *lock_p = lock;

  return SVN_NO_ERROR;
}


static svn_error_t *
read_lock_from_file (svn_lock_t **lock_p,
                     svn_fs_t *fs,
                     const char *path,
                     apr_file_t *fd,
                     apr_pool_t *pool)
{
  const char *rep_path, *abs_path;
  /* - Gen MD5 hash of full path to file. */
  rep_path = repository_abs_path (path, pool);

  SVN_ERR (abs_path_to_lock_file(&abs_path, fs, path, pool));
  SVN_ERR (read_lock_from_abs_path (lock_p, fs, abs_path, fd, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
read_lock_from_hash_name (svn_lock_t **lock_p,
                          svn_fs_t *fs,
                          const char *hash,
                          apr_file_t *fd,
                          apr_pool_t *pool)
{
  const char *abs_path;

  SVN_ERR (abs_path_to_lock_digest_file (&abs_path, fs, hash, pool));
  SVN_ERR (read_lock_from_abs_path (lock_p, fs, abs_path, fd, pool));
  
  return SVN_NO_ERROR;
}


static svn_error_t *
get_lock_from_path (svn_lock_t **lock_p,
                    svn_fs_t *fs,
                    const char *path,
                    apr_pool_t *pool)
{
  svn_lock_t *lock;
    
  SVN_ERR (read_lock_from_file (&lock, fs, path, NULL, pool));

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


/* A recursive function that puts all locks under PATH in FS into
   LOCKS.  FD, if non-NULL, will be read from by read_entries_file
   instead of read_entries_file opening PATH. */
static svn_error_t *
get_locks_under_path (apr_hash_t **locks, 
                      svn_fs_t *fs, 
                      const char *path,
                      apr_file_t *fd,
                      apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  const char *child, *abs_path;
  apr_off_t offset = 0;
  
  SVN_ERR (read_entries_file (&entries, path, fd, pool));
  if (fd)
    SVN_ERR (svn_io_file_close (fd, pool));
      
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi)) 
    {
      apr_size_t nbytes = 2;
      const void *key;
      char *buf = apr_palloc (pool, nbytes);
      apr_hash_this(hi, &key, NULL, NULL);
      child = key;

      SVN_ERR (abs_path_to_lock_digest_file (&abs_path, fs, child, pool));

      SVN_ERR (svn_io_file_open (&fd, abs_path, 
                                 APR_READ, APR_OS_DEFAULT, pool));
      SVN_ERR (svn_io_file_read_full (fd, buf, nbytes, &nbytes, pool));
      SVN_ERR (svn_io_file_seek (fd, APR_SET, &offset, pool));

      if (strncmp (buf, "K ", 2) == 0) /* We have a lock file. */
        {
          svn_lock_t *lock;
          SVN_ERR (read_lock_from_hash_name (&lock, fs, child, fd, pool));
          SVN_ERR (svn_io_file_close (fd, pool));

          apr_hash_set (*locks, lock->path, APR_HASH_KEY_STRING, lock);
        }
      else /* It's another entries file.  Recurse. */ 
        SVN_ERR (get_locks_under_path (locks, fs, abs_path, fd, pool));
    }
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
  SVN_ERR (write_lock_to_file (fs, new_lock, pool));
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

  SVN_ERR (write_lock_to_file (fs, lock, pool));

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
  SVN_ERR (get_lock_from_path_helper(fs, &existing_lock, path, pool));
  
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





svn_error_t *
svn_fs_fs__get_locks (apr_hash_t **locks,
                      svn_fs_t *fs,
                      const char *path,
                      apr_pool_t *pool)
{
  apr_finfo_t finfo;
  apr_status_t status;
  const char *digest_str, *abs_path;

  /* Make the hash that we'll return. */
  *locks = apr_hash_make(pool);

  /* Compose the absolute/rel path to PATH */
  SVN_ERR (abs_path_to_lock_file (&abs_path, fs, path, pool));

  /* Strip any trailing slash. */
  if ((strlen (abs_path) > 0)
      && (abs_path[strlen (abs_path) - 1] == '/'))
    {
      char *tmp = apr_pstrdup (pool, abs_path);
      tmp[strlen (abs_path) - 1] = '\0';
      abs_path = tmp;
    }
  status = apr_stat (&finfo, abs_path, APR_FINFO_TYPE, pool);

  /* If base dir doesn't exist, then we don't have any locks. */
  if (APR_STATUS_IS_ENOENT (status))
      return SVN_NO_ERROR;

  digest_str = make_digest (path, pool);
  abs_path = repository_abs_path (path, pool);
  SVN_ERR (abs_path_to_lock_file (&abs_path, fs, path, pool));
  
  /* Recursively walk lock "tree" */
  SVN_ERR (get_locks_under_path (locks, fs, abs_path, NULL, pool));
  
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
