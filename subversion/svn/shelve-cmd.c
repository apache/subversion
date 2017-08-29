/*
 * shelve-cmd.c -- Shelve commands.
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

#include "cl.h"

#include "svn_private_config.h"


/* First argument should be the name of a shelved change. */
static svn_error_t *
get_name(const char **name,
         apr_getopt_t *os,
         apr_pool_t *result_pool,
         apr_pool_t *scratch_pool)
{
  apr_array_header_t *args;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, scratch_pool));
  SVN_ERR(svn_utf_cstring_to_utf8(name,
                                  APR_ARRAY_IDX(args, 0, const char *),
                                  result_pool));
  return SVN_NO_ERROR;
}

/* ### Currently just reads the first line.
 */
static svn_error_t *
read_logmsg_from_patch(const char **logmsg,
                       const char *patch_abspath,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  apr_file_t *file;
  svn_stream_t *stream;
  svn_boolean_t eof;
  svn_stringbuf_t *line;

  SVN_ERR(svn_io_file_open(&file, patch_abspath,
                           APR_FOPEN_READ, APR_FPROT_OS_DEFAULT, scratch_pool));
  stream = svn_stream_from_aprfile2(file, FALSE /*disown*/, scratch_pool);
  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  SVN_ERR(svn_stream_close(stream));
  *logmsg = line->data;
  return SVN_NO_ERROR;
}

/* Display a list of shelves */
static svn_error_t *
shelves_list(const char *local_abspath,
             svn_boolean_t diffstat,
             svn_client_ctx_t *ctx,
             apr_pool_t *scratch_pool)
{
  apr_hash_t *dirents;
  apr_hash_index_t *hi;

  SVN_ERR(svn_client_shelves_list(&dirents, local_abspath,
                                  ctx, scratch_pool, scratch_pool));

  for (hi = apr_hash_first(scratch_pool, dirents); hi; hi = apr_hash_next(hi))
    {
      const char *name = apr_hash_this_key(hi);
      svn_io_dirent2_t *dirent = apr_hash_this_val(hi);
      int age = (apr_time_now() - dirent->mtime) / 1000000 / 60;
      const char *patch_abspath;
      const char *logmsg;

      if (! strstr(name, ".patch"))
        continue;

      patch_abspath = svn_dirent_join_many(scratch_pool,
                                           local_abspath, ".svn", "shelves", name,
                                           SVN_VA_NULL);
      SVN_ERR(read_logmsg_from_patch(&logmsg, patch_abspath,
                                     scratch_pool, scratch_pool));
      printf("%-30s %6d mins old %10ld bytes\n",
             name, age, (long)dirent->filesize);
      printf(" %.50s\n",
             logmsg);

      if (diffstat)
        {
          char *path = svn_path_join_many(scratch_pool,
                                          local_abspath, ".svn/shelves", name,
                                          SVN_VA_NULL);

          system(apr_psprintf(scratch_pool, "diffstat %s 2> /dev/null", path));
          printf("\n");
        }
    }

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__shelve(apr_getopt_t *os,
               void *baton,
               apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *local_abspath;
  const char *name;
  apr_array_header_t *targets;

  if (opt_state->quiet)
    ctx->notify_func2 = NULL; /* Easy out: avoid unneeded work */

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "", pool));

  if (opt_state->list)
    {
      if (os->ind < os->argc)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

      SVN_ERR(shelves_list(local_abspath,
                           ! opt_state->quiet /*diffstat*/,
                           ctx, pool));
      return SVN_NO_ERROR;
    }

  SVN_ERR(get_name(&name, os, pool, pool));

  if (opt_state->remove)
    {
      if (os->ind < os->argc)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

      SVN_ERR(svn_client_shelves_delete(name, local_abspath,
                                        opt_state->dry_run,
                                        ctx, pool));
      if (! opt_state->quiet)
        SVN_ERR(svn_cmdline_printf(pool, "deleted '%s'\n", name));
      return SVN_NO_ERROR;
    }

  /* Parse the remaining arguments as paths. */
  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));
  svn_opt_push_implicit_dot_target(targets, pool);

  {
      svn_depth_t depth = opt_state->depth;
      svn_error_t *err;

      /* shelve has no implicit dot-target `.', so don't you put that
         code here! */
      if (!targets->nelts)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);

      SVN_ERR(svn_cl__check_targets_are_local_paths(targets));

      if (depth == svn_depth_unknown)
        depth = svn_depth_infinity;

      SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, pool));

      if (ctx->log_msg_func3)
        SVN_ERR(svn_cl__make_log_msg_baton(&ctx->log_msg_baton3,
                                           opt_state, NULL, ctx->config,
                                           pool));
      err = svn_client_shelve(name,
                              targets, depth, opt_state->changelists,
                              opt_state->keep_local, opt_state->dry_run,
                              ctx, pool);
      if (ctx->log_msg_func3)
        SVN_ERR(svn_cl__cleanup_log_msg(ctx->log_msg_baton3,
                                        err, pool));
      else
        SVN_ERR(err);

      if (! opt_state->quiet)
        SVN_ERR(svn_cmdline_printf(pool, "shelved '%s'\n", name));
  }

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__unshelve(apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *local_abspath;
  const char *name;
  apr_array_header_t *targets;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "", pool));

  if (opt_state->list)
    {
      if (os->ind < os->argc)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

      SVN_ERR(shelves_list(local_abspath,
                           ! opt_state->quiet /*diffstat*/,
                           ctx, pool));
      return SVN_NO_ERROR;
    }

  SVN_ERR(get_name(&name, os, pool, pool));

  /* There should be no remaining arguments. */
  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));
  if (targets->nelts)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  if (opt_state->quiet)
    ctx->notify_func2 = NULL; /* Easy out: avoid unneeded work */

  SVN_ERR(svn_client_unshelve(name, local_abspath,
                              opt_state->keep_local, opt_state->dry_run,
                              ctx, pool));
  if (! opt_state->quiet)
    SVN_ERR(svn_cmdline_printf(pool, "unshelved '%s'\n", name));

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__shelves(apr_getopt_t *os,
                void *baton,
                apr_pool_t *pool)
{
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *local_abspath;

  /* There should be no remaining arguments. */
  if (os->ind < os->argc)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "", pool));
  SVN_ERR(shelves_list(local_abspath, TRUE /*diffstat*/, ctx, pool));

  return SVN_NO_ERROR;
}
