/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_repos_private.h
 * @brief Subversion-internal repos APIs.
 */

#ifndef SVN_REPOS_PRIVATE_H
#define SVN_REPOS_PRIVATE_H

#include <apr_pools.h>

#include "svn_repos.h"
#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/**
 * Permanently delete @a path at revision @a revision in @a fs.
 *
 * Do not change the content of any other node in the repository, even other
 * nodes that were copied from this one. The only other change in the
 * repository is to "copied from" pointers that were pointing to the
 * now-deleted node. These are removed or made to point to a previous
 * version of the now-deleted node.
 * (### TODO: details.)
 *
 * @a path is relative to the repository root and must start with "/".
 *
 * If administratively forbidden, return @c SVN_ERR_RA_NOT_AUTHORIZED. If not
 * implemented by the RA layer or by the server, return
 * @c SVN_ERR_RA_NOT_IMPLEMENTED.
 *
 * @note This functionality is not implemented in pre-1.7 servers and may not
 * be implemented in all 1.7 and later servers.
 *
 * @note TODO: Maybe create svn_repos_fs_begin_obliteration_txn() and
 * svn_repos_fs_commit_obliteration_txn() to enable an obliteration txn to be
 * constructed at a higher level.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_repos__obliterate_path_rev(svn_repos_t *repos,
                               const char *username,
                               svn_revnum_t revision,
                               const char *path,
                               apr_pool_t *pool);


/** Validate that property @a name is valid for use in a Subversion
 * repository; return @c SVN_ERR_REPOS_BAD_ARGS if it isn't.  For some
 * "svn:" properties, also validate the @a value, and return
 * @c SVN_ERR_BAD_PROPERTY_VALUE if it is not valid.
 * 
 * Use @a pool for temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_repos__validate_prop(const char *name,
                         const svn_string_t *value,
                         apr_pool_t *pool);

/**
 * Given the error @a err from svn_repos_fs_commit_txn(), return an
 * string containing either or both of the svn_fs_commit_txn() error
 * and the SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED wrapped error from
 * the post-commit hook.  Any error tracing placeholders in the error
 * chain are skipped over.
 *
 * ### This method should not be necessary, but there are a few
 * ### places, e.g. mod_dav_svn, where only a single error message
 * ### string is returned to the caller and it is useful to have both
 * ### error messages included in the message.
 *
 * Use @a pool to allocate the string in.
 *
 * @since New in 1.7.
 */
const char *
svn_repos__post_commit_error_str(svn_error_t *err,
                                 apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_REPOS_PRIVATE_H */
