/*
 * log.c:  return log messages
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

#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>

#include "client.h"

#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"



/*** Getting update information ***/



/*** Public Interface. ***/


svn_error_t *
svn_client_log (svn_client_auth_baton_t *auth_baton,
                const apr_array_header_t *targets,
                const svn_client_revision_t *start,
                const svn_client_revision_t *end,
                svn_boolean_t discover_changed_paths,
                svn_log_message_receiver_t receiver,
                void *receiver_baton,
                apr_pool_t *pool)
{
  svn_ra_plugin_t *ra_lib;  
  void *ra_baton, *session;
  svn_stringbuf_t *URL;
  svn_wc_entry_t *entry;
  svn_stringbuf_t *basename;
  apr_array_header_t *condensed_targets;
  svn_revnum_t start_revnum, end_revnum;

  if ((start->kind == svn_client_revision_unspecified)
      || (end->kind == svn_client_revision_unspecified))
    {
      return svn_error_create
        (SVN_ERR_CLIENT_BAD_REVISION, 0, NULL, pool,
         "svn_client_log: caller failed to supply revision");
    }

  SVN_ERR (svn_path_condense_targets (&basename, &condensed_targets,
                                      targets, pool));

  /* Get full URL from the common path, carefully. */
  SVN_ERR (svn_wc_entry (&entry, basename, pool));
  if (! entry)
    return svn_error_createf
      (SVN_ERR_UNVERSIONED_RESOURCE, 0, NULL, pool,
       "svn_client_log: %s is not under revision control", basename->data);
  if (! entry->url)
    return svn_error_createf
      (SVN_ERR_ENTRY_MISSING_URL, 0, NULL, pool,
       "svn_client_log: entry '%s' has no URL", basename->data);
  URL = svn_stringbuf_dup (entry->url, pool);

  /* Get the RA library that handles URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL->data, pool));

  /* Open a repository session to the URL. */
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, basename,
                                        TRUE, TRUE, auth_baton, pool));

  SVN_ERR (svn_client__get_revision_number
           (&start_revnum, ra_lib, session, start, basename->data, pool));
  SVN_ERR (svn_client__get_revision_number
           (&end_revnum, ra_lib, session, end, basename->data, pool));

  SVN_ERR (ra_lib->get_log (session,
                            condensed_targets,  /* ### todo: or `targets'? */
                            start_revnum,
                            end_revnum,
                            discover_changed_paths,
                            receiver,
                            receiver_baton));

  /* We're done with the RA session. */
  SVN_ERR (ra_lib->close (session));

  return SVN_NO_ERROR;
}


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
