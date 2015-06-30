/*
 * logic.c -- Mergeinfo normalization / cleanup logic used by the commands.
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

#include "svn_cmdline.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "private/svn_fspath.h"

#include "mergeinfo-normalizer.h"

#include "svn_private_config.h"


/*** Code. ***/

static svn_boolean_t
all_positive_ranges(svn_rangelist_t *ranges)
{
  int i;
  for (i = 0; i < ranges->nelts; ++i)
    {
      const svn_merge_range_t *range
        = APR_ARRAY_IDX(ranges, i, const svn_merge_range_t *);

      if (range->start > range->end)
        return FALSE;
    }

  return TRUE;
}

static svn_error_t *
remove_lines(svn_min__log_t *log,
             const char *relpath,
             svn_mergeinfo_t parent_mergeinfo,
             svn_mergeinfo_t subtree_mergeinfo,
             apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  apr_hash_index_t *hi;
  for (hi = apr_hash_first(scratch_pool, parent_mergeinfo);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *parent_path, *subtree_path;
      svn_rangelist_t *parent_ranges, *subtree_ranges;
      svn_rangelist_t *operative_outside_subtree, *operative_in_subtree;

      svn_pool_clear(iterpool);

      parent_path = apr_hash_this_key(hi);
      subtree_path = svn_fspath__join(parent_path, relpath, iterpool);
      parent_ranges = apr_hash_this_val(hi);
      subtree_ranges = svn_hash_gets(subtree_mergeinfo, subtree_path);

      if (subtree_ranges && all_positive_ranges(subtree_ranges))
        {
          svn_rangelist_t *subtree_only;
          svn_rangelist_t *parent_only;

          SVN_ERR(svn_rangelist_diff(&parent_only, &subtree_only,
                                     parent_ranges, subtree_ranges, FALSE,
                                     iterpool));
          subtree_only
            = svn_min__operative(log, subtree_path, parent_only, iterpool);

          operative_outside_subtree
            = svn_min__operative_outside_subtree(log, parent_path, subtree_path,
                                                 subtree_only, iterpool);
          operative_in_subtree
            = svn_min__operative(log, subtree_path, parent_only, iterpool);

          /* This will also work when subtree_only is empty. */
          if (   !operative_outside_subtree->nelts
              && !operative_in_subtree->nelts)
            {
              SVN_ERR(svn_rangelist_merge2(parent_ranges, subtree_only,
                                           parent_ranges->pool, iterpool));
              svn_hash_sets(subtree_mergeinfo, subtree_path, NULL);
            }
        }
    }

  /* TODO: Move subtree ranges to parent even if the parent has no entry
   * for the respective branches, yet. */

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

typedef struct progress_t
{
  int nodes_total;
  int nodes_todo;

  apr_int64_t nodes_removed;
  apr_int64_t obsoletes_removed;
  apr_int64_t ranges_removed;
} progress_t;

static svn_error_t *
remove_obsolete_lines(svn_ra_session_t *session,
                      svn_mergeinfo_t mergeinfo,
                      svn_min__opt_state_t *opt_state,
                      progress_t *progress,
                      apr_pool_t *scratch_pool)
{
  apr_array_header_t *to_remove;
  int i;
  apr_hash_index_t *hi;
  unsigned initial_count;

  if (!opt_state->remove_obsoletes)
    return SVN_NO_ERROR;

  initial_count = apr_hash_count(mergeinfo);
  to_remove = apr_array_make(scratch_pool, 16, sizeof(const char *));

  for (hi = apr_hash_first(scratch_pool, mergeinfo);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      svn_node_kind_t kind;

      SVN_ERR_ASSERT(*path == '/');
      SVN_ERR(svn_ra_check_path(session, path + 1, SVN_INVALID_REVNUM, &kind,
                                scratch_pool));
      if (kind == svn_node_none)
        APR_ARRAY_PUSH(to_remove, const char *) = path;
    }

  for (i = 0; i < to_remove->nelts; ++i)
    {
      const char *path = APR_ARRAY_IDX(to_remove, i, const char *);
      svn_hash_sets(mergeinfo, path, NULL);
    }

  progress->obsoletes_removed += initial_count - apr_hash_count(mergeinfo);

  return SVN_NO_ERROR;
}

static svn_boolean_t
inoperative(svn_min__log_t *log,
            const char *path,
            svn_revnum_t start,
            svn_revnum_t end,
            apr_pool_t *scratch_pool)
{
  svn_merge_range_t range = { 0 };
  apr_array_header_t *ranges = apr_array_make(scratch_pool, 1, sizeof(&range));

  range.start = start - 1;
  range.end = end;
  APR_ARRAY_PUSH(ranges, svn_merge_range_t *) = &range;

  return svn_min__operative(log, path, ranges, scratch_pool)->nelts == 0;
}

static svn_error_t *
shorten_lines(svn_mergeinfo_t mergeinfo,
              svn_min__log_t *log,
              svn_min__opt_state_t *opt_state,
              progress_t *progress,
              apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;

  if (!opt_state->combine_ranges)
    return SVN_NO_ERROR;

  for (hi = apr_hash_first(scratch_pool, mergeinfo);
       hi;
       hi = apr_hash_next(hi))
    {
      int source, dest;
      const char *path = apr_hash_this_key(hi);
      svn_rangelist_t *ranges = apr_hash_this_val(hi);

      if (ranges->nelts < 2 || !all_positive_ranges(ranges))
        continue;

      for (source = 1, dest = 0; source < ranges->nelts; ++source)
        {
          svn_merge_range_t *source_range
            = APR_ARRAY_IDX(ranges, source, svn_merge_range_t *);
          svn_merge_range_t *dest_range
            = APR_ARRAY_IDX(ranges, dest, svn_merge_range_t *);

          svn_pool_clear(iterpool);

          if (   (source_range->inheritable == dest_range->inheritable)
              && inoperative(log, path, dest_range->end + 1,
                              source_range->start, iterpool))
            {
              dest_range->end = source_range->end;
            }
          else
            {
              ++dest;
              APR_ARRAY_IDX(ranges, dest, svn_merge_range_t *)
                = source_range;
            }
        }

      progress->ranges_removed += ranges->nelts - dest - 1;
      ranges->nelts = dest + 1;
    }

  return SVN_NO_ERROR;
}


static const char *
progress_string(const progress_t *progress,
                svn_min__opt_state_t *opt_state,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  const char *obsoletes_str = apr_psprintf(scratch_pool,
                                           "%" APR_UINT64_T_FMT,
                                           progress->obsoletes_removed);
  const char *nodes_str = apr_psprintf(scratch_pool,
                                       "%" APR_UINT64_T_FMT,
                                       progress->nodes_removed);
  const char *ranges_str = apr_psprintf(scratch_pool,
                                        "%" APR_UINT64_T_FMT,
                                        progress->ranges_removed);

  svn_stringbuf_t *result = svn_stringbuf_create_empty(result_pool);
  svn_stringbuf_appendcstr(result,
                           apr_psprintf(scratch_pool,
                                        _("Processed %d nodes"),
                                        progress->nodes_total
                                          - progress->nodes_todo));

  if (opt_state->remove_obsoletes)
    svn_stringbuf_appendcstr(result,
                             apr_psprintf(scratch_pool,
                                          _(", removed %s branches"),
                                          obsoletes_str));

  if (opt_state->remove_redundants)
    svn_stringbuf_appendcstr(result,
                             apr_psprintf(scratch_pool,
                                          _(", removed m/i on %s sub-nodes"),
                                          nodes_str));

  if (opt_state->combine_ranges)
    svn_stringbuf_appendcstr(result,
                             apr_psprintf(scratch_pool,
                                          _(", combined %s ranges"),
                                          ranges_str));

  return result->data;
}

static svn_error_t *
default_processor(apr_array_header_t *wc_mergeinfo,
                  svn_min__log_t *log,
                  svn_ra_session_t *session,
                  svn_min__opt_state_t *opt_state,
                  apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  progress_t progress = { 0 };

  int i;
  progress.nodes_total = wc_mergeinfo->nelts;
  for (i = wc_mergeinfo->nelts - 1; i >= 0; --i)
    {
      const char *parent_path;
      const char *relpath;
      svn_mergeinfo_t parent_mergeinfo;
      svn_mergeinfo_t subtree_mergeinfo;

      svn_pool_clear(iterpool);
      progress.nodes_todo = i;

      /* Eliminate entries for deleted branches. */
      SVN_ERR(remove_obsolete_lines(session,
                                    svn_min__get_mergeinfo(wc_mergeinfo, i),
                                    opt_state, &progress, iterpool));

      /* Eliminate redundant sub-node mergeinfo. */
      if (opt_state->remove_redundants &&
          svn_min__get_mergeinfo_pair(&parent_path, &relpath,
                                      &parent_mergeinfo, &subtree_mergeinfo,
                                      wc_mergeinfo, i))
        {
          svn_mergeinfo_t parent_mergeinfo_copy;
          svn_mergeinfo_t subtree_mergeinfo_copy;

          /* Eliminate entries for deleted branches such that parent and
             sub-node mergeinfo align again. */
          SVN_ERR(remove_obsolete_lines(session, parent_mergeinfo,
                                        opt_state, &progress, iterpool));

          parent_mergeinfo_copy = svn_mergeinfo_dup(parent_mergeinfo,
                                                    iterpool);
          subtree_mergeinfo_copy = svn_mergeinfo_dup(subtree_mergeinfo,
                                                     iterpool);

          SVN_ERR(remove_lines(log, relpath, parent_mergeinfo_copy,
                               subtree_mergeinfo_copy, iterpool));

          if (apr_hash_count(subtree_mergeinfo_copy) == 0)
            {
              SVN_ERR(svn_mergeinfo_merge2(parent_mergeinfo,
                                           parent_mergeinfo_copy,
                                           apr_hash_pool_get(parent_mergeinfo),
                                           iterpool));
              apr_hash_clear(subtree_mergeinfo);
              ++progress.nodes_removed;
            }
        }

      /* Reduce the number of remaining ranges. */
      SVN_ERR(shorten_lines(svn_min__get_mergeinfo(wc_mergeinfo, i), log,
                            opt_state, &progress, iterpool));

      /* Print progress info. */
      if (!opt_state->quiet && i % 1000 == 0)
        SVN_ERR(svn_cmdline_printf(iterpool, "    %s.\n",
                                   progress_string(&progress, opt_state,
                                                   iterpool, iterpool)));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


static svn_boolean_t
needs_log(svn_min__opt_state_t *opt_state)
{
  return opt_state->combine_ranges || opt_state->remove_redundants;
}

static svn_boolean_t
needs_session(svn_min__opt_state_t *opt_state)
{
  return opt_state->remove_obsoletes;
}

static const char *
processing_title(svn_min__opt_state_t *opt_state,
                 apr_pool_t *result_pool)
{
  svn_stringbuf_t *result = svn_stringbuf_create_empty(result_pool);
  if (opt_state->remove_obsoletes)
    svn_stringbuf_appendcstr(result, _("Removing obsolete branches"));

  if (opt_state->remove_redundants)
    {
      if (svn_stringbuf_isempty(result))
        svn_stringbuf_appendcstr(result, _("Removing redundant mergeinfo"));
      else
        svn_stringbuf_appendcstr(result, _(" and redundant mergeinfo"));
    }

  if (opt_state->combine_ranges)
    {
      if (svn_stringbuf_isempty(result))
        svn_stringbuf_appendcstr(result, _("Combining revision ranges"));
      else
        svn_stringbuf_appendcstr(result, _(", combining revision ranges"));
    }

  svn_stringbuf_appendcstr(result, " ...\n");
  return result->data;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_min__run_command(apr_getopt_t *os,
                     void *baton,
                     svn_min__process_t processor,
                     apr_pool_t *pool)
{
  svn_min__cmd_baton_t *cmd_baton = baton;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_pool_t *subpool = svn_pool_create(pool);
  int i;

  if (processor == NULL)
    processor = default_processor;

  for (i = 0; i < cmd_baton->opt_state->targets->nelts; i++)
    {
      apr_array_header_t *wc_mergeinfo;
      svn_min__log_t *log = NULL;
      svn_ra_session_t *session = NULL;
      const char *url;
      const char *common_path;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_min__add_wc_info(baton, i, iterpool, subpool));

      /* scan working copy */
      svn_pool_clear(subpool);
      SVN_ERR(svn_min__read_mergeinfo(&wc_mergeinfo, cmd_baton, iterpool,
                                      subpool));

      /* Any mergeinfo at all? */
      if (wc_mergeinfo->nelts == 0)
        continue;

      /* fetch log */
      if (needs_log(cmd_baton->opt_state))
        {
          svn_pool_clear(subpool);
          common_path = svn_min__common_parent(wc_mergeinfo, subpool, subpool);
          SVN_ERR_ASSERT(*common_path == '/');
          url = svn_path_url_add_component2(cmd_baton->repo_root,
                                            common_path + 1,
                                            subpool);
          SVN_ERR(svn_min__log(&log, url, cmd_baton, iterpool, subpool));
        }

      /* open RA session */
      if (needs_session(cmd_baton->opt_state))
        {
          svn_pool_clear(subpool);
          SVN_ERR(svn_min__add_wc_info(baton, i, iterpool, subpool));
          SVN_ERR(svn_client_open_ra_session2(&session, cmd_baton->repo_root,
                                              NULL, cmd_baton->ctx, iterpool,
                                              subpool));
        }

      /* actual normalization */
      svn_pool_clear(subpool);
      if (!cmd_baton->opt_state->quiet)
        SVN_ERR(svn_cmdline_fputs(processing_title(cmd_baton->opt_state,
                                                   subpool),
                                  stdout, subpool));

      SVN_ERR((*processor)(wc_mergeinfo, log, session, cmd_baton->opt_state,
                           subpool));

      /* write results to disk */
      svn_pool_clear(subpool);
      if (!cmd_baton->opt_state->dry_run)
        SVN_ERR(svn_min__write_mergeinfo(cmd_baton, wc_mergeinfo, subpool));

      /* show results */
      if (!cmd_baton->opt_state->quiet)
        {
          SVN_ERR(svn_cmdline_printf(subpool, _("\nRemaining mergeinfo:\n")));
          SVN_ERR(svn_min__print_mergeinfo_stats(wc_mergeinfo, subpool));
        }
    }

  svn_pool_destroy(subpool);
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
