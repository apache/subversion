/*
 * patch-cmd.c -- Apply changes to a working copy.
 *
 * ====================================================================
 * Copyright (c) 2007-2008 CollabNet.  All rights reserved.
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
#include "svn_path.h"
#include "svn_error.h"
#include "svn_types.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__patch(apr_getopt_t *os,
              void *baton,
              apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *args, *targets;
  const char *patch_path = NULL, *target_path = NULL;
  apr_file_t *outfile, *errfile;
  apr_status_t status;
  svn_error_t *err;

  /* Sanity checks */

  /* Against the patch argument */
  {
    svn_node_kind_t patch_path_kind;

    SVN_ERR(svn_opt_parse_num_args(&args, os, 1, pool));
    SVN_ERR(svn_path_get_absolute(&patch_path,
                                  APR_ARRAY_IDX(args, 0, const char *),
                                  pool));
    SVN_ERR(svn_io_check_path(patch_path, &patch_path_kind, pool));
    if (patch_path_kind == svn_node_none)
      return svn_error_createf
              (APR_ENOENT, NULL, _("'%s' does not exist"),
              svn_path_local_style(patch_path, pool));
  }

  /* Against the WCPATH argument */
  {
    int wcformat;

    SVN_ERR(svn_client_args_to_target_array(&targets, os, opt_state->targets,
                                            ctx, pool));

    /* Should we ignore extra arguments?  Let's consider it as a misuse
     * for now, the user might miss something. */
    if (targets->nelts > 1)
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

    svn_opt_push_implicit_dot_target(targets, pool);

    target_path = APR_ARRAY_IDX(targets, 0, const char *);

    /* Ensure we're given a working copy path. */
    SVN_ERR(svn_wc_check_wc(target_path, &wcformat, pool));
    if (! wcformat)
      return svn_error_create(SVN_ERR_WC_NOT_DIRECTORY, 0, NULL);
  }

  if (! opt_state->quiet)
    svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, FALSE,
                         FALSE, FALSE, pool);

  
  /* stdout and stderr pipes we'll link upon call to the external program
   * (e.g. GNU patch) used to apply the unidiff. */
  if ((status = apr_file_open_stdout(&outfile, pool)))
    return svn_error_wrap_apr(status, _("Can't open stdout"));
  if ((status = apr_file_open_stderr(&errfile, pool)))
    return svn_error_wrap_apr(status, _("Can't open stderr"));

  /* OK we're good. */
  err = svn_client_patch(patch_path,
                         target_path,
                         opt_state->force,
                         outfile,
                         errfile,
                         ctx,
                         pool);

  if (err)
    {
      svn_error_t *root_err = svn_error_root_cause(err);
      if (root_err->apr_err == SVN_ERR_EXTERNAL_PROGRAM_MISSING)
        return svn_error_quick_wrap
                (err,
                 _("No 'patch' program was found in your system.  Please try\n"
                   "to use --patch-cmd or 'patch-cmd' run-time configuration\n"
                   "option or manually use an external tool to apply Unidiffs."));
      else
        return err;
    }

  return SVN_NO_ERROR;
}
