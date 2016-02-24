/*
 * resolve-cmd.c -- Subversion resolve subcommand
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
#include "svn_path.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_hash.h"
#include "cl.h"

#include "svn_private_config.h"



/*** Code. ***/

/* Baton for conflict_status_walker */
struct conflict_status_walker_baton
{
  svn_client_ctx_t *ctx;
  svn_client_conflict_option_id_t option_id;
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
  svn_boolean_t resolved_one;
  apr_hash_t *resolve_later;
  svn_cl__accept_t *accept_which;
  svn_boolean_t *quit;
  svn_boolean_t *external_failed;
  svn_boolean_t *printed_summary;
  const char *editor_cmd;
  apr_hash_t *config;
  const char *path_prefix;
  svn_cmdline_prompt_baton_t *pb;
  svn_cl__conflict_stats_t *conflict_stats;
};

/* Implements svn_wc_notify_func2_t to collect new conflicts caused by
   resolving a tree conflict. */
static void
tree_conflict_collector(void *baton,
                        const svn_wc_notify_t *notify,
                        apr_pool_t *pool)
{
  struct conflict_status_walker_baton *cswb = baton;

  if (cswb->notify_func)
    cswb->notify_func(cswb->notify_baton, notify, pool);

  if (cswb->resolve_later
      && (notify->action == svn_wc_notify_tree_conflict
          || notify->prop_state == svn_wc_notify_state_conflicted
          || notify->content_state == svn_wc_notify_state_conflicted))
    {
      if (!svn_hash_gets(cswb->resolve_later, notify->path))
        {
          const char *dup_path;

          dup_path = apr_pstrdup(apr_hash_pool_get(cswb->resolve_later),
                                 notify->path);

          svn_hash_sets(cswb->resolve_later, dup_path, dup_path);
        }
    }
}

/* 
 * Record a tree conflict resolution failure due to error condition ERR
 * in the RESOLVE_LATER hash table. If the hash table is not available
 * (meaning the caller does not wish to retry resolution later), or if
 * the error condition does not indicate circumstances where another
 * existing tree conflict is blocking the resolution attempt, then
 * return the error ERR itself.
 */
static svn_error_t *
handle_tree_conflict_resolution_failure(const char *local_abspath,
                                        svn_error_t *err,
                                        apr_hash_t *resolve_later)
{
  const char *dup_abspath;

  if (!resolve_later
      || (err->apr_err != SVN_ERR_WC_OBSTRUCTED_UPDATE
          && err->apr_err != SVN_ERR_WC_FOUND_CONFLICT))
    return svn_error_trace(err); /* Give up. Do not retry resolution later. */

  svn_error_clear(err);
  dup_abspath = apr_pstrdup(apr_hash_pool_get(resolve_later),
                            local_abspath);

  svn_hash_sets(resolve_later, dup_abspath, dup_abspath);

  return SVN_NO_ERROR; /* Caller may retry after resolving other conflicts. */
}

/* Implements svn_wc_status4_t to walk all conflicts to resolve.
 */
static svn_error_t *
conflict_status_walker(void *baton,
                       const char *local_abspath,
                       const svn_wc_status3_t *status,
                       apr_pool_t *scratch_pool)
{
  struct conflict_status_walker_baton *cswb = baton;
  apr_pool_t *iterpool;
  svn_boolean_t resolved = FALSE;
  svn_client_conflict_t *conflict;
  svn_error_t *err;
  svn_boolean_t tree_conflicted;

  if (!status->conflicted)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_client_conflict_get(&conflict, local_abspath, cswb->ctx,
                                  iterpool, iterpool));
  SVN_ERR(svn_client_conflict_get_conflicted(NULL, NULL, &tree_conflicted,
                                             conflict, iterpool, iterpool));
  err = svn_cl__resolve_conflict(&resolved, cswb->accept_which,
                                 cswb->quit, cswb->external_failed,
                                 cswb->printed_summary,
                                 conflict, cswb->editor_cmd,
                                 cswb->config, cswb->path_prefix,
                                 cswb->pb, cswb->conflict_stats,
                                 cswb->option_id, cswb->ctx,
                                 scratch_pool);
  if (err)
    {
      if (tree_conflicted)
        SVN_ERR(handle_tree_conflict_resolution_failure(local_abspath, err,
                                                        cswb->resolve_later));

      else
        return svn_error_trace(err);
    }

  if (resolved)
    cswb->resolved_one = TRUE;

  svn_pool_destroy(iterpool);

  /* If the has user decided to quit resolution, cancel the status walk. */
  if (*cswb->quit)
    return svn_error_create(SVN_ERR_CANCELLED, NULL, NULL);

  return SVN_NO_ERROR;
}

