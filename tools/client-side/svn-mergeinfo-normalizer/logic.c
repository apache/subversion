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
#include "private/svn_sorts_private.h"

#include "mergeinfo-normalizer.h"

#include "svn_private_config.h"


/*** Code. ***/

static svn_rangelist_t *
find_reverse_ranges(svn_rangelist_t *ranges,
                    apr_pool_t *result_pool)
{
  svn_rangelist_t *result = apr_array_make(result_pool, 0, ranges->elt_size);

  int i;
  for (i = 0; i < ranges->nelts; ++i)
    {
      const svn_merge_range_t *range
        = APR_ARRAY_IDX(ranges, i, const svn_merge_range_t *);

      if (range->start >= range->end)
        APR_ARRAY_PUSH(result, const svn_merge_range_t *) = range;
    }

  return result;
}

static svn_error_t *
print_ranges(svn_rangelist_t *ranges,
             const char *title,
             apr_pool_t *scratch_pool)
{
  svn_string_t *string;

  SVN_ERR(svn_rangelist_to_string(&string, ranges, scratch_pool));
  SVN_ERR(svn_cmdline_printf(scratch_pool, _("        %s%s\n"),
                             title, string->data));

  return SVN_NO_ERROR;
}

static svn_error_t *
show_missing_parent(const char *subtree_path,
                    svn_boolean_t misaligned,
                    svn_min__opt_state_t *opt_state,
                    apr_pool_t *scratch_pool)
{
  if (!opt_state->verbose && !opt_state->run_analysis)
    return SVN_NO_ERROR;

  if (misaligned)
    SVN_ERR(svn_cmdline_printf(scratch_pool,
                               _("    MISALIGNED branch: %s\n"),
                               subtree_path));
  else
    SVN_ERR(svn_cmdline_printf(scratch_pool,
                               _("    MISSING in parent: %s\n"),
                               subtree_path));

  return SVN_NO_ERROR;
}

static svn_error_t *
show_reverse_ranges(const char *subtree_path,
                    svn_rangelist_t *reverse_ranges,
                    svn_min__opt_state_t *opt_state,
                    apr_pool_t *scratch_pool)
{
  if (reverse_ranges->nelts)
    return SVN_NO_ERROR;

  if (opt_state->verbose || opt_state->run_analysis)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                 _("    Reverse range(s) found for %s:\n"),
                                 subtree_path));
      SVN_ERR(print_ranges(reverse_ranges, "", scratch_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
show_branch_elision(const char *branch,
                    svn_rangelist_t *subtree_only,
                    svn_rangelist_t *parent_only,
                    svn_rangelist_t *operative_outside_subtree,
                    svn_rangelist_t *operative_in_subtree,
                    svn_min__opt_state_t *opt_state,
                    apr_pool_t *scratch_pool)
{
  if (opt_state->verbose && !subtree_only->nelts && !parent_only->nelts)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                              _("    elide redundant branch %s\n"),
                              branch));
      return SVN_NO_ERROR;
    }

  if (operative_outside_subtree->nelts || operative_in_subtree->nelts)
    {
      if (opt_state->verbose || opt_state->run_analysis)
        {
          SVN_ERR(svn_cmdline_printf(scratch_pool,
                                      _("    CANNOT elide branch %s\n"),
                                      branch));
          if (operative_outside_subtree->nelts)
            SVN_ERR(print_ranges(operative_outside_subtree,
                                  _("revisions not movable to parent: "),
                                  scratch_pool));
          if (operative_in_subtree->nelts)
            SVN_ERR(print_ranges(operative_in_subtree,
                                  _("revisions missing in sub-node: "),
                                  scratch_pool));
        }
    }
  else if (opt_state->verbose)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                  _("    elide branch %s\n"),
                                  branch));
      if (subtree_only->nelts)
        SVN_ERR(print_ranges(subtree_only,
                              _("revisions moved to parent: "),
                              scratch_pool));
      if (parent_only->nelts)
        SVN_ERR(print_ranges(parent_only,
                              _("revisions inoperative in sub-node: "),
                              scratch_pool));
    }

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
show_removed_branch(const char *subtree_path,
                    svn_boolean_t local_only,
                    svn_min__opt_state_t *opt_state,
                    apr_pool_t *scratch_pool)
{
  if (opt_state->verbose)
    SVN_ERR(svn_cmdline_printf(scratch_pool,
                               _("    remove deleted branch %s\n"),
                               subtree_path));

  return SVN_NO_ERROR;
}

