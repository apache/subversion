/* fs.c --- creating, opening and closing filesystems
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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

#include <apr_general.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_thread_mutex.h>

#include "svn_fs.h"
#include "svn_delta.h"
#include "svn_version.h"
#include "fs.h"
#include "err.h"
#include "dag.h"
#include "fs_fs.h"
#include "revs-txns.h"
#include "tree.h"
#include "lock.h"
#include "svn_private_config.h"

#include "../libsvn_fs/fs-loader.h"

/* A prefix for the pool userdata variables used to hold
   per-filesystem shared data.  See fs_serialized_init. */
#define SVN_FSFS_SHARED_USERDATA_PREFIX "svn-fsfs-shared-"



/* If filesystem FS is already open, then return an
   SVN_ERR_FS_ALREADY_OPEN error.  Otherwise, return zero.  */
static svn_error_t *
check_already_open(svn_fs_t *fs)
{
  if (fs->fsap_data)
    return svn_error_create(SVN_ERR_FS_ALREADY_OPEN, 0,
                            _("Filesystem object already open"));
  else
    return SVN_NO_ERROR;
}



static svn_error_t *
fs_serialized_init(svn_fs_t *fs, apr_pool_t *common_pool, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *key;
  void *val;
  fs_fs_shared_data_t *ffsd;
  apr_status_t status;

  /* Note that we are allocating a small amount of long-lived data for
     each separate repository opened during the lifetime of the
     svn_fs_initialize pool.  It's unlikely that anyone will notice
     the modest expenditure; the alternative is to allocate each structure
     in a subpool, add a reference-count, and add a serialized deconstructor
     to the FS vtable.  That's more machinery than it's worth.

     Using the uuid to obtain the lock creates a corner case if a
     caller uses svn_fs_set_uuid on the repository in a process where
     other threads might be using the same repository through another
     FS object.  The only real-world consumer of svn_fs_set_uuid is
     "svnadmin load", so this is a low-priority problem, and we don't
     know of a better way of associating such data with the
     repository. */

  key = apr_pstrcat(pool, SVN_FSFS_SHARED_USERDATA_PREFIX, ffd->uuid,
                    (char *) NULL);
  status = apr_pool_userdata_get(&val, key, common_pool);
  if (status)
    return svn_error_wrap_apr(status, _("Can't fetch FSFS shared data"));
  ffsd = val;

  if (!ffsd)
    {
      ffsd = apr_pcalloc(common_pool, sizeof(*ffsd));
      ffsd->common_pool = common_pool;

#if APR_HAS_THREADS
      /* POSIX fcntl locks are per-process, so we need a mutex for
         intra-process synchronization when grabbing the repository write
         lock. */
      status = apr_thread_mutex_create(&ffsd->fs_write_lock,
                                       APR_THREAD_MUTEX_DEFAULT, common_pool);
      if (status)
        return svn_error_wrap_apr(status,
                                  _("Can't create FSFS write-lock mutex"));

      /* We also need a mutex for synchronising access to the active
         transaction list and free transaction pointer. */
      status = apr_thread_mutex_create(&ffsd->txn_list_lock,
                                       APR_THREAD_MUTEX_DEFAULT, common_pool);
      if (status)
        return svn_error_wrap_apr(status,
                                  _("Can't create FSFS txn list mutex"));
#endif

      key = apr_pstrdup(common_pool, key);
      status = apr_pool_userdata_set(ffsd, key, NULL, common_pool);
      if (status)
        return svn_error_wrap_apr(status, _("Can't store FSFS shared data"));
    }

  ffd->shared = ffsd;

  return SVN_NO_ERROR;
}



/* This function is provided for Subversion 1.0.x compatibility.  It
   has no effect for fsfs backed Subversion filesystems.  It conforms
   to the fs_library_vtable_t.bdb_set_errcall() API. */
static svn_error_t *
fs_set_errcall(svn_fs_t *fs,
               void (*db_errcall_fcn)(const char *errpfx, char *msg))
{

  return SVN_NO_ERROR;
}



/* The vtable associated with a specific open filesystem. */
static fs_vtable_t fs_vtable = {
  fs_serialized_init,
  svn_fs_fs__youngest_rev,
  svn_fs_fs__revision_prop,
  svn_fs_fs__revision_proplist,
  svn_fs_fs__change_rev_prop,
  svn_fs_fs__get_uuid,
  svn_fs_fs__set_uuid,
  svn_fs_fs__revision_root,
  svn_fs_fs__begin_txn,
  svn_fs_fs__open_txn,
  svn_fs_fs__purge_txn,
  svn_fs_fs__list_transactions,
  svn_fs_fs__deltify,
  svn_fs_fs__lock,
  svn_fs_fs__generate_lock_token,
  svn_fs_fs__unlock,
  svn_fs_fs__get_lock,
  svn_fs_fs__get_locks,
  fs_set_errcall
};


