/*
 * switch.c:  implement 'switch' feature via WC & RA interfaces.
 *
 * ====================================================================
 * Copyright (c) 2000-2008 CollabNet.  All rights reserved.
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
#include "svn_error.h"
#include "svn_time.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_config.h"
#include "svn_pools.h"
#include "client.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


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
svn_client__switch_internal(svn_revnum_t *result_rev,
                            const char *path,
                            const char *switch_url,
                            const svn_opt_revision_t *peg_revision,
                            const svn_opt_revision_t *revision,
                            svn_wc_adm_access_t *adm_access,
                            svn_depth_t depth,
                            svn_boolean_t depth_is_sticky,
                            svn_boolean_t *timestamp_sleep,
                            svn_boolean_t ignore_externals,
                            svn_boolean_t allow_unver_obstructions,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  const svn_ra_reporter3_t *reporter;
  void *report_baton;
  const svn_wc_entry_t *entry;
  const char *URL, *anchor, *target, *source_root, *switch_rev_url;
  svn_ra_session_t *ra_session;
  svn_revnum_t revnum;
  svn_error_t *err = SVN_NO_ERROR;
  svn_wc_adm_access_t *dir_access;
  const svn_boolean_t close_adm_access = ! adm_access;
  const char *diff3_cmd;
  svn_boolean_t use_commit_times;
  svn_boolean_t sleep_here;
  svn_boolean_t *use_sleep = timestamp_sleep ? timestamp_sleep : &sleep_here;
  const svn_delta_editor_t *switch_editor;
  void *switch_edit_baton;
  svn_wc_traversal_info_t *traversal_info = svn_wc_init_traversal_info(pool);
  const char *preserved_exts_str;
  apr_array_header_t *preserved_exts;
  svn_boolean_t server_supports_depth;
  svn_config_t *cfg = ctx->config ? apr_hash_get(ctx->config,
                                                 SVN_CONFIG_CATEGORY_CONFIG,
                                                 APR_HASH_KEY_STRING)
                                  : NULL;

  /* An unknown depth can't be sticky. */
  if (depth == svn_depth_unknown)
    depth_is_sticky = FALSE;

  /* Do not support the situation of both exclude and switch a target. */
  if (depth_is_sticky && depth == svn_depth_exclude)
    return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                             _("Cannot both exclude and switch a path"));

  /* Get the external diff3, if any. */
  svn_config_get(cfg, &diff3_cmd, SVN_CONFIG_SECTION_HELPERS,
                 SVN_CONFIG_OPTION_DIFF3_CMD, NULL);

  /* See if the user wants last-commit timestamps instead of current ones. */
  SVN_ERR(svn_config_get_bool(cfg, &use_commit_times,
                              SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_USE_COMMIT_TIMES, FALSE));

  /* See which files the user wants to preserve the extension of when
     conflict files are made. */
  svn_config_get(cfg, &preserved_exts_str, SVN_CONFIG_SECTION_MISCELLANY,
                 SVN_CONFIG_OPTION_PRESERVED_CF_EXTS, "");
  preserved_exts = *preserved_exts_str
    ? svn_cstring_split(preserved_exts_str, "\n\r\t\v ", FALSE, pool)
    : NULL;

  /* Sanity check.  Without these, the switch is meaningless. */
  SVN_ERR_ASSERT(path);
  SVN_ERR_ASSERT(switch_url && (switch_url[0] != '\0'));

  /* ### Need to lock the whole target tree to invalidate wcprops. Does
     non-recursive switch really need to invalidate the whole tree? */
  if (adm_access)
    {
      svn_wc_adm_access_t *a = adm_access;
      const char *dir_access_path;

      /* This is a little hacky, but open two new read-only access
         baton's to get the anchor and target access batons that would
         be used if a locked access baton was not available. */
      SVN_ERR(svn_wc_adm_open_anchor(&adm_access, &dir_access, &target, path,
                                     FALSE, -1, ctx->cancel_func,
                                     ctx->cancel_baton, pool));
      anchor = svn_wc_adm_access_path(adm_access);
      dir_access_path = svn_wc_adm_access_path(dir_access);
      SVN_ERR(svn_wc_adm_close2(adm_access, pool));

      SVN_ERR(svn_wc_adm_retrieve(&adm_access, a, anchor, pool));
      SVN_ERR(svn_wc_adm_retrieve(&dir_access, a, dir_access_path, pool));
    }
  else
    {
      SVN_ERR(svn_wc_adm_open_anchor(&adm_access, &dir_access, &target, path,
                                     TRUE, -1, ctx->cancel_func,
                                     ctx->cancel_baton, pool));
      anchor = svn_wc_adm_access_path(adm_access);
    }

  SVN_ERR(svn_wc__entry_versioned(&entry, anchor, adm_access, FALSE, pool));
  if (! entry->url)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("Directory '%s' has no URL"),
                             svn_path_local_style(anchor, pool));

  URL = apr_pstrdup(pool, entry->url);

  /* Open an RA session to 'source' URL */
  SVN_ERR(svn_client__ra_session_from_path(&ra_session, &revnum,
                                           &switch_rev_url,
                                           switch_url, adm_access,
                                           peg_revision, revision,
                                           ctx, pool));
  SVN_ERR(svn_ra_get_repos_root2(ra_session, &source_root, pool));

  /* Disallow a switch operation to change the repository root of the
     target. */
  if (! svn_path_is_ancestor(source_root, URL))
    return svn_error_createf
      (SVN_ERR_WC_INVALID_SWITCH, NULL,
       _("'%s'\n"
         "is not the same repository as\n"
         "'%s'"), URL, source_root);

  /* We may need to crop the tree if the depth is sticky */
  if (depth_is_sticky && depth < svn_depth_infinity)
    {
      const svn_wc_entry_t *target_entry;

      SVN_ERR(svn_wc_entry(
          &target_entry,
          svn_dirent_join(svn_wc_adm_access_path(adm_access), target, pool),
          adm_access, TRUE, pool));

      if (target_entry && target_entry->kind == svn_node_dir)
        {
          SVN_ERR(svn_wc_crop_tree(adm_access, target, depth,
                                   ctx->notify_func2, ctx->notify_baton2,
                                   ctx->cancel_func, ctx->cancel_baton,
                                   pool));
        }
    }

  SVN_ERR(svn_ra_reparent(ra_session, URL, pool));

  /* Fetch the switch (update) editor.  If REVISION is invalid, that's
     okay; the RA driver will call editor->set_target_revision() later on. */
  SVN_ERR(svn_wc_get_switch_editor3(&revnum, adm_access, target,
                                    switch_rev_url, use_commit_times, depth,
                                    depth_is_sticky, allow_unver_obstructions,
                                    ctx->notify_func2, ctx->notify_baton2,
                                    ctx->cancel_func, ctx->cancel_baton,
                                    ctx->conflict_func, ctx->conflict_baton,
                                    diff3_cmd, preserved_exts,
                                    &switch_editor, &switch_edit_baton,
                                    traversal_info, pool));

  /* Tell RA to do an update of URL+TARGET to REVISION; if we pass an
     invalid revnum, that means RA will use the latest revision. */
  SVN_ERR(svn_ra_do_switch2(ra_session, &reporter, &report_baton, revnum,
                            target, depth, switch_rev_url,
                            switch_editor, switch_edit_baton, pool));

  SVN_ERR(svn_ra_has_capability(ra_session, &server_supports_depth,
                                SVN_RA_CAPABILITY_DEPTH, pool));

  /* Drive the reporter structure, describing the revisions within
     PATH.  When we call reporter->finish_report, the update_editor
     will be driven by svn_repos_dir_delta2.

     We pass in a traversal_info for recording all externals. It
     shouldn't be needed for a switch if it wasn't for the relative
     externals of type '../path'. All of those must be resolved to 
     the new location.  */
  err = svn_wc_crawl_revisions4(path, dir_access, reporter, report_baton,
                                TRUE, depth, (! depth_is_sticky),
                                (! server_supports_depth),
                                use_commit_times,
                                ctx->notify_func2, ctx->notify_baton2,
                                traversal_info, 
                                pool);

  if (err)
    {
      /* Don't rely on the error handling to handle the sleep later, do
         it now */
      svn_io_sleep_for_timestamps(path, pool);
      return err;
    }
  *use_sleep = TRUE;

  /* We handle externals after the switch is complete, so that
     handling external items (and any errors therefrom) doesn't delay
     the primary operation. */
  if (SVN_DEPTH_IS_RECURSIVE(depth) && (! ignore_externals))
    err = svn_client__handle_externals(adm_access, traversal_info, switch_url,
                                       path, source_root, depth,
                                       use_sleep, ctx, pool);

  /* Sleep to ensure timestamp integrity (we do this regardless of
     errors in the actual switch operation(s)). */
  if (sleep_here)
    svn_io_sleep_for_timestamps(path, pool);

  /* Return errors we might have sustained. */
  if (err)
    return err;

  if (close_adm_access)
    SVN_ERR(svn_wc_adm_close2(adm_access, pool));

  /* Let everyone know we're finished here. */
  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(anchor, svn_wc_notify_update_completed, pool);
      notify->kind = svn_node_none;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_inapplicable;
      notify->lock_state = svn_wc_notify_lock_state_inapplicable;
      notify->revision = revnum;
      (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
    }

  /* If the caller wants the result revision, give it to them. */
  if (result_rev)
    *result_rev = revnum;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_switch2(svn_revnum_t *result_rev,
                   const char *path,
                   const char *switch_url,
                   const svn_opt_revision_t *peg_revision,
                   const svn_opt_revision_t *revision,
                   svn_depth_t depth,
                   svn_boolean_t depth_is_sticky,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t allow_unver_obstructions,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client__switch_internal(result_rev, path, switch_url,
                                     peg_revision, revision, NULL, depth,
                                     depth_is_sticky, NULL, ignore_externals,
                                     allow_unver_obstructions, ctx, pool);
}
