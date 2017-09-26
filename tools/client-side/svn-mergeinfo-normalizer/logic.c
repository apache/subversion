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

/* Scan RANGES for reverse merge ranges and return a copy of them,
 * allocated in RESULT_POOL. */
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

/* Scan RANGES for non-recursive merge ranges and return a copy of them,
 * allocated in RESULT_POOL. */
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

/* Print RANGES, prefixed by TITLE to console.  Use SCRATCH_POOL for
 * temporary allocations. */
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

/* Depending on the settings in OPT_STATE, write a message on console
 * that SUBTREE_PATH is not mentioned in the parent mergeinfo.  If the
 * MISALIGNED flag is set, then the relative path did not match.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
show_missing_parent(const char *subtree_path,
                    svn_boolean_t misaligned,
                    svn_min__opt_state_t *opt_state,
                    apr_pool_t *scratch_pool)
{
  /* Be quiet in normal processing mode. */
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

/* If REVERSE_RANGES is not empty and depending on the options in OPT_STATE,
 * show those ranges as "reverse ranges" for path SUBTREE_PATH.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
show_reverse_ranges(const char *subtree_path,
                    svn_rangelist_t *reverse_ranges,
                    svn_min__opt_state_t *opt_state,
                    apr_pool_t *scratch_pool)
{
  if (!reverse_ranges->nelts)
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

/* If NON_RECURSIVE_RANGES is not empty and depending on the options in
 * OPT_STATE, show those ranges as "reverse ranges" for path SUBTREE_PATH.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
show_non_recursive_ranges(const char *subtree_path,
                          svn_rangelist_t *non_recursive_ranges,
                          svn_min__opt_state_t *opt_state,
                          apr_pool_t *scratch_pool)
{
  if (!non_recursive_ranges->nelts)
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

/* Show the elision result of a single BRANCH (for a single node) on
 * console filtered by the OPT_STATE.  OPERATIVE_OUTSIDE_SUBTREE and
 * OPERATIVE_IN_SUBTREE are the revision ranges that prevented an elision.
 * SUBTREE_ONLY and PARENT_ONLY were differences that have been adjusted.
 * IMPLIED_IN_PARENT and IMPLIED_IN_SUBTREE are differences that could be
 * ignored.   Uses SCRATCH_POOL for temporary allocations. */
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
  else if (   opt_state->verbose
           || (opt_state->run_analysis && (   implied_in_parent->nelts
                                           || subtree_only->nelts
                                           || implied_in_subtree->nelts
                                           || parent_only->nelts)))
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

/* Progress tacking data structure. */
typedef struct progress_t
{
  /* Number of nodes with mergeinfo that we need to process. */
  int nodes_total;

  /* Number of nodes still to process. */
  int nodes_todo;

  /* Counter for nodes where the mergeinfo could be removed entirely. */
  apr_int64_t nodes_removed;

  /* Number mergeinfo lines removed because the respective branches had
   * been deleted. */
  apr_int64_t obsoletes_removed;

  /* Number of ranges combined so far. */
  apr_int64_t ranges_removed;

  /* Transient flag used to indicate whether we still have to print a
   * header before showing various details. */
  svn_boolean_t needs_header;
} progress_t;

/* Describes the "deletion" state of a branch. */
typedef enum deletion_state_t
{
  /* Path still exists. */
  ds_exists,

  /* Path does not exist but has not been deleted.
   * Catch-up merges etc. may introduce the path. */
  ds_implied,

  /* A (possibly indirect) copy of the path or one of its sub-nodes still
   * exists. */
  ds_has_copies,

  /* The path has been deleted (explicitly or indirectly via parent) and
   * no copy exists @HEAD. */
  ds_deleted
} deletion_state_t;

