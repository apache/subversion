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

#include "svn_client.h"
#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


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


/** Resolve @a peg to a repository location and open an RA session to there.
 * Set @a *target_p to the location and @a *session_p to the new session,
 * both allocated in @a result_pool.
 *
 * If @a peg->path_or_url is a URL then a peg revision kind of 'unspecified'
 * means 'head', otherwise it means 'base'.
 *
 * @a session_p may be NULL, in which case a temporary session is used.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client__peg_resolve(svn_client_target_t **target_p,
                        svn_ra_session_t **session_p,
                        const svn_client_peg_t *peg,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);


/* This property marks a branch root. Branches with the same value of this
 * property are mergeable. */
#define SVN_PROP_BRANCH_ROOT "svn:ignore" /* ### should be "svn:branch-root" */

/* Set *MARKER to the branch root marker that is common to SOURCE and
 * TARGET, or to NULL if neither has such a marker.
 * If only one has such a marker or they are different, throw an error. */
svn_error_t *
svn_client__check_branch_root_marker(const char **marker,
                                     const svn_client_peg_t *source,
                                     const svn_client_peg_t *target,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *pool);

/* Set *MERGEINFO_CAT to describe the merges into TARGET from paths in
 * SOURCE_LOCATIONS which is an array of (svn_location_segment_t *)
 * elements describing the history of the source branch over the
 * revision range of interest. */
svn_error_t *
svn_client__get_branch_to_branch_mergeinfo(svn_mergeinfo_catalog_t *mergeinfo_cat,
                                           const svn_client_peg_t *target,
                                           apr_array_header_t *source_locations,
                                           svn_client_ctx_t *ctx,
                                           apr_pool_t *result_pool,
                                           apr_pool_t *scratch_pool);

/* Set *SEGMENTS to an array of svn_location_segment_t * objects, each
   representing a reposition location segment for the history of TARGET
   between OLD_REVISION and YOUNG_REVISION, ordered from oldest segment
   to youngest.  *SEGMENTS may be empty but it will never be NULL.

   YOUNG_REVISION must not be NULL.  If OLD_REVISION is NULL it means the
   beginning of TARGET's history.  See svn_ra_get_location_segments() for
   the rules governing START_REVISION and END_REVISION in relation to the
   target's peg revision.

   This is similar to svn_client__repos_location_segments(), but takes
   high-level (pegged) target and revision number parameters.
   */
svn_error_t *
svn_client__get_location_segments(apr_array_header_t **segments,
                                  const svn_client_peg_t *target,
                                  const svn_opt_revision_t *young_revision,
                                  const svn_opt_revision_t *old_revision,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CLIENT_PRIVATE_H */
