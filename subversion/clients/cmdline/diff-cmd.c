/*
 * diff-cmd.c -- Display context diff of a file
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
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_types.h"
#include "cl.h"


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
  int i;

  options = svn_cstring_split (opt_state->extensions, " \t\n\r", TRUE, pool);

  /* Get an apr_file_t representing stdout and stderr, which is where
     we'll have the external 'diff' program print to. */
  if ((status = apr_file_open_stdout (&outfile, pool)))
    return svn_error_create (status, NULL, "can't open stdout");
  if ((status = apr_file_open_stderr (&errfile, pool)))
    return svn_error_create (status, NULL, "can't open stderr");

  if (! opt_state->old_target && ! opt_state->new_target
      && opt_state->start_revision.kind != svn_opt_revision_unspecified
      && opt_state->end_revision.kind != svn_opt_revision_unspecified
      && (os->argc - os->ind == 1)
      && svn_path_is_url (os->argv[os->ind]))
    {
      /* The 'svn diff -rN:M URL' case (matches 'svn merge'). */
      SVN_ERR (svn_opt_args_to_target_array (&targets, os,
                                             opt_state->targets,
                                             &(opt_state->start_revision),
                                             &(opt_state->end_revision),
                                             FALSE, /* no @revs */ pool));

      old_target = new_target = APR_ARRAY_IDX(targets, 0, const char *);
      targets->nelts = 0;
    }
  else if (! opt_state->old_target && ! opt_state->new_target
           && (os->argc - os->ind == 2)
           && svn_path_is_url (os->argv[os->ind])
           && svn_path_is_url (os->argv[os->ind + 1]))
    {
      /* The 'svn diff URL1[@N] URL2[@M]' case (matches 'svn merge'). */
      SVN_ERR (svn_opt_args_to_target_array (&targets, os,
                                             opt_state->targets,
                                             &(opt_state->start_revision),
                                             &(opt_state->end_revision),
                                             TRUE, /* extract @revs */ pool));

      old_target = APR_ARRAY_IDX(targets, 0, const char *);
      new_target = APR_ARRAY_IDX(targets, 1, const char *);
      targets->nelts = 0;

      if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
        opt_state->start_revision.kind = svn_opt_revision_head;
      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        opt_state->end_revision.kind = svn_opt_revision_head;
    }
  else
    {
      /* The 'svn diff [-rN[:M]] [--old OLD] [--new NEW] [PATH ...]' case */
      apr_array_header_t *tmp = apr_array_make (pool, 2, sizeof (const char *));
      apr_array_header_t *tmp2;

      SVN_ERR (svn_opt_args_to_target_array (&targets, os,
                                             opt_state->targets,
                                             &(opt_state->start_revision),
                                             &(opt_state->end_revision),
                                             FALSE, /* no @revs */ pool));

      APR_ARRAY_PUSH (tmp, const char *) = (opt_state->old_target
                                            ? opt_state->old_target : ".");
      APR_ARRAY_PUSH (tmp, const char *) = (opt_state->new_target
                                            ? opt_state->new_target
                                            :  APR_ARRAY_IDX(tmp, 0,
                                                             const char *));

      SVN_ERR (svn_opt_args_to_target_array (&tmp2, os, tmp,
                                             &(opt_state->start_revision),
                                             &(opt_state->end_revision),
                                             TRUE, /* extract @revs */ pool));

      old_target = APR_ARRAY_IDX(tmp2, 0, const char *);
      new_target = APR_ARRAY_IDX(tmp2, 1, const char *);

      /* Default to HEAD for an URL, BASE otherwise */
      if (opt_state->start_revision.kind == svn_opt_revision_unspecified)
        opt_state->start_revision.kind = (svn_path_is_url (old_target)
                                          ? svn_opt_revision_head
                                          : svn_opt_revision_base);

      /* Default to HEAD for an URL, WORKING otherwise */
      if (opt_state->end_revision.kind == svn_opt_revision_unspecified)
        opt_state->end_revision.kind = (svn_path_is_url (new_target)
                                        ? svn_opt_revision_head
                                        : svn_opt_revision_working);
    }

  svn_opt_push_implicit_dot_target (targets, pool);

  subpool = svn_pool_create (pool);
  for (i = 0; i < targets->nelts; ++i)
    {
      const char *path = APR_ARRAY_IDX(targets, i, const char *);
      const char *target1, *target2;

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
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}
