/**
 * @copyright
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_RA_PRIVATE_H */
