/* fs.c --- creating, opening and closing filesystems
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>              /* for EINVAL */
#include <db.h>

#include <apr_general.h>
#include <apr_pools.h>
#include <apr_file_io.h>

#include "svn_pools.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "fs.h"
#include "err.h"
#include "dag.h"
#include "svn_private_config.h"

/* These two includes will be removed once vtables are installed. */
#include "bdb/bdb-err.h"
#include "bdb/bdb-fs.h"


/* A default warning handling function.  */

static void
default_warning_func (void *baton, svn_error_t *err)
{
  /* The one unforgiveable sin is to fail silently.  Dumping to stderr
     or /dev/tty is not acceptable default behavior for server
     processes, since those may both be equivalent to /dev/null.  */
  abort ();
}

/* An APR pool cleanup function for a filesystem.  DATA must be a
   pointer to the filesystem to clean up.

   When the filesystem object's pool is freed, we want the resources
   held by Berkeley DB to go away, just like everything else.  So we
   register this cleanup function with the filesystem's pool, and let
   it take care of closing the databases, the environment, and any
   other DB objects we might be using.  APR calls this function before
   actually freeing the pool's memory.

   It's a pity that we can't return an svn_error_t object from an APR
   cleanup function.  For now, we return the rather generic
   SVN_ERR_FS_CLEANUP, and pass the real svn_error_t to the registered
   warning callback.  */

static apr_status_t
cleanup_fs_apr (void *data)
{
  return bdb_cleanup_fs_apr(data);
}


/* Allocating and freeing filesystem objects.  */

svn_fs_t *
svn_fs_new (apr_hash_t *fs_config, apr_pool_t *parent_pool)
{
  svn_fs_t *new_fs;

  /* Allocate a new filesystem object in its own pool, which is a
     subpool of POOL.  */
  {
    apr_pool_t *pool = svn_pool_create (parent_pool);

    new_fs = apr_pcalloc (pool, sizeof (svn_fs_t));
    new_fs->pool = pool;
  }

  new_fs->warning = default_warning_func;
  new_fs->config = fs_config;

  apr_pool_cleanup_register (new_fs->pool, new_fs,
                             cleanup_fs_apr,
                             apr_pool_cleanup_null);

  return new_fs;
}


void
svn_fs_set_warning_func (svn_fs_t *fs,
                         svn_fs_warning_callback_t warning,
                         void *warning_baton)
{
  fs->warning = warning;
  fs->warning_baton = warning_baton;
}


svn_error_t *
svn_fs_set_berkeley_errcall (svn_fs_t *fs, 
                             void (*db_errcall_fcn) (const char *errpfx,
                                                     char *msg))
{
  return bdb_set_berkeley_errcall (fs, db_errcall_fcn);
}


/* Filesystem creation/opening. */
const char *
svn_fs_berkeley_path (svn_fs_t *fs, apr_pool_t *pool)
{
  return apr_pstrdup (pool, fs->path);
}

svn_error_t *
svn_fs_create_berkeley (svn_fs_t *fs, const char *path)
{
  return bdb_create_fs (fs, path, NULL);
}


/* Gaining access to an existing filesystem.  */

svn_error_t *
svn_fs_open_berkeley (svn_fs_t *fs, const char *path)
{
  return bdb_open_fs (fs, path);
}



/* Running recovery on a filesystem.  */

svn_error_t *
svn_fs_berkeley_recover (const char *path,
                         apr_pool_t *pool)
{
  return bdb_recover_fs (path, pool);
}


/* Deleting a filesystem.  */

svn_error_t *
svn_fs_delete_berkeley (const char *path,
                        apr_pool_t *pool)
{
  return bdb_delete_fs (path, pool);
}



/* Miscellany */

const char *
svn_fs__canonicalize_abspath (const char *path, apr_pool_t *pool)
{
  char *newpath;
  int path_len;
  int path_i = 0, newpath_i = 0;
  int eating_slashes = 0;

  /* No PATH?  No problem. */
  if (! path)
    return NULL;
  
  /* Empty PATH?  That's just "/". */
  if (! *path)
    return apr_pstrdup (pool, "/");

  /* Now, the fun begins.  Alloc enough room to hold PATH with an
     added leading '/'. */
  path_len = strlen (path);
  newpath = apr_pcalloc (pool, path_len + 2);

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
          eating_slashes = 1;
        }
      else
        {
          /* The current character is NOT a '/'.  If we were eating
             slashes, we need not do that any more. */
          if (eating_slashes)
            eating_slashes = 0;
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
