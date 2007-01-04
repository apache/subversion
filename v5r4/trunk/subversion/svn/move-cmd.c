/*
 * move-cmd.c -- Subversion move command
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include "svn_path.h"
#include "cl.h"

#include "svn_private_config.h"



/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__move(apr_getopt_t *os,
             void *baton,
             apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  const char *src_path, *dst_path;
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;

  SVN_ERR(svn_opt_args_to_target_array2(&targets, os, 
                                        opt_state->targets, pool));

  if (targets->nelts < 2)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
  if (targets->nelts > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  src_path = ((const char **) (targets->elts))[0];
  dst_path = ((const char **) (targets->elts))[1];
  
  if (! opt_state->quiet)
    svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, FALSE,
                         FALSE, FALSE, pool);

  SVN_ERR(svn_cl__make_log_msg_baton(&(ctx->log_msg_baton2), opt_state,
                                     NULL, ctx->config, pool));

  if (opt_state->start_revision.kind != svn_opt_revision_unspecified
      && opt_state->start_revision.kind != svn_opt_revision_head)
    {
      return svn_error_create
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         _("Cannot specify revisions (except HEAD) with move operations"));
    }

  err = svn_client_move4(&commit_info, src_path, dst_path,
                         opt_state->force, ctx, pool);

  /* If dst_path already exists, try to move src_path as a child of it. */
  if (err && (err->apr_err == SVN_ERR_ENTRY_EXISTS
              || err->apr_err == SVN_ERR_FS_ALREADY_EXISTS))
    {
      const char *src_basename = svn_path_basename(src_path, pool);

      svn_error_clear(err);
      
      err = svn_client_move4(&commit_info, src_path,
                             svn_path_join(dst_path, src_basename, pool),
                             opt_state->force, ctx, pool);

    }

  if (err)
    err = svn_cl__may_need_force(err);
  SVN_ERR(svn_cl__cleanup_log_msg(ctx->log_msg_baton2, err));

  if (commit_info && ! opt_state->quiet)
    SVN_ERR(svn_cl__print_commit_info(commit_info, pool));

  return SVN_NO_ERROR;
}
