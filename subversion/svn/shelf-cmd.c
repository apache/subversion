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
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_props.h"
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

/* Parse the remaining arguments as paths relative to a WC.
 *
 * TARGETS are relative to current working directory.
 *
 * Set *targets_by_wcroot to a hash mapping (char *)wcroot_abspath to
 * (apr_array_header_t *)array of relpaths relative to that WC root.
 */
static svn_error_t *
targets_relative_to_wcs(apr_hash_t **targets_by_wcroot_p,
                         apr_array_header_t *targets,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_hash_t *targets_by_wcroot = apr_hash_make(result_pool);
  int i;

  /* Make each target relative to the WC root. */
  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      const char *wcroot_abspath;
      apr_array_header_t *paths;

      SVN_ERR(svn_dirent_get_absolute(&target, target, result_pool));
      SVN_ERR(svn_client_get_wc_root(&wcroot_abspath, target,
                                     ctx, result_pool, scratch_pool));
      paths = svn_hash_gets(targets_by_wcroot, wcroot_abspath);
      if (! paths)
        {
          paths = apr_array_make(result_pool, 0, sizeof(char *));
          svn_hash_sets(targets_by_wcroot, wcroot_abspath, paths);
        }
      target = svn_dirent_skip_ancestor(wcroot_abspath, target);

      if (target)
        APR_ARRAY_PUSH(paths, const char *) = target;
    }
  *targets_by_wcroot_p = targets_by_wcroot;
  return SVN_NO_ERROR;
}

/* Return targets relative to a WC. Error if they refer to more than one WC. */
static svn_error_t *
targets_relative_to_a_wc(const char **wc_root_abspath_p,
                         apr_array_header_t **paths_p,
                         apr_getopt_t *os,
                         const apr_array_header_t *known_targets,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_array_header_t *targets;
  apr_hash_t *targets_by_wcroot;
  apr_hash_index_t *hi;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      known_targets,
                                                      ctx, FALSE, result_pool));
  svn_opt_push_implicit_dot_target(targets, result_pool);

  SVN_ERR(targets_relative_to_wcs(&targets_by_wcroot, targets,
                                  ctx, result_pool, scratch_pool));
  if (apr_hash_count(targets_by_wcroot) != 1)
    return svn_error_create(SVN_ERR_ILLEGAL_TARGET, NULL,
                            _("All targets must be in the same WC"));

  hi = apr_hash_first(scratch_pool, targets_by_wcroot);
  *wc_root_abspath_p = apr_hash_this_key(hi);
  *paths_p = apr_hash_this_val(hi);
  return SVN_NO_ERROR;
}

/* Return a human-friendly description of DURATION.
 */
static char *
friendly_age_str(apr_time_t mtime,
                 apr_time_t time_now,
                 apr_pool_t *result_pool)
{
  int minutes = (int)((time_now - mtime) / 1000000 / 60);
  char *s;

  if (minutes >= 60 * 24)
    s = apr_psprintf(result_pool,
                     Q_("%d day ago", "%d days ago",
                        minutes / 60 / 24),
                     minutes / 60 / 24);
  else if (minutes >= 60)
    s = apr_psprintf(result_pool,
                     Q_("%d hour ago", "%d hours ago",
                        minutes / 60),
                     minutes / 60);
  else
    s = apr_psprintf(result_pool,
                     Q_("%d minute ago", "%d minutes ago",
                        minutes),
                     minutes);
  return s;
}

/* A comparison function for svn_sort__hash(), comparing the mtime of two
   svn_client_shelf_info_t's. */
