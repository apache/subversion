/*
 * merge-cmd.c -- Merging changes into a working copy.
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
#include "svn_types.h"
#include "cl.h"


/*** Code. ***/


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__merge (apr_getopt_t *os,
               void *baton,
               apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = baton;
  apr_array_header_t *targets;
  svn_client_auth_baton_t *auth_baton;
  const char *sourcepath1, *sourcepath2, *targetpath;
  svn_boolean_t using_alternate_syntax = FALSE;
  svn_error_t *err;
  svn_wc_notify_func_t notify_func = NULL;
  void *notify_baton;

  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  /* If the first opt_state revision is filled in at this point, then
     we know the user must have used the '-r' switch. */
  if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      /* sanity check:  they better have given supplied a *range*.  */
      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        {
          svn_opt_subcommand_help ("merge", svn_cl__cmd_table,
                                   svn_cl__options, pool);
          return svn_error_create (SVN_ERR_CL_INSUFFICIENT_ARGS,
                                   0, 0, "Second revision required.");
        }
      using_alternate_syntax = TRUE;
    }

  SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         TRUE, /* extract @rev revisions */
                                         pool));

  /* If there are no targets at all, then let's just give the user a
     friendly help message, rather than spewing an error.  */
  if (targets->nelts == 0)
    {
      return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, 0,
			       "" /* message is unused */);
    }

  if (using_alternate_syntax)
    {
      const char *source, *url;

      if ((targets->nelts < 1) || (targets->nelts > 2))
        {
          svn_opt_subcommand_help ("merge", svn_cl__cmd_table,
                                   svn_cl__options, pool);
          return svn_error_create (SVN_ERR_CL_INSUFFICIENT_ARGS, 0, 0,
                                   "Wrong number of paths given.");
        }

      /* the first path becomes both of the 'sources' */
      source = ((const char **)(targets->elts))[0];
      SVN_ERR (svn_cl__get_url_from_target(&url, source, pool));
      if (! url)
        return svn_error_createf (SVN_ERR_ENTRY_MISSING_URL, 0, NULL,
                                  "'%s' has no URL", source);

      sourcepath1 = sourcepath2 = url;
      
      /* decide where to apply the diffs, defaulting to '.' */
      if (targets->nelts == 2)
        targetpath = ((const char **) (targets->elts))[1];
      else
        targetpath = "";
    }
  else /* using @rev syntax, revs already extracted. */
    {
      if ((targets->nelts < 2) || (targets->nelts > 3))
        {
          svn_opt_subcommand_help ("merge", svn_cl__cmd_table,
                                   svn_cl__options, pool);
          return svn_error_create (SVN_ERR_CL_INSUFFICIENT_ARGS, 0, 0,
                                   "Wrong number of paths given.");
        }

      /* the first two paths become the 'sources' */
      sourcepath1 = ((const char **) (targets->elts))[0];
      sourcepath2 = ((const char **) (targets->elts))[1];
      
      /* decide where to apply the diffs, defaulting to '.' */
      if (targets->nelts == 3)
        targetpath = ((const char **) (targets->elts))[2];
      else
        targetpath = "";
    }

  if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
    opt_state->start_revision.kind = svn_opt_revision_head;
  if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
    opt_state->end_revision.kind = svn_opt_revision_head;

  /*  ### Is anyone still using this debugging printf?
      printf ("I would now call svn_client_merge with these arguments\n");
      printf ("sourcepath1 = %s\nrevision1 = %ld, %d\n"
          "sourcepath2 = %s\nrevision2 = %ld, %d\ntargetpath = %s\n",
          sourcepath1->data, (long int) opt_state->start_revision.value.number,
          opt_state->start_revision.kind,
          sourcepath2->data, (long int) opt_state->end_revision.value.number,
          opt_state->end_revision.kind,
          targetpath->data);
          fflush (stdout);
  */

  if (! opt_state->quiet)
    svn_cl__get_notifier (&notify_func, &notify_baton, FALSE, FALSE, pool);

  err = svn_client_merge (notify_func, notify_baton,
                          auth_baton,
                          sourcepath1,
                          &(opt_state->start_revision),
                          sourcepath2,
                          &(opt_state->end_revision),
                          targetpath,
                          opt_state->nonrecursive ? FALSE : TRUE,
                          opt_state->force,
                          opt_state->dry_run,
                          pool); 
  if (err)
     return svn_cl__may_need_force (err);

  return SVN_NO_ERROR;
}
