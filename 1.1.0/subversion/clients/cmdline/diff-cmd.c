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

#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_types.h"
#include "svn_utf.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* An svn_opt_subcommand_t to handle the 'diff' command.
   This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__diff (apr_getopt_t *os,
              void *baton,
              apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  apr_array_header_t *options;
  apr_array_header_t *targets;
  apr_file_t *outfile, *errfile;
  apr_status_t status;
  const char *old_target, *new_target;
  apr_pool_t *subpool;
  svn_boolean_t pegged_diff = FALSE;
  int i;

  /* Fall back to "" to get options initialized either way. */
  {
    const char *optstr = opt_state->extensions ? opt_state->extensions : "";
    options = svn_cstring_split (optstr, " \t\n\r", TRUE, pool);
  }

  /* Get an apr_file_t representing stdout and stderr, which is where
     we'll have the external 'diff' program print to. */
  if ((status = apr_file_open_stdout (&outfile, pool)))
    return svn_error_wrap_apr (status, _("Can't open stdout"));
  if ((status = apr_file_open_stderr (&errfile, pool)))
    return svn_error_wrap_apr (status, _("Can't open stderr"));

  if (! opt_state->old_target && ! opt_state->new_target
      && (os->argc - os->ind == 2)
      && svn_path_is_url (os->argv[os->ind])
      && svn_path_is_url (os->argv[os->ind + 1])
      && opt_state->start_revision.kind == svn_opt_revision_unspecified
      && opt_state->end_revision.kind == svn_opt_revision_unspecified)
    {
      /* The 'svn diff OLD_URL[@OLDREV] NEW_URL[@NEWREV]' case matches. */
      SVN_ERR (svn_opt_args_to_target_array (&targets, os,
                                             opt_state->targets,
                                             &(opt_state->start_revision),
                                             &(opt_state->end_revision),
                                             TRUE, /* extract @revs */ pool));

      old_target = APR_ARRAY_IDX (targets, 0, const char *);
      new_target = APR_ARRAY_IDX (targets, 1, const char *);
      targets->nelts = 0;
      
      if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
        opt_state->start_revision.kind = svn_opt_revision_head;
      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        opt_state->end_revision.kind = svn_opt_revision_head;
    }
  else if (opt_state->old_target)
    {
      apr_array_header_t *tmp, *tmp2;
      
      /* The 'svn diff --old=OLD[@OLDREV] [--new=NEW[@NEWREV]]
         [PATH...]' case matches. */

      SVN_ERR (svn_opt_args_to_target_array (&targets, os,
                                             opt_state->targets,
                                             NULL,
                                             NULL,
                                             FALSE, pool));

      tmp = apr_array_make (pool, 2, sizeof (const char *));
      APR_ARRAY_PUSH (tmp, const char *) = (opt_state->old_target);
      APR_ARRAY_PUSH (tmp, const char *) = (opt_state->new_target
                                            ? opt_state->new_target
                                            : APR_ARRAY_IDX (tmp, 0,
                                                             const char *));

      SVN_ERR (svn_opt_args_to_target_array (&tmp2, os, tmp,
                                             &(opt_state->start_revision),
                                             &(opt_state->end_revision),
                                             TRUE, /* extract @revs */ pool));

      old_target = APR_ARRAY_IDX (tmp2, 0, const char *);
      new_target = APR_ARRAY_IDX (tmp2, 1, const char *);

      if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
        opt_state->start_revision.kind = svn_path_is_url (old_target)
          ? svn_opt_revision_head : svn_opt_revision_base;
      
      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        opt_state->end_revision.kind = svn_path_is_url (new_target)
          ? svn_opt_revision_head : svn_opt_revision_working;
    }
  else
    {
      apr_array_header_t *tmp, *tmp2;
      svn_boolean_t working_copy_present = FALSE, url_present = FALSE;
      
      /* The 'svn diff [-r M[:N]] [TARGET[@REV]...]' case matches. */

      /* Here each target is a pegged object. Find out the starting
         and ending paths for each target. */
      SVN_ERR (svn_opt_args_to_target_array (&targets, os,
                                             opt_state->targets,
                                             NULL,
                                             NULL,
                                             FALSE, pool));

      svn_opt_push_implicit_dot_target (targets, pool);

      tmp = apr_array_make (pool, 2, sizeof (const char *));
      APR_ARRAY_PUSH (tmp, const char *) = ".";
      APR_ARRAY_PUSH (tmp, const char *) = ".";
      
      SVN_ERR (svn_opt_args_to_target_array (&tmp2, os, tmp,
                                             &(opt_state->start_revision),
                                             &(opt_state->end_revision),
                                             TRUE, /* extract @revs */ pool));

      old_target = APR_ARRAY_IDX (tmp2, 0, const char *);
      new_target = APR_ARRAY_IDX (tmp2, 1, const char *);

      /* Check to see if at least one of our paths is a working copy
         path. */
      for (i = 0; i < targets->nelts; ++i)
        {
          const char *path = APR_ARRAY_IDX (targets, i, const char *);
          if (! svn_path_is_url (path))
            working_copy_present = TRUE;
          else
            url_present = TRUE;
        }

      if (url_present && working_copy_present)
        return svn_error_createf (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                  _("Target lists to diff may not contain "
                                    "both working copy paths and URLs"));
          
      if (opt_state->start_revision.kind == svn_opt_revision_unspecified
          && working_copy_present)
          opt_state->start_revision.kind = svn_opt_revision_base;
      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        opt_state->end_revision.kind = working_copy_present
          ? svn_opt_revision_working : svn_opt_revision_head;

      /* Determine if we need to do pegged diffs. */
      if (opt_state->start_revision.kind != svn_opt_revision_base
          && opt_state->start_revision.kind != svn_opt_revision_working)
          pegged_diff = TRUE;

    }

  svn_opt_push_implicit_dot_target (targets, pool);

  subpool = svn_pool_create (pool);
  for (i = 0; i < targets->nelts; ++i)
    {
      const char *path = APR_ARRAY_IDX (targets, i, const char *);
      const char *target1, *target2;

      if (! pegged_diff)
        {
          svn_pool_clear (subpool);
          target1 = svn_path_join (old_target, path, subpool);
          target2 = svn_path_join (new_target, path, subpool);
          
          SVN_ERR (svn_client_diff (options,
                                    target1,
                                    &(opt_state->start_revision),
                                    target2,
                                    &(opt_state->end_revision),
                                    opt_state->nonrecursive ? FALSE : TRUE,
                                    opt_state->notice_ancestry ? FALSE : TRUE,
                                    opt_state->no_diff_deleted,
                                    outfile,
                                    errfile,
                                    ((svn_cl__cmd_baton_t *)baton)->ctx,
                                    pool));
        }
      else
        {
          const char *truepath;
          svn_opt_revision_t peg_revision;
          
          /* First check for a peg revision. */
          SVN_ERR (svn_opt_parse_path (&peg_revision, &truepath, path, pool));

          /* Set the default peg revision if one was not specified. */
          if (peg_revision.kind == svn_opt_revision_unspecified)
            peg_revision.kind = svn_path_is_url (path)
              ? svn_opt_revision_head : svn_opt_revision_working;

          SVN_ERR (svn_client_diff_peg (options,
                                        truepath,
                                        &peg_revision,
                                        &opt_state->start_revision,
                                        &opt_state->end_revision,
                                        opt_state->nonrecursive
                                        ? FALSE : TRUE,
                                        opt_state->notice_ancestry
                                        ? FALSE : TRUE,
                                        opt_state->no_diff_deleted,
                                        outfile,
                                        errfile,
                                        ((svn_cl__cmd_baton_t *)baton)->ctx,
                                        pool));
        }
    }
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}
