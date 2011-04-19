/*
 * update.c:  wrappers around wc update functionality
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
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

/* This is a helper for svn_client__update_internal(), which see for
   an explanation of most of these parameters.  Some stuff that's
   unique is as follows:

   ANCHOR_ABSPATH is the local absolute path of the update anchor.
   This is typically either the same as LOCAL_ABSPATH, or the
   immediate parent of LOCAL_ABSPATH.

   If NOTIFY_SUMMARY is set (and there's a notification hander in
   CTX), transmit the final update summary upon successful
   completion of the update.
*/
static svn_error_t *
update_internal(svn_revnum_t *result_rev,
                const char *local_abspath,
                const char *anchor_abspath,
                const svn_opt_revision_t *revision,
                svn_depth_t depth,
                svn_boolean_t depth_is_sticky,
                svn_boolean_t ignore_externals,
                svn_boolean_t allow_unver_obstructions,
                svn_boolean_t adds_as_modification,
                svn_boolean_t *timestamp_sleep,
                svn_boolean_t innerupdate,
                svn_boolean_t notify_summary,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  const svn_delta_editor_t *update_editor;
  void *update_edit_baton;
  const svn_ra_reporter3_t *reporter;
  void *report_baton;
  const char *anchor_url;
  const char *corrected_url;
  const char *target;
  const char *repos_root;
  svn_error_t *err;
  svn_revnum_t revnum;
  svn_boolean_t use_commit_times;
  svn_boolean_t sleep_here = FALSE;
  svn_boolean_t *use_sleep = timestamp_sleep ? timestamp_sleep : &sleep_here;
  const char *diff3_cmd;
  svn_ra_session_t *ra_session;
  const char *preserved_exts_str;
  apr_array_header_t *preserved_exts;
  svn_client__external_func_baton_t efb;
  svn_boolean_t server_supports_depth;
  svn_config_t *cfg = ctx->config ? apr_hash_get(ctx->config,
                                                 SVN_CONFIG_CATEGORY_CONFIG,
                                                 APR_HASH_KEY_STRING) : NULL;

  /* An unknown depth can't be sticky. */
  if (depth == svn_depth_unknown)
    depth_is_sticky = FALSE;

  if (strcmp(local_abspath, anchor_abspath))
    target = svn_dirent_basename(local_abspath, pool);
  else
    target = "";

  /* Get full URL from the ANCHOR. */
  SVN_ERR(svn_wc__node_get_url(&anchor_url, ctx->wc_ctx, anchor_abspath,
                               pool, pool));
  if (! anchor_url)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("'%s' has no URL"),
                             svn_dirent_local_style(anchor_abspath, pool));

  /* Check if our anchor exists in BASE. If it doesn't we can't update.
     ### For performance reasons this should be handled with the same query
     ### as retrieving the anchor url. */
  SVN_ERR(svn_wc__node_get_base_rev(&revnum, ctx->wc_ctx, anchor_abspath,
                                    pool));

  if (!SVN_IS_VALID_REVNUM(revnum))
    {
      if (ctx->notify_func2)
        {
          svn_wc_notify_t *nt;

          nt = svn_wc_create_notify(local_abspath,
                                    svn_wc_notify_update_skip_working_only,
                                    pool);

          ctx->notify_func2(ctx->notify_baton2, nt, pool);
        }
      return SVN_NO_ERROR;
    }

  /* We may need to crop the tree if the depth is sticky */
  if (depth_is_sticky && depth < svn_depth_infinity)
    {
      svn_node_kind_t target_kind;

      if (depth == svn_depth_exclude)
        {
          SVN_ERR(svn_wc_exclude(ctx->wc_ctx,
                                 local_abspath,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 ctx->notify_func2, ctx->notify_baton2,
                                 pool));

          /* Target excluded, we are done now */
          return SVN_NO_ERROR;
        }

      SVN_ERR(svn_wc_read_kind(&target_kind, ctx->wc_ctx, local_abspath, TRUE,
                               pool));
      if (target_kind == svn_node_dir)
        {
          SVN_ERR(svn_wc_crop_tree2(ctx->wc_ctx, local_abspath, depth,
                                    ctx->cancel_func, ctx->cancel_baton,
                                    ctx->notify_func2, ctx->notify_baton2,
                                    pool));
        }
    }

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

  /* Let everyone know we're starting a real update (unless we're
     asked not to). */
  if (ctx->notify_func2 && notify_summary)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(local_abspath, svn_wc_notify_update_started,
                               pool);
      notify->kind = svn_node_none;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_inapplicable;
      notify->lock_state = svn_wc_notify_lock_state_inapplicable;
      (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
    }

  /* Open an RA session for the URL */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, &corrected_url,
                                               anchor_url,
                                               anchor_abspath, NULL, TRUE,
                                               TRUE, ctx, pool));

  /* If we got a corrected URL from the RA subsystem, we'll need to
     relocate our working copy first. */
  if (corrected_url)
    {
      SVN_ERR(svn_client_relocate2(anchor_abspath, anchor_url, corrected_url,
                                   TRUE, ctx, pool));
      anchor_url = corrected_url;
    }

  /* ### todo: shouldn't svn_client__get_revision_number be able
     to take a URL as easily as a local path?  */
  SVN_ERR(svn_client__get_revision_number(&revnum, NULL, ctx->wc_ctx,
                                          local_abspath, ra_session, revision,
                                          pool));

  /* Take the chance to set the repository root on the target.
     It's nice to get this information into old WCs so they are "ready"
     when we start depending on it.  (We can never *depend* upon it in
     a strict sense, however.) */
  SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root, pool));

  /* Build a baton for the externals-info-gatherer callback. */
  efb.externals_new = apr_hash_make(pool);
  efb.externals_old = apr_hash_make(pool);
  efb.ambient_depths = apr_hash_make(pool);
  efb.result_pool = pool;

  SVN_ERR(svn_ra_has_capability(ra_session, &server_supports_depth,
                                SVN_RA_CAPABILITY_DEPTH, pool));

  /* Fetch the update editor.  If REVISION is invalid, that's okay;
     the RA driver will call editor->set_target_revision later on. */
  SVN_ERR(svn_wc_get_update_editor4(&update_editor, &update_edit_baton,
                                    &revnum, ctx->wc_ctx, anchor_abspath,
                                    target, use_commit_times, depth,
                                    depth_is_sticky, allow_unver_obstructions,
                                    adds_as_modification,
                                    server_supports_depth,
                                    diff3_cmd, preserved_exts,
                                    ctx->conflict_func, ctx->conflict_baton,
                                    ignore_externals
                                        ? NULL
                                        : svn_client__external_info_gatherer,
                                    &efb,
                                    ctx->cancel_func, ctx->cancel_baton,
                                    ctx->notify_func2, ctx->notify_baton2,
                                    pool, pool));

  /* Tell RA to do an update of URL+TARGET to REVISION; if we pass an
     invalid revnum, that means RA will use the latest revision.  */
  SVN_ERR(svn_ra_do_update2(ra_session, &reporter, &report_baton,
                            revnum, target,
                            depth_is_sticky ? depth : svn_depth_unknown,
                            FALSE, update_editor, update_edit_baton, pool));

  /* Drive the reporter structure, describing the revisions within
     PATH.  When we call reporter->finish_report, the
     update_editor will be driven by svn_repos_dir_delta2. */
  err = svn_wc_crawl_revisions5(ctx->wc_ctx, local_abspath, reporter,
                                report_baton, TRUE, depth, (! depth_is_sticky),
                                (! server_supports_depth),
                                use_commit_times,
                                ignore_externals
                                        ? NULL
                                        : svn_client__external_info_gatherer,
                                &efb, ctx->cancel_func, ctx->cancel_baton,
                                ctx->notify_func2, ctx->notify_baton2,
                                pool);

  if (err)
    {
      /* Don't rely on the error handling to handle the sleep later, do
         it now */
      svn_io_sleep_for_timestamps(local_abspath, pool);
      return svn_error_return(err);
    }
  *use_sleep = TRUE;

  /* We handle externals after the update is complete, so that
     handling external items (and any errors therefrom) doesn't delay
     the primary operation.  */
  if (SVN_DEPTH_IS_RECURSIVE(depth) && (! ignore_externals))
    {
      SVN_ERR(svn_client__gather_externals_in_locally_added_dirs(
                efb.externals_new, efb.ambient_depths, anchor_abspath,
                depth, ctx, pool));
      SVN_ERR(svn_client__handle_externals(efb.externals_old,
                                           efb.externals_new,
                                           efb.ambient_depths,
                                           svn_dirent_join(anchor_abspath,
                                                           target, pool),
                                           repos_root,
                                           depth, FALSE, use_sleep,
                                           ctx, pool));
    }

  if (sleep_here)
    svn_io_sleep_for_timestamps(local_abspath, pool);

  /* Let everyone know we're finished here (unless we're asked not to). */
  if (ctx->notify_func2 && notify_summary)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(local_abspath, svn_wc_notify_update_completed,
                               pool);
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
svn_client__update_internal(svn_revnum_t *result_rev,
                            const char *local_abspath,
                            const svn_opt_revision_t *revision,
                            svn_depth_t depth,
                            svn_boolean_t depth_is_sticky,
                            svn_boolean_t ignore_externals,
                            svn_boolean_t allow_unver_obstructions,
                            svn_boolean_t adds_as_modification,
                            svn_boolean_t *timestamp_sleep,
                            svn_boolean_t innerupdate,
                            svn_boolean_t make_parents,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  const char *anchor_abspath, *lockroot_abspath;
  svn_error_t *err;
  svn_opt_revision_t peg_revision = *revision;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(! (innerupdate && make_parents));

  if (make_parents)
    {
      int i;
      const char *parent_abspath = local_abspath;
      apr_array_header_t *missing_parents =
        apr_array_make(pool, 4, sizeof(const char *));

      while (1)
        {
          /* Try to lock.  If we can't lock because our target (or its
             parent) isn't a working copy, we'll try to walk up the
             tree to find a working copy, remembering this path's
             parent as one we need to flesh out.  */
          err = svn_wc__acquire_write_lock(&lockroot_abspath, ctx->wc_ctx,
                                           parent_abspath, !innerupdate,
                                           pool, pool);
          if (!err)
            break;
          if ((err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY)
              || svn_dirent_is_root(parent_abspath, strlen(parent_abspath)))
            return err;
          svn_error_clear(err);

          /* Remember the parent of our update target as a missing
             parent. */
          parent_abspath = svn_dirent_dirname(parent_abspath, pool);
          APR_ARRAY_PUSH(missing_parents, const char *) = parent_abspath;
        }

      /* Run 'svn up --depth=empty' (effectively) on the missing
         parents, if any. */
      anchor_abspath = lockroot_abspath;
      for (i = missing_parents->nelts - 1; i >= 0; i--)
        {
          const char *missing_parent =
            APR_ARRAY_IDX(missing_parents, i, const char *);
          err = update_internal(result_rev, missing_parent, anchor_abspath,
                                &peg_revision, svn_depth_empty, FALSE,
                                ignore_externals, allow_unver_obstructions,
                                adds_as_modification, timestamp_sleep,
                                innerupdate, FALSE, ctx, pool);
          if (err)
            goto cleanup;
          anchor_abspath = missing_parent;

          /* If we successfully updated a missing parent, let's re-use
             the returned revision number for future updates for the
             sake of consistency. */
          peg_revision.kind = svn_opt_revision_number;
          peg_revision.value.number = *result_rev;
        }
    }
  else
    {
      SVN_ERR(svn_wc__acquire_write_lock(&lockroot_abspath, ctx->wc_ctx,
                                         local_abspath, !innerupdate,
                                         pool, pool));
      anchor_abspath = lockroot_abspath;
    }

  err = update_internal(result_rev, local_abspath, anchor_abspath,
                        &peg_revision, depth, depth_is_sticky,
                        ignore_externals, allow_unver_obstructions,
                        adds_as_modification,
                        timestamp_sleep, innerupdate, TRUE, ctx, pool);
 cleanup:
  err = svn_error_compose_create(
            err,
            svn_wc__release_write_lock(ctx->wc_ctx, lockroot_abspath, pool));

  return svn_error_return(err);
}


