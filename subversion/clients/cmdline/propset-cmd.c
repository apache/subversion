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
#include "cl.h"


/*** Code. ***/

svn_error_t *
svn_cl__propset (apr_getopt_t *os,
                 svn_cl__opt_state_t *opt_state,
                 apr_pool_t *pool)
{
  svn_stringbuf_t *propname;
  const svn_string_t *propval = NULL;
  apr_array_header_t *targets;
  int i;
  int num_args_wanted = 2;

  if (opt_state->filedata) {
    propval = svn_string_create_from_buf (opt_state->filedata, pool);
    num_args_wanted = 1;
  }
  /* PROPNAME and PROPVAL expected as first 2 arguments if filedata
     was NULL */
  SVN_ERR (svn_cl__parse_num_args (os, opt_state,
                                   "propset", num_args_wanted, pool));

  propname  = ((svn_stringbuf_t **) (opt_state->args->elts))[0];
  if (num_args_wanted == 2)
    {
      svn_stringbuf_t *buf = ((svn_stringbuf_t **) (opt_state->args->elts))[1];
      propval = svn_string_create_from_buf (buf, pool);
    }

  /* suck up all the remaining arguments into a targets array */
  targets = svn_cl__args_to_target_array (os, opt_state, pool);

  /* Add "." if user passed 0 file arguments */
  svn_cl__push_implicit_dot_target(targets, pool);

  for (i = 0; i < targets->nelts; i++)
    {
      svn_stringbuf_t *target = ((svn_stringbuf_t **) (targets->elts))[i];
      SVN_ERR (svn_client_propset(propname->data, propval, target->data,
                                  opt_state->recursive, pool));

      if (! opt_state->quiet)
        printf ("property `%s' set %s on '%s'\n",
                propname->data,
                opt_state->recursive ? "(recursively)" : "",
                target->data);
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
