/*
 * import-cmd.c -- Import a file or tree into the repository.
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
#include "cl.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__import (apr_getopt_t *os,
                void *baton,
                apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  const char *path;
  const char *url;
  svn_client_commit_info_t *commit_info = NULL;

  /* Import takes two arguments, for example
   *
   *   $ svn import projects/test file:///home/jrandom/repos/trunk
   *                ^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
   *                 (source)       (repository)
   *
   * or
   *
   *   $ svn import file:///home/jrandom/repos/some/subdir
   *
   * What is the nicest behavior for import, from the user's point of
   * view?  This is a subtle question.  Seemingly intuitive answers
   * can lead to weird situations, such never being able to create
   * non-directories in the top-level of the repository.
   *
   * If 'source' is a file then the basename of 'url' is used as the
   * filename in the repository.  If 'source' is a directory then the
   * import happens directly in the repository target dir, creating
   * however many new entries are necessary.  If some part of 'url'
   * does not exist in the repository then parent directories are created
   * as necessary.
   *
   * In the case where no 'source' is given '.' (the current directory)
   * is implied.
   *
   * ### kff todo: review above behaviors.
   */

  SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         FALSE, pool));

  if (targets->nelts < 1)
    return svn_error_create
      (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
       "repository url required when importing");
  else if (targets->nelts > 2)
    return svn_error_create
      (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
       "too many arguments to import command");
  else if (targets->nelts == 1)
    {
      url = ((const char **) (targets->elts))[0];
      path = "";
    }
  else
    {
      path = ((const char **) (targets->elts))[0];
      url = ((const char **) (targets->elts))[1];
    }

  if (! svn_path_is_url (url))
    return svn_error_createf
      (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
       "invalid url '%s'", url);

  if (! opt_state->quiet)
    svn_cl__get_notifier (&ctx->notify_func, &ctx->notify_baton,
                          FALSE, FALSE, FALSE, pool);

  SVN_ERR (svn_cl__make_log_msg_baton (&(ctx->log_msg_baton), opt_state, 
                                       NULL, ctx->config, pool));
  SVN_ERR (svn_cl__cleanup_log_msg 
           (ctx->log_msg_baton, svn_client_import (&commit_info,
                                                   path,
                                                   url,
                                                   opt_state->nonrecursive,
                                                   ctx,
                                                   pool)));

  if (commit_info && ! opt_state->quiet)
    svn_cl__print_commit_info (commit_info);

  return SVN_NO_ERROR;
}
