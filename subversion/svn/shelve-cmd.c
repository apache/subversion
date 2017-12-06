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

/* We define this here to remove any further warnings about the usage of
   experimental functions in this file. */
#define SVN_EXPERIMENTAL

#include "svn_client.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_utf.h"

#include "cl.h"

#include "svn_private_config.h"
#include "private/svn_sorts_private.h"


/* Fetch the next argument. */
static svn_error_t *
get_next_argument(const char **arg,
                  apr_getopt_t *os,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  apr_array_header_t *args;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, scratch_pool));
  SVN_ERR(svn_utf_cstring_to_utf8(arg,
                                  APR_ARRAY_IDX(args, 0, const char *),
                                  result_pool));
  return SVN_NO_ERROR;
}

/* Return a human-friendly description of the time duration MINUTES.
 */
static char *
friendly_duration_str(int minutes,
                      apr_pool_t *result_pool)
{
  char *s;

  if (minutes >= 60 * 24)
    s = apr_psprintf(result_pool, _("%d days"), minutes / 60 / 24);
  else if (minutes >= 60)
    s = apr_psprintf(result_pool, _("%d hours"), minutes / 60);
  else
    s = apr_psprintf(result_pool, _("%d minutes"), minutes);
  return s;
}

/* A comparison function for svn_sort__hash(), comparing the mtime of two
   svn_client_shelved_patch_info_t's. */
static int
compare_shelved_patch_infos_by_mtime(const svn_sort__item_t *a,
                                     const svn_sort__item_t *b)
{
  svn_client_shelf_info_t *a_val = a->value;
  svn_client_shelf_info_t *b_val = b->value;

  return (a_val->mtime < b_val->mtime)
           ? -1 : (a_val->mtime > b_val->mtime) ? 1 : 0;
}

/* Return a list of shelves sorted by patch file mtime, oldest first.
 */
static svn_error_t *
list_sorted_by_date(apr_array_header_t **list,
                    const char *local_abspath,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *scratch_pool)
{
  apr_hash_t *shelved_patch_infos;

  SVN_ERR(svn_client_shelves_list(&shelved_patch_infos, local_abspath,
                                  ctx, scratch_pool, scratch_pool));
  *list = svn_sort__hash(shelved_patch_infos,
                         compare_shelved_patch_infos_by_mtime,
                         scratch_pool);
  return SVN_NO_ERROR;
}