/* Show the "removing obsoletes" header depending on OPT_STATE and PROGRESS.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
show_removing_obsoletes(svn_min__opt_state_t *opt_state,
                        progress_t *progress,
                        apr_pool_t *scratch_pool)
{
  if (   opt_state->remove_obsoletes
      && opt_state->verbose
      && progress
      && progress->needs_header)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                       _("\n    Trying to remove obsolete entries ...\n")));
      progress->needs_header = FALSE;
    }

  return SVN_NO_ERROR;
}

/* If in verbose mode according to OPT_STATE, print the deletion status
 * DELETION_STATE for SUBTREE_PATH to the console.  If REPORT_NON_REMOVALS
 * is set, report missing branches that can't be removed from mergeinfo.
 * In that case, show a SURVIVING_COPY when appropriate.
 *
 * Prefix the output with the appropriate section header based on the state
 * tracked in PROGRESS.  Use SCRATCH_POOL for temporaries.
 */
static svn_error_t *
show_removed_branch(const char *subtree_path,
                    svn_min__opt_state_t *opt_state,
                    deletion_state_t deletion_state,
                    svn_boolean_t report_non_removals,
                    const char *surviving_copy,
                    progress_t *progress,
                    apr_pool_t *scratch_pool)
{
  if (opt_state->verbose)
    switch (deletion_state)
      {
        case ds_deleted:
          SVN_ERR(show_removing_obsoletes(opt_state, progress,
                                          scratch_pool));
          SVN_ERR(svn_cmdline_printf(scratch_pool,
                                     _("    remove deleted branch %s\n"),
                                     subtree_path));
          break;

        case ds_implied:
          if (report_non_removals)
            {
              SVN_ERR(show_removing_obsoletes(opt_state, progress,
                                              scratch_pool));
              SVN_ERR(svn_cmdline_printf(scratch_pool,
                                         _("    keep POTENTIAL branch %s\n"),
                                         subtree_path));
            }
          break;

        case ds_has_copies:
          if (report_non_removals)
            {
              SVN_ERR(show_removing_obsoletes(opt_state, progress,
                                              scratch_pool));
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

/* If COPY copies SOURCE or one of its ancestors, return the path that the
 * node has in the copy target, allocated in RESULT_POOL.  Otherwise,
 * simply return the copy target, allocated in RESULT_POOL. */
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

/* Scan LOG for a copies of PATH or one of its sub-nodes from the segment
 * starting at START_REV down to END_REV.  Follow those copies until we
 * find one that has not been deleted @HEAD.  If none exist, return NULL.
 * Otherwise the return first such copy we find, allocated in RESULT_POOL.
 * Use SCRATCH_POOL for temporaries. */
static const char *
find_surviving_copy(svn_min__log_t *log,
                    const char *path,
                    svn_revnum_t start_rev,
                    svn_revnum_t end_rev,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  const char * survivor = NULL;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *copies = svn_min__get_copies(log, path, start_rev,
                                                   end_rev, scratch_pool,
                                                   scratch_pool);

  int i;
  for (i = 0; (i < copies->nelts) && !survivor; ++i)
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
          survivor = find_surviving_copy(log, copy_target,
                                         copy->revision, deletion_rev - 1,
                                         result_pool, iterpool);
        }
      else
        {
          survivor = apr_pstrdup(result_pool, copy_target);
        }
    }
 
  svn_pool_destroy(iterpool);

  return survivor;
}

/* Scan LOG for a copies of PATH or one of its sub-nodes from the segment
 * starting at START_REV down to END_REV.  Follow those copies and collect
 * those that have not been deleted @HEAD.  Return them in *SURVIVORS,
 * allocated in RESULT_POOL.  Use SCRATCH_POOL for temporary allocations. */
static void
find_surviving_copies(apr_array_header_t *survivors,
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
          find_surviving_copies(survivors, log, copy_target,
                                copy->revision, deletion_rev - 1,
                                result_pool, iterpool);
        }
      else
        {
          APR_ARRAY_PUSH(survivors, const char *) = apr_pstrdup(result_pool,
                                                                copy_target);
        }
    }
 
  svn_pool_destroy(iterpool);
}

/* Using LOOKUP and LOG, determine the deletion *STATE of PATH.  OPT_STATE,
 * PROGRESS and REPORT_NON_REMOVALS control the console output.  OPT_STATE
 * also makes this a no-op if removal of deleted branches is not been
 * enabled in it.
 *
 * If LOCAL_ONLY is set, only remove branches that are known to have been
 * deleted (as per LOOKUP) with no surviving copies etc.  This is for quick
 * checks.
 *
 * Track progress in PROGRESS and update MERGEINFO if we can remove the
 * info for branch PATH from it.
 *
 * Use SCRATCH_POOL for temporaries.
 */
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

  /* Skip if removal of deleted branches has not been . */
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
                              surviving_copy, progress, scratch_pool));

  return SVN_NO_ERROR;
}

