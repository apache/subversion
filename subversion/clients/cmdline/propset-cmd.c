/*
 * propset-cmd.c -- Display status information in current directory
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
svn_cl__propset (apr_getopt_t *os,
                 svn_cl__opt_state_t *opt_state,
                 apr_pool_t *pool)
{
  const char *pname, *pname_utf8;
  const svn_string_t *propval = NULL;
  apr_array_header_t *args, *targets;
  int i;

  /* PNAME and PROPVAL expected as first 2 arguments if filedata was
     NULL, else PNAME alone will precede the targets.  Get a UTF-8
     version of the name, too. */
  SVN_ERR (svn_cl__parse_num_args (&args, os,
                                   opt_state->filedata ? 1 : 2, pool));
  pname = ((const char **) (args->elts))[0];
  SVN_ERR (svn_utf_cstring_to_utf8 (&pname_utf8, pname, NULL, pool));

  /* Get the PROPVAL from either an external file, or from the command
     line. */
  if (opt_state->filedata) 
    propval = svn_string_create_from_buf (opt_state->filedata, pool);
  else
    propval = svn_string_create (((const char **) (args->elts))[1], pool);
  
  /* We only want special Subversion properties to be in UTF-8.  All
     others should remain in binary format.  ### todo: make this
     happen. */
  if (svn_prop_is_svn_prop (pname_utf8))
    SVN_ERR (svn_utf_string_to_utf8 (&propval, propval, pool));
      
  /* Suck up all the remaining arguments into a targets array */
  SVN_ERR (svn_cl__args_to_target_array (&targets, os, opt_state, 
                                         FALSE, pool));

  /* Add "." if user passed 0 file arguments */
  svn_cl__push_implicit_dot_target (targets, pool);

  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = ((const char **) (targets->elts))[i];
      SVN_ERR (svn_client_propset (pname_utf8, propval, target,
                                   opt_state->recursive, pool));

      if (! opt_state->quiet) 
        {
          const char *target_native;
          SVN_ERR (svn_utf_cstring_from_utf8 (&target_native, target, pool));
          printf ("property `%s' set%s on '%s'\n",
                  pname, 
                  opt_state->recursive ? " (recursively)" : "",
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
