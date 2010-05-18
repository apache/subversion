/*
 * cat-cmd.c -- Print the content of a file or URL.
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
#include "cl.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__cat(apr_getopt_t *os,
            void *baton,
            apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  int i;
  svn_stream_t *out;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, pool));

  /* Cat cannot operate on an implicit '.' so a filename is required */
  if (! targets->nelts)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  SVN_ERR(svn_stream_for_stdout(&out, pool));

  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      const char *truepath;
      svn_opt_revision_t peg_revision;

      svn_pool_clear(subpool);
      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      /* Get peg revisions. */
      SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target,
                                 subpool));

      SVN_ERR(svn_cl__try(svn_client_cat2(out, truepath, &peg_revision,
                                          &(opt_state->start_revision),
                                          ctx, subpool),
                           NULL, opt_state->quiet,
                           SVN_ERR_UNVERSIONED_RESOURCE,
                           SVN_ERR_ENTRY_NOT_FOUND,
                           SVN_ERR_CLIENT_IS_DIRECTORY,
                           SVN_NO_ERROR));
    }
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}
