/*
 * status-cmd.c -- Display status information in current directory
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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
svn_cl__status (apr_getopt_t *os,
                svn_cl__opt_state_t *opt_state,
                apr_pool_t *pool)
{
  svn_error_t *err;
  apr_hash_t *statushash;
  apr_array_header_t *targets;
  int i;
  svn_client_auth_t *auth_obj;

  targets = svn_cl__args_to_target_array (os, pool);

  /* Build an authentication object to give to libsvn_client. */
  auth_obj = svn_cl__make_auth_obj (opt_state, pool);

  /* Add "." if user passed 0 arguments */
  svn_cl__push_implicit_dot_target(targets, pool);

  for (i = 0; i < targets->nelts; i++)
    {
      svn_stringbuf_t *target = ((svn_stringbuf_t **) (targets->elts))[i];

      /* Recursion is the default, unless the nonrecursive option was
         specified on the command-line. */
      if (opt_state->nonrecursive)
        err = svn_client_status (&statushash, target, 0, auth_obj, pool);
      else
        err = svn_client_status (&statushash, target, 1, auth_obj, pool);

      if (err)
        return err;

      /* Pass the '-M' flag state to this routine. */
      svn_cl__print_status_list (statushash, opt_state->modified, pool);
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end: 
 */
