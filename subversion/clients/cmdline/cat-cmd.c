/*
 * cat-cmd.c -- Print the content of a file or URL.
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "cl.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__cat (apr_getopt_t *os,
             void *baton,
             apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  int i;
  apr_file_t *std_out;
  apr_status_t status;

  if (opt_state->end_revision.kind != svn_opt_revision_unspecified)
    return svn_error_createf (SVN_ERR_CLIENT_REVISION_RANGE, NULL,
                              "cat only accepts a single revision");

  SVN_ERR (svn_opt_args_to_target_array (&targets, os,
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         FALSE, pool));

  /* Cat cannot operate on an implicit '.' so a filename is required */
  if (! targets->nelts)
    return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, "");

  status = apr_file_open_stdout (&std_out, pool);
  if (!APR_STATUS_IS_SUCCESS (status))
    return svn_error_create (status, NULL, "Error opening stdout.");

  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = ((const char **) (targets->elts))[i];
      svn_stream_t *out = svn_stream_from_aprfile (std_out, pool);
      const char *URL;

      SVN_ERR (svn_cl__get_url_from_target (&URL, target, pool));
      if (! URL)
        return svn_error_createf (SVN_ERR_ENTRY_MISSING_URL, NULL,
                                  "'%s' has no URL", target);

      SVN_ERR (svn_client_cat (out, URL, &(opt_state->start_revision),
                               ctx, pool));
    }

  return SVN_NO_ERROR;
}
