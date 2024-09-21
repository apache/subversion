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

#include "svn_fs.h"
#include "svn_delta.h"
#include "svn_version.h"
#include "svn_pools.h"
#include "fs.h"
#include "fs_fs.h"
#include "fs_init.h"
#include "tree.h"
#include "lock.h"
#include "hotcopy.h"
#include "id.h"
#include "pack.h"
#include "recovery.h"
#include "rep-cache.h"
#include "revprops.h"
#include "transaction.h"
#include "util.h"
#include "verify.h"
#include "svn_private_config.h"
#include "private/svn_fs_util.h"
#include "private/svn_fs_fs_private.h"

#include "../libsvn_fs/fs-loader.h"

/* A prefix for the pool userdata variables used to hold
   per-filesystem shared data.  See fs_serialized_init. */
#define SVN_FSFS_SHARED_USERDATA_PREFIX "svn-fsfs-shared-"



/* Initialize the part of FS that requires global serialization across all
   instances.  The caller is responsible of ensuring that serialization.
   Use COMMON_POOL for process-wide and POOL for temporary allocations. */
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
     in a subpool, add a reference-count, and add a serialized destructor
     to the FS vtable.  That's more machinery than it's worth.

     Picking an appropriate key for the shared data is tricky, because,
     unfortunately, a filesystem UUID is not really unique.  It is implicitly
     shared between hotcopied (1), dump / loaded (2) or naively copied (3)
     filesystems.  We tackle this problem by using a combination of the UUID
     and an instance ID as the key.  This allows us to avoid key clashing
     in (1) and (2) for formats >= SVN_FS_FS__MIN_INSTANCE_ID_FORMAT, which
     do support instance IDs.  For old formats the shared data (locks, shared
     transaction data, ...) will still clash.

     Speaking of (3), there is not so much we can do about it, except maybe
     provide a convenient way of fixing things.  Naively copied filesystems
     have identical filesystem UUIDs *and* instance IDs.  With the key being
     a combination of these two, clashes can be fixed by changing either of
     them (or both), e.g. with svn_fs_set_uuid(). */

  SVN_ERR_ASSERT(fs->uuid);
  SVN_ERR_ASSERT(ffd->instance_id);

  key = apr_pstrcat(pool, SVN_FSFS_SHARED_USERDATA_PREFIX,
                    fs->uuid, ":", ffd->instance_id, SVN_VA_NULL);
  status = apr_pool_userdata_get(&val, key, common_pool);
  if (status)
    return svn_error_wrap_apr(status, _("Can't fetch FSFS shared data"));
  ffsd = val;

  if (!ffsd)
    {
      ffsd = apr_pcalloc(common_pool, sizeof(*ffsd));
      ffsd->common_pool = common_pool;

      /* POSIX fcntl locks are per-process, so we need a mutex for
         intra-process synchronization when grabbing the repository write
         lock. */
      SVN_ERR(svn_mutex__init(&ffsd->fs_write_lock,
                              SVN_FS_FS__USE_LOCK_MUTEX, common_pool));

      /* ... the pack lock ... */
      SVN_ERR(svn_mutex__init(&ffsd->fs_pack_lock,
                              SVN_FS_FS__USE_LOCK_MUTEX, common_pool));

      /* ... not to mention locking the txn-current file. */
      SVN_ERR(svn_mutex__init(&ffsd->txn_current_lock,
                              SVN_FS_FS__USE_LOCK_MUTEX, common_pool));

      /* We also need a mutex for synchronizing access to the active
         transaction list and free transaction pointer. */
      SVN_ERR(svn_mutex__init(&ffsd->txn_list_lock, TRUE, common_pool));

      key = apr_pstrdup(common_pool, key);
      status = apr_pool_userdata_set(ffsd, key, NULL, common_pool);
      if (status)
        return svn_error_wrap_apr(status, _("Can't store FSFS shared data"));
    }

  ffd->shared = ffsd;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__initialize_shared_data(svn_fs_t *fs,
                                  svn_mutex__t *common_pool_lock,
                                  apr_pool_t *pool,
                                  apr_pool_t *common_pool)
{
  SVN_MUTEX__WITH_LOCK(common_pool_lock,
                       fs_serialized_init(fs, common_pool, pool));

  return SVN_NO_ERROR;
}



