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
#include "svn_utf.h"
#include "cl.h"


/*** Code. ***/

svn_error_t *
svn_cl__propdel (apr_getopt_t *os,
                 svn_cl__opt_state_t *opt_state,
                 apr_pool_t *pool)
{
  const char *pname, *pname_utf8;
  apr_array_header_t *args, *targets;
  int i;

  /* Get the property's name (and a UTF-8 version of that name). */
  SVN_ERR (svn_cl__parse_num_args (&args, os, opt_state, "propdel", 1, pool));
  pname = ((const char **) (args->elts))[0];
  SVN_ERR (svn_utf_cstring_to_utf8 (pname, &pname_utf8, pool));

  /* Suck up all the remaining arguments into a targets array */
  SVN_ERR (svn_cl__args_to_target_array (&targets, os, opt_state, 
                                         FALSE, pool));

  /* Add "." if user passed 0 file arguments */
  svn_cl__push_implicit_dot_target (targets, pool);

  /* For each target, remove the property PNAME. */
  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = ((const char **) (targets->elts))[i];
      SVN_ERR (svn_client_propset (pname_utf8, NULL, target,
                                   opt_state->recursive, pool));
      if (! opt_state->quiet) 
        {
          const char *target_native;
          SVN_ERR (svn_utf_cstring_from_utf8 (target, &target_native, pool));
          printf ("property `%s' deleted%sfrom '%s'.\n", pname,
                  opt_state->recursive ? " (recursively) " : " ",
                  target_native);
        }
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