static int
compare_shelf_infos_by_mtime(const svn_sort__item_t *a,
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
  apr_hash_t *shelf_infos;

  SVN_ERR(svn_client_shelf_list(&shelf_infos, local_abspath,
                                ctx, scratch_pool, scratch_pool));
  *list = svn_sort__hash(shelf_infos,
                         compare_shelf_infos_by_mtime,
                         scratch_pool);
  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
stats(svn_client_shelf_t *shelf,
      int version,
      svn_client_shelf_version_t *shelf_version,
      apr_time_t time_now,
      svn_boolean_t with_logmsg,
      apr_pool_t *scratch_pool)
{
  char *age_str;
  char *version_str;
  apr_hash_t *paths;
  const char *paths_str = "";

  if (! shelf_version)
    {
      return SVN_NO_ERROR;
    }

  age_str = friendly_age_str(shelf_version->mtime, time_now, scratch_pool);
  if (version == shelf->max_version)
    version_str = apr_psprintf(scratch_pool,
                               _("version %d"), version);
  else
    version_str = apr_psprintf(scratch_pool,
                               Q_("version %d of %d", "version %d of %d",
                                  shelf->max_version),
                               version, shelf->max_version);
  SVN_ERR(svn_client_shelf_paths_changed(&paths, shelf_version,
                                         scratch_pool, scratch_pool));
  paths_str = apr_psprintf(scratch_pool,
                           Q_("%d path changed", "%d paths changed",
                              apr_hash_count(paths)),
                           apr_hash_count(paths));
  SVN_ERR(svn_cmdline_printf(scratch_pool,
                             "%-30s %s, %s, %s\n",
                             shelf->name, version_str, age_str, paths_str));

  if (with_logmsg)
    {
      char *log_message;

      SVN_ERR(svn_client_shelf_get_log_message(&log_message, shelf,
                                               scratch_pool));
      if (log_message)
        {
          SVN_ERR(svn_cmdline_printf(scratch_pool,
                                     _(" %.50s\n"),
                                     log_message));
        }
    }

  return SVN_NO_ERROR;
}

/* Display a list of shelves */
static svn_error_t *
shelves_list(const char *local_abspath,
             svn_boolean_t quiet,
             svn_client_ctx_t *ctx,
             apr_pool_t *scratch_pool)
{
  apr_time_t time_now = apr_time_now();
  apr_array_header_t *list;
  int i;

  SVN_ERR(list_sorted_by_date(&list,
                              local_abspath, ctx, scratch_pool));

  for (i = 0; i < list->nelts; i++)
    {
      const svn_sort__item_t *item = &APR_ARRAY_IDX(list, i, svn_sort__item_t);
      const char *name = item->key;
      svn_client_shelf_t *shelf;
      svn_client_shelf_version_t *shelf_version;

      SVN_ERR(svn_client_shelf_open_existing(&shelf, name, local_abspath,
                                             ctx, scratch_pool));
      SVN_ERR(svn_client_shelf_get_newest_version(&shelf_version, shelf,
                                                  scratch_pool, scratch_pool));
      if (quiet || !shelf_version)
        SVN_ERR(svn_cmdline_printf(scratch_pool, "%s\n", shelf->name));
      else
        SVN_ERR(stats(shelf, shelf->max_version, shelf_version, time_now,
                      TRUE /*with_logmsg*/, scratch_pool));
      SVN_ERR(svn_client_shelf_close(shelf, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Print info about each checkpoint of the shelf named NAME.
 */
static svn_error_t *
shelf_log(const char *name,
          const char *local_abspath,
          svn_client_ctx_t *ctx,
          apr_pool_t *scratch_pool)
{
  apr_time_t time_now = apr_time_now();
  svn_client_shelf_t *shelf;
  apr_array_header_t *versions;
  int i;

  SVN_ERR(svn_client_shelf_open_existing(&shelf, name, local_abspath,
                                         ctx, scratch_pool));
  SVN_ERR(svn_client_shelf_get_all_versions(&versions, shelf,
                                        scratch_pool, scratch_pool));
  for (i = 0; i < versions->nelts; i++)
    {
      svn_client_shelf_version_t *shelf_version
        = APR_ARRAY_IDX(versions, i, void *);

      SVN_ERR(stats(shelf, i + 1, shelf_version, time_now,
                    FALSE /*with_logmsg*/, scratch_pool));
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

/*
 * PATHS are relative to WC_ROOT_ABSPATH.
 */
static svn_error_t *
run_status_on_wc_paths(const char *paths_base_abspath,
                       const apr_array_header_t *paths,
                       svn_depth_t depth,
                       const apr_array_header_t *changelists,
                       svn_client_status_func_t status_func,
                       void *status_baton,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *scratch_pool)
{
  int i;

  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);
      const char *abspath = svn_dirent_join(paths_base_abspath, path,
                                            scratch_pool);

      SVN_ERR(svn_client_status6(NULL /*result_rev*/,
                                 ctx, abspath,
                                 NULL /*revision*/,
                                 depth,
                                 FALSE /*get_all*/,
                                 FALSE /*check_out_of_date*/,
                                 TRUE /*check_working_copy*/,
                                 TRUE /*no_ignore*/,
                                 TRUE /*ignore_externals*/,
                                 FALSE /*depth_as_sticky*/,
                                 changelists,
                                 status_func, status_baton,
                                 scratch_pool));
    }

  return SVN_NO_ERROR;
}

struct status_baton
{
  /* These fields correspond to the ones in the
     svn_cl__print_status() interface. */
  const char *target_abspath;
  const char *target_path;

  const char *header;
  svn_boolean_t quiet;  /* don't display statuses while checking them */
  svn_boolean_t modified;  /* set to TRUE when any modification is found */
  svn_client_ctx_t *ctx;
};

/* A status callback function for printing STATUS for PATH. */
static svn_error_t *
print_status(void *baton,
             const char *path,
             const svn_client_status_t *status,
             apr_pool_t *pool)
{
  struct status_baton *sb = baton;
  unsigned int conflicts;

  return svn_cl__print_status(sb->target_abspath, sb->target_path,
                              path, status,
                              TRUE /*suppress_externals_placeholders*/,
                              FALSE /*detailed*/,
                              FALSE /*show_last_committed*/,
                              TRUE /*skip_unrecognized*/,
                              FALSE /*repos_locks*/,
                              &conflicts, &conflicts, &conflicts,
                              sb->ctx,
                              pool);
}

/* Set BATON->modified to true if TARGET has any local modification or
 * any status that means we should not attempt to patch it.
 *
 * A callback of type svn_client_status_func_t. */
static svn_error_t *
modification_checker(void *baton,
                     const char *target,
                     const svn_client_status_t *status,
                     apr_pool_t *scratch_pool)
{
  struct status_baton *sb = baton;

  if (status->conflicted
      || ! (status->node_status == svn_wc_status_none
            || status->node_status == svn_wc_status_unversioned
            || status->node_status == svn_wc_status_normal))
    {
      if (!sb->quiet)
        {
          if (!sb->modified)  /* print the header only once */
            {
              SVN_ERR(svn_cmdline_printf(scratch_pool, "%s", sb->header));
            }
          SVN_ERR(print_status(baton, target, status, scratch_pool));
        }

      sb->modified = TRUE;
    }
  return SVN_NO_ERROR;
}

/** Shelve/save a new version of changes.
 *
 * Shelve in shelf @a name the local modifications found by @a paths,
 * @a depth, @a changelists. Revert the shelved changes from the WC
 * unless @a keep_local is true.
 *
 * If no local modifications are found, throw an error.
 *
 * If @a dry_run is true, don't actually do it.
 *
 * Report in @a *new_version_p the new version number (or, with dry run,
 * what it would be).
 */
static svn_error_t *
shelve(int *new_version_p,
       const char *name,
       const apr_array_header_t *paths,
       svn_depth_t depth,
       const apr_array_header_t *changelists,
       apr_hash_t *revprop_table,
       svn_boolean_t keep_local,
       svn_boolean_t dry_run,
       svn_boolean_t quiet,
       const char *local_abspath,
       svn_client_ctx_t *ctx,
       apr_pool_t *scratch_pool)
{
  svn_client_shelf_t *shelf;
  svn_client_shelf_version_t *previous_version;
  svn_client_shelf_version_t *new_version;
  const char *cwd_abspath;
  struct status_baton sb;

  SVN_ERR(svn_client_shelf_open_or_create(&shelf,
                                          name, local_abspath,
                                          ctx, scratch_pool));
  SVN_ERR(svn_client_shelf_get_newest_version(&previous_version, shelf,
                                              scratch_pool, scratch_pool));

  if (! quiet)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool, keep_local
        ? _("--- Save a new version of '%s' in WC root '%s'\n")
        : _("--- Shelve '%s' in WC root '%s'\n"),
        shelf->name, shelf->wc_root_abspath));
      SVN_ERR(stats(shelf, shelf->max_version, previous_version, apr_time_now(),
                    TRUE /*with_logmsg*/, scratch_pool));
    }

  sb.header = (keep_local
               ? _("--- Modifications to save:\n")
               : _("--- Modifications to shelve:\n"));
  sb.quiet = quiet;
  sb.modified = FALSE;
  sb.ctx = ctx;
  SVN_ERR(svn_dirent_get_absolute(&cwd_abspath, "", scratch_pool));
  SVN_ERR(run_status_on_wc_paths(cwd_abspath, paths, depth, changelists,
                                 modification_checker, &sb,
                                 ctx, scratch_pool));

  if (!sb.modified)
    {
      SVN_ERR(svn_client_shelf_close(shelf, scratch_pool));
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                               _("No local modifications found"));
    }

  if (! quiet)
    SVN_ERR(svn_cmdline_printf(scratch_pool,
                               keep_local ? _("--- Saving...\n")
                                          : _("--- Shelving...\n")));
  SVN_ERR(svn_client_shelf_save_new_version2(&new_version, shelf,
                                             paths, depth, changelists,
                                             scratch_pool));
  if (! new_version)
    {
      SVN_ERR(svn_client_shelf_close(shelf, scratch_pool));
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
        keep_local ? _("None of the local modifications could be saved")
                   : _("None of the local modifications could be shelved"));
    }

  /* Un-apply the patch, if required. */
  if (!keep_local)
    {
      SVN_ERR(svn_client_shelf_unapply(new_version,
                                       dry_run, scratch_pool));
    }

  /* Fetch the log message and any other revprops */
  if (ctx->log_msg_func3)
    {
      const char *tmp_file;
      apr_array_header_t *commit_items
        = apr_array_make(scratch_pool, 1, sizeof(void *));
      const char *message = "";

      SVN_ERR(ctx->log_msg_func3(&message, &tmp_file, commit_items,
                                 ctx->log_msg_baton3, scratch_pool));
      /* Abort the shelving if the log message callback requested so. */
      if (! message)
        return SVN_NO_ERROR;

      if (message && !dry_run)
        {
          svn_string_t *propval = svn_string_create(message, scratch_pool);

          if (! revprop_table)
            revprop_table = apr_hash_make(scratch_pool);
          svn_hash_sets(revprop_table, SVN_PROP_REVISION_LOG, propval);
        }
    }

  SVN_ERR(svn_client_shelf_revprop_set_all(shelf, revprop_table, scratch_pool));

  if (new_version_p)
    *new_version_p = shelf->max_version;

  if (dry_run)
    {
      SVN_ERR(svn_client_shelf_delete_newer_versions(shelf, previous_version,
                                                     scratch_pool));
    }

  SVN_ERR(svn_client_shelf_close(shelf, scratch_pool));
  return SVN_NO_ERROR;
}

