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
svn_client_switch (svn_client_auth_baton_t *auth_baton,
                   const char *path,
                   const char *switch_url,
                   const svn_client_revision_t *revision,
                   svn_boolean_t recurse,
                   svn_wc_notify_func_t notify_func,
                   void *notify_baton,
                   apr_pool_t *pool)
{
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  svn_wc_entry_t *entry, *session_entry;
  const char *URL, *anchor, *target;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  svn_revnum_t revnum;
  svn_error_t *err = NULL;

  /* Sanity check.  Without these, the switch is meaningless. */
  assert (path && (path[0] != '\0'));
  assert (switch_url && (switch_url[0] != '\0'));

  SVN_ERR (svn_wc_entry (&entry, path, FALSE, pool));
  if (! entry)
    return svn_error_createf
      (SVN_ERR_WC_PATH_NOT_FOUND, 0, NULL, pool,
       "svn_client_switch: %s is not under revision control", path);

  if (entry->kind == svn_node_file)
    {
      SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));
      
      /* get the parent entry */
      SVN_ERR (svn_wc_entry (&session_entry, anchor, FALSE, pool));
      if (! entry)
        return svn_error_createf
          (SVN_ERR_WC_PATH_NOT_FOUND, 0, NULL, pool,
           "svn_client_switch: %s is not under revision control", path);
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
      session_entry = entry;
    }

  if (! session_entry->url)
    return svn_error_createf
      (SVN_ERR_ENTRY_MISSING_URL, 0, NULL, pool,
       "svn_client_switch: entry '%s' has no URL", path);
  URL = apr_pstrdup (pool, session_entry->url);

  /* Get revnum set to something meaningful, so we can fetch the
     switch editor. */
  if (revision->kind == svn_client_revision_number)
    revnum = revision->value.number; /* do the trivial conversion manually */
  else
    revnum = SVN_INVALID_REVNUM; /* no matter, do real conversion later */

  /* Get the RA vtable that matches working copy's current URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL, pool));
    
  if (entry->kind == svn_node_dir)
    {
      const svn_delta_editor_t *switch_editor;
      void *switch_edit_baton;
      const svn_delta_edit_fns_t *wrapped_old_editor;
      void *wrapped_old_edit_baton;
      svn_wc_traversal_info_t *traversal_info
        = svn_wc_init_traversal_info (pool);

      /* Open an RA session to 'source' URL */
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, path,
                                            NULL, TRUE, TRUE, FALSE, 
                                            auth_baton, pool));
      SVN_ERR (svn_client__get_revision_number
               (&revnum, ra_lib, session, revision, path, pool));

      /* Fetch the switch (update) editor.  If REVISION is invalid, that's
         okay; the RA driver will call editor->set_target_revision() later
         on. */
      SVN_ERR (svn_wc_get_switch_editor (anchor, target,
                                         revnum, switch_url, recurse,
                                         notify_func, notify_baton,
                                         &switch_editor, &switch_edit_baton,
                                         traversal_info, pool));

      /* ### todo:  This is a TEMPORARY wrapper around our editor so we
         can use it with an old driver. */
      svn_delta_compat_wrap (&wrapped_old_editor, &wrapped_old_edit_baton, 
                             switch_editor, switch_edit_baton, pool);

      /* Tell RA to do a update of URL+TARGET to REVISION; if we pass an
         invalid revnum, that means RA will use the latest revision. */
      SVN_ERR (ra_lib->do_switch (session,
                                  &reporter, &report_baton,
                                  revnum,
                                  target,
                                  recurse,
                                  switch_url,
                                  wrapped_old_editor, wrapped_old_edit_baton));
      
      /* Drive the reporter structure, describing the revisions within
         PATH.  When we call reporter->finish_report, the
         update_editor will be driven by svn_repos_dir_delta. 

         We pass NULL for traversal_info because this is a switch, not
         an update, and therefore we don't want to handle any
         externals except the ones directly affected by the switch. */ 
      err = svn_wc_crawl_revisions (path, reporter, report_baton,
                                    TRUE, recurse,
                                    notify_func, notify_baton,
                                    NULL, /* no traversal info */
                                    pool);

      /* We handle externals after the switch is complete, so that
         handling external items (and any errors therefrom) doesn't
         delay the primary operation.  */
      SVN_ERR (svn_client__handle_externals
               (traversal_info,
                notify_func, notify_baton,
                auth_baton,
                FALSE,
                pool));
    }
  
  else if (entry->kind == svn_node_file)
    {
      /* If switching a single file, just fetch the file directly and
         "install" it into the working copy just like the
         update-editor does.  */

      apr_array_header_t *proparray;
      apr_hash_t *prophash;
      apr_hash_index_t *hi;
      apr_file_t *fp;
      const char *new_text_path;
      svn_stream_t *file_stream;
      svn_revnum_t fetched_rev = 1; /* this will be set by get_file() */

      /* Create a unique file */
      SVN_ERR (svn_io_open_unique_file (&fp, &new_text_path,
                                        path, ".new-text-base",
                                        FALSE, /* don't delete on close */
                                        pool));

      /* Create a generic stream that operates on this file.  */
      file_stream = svn_stream_from_aprfile (fp, pool);

      /* Open an RA session to 'target' file URL. */
      /* ### FIXME: we shouldn't be passing a NULL base-dir to
         open_ra_session.  This is a just a way of forcing the server
         to send a fulltext instead of svndiff data against something
         in our working copy.  We need to add a callback to
         open_ra_session that will fetch a 'source' stream from the
         WC, so that ra_dav's implementation of get_file() can use the
         svndiff data to construct a fulltext.  */
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, switch_url, NULL,
                                            NULL, TRUE, TRUE, TRUE,
                                            auth_baton, pool));
      SVN_ERR (svn_client__get_revision_number
               (&revnum, ra_lib, session, revision, path, pool));

      /* Passing "" as a relative path, since we opened the file URL.
         This pushes the text of the file into our file_stream, which
         means it ends up in our unique tmpfile.  We also get the full
         proplist. */
      SVN_ERR (ra_lib->get_file (session, "", revnum, file_stream,
                                 &fetched_rev, &prophash));
      SVN_ERR (svn_stream_close (file_stream));

      /* Convert the prophash into an array, which is what
         svn_wc_install_file (and its helpers) want.  */
      proparray = apr_array_make (pool, 1, sizeof(svn_prop_t));
      for (hi = apr_hash_first (pool, prophash); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          svn_prop_t *prop;
          
          apr_hash_this (hi, &key, NULL, &val);

          prop = apr_array_push (proparray);
          prop->name = key;
          prop->value = svn_string_create_from_buf ((svn_stringbuf_t *) val,
                                                    pool);
        }

      /* This the same code as the update-editor's close_file(). */
      {
        svn_wc_notify_state_t content_state;
        svn_wc_notify_state_t prop_state;
        
        SVN_ERR (svn_wc_install_file (&content_state, &prop_state,
                                      path, fetched_rev,
                                      new_text_path,
                                      proparray, TRUE, /* is full proplist */
                                      switch_url, /* new url */
                                      pool));     
        if (notify_func != NULL)
          (*notify_func) (notify_baton, path, svn_wc_notify_update_update,
                          svn_node_file,
                          NULL,
                          content_state,
                          prop_state,
                          SVN_INVALID_REVNUM);
      }
    }
  
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
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
