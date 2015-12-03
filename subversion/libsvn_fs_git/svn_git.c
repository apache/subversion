/* svn-git.c --- Some helper functions to ease working with libgit2
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

#include "svn_dirent_uri.h"

#include "svn_git.h"

#define DECLARE_GIT_CLEANUP(type,func)              \
  static apr_status_t cleanup_##type(void *baton)   \
  {                                                 \
      type *item = baton;                           \
      func(item);                                   \
      return APR_SUCCESS;                           \
  }

#define GIT_RELEASE_AT_CLEANUP(type, item, result_pool)       \
  do                                                          \
  {                                                           \
      type *val_for_cleanup = item;                           \
      apr_pool_cleanup_register(result_pool, val_for_cleanup, \
                                cleanup_##type,               \
                                apr_pool_cleanup_null);       \
  } while (0)

DECLARE_GIT_CLEANUP(git_repository, git_repository_free)
DECLARE_GIT_CLEANUP(git_commit, git_commit_free)
DECLARE_GIT_CLEANUP(git_object, git_object_free)
DECLARE_GIT_CLEANUP(git_tree, git_tree_free)

svn_error_t *
svn_git__repository_open(const git_repository **repo_p,
                         const char *local_abspath,
                         apr_pool_t *result_pool)
{
  git_repository *repo;

  GIT2_ERR(git_repository_open(&repo, local_abspath));

  GIT_RELEASE_AT_CLEANUP(git_repository, repo, result_pool);

  *repo_p = repo;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_git__repository_init(const git_repository **repo_p,
                         const char *local_abspath,
                         svn_boolean_t is_bare,
                         apr_pool_t *result_pool)
{
  git_repository *repo;

  GIT2_ERR(git_repository_init(&repo, local_abspath, is_bare));

  GIT_RELEASE_AT_CLEANUP(git_repository, repo, result_pool);

  *repo_p = repo;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_git__commit_lookup(const git_commit **commit_p,
                       git_repository *repo,
                       const git_oid *id,
                       apr_pool_t *result_pool)
{
  git_commit *commit;

  GIT2_ERR(git_commit_lookup(&commit, repo, id));

  if (commit)
    GIT_RELEASE_AT_CLEANUP(git_commit, commit, result_pool);

  *commit_p = commit;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_git__copy_commit(const git_commit **commit_p,
                     const git_commit *commit,
                     apr_pool_t *result_pool)
{
  git_object *object;
  git_commit *cmt;

  /* libgit2 objects are reference counted... so this
     is just syntactic sugar over an increment value */

  GIT2_ERR(git_object_dup(&object, (git_object*)commit));

  cmt = (git_commit*)object;

  if (object)
    GIT_RELEASE_AT_CLEANUP(git_commit, cmt, result_pool);

  *commit_p = cmt;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_git__commit_parent(const git_commit **commit_p,
                       const git_commit *commit,
                       int idx,
                       apr_pool_t *result_pool)
{
  git_commit *parent_cmt;

  GIT2_ERR(git_commit_parent(&parent_cmt, commit, idx));

  if (parent_cmt)
    GIT_RELEASE_AT_CLEANUP(git_commit, parent_cmt, result_pool);

  *commit_p = parent_cmt;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_git__commit_tree(const git_tree **tree_p,
                     const git_commit *commit,
                     apr_pool_t *result_pool)
{
  git_tree *tree;

  GIT2_ERR(git_commit_tree(&tree, commit));

  if (tree)
    GIT_RELEASE_AT_CLEANUP(git_tree, tree, result_pool);

  *tree_p = tree;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_git__tree_entry_to_object(const git_object **object_p,
                              const git_tree *tree,
                              const git_tree_entry *entry,
                              apr_pool_t *result_pool)
{
  git_object *object;

  GIT2_ERR(git_tree_entry_to_object(&object,
                                    git_tree_owner(tree),
                                    entry));

  if (object)
    GIT_RELEASE_AT_CLEANUP(git_object, object, result_pool);

  *object_p = object;

  return SVN_NO_ERROR;
}

/* svn_relpath_split, but then for the first component instead of the last */
static svn_error_t *
relpath_reverse_split(const char **root, const char **remaining,
                      const char *relpath,
                      apr_pool_t *result_pool)
{
  const char *ch = strchr(relpath, '/');
  SVN_ERR_ASSERT(svn_relpath_is_canonical(relpath));

  if (!ch)
    {
      if (root)
        *root = apr_pstrdup(result_pool, relpath);
      if (remaining)
        *remaining = "";
    }
  else
    {
      if (root)
        *root = apr_pstrmemdup(result_pool, relpath, ch - relpath);
      if (remaining)
        *remaining = apr_pstrdup(result_pool, ch + 1);
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_git__find_tree_entry(const git_tree_entry **entry, git_tree *tree,
                         const char *relpath,
                         apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  const char *basename, *tail;
  const git_tree_entry *e;

  SVN_ERR(relpath_reverse_split(&basename, &tail, relpath, scratch_pool));

  e = git_tree_entry_byname(tree, basename);
  if (e && !*tail)
    {
      *entry = e;
      return SVN_NO_ERROR;
    }
  else if (!e)
    {
      *entry = NULL;
      return SVN_NO_ERROR;
    }

  switch (git_tree_entry_type(e))
    {
      case GIT_OBJ_TREE:
        {
          git_object *obj;
          git_tree *sub_tree;

          SVN_ERR(svn_git__tree_entry_to_object(&obj, tree, e, result_pool));

          sub_tree = (git_tree*)obj;

          SVN_ERR(svn_git__find_tree_entry(entry, sub_tree, tail,
                                           result_pool, scratch_pool));
          break;
        }
      case GIT_OBJ_BLOB:
        *entry = NULL;
        break;
      default:
        SVN_ERR_MALFUNCTION();
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_git__commit_tree_entry(const git_tree_entry **entry_p,
                           const git_commit *commit,
                           const char *relpath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  git_tree *tree;

  SVN_ERR(svn_git__commit_tree(&tree, commit, result_pool));
  if (!tree)
    {
      /* Corrupt commit */
      *entry_p = NULL;
      return SVN_NO_ERROR;
    }
  SVN_ERR(svn_git__find_tree_entry(entry_p, tree, relpath,
                                   result_pool, scratch_pool));

  return SVN_NO_ERROR;
}
