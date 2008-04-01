/* fs.c --- creating, opening and closing filesystems
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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
#include "svn_pools.h"
#include "fs.h"
#include "err.h"
#include "fs_fs.h"
#include "tree.h"
#include "lock.h"
#include "id.h"
#include "dag.h"
#include "svn_private_config.h"
#include "private/svn_fs_util.h"

#include "../libsvn_fs/fs-loader.h"

/* Prefixes for the pool userdata variables used to hold
   per-filesystem shared data.  See fs_serialized_init. */
#define SVN_FSFS_SHARED_USERDATA_PREFIX "svn-fsfs-shared-data-"
#define SVN_FSFS_SHARED_CACHES_USERDATA_PREFIX "svn-fsfs-shared-caches-"


static svn_cache_dup_func_t dup_ids, dup_dir_listing;

static svn_error_t *
fs_serialized_init(svn_fs_t *fs, apr_pool_t *common_pool, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *key;
  void *val;
  fs_fs_shared_data_t *ffsd;
  fs_fs_shared_caches_t *ffsc;
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

  /* See the documentation before fs_fs_shared_data_t in fs.h for why
     there are two separate objects here. */

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

      /* ... not to mention locking the txn-current file. */
      status = apr_thread_mutex_create(&ffsd->txn_current_lock,
                                       APR_THREAD_MUTEX_DEFAULT, common_pool);
      if (status)
        return svn_error_wrap_apr(status,
                                  _("Can't create FSFS txn-current mutex"));
#endif

      key = apr_pstrdup(common_pool, key);
      status = apr_pool_userdata_set(ffsd, key, NULL, common_pool);
      if (status)
        return svn_error_wrap_apr(status, _("Can't store FSFS shared data"));
    }

  ffd->shared = ffsd;

  /* Now do it all again for the caches; this key contains the path. */
  key = apr_pstrcat(pool, SVN_FSFS_SHARED_CACHES_USERDATA_PREFIX, ffd->uuid,
                    "/", fs->path, (char *) NULL);
  status = apr_pool_userdata_get(&val, key, common_pool);
  if (status)
    return svn_error_wrap_apr(status, _("Can't fetch FSFS shared caches data"));
  ffsc = val;

  if (!ffsc)
    {
      ffsc = apr_pcalloc(common_pool, sizeof(*ffsc));

      /* Make the cache for revision roots.  For the vast majority of
       * commands, this is only going to contain a few entries (svnadmin
       * dump/verify is an exception here), so to reduce overhead let's
       * try to keep it to just one page.  I estimate each entry has about
       * 72 bytes of overhead (svn_revnum_t key, svn_fs_id_t +
       * id_private_t + 3 strings for value, and the cache_entry); the
       * default pool size is 8192, so about a hundred should fit
       * comfortably. */
      SVN_ERR(svn_cache_create_inprocess(&(ffsc->rev_root_id_cache),
                                         dup_ids, sizeof(svn_revnum_t),
                                         1, 100, TRUE, common_pool));

      /* Rough estimate: revision DAG nodes have size around 320 bytes, so
       * let's put 16 on a page. */
      SVN_ERR(svn_cache_create_inprocess(&(ffsc->rev_node_cache),
                                         svn_fs_fs__dag_dup_for_cache,
                                         APR_HASH_KEY_STRING,
                                         1024, 16, TRUE, common_pool));

      /* Very rough estimate: 1K per directory. */
      SVN_ERR(svn_cache_create_inprocess(&(ffsc->dir_cache),
                                         dup_dir_listing, APR_HASH_KEY_STRING,
                                         1024, 8, TRUE, common_pool));

      key = apr_pstrdup(common_pool, key);
      status = apr_pool_userdata_set(ffsc, key, NULL, common_pool);
      if (status)
        return svn_error_wrap_apr(status, _("Can't store FSFS shared caches data"));
    }

  ffd->shared_caches = ffsc;

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

/* Duplicate FS IDs as cache values. */
static svn_error_t *
dup_ids(void **out,
        void *in,
        apr_pool_t *pool)
{
  svn_fs_id_t *id = in;
  *out = svn_fs_fs__id_copy(id, pool);
  return SVN_NO_ERROR;
}

