/*
 * export-cmd.c -- Subversion export command
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
#include "svn_error.h"
#include "svn_path.h"
#include "cl.h"


/*** Code. ***/

svn_error_t *
svn_cl__export (apr_getopt_t *os,
                svn_cl__opt_state_t *opt_state,
                apr_pool_t *pool)
{
  svn_client_auth_baton_t *auth_baton;
  svn_wc_notify_func_t notify_func = NULL;
  void *notify_baton = NULL;
  const char *from, *to;
 
  SVN_ERR (svn_cl__parse_all_args (os, opt_state, "export", pool));

  /* Put commandline auth info into a baton for libsvn_client.  */
  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  if (opt_state->args->nelts == 1) 
    svn_path_split_nts(((const char **) (opt_state->args->elts))[0], NULL, &to, pool);
  else if (opt_state->args->nelts == 2) 
    to = ((const char **) (opt_state->args->elts))[1];
  else
    return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, 0, pool, "");

  from = ((const char **) (opt_state->args->elts))[0];

  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  if (! opt_state->quiet)
    svn_cl__get_notifier (&notify_func, &notify_baton, TRUE, FALSE, pool);

  SVN_ERR (svn_client_export (from,
                              to,
                              &(opt_state->start_revision),
                              auth_baton,
                              notify_func,
                              notify_baton,
                              pool));
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
