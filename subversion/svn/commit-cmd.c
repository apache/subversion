/*
 * commit-cmd.c -- Check changes into the repository.
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

#include <apr_general.h>

#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_config.h"
#include "cl.h"

#include "svn_private_config.h"

/* We shouldn't be including a private header here, but it is
 * necessary for fixing issue #3416 */
#include "private/svn_opt_private.h"


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__commit(apr_getopt_t *os,
               void *baton,
               apr_pool_t *pool)
{
  svn_error_t *err;
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  apr_array_header_t *condensed_targets;
  const char *base_dir;
  svn_config_t *cfg;
  svn_boolean_t no_unlock = FALSE;
  svn_commit_info_t *commit_info = NULL;
  int i;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, pool));

  /* Check that no targets are URLs */
  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      if (svn_path_is_url(target))
        return svn_error_create(SVN_ERR_WC_BAD_PATH, NULL,
                                "Must give local path (not URL) as the "
                                "target of a commit");
    }

  /* Add "." if user passed 0 arguments. */
  svn_opt_push_implicit_dot_target(targets, pool);

  SVN_ERR(svn_opt__eat_peg_revisions(&targets, targets, pool));

  /* Condense the targets (like commit does)... */
  SVN_ERR(svn_path_condense_targets(&base_dir,
                                    &condensed_targets,
                                    targets,
                                    TRUE,
                                    pool));

  if ((! condensed_targets) || (! condensed_targets->nelts))
    {
      const char *parent_dir, *base_name;

      SVN_ERR(svn_wc_get_actual_target(base_dir, &parent_dir,
                                       &base_name, pool));
      if (*base_name)
        base_dir = apr_pstrdup(pool, parent_dir);
    }

  if (! opt_state->quiet)
    svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, FALSE,
                         FALSE, FALSE, pool);

  if (opt_state->depth == svn_depth_unknown)
    opt_state->depth = svn_depth_infinity;

  /* Copies are done server-side, and cheaply, which means they're
   * effectively always done with infinite depth.
   * This is a potential cause of confusion for users trying to commit
   * copied subtrees in part by restricting the commit's depth.
   * See issue #3699. */
  if (opt_state->depth < svn_depth_infinity)
    SVN_ERR(svn_cmdline_printf(pool,
                               _("svn: warning: The depth of this commit "
                                 "is '%s', but copied directories will "
                                 "regardless be committed with depth '%s'. "
                                 "You must remove unwanted children of those "
                                 "directories in a separate commit.\n"),
                               svn_depth_to_word(opt_state->depth),
                               svn_depth_to_word(svn_depth_infinity)));

  cfg = apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_CONFIG,
                     APR_HASH_KEY_STRING);
  if (cfg)
    SVN_ERR(svn_config_get_bool(cfg, &no_unlock,
                                SVN_CONFIG_SECTION_MISCELLANY,
                                SVN_CONFIG_OPTION_NO_UNLOCK, FALSE));

  /* We're creating a new log message baton because we can use our base_dir
     to store the temp file, instead of the current working directory.  The
     client might not have write access to their working directory, but they
     better have write access to the directory they're committing.  */
  SVN_ERR(svn_cl__make_log_msg_baton(&(ctx->log_msg_baton3),
                                     opt_state, base_dir,
                                     ctx->config, pool));

  /* Commit. */
  err = svn_client_commit4(&commit_info,
                           targets,
                           opt_state->depth,
                           no_unlock,
                           opt_state->keep_changelists,
                           opt_state->changelists,
                           opt_state->revprop_table,
                           ctx,
                           pool);
  if (err)
    {
      svn_error_t *root_err = svn_error_root_cause(err);
      if (root_err->apr_err == SVN_ERR_UNKNOWN_CHANGELIST)
        {
          /* Strip any errors wrapped around this root cause.  Note
             that this handling differs from that of any other
             commands, because of the way 'commit' internally harvests
             its list of committables. */
          root_err = svn_error_dup(root_err);
          svn_error_clear(err);
          err = root_err;
        }
    }
  SVN_ERR(svn_cl__cleanup_log_msg(ctx->log_msg_baton3, err, pool));
  if (! err && ! opt_state->quiet)
    SVN_ERR(svn_cl__print_commit_info(commit_info, pool));

  return SVN_NO_ERROR;
}
