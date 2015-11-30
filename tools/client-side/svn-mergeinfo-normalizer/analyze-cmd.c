/*
 * analyze-cmd.c -- Print which MI can be elided, which one can not and why
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


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_min__analyze(apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_min__cmd_baton_t *cmd_baton = baton;

  /* If no option is given, default to "remove all you can". */
  if (   !cmd_baton->opt_state->remove_redundants
      && !cmd_baton->opt_state->remove_obsoletes
      && !cmd_baton->opt_state->combine_ranges
      && !cmd_baton->opt_state->remove_redundant_misaligned)
    {
      cmd_baton->opt_state->remove_redundants = TRUE;
      cmd_baton->opt_state->remove_obsoletes = TRUE;
      cmd_baton->opt_state->combine_ranges = TRUE;
      cmd_baton->opt_state->remove_redundant_misaligned = TRUE;
    }

  cmd_baton->opt_state->run_analysis = TRUE;
  cmd_baton->opt_state->dry_run = TRUE;

  SVN_ERR(svn_min__run_normalize(baton, pool));

  return SVN_NO_ERROR;
}
