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

#include <git2.h>

#include "svn_types.h"

#ifndef SVN_LIBSVN_FS__SVN_GIT_H
#define SVN_LIBSVN_FS__SVN_GIT_H

svn_error_t *
svn_git__wrap_git_error(void);

#define svn_git__wrap_git_error() \
          svn_error_trace(svn_git__wrap_git_error())

#define svn_fs_git__read_only_error()                                         \
          svn_error_create(SVN_ERR_FS_REP_NOT_MUTABLE, NULL,                  \
                           _("The Subversion git filesystem doesn't support " \
                             "write operations"))

#define GIT2_ERR(expr)                        \
  do {                                        \
    int svn_err__git_temp = (expr);           \
    if (svn_err__git_temp)                    \
      return svn_git__wrap_git_error();       \
  } while (0)

#define GIT2_ERR_NOTFOUND(x, expr)            \
  do {                                        \
    int svn_err__git_temp = (expr);           \
    if (svn_err__git_temp == GIT_ENOTFOUND)   \
      {                                       \
        giterr_clear();                       \
        *x = NULL;                            \
      }                                       \
    else if (svn_err__git_temp)               \
      return svn_git__wrap_git_error();       \
  } while (0)


/* Like git_repository_open() but lifetime limited by pool */
svn_error_t *
svn_git__repository_open(git_repository **repo_p,
                         const char *local_abspath,
                         apr_pool_t *result_pool);

/* Like git_repository_open() but lifetime limited by pool */
svn_error_t *
svn_git__repository_init(const git_repository **repo_p,
                         const char *local_abspath,
                         svn_boolean_t bare,
                         apr_pool_t *result_pool);

/* Like git_commit_lookup() but lifetime limited by pool */
svn_error_t *
svn_git__commit_lookup(const git_commit **commit_p,
                       git_repository *repo,
                       const git_oid *id,
                       apr_pool_t *result_pool);

/* Like git_tree_lookup() but lifetime limited by pool */
svn_error_t *
svn_git__tree_lookup(const git_tree **tree_p,
                     git_repository *repo,
                     const git_oid *id,
                     apr_pool_t *result_pool);


/* Like git_commit_parent() but lifetime limited by pool */
svn_error_t *
svn_git__commit_parent(const git_commit **commit_p,
                       const git_commit *commit,
                       int idx,
                       apr_pool_t *result_pool);

/* Makes (copy of) commit live as long as result_pool */
svn_error_t *
svn_git__copy_commit(const git_commit **commit_p,
                     const git_commit *commit,
                     apr_pool_t *result_pool);

/* Like git_commit_tree() but lifetime limited by pool */
svn_error_t *
svn_git__commit_tree(const git_tree **tree_p,
                     const git_commit *commit,
                     apr_pool_t *result_pool);

/* Like git_tree_entry_to_object() but lifetime limited by pool */
svn_error_t *
svn_git__tree_entry_to_object(const git_object **object_p,
                              git_repository *repository,
                              const git_tree_entry *entry,
                              apr_pool_t *result_pool);

svn_error_t *
svn_git__find_tree_entry(const git_tree_entry **entry, git_tree *tree,
                         const char *relpath,
                         apr_pool_t *result_pool, apr_pool_t *scratch_pool);

/* Combination of svn_git__commit_tree() + svn_git__find_tree_entry() */
svn_error_t *
svn_git__commit_tree_entry(const git_tree_entry **entry_p,
                           const git_commit *commit,
                           const char *relpath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);


/* Like git_treebuilder_new() but with lifetime limited by pool */
svn_error_t *
svn_git__treebuilder_new(git_treebuilder **treebuilder,
                         git_repository *repo,
                         const git_tree *source,
                         apr_pool_t *result_pool);

#endif /* SVN_LIBSVN_FS__SVN_GIT_H*/
