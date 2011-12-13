/*
 * merge-cmd.c -- Merging changes into a working copy.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#include "svn_client.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_types.h"
#include "cl.h"

#include "private/svn_client_private.h"
#include "private/svn_wc_private.h"
#include "svn_private_config.h"


/*** Code. ***/

#if defined(WIN32) || defined(__CYGWIN__) || defined(__OS2__)
#define SVN_USE_DOS_PATHS
#endif

/* A message to print after reintegration */
const char *keep_alive_message = N_(
  "To continue using the source branch after this reintegration, note the\n"
  "new revision number REV created by the commit and perform the following\n"
  "command in a working copy of that branch:\n"
  "\n"
  "  svn merge --record-only --change REV ^/%s .\n"
  "\n"
  "See \"Keeping a Reintegrated Branch Alive\" in the Svn Book here:\n"
  "<http://svnbook.red-bean.com/en/1.6/svn-book.html#svn.branchmerge.advanced.reintegratetwice>\n"
);

/* Set *REPOS_RELPATH to the repository path of PATH_OR_URL relative to the
 * repository root. */
static svn_error_t *
get_repos_relpath(const char **repos_relpath,
                  const char *path_or_url,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  const char *abspath_or_url, *url, *repos_url;

  if (svn_path_is_url(path_or_url))
    abspath_or_url = path_or_url;
  else
    SVN_ERR(svn_dirent_get_absolute(&abspath_or_url, path_or_url, pool));
  SVN_ERR(svn_client_url_from_path2(&url, abspath_or_url,
                                    ctx, pool, pool));
  SVN_ERR(svn_client_get_repos_root(&repos_url, NULL, abspath_or_url,
                                    ctx, pool, pool));
  *repos_relpath = svn_uri_skip_ancestor(repos_url, url, pool);
  return SVN_NO_ERROR;
}

/* Set *REPOS_RELPATH to the repository path of PATH_OR_URL relative to the
 * repository root, with a "^/" (or "^^/" on Windows) prefix. */
static svn_error_t *
quoted_repos_relpath(const char **repos_relpath,
                     const char *path_or_url,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  SVN_ERR(get_repos_relpath(repos_relpath, path_or_url, ctx, pool));
#ifdef SVN_USE_DOS_PATHS
  *repos_relpath = apr_psprintf(pool, "^^/%s", *repos_relpath);
#else
  *repos_relpath = apr_psprintf(pool, "^/%s", *repos_relpath);
#endif
  return SVN_NO_ERROR;
}

/* Set *TARGET_ABSPATH to the absolute path of, and *LOCK_ABSPATH to
 the absolute path to lock for, TARGET_WCPATH. */
