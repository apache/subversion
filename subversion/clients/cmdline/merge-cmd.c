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


svn_error_t *
svn_cl__merge (apr_getopt_t *os,
               svn_cl__opt_state_t *opt_state,
               apr_pool_t *pool)
{
  apr_array_header_t *targets;
  const svn_delta_editor_t *trace_editor = NULL;
  void *trace_edit_baton = NULL;
  svn_client_auth_baton_t *auth_baton;
  const char *parent_dir, *entry;
  const char *sourcepath1, *sourcepath2, *targetpath;
  svn_boolean_t using_alternate_syntax = FALSE;
  svn_error_t *err;

  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  /* If the first opt_state revision is filled in at this point, then
     we know the user must have used the '-r' switch. */
  if (opt_state->start_revision.kind != svn_client_revision_unspecified)
    {
      /* sanity check:  they better have given supplied a *range*.  */
      if (opt_state->end_revision.kind == svn_client_revision_unspecified)
        {
          svn_cl__subcommand_help ("merge", pool);
          return svn_error_create (SVN_ERR_CL_INSUFFICIENT_ARGS,
                                   0, 0, pool, "Second revision required.");
        }
      using_alternate_syntax = TRUE;
    }

  targets = svn_cl__args_to_target_array (os, opt_state,
                                          TRUE, /* extract @rev revisions */
                                          pool);
  
  if (using_alternate_syntax)
    {
      if (targets->nelts < 1)
        {
          svn_cl__subcommand_help ("merge", pool);
          return svn_error_create (SVN_ERR_CL_INSUFFICIENT_ARGS, 0, 0, pool,
                                   "Need at least one path.");
        }

      sourcepath1 = sourcepath2 = ((const char **) (targets->elts))[0];
      if (targets->nelts >= 2)
        targetpath = ((const char **) (targets->elts))[1];
      else
        targetpath = ".";
    }
  else /* using @rev syntax, revs already extracted. */
    {
      if (targets->nelts < 2)
        {
          svn_cl__subcommand_help ("merge", pool);
          return svn_error_create (SVN_ERR_CL_INSUFFICIENT_ARGS, 0, 0, pool,
                                   "Need at least two paths.");
        }

      sourcepath1 = ((const char **) (targets->elts))[0];
      sourcepath2 = ((const char **) (targets->elts))[1];
      if (targets->nelts >= 3)
        targetpath = ((const char **) (targets->elts))[2];
      else
        targetpath = ".";
    }

  if (opt_state->start_revision.kind == svn_client_revision_unspecified)
    opt_state->start_revision.kind = svn_client_revision_head;
  if (opt_state->end_revision.kind == svn_client_revision_unspecified)
    opt_state->end_revision.kind = svn_client_revision_head;

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

  SVN_ERR (svn_wc_get_actual_target (targetpath, &parent_dir, &entry, pool));

  if (! opt_state->quiet)
    SVN_ERR (svn_cl__get_trace_update_editor (&trace_editor, &trace_edit_baton,
                                              parent_dir, FALSE, TRUE, pool));

  /* ### NOTE:  see issue #748.  We used to pass TARGETPATH into this
     function, but that argument has been temporarily removed.  All
     merging action happens in "." now.  */
  err = svn_client_merge (trace_editor, trace_edit_baton,
                          auth_baton,
                          sourcepath1,
                          &(opt_state->start_revision),
                          sourcepath2,
                          &(opt_state->end_revision),
                          opt_state->nonrecursive ? FALSE : TRUE,
                          opt_state->force,
                          pool); 
  if (err)
     return svn_cl__may_need_force (err);

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
