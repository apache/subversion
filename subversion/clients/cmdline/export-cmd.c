/*
 * export-cmd.c -- Subversion export command
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

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "cl.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__export (apr_getopt_t *os,
                void *baton,
                apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = baton;
  svn_wc_notify_func_t notify_func = NULL;
  void *notify_baton = NULL;
  const char *from, *to;
  apr_array_header_t *targets;

  SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         FALSE, pool));

  /* We want exactly 1 or 2 targets for this subcommand. */
  if ((targets->nelts < 1) || (targets->nelts > 2))
    return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, "");
  
  /* The first target is the `from' path. */
  from = ((const char **) (targets->elts))[0];

  /* If only one target was given, split off the basename to use as
     the `to' path.  Else, a `to' path was supplied. */
  if (targets->nelts == 1) 
    to = svn_path_basename (from, pool);
  else
    to = ((const char **) (targets->elts))[1];

  if (! opt_state->quiet)
    svn_cl__get_notifier (&notify_func, &notify_baton, TRUE, FALSE, pool);

  /* Do the export. */
  SVN_ERR (svn_client_export (from,
                              to,
                              &(opt_state->start_revision),
                              svn_cl__make_auth_baton (opt_state, pool),
                              notify_func,
                              notify_baton,
                              pool));
  return SVN_NO_ERROR;
}
