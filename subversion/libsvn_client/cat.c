/*
 * cat.c:  implementation of the 'cat' command
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_io.h"
#include "client.h"



/*** Code. ***/

svn_error_t *
svn_client_cat (svn_stream_t* out,
                const char *url,
                const svn_opt_revision_t *revision,
                svn_client_auth_baton_t *auth_baton,
                apr_pool_t *pool)
{
  svn_ra_plugin_t *ra_lib;
  void *ra_baton, *session;
  svn_revnum_t rev;
  svn_node_kind_t url_kind;

  /* Get the RA library that handles URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, url, pool));

  /* Open a repository session to the URL. */
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, url, NULL, NULL,
                                        NULL, FALSE, FALSE, FALSE,
                                        auth_baton, pool));

  /* Resolve REVISION into a real revnum. */
  SVN_ERR (svn_client__get_revision_number (&rev, ra_lib, session,
                                            revision, NULL, pool));
  if (! SVN_IS_VALID_REVNUM (rev))
    SVN_ERR (ra_lib->get_latest_revnum (session, &rev));

  /* Decide if the URL is a file or directory. */
  SVN_ERR (ra_lib->check_path (&url_kind, session, "", rev));

  if (url_kind == svn_node_dir)
    return svn_error_createf(SVN_ERR_CLIENT_IS_DIRECTORY, 0, NULL,
                             "URL \"%s\" refers to directory", url);

  /* Get the file */
  SVN_ERR (ra_lib->get_file(session, "", rev, out, NULL, NULL));

  SVN_ERR (ra_lib->close (session));

  /* ### FIXME: do keyword expansion, newline translation?  Perhaps
     ### make it optional?  */

  return SVN_NO_ERROR;
}
