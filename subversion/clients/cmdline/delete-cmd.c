/*
 * delete-cmd.c -- Delete/undelete commands
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
#include "cl.h"



/*** Code. ***/

svn_error_t *
svn_cl__delete (apr_getopt_t *os,
                svn_cl__opt_state_t *opt_state,
                apr_pool_t *pool)
{
  apr_array_header_t *targets;
  svn_client_auth_baton_t *auth_baton = NULL;
  int i;
  svn_client_commit_info_t *commit_info = NULL;

  targets = svn_cl__args_to_target_array (os, opt_state, FALSE, pool);

  /* Build an authentication object to give to libsvn_client. */
  auth_baton = svn_cl__make_auth_baton (opt_state, pool);
            
  if (targets->nelts)
    for (i = 0; i < targets->nelts; i++)
      {
        svn_stringbuf_t *target = ((svn_stringbuf_t **) (targets->elts))[i];
        commit_info = NULL;
        SVN_ERR (svn_client_delete
                 (&commit_info, target, opt_state->force, 
                  auth_baton, 
                  &svn_cl__get_log_message,
                  svn_cl__make_log_msg_baton (opt_state, NULL, pool),
                  SVN_CL_NOTIFY(opt_state), 
                  svn_cl__make_notify_baton (pool),
                  pool));
        if (commit_info)
          svn_cl__print_commit_info (commit_info);
    }
  else
    {
      svn_cl__subcommand_help ("delete", pool);
      return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, 0, pool, "");
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