static svn_error_t *
walk_conflicts(svn_client_ctx_t *ctx,
               const char *local_abspath,
               svn_depth_t depth,
               svn_client_conflict_option_id_t option_id,
               svn_cl__accept_t *accept_which,
               svn_boolean_t *quit,
               svn_boolean_t *external_failed,
               svn_boolean_t *printed_summary,
               const char *editor_cmd,
               apr_hash_t *config,
               const char *path_prefix,
               svn_cmdline_prompt_baton_t *pb,
               svn_cl__conflict_stats_t *conflict_stats,
               apr_pool_t *scratch_pool)
{
  struct conflict_status_walker_baton cswb;
  apr_pool_t *iterpool = NULL;
  svn_error_t *err;

  if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  cswb.ctx = ctx;
  cswb.option_id = option_id;

  cswb.resolved_one = FALSE;
  cswb.resolve_later = (depth != svn_depth_empty)
                          ? apr_hash_make(scratch_pool)
                          : NULL;

  cswb.accept_which = accept_which;
  cswb.quit = quit;
  cswb.external_failed = external_failed;
  cswb.printed_summary = printed_summary;
  cswb.editor_cmd = editor_cmd;
  cswb.config = config;
  cswb.path_prefix = path_prefix;
  cswb.pb = pb;
  cswb.conflict_stats = conflict_stats;


  /* ### call notify.c code */
  if (ctx->notify_func2)
    ctx->notify_func2(ctx->notify_baton2,
                      svn_wc_create_notify(
                        local_abspath,
                        svn_wc_notify_conflict_resolver_starting,
                        scratch_pool),
                      scratch_pool);

  cswb.notify_func = ctx->notify_func2;
  cswb.notify_baton = ctx->notify_baton2;
  ctx->notify_func2 = tree_conflict_collector;
  ctx->notify_baton2 = &cswb;

  err = svn_wc_walk_status(ctx->wc_ctx,
                           local_abspath,
                           depth,
                           FALSE /* get_all */,
                           FALSE /* no_ignore */,
                           TRUE /* ignore_text_mods */,
                           NULL /* ignore_patterns */,
                           conflict_status_walker, &cswb,
                           ctx->cancel_func, ctx->cancel_baton,
                           scratch_pool);

  /* If we got new tree conflicts (or delayed conflicts) during the initial
     walk, we now walk them one by one as closure. */
  while (!err && cswb.resolve_later && apr_hash_count(cswb.resolve_later))
    {
      apr_hash_index_t *hi;
      svn_wc_status3_t *status = NULL;
      const char *tc_abspath = NULL;

      if (iterpool)
        svn_pool_clear(iterpool);
      else
        iterpool = svn_pool_create(scratch_pool);

      hi = apr_hash_first(scratch_pool, cswb.resolve_later);
      cswb.resolve_later = apr_hash_make(scratch_pool);
      cswb.resolved_one = FALSE;

      for (; hi && !err; hi = apr_hash_next(hi))
        {
          const char *relpath;
          svn_pool_clear(iterpool);

          tc_abspath = apr_hash_this_key(hi);

          if (ctx->cancel_func)
            SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

          relpath = svn_dirent_skip_ancestor(local_abspath,
                                             tc_abspath);

          if (!relpath
              || (depth >= svn_depth_empty
                  && depth < svn_depth_infinity
                  && strchr(relpath, '/')))
            {
              continue;
            }

          SVN_ERR(svn_wc_status3(&status, ctx->wc_ctx, tc_abspath,
                                 iterpool, iterpool));

          if (depth == svn_depth_files
              && status->kind == svn_node_dir)
            continue;

          err = svn_error_trace(conflict_status_walker(&cswb, tc_abspath,
                                                       status, scratch_pool));
        }

      /* None of the remaining conflicts got resolved, and non did provide
         an error...

         We can fix that if we disable the 'resolve_later' option...
       */
      if (!cswb.resolved_one && !err && tc_abspath
          && apr_hash_count(cswb.resolve_later))
        {
          /* Run the last resolve operation again. We still have status
             and tc_abspath for that one. */

          cswb.resolve_later = NULL; /* Produce proper error! */

          /* Recreate the error */
          err = svn_error_trace(conflict_status_walker(&cswb, tc_abspath,
                                                       status, scratch_pool));

          SVN_ERR_ASSERT(err != NULL);

          err = svn_error_createf(
                    SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, err,
                    _("Unable to resolve pending conflict on '%s'"),
                    svn_dirent_local_style(tc_abspath, scratch_pool));
          break;
        }
    }

  if (iterpool)
    svn_pool_destroy(iterpool);

  if (err)
    {
      if (err->apr_err == SVN_ERR_CANCELLED)
        {
          /* If QUIT is set, the user has selected the 'q' option at
           * the conflict prompt and the status walk was aborted.
           * This is not an error condition. */
          if (quit)
            {
              svn_error_clear(err);
              err = SVN_NO_ERROR;
            }
        }
      else if (err->apr_err != SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE)
        err = svn_error_createf(
                    SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, err,
                    _("Unable to resolve conflicts on '%s'"),
                    svn_dirent_local_style(local_abspath, scratch_pool));

      SVN_ERR(err);
    }

  ctx->notify_func2 = cswb.notify_func;
  ctx->notify_baton2 = cswb.notify_baton;

  /* ### call notify.c code */
  if (ctx->notify_func2)
    ctx->notify_func2(ctx->notify_baton2,
                      svn_wc_create_notify(local_abspath,
                                          svn_wc_notify_conflict_resolver_done,
                                          scratch_pool),
                      scratch_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_cl__walk_conflicts(apr_array_header_t *targets,
                       svn_cl__conflict_stats_t *conflict_stats,
                       svn_boolean_t is_resolve_cmd,
                       svn_cl__opt_state_t *opt_state,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *scratch_pool)
{
  svn_boolean_t had_error = FALSE;
  svn_boolean_t quit = FALSE;
  svn_boolean_t external_failed = FALSE;
  svn_boolean_t printed_summary = FALSE;
  svn_client_conflict_option_id_t option_id;
  svn_cmdline_prompt_baton_t *pb = apr_palloc(scratch_pool, sizeof(*pb));
  const char *path_prefix;
  svn_error_t *err;
  int i;
  apr_pool_t *iterpool;

  SVN_ERR(svn_dirent_get_absolute(&path_prefix, "", scratch_pool));

  pb->cancel_func = ctx->cancel_func;
  pb->cancel_baton = ctx->cancel_baton;

  switch (opt_state->accept_which)
    {
    case svn_cl__accept_working:
      option_id = svn_client_conflict_option_merged_text;
      break;
    case svn_cl__accept_base:
      option_id = svn_client_conflict_option_base_text;
      break;
    case svn_cl__accept_theirs_conflict:
      option_id = svn_client_conflict_option_incoming_text_where_conflicted;
      break;
    case svn_cl__accept_mine_conflict:
      option_id = svn_client_conflict_option_working_text_where_conflicted;
      break;
    case svn_cl__accept_theirs_full:
      option_id = svn_client_conflict_option_incoming_text;
      break;
    case svn_cl__accept_mine_full:
      option_id = svn_client_conflict_option_working_text;
      break;
    case svn_cl__accept_unspecified:
      if (is_resolve_cmd && opt_state->non_interactive)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("missing --accept option"));
      option_id = svn_client_conflict_option_unspecified;
      break;
    case svn_cl__accept_postpone:
      if (is_resolve_cmd)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("invalid 'accept' ARG"));
      option_id = svn_client_conflict_option_postpone;
      break;
    case svn_cl__accept_edit:
    case svn_cl__accept_launch:
      if (is_resolve_cmd)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("invalid 'accept' ARG"));
      option_id = svn_client_conflict_option_unspecified;
      break;
    default:
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("invalid 'accept' ARG"));
    }


  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      const char *local_abspath;
      svn_client_conflict_t *conflict;

      svn_pool_clear(iterpool);
 
      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      SVN_ERR(svn_dirent_get_absolute(&local_abspath, target, iterpool));

      if (opt_state->depth == svn_depth_empty)
        {
          svn_boolean_t resolved;

          SVN_ERR(svn_client_conflict_get(&conflict, local_abspath, ctx,
                                          iterpool, iterpool));
          err = svn_cl__resolve_conflict(&resolved,
                                         &opt_state->accept_which,
                                         &quit, &external_failed,
                                         &printed_summary,
                                         conflict, opt_state->editor_cmd,
                                         ctx->config, path_prefix,
                                         pb, conflict_stats,
                                         option_id, ctx,
                                         iterpool);
        }
      else
        {
          err = walk_conflicts(ctx, local_abspath, opt_state->depth,
                               option_id, &opt_state->accept_which,
                               &quit, &external_failed, &printed_summary,
                               opt_state->editor_cmd, ctx->config,
                               path_prefix, pb, conflict_stats, iterpool);
        }

      if (err)
        {
          svn_handle_warning2(stderr, err, "svn: ");
          svn_error_clear(err);
          had_error = TRUE;
        }
    }
  svn_pool_destroy(iterpool);

  if (had_error)
    return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                            _("Failure occurred resolving one or more "
                              "conflicts"));
  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__resolve(apr_getopt_t *os,
                void *baton,
                apr_pool_t *scratch_pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_cl__conflict_stats_t *conflict_stats =
    ((svn_cl__cmd_baton_t *) baton)->conflict_stats;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE,
                                                      scratch_pool));
  if (! targets->nelts)
    svn_opt_push_implicit_dot_target(targets, scratch_pool);

  if (opt_state->depth == svn_depth_unknown)
    {
      if (opt_state->accept_which == svn_cl__accept_unspecified)
        opt_state->depth = svn_depth_infinity;
      else
        opt_state->depth = svn_depth_empty;
    }

  SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, scratch_pool));

  SVN_ERR(svn_cl__check_targets_are_local_paths(targets));

  SVN_ERR(svn_cl__walk_conflicts(targets, conflict_stats, TRUE,
                                 opt_state, ctx, scratch_pool));

  return SVN_NO_ERROR;
}
