/*
 * merge-cmd.c -- Merging changes into a working copy.
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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
  const char *sourcepath1 = NULL, *sourcepath2 = NULL, *targetpath = "";
  svn_boolean_t using_rev_range_syntax = FALSE;
  svn_error_t *err;
  svn_opt_revision_t peg_revision1, peg_revision2;
  apr_array_header_t *options;

  SVN_ERR(svn_opt_args_to_target_array2(&targets, os,
                                        opt_state->targets, pool));
  if (targets->nelts >= 1)
    {
      SVN_ERR(svn_opt_parse_path(&peg_revision1, &sourcepath1,
                                 APR_ARRAY_IDX(targets, 0, const char *),
                                 pool));
      if (targets->nelts >= 2)
        SVN_ERR(svn_opt_parse_path(&peg_revision2, &sourcepath2,
                                   APR_ARRAY_IDX(targets, 1, const char *),
                                   pool));
    }

  /* If an (optional) source, or a source plus WC path is provided,
     the revision range syntax is in use. */
  if (targets->nelts <= 1)
    {
      using_rev_range_syntax = TRUE;
    }
  else if (targets->nelts == 2)
    {
      if (svn_path_is_url(sourcepath1) && !svn_path_is_url(sourcepath2))
        using_rev_range_syntax = TRUE;
    }

  /* If the first opt_state revision is filled in at this point, then
     we know the user must have used the '-r' or '-c' switch. */
  if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      /* A revision *range* is required. */
      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0,
                                _("Second revision required"));

      using_rev_range_syntax = TRUE;
    }

  if (using_rev_range_syntax)
    {
      if (targets->nelts < 1 && !opt_state->use_merge_history)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL, NULL);
      if (targets->nelts > 2)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Too many arguments given"));

      /* Set the default value for unspecified paths and peg revision. */
      if (targets->nelts == 0)
        {
          peg_revision1.kind = svn_opt_revision_head;
        }
      else
        {
          /* targets->nelts is 1 or 2 here. */
          sourcepath2 = sourcepath1;

          if (peg_revision1.kind == svn_opt_revision_unspecified)
            peg_revision1.kind = svn_path_is_url(sourcepath1)
              ? svn_opt_revision_head : svn_opt_revision_working;

          if (targets->nelts == 2)
            targetpath = APR_ARRAY_IDX(targets, 1, const char *);
        }
    }
  else /* using @rev syntax */
    {
      if (targets->nelts < 2)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL, NULL);
      if (targets->nelts > 3)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Too many arguments given"));

      opt_state->start_revision = peg_revision1;
      opt_state->end_revision = peg_revision2;

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

      /* Default peg revisions to each URL's youngest revision. */
      if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
        opt_state->start_revision.kind = svn_opt_revision_head;
      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        opt_state->end_revision.kind = svn_opt_revision_head;

      /* Decide where to apply the delta (defaulting to "."). */
      if (targets->nelts == 3)
        targetpath = APR_ARRAY_IDX(targets, 2, const char *);
    }

  /* If no targetpath was specified, see if we can infer it from the
     sourcepaths. */
  if (sourcepath1 && sourcepath2 && strcmp(targetpath, "") == 0)
    {
      /* If the sourcepath is a URL, it can only refer to a target in the
         current working directory.
         However, if the sourcepath is a local path, it can refer to a target
         somewhere deeper in the directory structure. */
      if (svn_path_is_url(sourcepath1))
        {
          char *sp1_basename, *sp2_basename;
          sp1_basename = svn_path_basename(sourcepath1, pool);
          sp2_basename = svn_path_basename(sourcepath2, pool);

          if (strcmp(sp1_basename, sp2_basename) == 0)
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
      else if (strcmp(sourcepath1, sourcepath2) == 0)
        {
          svn_node_kind_t kind;
          const char *decoded_path = svn_path_uri_decode(sourcepath1, pool);
          SVN_ERR(svn_io_check_path(decoded_path, &kind, pool));
          if (kind == svn_node_file)
            {
              targetpath = decoded_path;
            }
        }
    }

  if (! opt_state->quiet)
    svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, FALSE,
                         FALSE, FALSE, pool);

  if (opt_state->extensions)
    options = svn_cstring_split(opt_state->extensions, " \t\n\r", TRUE, pool);
  else
    options = NULL;

  if (using_rev_range_syntax)
    {
      if (! sourcepath1)
        {
          /* If a merge source was not specified, try to derive it. */
          apr_array_header_t *suggested_sources;
          svn_opt_revision_t working_rev;
          working_rev.kind = svn_opt_revision_working;

          SVN_ERR(svn_client_suggest_merge_sources(&suggested_sources,
                                                   targetpath, &working_rev, 
                                                   ctx, pool));
          if (! suggested_sources->nelts)
            return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                                     _("Unable to determine merge source for "
                                       "'%s' -- please provide an explicit "
                                       "source"),
                                     svn_path_local_style(targetpath, pool));
          sourcepath1 = APR_ARRAY_IDX(suggested_sources, 0, const char *);
        }
        
      err = svn_client_merge_peg3(sourcepath1,
                                  &(opt_state->start_revision),
                                  &(opt_state->end_revision),
                                  &peg_revision1,
                                  targetpath,
                                  opt_state->depth,
                                  opt_state->ignore_ancestry,
                                  opt_state->force,
                                  opt_state->record_only,
                                  opt_state->dry_run,
                                  options,
                                  ctx,
                                  pool);
    }
  else
    {
      err = svn_client_merge3(sourcepath1,
                              &(opt_state->start_revision),
                              sourcepath2,
                              &(opt_state->end_revision),
                              targetpath,
                              opt_state->depth,
                              opt_state->ignore_ancestry,
                              opt_state->force,
                              opt_state->record_only,
                              opt_state->dry_run,
                              options,
                              ctx,
                              pool);
    }
  if (err)
    return svn_cl__may_need_force(err);

  return SVN_NO_ERROR;
}
