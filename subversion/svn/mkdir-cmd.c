/*
 * mkdir-cmd.c -- Subversion mkdir command
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

#include "svn_pools.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_path.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__mkdir(apr_getopt_t *os,
              void *baton,
              apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;

  SVN_ERR(svn_opt_args_to_target_array2(&targets, os, 
                                        opt_state->targets, pool));

  if (! targets->nelts)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  if (! opt_state->quiet)
    svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, FALSE,
                         FALSE, FALSE, pool);

  if (! svn_path_is_url(APR_ARRAY_IDX(targets, 0, const char *)))
    {
      ctx->log_msg_func2 = NULL;
      if (opt_state->message || opt_state->filedata)
        {
          return svn_error_create
            (SVN_ERR_CL_UNNECESSARY_LOG_MESSAGE, NULL,
             _("Local, non-commit operations do not take a log message"));
        }
    }
  else
    {
      SVN_ERR(svn_cl__make_log_msg_baton(&(ctx->log_msg_baton2), opt_state,
                                         NULL, ctx->config, subpool));
    }

  err = svn_client_mkdir2(&commit_info, targets, ctx, subpool);

  if (ctx->log_msg_func2)
    err = svn_cl__cleanup_log_msg(ctx->log_msg_baton2, err);

  if (err)
    {
      if (err->apr_err == APR_EEXIST)
        return svn_error_quick_wrap
          (err, _("Try 'svn add' or 'svn add --non-recursive' instead?"));
      else
        return err;
    }

  if (commit_info && ! opt_state->quiet)
    SVN_ERR(svn_cl__print_commit_info(commit_info, subpool));

  return SVN_NO_ERROR;
}
