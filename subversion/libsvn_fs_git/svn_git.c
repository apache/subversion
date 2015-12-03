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

#include "fs_git.h"
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
