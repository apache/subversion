/*
 * status.c:  return the status of a working copy dirent
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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


/* Update STATUSHASH with update information for the versioned items
   underneath PATH / ADM_ACCESS, obtaining an auth baton from CTX.
   Set *YOUNGEST to the youngest revision in PATH's repository.

   If DESCEND is false, operate only on immediate children of PATH;
   otherwise, be fully recursive.

   Use POOL for all temporary allocation.

   Note: The incoming STATUSHASH already contains `svn_wc_status_t *'
   structures containing local-mod information.  This function just
   runs ra_lib->do_status() to drive an editor that adds update
   information to the structures. */
static svn_error_t *
add_update_info_to_status_hash (apr_hash_t *statushash,
                                svn_revnum_t *youngest,
                                const char *path,
                                svn_wc_adm_access_t *adm_access,
                                svn_client_ctx_t *ctx,
                                svn_boolean_t descend,
                                apr_pool_t *pool)
{
  svn_ra_plugin_t *ra_lib;  
  void *ra_baton, *session, *report_baton;
  const svn_delta_editor_t *status_editor;
  void *status_edit_baton;
  const svn_ra_reporter_t *reporter;
  const char *anchor, *target, *URL;
  svn_wc_adm_access_t *anchor_access;
  const svn_wc_entry_t *entry;

  /* Use PATH to get the update's anchor and targets. */
  SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));

  if (strlen (anchor) != strlen (path))
    /* Using pool cleanup to close it. This needs to be recursive so that
       auth data can be stored. */
    SVN_ERR (svn_wc_adm_open (&anchor_access, NULL, anchor, FALSE, TRUE,
                              pool));
  else
    anchor_access = adm_access;

  /* Get full URL from the ANCHOR. */
  SVN_ERR (svn_wc_entry (&entry, anchor, anchor_access, FALSE, pool));
  if (! entry)
    return svn_error_createf
      (SVN_ERR_ENTRY_NOT_FOUND, NULL,
       "add_update_info_to_status_hash: '%s' is not under revision control",
       anchor);
  if (! entry->url)
    return svn_error_createf
      (SVN_ERR_ENTRY_MISSING_URL, NULL,
       "add_update_info_to_status_hash: entry '%s' has no URL", anchor);
  URL = apr_pstrdup (pool, entry->url);

  /* Get the RA library that handles URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL, pool));

  /* Open a repository session to the URL. */
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, anchor,
                                        anchor_access, NULL, TRUE, TRUE, 
                                        ctx, pool));

  /* Tell RA to drive a status-editor; this will fill in the
     repos_status_* fields in each status struct. */
  SVN_ERR (svn_wc_get_status_editor (&status_editor, &status_edit_baton,
                                     path, adm_access, descend, statushash,
                                     youngest, ctx->cancel_func, 
                                     ctx->cancel_baton, pool));

  SVN_ERR (ra_lib->do_status (session,
                              &reporter, &report_baton,
                              target, descend,
                              status_editor, status_edit_baton, pool));

  /* Drive the reporter structure, describing the revisions within
     PATH.  When we call reporter->finish_report, the
     status_editor will be driven by svn_repos_dir_delta. */
  SVN_ERR (svn_wc_crawl_revisions (path, adm_access, reporter, report_baton, 
                                   FALSE, /* don't restore missing files */
                                   descend,
                                   NULL, NULL, /* notification is N/A */
                                   NULL,
                                   pool));

  return SVN_NO_ERROR;
}





/*** Public Interface. ***/


svn_error_t *
svn_client_status (apr_hash_t **statushash,
                   svn_revnum_t *youngest,
                   const char *path,
                   svn_boolean_t descend,
                   svn_boolean_t get_all,
                   svn_boolean_t update,
                   svn_boolean_t no_ignore,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  apr_hash_t *hash = apr_hash_make (pool);
  svn_wc_adm_access_t *adm_access;

  /* Need to lock the tree as even a non-recursive status requires the
     immediate directories to be locked. */
  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, path, FALSE, TRUE, pool));

  /* Ask the wc to give us a list of svn_wc_status_t structures.
     These structures contain nothing but information found in the
     working copy. */
  SVN_ERR (svn_wc_statuses (hash, path, adm_access,
                            descend, get_all, no_ignore,
                            ctx->notify_func, ctx->notify_baton,
                            ctx->cancel_func, ctx->cancel_baton,
                            ctx->config, pool));

  if (update)    
    {
      /* Add "dry-run" update information to our existing structures.
         (Pass the DESCEND flag here, since we may want to ignore update
         info that is below PATH.)  */
      SVN_ERR (add_update_info_to_status_hash (hash, youngest, path,
                                               adm_access, ctx,
                                               descend, pool));
    }

  SVN_ERR (svn_wc_adm_close (adm_access));

  /* If the caller wants us to contact the repository also... */
  *statushash = hash;

  return SVN_NO_ERROR;
}
