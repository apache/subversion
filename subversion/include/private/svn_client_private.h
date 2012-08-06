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
 * @file svn_client_private.h
 * @brief Subversion-internal client APIs.
 */

#ifndef SVN_CLIENT_PRIVATE_H
#define SVN_CLIENT_PRIVATE_H

#include <apr_pools.h>

#include "svn_ra.h"
#include "svn_client.h"
#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* A location in a repository. */
typedef struct svn_client__pathrev_t
{
  const char *repos_root_url;
  const char *repos_uuid;
  svn_revnum_t rev;
  const char *url;
} svn_client__pathrev_t;

/* Return a new path-rev structure, allocated in RESULT_POOL,
 * initialized with deep copies of REPOS_ROOT_URL, REPOS_UUID, REV and URL. */
svn_client__pathrev_t *
svn_client__pathrev_create(const char *repos_root_url,
                           const char *repos_uuid,
                           svn_revnum_t rev,
                           const char *url,
                           apr_pool_t *result_pool);

/* Return a new path-rev structure, allocated in RESULT_POOL,
 * initialized with deep copies of REPOS_ROOT_URL, REPOS_UUID, and REV,
 * and using the repository-relative RELPATH to construct the URL. */
svn_client__pathrev_t *
svn_client__pathrev_create_with_relpath(const char *repos_root_url,
                                        const char *repos_uuid,
                                        svn_revnum_t rev,
                                        const char *relpath,
                                        apr_pool_t *result_pool);

/* Set *PATHREV_P to a new path-rev structure, allocated in RESULT_POOL,
 * initialized with deep copies of the repository root URL and UUID from
 * RA_SESSION, and of REV and URL. */
svn_error_t *
svn_client__pathrev_create_with_session(svn_client__pathrev_t **pathrev_p,
                                        svn_ra_session_t *ra_session,
                                        svn_revnum_t rev,
                                        const char *url,
                                        apr_pool_t *result_pool);

/* Return a deep copy of PATHREV, allocated in RESULT_POOL. */
svn_client__pathrev_t *
svn_client__pathrev_dup(const svn_client__pathrev_t *pathrev,
                        apr_pool_t *result_pool);

/* Return a deep copy of PATHREV, with a URI-encoded representation of
 * RELPATH joined on to the URL.  Allocate the result in RESULT_POOL. */
svn_client__pathrev_t *
svn_client__pathrev_join_relpath(const svn_client__pathrev_t *pathrev,
                                 const char *relpath,
                                 apr_pool_t *result_pool);

/* Return the repository-relative relpath of PATHREV. */
const char *
svn_client__pathrev_relpath(const svn_client__pathrev_t *pathrev,
                            apr_pool_t *result_pool);

/* Return the repository-relative fspath of PATHREV. */
const char *
svn_client__pathrev_fspath(const svn_client__pathrev_t *pathrev,
                           apr_pool_t *result_pool);


/** Return @c SVN_ERR_ILLEGAL_TARGET if TARGETS contains a mixture of
 * URLs and paths; otherwise return SVN_NO_ERROR.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_client__assert_homogeneous_target_type(const apr_array_header_t *targets);


/* Create a svn_client_status_t structure *CST for LOCAL_ABSPATH, shallow
 * copying data from *STATUS wherever possible and retrieving the other values
 * where needed. Perform temporary allocations in SCRATCH_POOL and allocate the
 * result in RESULT_POOL
 */
