/*
 * copy-cmd.c -- Subversion copy command
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
#include "cl.h"

#include "svn_private_config.h"
#include "private/svn_opt_private.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__copy(apr_getopt_t *os,
             void *baton,
             apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets, *sources;
  const char *src_path, *dst_path;
  svn_boolean_t srcs_are_urls, dst_is_url;
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;
  int i;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, pool));
  if (targets->nelts < 2)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  /* Get the src list and associated peg revs */
  sources = apr_array_make(pool, targets->nelts - 1,
                           sizeof(svn_client_copy_source_t *));
  for (i = 0; i < (targets->nelts - 1); i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      svn_client_copy_source_t *source = apr_palloc(pool, sizeof(*source));
      const char *src;
      svn_opt_revision_t *peg_revision = apr_palloc(pool,
                                                    sizeof(*peg_revision));

      SVN_ERR(svn_opt_parse_path(peg_revision, &src, target, pool));
      source->path = src;
      source->revision = &(opt_state->start_revision);
      source->peg_revision = peg_revision;

      APR_ARRAY_PUSH(sources, svn_client_copy_source_t *) = source;
    }

  SVN_ERR(svn_opt__eat_peg_revisions(&targets, targets, pool));

  /* Figure out which type of trace editor to use.
     If the src_paths are not homogeneous, setup_copy will return an error. */
  src_path = APR_ARRAY_IDX(targets, 0, const char *);
  srcs_are_urls = svn_path_is_url(src_path);
  dst_path = APR_ARRAY_IDX(targets, targets->nelts - 1, const char *);
  apr_array_pop(targets);
  dst_is_url = svn_path_is_url(dst_path);

  if ((! srcs_are_urls) && (! dst_is_url))
    {
      /* WC->WC */
      if (! opt_state->quiet)
        svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2,
                             FALSE, FALSE, FALSE, pool);
    }
  else if ((! srcs_are_urls) && (dst_is_url))
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
  else if ((srcs_are_urls) && (! dst_is_url))
    {
      /* URL->WC : Use checkout-style notification. */
      if (! opt_state->quiet)
        svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, TRUE,
                             FALSE, FALSE, pool);
    }
  /* else URL -> URL, meaning that no notification is needed. */

  if (! dst_is_url)
    {
      ctx->log_msg_func3 = NULL;
      if (opt_state->message || opt_state->filedata || opt_state->revprop_table)
        return svn_error_create
          (SVN_ERR_CL_UNNECESSARY_LOG_MESSAGE, NULL,
           _("Local, non-commit operations do not take a log message "
             "or revision properties"));
    }

  if (ctx->log_msg_func3)
    SVN_ERR(svn_cl__make_log_msg_baton(&(ctx->log_msg_baton3), opt_state,
                                       NULL, ctx->config, pool));

  err = svn_client_copy5(&commit_info, sources, dst_path, TRUE,
                         opt_state->parents, opt_state->ignore_externals,
                         opt_state->revprop_table, ctx, pool);

  if (ctx->log_msg_func3)
    SVN_ERR(svn_cl__cleanup_log_msg(ctx->log_msg_baton3, err, pool));
  else if (err)
    return err;

  if (commit_info && ! opt_state->quiet)
    SVN_ERR(svn_cl__print_commit_info(commit_info, pool));

  return SVN_NO_ERROR;
}
