/* git-history.c --- svn history, delivered from git
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

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_fs.h"

#include "fs_git.h"
#include "../libsvn_fs/fs-loader.h"

static svn_fs_history_t *
history_make(const history_vtable_t *vtable,
             void *fsap_data,
             apr_pool_t *result_pool)
{
  svn_fs_history_t *h = apr_palloc(result_pool, sizeof(*h));
  h->vtable = vtable;
  h->fsap_data = fsap_data;

  return h;
}

/* ------------------------------------------------------- */
typedef struct fs_git_simple_history_t
{
  const char *next_path;
  svn_revnum_t rev;
  svn_revnum_t last_rev;
  svn_boolean_t initial_item;
} fs_git_simple_history_t;

static svn_error_t *
fs_git_simple_history_prev(svn_fs_history_t **prev_history_p,
                           svn_fs_history_t *history,
                           svn_boolean_t cross_copies,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  fs_git_simple_history_t *sht = history->fsap_data;

  if (!sht->initial_item
      && sht->rev == sht->last_rev)
    {
      *prev_history_p = NULL;
      return SVN_NO_ERROR;
    }

  sht = apr_pmemdup(result_pool, sht, sizeof(*sht));
  sht->next_path = apr_pstrdup(result_pool, sht->next_path);

  if (sht->initial_item)
    sht->initial_item = FALSE;
  else
    {
      sht->rev--;
    }

  *prev_history_p = history_make(history->vtable,
                                 sht, result_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_simple_history_location(const char **path,
                               svn_revnum_t *revision,
                               svn_fs_history_t *history,
                               apr_pool_t *pool)
{
  fs_git_simple_history_t *sht = history->fsap_data;

  *path = apr_pstrdup(pool, sht->next_path);
  *revision = sht->rev;
  return SVN_NO_ERROR;
}

static const history_vtable_t fs_git_simple_history_vtable =
{
  fs_git_simple_history_prev,
  fs_git_simple_history_location
};

svn_error_t *
svn_fs_git__make_history_simple(svn_fs_history_t **history_p,
                                svn_fs_root_t *root,
                                svn_revnum_t rev_start,
                                svn_revnum_t rev_end,
                                const char *path,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  fs_git_simple_history_t *sht = apr_palloc(result_pool, sizeof(*sht));
  sht->next_path = apr_pstrdup(result_pool, path ? path : "/"); 
  sht->rev = rev_start;
  sht->last_rev = rev_end;
  sht->initial_item = TRUE;

  *history_p = history_make(&fs_git_simple_history_vtable,
                              sht, result_pool);
  return SVN_NO_ERROR;
}

/* ------------------------------------------------------- */
typedef struct fs_git_commit_history_t
{
  apr_pool_t *pool;
  svn_fs_t *fs;
  const git_commit *commit;
  svn_revnum_t rev;
  const char *path;
  svn_boolean_t initial_item;
} fs_git_commit_history_t;

static svn_error_t *
fs_git_commit_history_prev(svn_fs_history_t **prev_history_p,
                           svn_fs_history_t *history,
                           svn_boolean_t cross_copies,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  fs_git_commit_history_t *p_cht = history->fsap_data;
  fs_git_commit_history_t *cht;
  const git_commit *prev_commit;

  prev_commit = p_cht->commit;
  if (p_cht->initial_item)
    {
      cht = apr_pcalloc(result_pool, sizeof(*cht));

      /* Copy same commit to new result pool */
      SVN_ERR(svn_git__copy_commit(&cht->commit, prev_commit,
                                   result_pool));
    }
  else if (git_commit_parentcount(prev_commit) > 0)
    {
      cht = apr_pcalloc(result_pool, sizeof(*cht));

      SVN_ERR(svn_git__commit_parent(&cht->commit, prev_commit, 0,
                                     result_pool));
    }
  else
    {
      *prev_history_p = NULL;
      return SVN_NO_ERROR; /* End of history */
    }

  cht->rev = SVN_INVALID_REVNUM;
  cht->pool = result_pool;
  cht->fs = p_cht->fs;

  *prev_history_p = history_make(history->vtable, cht, result_pool);

  if (!cross_copies)
    {
      const char *path1, *path2;
      svn_revnum_t rev_dummy;

      SVN_ERR(svn_fs_history_location(&path1, &rev_dummy, history,
                                      scratch_pool));
      SVN_ERR(svn_fs_history_location(&path2, &rev_dummy, *prev_history_p,
                                      scratch_pool));

      if (path1 && path2 && strcmp(path1, path2) != 0)
        {
          *prev_history_p = NULL;
          return SVN_NO_ERROR;
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
fs_git_commit_history_location(const char **path,
                               svn_revnum_t *revision,
                               svn_fs_history_t *history,
                               apr_pool_t *pool)
{
  fs_git_commit_history_t *cht = history->fsap_data;

  if (!SVN_IS_VALID_REVNUM(cht->rev))
    {
      SVN_ERR(svn_fs_git__db_fetch_rev(&cht->rev,
                                       &cht->path,
                                       cht->fs,
                                       git_commit_id(cht->commit),
                                       cht->pool, pool));
      if (cht->path && cht->path[0] != '/')
        {
          cht->path = apr_pstrcat(cht->pool, "/", cht->path, SVN_VA_NULL);
        }
    }

  *path = apr_pstrdup(pool, cht->path);
  *revision = cht->rev;
  return SVN_NO_ERROR;
}

static const history_vtable_t fs_git_commit_history_vtable =
{
  fs_git_commit_history_prev,
  fs_git_commit_history_location
};

svn_error_t *
svn_fs_git__make_history_commit(svn_fs_history_t **history_p,
                                svn_fs_root_t *root,
                                const git_commit *commit,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  fs_git_commit_history_t *cht = apr_palloc(result_pool, sizeof(*cht));

  cht->pool = result_pool;
  cht->fs = root->fs;
  cht->commit = commit;
  cht->rev = SVN_INVALID_REVNUM;
  cht->path = NULL;
  cht->initial_item = TRUE;

  *history_p = history_make(&fs_git_commit_history_vtable, cht, result_pool);
  return SVN_NO_ERROR;
}

/* ------------------------------------------------------- */
typedef struct fs_git_node_history_t
{
  apr_pool_t *pool;
  svn_fs_t *fs;
  const git_commit *commit;
  const char *relpath;

  svn_revnum_t rev;
  const char *path;
  svn_boolean_t last, first;
} fs_git_node_history_t;

static svn_error_t *
fs_git_node_history_prev(svn_fs_history_t **prev_history_p,
                         svn_fs_history_t *history,
                         svn_boolean_t cross_copies,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  fs_git_node_history_t *p_gnh = history->fsap_data;
  fs_git_node_history_t *gnh;
  apr_pool_t *cur_pool = svn_pool_create(result_pool);
  apr_pool_t *last_pool = svn_pool_create(result_pool);
  const git_commit *commit;
  const git_tree_entry *entry;
  const char *relpath;

  /* ### cross_copies is still unused! */

  if (p_gnh->last)
    {
      *prev_history_p = NULL;
      return SVN_NO_ERROR;
    }

  commit = p_gnh->commit;
  relpath = p_gnh->relpath;

  if (!p_gnh->first)
    SVN_ERR(svn_git__commit_parent(&commit, commit, 0, last_pool));

  SVN_ERR(svn_git__commit_tree_entry(&entry, commit, relpath,
                                     last_pool, cur_pool));

  while (TRUE)
    {
      const git_commit *pcommit;
      const git_tree_entry *pentry;
      svn_boolean_t modified = FALSE;

      svn_pool_clear(cur_pool);

      if (git_commit_parentcount(commit) == 0)
        {
          /* And finally report the revision in which the branch
             and this path were created */
          gnh = apr_pcalloc(result_pool, sizeof(*gnh));
          gnh->pool = result_pool;
          gnh->fs = p_gnh->fs;
          gnh->last = TRUE;
          gnh->rev = SVN_INVALID_REVNUM;

          SVN_ERR(svn_git__copy_commit(&gnh->commit, commit,
                                       result_pool));

          svn_pool_destroy(cur_pool);
          svn_pool_destroy(last_pool);

          *prev_history_p = history_make(history->vtable, gnh,
                                         result_pool);
          return SVN_NO_ERROR;
        }

      SVN_ERR(svn_git__commit_parent(&pcommit, commit, 0, cur_pool));
      SVN_ERR(svn_git__commit_tree_entry(&pentry, pcommit, relpath,
                                         cur_pool, last_pool));

      if (!pentry)
        {
          /* TODO: Follow renames */
        }

      if (pentry
          && (git_tree_entry_filemode(pentry) != git_tree_entry_filemode(entry)
              || git_oid_cmp(git_tree_entry_id(pentry),
                             git_tree_entry_id(entry))))
        {
          modified = TRUE; /* Report as changed */
        }

      if (modified || !pentry)
        {
          /* TODO: Follow in-branch renames */

          /* Report the revision in which this path was created */
          gnh = apr_pcalloc(result_pool, sizeof(*gnh));
          gnh->pool = result_pool;
          gnh->fs = p_gnh->fs;
          gnh->last = !pentry;
          gnh->relpath = apr_pstrdup(result_pool, relpath);
          gnh->rev = SVN_INVALID_REVNUM;

          SVN_ERR(svn_git__copy_commit(&gnh->commit, commit,
                                       result_pool));

          svn_pool_destroy(cur_pool);
          svn_pool_destroy(last_pool);

          *prev_history_p = history_make(history->vtable, gnh,
                                         result_pool);
          return SVN_NO_ERROR;
        }

      {
        apr_pool_t *tmp_pool;
        commit = pcommit;
        entry = pentry;

        tmp_pool = last_pool;
        last_pool = cur_pool;
        cur_pool = tmp_pool;
      }
    }
}

static svn_error_t *
fs_git_node_history_location(const char **path,
                             svn_revnum_t *revision,
                             svn_fs_history_t *history,
                             apr_pool_t *pool)
{
  fs_git_node_history_t *gnh = history->fsap_data;

  if (!SVN_IS_VALID_REVNUM(gnh->rev))
    {
      SVN_ERR(svn_fs_git__db_fetch_rev(&gnh->rev,
                                       &gnh->path,
                                       gnh->fs,
                                       git_commit_id(gnh->commit),
                                       gnh->pool, pool));

      gnh->path = apr_pstrcat(gnh->pool, "/",
                              svn_relpath_join(gnh->path, gnh->relpath,
                                               pool),
                              SVN_VA_NULL);
    }

  *path = apr_pstrdup(pool, gnh->path);
  *revision = gnh->rev;
  return SVN_NO_ERROR;
}

static const history_vtable_t fs_git_node_history_vtable =
{
  fs_git_node_history_prev,
  fs_git_node_history_location
};

svn_error_t *
svn_fs_git__make_history_node(svn_fs_history_t **history_p,
                              svn_fs_root_t *root,
                              const git_commit *commit,
                              const char *relpath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  fs_git_node_history_t *gnh = apr_pcalloc(result_pool, sizeof(*gnh));

  gnh->pool = result_pool;
  gnh->fs = root->fs;
  gnh->commit = commit;
  gnh->relpath = apr_pstrdup(result_pool, relpath);
  gnh->rev = SVN_INVALID_REVNUM;
  /*gnh->path = NULL;
  gnh->last = FALSE;*/
  gnh->first = TRUE;

  *history_p = history_make(&fs_git_node_history_vtable, gnh, result_pool);
  return SVN_NO_ERROR;
}
