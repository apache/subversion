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
  const char *propname;
  const svn_string_t *propval = NULL;
  apr_array_header_t *targets;
  int i;
  int num_args_wanted = 2;

  if (opt_state->filedata) {
    svn_stringbuf_t *buf_utf8;
    SVN_ERR (svn_utf_stringbuf_to_utf8 (opt_state->filedata, &buf_utf8, pool));
    propval = svn_string_create_from_buf (buf_utf8, pool);
    num_args_wanted = 1;
  }
  /* PROPNAME and PROPVAL expected as first 2 arguments if filedata
     was NULL */
  SVN_ERR (svn_cl__parse_num_args (os, opt_state,
                                   "propset", num_args_wanted, pool));

  propname  = ((const char **) (opt_state->args->elts))[0];
  if (num_args_wanted == 2)
    {
      const char *buf = ((const char **) (opt_state->args->elts))[1];
      propval = svn_string_create (buf, pool);
    }

  /* suck up all the remaining arguments into a targets array */
  targets = svn_cl__args_to_target_array (os, opt_state, FALSE, pool);

  /* Add "." if user passed 0 file arguments */
  svn_cl__push_implicit_dot_target(targets, pool);

  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = ((const char **) (targets->elts))[i];
      SVN_ERR (svn_client_propset(propname, propval, target,
                                  opt_state->recursive, pool));

      if (! opt_state->quiet) {
        const char *propname_native, *target_native;
        SVN_ERR (svn_utf_cstring_from_utf8 (propname, &propname_native, pool));
        SVN_ERR (svn_utf_cstring_from_utf8 (target, &target_native, pool));
        printf ("property `%s' set%s on '%s'\n",
                propname_native,

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
