/*
 * status.c:  return the status of a working copy dirent
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

#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>

#include "client.h"

#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_test.h"
#include "svn_io.h"



/*** Getting update information ***/


/* Open an RA session to URL, providing PATH/AUTH_BATON for
   authentication callbacks.

   STATUSHASH has presumably been filled with status structures that
   contain only local-mod information.  Ask RA->do_update() to drive a
   custom editor that will add update information to this collection
   of structures.  Also, use the RA session to fill in the "youngest
   revnum" field in each structure.  */
static svn_error_t *
add_update_info_to_status_hash (apr_hash_t *statushash,
                                svn_stringbuf_t *path,
                                svn_client_auth_baton_t *auth_baton,
                                apr_pool_t *pool)
{
  svn_ra_plugin_t *ra_lib;  
  svn_ra_callbacks_t *ra_callbacks;
  void *ra_baton, *cb_baton, *session, *edit_baton, *report_baton;
  svn_delta_edit_fns_t *status_editor;
  const svn_ra_reporter_t *reporter;
  svn_stringbuf_t *anchor, *target, *URL;
  svn_wc_entry_t *entry;
  svn_error_t *err;

  /* Use PATH to get the update's anchor and targets. */
  SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));

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


  /* Get the RA library that handles URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  err = svn_ra_get_ra_library (&ra_lib, ra_baton, URL->data, pool);
  
  if (err && (err->apr_err != SVN_ERR_RA_ILLEGAL_URL))
    return err;
  else if (err)
    return SVN_NO_ERROR;

  /* Open a repository session to the URL. */
  SVN_ERR (svn_client__get_ra_callbacks (&ra_callbacks, &cb_baton,
                                         auth_baton, anchor, TRUE,
                                         TRUE, pool));
  SVN_ERR (ra_lib->open (&session, URL, ra_callbacks, cb_baton, pool));


  /* Tell RA to drive a status-editor;  this will fill in the
     repos_status_ fields and repos_rev fields in each status struct. */

  SVN_ERR (svn_wc_get_status_editor (&status_editor, &edit_baton,
                                     anchor, target,
                                     statushash, pool));
  SVN_ERR (ra_lib->do_status (session,
                              &reporter, &report_baton,
                              target,
                              status_editor, edit_baton));

  /* Drive the reporter structure, describing the revisions within
     PATH.  When we call reporter->finish_report, the
     status_editor will be driven by svn_repos_dir_delta. */
  SVN_ERR (svn_wc_crawl_revisions (path, reporter, report_baton, 
                                   FALSE, /* don't notice unversioned stuff */
                                   pool));

  /* We're done with the RA session. */
  SVN_ERR (ra_lib->close (session));

  return SVN_NO_ERROR;
}





/*** Public Interface. ***/

/* Given PATH to a working copy directory or file, allocate and return
   a STATUSHASH structure containing the stati of all entries.  If
   DESCEND is non-zero, recurse fully, else do only immediate
   children.  (See svn_wc.h:svn_wc_statuses() for more verbiage on
   this). */
svn_error_t *
svn_client_status (apr_hash_t **statushash,
                   svn_stringbuf_t *path,
                   svn_boolean_t descend,
                   svn_client_auth_baton_t *auth_baton,
                   apr_pool_t *pool)
{
  apr_hash_t *hash = apr_hash_make (pool);

  /* Ask the wc to give us a list of svn_wc_status_t structures. 
     These structures will contain -local mods- only.  */
  SVN_ERR (svn_wc_statuses (hash, path, descend, pool));
  
  /* ### Right here is where we might parse an incoming switch about
     whether to contact the network or not.  :-) */

  /* ### Crap!  This next call needs to honor the DESCEND flag
     somehow! */

  /* Contact the repository, add -update info- to our structures. */
  SVN_ERR (add_update_info_to_status_hash (hash, path,
                                           auth_baton, pool));

  *statushash = hash;

  return SVN_NO_ERROR;
}









/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
