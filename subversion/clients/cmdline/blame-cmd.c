/*
 * blame-cmd.c -- Display blame information
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


/*** Includes. ***/
#include "svn_client.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_cmdline.h"
#include "cl.h"


/*** Code. ***/
static svn_error_t *
blame_receiver (void *baton,
                apr_off_t line_no,
                svn_revnum_t revision,
                const char *author,
                const char *date,
                const char *line,
                apr_pool_t *pool)
{
  svn_stream_t *out = baton;
  return svn_stream_printf (out, pool, "%6"SVN_REVNUM_T_FMT" %10s %s\n",
                            revision, author, line);
}
 

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__blame (apr_getopt_t *os,
               void *baton,
               apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_pool_t *subpool;
  apr_array_header_t *targets;
  svn_stream_t *out;
  int i;

  SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                         opt_state->targets,
                                         &opt_state->start_revision,
                                         &opt_state->end_revision,
                                         FALSE, pool));

  /* Blame needs a file on which to operate. */
  if (! targets->nelts)
    return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, "");

  if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
    {
      opt_state->start_revision.value.number = 1;
    }

  if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
    {
      opt_state->end_revision.kind = svn_opt_revision_head;
    }


  SVN_ERR (svn_stream_for_stdout (&out, pool));

  subpool = svn_pool_create (pool);

  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = ((const char **) (targets->elts))[i];
      svn_pool_clear (subpool);
      SVN_ERR (svn_client_blame (target,
                                 &opt_state->start_revision,
                                 &opt_state->end_revision,
                                 opt_state->strict,
                                 blame_receiver,
                                 out,
                                 ctx,
                                 subpool));
      SVN_ERR (svn_cl__check_cancel (ctx->cancel_baton));
    }
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}
