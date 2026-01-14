/*
 * upgrade-cmd.c -- Upgrade a working copy.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
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
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_version.h"
#include "private/svn_subr_private.h"
#include "cl.h"
#include "svn_private_config.h"


/*** Code. ***/


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__upgrade(apr_getopt_t *os,
                void *baton,
                apr_pool_t *scratch_pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  apr_pool_t *iterpool;
  int i;
  const svn_version_t *latest_version;

  latest_version = svn_client_latest_wc_version(scratch_pool);

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE,
                                                      scratch_pool));

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target(targets, scratch_pool);

  SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, scratch_pool));

  SVN_ERR(svn_cl__check_targets_are_local_paths(targets));

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      const svn_version_t *result_format_version;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));
      SVN_ERR(svn_client_upgrade2(&result_format_version, target,
                                  opt_state->compatible_version,
                                  ctx, iterpool, iterpool));

      /* Remind the user they can upgrade further if:
       *   - the user did not specify compatible-version explicitly
       *   - a higher version is available. */
      if (! opt_state->compatible_version
          && ! svn_version__at_least(result_format_version,
                                     latest_version->major,
                                     latest_version->minor, 0)
          && ! opt_state->quiet)
        {
          SVN_ERR(svn_cmdline_printf(
                    iterpool,
                    _("svn: The target working copy '%s' is at version %d.%d; "
                      "the highest version supported by this client can be "
                      "specified with '--compatible-version=%d.%d'.\n"),
                    svn_dirent_local_style(target, iterpool),
                    result_format_version->major, result_format_version->minor,
                    latest_version->major, latest_version->minor));
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
