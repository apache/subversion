/*
 * propdel-cmd.c -- Remove property from files/dirs
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
svn_cl__propdel (apr_getopt_t *os,
                 svn_cl__opt_state_t *opt_state,
                 apr_pool_t *pool)
{
  const char *pname;
  apr_array_header_t *targets;
  int i;

  SVN_ERR (svn_cl__parse_num_args (os, opt_state, "propdel", 1, pool));

  /* Get the property's name. */
  pname = ((const char **) (opt_state->args->elts))[0];

  /* Suck up all the remaining arguments into a targets array */
  targets = svn_cl__args_to_target_array (os, opt_state, FALSE, pool);

  /* Add "." if user passed 0 file arguments */
  svn_cl__push_implicit_dot_target (targets, pool);

  /* For each target, remove the property PNAME. */
  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = ((const char **) (targets->elts))[i];
      SVN_ERR (svn_client_propset (pname, NULL, target,
                                   opt_state->recursive, pool));

      if (! opt_state->quiet)
        printf ("property `%s' deleted %s from '%s'.\n", pname,
                opt_state->recursive ? "(recursively)" : "",
                target);
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
