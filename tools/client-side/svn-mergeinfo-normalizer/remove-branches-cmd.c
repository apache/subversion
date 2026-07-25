/*
 * remove-branches-cmd.c -- Remove specific branch entries from all mergeinfo
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

#include "mergeinfo-normalizer.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_min__remove_branches(apr_getopt_t *os,
                         void *baton,
                         apr_pool_t *pool)
{
  apr_array_header_t *branches;
  svn_min__cmd_baton_t *cmd_baton = baton;

  if (! cmd_baton->opt_state->filedata)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Parameter --file not given"));

  branches = svn_cstring_split(cmd_baton->opt_state->filedata->data,
                               "\n\r", FALSE, pool);

  cmd_baton->opt_state->remove_obsoletes = TRUE;
  cmd_baton->lookup = svn_min__branch_lookup_from_paths(branches, pool);

  SVN_ERR(svn_min__run_normalize(baton, pool));

  return SVN_NO_ERROR;
}
