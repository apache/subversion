/*
 * util.c: Subversion command line client utility functions. Any
 * functions that need to be shared across subcommands should be put
 * in here.
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

#include <apr_errno.h>

#include "svn_error.h"
#include "svn_path.h"

#include "mergeinfo-normalizer.h"

#include "svn_private_config.h"



static svn_error_t *
check_target_is_local_path(const char *target)
{
  if (svn_path_is_url(target))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("'%s' is not a local path"), target);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_min__add_wc_info(svn_min__cmd_baton_t *baton,
                     int idx,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_min__opt_state_t *opt_state = baton->opt_state;
  const char *target = APR_ARRAY_IDX(opt_state->targets, idx, const char *);
  const char *truepath;
  svn_opt_revision_t peg_revision;

  SVN_ERR(check_target_is_local_path(target));

  SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target,
                             scratch_pool));
  SVN_ERR(svn_dirent_get_absolute(&baton->local_abspath, truepath,
                                  result_pool));

  SVN_ERR(svn_client_get_wc_root(&baton->wc_root, baton->local_abspath,
                                 baton->ctx, result_pool, scratch_pool));
  SVN_ERR(svn_client_get_repos_root(&baton->repo_root, NULL,
                                    baton->local_abspath, baton->ctx,
                                    result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