/* Throw an error if any paths affected by SHELF:VERSION are currently
 * modified in the WC. */
static svn_error_t *
check_no_modified_paths(const char *paths_base_abspath,
                        svn_client_shelf_version_t *shelf_version,
                        svn_boolean_t quiet,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *scratch_pool)
{
  apr_hash_t *paths;
  struct status_baton sb;
  apr_hash_index_t *hi;

  sb.target_abspath = shelf_version->shelf->wc_root_abspath;
  sb.target_path = "";
  sb.header = _("--- Paths modified in shelf and in WC:\n");
  sb.quiet = quiet;
  sb.modified = FALSE;
  sb.ctx = ctx;

  SVN_ERR(svn_client_shelf_paths_changed(&paths, shelf_version,
                                         scratch_pool, scratch_pool));
  for (hi = apr_hash_first(scratch_pool, paths); hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      const char *abspath = svn_dirent_join(paths_base_abspath, path,
                                            scratch_pool);

      SVN_ERR(svn_client_status6(NULL /*result_rev*/,
                                 ctx, abspath,
                                 NULL /*revision*/,
                                 svn_depth_empty,
                                 FALSE /*get_all*/,
                                 FALSE /*check_out_of_date*/,
                                 TRUE /*check_working_copy*/,
                                 TRUE /*no_ignore*/,
                                 TRUE /*ignore_externals*/,
                                 FALSE /*depth_as_sticky*/,
                                 NULL /*changelists*/,
                                 modification_checker, &sb,
                                 scratch_pool));
    }
  if (sb.modified)
    {
      return svn_error_create(SVN_ERR_ILLEGAL_TARGET, NULL,
                              _("Cannot unshelve/restore, as at least one "
                                "path is modified in shelf and in WC"));
    }
  return SVN_NO_ERROR;
}

