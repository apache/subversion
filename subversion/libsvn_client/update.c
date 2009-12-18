/*
 * update.c:  wrappers around wc update functionality
 *
 * ====================================================================
 * Copyright (c) 2000-2009 CollabNet.  All rights reserved.
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

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_config.h"
#include "svn_time.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_io.h"
#include "client.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/*** Code. ***/


/* Context baton for file_fetcher below. */
struct ff_baton
{
  svn_client_ctx_t *ctx;       /* client context used to open ra session */
  const char *repos_root;      /* the root of the ra session */
  svn_ra_session_t *session;   /* the secondary ra session itself */
  apr_pool_t *pool;            /* the pool where the ra session is allocated */
};


/* Implementation of svn_wc_get_file_t.  A feeble callback wrapper
   around svn_ra_get_file(), so that the update_editor can use it to
   fetch any file, any time. */
static svn_error_t *
file_fetcher(void *baton,
             const char *path,
             svn_revnum_t revision,
             svn_stream_t *stream,
             svn_revnum_t *fetched_rev,
             apr_hash_t **props,
             apr_pool_t *pool)
{
  struct ff_baton *ffb = (struct ff_baton *)baton;

  if (! ffb->session)
    SVN_ERR(svn_client__open_ra_session_internal(&(ffb->session),
                                                 ffb->repos_root,
                                                 NULL, NULL, NULL,
                                                 FALSE, TRUE,
                                                 ffb->ctx, ffb->pool));
  return svn_ra_get_file(ffb->session, path, revision, stream,
                         fetched_rev, props, pool);
}


