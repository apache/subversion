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


/* Return true if KIND is a revision kind that is dependent on the working
 * copy. Otherwise, return false. */
#define SVN_CLIENT__REVKIND_NEEDS_WC(kind)                                 \
  ((kind) == svn_opt_revision_base ||                                      \
   (kind) == svn_opt_revision_previous ||                                  \
   (kind) == svn_opt_revision_working ||                                   \
   (kind) == svn_opt_revision_committed)                                   \

/* Return true if KIND is a revision kind that the WC can supply without
 * contacting the repository. Otherwise, return false. */
#define SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(kind)                           \
  ((kind) == svn_opt_revision_base ||                                      \
   (kind) == svn_opt_revision_working ||                                   \
   (kind) == svn_opt_revision_committed)

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

/* Given PATH_OR_URL, which contains either a working copy path or an
   absolute URL, a peg revision PEG_REVISION, and a desired revision
   REVISION, create an RA connection to that object as it exists in
   that revision, following copy history if necessary.  If REVISION is
   younger than PEG_REVISION, then PATH_OR_URL will be checked to see
   that it is the same node in both PEG_REVISION and REVISION.  If it
   is not, then @c SVN_ERR_CLIENT_UNRELATED_RESOURCES is returned.

   BASE_DIR_ABSPATH is the working copy path the ra_session corresponds to,
   and should only be used if PATH_OR_URL is a url
     ### else NULL? what's it for?

   If PEG_REVISION->kind is 'unspecified', the peg revision is 'head'
   for a URL or 'working' for a WC path.  If REVISION->kind is
   'unspecified', the operative revision is the peg revision.

   Store the resulting ra_session in *RA_SESSION_P.  Store the final
   resolved location of the object in *RESOLVED_LOC_P.  RESOLVED_LOC_P
   may be NULL if not wanted.

   Use authentication baton cached in CTX to authenticate against the
   repository.

   Use POOL for all allocations. */
svn_error_t *
svn_client__ra_session_from_path2(svn_ra_session_t **ra_session_p,
                                 svn_client__pathrev_t **resolved_loc_p,
                                 const char *path_or_url,
                                 const char *base_dir_abspath,
                                 const svn_opt_revision_t *peg_revision,
                                 const svn_opt_revision_t *revision,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *pool);

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

/* Find out what kind of symmetric merge would be needed, when the target
 * is only known as a repository location rather than a WC.
 *
 * Like svn_client__find_symmetric_merge() except that SOURCE_PATH_OR_URL @
 * SOURCE_REVISION should refer to a repository location and not a WC.
 *
 * ### The result, *MERGE_P, may not be suitable for passing to
 * svn_client__do_symmetric_merge().  The target WC state would not be
 * checked (as in the ALLOW_* flags).  We should resolve this problem:
 * perhaps add the allow_* params here, or provide another way of setting
 * them; and perhaps ensure __do_...() will accept the result iff given a
 * WC that matches the stored target location.
 */
svn_error_t *
svn_client__find_symmetric_merge_no_wc(
                                 svn_client__symmetric_merge_t **merge_p,
                                 const char *source_path_or_url,
                                 const svn_opt_revision_t *source_revision,
                                 const char *target_path_or_url,
                                 const svn_opt_revision_t *target_revision,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

/* Set *YCA, *BASE, *RIGHT, *TARGET to the repository locations of the
 * youngest common ancestor of the branches, the base chosen for 3-way
 * merge, the right-hand side of the source diff, and the target WC.
 *
 * Any of the output pointers may be NULL if not wanted.
 */
svn_error_t *
svn_client__symmetric_merge_get_locations(
                                svn_client__pathrev_t **yca,
                                svn_client__pathrev_t **base,
                                svn_client__pathrev_t **right,
                                svn_client__pathrev_t **target,
                                const svn_client__symmetric_merge_t *merge,
                                apr_pool_t *result_pool);

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

/* Return TRUE iff the symmetric merge represented by MERGE is going to be
 * a reintegrate-like merge: that is, merging in the opposite direction
 * from the last full merge.
 *
 * This function exists because the merge is NOT really symmetric and the
 * client can be more friendly if it knows something about the differences.
 */
svn_boolean_t
svn_client__symmetric_merge_is_reintegrate_like(
        const svn_client__symmetric_merge_t *merge);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CLIENT_PRIVATE_H */