/* Intercept patch notifications to detect when there is a conflict */
struct patch_notify_baton_t
{
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
  svn_boolean_t rejects;
};

/* Intercept patch notifications to detect when there is a conflict */
static void
patch_notify(void *baton,
             const svn_wc_notify_t *notify,
             apr_pool_t *pool)
{
  struct patch_notify_baton_t *b = baton;

  if (notify->action == svn_wc_notify_patch_rejected_hunk)
    b->rejects = TRUE;
  b->notify_func(b->notify_baton, notify, pool);
}

/** Restore/unshelve a given or newest version of changes.
 *
 * Restore local modifications from shelf @a name version @a arg,
 * or the newest version is @a arg is null.
 *
 * If @a dry_run is true, don't actually do it.
 */
static svn_error_t *
shelf_restore(const char *name,
              const char *arg,
              svn_boolean_t dry_run,
              svn_boolean_t quiet,
              const char *local_abspath,
              svn_client_ctx_t *ctx,
              apr_pool_t *scratch_pool)
{
  int version, old_version;
  apr_time_t time_now = apr_time_now();
  svn_client_shelf_t *shelf;
  svn_client_shelf_version_t *shelf_version;
  struct patch_notify_baton_t b;

  SVN_ERR(svn_client_shelf_open_existing(&shelf, name, local_abspath,
                                         ctx, scratch_pool));

  old_version = shelf->max_version;
  if (arg)
    {
      SVN_ERR(svn_cstring_atoi(&version, arg));
      SVN_ERR(svn_client_shelf_version_open(&shelf_version,
                                            shelf, version,
                                            scratch_pool, scratch_pool));
    }
  else
    {
      version = shelf->max_version;
      SVN_ERR(svn_client_shelf_get_newest_version(&shelf_version, shelf,
                                                  scratch_pool, scratch_pool));
    }

  if (! quiet)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                 _("--- Unshelve '%s' in WC root '%s'\n"),
                                 shelf->name, shelf->wc_root_abspath));
      SVN_ERR(stats(shelf, version, shelf_version, time_now,
                    TRUE /*with_logmsg*/, scratch_pool));
    }
  SVN_ERR(check_no_modified_paths(shelf->wc_root_abspath,
                                  shelf_version, quiet, ctx, scratch_pool));

  b.rejects = FALSE;
  b.notify_func = ctx->notify_func2;
  b.notify_baton = ctx->notify_baton2;
  ctx->notify_func2 = patch_notify;
  ctx->notify_baton2 = &b;

  SVN_ERR(svn_client_shelf_apply(shelf_version,
                                 dry_run, scratch_pool));
  ctx->notify_func2 = b.notify_func;
  ctx->notify_baton2 = b.notify_baton;

  if (b.rejects)
    {
      return svn_error_create(SVN_ERR_ILLEGAL_TARGET, NULL,
                              _("Unshelve/restore failed due to conflicts"));
    }

  if (! dry_run)
    {
      SVN_ERR(svn_client_shelf_delete_newer_versions(shelf, shelf_version,
                                                     scratch_pool));
    }

  if (!quiet)
    {
      if (version < old_version)
        SVN_ERR(svn_cmdline_printf(scratch_pool,
                  Q_("restored '%s' version %d and deleted %d newer version\n",
                     "restored '%s' version %d and deleted %d newer versions\n",
                     old_version - version),
                  name, version, old_version - version));
      else
        SVN_ERR(svn_cmdline_printf(scratch_pool,
                                   _("restored '%s' version %d (the newest version)\n"),
                                   name, version));
    }

  SVN_ERR(svn_client_shelf_close(shelf, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
shelf_diff(const char *name,
           const char *arg,
           const char *local_abspath,
           svn_client_ctx_t *ctx,
           apr_pool_t *scratch_pool)
{
  svn_client_shelf_t *shelf;
  svn_client_shelf_version_t *shelf_version;
  svn_stream_t *stream;

  SVN_ERR(svn_client_shelf_open_existing(&shelf, name, local_abspath,
                                         ctx, scratch_pool));

  if (arg)
    {
      int version;

      SVN_ERR(svn_cstring_atoi(&version, arg));
      SVN_ERR(svn_client_shelf_version_open(&shelf_version,
                                            shelf, version,
                                            scratch_pool, scratch_pool));
    }
  else
    {
      SVN_ERR(svn_client_shelf_get_newest_version(&shelf_version, shelf,
                                                  scratch_pool, scratch_pool));
    }

  SVN_ERR(svn_stream_for_stdout(&stream, scratch_pool));
  SVN_ERR(svn_client_shelf_export_patch(shelf_version, stream,
                                        scratch_pool));
  SVN_ERR(svn_stream_close(stream));

  SVN_ERR(svn_client_shelf_close(shelf, scratch_pool));
  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
static svn_error_t *
shelf_drop(const char *name,
           const char *local_abspath,
           svn_boolean_t dry_run,
           svn_boolean_t quiet,
           svn_client_ctx_t *ctx,
           apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_client_shelf_delete(name, local_abspath, dry_run,
                                  ctx, scratch_pool));
  if (! quiet)
    SVN_ERR(svn_cmdline_printf(scratch_pool,
                               _("deleted '%s'\n"),
                               name));
  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
shelf_shelve(int *new_version,
             const char *name,
             apr_array_header_t *targets,
             svn_depth_t depth,
             apr_array_header_t *changelists,
             apr_hash_t *revprop_table,
             svn_boolean_t keep_local,
             svn_boolean_t dry_run,
             svn_boolean_t quiet,
             svn_client_ctx_t *ctx,
             apr_pool_t *scratch_pool)
{
  const char *local_abspath;

  if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  SVN_ERR(svn_cl__check_targets_are_local_paths(targets));

  SVN_ERR(svn_cl__eat_peg_revisions(&targets, targets, scratch_pool));

  svn_opt_push_implicit_dot_target(targets, scratch_pool);

  /* ### TODO: check all paths are in same WC; for now use first path */
  SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                  APR_ARRAY_IDX(targets, 0, char *),
                                  scratch_pool));

  SVN_ERR(shelve(new_version, name,
                 targets, depth, changelists,
                 revprop_table,
                 keep_local, dry_run, quiet,
                 local_abspath, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__shelf_save(apr_getopt_t *os,
                   void *baton,
                   apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;

  opt_state->keep_local = TRUE;
  SVN_ERR(svn_cl__shelf_shelve(os, baton, pool));
  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__shelf_shelve(apr_getopt_t *os,
                     void *baton,
                     apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *name;
  apr_array_header_t *targets;

  if (opt_state->quiet)
    ctx->notify_func2 = NULL; /* Easy out: avoid unneeded work */

  SVN_ERR(get_next_argument(&name, os, pool, pool));

  /* Parse the remaining arguments as paths. */
  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, FALSE, pool));
  {
      int new_version;
      svn_error_t *err;

      if (ctx->log_msg_func3)
        SVN_ERR(svn_cl__make_log_msg_baton(&ctx->log_msg_baton3,
                                           opt_state, NULL, ctx->config,
                                           pool));
      err = shelf_shelve(&new_version, name,
                         targets, opt_state->depth, opt_state->changelists,
                         opt_state->revprop_table,
                         opt_state->keep_local, opt_state->dry_run,
                         opt_state->quiet, ctx, pool);
      if (ctx->log_msg_func3)
        SVN_ERR(svn_cl__cleanup_log_msg(ctx->log_msg_baton3,
                                        err, pool));
      else
        SVN_ERR(err);

      if (! opt_state->quiet)
        {
          if (opt_state->keep_local)
            SVN_ERR(svn_cmdline_printf(pool,
                                       _("saved '%s' version %d\n"),
                                       name, new_version));
          else
            SVN_ERR(svn_cmdline_printf(pool,
                                       _("shelved '%s' version %d\n"),
                                       name, new_version));
        }
  }

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__shelf_unshelve(apr_getopt_t *os,
                       void *baton,
                       apr_pool_t *scratch_pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *local_abspath;
  const char *name;
  const char *arg = NULL;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "", scratch_pool));

  if (os->ind < os->argc)
    {
      SVN_ERR(get_next_argument(&name, os, scratch_pool, scratch_pool));
    }
  else
    {
      SVN_ERR(name_of_youngest(&name,
                               local_abspath, ctx, scratch_pool, scratch_pool));
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                 _("unshelving the youngest shelf, '%s'\n"),
                                 name));
    }

  /* Which checkpoint number? */
  if (os->ind < os->argc)
    SVN_ERR(get_next_argument(&arg, os, scratch_pool, scratch_pool));

  if (os->ind < os->argc)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Too many arguments"));

  if (opt_state->quiet)
    ctx->notify_func2 = NULL; /* Easy out: avoid unneeded work */

  SVN_ERR(shelf_restore(name, arg,
                        opt_state->dry_run, opt_state->quiet,
                        local_abspath, ctx, scratch_pool));

  if (opt_state->drop)
    {
      SVN_ERR(shelf_drop(name, local_abspath,
                         opt_state->dry_run, opt_state->quiet,
                         ctx, scratch_pool));
    }
  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__shelf_list(apr_getopt_t *os,
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
                       opt_state->quiet,
                       ctx, pool));

  return SVN_NO_ERROR;
}

