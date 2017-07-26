/*
 * checkpoint-cmd.c -- Checkpoint commands.
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
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_hash.h"

#include "cl.h"

#include "svn_private_config.h"
#include "private/svn_sorts_private.h"


/*  */
static svn_error_t *
checkpoint_init(const char *local_abspath,
                svn_client_ctx_t *ctx,
                apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_client_checkpoint_init(local_abspath, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
checkpoint_list(const char *local_abspath,
                svn_boolean_t diffstat,
                svn_client_ctx_t *ctx,
                apr_pool_t *scratch_pool)
{
  apr_array_header_t *checkpoints;
  /*int current_checkpoint_number;*/
  svn_boolean_t first = TRUE;
  int i;

  SVN_ERR(svn_client_checkpoint_list(&checkpoints,
                                     local_abspath,
                                     ctx, scratch_pool, scratch_pool));

  /*SVN_ERR(svn_client_checkpoint_get_current(&current_checkpoint_number,
                                            local_abspath, ctx, scratch_pool));
  current_checkpoint_name = apr_psprintf(scratch_pool, "%d.patch",
                                         current_checkpoint_number);*/

  for (i = 0; i < checkpoints->nelts; i++)
    {
      svn_log_entry_t *log_entry = APR_ARRAY_IDX(checkpoints, i, void *);
      int number = (log_entry->revision - 1);
      const char *name = apr_psprintf(scratch_pool, "%d", number);
      char marker = (first ? '*' : ' ');
      svn_string_t *date = svn_hash_gets(log_entry->revprops, "svn:date");

      printf("%c %s %.16s\n",
             marker, name, date ? date->data : "");

      if (diffstat)
        {
          printf("\n");
        }
      first = FALSE;
    }

  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
checkpoint_save(/*const apr_array_header_t *paths,
                svn_depth_t depth,
                const apr_array_header_t *changelists,*/
                svn_boolean_t quiet,
                const char *local_abspath,
                svn_client_ctx_t *ctx,
                apr_pool_t *scratch_pool)
{
  int checkpoint_number;

  SVN_ERR(svn_client_checkpoint_save(&checkpoint_number,
                                     /*paths, depth, changelists,*/
                                     local_abspath, ctx, scratch_pool));
  if (! quiet)
    {
      if (checkpoint_number >= 0)
        SVN_ERR(svn_cmdline_printf(scratch_pool, "saved checkpoint %d\n",
                                   checkpoint_number));
      else
        SVN_ERR(svn_cmdline_printf(scratch_pool, "no changes\n"));
    }

  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
checkpoint_squash(/*const char *arg,
                    svn_boolean_t dry_run,
                    svn_boolean_t quiet,*/
                  const char *local_abspath,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_client_checkpoint_squash(local_abspath, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
checkpoint_revert(const char *arg,
                  svn_boolean_t dry_run,
                  svn_boolean_t quiet,
                  const char *local_abspath,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *scratch_pool)
{
  int old_checkpoint_number;
  int new_checkpoint_number;

  SVN_ERR(svn_client_checkpoint_get_current(&old_checkpoint_number,
                                            local_abspath, ctx, scratch_pool));
  if (arg)
    {
      SVN_ERR(svn_cstring_atoi(&new_checkpoint_number, arg));
    }
  else
    {
      new_checkpoint_number = old_checkpoint_number;
    }

  SVN_ERR(svn_client_checkpoint_revert(new_checkpoint_number,
                                       local_abspath,
                                       dry_run, ctx, scratch_pool));
#if 0
  int i;
  /* Delete any newer checkpoints */
  for (i = old_checkpoint_number; i > new_checkpoint_number; i--)
    {
      SVN_ERR(svn_client_checkpoint_delete(i, local_abspath,
                                           dry_run, ctx, scratch_pool));
      if (! quiet)
        SVN_ERR(svn_cmdline_printf(scratch_pool, "deleted checkpoint %d\n",
                                   i));
    }
#endif

  if (!quiet)
    SVN_ERR(svn_cmdline_printf(scratch_pool, "reverted to checkpoint %d\n",
                               new_checkpoint_number));

  return SVN_NO_ERROR;
}

/* First argument should be the subsubcommand. */
static svn_error_t *
get_subsubcommand(const char **subsubcommand,
                  apr_getopt_t *os,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  apr_array_header_t *args;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, scratch_pool));
  SVN_ERR(svn_utf_cstring_to_utf8(subsubcommand,
                                  APR_ARRAY_IDX(args, 0, const char *),
                                  result_pool));
  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__checkpoint(apr_getopt_t *os,
                   void *baton,
                   apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *subsubcommand;
  apr_array_header_t *targets;
  const char *local_abspath;

  SVN_ERR(get_subsubcommand(&subsubcommand, os, pool, pool));

  /* Parse the remaining arguments as paths. */
  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "", pool));

  if (opt_state->quiet)
    ctx->notify_func2 = NULL;

  if (strcmp(subsubcommand, "init") == 0)
    {
      if (targets->nelts)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Too many arguments"));

      SVN_ERR(checkpoint_init(local_abspath, ctx, pool));
    }
  else if (strcmp(subsubcommand, "list") == 0
           || strcmp(subsubcommand, "log") == 0)
    {
      if (targets->nelts)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Too many arguments"));

      SVN_ERR(checkpoint_list(local_abspath,
                              ! opt_state->quiet /*diffstat*/,
                              ctx, pool));
    }
  else if (strcmp(subsubcommand, "save") == 0)
    {
      if (targets->nelts)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Too many arguments"));

      /* ### TODO: Semantics for checkpointing selected paths.
      svn_depth_t depth = opt_state->depth;

      if (! targets->nelts)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL, NULL);

      SVN_ERR(svn_cl__check_targets_are_local_paths(targets));

      if (depth == svn_depth_unknown)
        depth = svn_depth_infinity;

      SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, pool));
      */

      SVN_ERR(checkpoint_save(/*targets, depth, opt_state->changelists,*/
                              opt_state->quiet, local_abspath, ctx, pool));
    }
  else if (strcmp(subsubcommand, "squash") == 0)
    {
      if (targets->nelts)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Too many arguments"));

      SVN_ERR(checkpoint_squash(/*NULL, opt_state->dry_run, opt_state->quiet,*/
                                local_abspath, ctx, pool));
    }
  else if (strcmp(subsubcommand, "revert") == 0)
    {
      if (targets->nelts)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Too many arguments"));

      SVN_ERR(checkpoint_revert(NULL, opt_state->dry_run, opt_state->quiet,
                                local_abspath, ctx, pool));
    }
  else if (strcmp(subsubcommand, "rollback") == 0)
    {
      const char *arg;

      if (targets->nelts != 1)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL, NULL);

      /* Which checkpoint number? */
      arg = APR_ARRAY_IDX(targets, 0, char *);

      SVN_ERR(checkpoint_revert(arg, opt_state->dry_run, opt_state->quiet,
                                local_abspath, ctx, pool));
    }
  else if (strcmp(subsubcommand, "finish") == 0
           || strcmp(subsubcommand, "uninit") == 0)
    {
      if (targets->nelts)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Too many arguments"));

      SVN_ERR(svn_client_checkpoint_uninit(local_abspath, ctx, pool));
    }
  else
    {
      return svn_error_createf(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
                               _("checkpoint: Unknown checkpoint command '%s'; "
                                 "try 'svn help checkpoint'"),
                               subsubcommand);
    }

  return SVN_NO_ERROR;
}
