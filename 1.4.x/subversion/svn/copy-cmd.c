/*
 * copy-cmd.c -- Subversion copy command
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
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__copy(apr_getopt_t *os,
             void *baton,
             apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  const char *src_path, *dst_path;
  svn_boolean_t src_is_url, dst_is_url;
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;

  SVN_ERR(svn_opt_args_to_target_array2(&targets, os, 
                                        opt_state->targets, pool));
  if (targets->nelts < 2)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
  if (targets->nelts > 2)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  src_path = ((const char **) (targets->elts))[0];
  dst_path = ((const char **) (targets->elts))[1];

  /* Figure out which type of trace editor to use. */
  src_is_url = svn_path_is_url(src_path);
  dst_is_url = svn_path_is_url(dst_path);

  if ((! src_is_url) && (! dst_is_url))
    {
      /* WC->WC */
      if (! opt_state->quiet)
        svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2,
                             FALSE, FALSE, FALSE, pool);
    }
  else if ((! src_is_url) && (dst_is_url))
    {
      /* WC->URL : Use notification. */
      /* ### todo:
         
         We'd like to use the notifier, but we MAY have a couple of
         problems with that, the same problems that used to apply to
         the old trace_editor:
         
         1) We don't know where the commit editor for this case will
            be anchored with respect to the repository, so we can't
            use the DST_URL.

         2) While we do know where the commit editor will be driven
            from with respect to our working copy, we don't know what
            basenames will be chosen for our committed things.  So a
            copy of dir1/foo.c to http://.../dir2/foo-copy-c would
            display like: "Adding   dir1/foo-copy.c", which could be a
            bogus path. 
      */
    }
  else if ((src_is_url) && (! dst_is_url))
    {
      /* URL->WC : Use checkout-style notification. */
      if (! opt_state->quiet)
        svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, TRUE,
                             FALSE, FALSE, pool);
    }
  else
    /* URL->URL : No notification needed. */
    ;

  if (! dst_is_url)
    {
      ctx->log_msg_func2 = NULL;
      if (opt_state->message || opt_state->filedata)
        return svn_error_create
          (SVN_ERR_CL_UNNECESSARY_LOG_MESSAGE, NULL,
           _("Local, non-commit operations do not take a log message"));
    }

  if (ctx->log_msg_func2)
    SVN_ERR(svn_cl__make_log_msg_baton(&(ctx->log_msg_baton2), opt_state,
                                       NULL, ctx->config, pool));

  err = svn_client_copy3(&commit_info, src_path,
                         &(opt_state->start_revision),
                         dst_path, ctx, pool);

  /* If dst_path already exists, try to copy src_path as a child of it. */
  if (err && (err->apr_err == SVN_ERR_ENTRY_EXISTS
              || err->apr_err == SVN_ERR_FS_ALREADY_EXISTS))
    {
      const char *src_basename = svn_path_basename(src_path, pool);

      svn_error_clear(err);
      
      err = svn_client_copy3(&commit_info, src_path,
                             &(opt_state->start_revision),
                             svn_path_join(dst_path, src_basename, pool),
                             ctx, pool);
    }

  if (ctx->log_msg_func2)
    SVN_ERR(svn_cl__cleanup_log_msg(ctx->log_msg_baton2, err));
  else if (err)
    return err;

  if (commit_info && ! opt_state->quiet)
    SVN_ERR(svn_cl__print_commit_info(commit_info, pool));

  return SVN_NO_ERROR;
}