/* "svn shelf-list-by-paths [PATH...]"
 *
 * TARGET_RELPATHS are all within the same WC, relative to WC_ROOT_ABSPATH.
 */
static svn_error_t *
shelf_list_by_paths(apr_array_header_t *target_relpaths,
                    const char *wc_root_abspath,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *scratch_pool)
{
  apr_array_header_t *shelves;
  apr_hash_t *paths_to_shelf_name = apr_hash_make(scratch_pool);
  apr_array_header_t *array;
  int i;

  SVN_ERR(list_sorted_by_date(&shelves,
                              wc_root_abspath, ctx, scratch_pool));

  /* Check paths are valid */
  for (i = 0; i < target_relpaths->nelts; i++)
    {
      char *target_relpath = APR_ARRAY_IDX(target_relpaths, i, char *);

      if (svn_path_is_url(target_relpath))
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("'%s' is not a local path"), target_relpath);
      SVN_ERR_ASSERT(svn_relpath_is_canonical(target_relpath));
    }

  /* Find the most recent shelf for each affected path */
  for (i = 0; i < shelves->nelts; i++)
    {
      svn_sort__item_t *item = &APR_ARRAY_IDX(shelves, i, svn_sort__item_t);
      const char *name = item->key;
      svn_client_shelf_t *shelf;
      svn_client_shelf_version_t *shelf_version;
      apr_hash_t *shelf_paths;
      int j;

      SVN_ERR(svn_client_shelf_open_existing(&shelf,
                                             name, wc_root_abspath,
                                             ctx, scratch_pool));
      SVN_ERR(svn_client_shelf_get_newest_version(&shelf_version, shelf,
                                                  scratch_pool, scratch_pool));
      SVN_ERR(svn_client_shelf_paths_changed(&shelf_paths,
                                             shelf_version,
                                             scratch_pool, scratch_pool));
      for (j = 0; j < target_relpaths->nelts; j++)
        {
          char *target_relpath = APR_ARRAY_IDX(target_relpaths, j, char *);
          apr_hash_index_t *hi;

          for (hi = apr_hash_first(scratch_pool, shelf_paths);
               hi; hi = apr_hash_next(hi))
            {
              const char *shelf_path = apr_hash_this_key(hi);

              if (svn_relpath_skip_ancestor(target_relpath, shelf_path))
                {
                  if (! svn_hash_gets(paths_to_shelf_name, shelf_path))
                    {
                      svn_hash_sets(paths_to_shelf_name, shelf_path, shelf->name);
                    }
                }
            }
        }
    }

  /* Print the results. */
  array = svn_sort__hash(paths_to_shelf_name,
                         svn_sort_compare_items_as_paths,
                         scratch_pool);
  for (i = 0; i < array->nelts; i++)
    {
      svn_sort__item_t *item = &APR_ARRAY_IDX(array, i, svn_sort__item_t);
      const char *path = item->key;
      const char *name = item->value;

      SVN_ERR(svn_cmdline_printf(scratch_pool, "%-20.20s %s\n",
                                 name,
                                 svn_dirent_local_style(path, scratch_pool)));
    }
  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__shelf_list_by_paths(apr_getopt_t *os,
                            void *baton,
                            apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *wc_root_abspath;
  apr_array_header_t *targets;

  /* Parse the remaining arguments as paths. */
  SVN_ERR(targets_relative_to_a_wc(&wc_root_abspath, &targets,
                                   os, opt_state->targets,
                                   ctx, pool, pool));

  SVN_ERR(shelf_list_by_paths(targets, wc_root_abspath, ctx, pool));
  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__shelf_diff(apr_getopt_t *os,
                   void *baton,
                   apr_pool_t *pool)
{
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *local_abspath;
  const char *name;
  const char *arg = NULL;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "", pool));

  SVN_ERR(get_next_argument(&name, os, pool, pool));

  /* Which checkpoint number? */
  if (os->ind < os->argc)
    SVN_ERR(get_next_argument(&arg, os, pool, pool));

  if (os->ind < os->argc)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Too many arguments"));

  SVN_ERR(shelf_diff(name, arg, local_abspath, ctx, pool));

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__shelf_drop(apr_getopt_t *os,
                   void *baton,
                   apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *name;
  const char *local_abspath;

  SVN_ERR(get_next_argument(&name, os, pool, pool));

  /* There should be no remaining arguments. */
  if (os->ind < os->argc)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "", pool));
  SVN_ERR(shelf_drop(name, local_abspath,
                     opt_state->dry_run, opt_state->quiet,
                     ctx, pool));

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__shelf_log(apr_getopt_t *os,
                  void *baton,
                  apr_pool_t *pool)
{
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *name;
  const char *local_abspath;

  SVN_ERR(get_next_argument(&name, os, pool, pool));

  /* There should be no remaining arguments. */
  if (os->ind < os->argc)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0,
                            _("Too many arguments"));

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "", pool));
  SVN_ERR(shelf_log(name, local_abspath,
                    ctx, pool));

  return SVN_NO_ERROR;
}
