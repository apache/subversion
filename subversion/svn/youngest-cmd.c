/*
 * youngest-cmd.c -- Print the youngest repository revision number
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

#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__youngest(apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets = NULL;
  const char *target_path = NULL; /* TARGET path (working copy path or URL) */
  const char *target_url = NULL; /* TARGET URL */
  svn_ra_session_t *session;
  svn_revnum_t latest_revision = SVN_INVALID_REVNUM;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target(targets, pool);

  /* We want exactly 0 or 1 targets for this subcommand. */
  if (targets->nelts > 1)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  /* Ensure that we have a URL to work with.. */
  target_path = APR_ARRAY_IDX(targets, 0, const char *);
  if (svn_path_is_url(target_path))
    {
      target_url = target_path;
    }
  else
    {
      SVN_ERR(svn_dirent_get_absolute(&target_path, target_path, pool));
      SVN_ERR(svn_client_url_from_path2(&target_url, target_path, ctx,
                                        pool, pool));
    }

  /* Get HEAD Revision from URL */
  SVN_ERR(svn_client_open_ra_session2(&session, target_url, NULL, ctx,
                                      pool, pool));
  SVN_ERR(svn_ra_get_latest_revnum(session, &latest_revision, pool));
  SVN_ERR(svn_cmdline_printf(pool, "%ld%s", latest_revision,
                             opt_state->no_newline ? "" : "\n"));
  return SVN_NO_ERROR;
}
