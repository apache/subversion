/* fs-util.c : internal utility functions used by both FSFS and BDB back
 * ends.
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

#include <string.h>

#include <apr_pools.h>
#include <apr_strings.h>

#include "svn_fs.h"
#include "svn_path.h"
#include "svn_private_config.h"

#include "private/svn_fs_util.h"
#include "../libsvn_fs/fs-loader.h"

const char *
svn_fs__canonicalize_abspath(const char *path, apr_pool_t *pool)
{
  char *newpath;
  int path_len;
  int path_i = 0, newpath_i = 0;
  svn_boolean_t eating_slashes = FALSE;

  /* No PATH?  No problem. */
  if (! path)
    return NULL;
  
  /* Empty PATH?  That's just "/". */
  if (! *path)
    return apr_pstrdup(pool, "/");

  /* Now, the fun begins.  Alloc enough room to hold PATH with an
     added leading '/'. */
  path_len = strlen(path);
  newpath = apr_pcalloc(pool, path_len + 2);

  /* No leading slash?  Fix that. */
  if (*path != '/')
    {
      newpath[newpath_i++] = '/';
    }
  
  for (path_i = 0; path_i < path_len; path_i++)
    {
      if (path[path_i] == '/')
        {
          /* The current character is a '/'.  If we are eating up
             extra '/' characters, skip this character.  Else, note
             that we are now eating slashes. */
          if (eating_slashes)
            continue;
          eating_slashes = TRUE;
        }
      else
        {
          /* The current character is NOT a '/'.  If we were eating
             slashes, we need not do that any more. */
          if (eating_slashes)
            eating_slashes = FALSE;
        }

      /* Copy the current character into our new buffer. */
      newpath[newpath_i++] = path[path_i];
    }
  
  /* Did we leave a '/' attached to the end of NEWPATH (other than in
     the root directory case)? */
  if ((newpath[newpath_i - 1] == '/') && (newpath_i > 1))
    newpath[newpath_i - 1] = '\0';

  return newpath;
}

svn_error_t *
svn_fs__check_fs(svn_fs_t *fs)
{
  if (fs->fsap_data)
    return SVN_NO_ERROR;
  else
    return svn_error_create(SVN_ERR_FS_NOT_OPEN, 0,
                            _("Filesystem object has not been opened yet"));
}


svn_error_t *
svn_fs__err_not_mutable(svn_fs_t *fs, svn_revnum_t rev, const char *path)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NOT_MUTABLE, 0,
     _("File is not mutable: filesystem '%s', revision %ld, path '%s'"),
     fs->path, rev, path);
}


svn_error_t *
svn_fs__err_not_directory(svn_fs_t *fs, const char *path)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NOT_DIRECTORY, 0,
     _("'%s' is not a directory in filesystem '%s'"),
     path, fs->path);
}


svn_error_t *
svn_fs__err_not_file(svn_fs_t *fs, const char *path)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NOT_FILE, 0,
     _("'%s' is not a file in filesystem '%s'"),
     path, fs->path);
}


svn_error_t *
svn_fs__err_no_such_lock(svn_fs_t *fs, const char *path)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NO_SUCH_LOCK, 0,
     _("No lock on path '%s' in filesystem '%s'"),
     path, fs->path);
}


svn_error_t *
svn_fs__err_lock_expired(svn_fs_t *fs, const char *token)
{
  return
    svn_error_createf
    (SVN_ERR_FS_LOCK_EXPIRED, 0,
     _("Lock has expired:  lock-token '%s' in filesystem '%s'"),
     token, fs->path);
}


svn_error_t *
svn_fs__err_no_user(svn_fs_t *fs)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NO_USER, 0,
     _("No username is currently associated with filesystem '%s'"),
     fs->path);
}


svn_error_t *
svn_fs__err_lock_owner_mismatch(svn_fs_t *fs,
                                   const char *username,
                                   const char *lock_owner)
{
  return
    svn_error_createf
    (SVN_ERR_FS_LOCK_OWNER_MISMATCH, 0,
     _("User '%s' is trying to use a lock owned by '%s' in filesystem '%s'"),
     username, lock_owner, fs->path);
}


svn_error_t *
svn_fs__err_path_already_locked(svn_fs_t *fs,
                                   svn_lock_t *lock)
{
  return
    svn_error_createf
    (SVN_ERR_FS_PATH_ALREADY_LOCKED, 0,
     _("Path '%s' is already locked by user '%s' in filesystem '%s'"),
     lock->path, lock->owner, fs->path);
}

char *
svn_fs__next_entry_name(const char **next_p,
                        const char *path,
                        apr_pool_t *pool)
{
  const char *end;

  /* Find the end of the current component.  */
  end = strchr(path, '/');

  if (! end)
    {
      /* The path contains only one component, with no trailing
         slashes. */
      *next_p = 0;
      return apr_pstrdup(pool, path);
    }
  else
    {
      /* There's a slash after the first component.  Skip over an arbitrary
         number of slashes to find the next one. */
      const char *next = end;
      while (*next == '/')
        next++;
      *next_p = next;
      return apr_pstrndup(pool, path, end - path);
    }
}
