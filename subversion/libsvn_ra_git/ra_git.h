/*
 * ra_git.h : shared internal declarations for ra_git module
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

#ifndef SVN_LIBSVN_RA_GIT_H
#define SVN_LIBSVN_RA_GIT_H

/* We compile in C89 mode, so the 'inline' keyword used by libgit2 isn't supported. */
#define inline APR_INLINE

#include <git2.h>

svn_error_t *
svn_ra_git__wrap_git_error(void);

svn_error_t *
svn_ra_git__check_path(svn_node_kind_t *kind, git_tree *tree, const char *path);

apr_hash_t *
svn_ra_git__make_revprops_hash(git_commit *commit, apr_pool_t *pool);

svn_error_t *
svn_ra_git__find_last_changed(svn_revnum_t *last_changed,
                              apr_hash_t *revmap,
                              const char *path,
                              svn_revnum_t pegrev,
                              git_repository *repos,
                              apr_pool_t *pool);

/* Git repositories don't have a UUID so a static UUID is as good as any. */
#define RA_GIT_UUID "a62d4ba0-b83e-11e3-8621-8f162a3365eb"

/* ---------------------------------------------------------------*/

/* Reporting the state of a working copy, for updates. */

/* Like svn_repos_begin_report3() but for git repositories. */
svn_error_t *
svn_ra_git__reporter_begin_report(void **report_baton,
                                  svn_revnum_t revnum,
                                  git_repository *repos,
                                  apr_hash_t *revmap,
                                  const char *fs_base,
                                  const char *target,
                                  const char *tgt_path,
                                  svn_boolean_t text_deltas,
                                  svn_depth_t depth,
                                  svn_boolean_t ignore_ancestry,
                                  svn_boolean_t send_copyfrom_args,
                                  const svn_delta_editor_t *editor,
                                  void *edit_baton,
                                  apr_size_t zero_copy_limit,
                                  apr_pool_t *pool);

/* Like svn_repos_set_path3() but for git repositories. */
svn_error_t *
svn_ra_git__reporter_set_path(void *report_baton,
                              const char *path,
                              svn_revnum_t revision,
                              svn_depth_t depth,
                              svn_boolean_t start_empty,
                              const char *lock_token,
                              apr_pool_t *pool);

/* Like svn_repos_link_path3() but for git repositories. */
svn_error_t *
svn_ra_git__reporter_link_path(void *report_baton,
                               const char *path,
                               const char *link_path,
                               svn_revnum_t revision,
                               svn_depth_t depth,
                               svn_boolean_t start_empty,
                               const char *lock_token,
                               apr_pool_t *pool);

/* Like svn_repos_delete_path() but for git repositories. */
svn_error_t *
svn_ra_git__reporter_delete_path(void *report_baton,
                                 const char *path,
                                 apr_pool_t *pool);

/* Like svn_repos_finish_report() but for git repositories. */
svn_error_t *
svn_ra_git__reporter_finish_report(void *report_baton,
                                   apr_pool_t *pool);

/* Like svn_repos_finish_report() but for git repositories. */
svn_error_t *
svn_ra_git__reporter_abort_report(void *report_baton,
                                  apr_pool_t *pool);

#endif /* SVN_LIBSVN_RA_GIT_H */
