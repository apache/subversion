/*
 * propget-cmd.c -- Display status information in current directory
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
svn_cl__propget (apr_getopt_t *os,
                 svn_cl__opt_state_t *opt_state,
                 apr_pool_t *pool)
{
  const char *propname;
  apr_array_header_t *targets;
  int i;

  /* PROPNAME is first argument */
  SVN_ERR (svn_cl__parse_num_args (os, opt_state,
                                   "propget", 1, pool));

  propname = ((const char **) (opt_state->args->elts))[0];

  /* suck up all the remaining arguments into a targets array */
  targets = svn_cl__args_to_target_array (os, opt_state, FALSE, pool);

  /* Add "." if user passed 0 file arguments */
  svn_cl__push_implicit_dot_target(targets, pool);

  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = ((const char **) (targets->elts))[i];
      apr_hash_t *props;
      apr_hash_index_t *hi;
      svn_boolean_t print_filenames = FALSE;

      SVN_ERR (svn_client_propget (&props, propname, target,
                                   opt_state->recursive, pool));

      print_filenames = (targets->nelts > 1 || apr_hash_count(props) > 1);

      for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
        {
          const char * filename; 
          const svn_string_t *propval;
          apr_hash_this(hi, (const void **)&filename, NULL, (void **)&propval);

          /* ### this won't handle binary property values */
          if (print_filenames)
            printf ("%s - %s\n", filename, propval->data);
          else
            printf ("%s\n", propval->data);
        }
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
