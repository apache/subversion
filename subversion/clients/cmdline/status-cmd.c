/*
 * status-cmd.c -- Display status information in current directory
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
#include "svn_pools.h"
#include "cl.h"


/*** Code. ***/

struct status_baton
{
  /* These fields all correspond to the ones in the
     svn_cl__print_status() interface. */
  svn_boolean_t detailed;
  svn_boolean_t show_last_committed;
  svn_boolean_t skip_unrecognized;
  apr_pool_t *pool;
};


/* A status callback function for printing STATUS for PATH. */
static void
print_status (void *baton,
              const char *path,
              svn_wc_status_t *status)
{
  struct status_baton *sb = baton;
  svn_cl__print_status (path, status, sb->detailed, sb->show_last_committed,
                        sb->skip_unrecognized, sb->pool);
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__status (apr_getopt_t *os,
                void *baton,
                apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  apr_pool_t * subpool;
  int i;
  svn_revnum_t youngest = SVN_INVALID_REVNUM;
  svn_opt_revision_t rev;
  struct status_baton sb;

  SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         FALSE, pool));

  /* We want our -u statuses to be against HEAD. */
  rev.kind = svn_opt_revision_head;

  /* The notification callback. */
  svn_cl__get_notifier (&ctx->notify_func, &ctx->notify_baton, FALSE, FALSE, 
                        FALSE, pool);

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target(targets, pool);

  subpool = svn_pool_create (pool);

  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = ((const char **) (targets->elts))[i];

      /* Retrieve a hash of status structures with the information
         requested by the user. */

      sb.detailed = (opt_state->verbose || opt_state->update);
      sb.show_last_committed = opt_state->verbose;
      sb.skip_unrecognized = opt_state->quiet;
      sb.pool = subpool;
      SVN_ERR (svn_client_status (&youngest, target, &rev,
                                  print_status, &sb,
                                  opt_state->nonrecursive ? FALSE : TRUE,
                                  opt_state->verbose,
                                  opt_state->update,
                                  opt_state->no_ignore,
                                  ctx, subpool));

      SVN_ERR (svn_cl__check_cancel (ctx->cancel_baton));
      svn_pool_clear (subpool);
    }

  svn_pool_destroy (subpool);
  
  return SVN_NO_ERROR;
}
