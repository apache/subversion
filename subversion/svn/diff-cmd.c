/*
 * diff-cmd.c -- Display context diff of a file
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include "svn_pools.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_types.h"
#include "svn_cmdline.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* Convert KIND into a single character for display to the user. */
static char
text_mod_char(svn_client_diff_summarize_kind_t kind)
{
  switch (kind)
    {
      case svn_client_diff_summarize_kind_modified:
        return 'M';

      case svn_client_diff_summarize_kind_added:
        return 'A';

      case svn_client_diff_summarize_kind_deleted:
        return 'D';

      default:
        return ' ';
    }
}

/* Print summary information about a given change, implements the
 * svn_client_diff_summarize_func_t interface. */
static svn_error_t *
summarize_func(const svn_client_diff_summarize_t *summary,
               void *baton,
               apr_pool_t *pool)
{
  const char *path = baton;

  /* Tack on the target path, so we can differentiate between different parts
   * of the output when we're given multiple targets. */
  path = svn_path_join(path, summary->path, pool);

  /* Convert non-urls to local style, so that things like "" show up as "." */
  if (! svn_path_is_url(path))
    path = svn_path_local_style(path, pool);

  /* Note: This output format tries to look like the output of 'svn status',
   *       thus the blank spaces where information that is not relevant to
   *       a diff summary would go. */

  SVN_ERR(svn_cmdline_printf(pool,
                             "%c%c     %s\n",
                             text_mod_char(summary->summarize_kind),
                             summary->prop_changed ? 'M' : ' ',
                             path));

  SVN_ERR(svn_cmdline_fflush(stdout));

  return SVN_NO_ERROR;
}