/* Display a list of shelves */
static svn_error_t *
shelves_list(const char *local_abspath,
             svn_boolean_t with_logmsg,
             svn_boolean_t with_diffstat,
             svn_client_ctx_t *ctx,
             apr_pool_t *scratch_pool)
{
  apr_array_header_t *list;
  int i;

  SVN_ERR(list_sorted_by_date(&list,
                              local_abspath, ctx, scratch_pool));

  for (i = 0; i < list->nelts; i++)
    {
      const svn_sort__item_t *item = &APR_ARRAY_IDX(list, i, svn_sort__item_t);
      const char *name = item->key;
      svn_client_shelf_t *shelf;
      svn_client_shelf_version_info_t *info;
      int age_mins;
      char *age_str;
      apr_hash_t *paths;

      SVN_ERR(svn_client_shelf_open(&shelf,
                                    name, local_abspath, ctx, scratch_pool));
      SVN_ERR(svn_client_shelf_version_get_info(&info,
                                                shelf, shelf->max_version,
                                                scratch_pool, scratch_pool));
      age_mins = (apr_time_now() - info->mtime) / 1000000 / 60;
      age_str = friendly_duration_str(age_mins, scratch_pool);

      SVN_ERR(svn_client_shelf_get_paths(&paths,
                                         shelf, shelf->max_version,
                                         scratch_pool, scratch_pool));

      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                 _("%-30s %s ago,  %d versions,  %d paths changed\n"),
                                 name, age_str, shelf->max_version,
                                 apr_hash_count(paths)));
      if (with_logmsg)
        {
          SVN_ERR(svn_cmdline_printf(scratch_pool,
                                     _(" %.50s\n"),
                                     shelf->log_message));
        }

      if (with_diffstat)
        {
#ifndef WIN32
          system(apr_psprintf(scratch_pool, "diffstat %s 2> /dev/null",
                              info->patch_abspath));
          SVN_ERR(svn_cmdline_printf(scratch_pool, "\n"));
#endif
        }
      SVN_ERR(svn_client_shelf_close(shelf, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Print info about each checkpoint of the shelf named NAME.
 */
static svn_error_t *
checkpoint_list(const char *name,
                const char *local_abspath,
                svn_boolean_t diffstat,
                svn_client_ctx_t *ctx,
                apr_pool_t *scratch_pool)
{
  svn_client_shelf_t *shelf;
  int i;

  SVN_ERR(svn_client_shelf_open(&shelf, name, local_abspath,
                                ctx, scratch_pool));

  for (i = 1; i <= shelf->max_version; i++)
    {
      svn_client_shelf_version_info_t *info;
      int age_mins;
      char *age_str;

      SVN_ERR(svn_client_shelf_version_get_info(&info,
                                                shelf, i,
                                                scratch_pool, scratch_pool));
      age_mins = (apr_time_now() - info->mtime) / 1000000 / 60;
      age_str = friendly_duration_str(age_mins, scratch_pool);

      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                 _("version %d: %s ago\n"),
                                 i, age_str));

      if (diffstat)
        {
          system(apr_psprintf(scratch_pool, "diffstat %s 2> /dev/null",
                              info->patch_abspath));
          SVN_ERR(svn_cmdline_printf(scratch_pool, "\n"));
        }
    }

  SVN_ERR(svn_client_shelf_close(shelf, scratch_pool));
  return SVN_NO_ERROR;
}

/* Find the name of the youngest shelf.
 */
static svn_error_t *
name_of_youngest(const char **name_p,
                 const char *local_abspath,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  apr_array_header_t *list;
  const svn_sort__item_t *youngest_item;

  SVN_ERR(list_sorted_by_date(&list,
                              local_abspath, ctx, scratch_pool));
  if (list->nelts == 0)
    return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
                            _("No shelves found"));

  youngest_item = &APR_ARRAY_IDX(list, list->nelts - 1, svn_sort__item_t);
  *name_p = apr_pstrdup(result_pool, youngest_item->key);
  return SVN_NO_ERROR;
}

/** Shelve/save a new version of changes.
 *
 * Shelve in shelf @a name the local modifications found by @a paths,
 * @a depth, @a changelists. Revert the shelved changes from the WC
 * unless @a keep_local is true.
 *
 * If @a dry_run is true, don't actually do it.
 */
static svn_error_t *
shelve(int *new_version_p,
       const char *name,
       const apr_array_header_t *paths,
       svn_depth_t depth,
       const apr_array_header_t *changelists,
       svn_boolean_t keep_local,
       svn_boolean_t dry_run,
       const char *local_abspath,
       svn_client_ctx_t *ctx,
       apr_pool_t *scratch_pool)
{
  svn_client_shelf_t *shelf;

  SVN_ERR(svn_client_shelf_open(&shelf,
                                name, local_abspath, ctx, scratch_pool));

  SVN_ERR(svn_client_shelf_save_new_version(shelf,
                                            paths, depth, changelists,
                                            scratch_pool));
  if (!keep_local)
    {
      /* Reverse-apply the patch. This should be a safer way to remove those
         changes from the WC than running a 'revert' operation. */
      SVN_ERR(svn_client_shelf_unapply(shelf, shelf->max_version,
                                       dry_run, scratch_pool));
    }

  SVN_ERR(svn_client_shelf_set_log_message(shelf, dry_run, scratch_pool));

  if (new_version_p)
    *new_version_p = shelf->max_version;

  if (dry_run)
    {
      SVN_ERR(svn_client_shelf_set_current_version(shelf,
                                                   shelf->max_version - 1,
                                                   scratch_pool));
    }

  SVN_ERR(svn_client_shelf_close(shelf, scratch_pool));
  return SVN_NO_ERROR;
}

