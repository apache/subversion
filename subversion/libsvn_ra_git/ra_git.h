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

typedef struct svn_ra_git__session_t
{
  /* Callbacks/baton passed to svn_ra_open. */
  const svn_ra_callbacks2_t *callbacks;
  void *callback_baton;

  /* Stashed config */
  apr_hash_t *config;

  /* The URL of the session. */
  const char *repos_root_url;

  /* The file:/// session backing the git session*/
  svn_ra_session_t *local_session;
  const char *local_repos_abspath;
  const char *local_repos_root_url;

  /* The UUID associated with REPOS (faked) */
  const char *uuid;

  /* The URL of the remote in git format. */
  const char *git_remote_url;
  svn_boolean_t fetch_done;

  /* The relative path in the tree the session is rooted at. */
  svn_stringbuf_t *repos_relpath_buf;

  /* The relative path in the tree the session is rooted at. */
  svn_stringbuf_t *session_url_buf;

  apr_pool_t *scratch_pool;

  /* Cached reference to svn_ra_open() to allow opening our
     ra_local session */
  svn_ra__open_func_t svn_ra_open;

  apr_uint64_t progress_bytes;
} svn_ra_git__session_t;

svn_error_t *
svn_ra_git__wrap_git_error(void);

#define GIT2_ERR(expr)                        \
  do {                                        \
    int svn_err__git_temp = (expr);           \
    if (svn_err__git_temp)                    \
      return svn_ra_git__wrap_git_error();    \
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
      return svn_ra_git__wrap_git_error();    \
  } while (0)

/* Git repositories don't have a UUID so a static UUID is as good as any. */
#define RA_GIT_UUID "a62d4ba0-b83e-11e3-8621-8f162a3365eb"

/* ---------------------------------------------------------------*/

typedef struct svn_ra_git_branch_t
{
  const char *name;
  const char *symref_target;
} svn_ra_git_branch_t;


svn_error_t *
svn_ra_git__split_url(const char **repos_root_url,
                      const char **repos_relpath,
                      const char **git_remote_url,
                      apr_array_header_t **branches,
                      svn_ra_git__session_t *session,
                      const char *url,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool);

svn_error_t *
svn_ra_git__ensure_local_session(svn_ra_session_t *session,
                                 apr_pool_t *scratch_pool);

svn_error_t *
svn_ra_git__git_fetch(svn_ra_session_t *session,
                      svn_boolean_t refresh,
                      apr_pool_t *scratch_pool);

#endif /* SVN_LIBSVN_RA_GIT_H */