svn_error_t *
svn_client__create_status(svn_client_status_t **cst,
                          svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          const svn_wc_status3_t *status,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

/* Set *ANCESTOR_URL and *ANCESTOR_REVISION to the URL and revision,
 * respectively, of the youngest common ancestor of the two locations
 * PATH_OR_URL1@REV1 and PATH_OR_URL2@REV2.  Set *ANCESTOR_RELPATH to
 * NULL and *ANCESTOR_REVISION to SVN_INVALID_REVNUM if they have no
 * common ancestor.  This function assumes that PATH_OR_URL1@REV1 and
 * PATH_OR_URL2@REV2 both refer to the same repository.
 *
 * Use the authentication baton cached in CTX to authenticate against
 * the repository.
 *
 * See also svn_client__get_youngest_common_ancestor().
 */
svn_error_t *
svn_client__youngest_common_ancestor(const char **ancestor_url,
                                     svn_revnum_t *ancestor_rev,
                                     const char *path_or_url1,
                                     const svn_opt_revision_t *revision1,
                                     const char *path_or_url2,
                                     const svn_opt_revision_t *revision2,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool);

/* Get the repository location of the base node at LOCAL_ABSPATH.
 *
 * A pathrev_t wrapper around svn_wc__node_get_base().
 *
 * Set *BASE_P to the location that this node was checked out at or last
 * updated/switched to, regardless of any uncommitted changes (delete,
 * replace and/or copy-here/move-here).
 *
 * If there is no base node at LOCAL_ABSPATH (such as when there is a
 * locally added/copied/moved-here node that is not part of a replace),
 * set *BASE_P to NULL.
 */
svn_error_t *
svn_client__wc_node_get_base(svn_client__pathrev_t **base_p,
                             const char *wc_abspath,
                             svn_wc_context_t *wc_ctx,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/* Get the original location of the WC node at LOCAL_ABSPATH.
 *
 * A pathrev_t wrapper around svn_wc__node_get_origin().
 *
 * Set *ORIGIN_P to the origin of the WC node at WC_ABSPATH.  If the node
 * is a local copy, give the copy-from location.  If the node is locally
 * added or deleted, set *ORIGIN_P to NULL.
 */
svn_error_t *
svn_client__wc_node_get_origin(svn_client__pathrev_t **origin_p,
                               const char *wc_abspath,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool);

/* A macro to mark sections of code that belong to the 'symmetric merge'
 * feature while it's still new. */
#define SVN_WITH_SYMMETRIC_MERGE

#ifdef SVN_WITH_SYMMETRIC_MERGE

/* Details of a symmetric merge. */
typedef struct svn_client__symmetric_merge_t svn_client__symmetric_merge_t;

/* Find the information needed to merge all unmerged changes from a source
 * branch into a target branch.  The information is the locations of the
 * youngest common ancestor, merge base, and such like.
 *
 * Set *MERGE to the information needed to merge all unmerged changes
 * (up to SOURCE_REVISION) from the source branch SOURCE_PATH_OR_URL @
 * SOURCE_REVISION into the target WC at TARGET_WCPATH.
 */
svn_error_t *
svn_client__find_symmetric_merge(svn_client__symmetric_merge_t **merge,
                                 const char *source_path_or_url,
                                 const svn_opt_revision_t *source_revision,
                                 const char *target_wcpath,
                                 svn_boolean_t allow_mixed_rev,
                                 svn_boolean_t allow_local_mods,
                                 svn_boolean_t allow_switched_subtrees,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

/* Perform a symmetric merge.
 *
 * Merge according to MERGE into the WC at TARGET_WCPATH.
 *
 * The other parameters are as in svn_client_merge4().
 *
 * ### TODO: There's little point in this function being the only way the
 * caller can use the result of svn_client__find_symmetric_merge().  The
 * contents of MERGE should be more public, or there should be other ways
 * the caller can use it, or these two functions should be combined into
 * one.  I want to make it more public, and also possibly have more ways
 * to use it in future (for example, do_symmetric_merge_with_step_by_-
 * step_confirmation).
 */
svn_error_t *
svn_client__do_symmetric_merge(const svn_client__symmetric_merge_t *merge,
                               const char *target_wcpath,
                               svn_depth_t depth,
                               svn_boolean_t force,
                               svn_boolean_t record_only,
                               svn_boolean_t dry_run,
                               const apr_array_header_t *merge_options,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *scratch_pool);

#endif /* SVN_WITH_SYMMETRIC_MERGE */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CLIENT_PRIVATE_H */
