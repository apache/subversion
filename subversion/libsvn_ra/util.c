/*
 * util.c:  Repository access utility routines.
 *
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
 */

/* ==================================================================== */

/*** Includes. ***/
#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_ra.h"

#include "svn_private_config.h"
#include "private/svn_ra_private.h"

/* Return an error with code SVN_ERR_UNSUPPORTED_FEATURE, and an error
   message referencing PATH_OR_URL, if the "server" pointed to be
   RA_SESSION doesn't support Merge Tracking (e.g. is pre-1.5).
   Perform temporary allocations in POOL. */
svn_error_t *
svn_ra__assert_mergeinfo_capable_server(svn_ra_session_t *ra_session,
                                        const char *path_or_url,
                                        apr_pool_t *pool)
{
  svn_boolean_t mergeinfo_capable;
  SVN_ERR(svn_ra_has_capability(ra_session, &mergeinfo_capable,
                                SVN_RA_CAPABILITY_MERGEINFO, pool));
  if (! mergeinfo_capable)
    {
      if (path_or_url == NULL)
        {
          svn_error_t *err = svn_ra_get_session_url(ra_session, &path_or_url,
                                                    pool);
          if (err)
            {
              /* The SVN_ERR_UNSUPPORTED_FEATURE error is more important,
                 so dummy up the session's URL and chuck this error. */
              svn_error_clear(err);
              path_or_url = "<repository>";
            }
        }
      return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                               _("Retrieval of mergeinfo unsupported by '%s'"),
                               svn_path_is_url(path_or_url) ?
                                 svn_uri_local_style(path_or_url, pool):
                                 svn_dirent_local_style(path_or_url, pool));
    }
  return SVN_NO_ERROR;
}