/* An svn_opt_subcommand_t to handle the 'diff' command.
   This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__diff(apr_getopt_t *os,
             void *baton,
             apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  apr_array_header_t *options;
  apr_array_header_t *targets;
  apr_file_t *outfile, *errfile;
  apr_status_t status;
  const char *old_target, *new_target;
  apr_pool_t *iterpool;
  svn_boolean_t pegged_diff = FALSE;
  int i;

  /* Fall back to "" to get options initialized either way. */
  {
    const char *optstr = opt_state->extensions ? opt_state->extensions : "";
    options = svn_cstring_split(optstr, " \t\n\r", TRUE, pool);
  }

  /* Get an apr_file_t representing stdout and stderr, which is where
     we'll have the external 'diff' program print to. */
  if ((status = apr_file_open_stdout(&outfile, pool)))
    return svn_error_wrap_apr(status, _("Can't open stdout"));
  if ((status = apr_file_open_stderr(&errfile, pool)))
    return svn_error_wrap_apr(status, _("Can't open stderr"));

  SVN_ERR(svn_opt_args_to_target_array2(&targets, os,
                                        opt_state->targets, pool));

  if (! opt_state->old_target && ! opt_state->new_target
      && (targets->nelts == 2)
      && svn_path_is_url(APR_ARRAY_IDX(targets, 0, const char *))
      && svn_path_is_url(APR_ARRAY_IDX(targets, 1, const char *))
      && opt_state->start_revision.kind == svn_opt_revision_unspecified
      && opt_state->end_revision.kind == svn_opt_revision_unspecified)
    {
      /* The 'svn diff OLD_URL[@OLDREV] NEW_URL[@NEWREV]' case matches. */

      SVN_ERR(svn_opt_parse_path(&opt_state->start_revision, &old_target,
                                 APR_ARRAY_IDX(targets, 0, const char *),
                                 pool));
      SVN_ERR(svn_opt_parse_path(&opt_state->end_revision, &new_target,
                                 APR_ARRAY_IDX(targets, 1, const char *),
                                 pool));
      targets->nelts = 0;
      
      if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
        opt_state->start_revision.kind = svn_opt_revision_head;
      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        opt_state->end_revision.kind = svn_opt_revision_head;
    }
  else if (opt_state->old_target)
    {
      apr_array_header_t *tmp, *tmp2;
      svn_opt_revision_t old_rev, new_rev;
      
      /* The 'svn diff --old=OLD[@OLDREV] [--new=NEW[@NEWREV]]
         [PATH...]' case matches. */

      tmp = apr_array_make(pool, 2, sizeof(const char *));
      APR_ARRAY_PUSH(tmp, const char *) = (opt_state->old_target);
      APR_ARRAY_PUSH(tmp, const char *) = (opt_state->new_target
                                            ? opt_state->new_target
                                           : APR_ARRAY_IDX(tmp, 0,
                                                           const char *));

      SVN_ERR(svn_opt_args_to_target_array2(&tmp2, os, tmp, pool));
      SVN_ERR(svn_opt_parse_path(&old_rev, &old_target,
                                 APR_ARRAY_IDX(tmp2, 0, const char *),
                                 pool));
      if (old_rev.kind != svn_opt_revision_unspecified)
        opt_state->start_revision = old_rev;
      SVN_ERR(svn_opt_parse_path(&new_rev, &new_target,
                                 APR_ARRAY_IDX(tmp2, 1, const char *),
                                 pool));
      if (new_rev.kind != svn_opt_revision_unspecified)
        opt_state->end_revision = new_rev;

      if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
        opt_state->start_revision.kind = svn_path_is_url(old_target)
          ? svn_opt_revision_head : svn_opt_revision_base;
      
      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        opt_state->end_revision.kind = svn_path_is_url(new_target)
          ? svn_opt_revision_head : svn_opt_revision_working;
    }
  else if (opt_state->new_target)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("'--new' option only valid with "
                                "'--old' option"));
    }
  else
    {
      svn_boolean_t working_copy_present = FALSE, url_present = FALSE;
      
      /* The 'svn diff [-r N[:M]] [TARGET[@REV]...]' case matches. */

      /* Here each target is a pegged object. Find out the starting
         and ending paths for each target. */

      svn_opt_push_implicit_dot_target(targets, pool);

      old_target = "";
      new_target = "";

      /* Check to see if at least one of our paths is a working copy
         path. */
      for (i = 0; i < targets->nelts; ++i)
        {
          const char *path = APR_ARRAY_IDX(targets, i, const char *);
          if (! svn_path_is_url(path))
            working_copy_present = TRUE;
          else
            url_present = TRUE;
        }

      if (url_present && working_copy_present)
        return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                 _("Target lists to diff may not contain "
                                   "both working copy paths and URLs"));
          
      if (opt_state->start_revision.kind == svn_opt_revision_unspecified
          && working_copy_present)
          opt_state->start_revision.kind = svn_opt_revision_base;
      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        opt_state->end_revision.kind = working_copy_present
          ? svn_opt_revision_working : svn_opt_revision_head;

      /* Determine if we need to do pegged diffs. */
      if ((opt_state->start_revision.kind != svn_opt_revision_base
           && opt_state->start_revision.kind != svn_opt_revision_working)
          || (opt_state->end_revision.kind != svn_opt_revision_base
              && opt_state->end_revision.kind != svn_opt_revision_working))
        pegged_diff = TRUE;

    }

  svn_opt_push_implicit_dot_target(targets, pool);

  iterpool = svn_pool_create(pool);
  for (i = 0; i < targets->nelts; ++i)
    {
      const char *path = APR_ARRAY_IDX(targets, i, const char *);
      const char *target1, *target2;

      svn_pool_clear(iterpool);
      if (! pegged_diff)
        {
          target1 = svn_path_join(old_target, path, iterpool);
          target2 = svn_path_join(new_target, path, iterpool);

          if (opt_state->summarize)
            SVN_ERR(svn_client_diff_summarize
                    (target1,
                     &opt_state->start_revision,
                     target2,
                     &opt_state->end_revision,
                     opt_state->nonrecursive ? FALSE : TRUE,
                     opt_state->notice_ancestry ? FALSE : TRUE,
                     summarize_func,
                     (void *) target1,
                     ((svn_cl__cmd_baton_t *)baton)->ctx,
                     iterpool));
          else          
            SVN_ERR(svn_client_diff3
                    (options,
                     target1,
                     &(opt_state->start_revision),
                     target2,
                     &(opt_state->end_revision),
                     opt_state->nonrecursive ? FALSE : TRUE,
                     opt_state->notice_ancestry ? FALSE : TRUE,
                     opt_state->no_diff_deleted,
                     opt_state->force,
                     svn_cmdline_output_encoding(pool),
                     outfile,
                     errfile,
                     ((svn_cl__cmd_baton_t *)baton)->ctx,
                     iterpool));
        }
      else
        {
          const char *truepath;
          svn_opt_revision_t peg_revision;
          
          /* First check for a peg revision. */
          SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, path, 
                                     iterpool));

          /* Set the default peg revision if one was not specified. */
          if (peg_revision.kind == svn_opt_revision_unspecified)
            peg_revision.kind = svn_path_is_url(path)
              ? svn_opt_revision_head : svn_opt_revision_working;

          if (opt_state->summarize)
            SVN_ERR(svn_client_diff_summarize_peg
                    (truepath,
                     &peg_revision,
                     &opt_state->start_revision,
                     &opt_state->end_revision,
                     opt_state->nonrecursive ? FALSE : TRUE,
                     opt_state->notice_ancestry ? FALSE : TRUE,
                     summarize_func,
                     (void *) truepath,
                     ((svn_cl__cmd_baton_t *)baton)->ctx,
                     iterpool));
          else
            SVN_ERR(svn_client_diff_peg3
                    (options,
                     truepath,
                     &peg_revision,
                     &opt_state->start_revision,
                     &opt_state->end_revision,
                     opt_state->nonrecursive ? FALSE : TRUE,
                     opt_state->notice_ancestry ? FALSE : TRUE,
                     opt_state->no_diff_deleted,
                     opt_state->force,
                     svn_cmdline_output_encoding(pool),
                     outfile,
                     errfile,
                     ((svn_cl__cmd_baton_t *)baton)->ctx,
                     iterpool));
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
