/* fs_git.h : interface to Subversion filesystem, private to libsvn_fs_git
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

#include "svn_fs.h"
#include "private/svn_sqlite.h"

#ifndef SVN_LIBSVN_FS__FS_GIT_H
#define SVN_LIBSVN_FS__FS_GIT_H

svn_error_t *
svn_fs_git__wrap_git_error(void);

#define svn_fs_git__wrap_git_error() \
          svn_error_trace(svn_fs_git__wrap_git_error())

#define svn_fs_git__read_only_error()                                         \
          svn_error_create(SVN_ERR_FS_REP_NOT_MUTABLE, NULL,                  \
                           _("The Subversion git filesystem doesn't support " \
                             "write operations"))

#define GIT2_ERR(expr)                        \
  do {                                        \
    int svn_err__git_temp = (expr);           \
    if (svn_err__git_temp)                    \
      return svn_fs_git__wrap_git_error();    \
  } while (0)

typedef struct svn_fs_git_fs_t
{
  git_repository *repos;
  svn_sqlite__db_t *sdb;

  git_revwalk *revwalk;

  svn_error_t *(*svn_fs_open)(svn_fs_t **,
                              const char *,
                              apr_hash_t *,
                              apr_pool_t *,
                              apr_pool_t *);

} svn_fs_git_fs_t;

svn_error_t *
svn_fs_git__initialize_fs_struct(svn_fs_t *fs,
                                 apr_pool_t *scratch_pool);

svn_error_t *
svn_fs_git__create(svn_fs_t *fs,
                   const char *path,
                   apr_pool_t *scratch_pool);

svn_error_t *
svn_fs_git__open(svn_fs_t *fs,
                 const char *path,
                 apr_pool_t *scratch_pool);


/* From util.c */
/* Gets a stream to read the file with the specified OID. */
svn_error_t *
svn_fs_git__get_blob_stream(svn_stream_t **stream,
                            svn_fs_t *fs,
                            const git_oid *oid,
                            apr_pool_t *result_pool);


/* From gitdb.c */
svn_error_t *
svn_fs_git__db_open(svn_fs_t *fs,
                    apr_pool_t *scratch_pool);

svn_error_t *
svn_fs_git__db_create(svn_fs_t *fs,
                      apr_pool_t *scratch_pool);

svn_error_t *
svn_fs_git__db_youngest_rev(svn_revnum_t *youngest_p,
                            svn_fs_t *fs,
                            apr_pool_t *pool);

svn_error_t *
svn_fs_git__db_ensure_commit(svn_revnum_t *commit_rev,
                             svn_fs_t *fs,
                             git_oid *oid,
                             svn_revnum_t youngest_rev,
                             svn_revnum_t prev_rev,
                             const char *relpath,
                             git_reference *ref);

svn_error_t *
svn_fs_git__db_fetch_oid(svn_boolean_t *found,
                         const git_oid **oid,
                         const char **path,
                         svn_fs_t *fs,
                         svn_revnum_t revnum,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);

svn_error_t *
svn_fs_git__db_fetch_rev(svn_revnum_t *revnum,
                         const char **path,
                         svn_fs_t *fs,
                         const git_oid *oid,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);


svn_error_t *
svn_fs_git__db_fetch_checksum(svn_checksum_t **checksum,
                              svn_fs_t *fs,
                              const git_oid *oid,
                              svn_checksum_kind_t kind,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);

svn_error_t *
svn_fs_git__db_find_branch(const char **branch_path,
                           const git_oid **oid,
                           svn_revnum_t *from_rev,
                           svn_fs_t *fs,
                           const char *relpath,
                           svn_revnum_t rev,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

svn_error_t *
svn_fs_git__db_tag_create(svn_revnum_t *tag_rev,
                          svn_fs_t *fs,
                          const char *relpath,
                          svn_revnum_t youngest_rev,
                          svn_revnum_t from_rev,
                          apr_pool_t *scratch_pool);

svn_error_t *
svn_fs_git__db_get_tags_branches(apr_hash_t **tags,
                                 apr_hash_t **branches,
                                 svn_fs_t *fs,
                                 const char *relpath,
                                 svn_revnum_t rev,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

svn_error_t *
svn_fs_git__db_set_uuid(svn_fs_t *fs,
                        const char *uuid,
                        apr_pool_t *scratch_pool);

/* */
svn_error_t *
svn_fs_git__revision_root(svn_fs_root_t **root_p,
                          svn_fs_t *fs,
                          svn_revnum_t rev,
                          apr_pool_t *pool);

/* From git-history.c */
svn_error_t *
svn_fs_git__make_history_simple(svn_fs_history_t **history_p,
                                svn_fs_root_t *root,
                                svn_revnum_t rev_start,
                                svn_revnum_t rev_end,
                                const char *path,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool);

svn_error_t *
svn_fs_git__make_history_commit(svn_fs_history_t **history_p,
                                svn_fs_root_t *root,
                                const git_commit *commit,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool);

svn_error_t *
svn_fs_git__make_history_node(svn_fs_history_t **history_p,
                              svn_fs_root_t *root,
                              const git_commit *commit,
                              const char *relpath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);


/* From revmap.c */
/* Until there is a formal api, this will synchronize the revisions */
svn_error_t *
svn_fs_git__revmap_update(svn_fs_t *fs,
                          svn_fs_git_fs_t *fgf,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *scratch_pool);


#endif
