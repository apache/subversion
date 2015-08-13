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

static svn_rangelist_t *
find_non_recursive_ranges(svn_rangelist_t *ranges,
                          apr_pool_t *result_pool)
{
  svn_rangelist_t *result = apr_array_make(result_pool, 0, ranges->elt_size);

  int i;
  for (i = 0; i < ranges->nelts; ++i)
    {
      const svn_merge_range_t *range
        = APR_ARRAY_IDX(ranges, i, const svn_merge_range_t *);

      if (!range->inheritable)
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
                                 _("    REVERSE RANGE(S) found for %s:\n"),
                                 subtree_path));
      SVN_ERR(print_ranges(reverse_ranges, "", scratch_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
show_non_recursive_ranges(const char *subtree_path,
                          svn_rangelist_t *non_recursive_ranges,
                          svn_min__opt_state_t *opt_state,
                          apr_pool_t *scratch_pool)
{
  if (non_recursive_ranges->nelts)
    return SVN_NO_ERROR;

  if (opt_state->verbose || opt_state->run_analysis)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                            _("    NON-RECURSIVE RANGE(S) found for %s:\n"),
                            subtree_path));
      SVN_ERR(print_ranges(non_recursive_ranges, "", scratch_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
show_branch_elision(const char *branch,
                    svn_rangelist_t *subtree_only,
                    svn_rangelist_t *parent_only,
                    svn_rangelist_t *operative_outside_subtree,
                    svn_rangelist_t *operative_in_subtree,
                    svn_rangelist_t *implied_in_parent,
                    svn_rangelist_t *implied_in_subtree,
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
      SVN_ERR(svn_rangelist_remove(&subtree_only, implied_in_parent,
                                   subtree_only, TRUE, scratch_pool));
      SVN_ERR(svn_rangelist_remove(&parent_only, implied_in_subtree,
                                   parent_only, TRUE, scratch_pool));

      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                  _("    elide branch %s\n"),
                                  branch));
      if (implied_in_parent->nelts)
        SVN_ERR(print_ranges(implied_in_parent,
                              _("revisions implied in parent: "),
                              scratch_pool));
      if (subtree_only->nelts)
        SVN_ERR(print_ranges(subtree_only,
                              _("revisions moved to parent: "),
                              scratch_pool));
      if (implied_in_subtree->nelts)
        SVN_ERR(print_ranges(implied_in_subtree,
                              _("revisions implied in sub-node: "),
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

typedef enum deletion_state_t
{
  ds_exists,
  ds_implied,
  ds_has_copies,
  ds_deleted
} deletion_state_t;

static svn_error_t *
show_removed_branch(const char *subtree_path,
                    svn_min__opt_state_t *opt_state,
                    deletion_state_t deletion_state,
                    svn_boolean_t report_non_removals,
                    const char *surviving_copy,
                    apr_pool_t *scratch_pool)
{
  if (opt_state->verbose)
    switch (deletion_state)
      {
        case ds_deleted:
          SVN_ERR(svn_cmdline_printf(scratch_pool,
                                     _("    remove deleted branch %s\n"),
                                     subtree_path));
          break;

        case ds_implied:
          if (report_non_removals)
            SVN_ERR(svn_cmdline_printf(scratch_pool,
                                       _("    keep POTENTIAL branch %s\n"),
                                       subtree_path));
          break;

        case ds_has_copies:
          if (report_non_removals)
            {
              SVN_ERR(svn_cmdline_printf(scratch_pool,
                                        _("    has SURVIVING COPIES: %s\n"),
                                        subtree_path));
              SVN_ERR(svn_cmdline_printf(scratch_pool,
                                         _("        e.g.: %s\n"),
                                        surviving_copy));
            }
          break;

        default:
          break;
      }

  return SVN_NO_ERROR;
}

static const char *
get_copy_target_path(const char *source,
                     const svn_min__copy_t *copy,
                     apr_pool_t *result_pool)
{
  if (svn_dirent_is_ancestor(copy->copyfrom_path, source))
    {
      const char *relpath = svn_dirent_skip_ancestor(copy->copyfrom_path,
                                                     source);
      return svn_dirent_join(copy->path, relpath, result_pool);
    }

  return apr_pstrdup(result_pool, copy->path);
}

static const char *
find_surviving_copy(svn_min__log_t *log,
                    const char *path,
                    svn_revnum_t start_rev,
                    svn_revnum_t end_rev,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  const char *surviver = NULL;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *copies = svn_min__get_copies(log, path, start_rev,
                                                   end_rev, scratch_pool,
                                                   scratch_pool);

  int i;
  for (i = 0; (i < copies->nelts) && !surviver; ++i)
    {
      const char *copy_target;
      const svn_min__copy_t *copy;
      svn_revnum_t deletion_rev;
      svn_pool_clear(iterpool);

      copy = APR_ARRAY_IDX(copies, i, const svn_min__copy_t *);
      copy_target = get_copy_target_path(path, copy, iterpool);

      /* Is this a surviving copy? */
      deletion_rev = svn_min__find_deletion(log, copy_target,
                                            SVN_INVALID_REVNUM,
                                            copy->revision, iterpool);
      if (SVN_IS_VALID_REVNUM(deletion_rev))
        {
          /* Are there surviving sub-copies? */
          surviver = find_surviving_copy(log, copy_target,
                                         copy->revision, deletion_rev - 1,
                                         result_pool, iterpool);
        }
      else
        {
          surviver = apr_pstrdup(result_pool, copy_target);
        }
    }
 
  svn_pool_destroy(iterpool);

  return surviver;
}

static void
find_surviving_copies(apr_array_header_t *survivers,
                      svn_min__log_t *log,
                      const char *path,
                      svn_revnum_t start_rev,
                      svn_revnum_t end_rev,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *copies = svn_min__get_copies(log, path, start_rev,
                                                   end_rev, scratch_pool,
                                                   scratch_pool);

  int i;
  for (i = 0; i < copies->nelts; ++i)
    {
      const char *copy_target;
      const svn_min__copy_t *copy;
      svn_revnum_t deletion_rev;
      svn_pool_clear(iterpool);

      copy = APR_ARRAY_IDX(copies, i, const svn_min__copy_t *);
      copy_target = get_copy_target_path(path, copy, iterpool);

      /* Is this a surviving copy? */
      deletion_rev = svn_min__find_deletion(log, copy_target,
                                            SVN_INVALID_REVNUM,
                                            copy->revision, iterpool);
      if (SVN_IS_VALID_REVNUM(deletion_rev))
        {
          /* Are there surviving sub-copies? */
          find_surviving_copies(survivers, log, copy_target,
                                copy->revision, deletion_rev - 1,
                                result_pool, iterpool);
        }
      else
        {
          APR_ARRAY_PUSH(survivers, const char *) = apr_pstrdup(result_pool,
                                                                copy_target);
        }
    }
 
  svn_pool_destroy(iterpool);
}

static svn_error_t *
remove_obsolete_line(deletion_state_t *state,
                     svn_min__branch_lookup_t *lookup,
                     svn_min__log_t *log,
                     svn_mergeinfo_t mergeinfo,
                     const char *path,
                     svn_min__opt_state_t *opt_state,
                     progress_t *progress,
                     svn_boolean_t local_only,
                     svn_boolean_t report_non_removals,
                     apr_pool_t *scratch_pool)
{
  svn_boolean_t deleted;
  const char *surviving_copy = NULL;

  if (!opt_state->remove_obsoletes)
    {
      *state = ds_exists;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_min__branch_lookup(&deleted, lookup, path, local_only,
                                 scratch_pool));
  if (deleted)
    {
      if (log)
        {
          svn_revnum_t creation_rev, deletion_rev;

          /* Look for an explicit deletion since the last creation
           * (or parent creation).  Otherwise, the PATH never existed
           * but is implied and may be needed as soon as there is a
           * catch-up merge. */
          creation_rev = svn_min__find_copy(log, path, SVN_INVALID_REVNUM,
                                            0, scratch_pool);
          deletion_rev = svn_min__find_deletion(log, path,
                                                SVN_INVALID_REVNUM,
                                                creation_rev, scratch_pool);
          surviving_copy = find_surviving_copy(log, path,
                                               SVN_IS_VALID_REVNUM(deletion_rev)
                                                 ? deletion_rev - 1
                                                 : deletion_rev,
                                               creation_rev,
                                               scratch_pool, scratch_pool);

          if (surviving_copy)
            {
              *state = ds_has_copies;
            }
          else
            {
              *state = SVN_IS_VALID_REVNUM(deletion_rev) ? ds_deleted
                                                         : ds_implied;
            }
        }
      else
        {
          *state = ds_deleted;
        }

      /* Remove branch if it has actually been deleted. */
      if (*state == ds_deleted)
        {
          svn_hash_sets(mergeinfo, path, NULL);

          if (progress)
            ++progress->obsoletes_removed;
        }
    }
  else
    {
      *state = ds_exists;
    }

  SVN_ERR(show_removed_branch(path, opt_state, *state, report_non_removals,
                              surviving_copy, scratch_pool));

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
remove_obsolete_lines(svn_min__branch_lookup_t *lookup,
                      svn_min__log_t *log,
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

  SVN_ERR(show_removing_obsoletes(opt_state, scratch_pool));

  iterpool = svn_pool_create(scratch_pool);
  sorted_mi = svn_sort__hash(mergeinfo,
                             svn_sort_compare_items_lexically,
                             scratch_pool);
  for (i = 0; i < sorted_mi->nelts; ++i)
    {
      const char *path = APR_ARRAY_IDX(sorted_mi, i, svn_sort__item_t).key;
      deletion_state_t state;

      svn_pool_clear(iterpool);
      SVN_ERR(remove_obsolete_line(&state, lookup, log, mergeinfo, path,
                                   opt_state, progress, local_only, TRUE,
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

/* Remove all ranges from RANGES where the history of SOURCE_PATH@RANGE and
   TARGET_PATH@HEAD overlap.  Return the list of removed ranges.

   Note that SOURCE_PATH@RANGE may actually refer to different branches
   created or re-created and then deleted at different points in time.
 */
static svn_error_t *
remove_overlapping_history(svn_rangelist_t **removed,
                           svn_rangelist_t **ranges,
                           svn_min__log_t *log,
                           const char *source_path,
                           const char *target_path,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  apr_array_header_t *target_history;
  apr_array_header_t *source_history;
  apr_array_header_t *deletions;
  svn_revnum_t source_rev, next_deletion;
  apr_pool_t *iterpool;
  int i;

  svn_rangelist_t *result = apr_array_make(result_pool, 0,
                                           sizeof(svn_merge_range_t *));

  /* In most cases, there is nothing to do. */
  if (!(*ranges)->nelts)
    {
      *removed = result;
      return SVN_NO_ERROR;
    }

  /* The history of the working copy branch ("target") is always the same. */
  iterpool = svn_pool_create(scratch_pool);
  target_history = svn_min__get_history(log, target_path, SVN_INVALID_REVNUM,
                                        0, scratch_pool, scratch_pool);

  /* Collect the deletion revisions, i.e. the revisons separating different
     branches with the same name. */
  deletions = svn_min__find_deletions(log, source_path, scratch_pool,
                                      scratch_pool);
  next_deletion = SVN_INVALID_REVNUM;

  /* Get the history of each of these branches up to the point where the
     respective previous branch was deleted (or r0). Intersect with the
     target history and RANGES. */
  for (i = 0; i <= deletions->nelts; ++i)
    {
      apr_array_header_t *common_history;
      apr_array_header_t *common_ranges;
      apr_array_header_t *removable_ranges;
      svn_pool_clear(iterpool);

      /* First iteration: HEAD to whatever latest deletion or r0.

         NEXT_DELETION points to the last revision that may contain
         changes of the previous branch at SOURCE_PATH.  The deletion
         rev itself is not relevant but may instead contains the modyfing
         creation of the next incarnation of that branch. */
      source_rev = next_deletion;
      next_deletion = i <  deletions->nelts
                    ? APR_ARRAY_IDX(deletions, i, svn_revnum_t) - 1
                    : 0;

      /* Determine the overlapping history of merge source & target. */
      source_history = svn_min__get_history(log, source_path,
                                            source_rev, next_deletion,
                                            iterpool, iterpool);
      common_history = svn_min__intersect_history(source_history,
                                                  target_history, iterpool);

      /* Remove that overlap from RANGES. */
      common_ranges = svn_min__history_ranges(common_history, iterpool);
      if (!common_ranges->nelts)
        continue;

      SVN_ERR(svn_rangelist_intersect(&removable_ranges, common_ranges,
                                      *ranges, TRUE, iterpool));
      SVN_ERR(svn_rangelist_remove(ranges, removable_ranges, *ranges, TRUE,
                                   (*ranges)->pool));
      SVN_ERR(svn_rangelist_merge2(result, removable_ranges, result_pool,
                                   result_pool));
    }

  svn_pool_destroy(iterpool);
  *removed = result;

  return SVN_NO_ERROR;
}

static svn_error_t *
remove_lines(svn_min__log_t *log,
             svn_min__branch_lookup_t *lookup,
             const char *fs_path,
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
      const char *parent_path, *subtree_path, *parent_fs_path;
      svn_rangelist_t *parent_ranges, *subtree_ranges;
      svn_rangelist_t *reverse_ranges, *non_recursive_ranges;
      svn_rangelist_t *subtree_only, *parent_only;
      svn_rangelist_t *operative_outside_subtree, *operative_in_subtree;
      svn_rangelist_t *implied_in_subtree, *implied_in_parent;
      const svn_sort__item_t *item;
      deletion_state_t state;

      svn_pool_clear(iterpool);

      item = &APR_ARRAY_IDX(sorted_mi, i, svn_sort__item_t);
      subtree_path = item->key;

      /* Maybe, this branch is known to be obsolete anyway.
         Do a quick check based on previous lookups. */
      SVN_ERR(remove_obsolete_line(&state, lookup, log,
                                   subtree_mergeinfo, subtree_path,
                                   opt_state, NULL, TRUE, FALSE,
                                   iterpool));
      if (state == ds_deleted)
        continue;

      /* Find the parent m/i entry for the same branch. */
      parent_path = get_parent_path(subtree_path, relpath, iterpool);
      parent_fs_path = get_parent_path(fs_path, relpath, iterpool);
      subtree_ranges = item->value;
      parent_ranges = svn_hash_gets(parent_mergeinfo, parent_path);

      /* Is there any? */
      if (!parent_ranges)
        {
          /* There is none.  Before we flag that as a problem, maybe the
             branch has been deleted after all?  This time contact the
             repository. */
          SVN_ERR(remove_obsolete_line(&state, lookup, log,
                                       subtree_mergeinfo, subtree_path,
                                       opt_state, NULL, FALSE, FALSE,
                                       iterpool));

          /* If still relevant, we need to keep the whole m/i on this node.
             Therefore, report the problem. */
          if (state != ds_deleted)
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
          SVN_ERR(remove_obsolete_line(&state, lookup, log,
                                       subtree_mergeinfo, subtree_path,
                                       opt_state, NULL, FALSE, FALSE,
                                       iterpool));
          if (state != ds_deleted)
            SVN_ERR(show_reverse_ranges(subtree_path, reverse_ranges,
                                        opt_state, iterpool));

          continue;
        }

      /* We don't know how to handle reverse ranges (there should be none).
         So, we must check for them - just to be sure. */
      non_recursive_ranges = find_non_recursive_ranges(subtree_ranges,
                                                       iterpool);
      if (non_recursive_ranges->nelts)
        {
          /* We really found non-recursive merges?
             Try to get rid of them. */
          SVN_ERR(remove_obsolete_line(&state, lookup, log,
                                       subtree_mergeinfo, subtree_path,
                                       opt_state, NULL, FALSE, FALSE,
                                       iterpool));
          if (state != ds_deleted)
            SVN_ERR(show_non_recursive_ranges(subtree_path,
                                              non_recursive_ranges,
                                              opt_state, iterpool));

          continue;
        }

      non_recursive_ranges = find_non_recursive_ranges(parent_ranges,
                                                       iterpool);
      if (non_recursive_ranges->nelts)
        {
          /* We really found non-recursive merges at the parent?
             Try to get rid of them at the parent and sub-node alike. */
          SVN_ERR(remove_obsolete_line(&state, lookup, log,
                                       subtree_mergeinfo, parent_path,
                                       opt_state, NULL, FALSE, FALSE,
                                       iterpool));
          if (state == ds_deleted)
            SVN_ERR(remove_obsolete_line(&state, lookup, log,
                                         subtree_mergeinfo, subtree_path,
                                         opt_state, NULL, FALSE, FALSE,
                                         iterpool));
          if (state != ds_deleted)
            SVN_ERR(show_non_recursive_ranges(parent_path,
                                              non_recursive_ranges,
                                              opt_state, iterpool));

          continue;
        }

      /* Try the actual elision, i.e. compare parent and sub-tree m/i.
         Where they don't fit, figure out if they can be aligned. */
      SVN_ERR(svn_rangelist_diff(&parent_only, &subtree_only,
                                 parent_ranges, subtree_ranges, TRUE,
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

      /* Find revs that are missing in the sub-tree- m/i but affect paths in
         the sub-tree. */
      operative_in_subtree
        = svn_min__operative(log, subtree_path, parent_only, iterpool);

      /* Remove revision ranges that are implied by the "natural" history
         of the merged branch vs. the current branch. */
      SVN_ERR(remove_overlapping_history(&implied_in_subtree,
                                         &operative_in_subtree, log,
                                         subtree_path, fs_path,
                                         iterpool, iterpool));
      SVN_ERR(remove_overlapping_history(&implied_in_parent,
                                         &operative_outside_subtree, log,
                                         parent_path, parent_fs_path,
                                         iterpool, iterpool));

      /* Before we show a branch as "CANNOT elide", make sure it is even
         still relevant. */
      if (   operative_outside_subtree->nelts
          || operative_in_subtree->nelts)
        {
          /* This branch can't be elided.  Maybe, it is obsolete anyway. */
          SVN_ERR(remove_obsolete_line(&state, lookup, log,
                                       subtree_mergeinfo, subtree_path,
                                       opt_state, NULL, FALSE, FALSE,
                                       iterpool));
          if (state == ds_deleted)
            continue;
        }

      /* Log whether an elision was possible. */
      SVN_ERR(show_branch_elision(subtree_path, subtree_only,
                                  parent_only, operative_outside_subtree,
                                  operative_in_subtree, implied_in_parent,
                                  implied_in_subtree, opt_state, iterpool));

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
      const char *fs_path;
      svn_mergeinfo_t parent_mergeinfo;
      svn_mergeinfo_t subtree_mergeinfo;
      svn_mergeinfo_t subtree_mergeinfo_copy;
      svn_mergeinfo_t mergeinfo_to_report;

      svn_pool_clear(iterpool);
      progress.nodes_todo = i;

      /* Get the relevant mergeinfo. */
      svn_min__get_mergeinfo_pair(&fs_path, &parent_path, &relpath,
                                  &parent_mergeinfo, &subtree_mergeinfo,
                                  wc_mergeinfo, i);
      SVN_ERR(show_elision_header(parent_path, relpath, opt_state,
                                  scratch_pool));

      /* Modify the mergeinfo here. */
      subtree_mergeinfo_copy = svn_mergeinfo_dup(subtree_mergeinfo,
                                                 iterpool);

      /* Eliminate redundant sub-node mergeinfo. */
      if (opt_state->remove_redundants && parent_mergeinfo)
        {
          svn_mergeinfo_t parent_mergeinfo_copy;
          mergeinfo_to_report = subtree_mergeinfo_copy;

          /* Try to elide the mergeinfo for all branches. */
          parent_mergeinfo_copy = svn_mergeinfo_dup(parent_mergeinfo,
                                                    iterpool);

          SVN_ERR(remove_lines(log, lookup, fs_path, relpath,
                               parent_mergeinfo_copy, subtree_mergeinfo_copy,
                               opt_state, iterpool));

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
              SVN_ERR(remove_obsolete_lines(lookup, log, subtree_mergeinfo,
                                            opt_state, &progress, FALSE,
                                            iterpool));
            }
        }
      else
        {
          /* Eliminate deleted branches. */
          mergeinfo_to_report = subtree_mergeinfo;
          SVN_ERR(remove_obsolete_lines(lookup, log, subtree_mergeinfo,
                                        opt_state, &progress, FALSE,
                                        iterpool));
        }

      /* Reduce the number of remaining ranges. */
      SVN_ERR(shorten_lines(subtree_mergeinfo, log, opt_state, &progress,
                            iterpool));

      /* Display what's left. */
      SVN_ERR(show_elision_result(parent_mergeinfo, mergeinfo_to_report,
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

static void
eliminate_subpaths(apr_array_header_t *paths)
{
  int source, dest;
  if (paths->nelts < 2)
    return;

  svn_sort__array(paths, svn_sort_compare_paths);

  for (source = 1, dest = 0; source < paths->nelts; ++source)
    {
      const char *source_path = APR_ARRAY_IDX(paths, source, const char *);
      const char *dest_path = APR_ARRAY_IDX(paths, dest, const char *);

      if (!svn_dirent_is_ancestor(dest_path, source_path))
        {
          ++dest;
          APR_ARRAY_IDX(paths, dest, const char *) = source_path;
        }
    }

  paths->nelts = dest + 1;
}

static svn_error_t *
show_obsoletes_summary(svn_min__branch_lookup_t *lookup,
                       svn_min__log_t *log,
                       svn_min__opt_state_t *opt_state,
                       apr_pool_t *scratch_pool)
{
  apr_array_header_t *paths;
  apr_pool_t *iterpool;
  int i;

  if (!opt_state->run_analysis || !opt_state->remove_obsoletes)
    return SVN_NO_ERROR;

  paths = svn_min__branch_deleted_list(lookup, scratch_pool, scratch_pool);
  if (!paths->nelts)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                            _("\nNo missing branches were detected.\n\n")));
      return SVN_NO_ERROR;
    }

  iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_cmdline_printf(iterpool,
                             _("\nEncountered %d missing branches:\n"),
                             paths->nelts));
  for (i = 0; i < paths->nelts; ++i)
    {
      svn_revnum_t deletion_rev;
      apr_array_header_t *surviving_copies = NULL;
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      svn_pool_clear(iterpool);
      surviving_copies = apr_array_make(iterpool, 16, sizeof(const char *));
      if (log)
        {
          /* Look for a deletion since the last creation
           * (or parent creation). */
          svn_revnum_t creation_rev = svn_min__find_copy(log, path,
                                                         SVN_INVALID_REVNUM,
                                                         0, iterpool);
          deletion_rev = svn_min__find_deletion(log, path,
                                                SVN_INVALID_REVNUM,
                                                creation_rev, iterpool);
          find_surviving_copies(surviving_copies, log, path,
                                SVN_IS_VALID_REVNUM(deletion_rev) 
                                  ? deletion_rev - 1
                                  : deletion_rev,
                                creation_rev,
                                scratch_pool, scratch_pool);
        }
      else
        {
          deletion_rev = SVN_INVALID_REVNUM;
        }

      if (surviving_copies->nelts)
        {
          int k;
          int limit = opt_state->verbose ? INT_MAX : 4;

          eliminate_subpaths(surviving_copies);
          SVN_ERR(svn_cmdline_printf(iterpool,
                                     _("    [copied or moved] %s\n"),
                                     path));
          for (k = 0; k < surviving_copies->nelts && k < limit; ++k)
            {
              path = APR_ARRAY_IDX(surviving_copies, k, const char *);
              SVN_ERR(svn_cmdline_printf(iterpool,
                                         _("        -> %s\n"),
                                         path));
            }

          if (k < surviving_copies->nelts)
            SVN_ERR(svn_cmdline_printf(iterpool,
                                       _("        (and %d more)\n"),
                                       surviving_copies->nelts - k));
        }
      else if (SVN_IS_VALID_REVNUM(deletion_rev))
        SVN_ERR(svn_cmdline_printf(iterpool, _("    [r%ld] %s\n"),
                                  deletion_rev, path));
      else if (log)
        SVN_ERR(svn_cmdline_printf(iterpool, _("    [potential branch] %s\n"),
                                   path));
      else
        SVN_ERR(svn_cmdline_printf(iterpool, _("    %s\n"), path));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
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
      svn_min__branch_lookup_t *lookup = cmd_baton->lookup;
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
      if (!lookup && needs_session(cmd_baton->opt_state))
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

      SVN_ERR(svn_min__remove_empty_mergeinfo(wc_mergeinfo));

      /* Show a summary of deleted branches. */
      SVN_ERR(show_obsoletes_summary(lookup, log, cmd_baton->opt_state,
                                     iterpool));

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
