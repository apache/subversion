/* fs.c --- creating, opening and closing filesystems
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
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
#include "fs_x.h"
#include "pack.h"
#include "recovery.h"
#include "hotcopy.h"
#include "verify.h"
#include "tree.h"
#include "lock.h"
#include "id.h"
#include "revprops.h"
#include "rep-cache.h"
#include "transaction.h"
#include "svn_private_config.h"
#include "private/svn_fs_util.h"

#include "../libsvn_fs/fs-loader.h"

/* A prefix for the pool userdata variables used to hold
   per-filesystem shared data.  See fs_serialized_init. */
#define SVN_FSX_SHARED_USERDATA_PREFIX "svn-fsx-shared-"



/* Initialize the part of FS that requires global serialization across all
   instances.  The caller is responsible of ensuring that serialization.
   Use COMMON_POOL for process-wide and POOL for temporary allocations. */
static svn_error_t *
x_serialized_init(svn_fs_t *fs, apr_pool_t *common_pool, apr_pool_t *pool)
{
  fs_x_data_t *ffd = fs->fsap_data;
  const char *key;
  void *val;
  fs_x_shared_data_t *ffsd;
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

  SVN_ERR_ASSERT(fs->uuid);
  key = apr_pstrcat(pool, SVN_FSX_SHARED_USERDATA_PREFIX, fs->uuid,
                    SVN_VA_NULL);
  status = apr_pool_userdata_get(&val, key, common_pool);
  if (status)
    return svn_error_wrap_apr(status, _("Can't fetch FSX shared data"));
  ffsd = val;

  if (!ffsd)
    {
      ffsd = apr_pcalloc(common_pool, sizeof(*ffsd));
      ffsd->common_pool = common_pool;

      /* POSIX fcntl locks are per-process, so we need a mutex for
         intra-process synchronization when grabbing the repository write
         lock. */
      SVN_ERR(svn_mutex__init(&ffsd->fs_write_lock,
                              TRUE, TRUE, common_pool));

      /* ... the pack lock ... */
      SVN_ERR(svn_mutex__init(&ffsd->fs_pack_lock,
                              TRUE, TRUE, common_pool));

      /* ... not to mention locking the txn-current file. */
      SVN_ERR(svn_mutex__init(&ffsd->txn_current_lock,
                              TRUE, TRUE, common_pool));

      /* We also need a mutex for synchronizing access to the active
         transaction list and free transaction pointer. */
      SVN_ERR(svn_mutex__init(&ffsd->txn_list_lock,
                              TRUE, TRUE, common_pool));

      key = apr_pstrdup(common_pool, key);
      status = apr_pool_userdata_set(ffsd, key, NULL, common_pool);
      if (status)
        return svn_error_wrap_apr(status, _("Can't store FSX shared data"));
    }

  ffd->shared = ffsd;

  return SVN_NO_ERROR;
}



/* This function is provided for Subversion 1.0.x compatibility.  It
   has no effect for fsx backed Subversion filesystems.  It conforms
   to the fs_library_vtable_t.bdb_set_errcall() API. */
static svn_error_t *
x_set_errcall(svn_fs_t *fs,
              void (*db_errcall_fcn)(const char *errpfx, char *msg))
{

  return SVN_NO_ERROR;
}

struct x_freeze_baton_t {
  svn_fs_t *fs;
  svn_fs_freeze_func_t freeze_func;
  void *freeze_baton;
};

