/*
 * diff-cmd.c -- Display context diff of a file
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_types.h"
#include "cl.h"


/*** Code. ***/

/* An svn_cl__cmd_proc_t to handle the 'diff' command. */
svn_error_t *
svn_cl__diff (apr_getopt_t *os,
              svn_cl__opt_state_t *opt_state,
              apr_pool_t *pool)
{
  apr_array_header_t *options;
  apr_array_header_t *targets;
  apr_array_header_t *condensed_targets;
  svn_client_auth_baton_t *auth_baton;
  apr_file_t *outfile, *errfile;
  apr_status_t status;
  int i;

  options = svn_cl__stringlist_to_array (opt_state->extensions, pool);

  targets = svn_cl__args_to_target_array (os, opt_state, TRUE, pool);
  svn_cl__push_implicit_dot_target (targets, pool);
  SVN_ERR (svn_path_remove_redundancies (&condensed_targets,
                                         targets,
                                         pool));

  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  if (opt_state->start_revision.kind == svn_client_revision_unspecified)
    opt_state->start_revision.kind = svn_client_revision_base;

  if (opt_state->end_revision.kind == svn_client_revision_unspecified)
    opt_state->end_revision.kind = svn_client_revision_working;

  /* Get an apr_file_t representing stdout and stderr, which is where
     we'll have the diff program print to. */
  if ((status = apr_file_open_stdout (&outfile, pool)))
    return svn_error_create (status, 0, NULL, pool, "can't open stdout");
  if ((status = apr_file_open_stderr (&errfile, pool)))
    return svn_error_create (status, 0, NULL, pool, "can't open stderr");

  for (i = 0; i < condensed_targets->nelts; ++i)
    {
      svn_stringbuf_t *target
        = ((svn_stringbuf_t **) (condensed_targets->elts))[i];

      SVN_ERR (svn_client_diff (options,
                                auth_baton,
                                target,
                                &(opt_state->start_revision),
                                target,
                                &(opt_state->end_revision),
                                opt_state->nonrecursive ? FALSE : TRUE,
                                outfile,
                                errfile,
                                pool));
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