static svn_error_t *
fs_refresh_revprops(svn_fs_t *fs,
                    apr_pool_t *scratch_pool)
{
  svn_fs_fs__reset_revprop_cache(fs);

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

struct fs_freeze_baton_t {
  svn_fs_t *fs;
  svn_fs_freeze_func_t freeze_func;
  void *freeze_baton;
};

static svn_error_t *
fs_freeze_body(void *baton,
               apr_pool_t *pool)
{
  struct fs_freeze_baton_t *b = baton;
  svn_boolean_t exists;

  SVN_ERR(svn_fs_fs__exists_rep_cache(&exists, b->fs, pool));
  if (exists)
    SVN_ERR(svn_fs_fs__with_rep_cache_lock(b->fs,
                                           b->freeze_func, b->freeze_baton,
                                           pool));
  else
    SVN_ERR(b->freeze_func(b->freeze_baton, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_freeze_body2(void *baton,
                apr_pool_t *pool)
{
  struct fs_freeze_baton_t *b = baton;
  SVN_ERR(svn_fs_fs__with_write_lock(b->fs, fs_freeze_body, baton, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_freeze(svn_fs_t *fs,
          svn_fs_freeze_func_t freeze_func,
          void *freeze_baton,
          apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  struct fs_freeze_baton_t b;

  b.fs = fs;
  b.freeze_func = freeze_func;
  b.freeze_baton = freeze_baton;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  if (ffd->format >= SVN_FS_FS__MIN_PACK_LOCK_FORMAT)
    SVN_ERR(svn_fs_fs__with_pack_lock(fs, fs_freeze_body2, &b, pool));
  else
    SVN_ERR(fs_freeze_body2(&b, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_info(const void **fsfs_info,
        svn_fs_t *fs,
        apr_pool_t *result_pool,
        apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_fs_fsfs_info_t *info = apr_palloc(result_pool, sizeof(*info));
  info->fs_type = SVN_FS_TYPE_FSFS;
  info->shard_size = ffd->max_files_per_dir;
  info->min_unpacked_rev = ffd->min_unpacked_rev;
  info->log_addressing = ffd->use_log_addressing;
  *fsfs_info = info;
  return SVN_NO_ERROR;
}

/* Wrapper around svn_fs_fs__set_uuid() adapting between function
   signatures. */
static svn_error_t *
fs_set_uuid(svn_fs_t *fs,
            const char *uuid,
            apr_pool_t *pool)
{
  /* Whenever we set a new UUID, imply that FS will also be a different
   * instance (on formats that support this). */
  return svn_error_trace(svn_fs_fs__set_uuid(fs, uuid, NULL, pool));
}


static svn_error_t *
fs_ioctl(svn_fs_t *fs, svn_fs_ioctl_code_t ctlcode,
         void *input_void, void **output_p,
         svn_cancel_func_t cancel_func,
         void *cancel_baton,
         apr_pool_t *result_pool,
         apr_pool_t *scratch_pool)
{
  if (strcmp(ctlcode.fs_type, SVN_FS_TYPE_FSFS) == 0)
    {
      if (ctlcode.code == SVN_FS_FS__IOCTL_GET_STATS.code)
        {
          svn_fs_fs__ioctl_get_stats_input_t *input = input_void;
          svn_fs_fs__ioctl_get_stats_output_t *output;

          output = apr_pcalloc(result_pool, sizeof(*output));
          SVN_ERR(svn_fs_fs__get_stats(&output->stats, fs,
                                       input->progress_func,
                                       input->progress_baton,
                                       cancel_func, cancel_baton,
                                       result_pool, scratch_pool));
          *output_p = output;
          return SVN_NO_ERROR;
        }
      else if (ctlcode.code == SVN_FS_FS__IOCTL_DUMP_INDEX.code)
        {
          svn_fs_fs__ioctl_dump_index_input_t *input = input_void;

          SVN_ERR(svn_fs_fs__dump_index(fs, input->revision,
                                        input->callback_func,
                                        input->callback_baton,
                                        cancel_func, cancel_baton,
                                        scratch_pool));
          *output_p = NULL;
          return SVN_NO_ERROR;
        }
      else if (ctlcode.code == SVN_FS_FS__IOCTL_LOAD_INDEX.code)
        {
          svn_fs_fs__ioctl_load_index_input_t *input = input_void;

          SVN_ERR(svn_fs_fs__load_index(fs, input->revision, input->entries,
                                        scratch_pool));
          *output_p = NULL;
          return SVN_NO_ERROR;
        }
      else if (ctlcode.code == SVN_FS_FS__IOCTL_REVISION_SIZE.code)
        {
          svn_fs_fs__ioctl_revision_size_input_t *input = input_void;
          svn_fs_fs__ioctl_revision_size_output_t *output
            = apr_pcalloc(result_pool, sizeof(*output));

          SVN_ERR(svn_fs_fs__revision_size(&output->rev_size,
                                           fs, input->revision,
                                           scratch_pool));
          *output_p = output;
          return SVN_NO_ERROR;
        }
      else if (ctlcode.code == SVN_FS_FS__IOCTL_BUILD_REP_CACHE.code)
        {
          svn_fs_fs__ioctl_build_rep_cache_input_t *input = input_void;

          SVN_ERR(svn_fs_fs__build_rep_cache(fs,
                                             input->start_rev,
                                             input->end_rev,
                                             input->progress_func,
                                             input->progress_baton,
                                             cancel_func,
                                             cancel_baton,
                                             scratch_pool));

          *output_p = NULL;
          return SVN_NO_ERROR;
        }
    }

  return svn_error_create(SVN_ERR_FS_UNRECOGNIZED_IOCTL_CODE, NULL, NULL);
}

/* The vtable associated with a specific open filesystem. */
static fs_vtable_t fs_vtable = {
  svn_fs_fs__youngest_rev,
  fs_refresh_revprops,
  svn_fs_fs__revision_prop,
  svn_fs_fs__get_revision_proplist,
  svn_fs_fs__change_rev_prop,
  fs_set_uuid,
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
  svn_fs_fs__info_format,
  svn_fs_fs__info_config_files,
  fs_info,
  svn_fs_fs__verify_root,
  fs_freeze,
  fs_set_errcall,
  fs_ioctl
};


/* Creating a new filesystem. */

/* Set up vtable and fsap_data fields in FS. */
static svn_error_t *
initialize_fs_struct(svn_fs_t *fs)
{
  fs_fs_data_t *ffd = apr_pcalloc(fs->pool, sizeof(*ffd));
  ffd->use_log_addressing = FALSE;
  ffd->revprop_prefix = 0;
  ffd->flush_to_disk = TRUE;

  fs->vtable = &fs_vtable;
  fs->fsap_data = ffd;
  return SVN_NO_ERROR;
}

/* Reset vtable and fsap_data fields in FS such that the FS is basically
 * closed now.  Note that FS must not hold locks when you call this. */
static void
uninitialize_fs_struct(svn_fs_t *fs)
{
  fs->vtable = NULL;
  fs->fsap_data = NULL;
}

/* This implements the fs_library_vtable_t.create() API.  Create a new
   fsfs-backed Subversion filesystem at path PATH and link it into
   *FS.  Perform temporary allocations in POOL, and fs-global allocations
   in COMMON_POOL.  The latter must be serialized using COMMON_POOL_LOCK. */
static svn_error_t *
fs_create(svn_fs_t *fs,
          const char *path,
          svn_mutex__t *common_pool_lock,
          apr_pool_t *scratch_pool,
          apr_pool_t *common_pool)
{
  SVN_ERR(svn_fs__check_fs(fs, FALSE));

  SVN_ERR(initialize_fs_struct(fs));

  SVN_ERR(svn_fs_fs__create(fs, path, scratch_pool));

  SVN_ERR(svn_fs_fs__initialize_caches(fs, scratch_pool));
  SVN_MUTEX__WITH_LOCK(common_pool_lock,
                       fs_serialized_init(fs, common_pool, scratch_pool));

  return SVN_NO_ERROR;
}



/* Gaining access to an existing filesystem.  */

/* This implements the fs_library_vtable_t.open() API.  Open an FSFS
   Subversion filesystem located at PATH, set *FS to point to the
   correct vtable for the filesystem.  Use POOL for any temporary
   allocations, and COMMON_POOL for fs-global allocations.
   The latter must be serialized using COMMON_POOL_LOCK. */
static svn_error_t *
fs_open(svn_fs_t *fs,
        const char *path,
        svn_mutex__t *common_pool_lock,
        apr_pool_t *scratch_pool,
        apr_pool_t *common_pool)
{
  apr_pool_t *subpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_fs__check_fs(fs, FALSE));

  SVN_ERR(initialize_fs_struct(fs));

  SVN_ERR(svn_fs_fs__open(fs, path, subpool));

  SVN_ERR(svn_fs_fs__initialize_caches(fs, subpool));
  SVN_MUTEX__WITH_LOCK(common_pool_lock,
                       fs_serialized_init(fs, common_pool, subpool));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}



/* This implements the fs_library_vtable_t.open_for_recovery() API. */
static svn_error_t *
fs_open_for_recovery(svn_fs_t *fs,
                     const char *path,
                     svn_mutex__t *common_pool_lock,
                     apr_pool_t *pool,
                     apr_pool_t *common_pool)
{
  svn_error_t * err;
  svn_revnum_t youngest_rev;
  apr_pool_t * subpool = svn_pool_create(pool);

  /* Recovery for FSFS is currently limited to recreating the 'current'
     file from the latest revision. */

  /* The only thing we have to watch out for is that the 'current' file
     might not exist or contain garbage.  So we'll try to read it here
     and provide or replace the existing file if we couldn't read it.
     (We'll also need it to exist later anyway as a source for the new
     file's permissions). */

  /* Use a partly-filled fs pointer first to create 'current'. */
  fs->path = apr_pstrdup(fs->pool, path);

  SVN_ERR(initialize_fs_struct(fs));

  /* Figure out the repo format and check that we can even handle it. */
  SVN_ERR(svn_fs_fs__read_format_file(fs, subpool));

  /* Now, read 'current' and try to patch it if necessary. */
  err = svn_fs_fs__youngest_rev(&youngest_rev, fs, subpool);
  if (err)
    {
      const char *file_path;

      /* 'current' file is missing or contains garbage.  Since we are trying
       * to recover from whatever problem there is, being picky about the
       * error code here won't do us much good.  If there is a persistent
       * problem that we can't fix, it will show up when we try rewrite the
       * file a few lines further below and we will report the failure back
       * to the caller.
       *
       * Start recovery with HEAD = 0. */
      svn_error_clear(err);
      file_path = svn_fs_fs__path_current(fs, subpool);

      /* Best effort to ensure the file exists and is valid.
       * This may fail for r/o filesystems etc. */
      SVN_ERR(svn_io_remove_file2(file_path, TRUE, subpool));
      SVN_ERR(svn_io_file_create_empty(file_path, subpool));
      SVN_ERR(svn_fs_fs__write_current(fs, 0, 1, 1, subpool));
    }

  uninitialize_fs_struct(fs);
  svn_pool_destroy(subpool);

  /* Now open the filesystem properly by calling the vtable method directly. */
  return fs_open(fs, path, common_pool_lock, pool, common_pool);
}



/* This implements the fs_library_vtable_t.upgrade_fs() API. */
static svn_error_t *
fs_upgrade(svn_fs_t *fs,
           const char *path,
           svn_fs_upgrade_notify_t notify_func,
           void *notify_baton,
           svn_cancel_func_t cancel_func,
           void *cancel_baton,
           svn_mutex__t *common_pool_lock,
           apr_pool_t *pool,
           apr_pool_t *common_pool)
{
  SVN_ERR(fs_open(fs, path, common_pool_lock, pool, common_pool));
  return svn_fs_fs__upgrade(fs, notify_func, notify_baton,
                            cancel_func, cancel_baton, pool);
}

static svn_error_t *
fs_verify(svn_fs_t *fs, const char *path,
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
  SVN_ERR(fs_open(fs, path, common_pool_lock, pool, common_pool));
  return svn_fs_fs__verify(fs, start, end, notify_func, notify_baton,
                           cancel_func, cancel_baton, pool);
}

static svn_error_t *
fs_pack(svn_fs_t *fs,
        const char *path,
        svn_fs_pack_notify_t notify_func,
        void *notify_baton,
        svn_cancel_func_t cancel_func,
        void *cancel_baton,
        svn_mutex__t *common_pool_lock,
        apr_pool_t *pool,
        apr_pool_t *common_pool)
{
  SVN_ERR(fs_open(fs, path, common_pool_lock, pool, common_pool));
  return svn_fs_fs__pack(fs, 0, notify_func, notify_baton,
                         cancel_func, cancel_baton, pool);
}




/* This implements the fs_library_vtable_t.hotcopy() API.  Copy a
   possibly live Subversion filesystem SRC_FS from SRC_PATH to a
   DST_FS at DEST_PATH. If INCREMENTAL is TRUE, make an effort not to
   re-copy data which already exists in DST_FS.
   The CLEAN_LOGS argument is ignored and included for Subversion
   1.0.x compatibility.  Indicate progress via the optional NOTIFY_FUNC
   callback using NOTIFY_BATON.  Perform all temporary allocations in POOL. */
static svn_error_t *
fs_hotcopy(svn_fs_t *src_fs,
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
  SVN_ERR(fs_open(src_fs, src_path, common_pool_lock, pool, common_pool));

  SVN_ERR(svn_fs__check_fs(dst_fs, FALSE));
  SVN_ERR(initialize_fs_struct(dst_fs));

  /* In INCREMENTAL mode, svn_fs_fs__hotcopy() will open DST_FS.
     Otherwise, it's not an FS yet --- possibly just an empty dir --- so
     can't be opened.
   */
  return svn_fs_fs__hotcopy(src_fs, dst_fs, src_path, dst_path,
                            incremental, notify_func, notify_baton,
                            cancel_func, cancel_baton, common_pool_lock,
                            pool, common_pool);
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
  return svn_error_trace(svn_io_remove_dir2(path, FALSE, NULL, NULL, pool));
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

static svn_error_t *
fs_set_svn_fs_open(svn_fs_t *fs,
                   svn_error_t *(*svn_fs_open_)(svn_fs_t **,
                                                const char *,
                                                apr_hash_t *,
                                                apr_pool_t *,
                                                apr_pool_t *))
{
  fs_fs_data_t *ffd = fs->fsap_data;
  ffd->svn_fs_open_ = svn_fs_open_;
  return SVN_NO_ERROR;
}

static void *
fs_info_dup(const void *fsfs_info_void,
            apr_pool_t *result_pool)
{
  /* All fields are either ints or static strings. */
  const svn_fs_fsfs_info_t *fsfs_info = fsfs_info_void;
  return apr_pmemdup(result_pool, fsfs_info, sizeof(*fsfs_info));
}


/* Base FS library vtable, used by the FS loader library. */

static fs_library_vtable_t library_vtable = {
  fs_version,
  fs_create,
  fs_open,
  fs_open_for_recovery,
  fs_upgrade,
  fs_verify,
  fs_delete_fs,
  fs_hotcopy,
  fs_get_description,
  svn_fs_fs__recover,
  fs_pack,
  fs_logfiles,
  NULL /* parse_id */,
  fs_set_svn_fs_open,
  fs_info_dup,
  NULL /* ioctl */
};

svn_error_t *
svn_fs_fs__init(const svn_version_t *loader_version,
                fs_library_vtable_t **vtable, apr_pool_t* common_pool)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_delta", svn_delta_version },
      { "svn_fs_util", svn_fs_util__version },
      { NULL, NULL }
    };

  /* Simplified version check to make sure we can safely use the
     VTABLE parameter. The FS loader does a more exhaustive check. */
  if (loader_version->major != SVN_VER_MAJOR)
    return svn_error_createf(SVN_ERR_VERSION_MISMATCH, NULL,
                             _("Unsupported FS loader version (%d) for fsfs"),
                             loader_version->major);
  SVN_ERR(svn_ver_check_list2(fs_version(), checklist, svn_ver_equal));

  *vtable = &library_vtable;
  return SVN_NO_ERROR;
}