/* Creating a new filesystem. */

/* This implements the fs_library_vtable_t.create() API.  Create a new
   fsfs-backed Subversion filesystem at path PATH and link it into
   *FS.  Perform temporary allocations in POOL. */
static svn_error_t *
fs_create(svn_fs_t *fs, const char *path, apr_pool_t *pool)
{
  fs_fs_data_t *ffd;

  SVN_ERR(check_already_open(fs));

  ffd = apr_pcalloc(fs->pool, sizeof(*ffd));
  fs->vtable = &fs_vtable;
  fs->fsap_data = ffd;

  SVN_ERR(svn_fs_fs__create(fs, path, pool));

  return SVN_NO_ERROR;
}



/* Gaining access to an existing filesystem.  */

/* This implements the fs_library_vtable_t.open() API.  Open an FSFS
   Subversion filesystem located at PATH, set *FS to point to the
   correct vtable for the filesystem.  Use POOL for any temporary
   allocations. */
static svn_error_t *
fs_open(svn_fs_t *fs, const char *path, apr_pool_t *pool)
{
  fs_fs_data_t *ffd;

  ffd = apr_pcalloc(fs->pool, sizeof(*ffd));
  fs->vtable = &fs_vtable;
  fs->fsap_data = ffd;

  SVN_ERR(svn_fs_fs__open(fs, path, pool));

  return SVN_NO_ERROR;
}



/* This implements the fs_library_vtable_t.hotcopy() API.  Copy a
   possibly live Subversion filesystem from SRC_PATH to DEST_PATH.
   The CLEAN_LOGS argument is ignored and included for Subversion
   1.0.x compatibility.  Perform all temporary allocations in POOL. */
static svn_error_t *
fs_hotcopy(const char *src_path, 
           const char *dest_path, 
           svn_boolean_t clean_logs, 
           apr_pool_t *pool)
{
  SVN_ERR(svn_fs_fs__hotcopy(src_path, dest_path, pool));

  return SVN_NO_ERROR;
}



/* This function is included for Subversion 1.0.x compatibility.  It has
   no effect for fsfs backed Subversion filesystems.  It conforms to
   the fs_library_vtable_t.bdb_recover() API. */
static svn_error_t *
fs_recover(const char *path,
           apr_pool_t *pool)
{
  /* This is a no-op for FSFS. */

  return SVN_NO_ERROR;
}




/* This function is included for Subversion 1.0.x compatibility.  It
   has no effect for fsfs backed Subversion filesystems.  It conforms
   to the fs_library_vtable_t.bdb_logfiles() API. */
static svn_error_t *
fs_logfiles(apr_array_header_t **logfiles,
            const char *path,
            svn_boolean_t only_unused,
            apr_pool_t *pool)
{
  /* A no-op for FSFS. */
  *logfiles = apr_array_make(pool, 0, sizeof(const char *));

  return SVN_NO_ERROR;
}





/* Delete the filesystem located at path PATH.  Perform any temporary
   allocations in POOL. */
static svn_error_t *
fs_delete_fs(const char *path,
             apr_pool_t *pool)
{
  /* Remove everything. */
  SVN_ERR(svn_io_remove_dir(path, pool));

  return SVN_NO_ERROR;
}


 
/* Miscellany */

const char *
svn_fs_fs__canonicalize_abspath(const char *path, apr_pool_t *pool)
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

static const svn_version_t *
fs_version(void)
{
  SVN_VERSION_BODY;
}

static const char *
fs_get_description(void)
{
  return _("Module for working with a plain file (FSFS) repository.");
}



/* Base FS library vtable, used by the FS loader library. */

static fs_library_vtable_t library_vtable = {
  fs_version,
  fs_create,
  fs_open,
  fs_delete_fs,
  fs_hotcopy,
  fs_get_description,
  fs_recover,
  fs_logfiles
};

svn_error_t *
svn_fs_fs__init(const svn_version_t *loader_version,
                fs_library_vtable_t **vtable)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_delta", svn_delta_version },
      { NULL, NULL }
    };

  /* Simplified version check to make sure we can safely use the
     VTABLE parameter. The FS loader does a more exhaustive check. */
  if (loader_version->major != SVN_VER_MAJOR)
    return svn_error_createf(SVN_ERR_VERSION_MISMATCH, NULL,
                             _("Unsupported FS loader version (%d) for fsfs"),
                             loader_version->major);
  SVN_ERR(svn_ver_check_list(fs_version(), checklist));

  *vtable = &library_vtable;
  return SVN_NO_ERROR;
}
