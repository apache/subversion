/*
 * unlcok-cmd.c -- Unlock a working copy path.
 *
 * ====================================================================
 * Copyright (c) 2004-2007 CollabNet.  All rights reserved.
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
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_cmdline.h"
#include "cl.h"
#include "svn_private_config.h"


/*** Code. ***/


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__unlock(apr_getopt_t *os,
               void *baton,
               apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  apr_array_header_t *changelist_targets = NULL, *combined_targets = NULL;

  /* Before allowing svn_opt_args_to_target_array() to canonicalize
     all the targets, we need to build a list of targets made of both
     ones the user typed, as well as any specified by --changelist.  */
  if (opt_state->changelist)
    {
      SVN_ERR(svn_client_get_changelist(&changelist_targets,
                                        opt_state->changelist,
                                        "",
                                        ctx,
                                        pool));
      if (apr_is_empty_array(changelist_targets))
        return svn_error_createf(SVN_ERR_UNKNOWN_CHANGELIST, NULL,
                                 _("Unknown changelist '%s'"),
                                 opt_state->changelist);
    }

  if (opt_state->targets && changelist_targets)
    combined_targets = apr_array_append(pool, opt_state->targets,
                                        changelist_targets);
  else if (opt_state->targets)
    combined_targets = opt_state->targets;
  else if (changelist_targets)
    combined_targets = changelist_targets;

  SVN_ERR(svn_opt_args_to_target_array2(&targets, os,
                                        combined_targets, pool));

  /* We don't support unlock on directories, so "." is not relevant. */
  if (! targets->nelts)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, FALSE,
                       FALSE, FALSE, pool);

  SVN_ERR(svn_client_unlock(targets, opt_state->force, ctx, pool));

  return SVN_NO_ERROR;
}
