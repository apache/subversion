/* git-lib.c --- fs library level operations
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

#include "svn_private_config.h"

#include "private/svn_atomic.h"
#include "private/svn_fs_util.h"

#include "../libsvn_fs/fs-loader.h"
#include "fs_git.h"

static const svn_version_t *
fs_git_get_version(void)
{
  SVN_VERSION_BODY;
}

static svn_error_t *
fs_git_create(svn_fs_t *fs,
              const char *path,
              svn_mutex__t *common_pool_lock,
              apr_pool_t *scratch_pool,
              apr_pool_t *common_pool)
{
  SVN_ERR(svn_fs__check_fs(fs, FALSE));

  SVN_ERR(svn_fs_git__initialize_fs_struct(fs, scratch_pool));

  SVN_ERR(svn_fs_git__create(fs, path, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_open_fs(svn_fs_t *fs,
               const char *path,
               svn_mutex__t *common_pool_lock,
               apr_pool_t *scratch_pool,
               apr_pool_t *common_pool)
{
  SVN_ERR(svn_fs__check_fs(fs, FALSE));

  SVN_ERR(svn_fs_git__initialize_fs_struct(fs, scratch_pool));

  SVN_ERR(svn_fs_git__open(fs, path, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_open_fs_for_recovery(svn_fs_t *fs,
                            const char *path,
                            svn_mutex__t *common_pool_lock,
                            apr_pool_t *pool,
                            apr_pool_t *common_pool)
{
  apr_pool_t * subpool = svn_pool_create(pool);

  SVN_ERR(svn_fs__check_fs(fs, FALSE));

  SVN_ERR(svn_fs_git__initialize_fs_struct(fs, subpool));

  SVN_ERR(svn_fs_git__open(fs, path, subpool));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_upgrade_fs(svn_fs_t *fs,
                  const char *path,
                  svn_fs_upgrade_notify_t notify_func,
                  void *notify_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  svn_mutex__t *common_pool_lock,
                  apr_pool_t *scratch_pool,
                  apr_pool_t *common_pool)
{
  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_verify_fs(svn_fs_t *fs,
                 const char *path,
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
  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_delete_fs(const char *path,
                 apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_hotcopy(svn_fs_t *src_fs,
               svn_fs_t *dst_fs,
               const char *src_path,
               const char *dst_path,
               svn_boolean_t clean,
               svn_boolean_t incremental,
               svn_fs_hotcopy_notify_t notify_func,
               void *notify_baton,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_mutex__t *common_pool_lock,
               apr_pool_t *pool,
               apr_pool_t *common_pool)
{
  SVN_ERR(svn_fs__check_fs(src_fs, TRUE));
  SVN_ERR(svn_fs__check_fs(dst_fs, TRUE));

  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static const char *
fs_git_get_description(void)
{
  return _("Experimental module for reading a GIT repository.");
}

static svn_error_t *
fs_git_recover(svn_fs_t *fs,
               svn_cancel_func_t cancel_func, void *cancel_baton,
               apr_pool_t *pool)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  SVN_ERR(svn_fs_git__revmap_update(fs, fgf,
                                    cancel_func, cancel_baton,
                                    pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_pack_fs(svn_fs_t *fs,
               const char *path,
               svn_fs_pack_notify_t notify_func,
               void *notify_baton,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_mutex__t *common_pool_lock,
               apr_pool_t *pool,
               apr_pool_t *common_pool)
{
  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_logfiles(apr_array_header_t **logfiles,
                const char *path,
                svn_boolean_t only_unused,
                apr_pool_t *pool)
{
  /* A no-op for GIT. */
  *logfiles = apr_array_make(pool, 0, sizeof(const char *));

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_set_svn_fs_open(svn_fs_t *fs,
                       svn_error_t *(*svn_fs_open_)(svn_fs_t **,
                                                    const char *,
                                                    apr_hash_t *,
                                                    apr_pool_t *,
                                                    apr_pool_t *))
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;
  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  fgf->svn_fs_open = svn_fs_open_;

  return SVN_NO_ERROR;
}

static void *
fs_git_info_fsap_dup(const void *fsap_info,
                      apr_pool_t *result_pool)
{
  const svn_fs_git_info_t *fs_info = fsap_info;
  svn_fs_git_info_t *dup_info;

  dup_info = apr_pmemdup(result_pool, fs_info, sizeof(*fs_info));

  return dup_info;
}

static fs_library_vtable_t library_vtable =
{
  fs_git_get_version,
  fs_git_create,
  fs_git_open_fs,
  fs_git_open_fs_for_recovery,
  fs_git_upgrade_fs,
  fs_git_verify_fs,
  fs_git_delete_fs,
  fs_git_hotcopy,
  fs_git_get_description,
  fs_git_recover,
  fs_git_pack_fs,
  fs_git_logfiles,
  NULL /* parse_id */,
  fs_git_set_svn_fs_open,
  fs_git_info_fsap_dup
};

static volatile svn_atomic_t libgit2_init_state = 0;

static svn_error_t *
initialize_libgit2(void *baton,
                   apr_pool_t *pool)
{
  git_libgit2_init();

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_git__init(const svn_version_t *loader_version,
                 fs_library_vtable_t **vtable,
                 apr_pool_t* common_pool)
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
                             _("Unsupported FS loader version (%d) for fsx"),
                             loader_version->major);
  SVN_ERR(svn_ver_check_list2(fs_git_get_version(), checklist, svn_ver_equal));

  SVN_ERR(svn_atomic__init_once(&libgit2_init_state, initialize_libgit2, NULL,
                                common_pool));

  *vtable = &library_vtable;
  return SVN_NO_ERROR;
}
