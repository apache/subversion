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

/* We compile in C89 mode, so the 'inline' keyword used by libgit2 isn't supported. */
#define inline APR_INLINE
#include <git2.h>
#undef inline

#include "svn_types.h"

#ifndef SVN_LIBSVN_FS__SVN_GIT_H
#define SVN_LIBSVN_FS__SVN_GIT_H


/* Like git_repository_open() but lifetime limited by pool */
svn_error_t *
svn_git__repository_open(const git_repository **repo_p,
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
svn_git__commit_lookup(const git_commit **commit,
                       git_repository *repo,
                       const git_oid *id,
                       apr_pool_t *result_pool);

/* Like git_commit_tree() but lifetime limited by pool */
svn_error_t *
svn_git__commit_tree(const git_tree **tree_p,
                     const git_commit *commit,
                     apr_pool_t *result_pool);

/* Like git_tree_entry_to_object() but lifetime limited by pool */
svn_error_t *
svn_git__tree_entry_to_object(const git_object **object_p,
                              const git_tree *tree,
                              const git_tree_entry *entry,
                              apr_pool_t *result_pool);


#endif /* SVN_LIBSVN_FS__SVN_GIT_H*/
