/*
 * merge-cmd.c -- Merging changes into a working copy.
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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

#include "svn_client.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_types.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__merge(apr_getopt_t *os,
              void *baton,
              apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  const char *sourcepath1, *sourcepath2, *targetpath;
  svn_boolean_t using_alternate_syntax = FALSE;
  svn_error_t *err;
  svn_opt_revision_t peg_revision;
  apr_array_header_t *options;

  /* If the first opt_state revision is filled in at this point, then
     we know the user must have used the '-r' switch. */
  if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      /* sanity check:  they better have given supplied a *range*.  */
      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0,
                                _("Second revision required"));

      using_alternate_syntax = TRUE;
    }

  SVN_ERR(svn_opt_args_to_target_array2(&targets, os, 
                                        opt_state->targets, pool));

  if (using_alternate_syntax)
    {
      if (targets->nelts < 1)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL, NULL);
      if (targets->nelts > 2)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Too many arguments given"));

      SVN_ERR(svn_opt_parse_path(&peg_revision, &sourcepath1,
                                 ((const char **)(targets->elts))[0], pool));
      sourcepath2 = sourcepath1;

      /* Set the default peg revision if one was not specified. */
      if (peg_revision.kind == svn_opt_revision_unspecified)
        peg_revision.kind = svn_path_is_url(sourcepath1)
          ? svn_opt_revision_head : svn_opt_revision_working;

      /* decide where to apply the diffs, defaulting to '.' */
      if (targets->nelts == 2)
        targetpath = ((const char **) (targets->elts))[1];
      else
        targetpath = "";
    }
  else /* using @rev syntax */
    {
      if (targets->nelts < 2)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL, NULL);
      if (targets->nelts > 3)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Too many arguments given"));

      /* the first two paths become the 'sources' */
      SVN_ERR(svn_opt_parse_path(&opt_state->start_revision, &sourcepath1,
                                 ((const char **) (targets->elts))[0],
                                 pool));
      SVN_ERR(svn_opt_parse_path(&opt_state->end_revision, &sourcepath2,
                                 ((const char **) (targets->elts))[1],
                                 pool));
      
      /* Catch 'svn merge wc_path1 wc_path2 [target]' without explicit
         revisions--since it ignores local modifications it may not do what
         the user expects.  Forcing the user to specify a repository
         revision should avoid any confusion. */
      if ((opt_state->start_revision.kind == svn_opt_revision_unspecified
           && ! svn_path_is_url(sourcepath1))
          ||
          (opt_state->end_revision.kind == svn_opt_revision_unspecified
           && ! svn_path_is_url(sourcepath2)))
        return svn_error_create
          (SVN_ERR_CLIENT_BAD_REVISION, 0,
           _("A working copy merge source needs an explicit revision"));

      /* decide where to apply the diffs, defaulting to '.' */
      if (targets->nelts == 3)
        targetpath = ((const char **) (targets->elts))[2];
      else
        targetpath = "";
    }

  /* If no targetpath was specified, see if we can infer it from the
     sourcepaths. */
  if (! strcmp(targetpath, ""))
    {
      char *sp1_basename, *sp2_basename;
      sp1_basename = svn_path_basename(sourcepath1, pool);
      sp2_basename = svn_path_basename(sourcepath2, pool);

      if (! strcmp(sp1_basename, sp2_basename))
        {
          svn_node_kind_t kind;
          const char *decoded_path = svn_path_uri_decode(sp1_basename, pool);
          SVN_ERR(svn_io_check_path(decoded_path, &kind, pool));
          if (kind == svn_node_file) 
            {
              targetpath = decoded_path;
            }
        }
    }

  if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
    opt_state->start_revision.kind = svn_opt_revision_head;
  if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
    opt_state->end_revision.kind = svn_opt_revision_head;

  if (! opt_state->quiet)
    svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, FALSE,
                         FALSE, FALSE, pool);

  if (opt_state->extensions)
    options = svn_cstring_split(opt_state->extensions, " \t\n\r", TRUE, pool);
  else
    options = NULL;

  if (using_alternate_syntax)
    {
      err = svn_client_merge_peg2(sourcepath1,
                                  &(opt_state->start_revision),
                                  &(opt_state->end_revision),
                                  &peg_revision,
                                  targetpath,
                                  opt_state->nonrecursive ? FALSE : TRUE,
                                  opt_state->ignore_ancestry,
                                  opt_state->force,
                                  opt_state->dry_run,
                                  options,
                                  ctx,
                                  pool);
    }
  else
    {
      err = svn_client_merge2(sourcepath1,
                              &(opt_state->start_revision),
                              sourcepath2,
                              &(opt_state->end_revision),
                              targetpath,
                              opt_state->nonrecursive ? FALSE : TRUE,
                              opt_state->ignore_ancestry,
                              opt_state->force,
                              opt_state->dry_run,
                              options,
                              ctx,
                              pool);
    }
  if (err)
    return svn_cl__may_need_force(err);

  return SVN_NO_ERROR;
}
