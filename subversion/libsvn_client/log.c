/*
 * log.c:  return log messages
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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



/*** Getting update information ***/



/*** Public Interface. ***/


svn_error_t *
svn_client_log (svn_client_auth_baton_t *auth_baton,
                apr_hash_t *paths,
                svn_revnum_t start,
                svn_revnum_t end,
                svn_boolean_t discover_changed_paths,
                svn_ra_log_message_receiver_t receiver,
                void *receiver_baton,
                apr_pool_t *pool)
{
  /* ### todo: ignore PATHS for now, since the server does too. */

  svn_ra_plugin_t *ra_lib;  
  svn_ra_callbacks_t *ra_callbacks;
  void *ra_baton, *cb_baton, *session;
  svn_stringbuf_t *anchor, *target, *URL;
  svn_wc_entry_t *entry;

  /* Use PATH to get the update's anchor and targets. */
  {
    svn_stringbuf_t *path = svn_stringbuf_create (".", pool);
    SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));
  }

  /* ### todo: this is exactly the same logic as in status.c.  I
     wonder how many other places it's repeated in... perhaps it's
     time to abstract this? */

  /* Get full URL from the ANCHOR. */
  SVN_ERR (svn_wc_entry (&entry, anchor, pool));
  if (! entry)
    return svn_error_createf
      (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, pool,
       "svn_client_update: %s is not under revision control", anchor->data);
  if (entry->existence == svn_wc_existence_deleted)
    return svn_error_createf
      (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL, pool,
       "svn_client_update: entry '%s' has been deleted", anchor->data);
  if (! entry->ancestor)
    return svn_error_createf
      (SVN_ERR_WC_ENTRY_MISSING_ANCESTRY, 0, NULL, pool,
       "svn_client_update: entry '%s' has no URL", anchor->data);
  URL = svn_stringbuf_create (entry->ancestor->data, pool);

  /* Do RA interaction here to figure out what is out of date with
     respect to the repository.  All RA errors are non-fatal!! */

  /* Get the RA library that handles URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  if (svn_ra_get_ra_library (&ra_lib, ra_baton, URL->data, pool) != NULL)
    return SVN_NO_ERROR;

  /* Open a repository session to the URL. */
  SVN_ERR (svn_client__get_ra_callbacks (&ra_callbacks, &cb_baton,
                                         auth_baton, anchor, TRUE,
                                         TRUE, pool));
  if (ra_lib->open (&session, URL, ra_callbacks, cb_baton, pool) != NULL)
    return SVN_NO_ERROR;

  SVN_ERR (ra_lib->get_log (session,
                            paths,
                            start,
                            end,
                            discover_changed_paths,
                            receiver,
                            receiver_baton));

  /* We're done with the RA session. */
  (void) ra_lib->close (session);

  return SVN_NO_ERROR;
}


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