static svn_error_t *
remove_obsolete_line(svn_boolean_t *deleted,
                     svn_min__branch_lookup_t *lookup,
                     svn_mergeinfo_t mergeinfo,
                     const char *path,
                     svn_min__opt_state_t *opt_state,
                     progress_t *progress,
                     svn_boolean_t local_only,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_min__branch_lookup(deleted, lookup, path, local_only,
                                 scratch_pool));
  if (*deleted)
    {
      svn_hash_sets(mergeinfo, path, NULL);

      if (progress)
        ++progress->obsoletes_removed;
      SVN_ERR(show_removed_branch(path, local_only, opt_state, scratch_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
remove_obsolete_lines(svn_min__branch_lookup_t *lookup,
                      svn_mergeinfo_t mergeinfo,
                      svn_min__opt_state_t *opt_state,
                      progress_t *progress,
                      svn_boolean_t local_only,
                      apr_pool_t *scratch_pool)
{
  int i;
  apr_array_header_t *sorted_mi;
  apr_pool_t *iterpool;

  if (!opt_state->remove_obsoletes)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);
  sorted_mi = svn_sort__hash(mergeinfo,
                             svn_sort_compare_items_lexically,
                             scratch_pool);
  for (i = 0; i < sorted_mi->nelts; ++i)
    {
      const char *path = APR_ARRAY_IDX(sorted_mi, i, svn_sort__item_t).key;
      svn_boolean_t deleted;

      svn_pool_clear(iterpool);
      SVN_ERR(remove_obsolete_line(&deleted, lookup, mergeinfo, path,
                                   opt_state, progress, local_only,
                                   iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static const char *
get_parent_path(const char *child,
                const char *relpath,
                apr_pool_t *result_pool)
{
  apr_size_t child_len = strlen(child);
  apr_size_t rel_path_len = strlen(relpath);

  if (child_len > rel_path_len)
    {
      apr_size_t parent_len = child_len - rel_path_len - 1;
      if (   child[parent_len] == '/'
          && !strcmp(child + parent_len + 1, relpath))
        return apr_pstrmemdup(result_pool, child, parent_len);
    }

  return "";
}

static svn_error_t *
remove_lines(svn_min__log_t *log,
             svn_min__branch_lookup_t *lookup,
             const char *relpath,
             svn_mergeinfo_t parent_mergeinfo,
             svn_mergeinfo_t subtree_mergeinfo,
             svn_min__opt_state_t *opt_state,
             apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *sorted_mi;
  int i;

  sorted_mi = svn_sort__hash(subtree_mergeinfo,
                             svn_sort_compare_items_lexically,
                             scratch_pool);
  for (i = 0; i < sorted_mi->nelts; ++i)
    {
      const char *parent_path, *subtree_path;
      svn_rangelist_t *parent_ranges, *subtree_ranges, *reverse_ranges;
      svn_rangelist_t *subtree_only, *parent_only;
      svn_rangelist_t *operative_outside_subtree, *operative_in_subtree;
      const svn_sort__item_t *item;
      svn_boolean_t deleted;

      svn_pool_clear(iterpool);

      item = &APR_ARRAY_IDX(sorted_mi, i, svn_sort__item_t);
      subtree_path = item->key;

      /* Maybe, this branch is known to be obsolete anyway.
         Do a quick check based on previous lookups. */
      SVN_ERR(remove_obsolete_line(&deleted, lookup, subtree_mergeinfo,
                                   subtree_path, opt_state, NULL, TRUE,
                                   iterpool));
      if (deleted)
        continue;

      /* Find the parent m/i entry for the same branch. */
      parent_path = get_parent_path(subtree_path, relpath, iterpool);
      subtree_ranges = item->value;
      parent_ranges = svn_hash_gets(parent_mergeinfo, parent_path);

      /* Is there any? */
      if (!parent_ranges)
        {
          /* There is none.  Before we flag that as a problem, maybe the
             branch has been deleted after all?  This time contact the
             repository. */
          SVN_ERR(remove_obsolete_line(&deleted, lookup, subtree_mergeinfo,
                                       subtree_path, opt_state, NULL, FALSE,
                                       iterpool));

          /* If still relevant, we need to keep the whole m/i on this node.
             Therefore, report the problem. */
          if (!deleted)
            SVN_ERR(show_missing_parent(subtree_path, !*parent_path,
                                        opt_state, scratch_pool));

          continue;
        }

      /* We don't know how to handle reverse ranges (there should be none).
         So, we must check for them - just to be sure. */
      reverse_ranges = find_reverse_ranges(subtree_ranges, iterpool);
      if (reverse_ranges->nelts)
        {
          /* We really found a reverse revision range!?
             Try to get rid of it. */
          SVN_ERR(remove_obsolete_line(&deleted, lookup, subtree_mergeinfo,
                                       subtree_path, opt_state, NULL, FALSE,
                                       iterpool));
          if (!deleted)
            SVN_ERR(show_reverse_ranges(subtree_path, reverse_ranges,
                                        opt_state, iterpool));

          continue;
        }

      /* Try the actual elision, i.e. compare parent and sub-tree m/i.
         Where they don't fit, figure out if they can be aligned. */
      SVN_ERR(svn_rangelist_diff(&parent_only, &subtree_only,
                                 parent_ranges, subtree_ranges, FALSE,
                                 iterpool));

      /* From the set of revisions missing on the parent, remove those that
         don't actually affect the sub-tree.  Those can safely be ignored. */
      subtree_only
        = svn_min__operative(log, subtree_path, subtree_only, iterpool);

      /* Find revs that are missing in the parent m/i but affect paths
         outside the sub-tree. */
      operative_outside_subtree
        = svn_min__operative_outside_subtree(log, parent_path, subtree_path,
                                             subtree_only, iterpool);

      /* Find revs that are sub-tree m/i but affect paths in the sub-tree. */
      operative_in_subtree
        = svn_min__operative(log, subtree_path, parent_only, iterpool);

      /* Before we show a branch as "CANNOT elide", make sure it is even
         still relevant. */
      if (   operative_outside_subtree->nelts
          || operative_in_subtree->nelts)
        {
          /* This branch can't be elided.  Maybe, it is obsolete anyway. */
          SVN_ERR(remove_obsolete_line(&deleted, lookup, subtree_mergeinfo,
                                       subtree_path, opt_state, NULL, FALSE,
                                       iterpool));
          if (deleted)
            continue;
        }

      /* Log whether an elision was possible. */
      SVN_ERR(show_branch_elision(subtree_path, subtree_only,
                                  parent_only, operative_outside_subtree,
                                  operative_in_subtree, opt_state,
                                  iterpool));

      /* This will also work when subtree_only is empty. */
      if (   !operative_outside_subtree->nelts
          && !operative_in_subtree->nelts)
        {
          SVN_ERR(svn_rangelist_merge2(parent_ranges, subtree_only,
                                       parent_ranges->pool, iterpool));
          svn_hash_sets(subtree_mergeinfo, subtree_path, NULL);
        }
    }

  /* TODO: Move subtree ranges to parent even if the parent has no entry
   * for the respective branches, yet. */

  svn_pool_destroy(iterpool);

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

      if (ranges->nelts < 2 || find_reverse_ranges(ranges, iterpool)->nelts)
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
show_elision_header(const char *parent_path,
                    const char *relpath,
                    svn_min__opt_state_t *opt_state,
                    apr_pool_t *scratch_pool)
{
  if (opt_state->verbose)
    {
      if (*relpath)
        SVN_ERR(svn_cmdline_printf(scratch_pool,
                                  _("Trying to elide mergeinfo from path\n"
                                    "    %s\n"
                                    "    into mergeinfo at path\n"
                                    "    %s\n\n"),
                                  svn_dirent_join(parent_path, relpath,
                                                  scratch_pool),
                                  parent_path));
      else
        SVN_ERR(svn_cmdline_printf(scratch_pool,
                                  _("Trying to elide mergeinfo at path\n"
                                    "    %s\n\n"),
                                  parent_path));
    }
  else if (opt_state->run_analysis)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                _("Trying to elide mergeinfo at path %s\n"),
                                svn_dirent_join(parent_path, relpath,
                                                scratch_pool)));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
show_removing_obsoletes(svn_min__opt_state_t *opt_state,
                        apr_pool_t *scratch_pool)
{
  if (opt_state->remove_obsoletes && opt_state->verbose)
    SVN_ERR(svn_cmdline_printf(scratch_pool,
                               _("\n    Removing obsolete entries ...\n")));

  return SVN_NO_ERROR;
}

static svn_error_t *
show_elision_result(svn_mergeinfo_t parent_mergeinfo,
                    svn_mergeinfo_t subtree_mergeinfo,
                    svn_min__opt_state_t *opt_state,
                    apr_pool_t *scratch_pool)
{
  if (opt_state->verbose)
    {
      if (apr_hash_count(subtree_mergeinfo))
        {
          apr_array_header_t *sorted_mi;
          int i;
          apr_pool_t *iterpool = svn_pool_create(scratch_pool);

          if (parent_mergeinfo)
            SVN_ERR(svn_cmdline_printf(scratch_pool,
                      _("\n    Sub-tree merge info cannot be elided due to "
                        "the following branches:\n")));
          else
            SVN_ERR(svn_cmdline_printf(scratch_pool,
                  _("\n    Merge info kept for the following branches:\n")));

          sorted_mi = svn_sort__hash(subtree_mergeinfo,
                                    svn_sort_compare_items_lexically,
                                    scratch_pool);
          for (i = 0; i < sorted_mi->nelts; ++i)
            {
              const char *branch = APR_ARRAY_IDX(sorted_mi, i,
                                                  svn_sort__item_t).key;
              svn_pool_clear(iterpool);
              SVN_ERR(svn_cmdline_printf(scratch_pool, _("    %s\n"),
                                         branch));
            }

          SVN_ERR(svn_cmdline_printf(scratch_pool, _("\n")));
          svn_pool_destroy(iterpool);
        }
      else
        {
          SVN_ERR(svn_cmdline_printf(scratch_pool,
                    _("\n    All sub-tree mergeinfo has been elided.\n\n")));
        }
    }
  else if (opt_state->run_analysis)
    {
      if (apr_hash_count(subtree_mergeinfo))
        {
          if (parent_mergeinfo)
            SVN_ERR(svn_cmdline_printf(scratch_pool, _("\n")));
          else
            SVN_ERR(svn_cmdline_printf(scratch_pool,
                                   _("    Keeping top-level mergeinfo.\n")));
        }
      else
        {
          SVN_ERR(svn_cmdline_printf(scratch_pool,
                      _("    All sub-tree mergeinfo has been elided.\n\n")));
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
normalize(apr_array_header_t *wc_mergeinfo,
          svn_min__log_t *log,
          svn_min__branch_lookup_t *lookup,
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

      /* Get the relevant mergeinfo. */
      svn_min__get_mergeinfo_pair(&parent_path, &relpath,
                                  &parent_mergeinfo, &subtree_mergeinfo,
                                  wc_mergeinfo, i);
      SVN_ERR(show_elision_header(parent_path, relpath, opt_state,
                                  scratch_pool));

      /* Eliminate redundant sub-node mergeinfo. */
      if (opt_state->remove_redundants && parent_mergeinfo)
        {
          svn_mergeinfo_t parent_mergeinfo_copy;
          svn_mergeinfo_t subtree_mergeinfo_copy;

          /* Try to elide the mergeinfo for all branches. */
          parent_mergeinfo_copy = svn_mergeinfo_dup(parent_mergeinfo,
                                                    iterpool);
          subtree_mergeinfo_copy = svn_mergeinfo_dup(subtree_mergeinfo,
                                                     iterpool);

          SVN_ERR(remove_lines(log, lookup, relpath, parent_mergeinfo_copy,
                               subtree_mergeinfo_copy, opt_state,
                               iterpool));

          /* If all sub-tree mergeinfo could be elided, clear it.  Update
             the parent mergeinfo in case we moved some up the tree. */
          if (apr_hash_count(subtree_mergeinfo_copy) == 0)
            {
              SVN_ERR(svn_mergeinfo_merge2(parent_mergeinfo,
                                           parent_mergeinfo_copy,
                                           apr_hash_pool_get(parent_mergeinfo),
                                           iterpool));
              apr_hash_clear(subtree_mergeinfo);
              ++progress.nodes_removed;
            }
          else
            {
              /* We have to keep the sub-tree m/i but we can remove entries
                 for deleted branches from it. */
              SVN_ERR(show_removing_obsoletes(opt_state, iterpool));
              SVN_ERR(remove_obsolete_lines(lookup, subtree_mergeinfo,
                                            opt_state, &progress, FALSE,
                                            iterpool));
            }
        }
      else
        {
          /* Eliminate deleted branches. */
          SVN_ERR(remove_obsolete_lines(lookup, subtree_mergeinfo, opt_state,
                                        &progress, FALSE, iterpool));
        }

      /* Reduce the number of remaining ranges. */
      SVN_ERR(shorten_lines(subtree_mergeinfo, log, opt_state, &progress,
                            iterpool));

      /* Display what's left. */
      SVN_ERR(show_elision_result(parent_mergeinfo, subtree_mergeinfo,
                                  opt_state, scratch_pool));

      /* Print progress info. */
      if (   !opt_state->verbose && !opt_state->run_analysis
          && !opt_state->quiet && i % 100 == 0)
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
svn_min__run_normalize(apr_getopt_t *os,
                       void *baton,
                       apr_pool_t *pool)
{
  svn_min__cmd_baton_t *cmd_baton = baton;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_pool_t *subpool = svn_pool_create(pool);
  int i;

  for (i = 0; i < cmd_baton->opt_state->targets->nelts; i++)
    {
      apr_array_header_t *wc_mergeinfo;
      svn_min__log_t *log = NULL;
      svn_min__branch_lookup_t *lookup = NULL;
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
          svn_ra_session_t *session;

          svn_pool_clear(subpool);
          SVN_ERR(svn_min__add_wc_info(baton, i, iterpool, subpool));
          SVN_ERR(svn_client_open_ra_session2(&session, cmd_baton->repo_root,
                                              NULL, cmd_baton->ctx, iterpool,
                                              subpool));
          lookup = svn_min__branch_lookup_create(session, iterpool);
        }

      /* actual normalization */
      svn_pool_clear(subpool);
      if (!cmd_baton->opt_state->quiet)
        SVN_ERR(svn_cmdline_fputs(processing_title(cmd_baton->opt_state,
                                                   subpool),
                                  stdout, subpool));

      SVN_ERR(normalize(wc_mergeinfo, log, lookup, cmd_baton->opt_state,
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
