/*
 * propset-cmd.c -- Display status information in current directory
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
svn_cl__propset (svn_cl__opt_state_t *opt_state,
                 apr_array_header_t *targets,
                 apr_pool_t *pool)
{
  svn_string_t *name  = ((svn_string_t **) (opt_state->args->elts))[0];
  svn_string_t *value = ((svn_string_t **) (opt_state->args->elts))[1];
  svn_error_t *err;
  int i;

  if (! strcmp (value->data, ""))
    /* The user wants to delete the property. */
    value = NULL;

  if (targets->nelts)
    for (i = 0; i < targets->nelts; i++)
      {
        svn_string_t *target = ((svn_string_t **) (targets->elts))[i];
        err = svn_wc_prop_set (name, value, target, pool);
        if (err)
          return err;

        /* kff todo: prints may be temporary.  Ben? */
        if (value)
          printf ("property `%s' set on %s.\n",
                  name->data, target->data);
        else
          printf ("property `%s' deleted from %s\n", 
                  name->data, target->data);
      }
  else
    {
      fprintf (stderr, "svn propset: arguments required\n");
      err = svn_cl__help (opt_state, targets, pool);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
