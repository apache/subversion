/*
 * diff-cmd.c -- Display context diff of a file
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
#include "svn_types.h"
#include "cl.h"


/*** Code. ***/

svn_error_t *
svn_cl__diff (apr_getopt_t *os,
              svn_cl__opt_state_t *opt_state,
              apr_pool_t *pool)
{
  svn_error_t *err;
  apr_array_header_t *targets;
  svn_boolean_t recurse = TRUE;
  int i;

  targets = svn_cl__args_to_target_array (os, pool);

  /* Add "." if user passed 0 arguments */
  svn_cl__push_implicit_dot_target(targets, pool);

  /* Check whether the user specified no recursion. */
  if (opt_state->nonrecursive)
    {
      recurse = FALSE;
    }

  for (i = 0; i < targets->nelts; i++)
    {
      svn_stringbuf_t *target = ((svn_stringbuf_t **) (targets->elts))[i];
      enum svn_node_kind kind;

      SVN_ERR (svn_io_check_path (target, &kind, pool));

      switch (kind) 
        {
        case svn_node_file:
          err = svn_cl__print_file_diff (target, pool);
          break;
        case svn_node_dir:
          err = svn_cl__print_dir_diff (target, recurse, pool);
          break;
        case svn_node_unknown:
          err = svn_error_createf (0, 0, NULL, pool,
                                  "File type unrecognized for target `%s'.",
                                  target->data);
          break;                                  
        case svn_node_none:
          err = svn_error_createf (APR_ENOENT, 0, NULL, pool, 
                                   "Target `%s' not found.", target->data);
          break;
        }

      if (err) return err;
    }
  
  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end: 
 */
