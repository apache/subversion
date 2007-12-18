/*
 * util.c:  Repository access utility routines.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* ==================================================================== */

/*** Includes. ***/
#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_error_codes.h"
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
                               svn_path_local_style(path_or_url, pool));
    }
  return SVN_NO_ERROR;
}
