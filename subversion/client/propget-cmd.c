/*
 * propget-cmd.c -- Display status information in current directory
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
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
svn_cl__propget (svn_cl__opt_state_t *opt_state,
                 apr_array_header_t *targets,
                 apr_pool_t *pool)
{
  svn_string_t *name  = ((svn_string_t **) (opt_state->args->elts))[0];
  apr_hash_t *prop_hash = apr_hash_make (pool);
  svn_error_t *err;
  int i;

  /* Add "." if user passed 0 file arguments */
  push_implicit_dot_target(targets, pool);

  /* FIXME: reformat block to remove extra spaces */

    for (i = 0; i < targets->nelts; i++)
      {
        svn_string_t *value;
        svn_string_t *target = ((svn_string_t **) (targets->elts))[i];
        err = svn_wc_prop_get (&value, name, target, pool);
        if (err)
          return err;

        /* kff todo: this seems like an odd way to do this... */

        apr_hash_set (prop_hash, name->data, name->len,
                      value);
        svn_cl__print_prop_hash (prop_hash, pool);
      }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
