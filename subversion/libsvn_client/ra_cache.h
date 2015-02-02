/**
 * ra_cache.h : RA session abstraction layer
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

#ifndef SVN_LIBSVN_CLIENT_RA_CACHE_H
#define SVN_LIBSVN_CLIENT_RA_CACHE_H

#include <apr_pools.h>

#include "svn_auth.h"
#include "svn_client.h"
#include "svn_ra.h"
#include "svn_types.h"

#include "client.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Initializes the PRIVATE_CTX->ra_cache structure. Will use CONFIG
   for for RA sessions created in this context. Assumes that CTX was
   allocated from POOL. */
void
svn_client__ra_cache_init(svn_client__private_ctx_t *private_ctx,
                          apr_hash_t *config,
                          apr_pool_t *pool);

/* Open new repository access session to the repository at BASE_URL or
   reuses existing session cached in the private owner of CTX.

   The function behavior is the same as svn_ra_open4() with ability
   to reuse sessions for same repository.

   The created session will be automatically returned to the list of
   reusable sessions on RESULT_POOL cleanup or by explicit
   svn_client__ra_cache_release_session() call.

   Uses SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_client__ra_cache_open_session(svn_ra_session_t **session_p,
                                  const char **corrected_p,
                                  svn_client_ctx_t *ctx,
                                  const char *base_url,
                                  const char *uuid,
                                  svn_ra_callbacks2_t *cbtable,
                                  void *callback_baton,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool);

/* Returns RA SESSION back to the cache in the private owner of CTX. */
void
svn_client__ra_cache_release_session(svn_client_ctx_t *ctx,
                                     svn_ra_session_t *session);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_CLIENT_RA_CACHE_H */
