/*
 * commit-cmd.c -- Check changes into the repository.
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

#include <apr_general.h>
#include <apr_file_info.h>
#include <apr_lib.h>

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_sorts.h"
#include "cl.h"

#include "client_errors.h"



/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__commit (apr_getopt_t *os,
                void *baton,
                apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = baton;
  apr_array_header_t *targets;
  apr_array_header_t *condensed_targets;
  const char *base_dir;
  svn_client_auth_baton_t *auth_baton;
  svn_client_commit_info_t *commit_info = NULL;
  svn_wc_notify_func_t notify_func = NULL;
  void *notify_baton = NULL;
  void *log_msg_baton;

  SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         FALSE, pool));

  /* Build an authentication object to give to libsvn_client. */
  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  /* Add "." if user passed 0 arguments. */
  svn_opt_push_implicit_dot_target (targets, pool);

  /* Condense the targets (like commit does)... */
  SVN_ERR (svn_path_condense_targets (&base_dir,
                                      &condensed_targets,
                                      targets,
                                      pool));

  if ((! condensed_targets) || (! condensed_targets->nelts))
    {
      const char *parent_dir, *base_name;

      SVN_ERR (svn_wc_get_actual_target (base_dir, &parent_dir, 
                                         &base_name, pool));
      if (base_name)
        base_dir = apr_pstrdup (pool, parent_dir);
    }

  if (! opt_state->quiet)
    svn_cl__get_notifier (&notify_func, &notify_baton, FALSE, FALSE, pool);

  /* Commit. */
  log_msg_baton = svn_cl__make_log_msg_baton (opt_state, base_dir, pool);
  SVN_ERR (svn_cl__cleanup_log_msg
           (log_msg_baton, svn_client_commit (&commit_info,
                                              notify_func, notify_baton,
                                              auth_baton,
                                              targets,
                                              svn_cl__get_log_message,
                                              log_msg_baton,
                                              opt_state->nonrecursive,
                                              pool)));
  if (commit_info && ! opt_state->quiet)
    svn_cl__print_commit_info (commit_info);

  return SVN_NO_ERROR;
}
