/*
 * switch.c:  implement 'switch' feature via wc & ra interfaces.
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

#include <assert.h>

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_time.h"
#include "svn_config.h"
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
svn_client_switch (const char *path,
                   const char *switch_url,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t recurse,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  const svn_wc_entry_t *entry, *session_entry;
  const char *URL, *anchor, *target;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  svn_revnum_t revnum;
  svn_error_t *err = SVN_NO_ERROR;
  svn_wc_adm_access_t *adm_access;
  const char *diff3_cmd;
  svn_boolean_t timestamp_sleep = FALSE;  
  svn_boolean_t use_commit_times;
  const char *commit_time_str;
  const svn_delta_editor_t *switch_editor;
  void *switch_edit_baton;
  svn_wc_traversal_info_t *traversal_info = svn_wc_init_traversal_info (pool);
  svn_config_t *cfg = ctx->config ? apr_hash_get (ctx->config, 
                                                  SVN_CONFIG_CATEGORY_CONFIG,  
                                                  APR_HASH_KEY_STRING)
                                  : NULL;
  
  /* Get the external diff3, if any. */
  svn_config_get (cfg, &diff3_cmd, SVN_CONFIG_SECTION_HELPERS,
                  SVN_CONFIG_OPTION_DIFF3_CMD, NULL);

  /* See if the user wants last-commit timestamps instead of current ones. */
  svn_config_get (cfg, &commit_time_str, SVN_CONFIG_SECTION_MISCELLANY,
                  SVN_CONFIG_OPTION_USE_COMMIT_TIMES, NULL);
  if (commit_time_str)
    use_commit_times = (strcasecmp (commit_time_str, "yes") == 0)
                        ? TRUE : FALSE;
  else
    use_commit_times = FALSE;

  /* Sanity check.  Without these, the switch is meaningless. */
  assert (path);
  assert (switch_url && (switch_url[0] != '\0'));

  /* ### Note: we don't pass the `recurse' flag as the tree_lock
     argument to probe_open below, only because the ra layer is
     planning to blindly invalidate all wcprops below path anyway, and
     it needs a full tree lock to do so.  If someday the ra layer gets
     smarter about this, then we can start passing `recurse' below
     again.  See issue #1000 and related commits for details. */
  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, path, TRUE, TRUE, pool));
  SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));
  
  if (! entry)
    return svn_error_createf
      (SVN_ERR_WC_PATH_NOT_FOUND, NULL,
       "svn_client_switch: '%s' is not under version control", path);

  if (! entry->url)
    return svn_error_createf
      (SVN_ERR_ENTRY_MISSING_URL, NULL,
       "svn_client_switch: entry '%s' has no URL", path);

  if (entry->kind == svn_node_file)
    {
      SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));
      
      /* get the parent entry */
      SVN_ERR (svn_wc_entry (&session_entry, anchor, adm_access, FALSE, pool));
      if (! session_entry)
        return svn_error_createf
          (SVN_ERR_WC_PATH_NOT_FOUND, NULL,
           "svn_client_switch: '%s' is not under version control", anchor);

      if (! session_entry->url)
        return svn_error_createf
          (SVN_ERR_ENTRY_MISSING_URL, NULL,
           "svn_client_switch: directory '%s' has no URL", anchor);
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

  URL = apr_pstrdup (pool, session_entry->url);

  /* Get revnum set to something meaningful, so we can fetch the
     switch editor. */
  if (revision->kind == svn_opt_revision_number)
    revnum = revision->value.number; /* do the trivial conversion manually */
  else
    revnum = SVN_INVALID_REVNUM; /* no matter, do real conversion later */

  /* Get the RA vtable that matches working copy's current URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL, pool));

  /* Open an RA session to 'source' URL */
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, anchor, 
                                        adm_access, NULL, TRUE, FALSE, 
                                        ctx, pool));
  SVN_ERR (svn_client__get_revision_number
           (&revnum, ra_lib, session, revision, path, pool));

  /* Fetch the switch (update) editor.  If REVISION is invalid, that's
     okay; the RA driver will call editor->set_target_revision() later on. */
  SVN_ERR (svn_wc_get_switch_editor (adm_access, target,
                                     revnum, switch_url,
                                     use_commit_times, recurse,
                                     ctx->notify_func, ctx->notify_baton,
                                     ctx->cancel_func, ctx->cancel_baton,
                                     diff3_cmd,
                                     &switch_editor, &switch_edit_baton,
                                     traversal_info, pool));

  /* Tell RA to do a update of URL+TARGET to REVISION; if we pass an
     invalid revnum, that means RA will use the latest revision. */
  SVN_ERR (ra_lib->do_switch (session, &reporter, &report_baton, revnum,
                              target, recurse, switch_url,
                              switch_editor, switch_edit_baton, pool));
      
  /* Drive the reporter structure, describing the revisions within
     PATH.  When we call reporter->finish_report, the update_editor
     will be driven by svn_repos_dir_delta.

     We pass NULL for traversal_info because this is a switch, not an
     update, and therefore we don't want to handle any externals
     except the ones directly affected by the switch. */ 
  err = svn_wc_crawl_revisions (path, adm_access, reporter, report_baton,
                                TRUE, recurse, use_commit_times,
                                ctx->notify_func, ctx->notify_baton,
                                NULL, /* no traversal info */
                                pool);
    
  /* We handle externals after the switch is complete, so that
     handling external items (and any errors therefrom) doesn't delay
     the primary operation.  We ignore the timestamp_sleep value since
     there is an unconditional sleep later on. */
  if (! err)
    err = svn_client__handle_externals (traversal_info, FALSE,
                                        &timestamp_sleep, ctx, pool);

  /* Sleep to ensure timestamp integrity (we do this regardless of
     errors in the actual switch operation(s)). */
  svn_sleep_for_timestamps ();

  /* Return errors we might have sustained. */
  if (err)
    return err;

  SVN_ERR (svn_wc_adm_close (adm_access));

  return SVN_NO_ERROR;
}
