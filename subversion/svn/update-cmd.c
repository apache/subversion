/*
 * update-cmd.c -- Bring work tree in sync with repository
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#include "svn_pools.h"
#include "svn_client.h"
#include "svn_path.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__update(apr_getopt_t *os,
               void *baton,
               apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  svn_depth_t depth;
  svn_boolean_t depth_is_sticky;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, pool));

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target(targets, pool);

  SVN_ERR(svn_opt_eat_peg_revisions(&targets, targets, pool));

  /* If using changelists, convert targets into a set of paths that
     match the specified changelist(s). */
  if (opt_state->changelists)
    {
      svn_depth_t cl_depth = opt_state->depth;
      if (cl_depth == svn_depth_unknown)
        cl_depth = svn_depth_infinity;
      SVN_ERR(svn_cl__changelist_paths(&targets,
                                       opt_state->changelists, targets,
                                       cl_depth, ctx, pool));
    }

  if (! opt_state->quiet)
    svn_cl__get_notifier(&ctx->notify_func2, &ctx->notify_baton2,
                         FALSE, FALSE, FALSE, pool);

  /* Deal with depthstuffs. */
  if (opt_state->set_depth != svn_depth_unknown)
    {
      depth = opt_state->set_depth;
      depth_is_sticky = TRUE;
    }
  else
    {
      depth = opt_state->depth;
      depth_is_sticky = FALSE;
    }

  SVN_ERR(svn_client_update3(NULL, targets,
                             &(opt_state->start_revision),
                             depth, depth_is_sticky,
                             opt_state->ignore_externals,
                             opt_state->force,
                             ctx, pool));

  if (! opt_state->quiet)
    SVN_ERR(svn_cl__print_conflict_stats(ctx->notify_baton2, pool));
  
  return SVN_NO_ERROR;
}
