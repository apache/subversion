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
  svn_boolean_t two_sources_specified = TRUE;
  svn_error_t *err;
  svn_opt_revision_t first_range_start, first_range_end, peg_revision1,
    peg_revision2;
  apr_array_header_t *options, *ranges_to_merge = opt_state->revision_ranges;

  SVN_ERR(svn_opt_args_to_target_array2(&targets, os,
                                        opt_state->targets, pool));

  /* Parse at least one, and possible two, sources. */
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

  /* If nothing (ie, "."), a single source, or a source URL plus WC path is
     provided, then we don't have two distinct sources. */
  if (targets->nelts <= 1)
    {
      two_sources_specified = FALSE;
    }
  else if (targets->nelts == 2)
    {
      if (svn_path_is_url(sourcepath1) && !svn_path_is_url(sourcepath2))
        two_sources_specified = FALSE;
    }

  if (opt_state->revision_ranges->nelts > 0)
    {
      first_range_start = APR_ARRAY_IDX(opt_state->revision_ranges, 0,
                                        svn_opt_revision_range_t *)->start;
      first_range_end = APR_ARRAY_IDX(opt_state->revision_ranges, 0,
                                      svn_opt_revision_range_t *)->end;
    }
  else
    {
      first_range_start.kind = first_range_end.kind =
        svn_opt_revision_unspecified;
    }

  /* If revision_ranges has at least one real range at this point, then
     we know the user must have used the '-r' and/or '-c' switch(es). 
     This means we're *not* doing two distinct sources. */
  if (first_range_start.kind != svn_opt_revision_unspecified)
    {
      /* A revision *range* is required. */
      if (first_range_end.kind == svn_opt_revision_unspecified)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0,
                                _("Second revision required"));

      two_sources_specified = FALSE;
    }

  if (! two_sources_specified) /* TODO: Switch order of if */
    {
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
          /* targets->nelts is 1 ("svn merge SOURCE") or 2 ("svn merge
             SOURCE WCPATH") here. */
          sourcepath2 = sourcepath1;

          if (peg_revision1.kind == svn_opt_revision_unspecified)
            peg_revision1.kind = svn_path_is_url(sourcepath1)
              ? svn_opt_revision_head : svn_opt_revision_working;

          if (targets->nelts == 2)
            {
              targetpath = APR_ARRAY_IDX(targets, 1, const char *);
              if (svn_path_is_url(targetpath))
                return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                        _("Cannot specifify a revision range "
                                          "with two URLs"));
            }
        }
    }
  else /* using @rev syntax */
    {
      if (targets->nelts < 2)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL, NULL);
      if (targets->nelts > 3)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Too many arguments given"));

      first_range_start = peg_revision1;
      first_range_end = peg_revision2;

      /* Catch 'svn merge wc_path1 wc_path2 [target]' without explicit
         revisions--since it ignores local modifications it may not do what
         the user expects.  Forcing the user to specify a repository
         revision should avoid any confusion. */
      if ((first_range_start.kind == svn_opt_revision_unspecified
           && ! svn_path_is_url(sourcepath1))
          ||
          (first_range_end.kind == svn_opt_revision_unspecified
           && ! svn_path_is_url(sourcepath2)))
        return svn_error_create
          (SVN_ERR_CLIENT_BAD_REVISION, 0,
           _("A working copy merge source needs an explicit revision"));

      /* Default peg revisions to each URL's youngest revision. */
      if (first_range_start.kind == svn_opt_revision_unspecified)
        first_range_start.kind = svn_opt_revision_head;
      if (first_range_end.kind == svn_opt_revision_unspecified)
        first_range_end.kind = svn_opt_revision_head;

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

  if (! two_sources_specified) /* TODO: Switch order of if */
    {
      /* If we don't have a source, use the target as the source. */
      if (! sourcepath1)
        sourcepath1 = targetpath;

      /* If we don't have at least one valid revision range, pick a
         good one that spans the entire set of revisions on our
         source. */
      if ((first_range_start.kind == svn_opt_revision_unspecified)
          && (first_range_end.kind == svn_opt_revision_unspecified))
        {
          svn_opt_revision_range_t *range = apr_pcalloc(pool, sizeof(*range));
          ranges_to_merge = apr_array_make(pool, 1, sizeof(range));
          range->start.kind = svn_opt_revision_number;
          range->start.value.number = 1;
          range->end.kind = svn_opt_revision_head;
          APR_ARRAY_PUSH(ranges_to_merge, svn_opt_revision_range_t *) = range;
        }

      if (opt_state->reintegrate)
        err = svn_client_merge_reintegrate(sourcepath1,
                                           &peg_revision1,
                                           targetpath,
                                           opt_state->ignore_ancestry,
                                           opt_state->force,
                                           opt_state->record_only,
                                           opt_state->dry_run,
                                           options, ctx, pool);
      else
        err = svn_client_merge_peg3(sourcepath1,
                                    ranges_to_merge,
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
                              &first_range_start,
                              sourcepath2,
                              &first_range_end,
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