/* Duplicate directory listings as cache values. */
static svn_error_t *
dup_dir_listing(void **out,
                void *in,
                apr_pool_t *pool)
{
  apr_hash_t *new_entries = apr_hash_make(pool), *entries = in;
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      void *val;
      svn_fs_dirent_t *dirent, *new_dirent;

      apr_hash_this(hi, NULL, NULL, &val);
      dirent = val;
      new_dirent = apr_palloc(pool, sizeof(*new_dirent));
      new_dirent->name = apr_pstrdup(pool, dirent->name);
      new_dirent->kind = dirent->kind;
      new_dirent->id = svn_fs_fs__id_copy(dirent->id, pool);
      apr_hash_set(new_entries, new_dirent->name, APR_HASH_KEY_STRING,
                   new_dirent);
    }

  *out = new_entries;
  return SVN_NO_ERROR;
}


/* Set up vtable and fsap_data fields in FS. */
static svn_error_t *
initialize_fs_struct(svn_fs_t *fs)
{
  fs_fs_data_t *ffd = apr_pcalloc(fs->pool, sizeof(*ffd));
  fs->vtable = &fs_vtable;
  fs->fsap_data = ffd;

  return SVN_NO_ERROR;
}

/* This implements the fs_library_vtable_t.create() API.  Create a new
   fsfs-backed Subversion filesystem at path PATH and link it into
   *FS.  Perform temporary allocations in POOL, and fs-global allocations
   in COMMON_POOL. */
static svn_error_t *
fs_create(svn_fs_t *fs, const char *path, apr_pool_t *pool,
          apr_pool_t *common_pool)
{
  SVN_ERR(svn_fs__check_fs(fs, FALSE));

  SVN_ERR(initialize_fs_struct(fs));

  SVN_ERR(svn_fs_fs__create(fs, path, pool));
  return fs_serialized_init(fs, common_pool, pool);
}



/* Gaining access to an existing filesystem.  */

/* This implements the fs_library_vtable_t.open() API.  Open an FSFS
   Subversion filesystem located at PATH, set *FS to point to the
   correct vtable for the filesystem.  Use POOL for any temporary
   allocations, and COMMON_POOL for fs-global allocations. */
static svn_error_t *
fs_open(svn_fs_t *fs, const char *path, apr_pool_t *pool,
        apr_pool_t *common_pool)
{
  SVN_ERR(initialize_fs_struct(fs));

  SVN_ERR(svn_fs_fs__open(fs, path, pool));
  return fs_serialized_init(fs, common_pool, pool);
}



/* This implements the fs_library_vtable_t.open_for_recovery() API. */
static svn_error_t *
fs_open_for_recovery(svn_fs_t *fs,
                     const char *path,
                     apr_pool_t *pool, apr_pool_t *common_pool)
{
  /* Recovery for FSFS is currently limited to recreating the current
     file from the latest revision. */

  /* The only thing we have to watch out for is that the current file
     might not exist.  So we'll try to create it here unconditionally,
     and just ignore any errors that might indicate that it's already
     present. (We'll need it to exist later anyway as a source for the
     new file's permissions). */

  /* Use a partly-filled fs pointer first to create current.  This will fail
     if current already exists, but we don't care about that. */
  fs->path = apr_pstrdup(fs->pool, path);
  svn_error_clear(svn_io_file_create(svn_fs_fs__path_current(fs, pool),
                                     "0 1 1\n", pool));

  /* Now open the filesystem properly by calling the vtable method directly. */
  return fs_open(fs, path, pool, common_pool);
}



/* This implements the fs_library_vtable_t.uprade_fs() API. */
static svn_error_t *
fs_upgrade(svn_fs_t *fs, const char *path, apr_pool_t *pool,
           apr_pool_t *common_pool)
{
  SVN_ERR(svn_fs__check_fs(fs, FALSE));
  SVN_ERR(initialize_fs_struct(fs));
  SVN_ERR(svn_fs_fs__open(fs, path, pool));
  SVN_ERR(fs_serialized_init(fs, common_pool, pool));
  return svn_fs_fs__upgrade(fs, pool);
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
  return svn_fs_fs__hotcopy(src_path, dest_path, pool);
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
  return svn_io_remove_dir2(path, FALSE, NULL, NULL, pool);
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
  fs_open_for_recovery,
  fs_upgrade,
  fs_delete_fs,
  fs_hotcopy,
  fs_get_description,
  svn_fs_fs__recover,
  fs_logfiles
};

svn_error_t *
svn_fs_fs__init(const svn_version_t *loader_version,
                fs_library_vtable_t **vtable, apr_pool_t* common_pool)
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