svn_error_t *
svn_client_update4(apr_array_header_t **result_revs,
                   const apr_array_header_t *paths,
                   const svn_opt_revision_t *revision,
                   svn_depth_t depth,
                   svn_boolean_t depth_is_sticky,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t allow_unver_obstructions,
                   svn_boolean_t adds_as_modification,
                   svn_boolean_t make_parents,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  int i;
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *path = NULL;
  svn_boolean_t sleep = FALSE;

  if (result_revs)
    *result_revs = apr_array_make(pool, paths->nelts, sizeof(svn_revnum_t));

  for (i = 0; i < paths->nelts; ++i)
    {
      path = APR_ARRAY_IDX(paths, i, const char *);

      if (svn_path_is_url(path))
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("'%s' is not a local path"), path);
    }

  for (i = 0; i < paths->nelts; ++i)
    {
      svn_error_t *err = SVN_NO_ERROR;
      svn_revnum_t result_rev;
      const char *local_abspath;
      path = APR_ARRAY_IDX(paths, i, const char *);

      svn_pool_clear(subpool);

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, subpool));
      err = svn_client__update_internal(&result_rev, local_abspath,
                                        revision, depth, depth_is_sticky,
                                        ignore_externals,
                                        allow_unver_obstructions,
                                        adds_as_modification,
                                        &sleep, FALSE, make_parents,
                                        ctx, subpool);

      if (err)
        {
          if(err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY)
            return svn_error_return(err);

          svn_error_clear(err);

          /* SVN_ERR_WC_NOT_WORKING_COPY: it's not versioned */
          
          result_rev = SVN_INVALID_REVNUM;
          if (ctx->notify_func2)
            {
              svn_wc_notify_t *notify;
              notify = svn_wc_create_notify(path,
                                            svn_wc_notify_skip,
                                            subpool);
              (*ctx->notify_func2)(ctx->notify_baton2, notify, subpool);
            }
        }
      if (result_revs)
        APR_ARRAY_PUSH(*result_revs, svn_revnum_t) = result_rev;
    }

  svn_pool_destroy(subpool);
  if (sleep)
    svn_io_sleep_for_timestamps((paths->nelts == 1) ? path : NULL, pool);

  return SVN_NO_ERROR;
}
