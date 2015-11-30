/* git-fs.c --- the git filesystem
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

#include "svn_fs.h"
#include "svn_version.h"
#include "svn_pools.h"

#include "svn_private_config.h"

#include "private/svn_fs_util.h"

#include "../libsvn_fs/fs-loader.h"
#include "fs_git.h"


static svn_error_t *
fs_git_youngest_rev(svn_revnum_t *youngest_p, svn_fs_t *fs, apr_pool_t *pool)
{
  SVN_ERR(svn_fs_git__db_youngest_rev(youngest_p, fs, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_refresh_revprops(svn_fs_t *fs, apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_revision_prop(svn_string_t **value_p, svn_fs_t *fs, svn_revnum_t rev, const char *propname, svn_boolean_t refresh, apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_revision_proplist(apr_hash_t **table_p, svn_fs_t *fs, svn_revnum_t rev, svn_boolean_t refresh, apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  *table_p = apr_hash_make(result_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *fs_git_change_rev_prop(svn_fs_t *fs, svn_revnum_t rev, const char *name, const svn_string_t *const *old_value_p, const svn_string_t *value, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_set_uuid(svn_fs_t *fs, const char *uuid, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_revision_root(svn_fs_root_t **root_p, svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_begin_txn(svn_fs_txn_t **txn_p, svn_fs_t *fs, svn_revnum_t rev, apr_uint32_t flags, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_open_txn(svn_fs_txn_t **txn, svn_fs_t *fs, const char *name, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_purge_txn(svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_list_transactions(apr_array_header_t **names_p, svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_deltify(svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_lock(svn_fs_t *fs, apr_hash_t *targets, const char *comment, svn_boolean_t is_dav_comment, apr_time_t expiration_date, svn_boolean_t steal_lock, svn_fs_lock_callback_t lock_callback, void *lock_baton, apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_generate_lock_token(const char **token, svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_unlock(svn_fs_t *fs, apr_hash_t *targets, svn_boolean_t break_lock, svn_fs_lock_callback_t lock_callback, void *lock_baton, apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_get_lock(svn_lock_t **lock, svn_fs_t *fs, const char *path, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_get_locks(svn_fs_t *fs, const char *path, svn_depth_t depth, svn_fs_get_locks_callback_t get_locks_func, void *get_locks_baton, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_info_format(int *fs_format, svn_version_t **supports_version, svn_fs_t *fs, apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  *fs_format = 0;
  *supports_version = apr_palloc(result_pool, sizeof(svn_version_t));

  (*supports_version)->major = SVN_VER_MAJOR;
  (*supports_version)->minor = 10;
  (*supports_version)->patch = 0;
  (*supports_version)->tag = "";

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_info_config_files(apr_array_header_t **files, svn_fs_t *fs, apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_info_fsap(const void **fsap_info, svn_fs_t *fs, apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_verify_root(svn_fs_root_t *root, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_freeze(svn_fs_t *fs, svn_fs_freeze_func_t freeze_func, void *freeze_baton, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_bdb_set_errcall(svn_fs_t *fs, void(*handler)(const char *errpfx, char *msg))
{
  return SVN_NO_ERROR;
}

static fs_vtable_t fs_vtable =
{
  fs_git_youngest_rev,
  fs_git_refresh_revprops,
  fs_git_revision_prop,
  fs_git_revision_proplist,
  fs_git_change_rev_prop,
  fs_git_set_uuid,
  fs_git_revision_root,
  fs_git_begin_txn,
  fs_git_open_txn,
  fs_git_purge_txn,
  fs_git_list_transactions,
  fs_git_deltify,
  fs_git_lock,
  fs_git_generate_lock_token,
  fs_git_unlock,
  fs_git_get_lock,
  fs_git_get_locks,
  fs_git_info_format,
  fs_git_info_config_files,
  fs_git_info_fsap,
  fs_git_verify_root,
  fs_git_freeze,
  fs_git_bdb_set_errcall
};

static apr_status_t
fs_git_cleanup(void *baton)
{
  svn_fs_t *fs = baton;
  svn_fs_git_fs_t *fgf = fs->fsap_data;

  if (fgf->revwalk)
    {
      git_revwalk_free(fgf->revwalk);
      fgf->revwalk = NULL;
    }

  if (fgf->repos)
    {
      git_repository_free(fgf->repos);
      fgf->repos = NULL;
    }

  return APR_SUCCESS;
}

svn_error_t *
svn_fs_git__initialize_fs_struct(svn_fs_t *fs,
                                 apr_pool_t *scratch_pool)
{
  svn_fs_git_fs_t *fgf = apr_pcalloc(fs->pool, sizeof(*fgf));

  fs->vtable = &fs_vtable;
  fs->fsap_data = fgf;

  apr_pool_cleanup_register(fs->pool, fs, fs_git_cleanup,
                            apr_pool_cleanup_null);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_git__create(svn_fs_t *fs,
                   const char *path,
                   apr_pool_t *scratch_pool)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;

  fs->path = apr_pstrdup(fs->pool, path);

  GIT2_ERR(git_repository_init(&fgf->repos, path, TRUE /* is_bare */));

  SVN_ERR(svn_fs_git__db_create(fs, scratch_pool));

  return APR_SUCCESS;
}

svn_error_t *
svn_fs_git__open(svn_fs_t *fs,
                 const char *path,
                 apr_pool_t *scratch_pool)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;

  fs->path = apr_pstrdup(fs->pool, path);

  GIT2_ERR(git_repository_open(&fgf->repos, path));

  SVN_ERR(svn_fs_git__db_open(fs, scratch_pool));

  return APR_SUCCESS;
}