/* If enabled in OPT_STATE, use LOG and LOOKUP to remove all lines form
 * MERGEINFO that refer to deleted branches.
 *
 * If LOCAL_ONLY is set, only remove branches that are known to have been
 * deleted as per LOOKUP - this is for quick checks.  Track progress in
 * PROGRESS.
 *
 * Use SCRATCH_POOL for temporaries.
 */
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

  /* Skip if removal of deleted branches has not been . */
  if (!opt_state->remove_obsoletes)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);

  /* Sort branches by name to ensure a nicely sorted operations log. */
  sorted_mi = svn_sort__hash(mergeinfo,
                             svn_sort_compare_items_lexically,
                             scratch_pool);

  /* Only show the section header if we removed at least one line. */
  progress->needs_header = TRUE;

  /* Simply iterate over all branches mentioned in the mergeinfo. */
  for (i = 0; i < sorted_mi->nelts; ++i)
    {
      const char *path = APR_ARRAY_IDX(sorted_mi, i, svn_sort__item_t).key;
      deletion_state_t state;

      svn_pool_clear(iterpool);
      SVN_ERR(remove_obsolete_line(&state, lookup, log, mergeinfo, path,
                                   opt_state, progress, local_only, TRUE,
                                   iterpool));
    }

  progress->needs_header = FALSE;
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Return the ancestor of CHILD such that adding RELPATH to it leads to
 * CHILD.  Return an empty string if no such ancestor exists.  Allocate the
 * result in RESULT_POOL. */
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

/* Remove all ranges from *RANGES where the history of SOURCE_PATH@RANGE
 * and TARGET_PATH@HEAD overlap.  Return the list of *REMOVED ranges,
 * allocated in RESULT_POOL.  Use SCRATCH_POOL for temporary allocations.
 *
 * Note that SOURCE_PATH@RANGE may actually refer to different branches
 * created or re-created and then deleted at different points in time.
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

/* Scan RANGES for non-recursive ranges.  If there are any, remove all
 * ranges that where the history of SOURCE_PATH@RANGE and TARGET_PATH@HEAD
 * overlap.  Also remove all ranges that are not operative on OP_PATH.
 *
 * The remaining ranges are the ones actually relevant to a future merge.
 * Return those in *NON_RECURSIVE_RANGES, allocated in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations.
 *
 * Note that SOURCE_PATH@RANGE may actually refer to different branches
 * created or re-created and then deleted at different points in time.
 */
static svn_error_t *
find_relevant_non_recursive_ranges(svn_rangelist_t **non_recursive_ranges,
                                   svn_rangelist_t *ranges,
                                   svn_min__log_t *log,
                                   const char *source_path,
                                   const char *target_path,
                                   const char *op_path,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  apr_array_header_t *implied;
  apr_array_header_t *result
    = find_non_recursive_ranges(ranges, scratch_pool);

  SVN_ERR(remove_overlapping_history(&implied, &result,
                                     log, source_path, target_path,
                                     scratch_pool, scratch_pool));
  *non_recursive_ranges = svn_min__operative(log, op_path, result,
                                             result_pool);

  return SVN_NO_ERROR;
}

/* Show the results of an attempt at "misaligned branch elision".
 * SOURCE_BRANCH was to be elided because TARGET_BRANCH would cover it all.
 * There were MISSING revisions exclusively in SOURCE_BRANCH.  OPT_STATE
 * filters the output and SCRATCH_POOL is used for temporary allocations.
 */
