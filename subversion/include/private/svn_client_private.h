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

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CLIENT_PRIVATE_H */