svn_error_t *
svn_client__update_internal(svn_revnum_t *result_rev,
                            const char *path,
                            const svn_opt_revision_t *revision,
                            svn_depth_t depth,
                            svn_boolean_t depth_is_sticky,
                            svn_boolean_t ignore_externals,
                            svn_boolean_t allow_unver_obstructions,
                            svn_boolean_t *timestamp_sleep,
                            svn_boolean_t send_copyfrom_args,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  const svn_delta_editor_t *update_editor;
  void *update_edit_baton;
  const svn_ra_reporter3_t *reporter;
  void *report_baton;
  const svn_wc_entry_t *entry;
  const char *anchor, *target;
  const char *repos_root;
  svn_error_t *err;
  svn_revnum_t revnum;
  int levels_to_lock;
  svn_wc_traversal_info_t *traversal_info = svn_wc_init_traversal_info(pool);
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t use_commit_times;
  svn_boolean_t sleep_here = FALSE;
  svn_boolean_t *use_sleep = timestamp_sleep ? timestamp_sleep : &sleep_here;
  const char *diff3_cmd;
  svn_ra_session_t *ra_session;
  svn_wc_adm_access_t *dir_access;
  const char *preserved_exts_str;
  apr_array_header_t *preserved_exts;
  struct ff_baton *ffb;
  svn_boolean_t server_supports_depth;
  svn_config_t *cfg = ctx->config ? apr_hash_get(ctx->config,
                                                 SVN_CONFIG_CATEGORY_CONFIG,
                                                 APR_HASH_KEY_STRING) : NULL;

  /* An unknown depth can't be sticky. */
  if (depth == svn_depth_unknown)
    depth_is_sticky = FALSE;

  /* ### Ah, the irony.  We'd like to base our levels_to_lock on the
     ### depth we're going to use for the update.  But that may depend
     ### on the depth in the working copy, which we can't discover
     ### without calling adm_open.  We could expend an extra call,
     ### with levels_to_lock=0, to get the real depth (but only if we
     ### need to) and then make the real call... but it's not worth
     ### the complexity right now.  If the requested depth tells us to
     ### lock the entire tree when we don't actually need to, that's a
     ### performance hit, but (except for access contention) it is not
     ### a correctness problem. */

  /* We may have to crop the subtree if the depth is sticky, so lock the
     entire tree in such a situation*/
  levels_to_lock = depth_is_sticky
    ? -1 : SVN_WC__LEVELS_TO_LOCK_FROM_DEPTH(depth);

  /* Sanity check.  Without this, the update is meaningless. */
  SVN_ERR_ASSERT(path);

  if (svn_path_is_url(path))
    return svn_error_createf(SVN_ERR_WC_NOT_DIRECTORY, NULL,
                             _("Path '%s' is not a directory"),
                             path);

  /* Use PATH to get the update's anchor and targets and get a write lock */
  SVN_ERR(svn_wc_adm_open_anchor(&adm_access, &dir_access, &target, path,
                                 TRUE, levels_to_lock,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));
  anchor = svn_wc_adm_access_path(adm_access);

  /* Get full URL from the ANCHOR. */
  SVN_ERR(svn_wc__entry_versioned(&entry, anchor, adm_access, FALSE, pool));
  if (! entry->url)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("Entry '%s' has no URL"),
                             svn_path_local_style(anchor, pool));

  /* We may need to crop the tree if the depth is sticky */
  if (depth_is_sticky && depth < svn_depth_infinity)
    {
      const svn_wc_entry_t *target_entry;
      SVN_ERR(svn_wc_entry(&target_entry,
          svn_dirent_join(svn_wc_adm_access_path(adm_access), target, pool),
          adm_access, TRUE, pool));

      if (target_entry && target_entry->kind == svn_node_dir)
        {
          SVN_ERR(svn_wc_crop_tree(adm_access, target, depth,
                                   ctx->notify_func2, ctx->notify_baton2,
                                   ctx->cancel_func, ctx->cancel_baton,
                                   pool));
          /* If we are asked to exclude a target, we can just stop now. */
          if (depth == svn_depth_exclude)
            {
              SVN_ERR(svn_wc_adm_close2(adm_access, pool));
              return SVN_NO_ERROR;
            }
        }
    }

  /* Get revnum set to something meaningful, so we can fetch the
     update editor. */
  if (revision->kind == svn_opt_revision_number)
    revnum = revision->value.number;
  else
    revnum = SVN_INVALID_REVNUM;

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

  /* Open an RA session for the URL */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, entry->url,
                                               anchor, adm_access,
                                               NULL, TRUE, TRUE,
                                               ctx, pool));

  /* ### todo: shouldn't svn_client__get_revision_number be able
     to take a URL as easily as a local path?  */
  SVN_ERR(svn_client__get_revision_number
          (&revnum, NULL, ra_session, revision, path, pool));

  /* Take the chance to set the repository root on the target.
     Why do we bother doing this for old working copies?
     There are two reasons: first, it's nice to get this information into
     old WCs so they are "ready" when we start depending on it.  (We can
     never *depend* upon it in a strict sense, however.)
     Second, if people mix old and new clients, this information will
     be dropped by the old clients, which might be annoying. */
  SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root, pool));
  SVN_ERR(svn_wc_maybe_set_repos_root(dir_access, path, repos_root, pool));

  /* Build a baton for the file-fetching callback. */
  ffb = apr_pcalloc(pool, sizeof(*ffb));
  ffb->ctx = ctx;
  ffb->repos_root = repos_root;
  ffb->pool = pool;

  /* Fetch the update editor.  If REVISION is invalid, that's okay;
     the RA driver will call editor->set_target_revision later on. */
  SVN_ERR(svn_wc_get_update_editor3(&revnum, adm_access, target,
                                    use_commit_times, depth, depth_is_sticky,
                                    allow_unver_obstructions,
                                    ctx->notify_func2, ctx->notify_baton2,
                                    ctx->cancel_func, ctx->cancel_baton,
                                    ctx->conflict_func, ctx->conflict_baton,
                                    file_fetcher, ffb,
                                    diff3_cmd, preserved_exts,
                                    &update_editor, &update_edit_baton,
                                    traversal_info,
                                    pool));

  /* Tell RA to do an update of URL+TARGET to REVISION; if we pass an
     invalid revnum, that means RA will use the latest revision.  */
  SVN_ERR(svn_ra_do_update2(ra_session,
                            &reporter, &report_baton,
                            revnum,
                            target,
                            depth,
                            send_copyfrom_args,
                            update_editor, update_edit_baton, pool));

  SVN_ERR(svn_ra_has_capability(ra_session, &server_supports_depth,
                                SVN_RA_CAPABILITY_DEPTH, pool));

  /* Drive the reporter structure, describing the revisions within
     PATH.  When we call reporter->finish_report, the
     update_editor will be driven by svn_repos_dir_delta2. */
  err = svn_wc_crawl_revisions4(path, dir_access, reporter, report_baton,
                                TRUE, depth, (! depth_is_sticky),
                                (! server_supports_depth),
                                use_commit_times,
                                ctx->notify_func2, ctx->notify_baton2,
                                traversal_info, pool);

  if (err)
    {
      /* Don't rely on the error handling to handle the sleep later, do
         it now */
      svn_io_sleep_for_timestamps(path, pool);
      return err;
    }
  *use_sleep = TRUE;

  /* We handle externals after the update is complete, so that
     handling external items (and any errors therefrom) doesn't delay
     the primary operation.  */
  if (SVN_DEPTH_IS_RECURSIVE(depth) && (! ignore_externals))
    SVN_ERR(svn_client__handle_externals(adm_access,
                                         traversal_info,
                                         entry->url,
                                         anchor,
                                         repos_root,
                                         depth,
                                         use_sleep, ctx, pool));

  if (sleep_here)
    svn_io_sleep_for_timestamps(path, pool);

  SVN_ERR(svn_wc_adm_close2(adm_access, pool));

  /* Let everyone know we're finished here. */
  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(path, svn_wc_notify_update_completed, pool);
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
svn_client_update3(apr_array_header_t **result_revs,
                   const apr_array_header_t *paths,
                   const svn_opt_revision_t *revision,
                   svn_depth_t depth,
                   svn_boolean_t depth_is_sticky,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t allow_unver_obstructions,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  int i;
  svn_error_t *err = SVN_NO_ERROR;
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *path = NULL;

  if (result_revs)
    *result_revs = apr_array_make(pool, paths->nelts, sizeof(svn_revnum_t));

  for (i = 0; i < paths->nelts; ++i)
    {
      svn_boolean_t sleep;
      svn_revnum_t result_rev;
      path = APR_ARRAY_IDX(paths, i, const char *);

      svn_pool_clear(subpool);

      if (ctx->cancel_func && (err = ctx->cancel_func(ctx->cancel_baton)))
        break;

      err = svn_client__update_internal(&result_rev, path, revision, depth,
                                        depth_is_sticky, ignore_externals,
                                        allow_unver_obstructions,
                                        &sleep, TRUE, ctx, subpool);
      if (err && err->apr_err != SVN_ERR_WC_NOT_DIRECTORY)
        {
          return err;
        }
      else if (err)
        {
          /* SVN_ERR_WC_NOT_DIRECTORY: it's not versioned */
          svn_error_clear(err);
          err = SVN_NO_ERROR;
          result_rev = SVN_INVALID_REVNUM;
          if (ctx->notify_func2)
            (*ctx->notify_func2)(ctx->notify_baton2,
                                 svn_wc_create_notify(path,
                                                      svn_wc_notify_skip,
                                                      subpool), subpool);
        }
      if (result_revs)
        APR_ARRAY_PUSH(*result_revs, svn_revnum_t) = result_rev;
    }

  svn_pool_destroy(subpool);
  svn_io_sleep_for_timestamps((paths->nelts == 1) ? path : NULL, pool);

  return err;
}
