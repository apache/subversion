/*
 * merge-cmd.c -- Merging changes into a working copy.
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  const char *sourcepath1, *sourcepath2, *targetpath;
  svn_boolean_t using_alternate_syntax = FALSE;
  svn_error_t *err;

  /* If the first opt_state revision is filled in at this point, then
     we know the user must have used the '-r' switch. */
  if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      /* sanity check:  they better have given supplied a *range*.  */
      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        {
          svn_opt_subcommand_help ("merge", svn_cl__cmd_table,
                                   svn_cl__options, pool);
          return svn_error_create (SVN_ERR_CL_INSUFFICIENT_ARGS, 0,
                                   "Second revision required.");
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
      return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0,
                               "" /* message is unused */);
    }

  if (using_alternate_syntax)
    {
      if ((targets->nelts < 1) || (targets->nelts > 2))
        {
          svn_opt_subcommand_help ("merge", svn_cl__cmd_table,
                                   svn_cl__options, pool);
          return svn_error_create (SVN_ERR_CL_INSUFFICIENT_ARGS, 0,
                                   "Wrong number of paths given.");
        }

      /* the first path becomes both of the 'sources' */
      sourcepath1 = sourcepath2 = ((const char **)(targets->elts))[0];
      
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
          return svn_error_create (SVN_ERR_CL_INSUFFICIENT_ARGS, 0,
                                   "Wrong number of paths given.");
        }

      /* the first two paths become the 'sources' */
      sourcepath1 = ((const char **) (targets->elts))[0];
      sourcepath2 = ((const char **) (targets->elts))[1];
      
      /* Catch 'svn merge wc_path1 wc_path2 [target]' without explicit
         revisions--since it ignores local modifications it may not do what
         the user expects.  Forcing the user to specify a repository
         revision should avoid any confusion. */
      if ((opt_state->start_revision.kind == svn_opt_revision_unspecified
           && ! svn_path_is_url (sourcepath1))
          ||
          (opt_state->end_revision.kind == svn_opt_revision_unspecified
           && ! svn_path_is_url (sourcepath2)))
        return svn_error_create
          (SVN_ERR_CLIENT_BAD_REVISION, 0,
           "A working copy merge source needs an explicit revision");

      /* decide where to apply the diffs, defaulting to '.' */
      if (targets->nelts == 3)
        targetpath = ((const char **) (targets->elts))[2];
      else
        targetpath = "";
    }

  /* If no targetpath was specified, see if we can infer it from the
     sourcepaths. */
  if (! strcmp (targetpath, ""))
    {
      char *sp1_basename, *sp2_basename;
      sp1_basename = svn_path_basename (sourcepath1, pool);
      sp2_basename = svn_path_basename (sourcepath2, pool);

      if (! strcmp (sp1_basename, sp2_basename))
        {
          svn_node_kind_t kind;
          SVN_ERR (svn_io_check_path (sp1_basename, &kind, pool));
          if (kind == svn_node_file) 
            {
              targetpath = sp1_basename;
            }
        }
    }

  if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
    opt_state->start_revision.kind = svn_opt_revision_head;
  if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
    opt_state->end_revision.kind = svn_opt_revision_head;

  if (! opt_state->quiet)
    svn_cl__get_notifier (&ctx->notify_func, &ctx->notify_baton, FALSE, FALSE,
                          FALSE, pool);

  err = svn_client_merge (sourcepath1,
                          &(opt_state->start_revision),
                          sourcepath2,
                          &(opt_state->end_revision),
                          targetpath,
                          opt_state->nonrecursive ? FALSE : TRUE,
                          opt_state->notice_ancestry ? FALSE : TRUE,
                          opt_state->force,
                          opt_state->dry_run,
                          ctx,
                          pool); 
  if (err)
     return svn_cl__may_need_force (err);

  return SVN_NO_ERROR;
}
