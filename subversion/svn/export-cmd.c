/*
 * export-cmd.c -- Subversion export command
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

#include "svn_client.h"
#include "svn_error.h"
#include "svn_path.h"
#include "cl.h"

#include "svn_private_config.h"
#include "private/svn_opt_private.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__export(apr_getopt_t *os,
               void *baton,
               apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *from, *to;
  apr_array_header_t *targets;
  svn_error_t *err;
  svn_opt_revision_t peg_revision;
  const char *truefrom;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, pool));

  /* We want exactly 1 or 2 targets for this subcommand. */
  if (targets->nelts < 1)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
  if (targets->nelts > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  /* The first target is the `from' path. */
  from = APR_ARRAY_IDX(targets, 0, const char *);

  /* Get the peg revision if present. */
  SVN_ERR(svn_opt_parse_path(&peg_revision, &truefrom, from, pool));

  /* If only one target was given, split off the basename to use as
     the `to' path.  Else, a `to' path was supplied. */
  if (targets->nelts == 1)
    to = svn_path_uri_decode(svn_path_basename(truefrom, pool), pool);
  else
    to = APR_ARRAY_IDX(targets, 1, const char *);

  SVN_ERR(svn_opt__split_arg_at_peg_revision(&to, NULL, to, pool));

  if (! opt_state->quiet)
    svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, FALSE, TRUE,
                         FALSE, pool);

  if (opt_state->depth == svn_depth_unknown)
    opt_state->depth = svn_depth_infinity;

  /* Decode the partially encoded URL and escape all URL unsafe characters. */
  if (svn_path_is_url(truefrom))
    truefrom = svn_path_uri_encode(svn_path_uri_decode(truefrom, pool), pool);

  /* Do the export. */
  err = svn_client_export4(NULL, truefrom, to, &peg_revision,
                           &(opt_state->start_revision),
                           opt_state->force, opt_state->ignore_externals,
                           opt_state->depth,
                           opt_state->native_eol, ctx,
                           pool);
  if (err && err->apr_err == SVN_ERR_WC_OBSTRUCTED_UPDATE && !opt_state->force)
    SVN_ERR_W(err,
              _("Destination directory exists; please remove "
                "the directory or use --force to overwrite"));

  return err;
}
