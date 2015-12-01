/* git-revroot.c --- a git commit mapped as a revision root
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
#include "svn_hash.h"
#include "svn_version.h"
#include "svn_pools.h"

#include "svn_private_config.h"

#include "private/svn_fs_util.h"

#include "../libsvn_fs/fs-loader.h"
#include "fs_git.h"

typedef struct svn_fs_git_root_t
{
  git_commit *commit;
  const char *rev_path;
  svn_boolean_t exact;
} svn_fs_git_root_t;

static apr_status_t
git_root_cleanup(void *baton)
{
  svn_fs_git_root_t *fgr = baton;

  if (fgr->commit)
    {
      git_commit_free(fgr->commit);
      fgr->commit = NULL;
    }

  return APR_SUCCESS;
}

static svn_fs_id_t *
make_id(svn_fs_root_t *root,
        const char *path,
        apr_pool_t *result_pool)
{
  return NULL;
}

 /* Determining what has changed in a root */
static svn_error_t *
fs_git_paths_changed(apr_hash_t **changed_paths_p,
                     svn_fs_root_t *root,
                     apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

/* Generic node operations */
static svn_error_t *
fs_git_check_path(svn_node_kind_t *kind_p, svn_fs_root_t *root,
                  const char *path, apr_pool_t *pool)
{
  if (*path == '/')
    path++;
  if (!*path)
    {
      *kind_p = svn_node_dir;
      return SVN_NO_ERROR;
    }

  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_node_history(svn_fs_history_t **history_p,
                    svn_fs_root_t *root, const char *path,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_node_id(const svn_fs_id_t **id_p, svn_fs_root_t *root,
               const char *path, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_node_relation(svn_fs_node_relation_t *relation,
                     svn_fs_root_t *root_a, const char *path_a,
                     svn_fs_root_t *root_b, const char *path_b,
                     apr_pool_t *scratch_pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_node_created_rev(svn_revnum_t *revision,
                        svn_fs_root_t *root, const char *path,
                        apr_pool_t *pool)
{
  /*svn_fs_git_root_t *fgr = root->fsap_data;*/
  if (*path == '/' && path[1] == '\0')
    {
      *revision = root->rev;
      return SVN_NO_ERROR;
    }

  *revision = root->rev; /* ### Needs path walk */

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_node_origin_rev(svn_revnum_t *revision,
                       svn_fs_root_t *root, const char *path,
                       apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_node_created_path(const char **created_path,
                         svn_fs_root_t *root, const char *path,
                         apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_delete_node(svn_fs_root_t *root, const char *path,
                   apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_copy(svn_fs_root_t *from_root, const char *from_path,
            svn_fs_root_t *to_root, const char *to_path,
            apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_revision_link(svn_fs_root_t *from_root,
                     svn_fs_root_t *to_root,
                     const char *path,
                     apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_copied_from(svn_revnum_t *rev_p, const char **path_p,
                   svn_fs_root_t *root, const char *path,
                   apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_closest_copy(svn_fs_root_t **root_p, const char **path_p,
                    svn_fs_root_t *root, const char *path,
                    apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

/* Property operations */
static svn_error_t *
fs_git_node_prop(svn_string_t **value_p, svn_fs_root_t *root,
                 const char *path, const char *propname,
                 apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}
static svn_error_t *
fs_git_node_proplist(apr_hash_t **table_p, svn_fs_root_t *root,
                     const char *path, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_node_has_props(svn_boolean_t *has_props, svn_fs_root_t *root,
                      const char *path, apr_pool_t *scratch_pool)
{
  *has_props = FALSE;
  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_change_node_prop(svn_fs_root_t *root, const char *path,
                        const char *name,
                        const svn_string_t *value,
                        apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_props_changed(int *changed_p, svn_fs_root_t *root1,
                     const char *path1, svn_fs_root_t *root2,
                     const char *path2, svn_boolean_t strict,
                     apr_pool_t *scratch_pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

/* Directories */
static svn_error_t *
fs_git_dir_entries(apr_hash_t **entries_p, svn_fs_root_t *root,
                   const char *path, apr_pool_t *pool)
{
  svn_fs_dirent_t *de;

  *entries_p = apr_hash_make(pool);
  if (!root->rev)
    return SVN_NO_ERROR;
  else if (*path == '/' && path[1] == '\0')
    {
      de = apr_pcalloc(pool, sizeof(*de));
      de->kind = svn_node_dir;
      de->id = make_id(root, path, pool);
      de->name = "trunk";
      svn_hash_sets(*entries_p, "trunk", de);

      de = apr_pcalloc(pool, sizeof(*de));
      de->kind = svn_node_dir;
      de->id = make_id(root, path, pool);
      de->name = "branches";
      svn_hash_sets(*entries_p, "branches", de);

      de = apr_pcalloc(pool, sizeof(*de));
      de->kind = svn_node_dir;
      de->id = make_id(root, path, pool);
      de->name = "tags";
      svn_hash_sets(*entries_p, "tags", de);

      return SVN_NO_ERROR;
    }
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_dir_optimal_order(apr_array_header_t **ordered_p,
                         svn_fs_root_t *root,
                         apr_hash_t *entries,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_make_dir(svn_fs_root_t *root, const char *path,
                apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

/* Files */
static svn_error_t *
fs_git_file_length(svn_filesize_t *length_p, svn_fs_root_t *root,
                   const char *path, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_file_checksum(svn_checksum_t **checksum,
                     svn_checksum_kind_t kind, svn_fs_root_t *root,
                     const char *path, apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_file_contents(svn_stream_t **contents,
                     svn_fs_root_t *root, const char *path,
                     apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_try_process_file_contents(svn_boolean_t *success,
                                 svn_fs_root_t *target_root,
                                 const char *target_path,
                                 svn_fs_process_contents_func_t processor,
                                 void* baton,
                                 apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_make_file(svn_fs_root_t *root, const char *path,
                 apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_apply_textdelta(svn_txdelta_window_handler_t *contents_p,
                       void **contents_baton_p,
                       svn_fs_root_t *root, const char *path,
                       svn_checksum_t *base_checksum,
                       svn_checksum_t *result_checksum,
                       apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_apply_text(svn_stream_t **contents_p, svn_fs_root_t *root,
                  const char *path, svn_checksum_t *result_checksum,
                  apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_contents_changed(int *changed_p, svn_fs_root_t *root1,
                        const char *path1, svn_fs_root_t *root2,
                        const char *path2, svn_boolean_t strict,
                        apr_pool_t *scratch_pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

static svn_error_t *
fs_git_get_file_delta_stream(svn_txdelta_stream_t **stream_p,
                             svn_fs_root_t *source_root,
                             const char *source_path,
                             svn_fs_root_t *target_root,
                             const char *target_path,
                             apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}

/* Merging. */
static svn_error_t *
fs_git_merge(const char **conflict_p,
             svn_fs_root_t *source_root,
             const char *source_path,
             svn_fs_root_t *target_root,
             const char *target_path,
             svn_fs_root_t *ancestor_root,
             const char *ancestor_path,
             apr_pool_t *pool)
{
  return svn_error_create(APR_ENOTIMPL, NULL, NULL);
}
/* Mergeinfo. */
static svn_error_t *
fs_git_get_mergeinfo(svn_mergeinfo_catalog_t *catalog,
                     svn_fs_root_t *root,
                     const apr_array_header_t *paths,
                     svn_mergeinfo_inheritance_t inherit,
                     svn_boolean_t include_descendants,
                     svn_boolean_t adjust_inherited_mergeinfo,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL, NULL);
}

static root_vtable_t root_vtable =
{
  fs_git_paths_changed,
  fs_git_check_path,
  fs_git_node_history,
  fs_git_node_id,
  fs_git_node_relation,
  fs_git_node_created_rev,
  fs_git_node_origin_rev,
  fs_git_node_created_path,
  fs_git_delete_node,
  fs_git_copy,
  fs_git_revision_link,
  fs_git_copied_from,
  fs_git_closest_copy,
  fs_git_node_prop,
  fs_git_node_proplist,
  fs_git_node_has_props,
  fs_git_change_node_prop,
  fs_git_props_changed,
  fs_git_dir_entries,
  fs_git_dir_optimal_order,
  fs_git_make_dir,
  fs_git_file_length,
  fs_git_file_checksum,
  fs_git_file_contents,
  fs_git_try_process_file_contents,
  fs_git_make_file,
  fs_git_apply_textdelta,
  fs_git_apply_text,
  fs_git_contents_changed,
  fs_git_get_file_delta_stream,
  fs_git_merge,
  fs_git_get_mergeinfo
};

svn_error_t *
svn_fs_git__revision_root(svn_fs_root_t **root_p, svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool)
{
  svn_fs_git_fs_t *fgf = fs->fsap_data;
  svn_fs_root_t *root;
  svn_fs_git_root_t *fgr;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));


  fgr = apr_pcalloc(pool, sizeof(*fgr));
  root = apr_pcalloc(pool, sizeof(*root));

  root->pool = pool;
  root->fs = fs;
  root->is_txn_root = FALSE;
  root->txn = NULL;
  root->txn_flags = 0;
  root->rev = rev;

  root->vtable = &root_vtable;
  root->fsap_data = fgr;

  if (rev > 0)
    {
      git_oid *oid;
      SVN_ERR(svn_fs_git__db_fetch_oid(&fgr->exact, &oid, &fgr->rev_path,
                                       fs, rev, pool, pool));

      if (oid)
        GIT2_ERR(git_commit_lookup(&fgr->commit, fgf->repos, oid));

      apr_pool_cleanup_register(pool, fgr, git_root_cleanup,
                                apr_pool_cleanup_null);
    }

  *root_p = root;

  return SVN_NO_ERROR;
}

