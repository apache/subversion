/*
 * changepassword-cmd.c -- Associate (or deassociate) a master password
 *                         with the local authentication credential cache.
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

#include "svn_client.h"
#include "svn_error.h"
#include "svn_auth.h"

#include "cl.h"




/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__changepassword(apr_getopt_t *os,
                       void *baton,
                       apr_pool_t *pool)
{
  svn_cl__cmd_baton_t *cmd_baton = baton;
  svn_cl__opt_state_t *opt_state = cmd_baton->opt_state;
  svn_auth_baton_t *auth_baton = cmd_baton->ctx->auth_baton;
  apr_array_header_t *args;
  const char *new_password = NULL;

  SVN_ERR(svn_opt_parse_all_args(&args, os, pool));

  /* If we're not removing changepasswords, then our only argument should
     be the new password; otherwise, there should be no arguments. */
  if (! opt_state->remove)
    {
      if (args->nelts < 1)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
      if (args->nelts > 1)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);
      new_password = APR_ARRAY_IDX(args, 0, const char *);
    }
  else
    {
      if (args->nelts > 0)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);
    }

  SVN_ERR(svn_auth_master_passphrase_set(auth_baton, new_password, pool));
  return SVN_NO_ERROR;
}
