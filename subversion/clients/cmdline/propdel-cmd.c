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

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__propdel (apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = baton;
  const char *pname, *pname_utf8;
  apr_array_header_t *args, *targets;
  int i;

  /* Get the property's name (and a UTF-8 version of that name). */
  SVN_ERR (svn_opt_parse_num_args (&args, os, 1, pool));
  pname = ((const char **) (args->elts))[0];
  SVN_ERR (svn_utf_cstring_to_utf8 (&pname_utf8, pname, NULL, pool));

  /* Suck up all the remaining arguments into a targets array */
  SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         FALSE, pool));

  /* Add "." if user passed 0 file arguments */
  svn_opt_push_implicit_dot_target (targets, pool);

  /* Decide if we're deleting a versioned working copy prop or an
     unversioned repository revision prop.  The existence of the '-r'
     flag is the key. */
  if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      svn_revnum_t rev;
      const char *URL, *target;
      svn_client_auth_baton_t *auth_baton;

      auth_baton = svn_cl__make_auth_baton (opt_state, pool);

      /* Either we have a URL target, or an implicit wc-path ('.')
         which needs to be converted to a URL. */
      if (targets->nelts <= 0)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL, pool,
                                "No URL target available.");
      target = ((const char **) (targets->elts))[0];
      SVN_ERR (svn_cl__get_url_from_target (&URL, target, pool));  
      if (URL == NULL)
        return svn_error_create(SVN_ERR_UNVERSIONED_RESOURCE, 0, NULL,
                                pool,
                                "Either a URL or versioned item is required.");

      /* Let libsvn_client do the real work. */
      SVN_ERR (svn_client_revprop_set (pname_utf8, NULL,
                                       URL, &(opt_state->start_revision),
                                       auth_baton, &rev, pool));
      if (! opt_state->quiet) 
        {
          const char *target_native;
          SVN_ERR (svn_utf_cstring_from_utf8 (&target_native,
                                              target, pool));
          printf ("property `%s' deleted from repository revision '%"
                  SVN_REVNUM_T_FMT"'\n",
                  pname, rev);
        }      
    }

  else
    {
      /* For each target, remove the property PNAME. */
      for (i = 0; i < targets->nelts; i++)
        {
          const char *target = ((const char **) (targets->elts))[i];
          SVN_ERR (svn_client_propset (pname_utf8, NULL, target,
                                       opt_state->recursive, pool));
          if (! opt_state->quiet) 
            {
              const char *target_native;
              SVN_ERR (svn_utf_cstring_from_utf8 (&target_native,
                                                  target, pool));
              printf ("property `%s' deleted%sfrom '%s'.\n", pname,
                      opt_state->recursive ? " (recursively) " : " ",
                      target_native);
            }
        }
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
