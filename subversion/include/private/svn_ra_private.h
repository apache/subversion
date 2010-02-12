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
 * @file svn_ra_private.h
 * @brief The Subversion repository access library - Internal routines
 */

#ifndef SVN_RA_PRIVATE_H
#define SVN_RA_PRIVATE_H

#include <apr_pools.h>

#include "svn_error.h"
#include "svn_ra.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Return an error with code SVN_ERR_UNSUPPORTED_FEATURE, and an error
   message referencing PATH_OR_URL, if the "server" pointed to be
   RA_SESSION doesn't support Merge Tracking (e.g. is pre-1.5).
   Perform temporary allocations in POOL. */
svn_error_t *
svn_ra__assert_mergeinfo_capable_server(svn_ra_session_t *ra_session,
                                        const char *path_or_url,
                                        apr_pool_t *pool);

/** Permanently delete @a path (relative to the URL of @a session) in revision
 * @a rev.
 *
 * Do not change the content of other node in the repository, even other nodes
 * that were copied from this one. The only other change in the repository is
 * to "copied from" pointers that were pointing to the now-deleted node. These
 * are removed or made to point to a previous version of the now-deleted node.
 * (### TODO: details.)
 *
 * If administratively forbidden, return @c SVN_ERR_RA_NOT_AUTHORIZED. If not
 * implemented by the server, return @c SVN_ERR_RA_NOT_IMPLEMENTED.
 *
 * @note This functionality is not implemented in pre-1.7 servers and may not
 * be implemented in all 1.7 and later servers.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_ra__obliterate_path_rev(svn_ra_session_t *session,
                            svn_revnum_t rev,
                            const char *path,
                            apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_RA_PRIVATE_H */
