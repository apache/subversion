/*
 * switch.c:  implement 'switch' feature via WC & RA interfaces.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include "svn_private_config.h"


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
svn_client_switch (svn_revnum_t *result_rev,
                   const char *path,
                   const char *switch_url,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t recurse,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  const svn_wc_entry_t *entry;
  const char *URL, *anchor, *target;
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  svn_revnum_t revnum;
  svn_error_t *err = SVN_NO_ERROR;
  svn_wc_adm_access_t *adm_access, *dir_access;
  svn_node_kind_t kind;
  const char *diff3_cmd;
  svn_boolean_t timestamp_sleep = FALSE;  
  svn_boolean_t use_commit_times;
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
  SVN_ERR (svn_config_get_bool (cfg, &use_commit_times,
                                SVN_CONFIG_SECTION_MISCELLANY,
                                SVN_CONFIG_OPTION_USE_COMMIT_TIMES, FALSE));

  /* Sanity check.  Without these, the switch is meaningless. */
  assert (path);
  assert (switch_url && (switch_url[0] != '\0'));

  /* Use PATH to get the update's anchor and targets and get a write lock */
  SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));

  /* Get a write-lock on the anchor and target.  We need a lock on
     the whole target tree so we can invalidate wcprops on it. */
  SVN_ERR (svn_wc_adm_open2 (&adm_access, NULL, anchor, TRUE,
                             *target ? 0 : -1, pool));
  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if (*target && (kind == svn_node_dir))
    SVN_ERR (svn_wc_adm_open2 (&dir_access, adm_access, path,
                               TRUE, -1, pool));
  else
    dir_access = adm_access;

  SVN_ERR (svn_wc_entry (&entry, anchor, adm_access, FALSE, pool));
  if (! entry)
    return svn_error_createf (SVN_ERR_UNVERSIONED_RESOURCE, NULL, 
                              _("'%s' is not under version control"), anchor);
  if (! entry->url)
    return svn_error_createf (SVN_ERR_ENTRY_MISSING_URL, NULL,
                              _("Directory '%s' has no URL"), anchor);

  URL = apr_pstrdup (pool, entry->url);

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
  SVN_ERR (svn_wc_get_switch_editor (&revnum, adm_access, target,
                                     switch_url, use_commit_times, recurse,
                                     ctx->notify_func, ctx->notify_baton,
                                     ctx->cancel_func, ctx->cancel_baton,
                                     diff3_cmd,
                                     &switch_editor, &switch_edit_baton,
                                     traversal_info, pool));

  /* Tell RA to do an update of URL+TARGET to REVISION; if we pass an
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
  err = svn_wc_crawl_revisions (path, dir_access, reporter, report_baton,
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

  /* Let everyone know we're finished here. */
  if (ctx->notify_func)
    (*ctx->notify_func) (ctx->notify_baton,
                         anchor,
                         svn_wc_notify_update_completed,
                         svn_node_none,
                         NULL,
                         svn_wc_notify_state_inapplicable,
                         svn_wc_notify_state_inapplicable,
                         revnum);

  /* If the caller wants the result revision, give it to them. */
  if (result_rev)
    *result_rev = revnum;
  
  return SVN_NO_ERROR;
}
