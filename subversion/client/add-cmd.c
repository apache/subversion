/*
 * add-cmd.c -- Subversion add/unadd commands
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
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
svn_cl__add (apr_getopt_t *os,
             svn_cl__opt_state_t *opt_state,
             apr_pool_t *pool)
{
  svn_error_t *err;
  apr_array_header_t *targets;
  int i;
  svn_boolean_t recursive = opt_state->recursive;

  targets = svn_cl__args_to_target_array (os, pool);

  if (targets->nelts)
    for (i = 0; i < targets->nelts; i++)
      {
        svn_string_t *target = ((svn_string_t **) (targets->elts))[i];
        err = svn_client_add (target, recursive, pool);
        if (err)
          {
            if (err->apr_err == SVN_ERR_WC_ENTRY_EXISTS)
              svn_handle_warning(err, err->message);
            else
              return err;
          }
      }
  else
    {
      svn_cl__subcommand_help ("add", pool);
      return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, 0, pool, "");
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__unadd (apr_getopt_t *os,
               svn_cl__opt_state_t *opt_state,
               apr_pool_t *pool)
{
  apr_array_header_t *targets;
  int i;

  targets = svn_cl__args_to_target_array (os, pool);

  if (targets->nelts)
    for (i = 0; i < targets->nelts; i++)
      {
        svn_string_t *target = ((svn_string_t **) (targets->elts))[i];
        SVN_ERR (svn_client_unadd (target, pool));
      }
  else
    {
      svn_cl__subcommand_help ("unadd", pool);
      return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, 0, pool, "");
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