static svn_error_t *
show_misaligned_branch_elision(const char *source_branch,
                               const char *target_branch,
                               svn_rangelist_t *missing,
                               svn_min__opt_state_t *opt_state,
                               apr_pool_t *scratch_pool)
{
  if (opt_state->verbose || opt_state->run_analysis)
    {
      if (missing->nelts)
        {
          SVN_ERR(svn_cmdline_printf(scratch_pool,
                        _("    CANNOT elide MISALIGNED branch %s\n"
                          "        to likely correctly aligned branch %s\n"),
                                     source_branch, target_branch));
          SVN_ERR(print_ranges(missing,
                        _("revisions not merged from likely correctly"
                          " aligned branch: "),
                               scratch_pool));
        }
      else
        {
          SVN_ERR(svn_cmdline_printf(scratch_pool,
                        _("    elide misaligned branch %s\n"
                          "        to likely correctly aligned branch %s\n"),
                                     source_branch, target_branch));
        }
    }

  return SVN_NO_ERROR;
}

/* Search MERGEINFO for branches that are sub-branches of one another.
 * If exactly one of them shares the base with the FS_PATH to which the m/i
 * is attached, than this is likely the properly aligned branch while the
 * others are misaligned.
 *
 * Using LOG, determine those misaligned branches whose operative merged
 * revisions are already covered by the merged revisions of the likely
 * correctly aligned branch.  In that case, remove those misaligned branch
 * entries from MERGEINFO.
 *
 * OPT_STATE filters the output and SCRATCH_POOL is used for temporaries.
 */
