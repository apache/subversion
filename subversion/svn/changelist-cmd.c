/*
 * changelist-cmd.c -- Associate (or deassociate) a wc path with a changelist.
 *
 * ====================================================================
 * Copyright (c) 2006-2007 CollabNet.  All rights reserved.
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

#include "svn_client.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_utf.h"

#include "cl.h"

#include "svn_private_config.h"




/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__changelist(apr_getopt_t *os,
                   void *baton,
                   apr_pool_t *pool)
{
  const char *changelist_name = NULL;
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  svn_depth_t depth = opt_state->depth;

  /* If we're not removing changelists, then our first argument should
     be the name of a changelist. */

  if (! opt_state->remove)
    {
      apr_array_header_t *args;
      SVN_ERR(svn_opt_parse_num_args(&args, os, 1, pool));
      changelist_name = APR_ARRAY_IDX(args, 0, const char *);
      if (changelist_name[0] == '\0')
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Changelist names must not be empty"));
      SVN_ERR(svn_utf_cstring_to_utf8(&changelist_name,
                                      changelist_name, pool));
    }

  /* Parse the remaining arguments as paths. */
  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, pool));

  /* Changelist has no implicit dot-target `.', so don't you put that
     code here! */
  if (! targets->nelts)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

  if (! opt_state->quiet)
    svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, FALSE,
                         FALSE, FALSE, pool);
  else
    /* FIXME: This is required because svn_client_create_context()
       always initializes ctx->notify_func2 to a wrapper function
       which calls ctx->notify_func() if it isn't NULL.  In other
       words, typically, ctx->notify_func2 is never NULL.  This isn't
       usually a problem, but the changelist logic generates
       svn_error_t's as part of its notification.

       So, svn_wc_set_changelist() checks its notify_func (our
       ctx->notify_func2) for NULL-ness, and seeing non-NULL-ness,
       generates a notificaton object and svn_error_t to describe some
       problem.  It passes that off to its notify_func (our
       ctx->notify_func2) which drops the notification on the floor
       (because it wraps a NULL ctx->notify_func).  But svn_error_t's
       dropped on the floor cause SEGFAULTs at pool cleanup time --
       they need instead to be cleared.

       SOOOooo... we set our ctx->notify_func2 to NULL so the WC code
       doesn't even generate the errors.  */
    ctx->notify_func2 = NULL;

  if (depth == svn_depth_unknown)
    depth = svn_depth_empty;

  if (changelist_name)
    {
      return svn_cl__try
              (svn_client_add_to_changelist(targets, changelist_name,
                                            depth, opt_state->changelists,
                                            ctx, pool),
               NULL, opt_state->quiet,
               SVN_ERR_UNVERSIONED_RESOURCE,
               SVN_ERR_WC_PATH_NOT_FOUND,
               SVN_NO_ERROR);
    }
  else
    {
      return svn_cl__try
              (svn_client_remove_from_changelists(targets, depth,
                                                  opt_state->changelists,
                                                  ctx, pool),
               NULL, opt_state->quiet,
               SVN_ERR_UNVERSIONED_RESOURCE,
               SVN_ERR_WC_PATH_NOT_FOUND,
               SVN_NO_ERROR);
    }
}