/** Restore/unshelve a given or newest version of changes.
 *
 * Restore local modifications from shelf @a name version @a arg,
 * or the newest version is @a arg is null.
 *
 * If @a dry_run is true, don't actually do it.
 */
static svn_error_t *
restore(const char *name,
        const char *arg,
        svn_boolean_t dry_run,
        svn_boolean_t quiet,
        const char *local_abspath,
        svn_client_ctx_t *ctx,
        apr_pool_t *scratch_pool)
{
  int version, old_version;
  svn_client_shelf_t *shelf;

  SVN_ERR(svn_client_shelf_open(&shelf, name, local_abspath,
                                ctx, scratch_pool));
  if (shelf->max_version <= 0)
    {
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                               _("Shelf '%s' not found"),
                               name);
    }

  old_version = shelf->max_version;
  if (arg)
    {
      SVN_ERR(svn_cstring_atoi(&version, arg));
    }
  else
    {
      version = shelf->max_version;
    }

  SVN_ERR(svn_client_shelf_apply(shelf, version,
                                 dry_run, scratch_pool));

  if (! dry_run)
    {
      SVN_ERR(svn_client_shelf_set_current_version(shelf, version,
                                                   scratch_pool));
    }

  if (!quiet)
    {
      if (version < old_version)
        SVN_ERR(svn_cmdline_printf(scratch_pool,
                                   _("restored '%s' version %d and deleted %d newer versions\n"),
                                   name, version, old_version - version));
      else
        SVN_ERR(svn_cmdline_printf(scratch_pool,
                                   _("restored '%s' version %d (the newest version)\n"),
                                   name, version));
    }

  SVN_ERR(svn_client_shelf_close(shelf, scratch_pool));
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
                           ! opt_state->quiet /*with_logmsg*/,
                           ! opt_state->quiet /*with_diffstat*/,
                           ctx, pool));
      return SVN_NO_ERROR;
    }

  SVN_ERR(get_next_argument(&name, os, pool, pool));

  if (opt_state->remove)
    {
      if (os->ind < os->argc)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

      SVN_ERR(svn_client_shelf_delete(name, local_abspath,
                                      opt_state->dry_run, ctx, pool));
      if (! opt_state->quiet)
        SVN_ERR(svn_cmdline_printf(pool,
                                   _("deleted '%s'\n"),
                                   name));
      return SVN_NO_ERROR;
    }

  /* Parse the remaining arguments as paths. */
  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));
  svn_opt_push_implicit_dot_target(targets, pool);

  {
      svn_depth_t depth = opt_state->depth;
      int new_version;
      svn_error_t *err;

      SVN_ERR(svn_cl__check_targets_are_local_paths(targets));

      if (depth == svn_depth_unknown)
        depth = svn_depth_infinity;

      SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, pool));
      /* ### TODO: check all paths are in same WC; for now use first path */
      SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                      APR_ARRAY_IDX(targets, 0, char *),
                                      pool));

      if (ctx->log_msg_func3)
        SVN_ERR(svn_cl__make_log_msg_baton(&ctx->log_msg_baton3,
                                           opt_state, NULL, ctx->config,
                                           pool));
      err = shelve(&new_version, name,
                   targets, depth, opt_state->changelists,
                   opt_state->keep_local, opt_state->dry_run,
                   local_abspath, ctx, pool);
      if (ctx->log_msg_func3)
        SVN_ERR(svn_cl__cleanup_log_msg(ctx->log_msg_baton3,
                                        err, pool));
      else
        SVN_ERR(err);

      if (! opt_state->quiet)
        SVN_ERR(svn_cmdline_printf(pool,
                                   _("shelved '%s' version %d\n"),
                                   name, new_version));
  }

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__unshelve(apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *scratch_pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *local_abspath;
  const char *name;
  apr_array_header_t *targets;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "", scratch_pool));

  if (opt_state->list)
    {
      if (os->ind < os->argc)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

      SVN_ERR(shelves_list(local_abspath,
                           ! opt_state->quiet /*with_logmsg*/,
                           ! opt_state->quiet /*with_diffstat*/,
                           ctx, scratch_pool));
      return SVN_NO_ERROR;
    }

  if (os->ind < os->argc)
    {
      SVN_ERR(get_next_argument(&name, os, scratch_pool, scratch_pool));
    }
  else
    {
      SVN_ERR(name_of_youngest(&name,
                               local_abspath, ctx, scratch_pool, scratch_pool));
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                 _("unshelving the youngest change, '%s'\n"),
                                 name));
    }

  /* There should be no remaining arguments. */
  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, scratch_pool));
  if (targets->nelts)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  if (opt_state->quiet)
    ctx->notify_func2 = NULL; /* Easy out: avoid unneeded work */

  SVN_ERR(restore(name, NULL,
                  opt_state->dry_run, opt_state->quiet,
                  local_abspath, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__shelves(apr_getopt_t *os,
                void *baton,
                apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *local_abspath;

  /* There should be no remaining arguments. */
  if (os->ind < os->argc)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "", pool));
  SVN_ERR(shelves_list(local_abspath,
                       ! opt_state->quiet /*with_logmsg*/,
                       ! opt_state->quiet /*with_diffstat*/,
                       ctx, pool));

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
  const char *name;

  if (opt_state->list)
    subsubcommand = "list";
  else
    SVN_ERR(get_next_argument(&subsubcommand, os, pool, pool));

  SVN_ERR(get_next_argument(&name, os, pool, pool));

  /* Parse the remaining arguments as paths. */
  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "", pool));

  if (opt_state->quiet)
    ctx->notify_func2 = NULL;

  if (strcmp(subsubcommand, "list") == 0)
    {
      if (targets->nelts)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Too many arguments"));

      SVN_ERR(checkpoint_list(name, local_abspath,
                              ! opt_state->quiet /*diffstat*/,
                              ctx, pool));
    }
  else if (strcmp(subsubcommand, "save") == 0)
    {
      svn_depth_t depth
        = (opt_state->depth == svn_depth_unknown) ? svn_depth_infinity
                                                  : opt_state->depth;
      int new_version;
      svn_error_t *err;

      svn_opt_push_implicit_dot_target(targets, pool);
      SVN_ERR(svn_cl__check_targets_are_local_paths(targets));
      SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, pool));
      /* ### TODO: check all paths are in same WC; for now use first path */
      SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                      APR_ARRAY_IDX(targets, 0, char *),
                                      pool));

      if (ctx->log_msg_func3)
        SVN_ERR(svn_cl__make_log_msg_baton(&ctx->log_msg_baton3,
                                           opt_state, NULL, ctx->config,
                                           pool));
      err = shelve(&new_version,
                   name, targets, depth, opt_state->changelists,
                   TRUE /*keep_local*/, opt_state->dry_run,
                   local_abspath, ctx, pool);
      if (ctx->log_msg_func3)
        SVN_ERR(svn_cl__cleanup_log_msg(ctx->log_msg_baton3,
                                        err, pool));
      else
        SVN_ERR(err);

      if (!opt_state->quiet)
        SVN_ERR(svn_cmdline_printf(pool,
                                   _("saved '%s' version %d\n"),
                                   name, new_version));
    }
  else if (strcmp(subsubcommand, "restore") == 0)
    {
      const char *arg;

      if (targets->nelts > 1)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("Too many arguments"));

      /* Which checkpoint number? */
      if (targets->nelts != 1)
        arg = NULL;
      else
        arg = APR_ARRAY_IDX(targets, 0, char *);

      SVN_ERR(restore(name, arg,
                      opt_state->dry_run, opt_state->quiet,
                      local_abspath, ctx, pool));
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
