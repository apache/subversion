/*
 * switch.c:  implement 'switch' feature via wc & ra interfaces.
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
#include "client.h"



/*** Code. ***/

/* This feature is essentially identical to 'svn update' (see
   ./update.c), but with two differences:

     - the reporter->finish_report() routine needs to make the server
       run delta_dirs() on two *different* paths, rather than on two
       identical paths.

     - after the update runs, we need to more than just
       ensure_uniform_revision;  we need to rewrite all the entries'
       URL attributes.
*/


svn_error_t *
svn_client_switch (const svn_delta_edit_fns_t *before_editor,
                   void *before_edit_baton,
                   const svn_delta_edit_fns_t *after_editor,
                   void *after_edit_baton,
                   svn_client_auth_baton_t *auth_baton,
                   svn_stringbuf_t *path,
                   svn_stringbuf_t *switch_url,
                   const svn_client_revision_t *revision,
                   svn_boolean_t recurse,
                   svn_wc_notify_func_t notify_func,
                   void *notify_baton,
                   apr_pool_t *pool)
{
  const svn_delta_edit_fns_t *switch_editor;
  void *switch_edit_baton;
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  svn_wc_entry_t *entry;
  svn_stringbuf_t *URL, *anchor, *target;
  svn_error_t *err;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  svn_revnum_t revnum;

  /* Sanity check.  Without these, the switch is meaningless. */
  assert (path != NULL);
  assert (path->len > 0);
  assert (switch_url != NULL);
  assert (switch_url->len > 0);

  SVN_ERR (svn_wc_entry (&entry, path, pool));
  if (! entry)
    return svn_error_createf
      (SVN_ERR_WC_PATH_NOT_FOUND, 0, NULL, pool,
       "svn_client_update: %s is not under revision control", path->data);

  if (entry->kind == svn_node_file)
    {
      SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));
      
      /* 'entry' now refers to parent dir */
      SVN_ERR (svn_wc_entry (&entry, anchor, pool));
      if (! entry)
        return svn_error_createf
          (SVN_ERR_WC_PATH_NOT_FOUND, 0, NULL, pool,
           "svn_client_update: %s is not under revision control", path->data);
    }
  else if (entry->kind == svn_node_dir)
    {
      /* Unlike 'svn up', we do *not* split the local path into an
         anchor/target pair.  We do this because we know that the
         target isn't going to be deleted, because we're doing a
         switch.  This means the update editor gets anchored on PATH
         itself, and thus PATH's name will never change, which is
         exactly what we want. */
      anchor = path;
      target = NULL;
      
      /* 'entry' still refers to PATH */
    }

  if (! entry->url)
    return svn_error_createf
      (SVN_ERR_WC_ENTRY_MISSING_URL, 0, NULL, pool,
       "svn_client_switch: entry '%s' has no URL", path->data);
  URL = svn_stringbuf_dup (entry->url, pool);

  /* Get revnum set to something meaningful, so we can fetch the
     switch editor. */
  if (revision->kind == svn_client_revision_number)
    revnum = revision->value.number; /* do the trivial conversion manually */
  else
    revnum = SVN_INVALID_REVNUM; /* no matter, do real conversion later */

  /* Fetch the switch (update) editor.  If REVISION is invalid, that's
     okay; the RA driver will call editor->set_target_revision() later
     on. */
  SVN_ERR (svn_wc_get_switch_editor (anchor,
                                     target,
                                     revnum,
                                     switch_url,
                                     recurse,
                                     &switch_editor,
                                     &switch_edit_baton,
                                     pool));

  /* Wrap it up with outside editors. */
  svn_delta_wrap_editor (&switch_editor, &switch_edit_baton,
                         before_editor, before_edit_baton,
                         switch_editor, switch_edit_baton,
                         after_editor, after_edit_baton, pool);

  /* Get the RA vtable that matches working copy's current URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL->data, pool));
  
  /* Open an RA session to this URL */
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, path,
                                        TRUE, TRUE, auth_baton, pool));
  
  SVN_ERR (svn_client__get_revision_number
           (&revnum, ra_lib, session, revision, path->data, pool));

  /* ### Note: the whole RA interface below will probably change soon. */ 

  /* Tell RA to do a update of URL+TARGET to REVISION; if we pass an
     invalid revnum, that means RA will use the latest revision. */
  SVN_ERR (ra_lib->do_switch (session,
                              &reporter, &report_baton,
                              revnum,
                              target,
                              recurse,
                              switch_url,
                              switch_editor, switch_edit_baton));

  /* Drive the reporter structure, describing the revisions within
     PATH.  When we call reporter->finish_report, the
     update_editor will be driven by svn_repos_dir_delta. */ 
  err = svn_wc_crawl_revisions (path, reporter, report_baton,
                                TRUE, recurse,
                                notify_func, notify_baton,
                                pool);
  
  /* Sleep for one second to ensure timestamp integrity. */
  apr_sleep (APR_USEC_PER_SEC * 1);

  if (err)
    return err;

  /* Close the RA session. */
  SVN_ERR (ra_lib->close (session));


  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
