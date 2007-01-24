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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_client.h"
#include "svn_error.h"
#include "cl.h"


/*** Code. ***/


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__changelist(apr_getopt_t *os,
                   void *baton,
                   apr_pool_t *pool)
{
  const char *changelist_name;
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  apr_array_header_t *paths;
  int i;

  SVN_ERR(svn_opt_args_to_target_array2(&targets, os,
                                        opt_state->targets, pool));

  if (opt_state->clear)
    {
      if (targets->nelts < 1)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

      changelist_name = NULL;
      paths = targets;
    }
  else
    {
      if (targets->nelts < 2)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

      changelist_name = APR_ARRAY_IDX(targets, 0, const char *);
      paths = apr_array_make(pool, targets->nelts-1, sizeof(const char *));

      for (i = 1; i < targets->nelts; i++)
        APR_ARRAY_PUSH(paths, const char *) = APR_ARRAY_IDX(targets, i,
                                                            const char *);
    }

  svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2, FALSE,
                       FALSE, FALSE, pool);

  SVN_ERR(svn_cl__try
          (svn_client_set_changelist(paths, changelist_name,
                                     ctx, pool),
           NULL, opt_state->quiet,
           SVN_ERR_UNVERSIONED_RESOURCE,
           SVN_ERR_WC_PATH_NOT_FOUND,
           SVN_NO_ERROR));

  return SVN_NO_ERROR;
}