static svn_error_t *
get_target_and_lock_abspath(const char **target_abspath,
                            const char **lock_abspath,
                            const char *target_wcpath,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  SVN_ERR(svn_dirent_get_absolute(target_abspath, target_wcpath,
                                  scratch_pool));
  SVN_ERR(svn_wc_read_kind(&kind, ctx->wc_ctx, *target_abspath, FALSE,
                           scratch_pool));
  if (kind == svn_node_dir)
    *lock_abspath = *target_abspath;
  else
    *lock_abspath = svn_dirent_dirname(*target_abspath, scratch_pool);

  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
merge_reintegrate_locked(const char *source,
                         const svn_opt_revision_t *peg_revision,
                         const char *target_wcpath,
                         const char *target_wc_abspath,
                         svn_boolean_t dry_run,
                         svn_boolean_t quiet,
                         const apr_array_header_t *merge_options,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *scratch_pool)
{
  svn_ra_session_t *source_ra_session;
  svn_ra_session_t *target_ra_session;
  const char *url1, *url2;
  svn_revnum_t rev1, rev2;
  svn_revnum_t yc_ancestor_rev;

  SVN_ERR(svn_client_find_reintegrate_merge(
            &source_ra_session, &target_ra_session,
            &url1, &rev1, &url2, &rev2, &yc_ancestor_rev,
            source, peg_revision, target_wc_abspath,
            ctx, scratch_pool, scratch_pool));

  if (! quiet)
    {
      const char *relpath1, *relpath2;

      SVN_ERR(quoted_repos_relpath(&relpath1, url1, ctx, scratch_pool));
      SVN_ERR(quoted_repos_relpath(&relpath2, url2, ctx, scratch_pool));

      printf(_("The reintegrate merge will be equivalent to:\n"
               "  svn merge %s@%ld %s@%ld %s\n"),
             relpath1, rev1, relpath2, rev2,
             svn_path_local_style(target_wcpath, scratch_pool));
    }

  SVN_ERR(svn_client_do_reintegrate_merge(
            source_ra_session, target_ra_session,
            url1, rev1, url2, rev2, yc_ancestor_rev,
            target_wc_abspath,
            dry_run, merge_options, ctx, scratch_pool));
  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
merge_reintegrate(const char *source,
                  const svn_opt_revision_t *peg_revision,
                  const char *target_wcpath,
                  svn_boolean_t dry_run,
                  svn_boolean_t quiet,
                  const apr_array_header_t *merge_options,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  const char *target_wc_abspath, *lock_abspath;

  SVN_ERR(get_target_and_lock_abspath(&target_wc_abspath, &lock_abspath,
                                      target_wcpath, ctx, pool));

  if (!dry_run)
    SVN_WC__CALL_WITH_WRITE_LOCK(
      merge_reintegrate_locked(source, peg_revision,
                               target_wcpath, target_wc_abspath,
                               dry_run, quiet, merge_options, ctx, pool),
      ctx->wc_ctx, lock_abspath, FALSE /* lock_anchor */, pool);
  else
    SVN_ERR(merge_reintegrate_locked(source, peg_revision,
                                     target_wcpath, target_wc_abspath,
                                     dry_run, quiet, merge_options, ctx, pool));

  return SVN_NO_ERROR;
}

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
  const char *source1_repos_relpath, *source2_repos_relpath;
  const char *target_repos_relpath;

  /* Merge doesn't support specifying a revision or revision range
     when using --reintegrate. */
  if (opt_state->reintegrate
      && opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("-r and -c can't be used with --reintegrate"));
    }

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* For now, we require at least one source.  That may change in
     future versions of Subversion, for example if we have support for
     negated mergeinfo.  See this IRC conversation:

       <bhuvan>   kfogel: yeah, i think you are correct; we should
                  specify the source url

       <kfogel>   bhuvan: I'll change the help output and propose for
                  backport.  Thanks.

       <bhuvan>   kfogel: np; while we are at it, 'svn merge' simply
                  returns nothing; i think we should say: """svn: Not
                  enough arguments provided; try 'svn help' for more
                  info"""

       <kfogel>   good idea

       <kfogel>   (in the future, 'svn merge' might actually do
                  something, but that's all the more reason to make
                  sure it errors now)

       <cmpilato> actually, i'm pretty sure 'svn merge' does something

       <cmpilato> it says "please merge any unmerged changes from
                  myself to myself."

       <cmpilato> :-)

       <kfogel>   har har

       <cmpilato> kfogel: i was serious.

       <kfogel>   cmpilato: urrr, uh.  Is that meaningful?  Is there
                  ever a reason for a user to run it?

       <cmpilato> kfogel: not while we don't have support for negated
                  mergeinfo.

       <kfogel>   cmpilato: do you concur that until it does something
                  useful it should error?

       <cmpilato> kfogel: yup.

       <kfogel>   cool
  */
  if (targets->nelts < 1)
    {
      /* Infer the merge source automatically, assuming a sync merge. */
      svn_client_peg_t *target_peg, *source_peg;

      SVN_ERR(svn_client_peg_create(&target_peg, "", NULL, pool));
      SVN_ERR(svn_cl__find_merge_source_branch(&source_peg, target_peg, ctx, pool));
      /*SVN_ERR(svn_client__resolve_target_location_from_peg(&source, source_peg, NULL, ctx, pool));*/
      if (! opt_state->quiet)
        printf(_("Assuming source branch is copy-source of target branch: '%s'\n"),
                 svn_cl__peg_for_display(source_peg, pool));
      peg_revision1 = source_peg->peg_revision;
      sourcepath1 = source_peg->path_or_url;
    }
  else  /* Parse at least one, and possible two, sources. */
    {
      SVN_ERR(svn_opt_parse_path(&peg_revision1, &sourcepath1,
                                 APR_ARRAY_IDX(targets, 0, const char *),
                                 pool));
      if (targets->nelts >= 2)
        SVN_ERR(svn_opt_parse_path(&peg_revision2, &sourcepath2,
                                   APR_ARRAY_IDX(targets, 1, const char *),
                                   pool));
    }

  /* We could have one or two sources.  Deliberately written to stay
     correct even if we someday permit implied merge source. */
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
                                    _("Cannot specify a revision range "
                                      "with two URLs"));
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
      if (first_range_start.kind != svn_opt_revision_number
          && first_range_start.kind != svn_opt_revision_date
          && first_range_start.kind != svn_opt_revision_head
          && ! svn_path_is_url(sourcepath1))
        return svn_error_createf(
          SVN_ERR_CLIENT_BAD_REVISION, NULL,
          _("Invalid merge source '%s'; a working copy path can only be "
            "used with a repository revision (a number, a date, or head)"),
          svn_dirent_local_style(sourcepath1, pool));
      if (first_range_end.kind != svn_opt_revision_number
          && first_range_end.kind != svn_opt_revision_date
          && first_range_end.kind != svn_opt_revision_head
          && ! svn_path_is_url(sourcepath2))
        return svn_error_createf(
          SVN_ERR_CLIENT_BAD_REVISION, NULL,
          _("Invalid merge source '%s'; a working copy path can only be "
            "used with a repository revision (a number, a date, or head)"),
          svn_dirent_local_style(sourcepath2, pool));

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
      /* If the sourcepath is a URL, it can only refer to a target in
         the current working directory.  However, if the sourcepath is
         a local path, it can refer to a target somewhere deeper in
         the directory structure. */
      if (svn_path_is_url(sourcepath1))
        {
          const char *sp1_basename = svn_uri_basename(sourcepath1, pool);
          const char *sp2_basename = svn_uri_basename(sourcepath2, pool);

          if (strcmp(sp1_basename, sp2_basename) == 0)
            {
              svn_node_kind_t kind;

              SVN_ERR(svn_io_check_path(sp1_basename, &kind, pool));
              if (kind == svn_node_file)
                {
                  targetpath = sp1_basename;
                }
            }
        }
      else if (strcmp(sourcepath1, sourcepath2) == 0)
        {
          svn_node_kind_t kind;

          SVN_ERR(svn_io_check_path(sourcepath1, &kind, pool));
          if (kind == svn_node_file)
            {
              targetpath = sourcepath1;
            }
        }
    }
  SVN_ERR(quoted_repos_relpath(&source1_repos_relpath, sourcepath1, ctx, pool));
  SVN_ERR(quoted_repos_relpath(&source2_repos_relpath, sourcepath2, ctx, pool));
  SVN_ERR(quoted_repos_relpath(&target_repos_relpath, targetpath, ctx, pool));

  if (opt_state->extensions)
    options = svn_cstring_split(opt_state->extensions, " \t\n\r", TRUE, pool);
  else
    options = NULL;

  /* More input validation. */
  if (opt_state->reintegrate)
    {
      if (opt_state->depth != svn_depth_unknown)
        return svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                                _("--depth cannot be used with "
                                  "--reintegrate"));

      if (opt_state->force)
        return svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                                _("--force cannot be used with "
                                  "--reintegrate"));

      if (two_sources_specified)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--reintegrate can only be used with "
                                  "a single merge source"));
      if (opt_state->allow_mixed_rev)
        return svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                                _("--allow-mixed-revisions cannot be used "
                                  "with --reintegrate"));
    }

  if (opt_state->dry_run && ! opt_state->quiet)
    printf(_("This is a dry-run merge: the working copy will not be changed.\n"));

  if (opt_state->reintegrate)
    {
      if (! opt_state->quiet)
        printf(_("Reintegrate merge\n"
                 "  from '%s' into WC of '%s'\n"),
             source1_repos_relpath, target_repos_relpath);
      err = merge_reintegrate(sourcepath1, &peg_revision1, targetpath,
                              opt_state->dry_run, opt_state->quiet,
                              options, ctx, pool);

      /* Tell the user how to keep the source branch alive. */
      if (! err)
        printf(_(keep_alive_message), target_repos_relpath);
    }
  else if (! two_sources_specified)
    {
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
          range->end = peg_revision1;
          APR_ARRAY_PUSH(ranges_to_merge, svn_opt_revision_range_t *) = range;

          if (! opt_state->quiet)
            printf(_("Sync merge\n"
                     "  from '%s' into WC of '%s'\n"),
                   source1_repos_relpath, target_repos_relpath);
        }
      else
        {
          if (! opt_state->quiet)
            printf(_("Cherry-pick merge\n"
                     "  from '%s' into WC of '%s'\n"),
                   source1_repos_relpath, target_repos_relpath);
        }

      err = svn_client_merge_peg4(sourcepath1,
                                  ranges_to_merge,
                                  &peg_revision1,
                                  targetpath,
                                  opt_state->depth,
                                  opt_state->ignore_ancestry,
                                  opt_state->force,
                                  opt_state->record_only,
                                  opt_state->dry_run,
                                  opt_state->allow_mixed_rev,
                                  options,
                                  ctx,
                                  pool);
    }
  else
    {
      if (svn_path_is_url(sourcepath1) != svn_path_is_url(sourcepath2))
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Merge sources must both be "
                                  "either paths or URLs"));
      if (! opt_state->quiet)
        printf(_("Two-URL merge\n"
                 "  from diff between '%s' and '%s' into WC of '%s'\n"),
               source1_repos_relpath, source2_repos_relpath,
               target_repos_relpath);
      err = svn_client_merge4(sourcepath1,
                              &first_range_start,
                              sourcepath2,
                              &first_range_end,
                              targetpath,
                              opt_state->depth,
                              opt_state->ignore_ancestry,
                              opt_state->force,
                              opt_state->record_only,
                              opt_state->dry_run,
                              opt_state->allow_mixed_rev,
                              options,
                              ctx,
                              pool);
    }

  if (! opt_state->quiet)
    SVN_ERR(svn_cl__print_conflict_stats(ctx->notify_baton2, pool));

  if (err)
    {
      if(err->apr_err == SVN_ERR_CLIENT_INVALID_MERGEINFO_NO_MERGETRACKING)
        {
          err = svn_error_quick_wrap(
            err,
            _("Merge tracking not possible, use --ignore-ancestry or\n"
              "fix invalid mergeinfo in target with 'svn propset'"));
        }
      else if (! opt_state->reintegrate)
        {
          return svn_cl__may_need_force(err);
        }
    }

  return svn_error_trace(err);
}
