/* list.c : listing repository contents
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

#include <apr_pools.h>
#include <apr_fnmatch.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_time.h"

#include "private/svn_repos_private.h"
#include "private/svn_sorts_private.h"
#include "svn_private_config.h" /* for SVN_TEMPLATE_ROOT_DIR */

#include "repos.h"



/* Utility function.  Given DIRENT->KIND, set all other elements of *DIRENT
 * with the values retrieved for PATH under ROOT.  Allocate them in POOL.
 */
static svn_error_t *
fill_dirent(svn_dirent_t *dirent,
            svn_fs_root_t *root,
            const char *path,
            apr_pool_t *pool)
{
  const char *datestring;

  if (dirent->kind == svn_node_file)
    SVN_ERR(svn_fs_file_length(&(dirent->size), root, path, pool));

  SVN_ERR(svn_fs_node_has_props(&dirent->has_props, root, path, pool));

  SVN_ERR(svn_repos_get_committed_info(&(dirent->created_rev),
                                       &datestring,
                                       &(dirent->last_author),
                                       root, path, pool));
  if (datestring)
    SVN_ERR(svn_time_from_cstring(&(dirent->time), datestring, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_stat(svn_dirent_t **dirent,
               svn_fs_root_t *root,
               const char *path,
               apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_dirent_t *ent;

  SVN_ERR(svn_fs_check_path(&kind, root, path, pool));

  if (kind == svn_node_none)
    {
      *dirent = NULL;
      return SVN_NO_ERROR;
    }

  ent = svn_dirent_create(pool);
  ent->kind = kind;

  SVN_ERR(fill_dirent(ent, root, path, pool));

  *dirent = ent;
  return SVN_NO_ERROR;
}

/* Return TRUE of DIRNAME matches any of the const char * in PATTERNS.
 * Note that any DIRNAME will match if PATTERNS is empty. */
static svn_boolean_t
matches_any(const char *dirname,
            apr_array_header_t *patterns)
{
  int i;
  if (!patterns->nelts)
    return TRUE;

  for (i = 0; i < patterns->nelts; ++i)
    {
      const char *pattern = APR_ARRAY_IDX(patterns, i, const char *);
      if (apr_fnmatch(pattern, dirname, APR_FNM_PERIOD) == APR_SUCCESS)
        return TRUE;
    }

  return FALSE;
}

/* Utility to prevent code duplication.
 *
 * Construct a svn_dirent_t for PATH of type KIND under ROOT and, if
 * PATH_INFO_ONLY is not set, fill it.  Call RECEIVER with the result
 * and RECEIVER_BATON.
 *
 * Use POOL for temporary allocations.
 */
static svn_error_t *
report_dirent(svn_fs_root_t *root,
              const char *path,
              svn_node_kind_t kind,
              svn_boolean_t path_info_only,
              svn_repos_dirent_receiver_t receiver,
              void *receiver_baton,
              apr_pool_t *pool)
{
  svn_dirent_t dirent = { 0 };

  /* Fetch the details to report - if required. */
  dirent.kind = kind;
  if (!path_info_only)
    SVN_ERR(fill_dirent(&dirent, root, path, pool));

  /* Report the entry. */
  SVN_ERR(receiver(path, &dirent, receiver_baton, pool));

  return SVN_NO_ERROR;
}

/* Utility data struct, used to attach a filter result flag to a dirent. */
typedef struct filtered_dirent_t
{
  /* Actual dirent, never NULL. */
  svn_fs_dirent_t *dirent;

  /* DIRENT passed the filter. */
  svn_boolean_t is_match;
} filtered_dirent_t;

/* Implement a standard sort function for filtered_dirent_t *, sorting them
 * by entry name. */
static int
compare_filtered_dirent(const void *lhs,
                        const void *rhs)
{
  const filtered_dirent_t *lhs_dirent = (const filtered_dirent_t *)lhs;
  const filtered_dirent_t *rhs_dirent = (const filtered_dirent_t *)rhs;

  return strcmp(lhs_dirent->dirent->name, rhs_dirent->dirent->name);
}

/* Core of svn_repos_list with the same parameter list.
 *
 * However, DEPTH is not svn_depth_empty and PATH has already been reported.
 * Therefore, we can call this recursively.
 */
static svn_error_t *
do_list(svn_fs_root_t *root,
        const char *path,
        apr_array_header_t *patterns,
        svn_depth_t depth,
        svn_boolean_t path_info_only,
        svn_repos_authz_func_t authz_read_func,
        void *authz_read_baton,
        svn_repos_dirent_receiver_t receiver,
        void *receiver_baton,
        svn_cancel_func_t cancel_func,
        void *cancel_baton,
        apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_hash_index_t *hi;
  apr_array_header_t *sorted;
  int i;

  /* Fetch all directory entries, filter and sort them.
   *
   * Performance trade-off:
   * Constructing a full path vs. faster sort due to authz filtering.
   * We filter according to DEPTH and PATTERNS only because constructing
   * the full path required for authz is somewhat expensive and we don't
   * want to do this twice while authz will rarely filter paths out.
   */
  SVN_ERR(svn_fs_dir_entries(&entries, root, path, pool));
  sorted = apr_array_make(pool, apr_hash_count(entries),
                          sizeof(filtered_dirent_t));
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      filtered_dirent_t filtered;
      svn_pool_clear(iterpool);

      filtered.dirent = apr_hash_this_val(hi);

      /* Skip directories if we want to report files only. */
      if (filtered.dirent->kind == svn_node_dir && depth == svn_depth_files)
        continue;

      /* We can skip files that don't match any of the search patterns. */
      filtered.is_match = matches_any(filtered.dirent->name, patterns);
      if (!filtered.is_match && filtered.dirent->kind == svn_node_file)
        continue;

      APR_ARRAY_PUSH(sorted, filtered_dirent_t) = filtered;
    }

  svn_sort__array(sorted, compare_filtered_dirent);

  /* Iterate over all remaining directory entries and report them.
   * Recurse into sub-directories if requested. */
  for (i = 0; i < sorted->nelts; ++i)
    {
      const char *sub_path;
      filtered_dirent_t *filtered;
      svn_fs_dirent_t *dirent;

      svn_pool_clear(iterpool);

      filtered = &APR_ARRAY_IDX(sorted, i, filtered_dirent_t);
      dirent = filtered->dirent;

      /* Skip paths that we don't have access to? */
      sub_path = svn_dirent_join(path, dirent->name, iterpool);
      if (authz_read_func)
        {
          svn_boolean_t has_access;
          SVN_ERR(authz_read_func(&has_access, root, path, authz_read_baton,
                                  iterpool));
          if (!has_access)
            continue;
        }

      /* Report entry, if it passed the filter. */
      if (filtered->is_match)
        SVN_ERR(report_dirent(root, sub_path, dirent->kind, path_info_only,
                              receiver, receiver_baton, iterpool));

      /* Check for cancellation before recursing down.  This should be
       * slightly more responsive for deep trees. */
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      /* Recurse on directories. */
      if (depth == svn_depth_infinity && dirent->kind == svn_node_dir)
        SVN_ERR(do_list(root, sub_path, patterns, svn_depth_infinity,
                        path_info_only, authz_read_func, authz_read_baton,
                        receiver, receiver_baton, cancel_func,
                        cancel_baton, iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_list(svn_fs_root_t *root,
               const char *path,
               apr_array_header_t *patterns,
               svn_depth_t depth,
               svn_boolean_t path_info_only,
               svn_repos_authz_func_t authz_read_func,
               void *authz_read_baton,
               svn_repos_dirent_receiver_t receiver,
               void *receiver_baton,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *pool)
{
  /* Parameter check. */
  svn_node_kind_t kind;
  if (depth < svn_depth_empty)
    return svn_error_createf(SVN_ERR_REPOS_BAD_ARGS, NULL,
                             "Invalid depth '%d' in svn_repos_list", depth);

  /* Do we have access this sub-tree? */
  if (authz_read_func)
    {
      svn_boolean_t has_access;
      SVN_ERR(authz_read_func(&has_access, root, path, authz_read_baton,
                              pool));
      if (!has_access)
        return SVN_NO_ERROR;
    }

  /* Does the sub-tree even exist?
   *
   * Note that we must do this after the authz check to not indirectly
   * confirm the existence of PATH. */
  SVN_ERR(svn_fs_check_path(&kind, root, path, pool));
  if (kind != svn_node_dir)
    return svn_error_createf(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                             "There is no directory '%s'", path);

  /* Actually report PATH, if it passes the filters. */
  if (matches_any(svn_dirent_dirname(path, pool), patterns))
    SVN_ERR(report_dirent(root, path, kind, path_info_only,
                          receiver, receiver_baton, pool));

  /* Report directory contents if requested. */
  if (depth > svn_depth_empty)
    SVN_ERR(do_list(root, path, patterns, depth,
                    path_info_only, authz_read_func, authz_read_baton,
                    receiver, receiver_baton, cancel_func, cancel_baton,
                    pool));

  return SVN_NO_ERROR;
}