static svn_error_t *
x_freeze_body(void *baton,
              apr_pool_t *pool)
{
  struct x_freeze_baton_t *b = baton;
  svn_boolean_t exists;

  SVN_ERR(svn_fs_x__exists_rep_cache(&exists, b->fs, pool));
  if (exists)
    SVN_ERR(svn_fs_x__lock_rep_cache(b->fs, pool));

  SVN_ERR(b->freeze_func(b->freeze_baton, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
x_freeze(svn_fs_t *fs,
         svn_fs_freeze_func_t freeze_func,
         void *freeze_baton,
         apr_pool_t *pool)
{
  struct x_freeze_baton_t b;

  b.fs = fs;
  b.freeze_func = freeze_func;
  b.freeze_baton = freeze_baton;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));
  SVN_ERR(svn_fs_x__with_write_lock(fs, x_freeze_body, &b, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
x_info(const void **fsx_info,
       svn_fs_t *fs,
       apr_pool_t *result_pool,
       apr_pool_t *scratch_pool)
{
  fs_x_data_t *ffd = fs->fsap_data;
  svn_fs_fsx_info_t *info = apr_palloc(result_pool, sizeof(*info));
  info->fs_type = SVN_FS_TYPE_FSX;
  info->shard_size = ffd->max_files_per_dir;
  info->min_unpacked_rev = ffd->min_unpacked_rev;
  *fsx_info = info;
  return SVN_NO_ERROR;
}



/* The vtable associated with a specific open filesystem. */
static fs_vtable_t fs_vtable = {
  svn_fs_x__youngest_rev,
  svn_fs_x__revision_prop,
  svn_fs_x__revision_proplist,
  svn_fs_x__change_rev_prop,
  svn_fs_x__set_uuid,
  svn_fs_x__revision_root,
  svn_fs_x__begin_txn,
  svn_fs_x__open_txn,
  svn_fs_x__purge_txn,
  svn_fs_x__list_transactions,
  svn_fs_x__deltify,
  svn_fs_x__lock,
  svn_fs_x__generate_lock_token,
  svn_fs_x__unlock,
  svn_fs_x__get_lock,
  svn_fs_x__get_locks,
  svn_fs_x__info_format,
  svn_fs_x__info_config_files,
  x_info,
  svn_fs_x__verify_root,
  x_freeze,
  x_set_errcall
};


/* Creating a new filesystem. */

/* Set up vtable and fsap_data fields in FS. */
static svn_error_t *
initialize_fs_struct(svn_fs_t *fs)
{
  fs_x_data_t *ffd = apr_pcalloc(fs->pool, sizeof(*ffd));
  fs->vtable = &fs_vtable;
  fs->fsap_data = ffd;
  return SVN_NO_ERROR;
}

/* This implements the fs_library_vtable_t.create() API.  Create a new
   fsx-backed Subversion filesystem at path PATH and link it into
   *FS.  Perform temporary allocations in POOL, and fs-global allocations
   in COMMON_POOL.  The latter must be serialized using COMMON_POOL_LOCK. */
static svn_error_t *
x_create(svn_fs_t *fs,
         const char *path,
         svn_mutex__t *common_pool_lock,
         apr_pool_t *pool,
         apr_pool_t *common_pool)
{
  SVN_ERR(svn_fs__check_fs(fs, FALSE));

  SVN_ERR(initialize_fs_struct(fs));

  SVN_ERR(svn_fs_x__create(fs, path, pool));

  SVN_ERR(svn_fs_x__initialize_caches(fs, pool));
  SVN_MUTEX__WITH_LOCK(common_pool_lock,
                       x_serialized_init(fs, common_pool, pool));

  return SVN_NO_ERROR;
}



/* Gaining access to an existing filesystem.  */

/* This implements the fs_library_vtable_t.open() API.  Open an FSX
   Subversion filesystem located at PATH, set *FS to point to the
   correct vtable for the filesystem.  Use POOL for any temporary
   allocations, and COMMON_POOL for fs-global allocations.
   The latter must be serialized using COMMON_POOL_LOCK.  */
static svn_error_t *
x_open(svn_fs_t *fs,
       const char *path,
       svn_mutex__t *common_pool_lock,
       apr_pool_t *pool,
       apr_pool_t *common_pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(svn_fs__check_fs(fs, FALSE));

  SVN_ERR(initialize_fs_struct(fs));

  SVN_ERR(svn_fs_x__open(fs, path, subpool));

  SVN_ERR(svn_fs_x__initialize_caches(fs, subpool));
  SVN_MUTEX__WITH_LOCK(common_pool_lock,
                       x_serialized_init(fs, common_pool, subpool));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}



/* This implements the fs_library_vtable_t.open_for_recovery() API. */
static svn_error_t *
x_open_for_recovery(svn_fs_t *fs,
                    const char *path,
                    svn_mutex__t *common_pool_lock,
                    apr_pool_t *pool,
                    apr_pool_t *common_pool)
{
  /* Recovery for FSX is currently limited to recreating the 'current'
     file from the latest revision. */

  /* The only thing we have to watch out for is that the 'current' file
     might not exist.  So we'll try to create it here unconditionally,
     and just ignore any errors that might indicate that it's already
     present. (We'll need it to exist later anyway as a source for the
     new file's permissions). */

  /* Use a partly-filled fs pointer first to create 'current'.  This will fail
     if 'current' already exists, but we don't care about that. */
  fs->path = apr_pstrdup(fs->pool, path);
  svn_error_clear(svn_io_file_create(svn_fs_x__path_current(fs, pool),
                                     "0 1 1\n", pool));

  /* Now open the filesystem properly by calling the vtable method directly. */
  return x_open(fs, path, common_pool_lock, pool, common_pool);
}



/* This implements the fs_library_vtable_t.upgrade_fs() API. */
static svn_error_t *
x_upgrade(svn_fs_t *fs,
          const char *path,
          svn_fs_upgrade_notify_t notify_func,
          void *notify_baton,
          svn_cancel_func_t cancel_func,
          void *cancel_baton,
          svn_mutex__t *common_pool_lock,
          apr_pool_t *pool,
          apr_pool_t *common_pool)
{
  SVN_ERR(x_open(fs, path, common_pool_lock, pool, common_pool));
  return svn_fs_x__upgrade(fs, notify_func, notify_baton,
                           cancel_func, cancel_baton, pool);
}

static svn_error_t *
x_verify(svn_fs_t *fs, const char *path,
         svn_revnum_t start,
         svn_revnum_t end,
         svn_fs_progress_notify_func_t notify_func,
         void *notify_baton,
         svn_cancel_func_t cancel_func,
         void *cancel_baton,
         svn_mutex__t *common_pool_lock,
         apr_pool_t *pool,
         apr_pool_t *common_pool)
{
  SVN_ERR(x_open(fs, path, common_pool_lock, pool, common_pool));
  return svn_fs_x__verify(fs, start, end, notify_func, notify_baton,
                          cancel_func, cancel_baton, pool);
}

static svn_error_t *
x_pack(svn_fs_t *fs,
       const char *path,
       svn_fs_pack_notify_t notify_func,
       void *notify_baton,
       svn_cancel_func_t cancel_func,
       void *cancel_baton,
       svn_mutex__t *common_pool_lock,
       apr_pool_t *pool,
       apr_pool_t *common_pool)
{
  SVN_ERR(x_open(fs, path, common_pool_lock, pool, common_pool));
  return svn_fs_x__pack(fs, notify_func, notify_baton,
                        cancel_func, cancel_baton, pool);
}




/* This implements the fs_library_vtable_t.hotcopy() API.  Copy a
   possibly live Subversion filesystem SRC_FS from SRC_PATH to a
   DST_FS at DEST_PATH. If INCREMENTAL is TRUE, make an effort not to
   re-copy data which already exists in DST_FS.
   The CLEAN_LOGS argument is ignored and included for Subversion
   1.0.x compatibility.  The NOTIFY_FUNC and NOTIFY_BATON arguments
   are also currently ignored.  Perform all temporary allocations in POOL. */
static svn_error_t *
x_hotcopy(svn_fs_t *src_fs,
          svn_fs_t *dst_fs,
          const char *src_path,
          const char *dst_path,
          svn_boolean_t clean_logs,
          svn_boolean_t incremental,
          svn_fs_hotcopy_notify_t notify_func,
          void *notify_baton,
          svn_cancel_func_t cancel_func,
          void *cancel_baton,
          svn_mutex__t *common_pool_lock,
          apr_pool_t *pool,
          apr_pool_t *common_pool)
{
  /* Open the source repo as usual. */
  SVN_ERR(x_open(src_fs, src_path, common_pool_lock, pool, common_pool));
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  /* Provide FFD for DST_FS, test / initialize target repo, remove FFD. */
  SVN_ERR(initialize_fs_struct(dst_fs));
  SVN_ERR(svn_fs_x__hotcopy_prepare_target(src_fs, dst_fs, dst_path,
                                           incremental, pool));
  dst_fs->fsap_data = NULL;

  /* Now, the destination repo should open just fine. */
  SVN_ERR(x_open(dst_fs, dst_path, common_pool_lock, pool, common_pool));
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  /* Now, we may copy data as needed ... */
  return svn_fs_x__hotcopy(src_fs, dst_fs,
                           incremental, cancel_func, cancel_baton, pool);
}



/* This function is included for Subversion 1.0.x compatibility.  It
   has no effect for fsx backed Subversion filesystems.  It conforms
   to the fs_library_vtable_t.bdb_logfiles() API. */
static svn_error_t *
x_logfiles(apr_array_header_t **logfiles,
           const char *path,
           svn_boolean_t only_unused,
           apr_pool_t *pool)
{
  /* A no-op for FSX. */
  *logfiles = apr_array_make(pool, 0, sizeof(const char *));

  return SVN_NO_ERROR;
}





/* Delete the filesystem located at path PATH.  Perform any temporary
   allocations in POOL. */
static svn_error_t *
x_delete_fs(const char *path,
            apr_pool_t *pool)
{
  /* Remove everything. */
  return svn_io_remove_dir2(path, FALSE, NULL, NULL, pool);
}

static const svn_version_t *
x_version(void)
{
  SVN_VERSION_BODY;
}

static const char *
x_get_description(void)
{
  return _("Module for working with an experimental (FSX) repository.");
}

static svn_error_t *
x_set_svn_fs_open(svn_fs_t *fs,
                  svn_error_t *(*svn_fs_open_)(svn_fs_t **,
                                               const char *,
                                               apr_hash_t *,
                                               apr_pool_t *,
                                               apr_pool_t *))
{
  fs_x_data_t *ffd = fs->fsap_data;
  ffd->svn_fs_open_ = svn_fs_open_;
  return SVN_NO_ERROR;
}

static void *
x_info_dup(const void *fsx_info_void,
           apr_pool_t *result_pool)
{
  /* All fields are either ints or static strings. */
  const svn_fs_fsx_info_t *fsx_info = fsx_info_void;
  return apr_pmemdup(result_pool, fsx_info, sizeof(*fsx_info));
}


/* Base FS library vtable, used by the FS loader library. */

static fs_library_vtable_t library_vtable = {
  x_version,
  x_create,
  x_open,
  x_open_for_recovery,
  x_upgrade,
  x_verify,
  x_delete_fs,
  x_hotcopy,
  x_get_description,
  svn_fs_x__recover,
  x_pack,
  x_logfiles,
  NULL /* parse_id */,
  x_set_svn_fs_open,
  x_info_dup
};

svn_error_t *
svn_fs_x__init(const svn_version_t *loader_version,
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
                             _("Unsupported FS loader version (%d) for fsx"),
                             loader_version->major);
  SVN_ERR(svn_ver_check_list2(x_version(), checklist, svn_ver_equal));

  *vtable = &library_vtable;
  return SVN_NO_ERROR;
}
