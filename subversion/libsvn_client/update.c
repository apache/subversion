/*
 * update.c:  wrappers around wc update functionality
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

#include <assert.h>

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_io.h"
#include "client.h"



/*** Code. ***/

svn_error_t *
svn_client_update (svn_client_auth_baton_t *auth_baton,
                   const char *path,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t recurse,
                   svn_wc_notify_func_t notify_func,
                   void *notify_baton,
                   apr_pool_t *pool)
{
  const svn_delta_editor_t *update_editor;
  void *update_edit_baton;
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  const svn_wc_entry_t *entry;
  const char *URL, *anchor, *target;
  svn_error_t *err;
  svn_revnum_t revnum;
  svn_wc_traversal_info_t *traversal_info = svn_wc_init_traversal_info (pool);
  svn_wc_adm_access_t *adm_access;

  /* Sanity check.  Without this, the update is meaningless. */
  assert (path);

  /* Use PATH to get the update's anchor and targets and get a write lock */
  SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));
  SVN_ERR (svn_wc_adm_open (&adm_access, NULL, anchor, TRUE, TRUE, pool));

  /* Get full URL from the ANCHOR. */
  SVN_ERR (svn_wc_entry (&entry, anchor, adm_access, FALSE, pool));
  if (! entry)
    return svn_error_createf
      (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL,
       "svn_client_update: %s is not under revision control", anchor);
  if (! entry->url)
    return svn_error_createf
      (SVN_ERR_ENTRY_MISSING_URL, 0, NULL,
       "svn_client_update: entry '%s' has no URL", anchor);
  URL = apr_pstrdup (pool, entry->url);

  /* Get revnum set to something meaningful, so we can fetch the
     update editor. */
  if (revision->kind == svn_opt_revision_number)
    revnum = revision->value.number; /* do the trivial conversion manually */
  else
    revnum = SVN_INVALID_REVNUM; /* no matter, do real conversion later */

  /* Fetch the update editor.  If REVISION is invalid, that's okay;
     the RA driver will call editor->set_target_revision later on. */
  SVN_ERR (svn_wc_get_update_editor (adm_access,
                                     target,
                                     revnum,
                                     recurse,
                                     notify_func, notify_baton,
                                     &update_editor, &update_edit_baton,
                                     traversal_info,
                                     pool));

    {
      void *ra_baton, *session;
      svn_ra_plugin_t *ra_lib;
      svn_node_kind_t kind;
      svn_wc_adm_access_t *dir_access;

      /* Get the RA vtable that matches URL. */
      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL, pool));

      /* Open an RA session for the URL */
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, anchor,
                                            adm_access, NULL, TRUE, TRUE, 
                                            auth_baton, pool));

      /* ### todo: shouldn't svn_client__get_revision_number be able
         to take a url as easily as a local path?  */
      SVN_ERR (svn_client__get_revision_number
               (&revnum, ra_lib, session, revision, anchor, pool));

      /* Tell RA to do a update of URL+TARGET to REVISION; if we pass an
         invalid revnum, that means RA will use the latest revision.  */
      SVN_ERR (ra_lib->do_update (session,
                                  &reporter, &report_baton,
                                  revnum,
                                  target,
                                  recurse,
                                  update_editor, update_edit_baton));

      SVN_ERR (svn_io_check_path (path, &kind, pool));
      SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access,
                                    (kind == svn_node_dir
                                     ? path
                                     : svn_path_dirname (path, pool)),
                                    pool));

      /* Drive the reporter structure, describing the revisions within
         PATH.  When we call reporter->finish_report, the
         update_editor will be driven by svn_repos_dir_delta. */
      err = svn_wc_crawl_revisions (path, dir_access, reporter, report_baton,
                                    TRUE, recurse,
                                    notify_func, notify_baton,
                                    traversal_info, pool);
      
      /* Sleep for one second to ensure timestamp integrity. */
      apr_sleep (APR_USEC_PER_SEC * 1);

      if (err)
        return err;

      /* Close the RA session. */
      SVN_ERR (ra_lib->close (session));
    }      
  
  /* We handle externals after the update is complete, so that
     handling external items (and any errors therefrom) doesn't delay
     the primary operation.  */
  if (recurse)
    SVN_ERR (svn_client__handle_externals (traversal_info,
                                           notify_func, notify_baton,
                                           auth_baton,
                                           TRUE, /* update unchanged ones */
                                           pool));

  SVN_ERR (svn_wc_adm_close (adm_access));

  return SVN_NO_ERROR;
}