static svn_error_t *
remove_redundant_misaligned_branches(svn_min__log_t *log,
                                     const char *fs_path,
                                     svn_mergeinfo_t mergeinfo,
                                     svn_min__opt_state_t *opt_state,
                                     apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i, k;
  const char *base_name = svn_dirent_basename(fs_path, scratch_pool);
  apr_array_header_t *sorted_mi;

  sorted_mi = svn_sort__hash(mergeinfo,
                             svn_sort_compare_items_lexically,
                             scratch_pool);

  for (i = 0; i < sorted_mi->nelts - 1; i = k)
    {
      const char *item_path, *sub_item_path;
      int maybe_aligned_index = -1;
      int maybe_aligned_found = 0;
      int sub_branch_count = 0;

      svn_pool_clear(iterpool);

      /* Find the range of branches that are sub-branches of the one at I. */
      item_path = APR_ARRAY_IDX(sorted_mi, i, svn_sort__item_t).key;
      if (!strcmp(base_name, svn_dirent_basename(item_path, iterpool)))
        {
          maybe_aligned_index = i;
          maybe_aligned_found = 1;
        }

      for (k = i + 1; k < sorted_mi->nelts; ++k)
        {
          sub_item_path = APR_ARRAY_IDX(sorted_mi, k, svn_sort__item_t).key;
          if (!svn_dirent_is_ancestor(item_path, sub_item_path))
            break;

          if (!strcmp(base_name,
                      svn_dirent_basename(sub_item_path, iterpool)))
            {
              maybe_aligned_index = k;
              maybe_aligned_found++;
            }
        }

      /* Found any?  If so, did we identify exactly one of them as likely
       * being properly aligned? */
      sub_branch_count = k - i - 1;
      if ((maybe_aligned_found != 1) || (sub_branch_count == 0))
        continue;

      /* Try to elide all misaligned branches individually. */
      for (k = i; k < i + sub_branch_count + 1; ++k)
        {
          svn_sort__item_t *source_item, *target_item;
          svn_rangelist_t *missing, *dummy;

          /* Is this one of the misaligned branches? */
          if (k == maybe_aligned_index)
            continue;

          source_item = &APR_ARRAY_IDX(sorted_mi, k, svn_sort__item_t);
          target_item = &APR_ARRAY_IDX(sorted_mi, maybe_aligned_index,
                                       svn_sort__item_t);

          /* Elide into sub-branch or parent branch (can't be equal here).
           * Because we only know these are within the I tree, source and
           * target may be siblings.  Check that they actually have an
           * ancestor relationship.
           */
          if (k < maybe_aligned_index)
            {
              if (!svn_dirent_is_ancestor(source_item->key, target_item->key))
                continue;
            }
          else
            {
              if (!svn_dirent_is_ancestor(target_item->key, source_item->key))
                continue;
            }

          /* Determine which revisions are MISSING in target. */
          SVN_ERR(svn_rangelist_diff(&missing, &dummy,
                                     source_item->value, target_item->value,
                                     TRUE, iterpool));
          missing = svn_min__operative(log, source_item->key, missing,
                                       iterpool);

          /* Show the result and elide the branch if we can. */
          SVN_ERR(show_misaligned_branch_elision(source_item->key,
                                                 target_item->key,
                                                 missing,
                                                 opt_state,
                                                 iterpool));
          if (!missing->nelts)
            svn_hash_sets(mergeinfo, source_item->key, NULL);
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Try to elide as many lines from SUBTREE_MERGEINFO for node at FS_PATH as
 * possible using LOG and LOOKUP.  OPT_STATE determines if we may remove
 * deleted branches.  Elision happens by comparing the node's mergeinfo
 * with the PARENT_MERGEINFO using REL_PATH to match up the branch paths.
 *
 * SIBLING_MERGEINFO contains the mergeinfo of all nodes with mergeinfo
 * immediately below the parent.  It can be used to "summarize" m/i over
 * all sub-nodes and elide that to the parent.
 *
 * Use SCRATCH_POOL for temporaries.
 */
static svn_error_t *
remove_lines(svn_min__log_t *log,
             svn_min__branch_lookup_t *lookup,
             const char *fs_path,
             const char *relpath,
             svn_mergeinfo_t parent_mergeinfo,
             svn_mergeinfo_t subtree_mergeinfo,
             apr_array_header_t *sibling_mergeinfo,
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

      /* We don't know how to handle non-recursive ranges (they are legal,
       * though).  So, we must if there are any that would actually
       * affect future merges. */
      SVN_ERR(find_relevant_non_recursive_ranges(&non_recursive_ranges,
                                                 subtree_ranges, log,
                                                 subtree_path, fs_path,
                                                 subtree_path,
                                                 iterpool, iterpool));
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

      if (parent_ranges && parent_ranges->nelts)
        {
          /* Any non-recursive ranges at the parent node that are
           * operative on the sub-node and not implicit part of the
           * branch history? */
          SVN_ERR(find_relevant_non_recursive_ranges(&non_recursive_ranges,
                                                     parent_ranges, log,
                                                     parent_path,
                                                     parent_fs_path,
                                                     subtree_path,
                                                     iterpool, iterpool));
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
        }

      /* Are there any parent ranges to which to elide sub-tree m/i? */
      if (!parent_ranges)
        {
          /* There is none.  Before we flag that as a problem, maybe the
             branch has been deleted after all?  This time contact the
             repository. */
          SVN_ERR(remove_obsolete_line(&state, lookup, log,
                                       subtree_mergeinfo, subtree_path,
                                       opt_state, NULL, FALSE, FALSE,
                                       iterpool));

          if (state == ds_deleted)
            continue;

          /* Find revs that are missing in the sub-tree- m/i but affect
             paths in the sub-tree. */
          subtree_only = subtree_ranges;
          operative_in_subtree
            = svn_min__operative(log, subtree_path, subtree_only, iterpool);
          SVN_ERR(remove_overlapping_history(&implied_in_subtree,
                                             &operative_in_subtree, log,
                                             subtree_path, fs_path,
                                             iterpool, iterpool));

          if (operative_in_subtree->nelts)
            {
              /* If still relevant, we need to keep the whole m/i on this
                 node.  Therefore, report the problem. */
              SVN_ERR(show_missing_parent(subtree_path, !*parent_path,
                                          opt_state, scratch_pool));
            }
          else
            {
              /* This branch entry is some sort of artefact that doesn't
                 refer to any actual changes and can therefore be removed.
                 Report why that is. */
              apr_array_header_t *empty = operative_in_subtree;
              SVN_ERR(svn_rangelist_remove(&subtree_only, implied_in_subtree,
                                           subtree_only, TRUE, iterpool));
              SVN_ERR(show_branch_elision(subtree_path, empty,
                                          subtree_only, empty, empty, empty,
                                          implied_in_subtree, opt_state,
                                          iterpool));

              svn_hash_sets(subtree_mergeinfo, subtree_path, NULL);
            }

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

      /* Try harder:
       * There are cases where a merge affected multiple sibling nodes, got
       * recorded there but was not recorded at the parent.  Remove these
       * from the list of revisions that couldn't be propagated to the
       * parent node. */
      if (operative_outside_subtree->nelts && sibling_mergeinfo->nelts > 1)
        {
          apr_hash_t *sibling_ranges;
          SVN_ERR(svn_min__sibling_ranges(&sibling_ranges, sibling_mergeinfo,
                                          parent_path,
                                          operative_outside_subtree,
                                          iterpool, iterpool));

          operative_outside_subtree
            = svn_min__operative_outside_all_subtrees(log, parent_path,
                                                  operative_outside_subtree,
                                                      sibling_ranges,
                                                      iterpool, iterpool);
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

/* Return TRUE if revisions START to END are inoperative on PATH, according
 * to LOG.  Use SCRATCH_POOL for temporaries. */
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

/* Use LOG to determine what revision ranges in MERGEINFO can be combined
 * because the revisions in between them are inoperative on the respective
 * branch (sub-)path.   Combine those revision ranges and update PROGRESS.
 * Make this a no-op  if it has not been enabled in OPT_STATE.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
shorten_lines(svn_mergeinfo_t mergeinfo,
              svn_min__log_t *log,
              svn_min__opt_state_t *opt_state,
              progress_t *progress,
              apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;

  /* Skip if this operation has not been enabled. */
  if (!opt_state->combine_ranges)
    return SVN_NO_ERROR;

  /* Process each branch independently. */
  for (hi = apr_hash_first(scratch_pool, mergeinfo);
       hi;
       hi = apr_hash_next(hi))
    {
      int source, dest;
      const char *path = apr_hash_this_key(hi);
      svn_rangelist_t *ranges = apr_hash_this_val(hi);

      /* Skip edge cases. */
      if (ranges->nelts < 2 || find_reverse_ranges(ranges, iterpool)->nelts)
        continue;

      /* Merge ranges where possible. */
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

      /* Update progress. */
      progress->ranges_removed += ranges->nelts - dest - 1;
      ranges->nelts = dest + 1;
    }

  return SVN_NO_ERROR;
}

/* Construct a 1-line progress info based on the PROGRESS and selected
 * processing options in OPT_STATE.  Allocate the result in RESULT_POOL
 * and use SCRATCH_POOL for temporaries. */
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

/* Depending on the options in OPT_STATE, print the header to be shown
 * before processing the m/i at REL_PATH relative to the parent mergeinfo
 * at PARENT_PATH.  If there is no parent m/i, RELPATH is empty. 
 * Use SCRATCH_POOL temporary allocations.*/
static svn_error_t *
show_elision_header(const char *parent_path,
                    const char *relpath,
                    svn_min__opt_state_t *opt_state,
                    apr_pool_t *scratch_pool)
{
  if (opt_state->verbose)
    {
      /* In verbose mode, be specific of what gets elided to where. */
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
      /* If we are not in analysis mode, only the progress would be shown
       * and we would stay quiet here. */
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                _("Trying to elide mergeinfo at path %s\n"),
                                svn_dirent_join(parent_path, relpath,
                                                scratch_pool)));
    }

  return SVN_NO_ERROR;
}

/* Given the PARENT_MERGEINFO and the current nodes's SUBTREE_MERGEINFO
 * after the processing / elision attempt, print a summary of the results
 * to console.  Get the verbosity setting from OPT_STATE.  Use SCRATCH_POOL
 * for temporary allocations. */
static svn_error_t *
show_elision_result(svn_mergeinfo_t parent_mergeinfo,
                    svn_mergeinfo_t subtree_mergeinfo,
                    svn_min__opt_state_t *opt_state,
                    apr_pool_t *scratch_pool)
{
  if (opt_state->verbose)
    {
      /* In verbose mode, tell the user what branches survived. */
      if (apr_hash_count(subtree_mergeinfo))
        {
          apr_array_header_t *sorted_mi;
          int i;
          apr_pool_t *iterpool = svn_pool_create(scratch_pool);

          if (parent_mergeinfo)
            SVN_ERR(svn_cmdline_printf(scratch_pool,
                      _("\n    Sub-tree merge info cannot be elided due to "
                        "the following branch(es):\n")));
          else
            SVN_ERR(svn_cmdline_printf(scratch_pool,
                _("\n    Merge info kept for the following branch(es):\n")));

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
      /* If we are not in analysis mode, only the progress would be shown
       * and we would stay quiet here. */
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

/* Main normalization function. Process all mergeinfo in WC_MERGEINFO, one
 * by one, bottom-up and try to elide it by comparing it with and aligning
 * it to the respective parent mergeinfo.  This modified the contents of
 * WC_MERGEINFO.
 *
 * LOG and LOOKUP provide the repository info needed to perform the
 * normalization steps selected in OPT_STATE.  LOG and LOOKUP may be NULL.
 *
 * Use SCRATCH_POOL for temporary allocations.
 */
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
      apr_array_header_t *sibling_mergeinfo;

      svn_pool_clear(iterpool);
      progress.nodes_todo = i;

      /* Get the relevant mergeinfo. */
      svn_min__get_mergeinfo_pair(&fs_path, &parent_path, &relpath,
                                  &parent_mergeinfo, &subtree_mergeinfo,
                                  &sibling_mergeinfo, wc_mergeinfo, i);
      SVN_ERR(show_elision_header(parent_path, relpath, opt_state,
                                  scratch_pool));

      /* Get rid of some of the easier cases of misaligned branches.
       * Directly modify the orignal mergeinfo. */
      if (opt_state->remove_redundant_misaligned)
        SVN_ERR(remove_redundant_misaligned_branches(log, fs_path,
                                                     subtree_mergeinfo,
                                                     opt_state, iterpool));

      /* Modify this copy of the mergeinfo.
       * If we can elide it all, drop the original. */
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
                               sibling_mergeinfo, opt_state, iterpool));

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

/* Return TRUE, if the operations selected in OPT_STATE require the log. */
static svn_boolean_t
needs_log(svn_min__opt_state_t *opt_state)
{
  return opt_state->combine_ranges || opt_state->remove_redundants;
}

/* Return TRUE, if the operations selected in OPT_STATE require a
 * connection (session) to the repository. */
static svn_boolean_t
needs_session(svn_min__opt_state_t *opt_state)
{
  return opt_state->remove_obsoletes;
}

/* Based on the operation selected in OPT_STATE, return a descriptive
 * string of what we plan to do.  Allocate that string in RESULT_POOL. */
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

/* Sort paths in PATHS and remove all paths whose ancestors are also in
 * PATHS. */
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

/* If enabled by OPT_STATE, show the list of missing paths encountered by
 * LOOKUP and use LOG to determine their fate.  LOG may be NULL. 
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
show_obsoletes_summary(svn_min__branch_lookup_t *lookup,
                       svn_min__log_t *log,
                       svn_min__opt_state_t *opt_state,
                       apr_pool_t *scratch_pool)
{
  apr_array_header_t *paths;
  apr_pool_t *iterpool;
  int i;

  /* Skip when summary has not been enabled */
  if (!opt_state->run_analysis || !opt_state->remove_obsoletes)
    return SVN_NO_ERROR;

  /* Get list of all missing paths.  Early exist if there are none. */
  paths = svn_min__branch_deleted_list(lookup, scratch_pool, scratch_pool);
  if (!paths->nelts)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                            _("\nNo missing branches were detected.\n\n")));
      return SVN_NO_ERROR;
    }

  /* Process them all. */
  iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_cmdline_printf(iterpool,
                             _("\nEncountered %d missing branch(es):\n"),
                             paths->nelts));
  for (i = 0; i < paths->nelts; ++i)
    {
      svn_revnum_t deletion_rev;
      apr_array_header_t *surviving_copies = NULL;
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      /* For PATH, gather deletion and copy survival info. */
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

      /* Show state / results to the extend we've got them. */
      if (surviving_copies->nelts)
        {
          int k;

          /* There maybe thousands of surviving (sub-node) copies.
           * Restrict the output unless the user asked us to be verbose. */
          int limit = opt_state->verbose ? INT_MAX : 4;

          /* Reasonably reduce the output. */
          eliminate_subpaths(surviving_copies);
          SVN_ERR(svn_cmdline_printf(iterpool,
                                     _("    [r%ld, copied or moved] %s\n"),
                                     deletion_rev, path));
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

/* Set the path and url members in BATON to handle the IDX-th target
 * specified at the command line.  Allocate the paths in RESULT_POOL and
 * use SCRATCH_POOL for temporaries. */
static svn_error_t *
add_wc_info(svn_min__cmd_baton_t *baton,
            int idx,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  svn_min__opt_state_t *opt_state = baton->opt_state;
  const char *target = APR_ARRAY_IDX(opt_state->targets, idx, const char *);
  const char *truepath;
  svn_opt_revision_t peg_revision;

  if (svn_path_is_url(target))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("'%s' is not a local path"), target);

  SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target,
                             scratch_pool));
  SVN_ERR(svn_dirent_get_absolute(&baton->local_abspath, truepath,
                                  result_pool));

  SVN_ERR(svn_client_get_wc_root(&baton->wc_root, baton->local_abspath,
                                 baton->ctx, result_pool, scratch_pool));
  SVN_ERR(svn_client_get_repos_root(&baton->repo_root, NULL,
                                    baton->local_abspath, baton->ctx,
                                    result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Set *URL to a URL within CMD_BATON's repository that covers all FS paths
 * in WC_MERGEINFO.  Use SESSION to access the repository.  Allocate *URL
 * in RESULT_POOL and use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
get_url(const char **url,
        apr_array_header_t *wc_mergeinfo,
        svn_ra_session_t *session,
        svn_min__cmd_baton_t *cmd_baton,
        apr_pool_t *result_pool,
        apr_pool_t *scratch_pool)
{
  /* This is the deepest FS path that we may use. */
  const char *path = svn_min__common_parent(wc_mergeinfo, scratch_pool,
                                            scratch_pool);
  SVN_ERR_ASSERT(*path == '/');
  ++path;

  /* While we are not at the repository root, check that PATH actually
   * exists @HEAD.  If it doesn't retry with its parent. */
  while (strlen(path))
    {
      svn_node_kind_t kind;
      SVN_ERR(svn_ra_check_path(session, path, SVN_INVALID_REVNUM, &kind,
                                scratch_pool));
      if (kind != svn_node_none)
        break;

      path = svn_dirent_dirname(path, scratch_pool);
    }

  /* Construct the result. */
  *url = svn_path_url_add_component2(cmd_baton->repo_root, path,
                                     result_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_min__run_normalize(void *baton,
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
      svn_ra_session_t *session = NULL;
      svn_min__branch_lookup_t *lookup = cmd_baton->lookup;

      /* next target */
      svn_pool_clear(iterpool);
      SVN_ERR(add_wc_info(baton, i, iterpool, subpool));

      /* scan working copy */
      svn_pool_clear(subpool);
      SVN_ERR(svn_min__read_mergeinfo(&wc_mergeinfo, cmd_baton, iterpool,
                                      subpool));

      /* Any mergeinfo at all? */
      if (wc_mergeinfo->nelts == 0)
        continue;

      /* Open RA session.  Even if we don't need it for LOOKUP, checking
       * the url for the LOG will require the session object. */
      if (   (!lookup && needs_session(cmd_baton->opt_state))
          || needs_log(cmd_baton->opt_state))
        {
          svn_pool_clear(subpool);
          SVN_ERR(add_wc_info(baton, i, iterpool, subpool));
          SVN_ERR(svn_client_open_ra_session2(&session, cmd_baton->repo_root,
                                              NULL, cmd_baton->ctx, iterpool,
                                              subpool));
          if (!lookup)
            lookup = svn_min__branch_lookup_create(session, iterpool);
        }

      /* fetch log */
      if (needs_log(cmd_baton->opt_state))
        {
          const char *url;

          svn_pool_clear(subpool);
          SVN_ERR(get_url(&url, wc_mergeinfo, session, cmd_baton, subpool,
                          subpool));
          SVN_ERR(svn_min__log(&log, url, cmd_baton, iterpool, subpool));
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
