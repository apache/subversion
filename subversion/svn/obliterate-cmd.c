/*
 * obliterate-cmd.c -- Subversion command to permanently delete history.
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
#include "svn_path.h"
#include "svn_client.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "cl.h"
#include "svn_private_config.h"


/*** Code. ***/


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__obliterate(apr_getopt_t *os,
                   void *baton,
                   apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  svn_opt_revision_t rev;
  svn_revnum_t revnum;
  const char *url;
  const char *path;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, pool));

  /* Parse the argument into TRUEPATH and REVNUM. */
  if (targets->nelts != 1)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Wrong number of arguments"));
  path = APR_ARRAY_IDX(targets, 0, const char *);
  SVN_ERR(svn_opt_parse_path(&rev, &url, path, pool));
  if (rev.kind != svn_opt_revision_number)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Target must specify the revision as a number"));
  if (! svn_path_is_url(url))
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Target must specify a URL"));
  revnum = rev.value.number;

  SVN_ERR(svn_client__obliterate(url, revnum, ctx, pool));

  return SVN_NO_ERROR;
}
