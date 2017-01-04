/*
 * log.c -- Fetch log data and implement the log queries
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

#include "svn_cmdline.h"
#include "svn_client.h"
#include "svn_dirent_uri.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_sorts.h"
#include "svn_pools.h"
#include "svn_hash.h"

#include "private/svn_fspath.h"
#include "private/svn_subr_private.h"
#include "private/svn_sorts_private.h"

#include "mergeinfo-normalizer.h"

#include "svn_private_config.h"



/* Describes all changes of a single revision.
 * Note that all strings are shared within a given svn_min__log_t instance.
 */
typedef struct log_entry_t
{
  /* Revision being described. */
  svn_revnum_t revision;

  /* FS path that is equal or a parent of any in PATHS. */
  const char *common_base;

  /* Sorted list of all FS paths touched. Elements are const char*. */
  apr_array_header_t *paths;
} log_entry_t;

/* Describes a deletion.
 * Note that replacements are treated as additions + deletions.
 */
typedef struct deletion_t
{
  /* Path being deleted (or replaced). */
  const char *path;

  /* Revision in which this deletion happened.*/
  svn_revnum_t revision;
} deletion_t;

/* Note that all FS paths are internalized and shared within this object.
 */
struct svn_min__log_t
{
  /* Dictionary of all FS paths used in this log.
   * The hash itself is only temporary and will be destroyed after the log
   * has been constructed and all paths got internalized. */
  apr_hash_t *unique_paths;

  /* Oldest revision we received. */
  svn_revnum_t first_rev;

  /* Latest revision we received. */
  svn_revnum_t head_rev;

  /* Log contents we received.  Entries are log_entry_t *. */
  apr_array_header_t *entries;

  /* List of all copy operations we encountered, sorted by target&rev. */
  apr_array_header_t *copies;

  /* Like COPIES but sorted by source&source-rev. */
  apr_array_header_t *copies_by_source;

  /* List of all deletions we encountered, sorted by path&rev. */
  apr_array_header_t *deletions;

  /* If set, don't show progress nor summary. */
  svn_boolean_t quiet;
};

/* Comparison function defining the order in svn_min__log_t.COPIES. */
static int
copy_order(const void *lhs,
           const void *rhs)
{
  const svn_min__copy_t *lhs_copy = *(const svn_min__copy_t * const *)lhs;
  const svn_min__copy_t *rhs_copy = *(const svn_min__copy_t * const *)rhs;

  int diff = strcmp(lhs_copy->path, rhs_copy->path);
  if (diff)
    return diff;

  if (lhs_copy->revision < rhs_copy->revision)
    return -1;

  return lhs_copy->revision == rhs_copy->revision ? 0 : 1;
}

/* Comparison function defining the order in svn_min__log_t.COPIES_BY_SOURCE.
 */
static int
copy_by_source_order(const void *lhs,
                     const void *rhs)
{
  const svn_min__copy_t *lhs_copy = *(const svn_min__copy_t * const *)lhs;
  const svn_min__copy_t *rhs_copy = *(const svn_min__copy_t * const *)rhs;

  int diff = strcmp(lhs_copy->copyfrom_path, rhs_copy->copyfrom_path);
  if (diff)
    return diff;

  if (lhs_copy->copyfrom_revision < rhs_copy->copyfrom_revision)
    return -1;

  return lhs_copy->copyfrom_revision == rhs_copy->copyfrom_revision ? 0 : 1;
}

/* Comparison function defining the order in svn_min__log_t.DELETIONS. */
static int
deletion_order(const void *lhs,
               const void *rhs)
{
  const deletion_t *lhs_deletion = *(const deletion_t * const *)lhs;
  const deletion_t *rhs_deletion = *(const deletion_t * const *)rhs;

  int diff = strcmp(lhs_deletion->path, rhs_deletion->path);
  if (diff)
    return diff;

  if (lhs_deletion->revision < rhs_deletion->revision)
    return -1;

  return lhs_deletion->revision == rhs_deletion->revision ? 0 : 1;
}

/* Return the string stored in UNIQUE_PATHS with the value PATH of PATH_LEN
 * characters.  If the hash does not have a matching entry, add one.
 * Allocate all strings in RESULT_POOL. */
static const char *
internalize(apr_hash_t *unique_paths,
            const char *path,
            apr_ssize_t path_len,
            apr_pool_t *result_pool)
{
  const char *result = apr_hash_get(unique_paths, path, path_len);
  if (result == NULL)
    {
      result = apr_pstrmemdup(result_pool, path, path_len);
      apr_hash_set(unique_paths, result, path_len, result);
    }

  return result;
}

/* Implements svn_log_entry_receiver_t.  Copies the info of LOG_ENTRY into
 * (svn_min__log_t *)BATON. */
static svn_error_t *
log_entry_receiver(void *baton,
                   svn_log_entry_t *log_entry,
                   apr_pool_t *scratch_pool)
{
  svn_min__log_t *log = baton;
  apr_pool_t *result_pool = log->entries->pool;
  log_entry_t *entry;
  apr_hash_index_t *hi;
  const char *common_base;
  int count;

  /* Don't care about empty revisions. Skip them. */
  if (!log_entry->changed_paths || !apr_hash_count(log_entry->changed_paths))
    return SVN_NO_ERROR;

  /* Copy changed paths list. Collect deletions and copies. */
  entry = apr_pcalloc(result_pool, sizeof(*entry));
  entry->revision = log_entry->revision;
  entry->paths = apr_array_make(result_pool,
                                apr_hash_count(log_entry->changed_paths),
                                sizeof(const char *));

  for (hi = apr_hash_first(scratch_pool, log_entry->changed_paths);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      svn_log_changed_path_t *change = apr_hash_this_val(hi);

      path = internalize(log->unique_paths, path, apr_hash_this_key_len(hi),
                         result_pool);
      APR_ARRAY_PUSH(entry->paths, const char *) = path;

      if (change->action == 'D' || change->action == 'R')
        {
          deletion_t *deletion = apr_pcalloc(result_pool, sizeof(*deletion));
          deletion->path = path;
          deletion->revision = log_entry->revision;

          APR_ARRAY_PUSH(log->deletions, deletion_t *) = deletion;
        }

      if (SVN_IS_VALID_REVNUM(change->copyfrom_rev))
        {
          svn_min__copy_t *copy = apr_pcalloc(result_pool, sizeof(*copy));
          copy->path = path;
          copy->revision = log_entry->revision;
          copy->copyfrom_path = internalize(log->unique_paths,
                                            change->copyfrom_path,
                                            strlen(change->copyfrom_path),
                                            result_pool);
          copy->copyfrom_revision = change->copyfrom_rev;

          APR_ARRAY_PUSH(log->copies, svn_min__copy_t *) = copy;
        }
    }

  /* Determine the common base of all changed paths. */
  count = entry->paths->nelts;
  if (count == 1)
    {
      entry->common_base = APR_ARRAY_IDX(entry->paths, 0, const char *);
    }
  else
    {
      svn_sort__array(entry->paths, svn_sort_compare_paths);

      common_base = svn_dirent_get_longest_ancestor(
                      APR_ARRAY_IDX(entry->paths, 0, const char *),
                      APR_ARRAY_IDX(entry->paths, count - 1, const char *),
                      scratch_pool);
      entry->common_base = internalize(log->unique_paths, common_base,
                                       strlen(common_base), result_pool);
    }

  /* Done with that reivison. */
  APR_ARRAY_PUSH(log->entries, log_entry_t *) = entry;

  /* Update log-global state. */
  log->first_rev = log_entry->revision;
  if (log->head_rev == SVN_INVALID_REVNUM)
    log->head_rev = log_entry->revision;

  /* Show progress. */
  if (log->entries->nelts % 1000 == 0 && !log->quiet)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool, "."));
      SVN_ERR(svn_cmdline_fflush(stdout));
    }

  return SVN_NO_ERROR;
}

/* Print some statistics about LOG to console. Use SCRATCH_POOL for
 * temporary allocations. */
static svn_error_t *
print_log_stats(svn_min__log_t *log,
                apr_pool_t *scratch_pool)
{
  int change_count = 0;

  int i;
  for (i = 0; i < log->entries->nelts; ++i)
    {
      const log_entry_t *entry = APR_ARRAY_IDX(log->entries, i,
                                               const log_entry_t *);
      change_count += entry->paths->nelts;
    }

  SVN_ERR(svn_cmdline_printf(scratch_pool,
                             _("    Received %d revisions from %ld to %ld.\n"),
                             log->entries->nelts, log->first_rev,
                             log->head_rev));
  SVN_ERR(svn_cmdline_printf(scratch_pool,
                             _("    Received %d path changes.\n"),
                             change_count));
  SVN_ERR(svn_cmdline_printf(scratch_pool,
                             _("    Pool has %u different paths.\n\n"),
                             apr_hash_count(log->unique_paths)));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_min__log(svn_min__log_t **log,
             const char *url,
             svn_min__cmd_baton_t *baton,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  svn_client_ctx_t *ctx = baton->ctx;
  svn_min__log_t *result;

  /* Prepare API parameters for fetching the full log for URL,
   * including changed paths, excluding revprops.
   */
  apr_array_header_t *targets;
  apr_array_header_t *revisions;
  apr_array_header_t *revprops;
  svn_opt_revision_t peg_revision = { svn_opt_revision_head };
  svn_opt_revision_range_t range = { { svn_opt_revision_unspecified },
                                     { svn_opt_revision_unspecified } };

  targets = apr_array_make(scratch_pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(targets, const char *) = url;

  revisions = apr_array_make(scratch_pool, 1, sizeof(&range));
  APR_ARRAY_PUSH(revisions, svn_opt_revision_range_t *) = &range;

  revprops = apr_array_make(scratch_pool, 0, sizeof(const char *));

  /* The log object to fill. */
  result = apr_pcalloc(result_pool, sizeof(*result));
  result->unique_paths = svn_hash__make(scratch_pool);
  result->first_rev = SVN_INVALID_REVNUM;
  result->head_rev = SVN_INVALID_REVNUM;
  result->entries = apr_array_make(result_pool, 1024, sizeof(log_entry_t *));
  result->copies = apr_array_make(result_pool, 1024,
                                  sizeof(svn_min__copy_t *));
  result->deletions = apr_array_make(result_pool, 1024, sizeof(deletion_t *));
  result->quiet = baton->opt_state->quiet;

  if (!baton->opt_state->quiet)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool,
                                 _("Fetching log for %s ..."),
                                 url));
      SVN_ERR(svn_cmdline_fflush(stdout));
    }

  SVN_ERR(svn_client_log5(targets,
                          &peg_revision,
                          revisions,
                          0, /* no limit */
                          TRUE, /* verbose */
                          TRUE, /* stop-on-copy */
                          FALSE, /* merge history */
                          revprops,
                          log_entry_receiver,
                          result,
                          ctx,
                          scratch_pool));

  /* Complete arrays in RESULT. */
  result->copies_by_source = apr_array_copy(result_pool, result->copies);

  svn_sort__array_reverse(result->entries, scratch_pool);
  svn_sort__array(result->copies, copy_order);
  svn_sort__array(result->copies_by_source, copy_by_source_order);
  svn_sort__array(result->deletions, deletion_order);

  /* Show that we are done. */
  if (!baton->opt_state->quiet)
    {
      SVN_ERR(svn_cmdline_printf(scratch_pool, "\n"));
      SVN_ERR(print_log_stats(result, scratch_pool));
    }

  result->unique_paths = NULL;
  *log = result;

  return SVN_NO_ERROR;
}

/* Append REVISION with the INHERITABLE setting to RANGES.  RANGES must be
 * sorted and REVISION must be larger than the largest revision in RANGES. */
static void
append_rev_to_ranges(svn_rangelist_t *ranges,
                     svn_revnum_t revision,
                     svn_boolean_t inheritable)
{
  /* In many cases, we can save memory by simply extending the last range. */
  svn_merge_range_t *range;
  if (ranges->nelts)
    {
      range = APR_ARRAY_IDX(ranges, ranges->nelts - 1, svn_merge_range_t *);
      if (range->end + 1 == revision && range->inheritable == inheritable)
        {
          range->end = revision;
          return;
        }
    }

  /* We need to add a new range. */
  range = apr_pcalloc(ranges->pool, sizeof(*range));
  range->start = revision - 1;
  range->end = revision;
  range->inheritable = inheritable;

  APR_ARRAY_PUSH(ranges, svn_merge_range_t *) = range;
}

/* Comparison function comparing the log_entry_t * in *LHS with the
 * svn_revnum_t in *rhs.
 */
static int
compare_rev_log_entry(const void *lhs,
                      const void *rhs)
{
  const log_entry_t *entry = *(const log_entry_t * const *)lhs;
  svn_revnum_t revision = *(const svn_revnum_t *)rhs;

  if (entry->revision < revision)
    return -1;

  return entry->revision == revision ? 0 : 1;
}

/* Restrict RANGE to the range of revisions covered by LOG. Cut-off from
 * both sides will be added to RANGES. */
static void
restrict_range(svn_min__log_t *log,
               svn_merge_range_t *range,
               svn_rangelist_t *ranges)
{
  /* Cut off at the earliest revision. */
  if (range->start + 1 < log->first_rev)
    {
      svn_merge_range_t *new_range
        = apr_pmemdup(ranges->pool, range, sizeof(*range));
      new_range->end = MIN(new_range->end, log->first_rev - 1);

      APR_ARRAY_PUSH(ranges, svn_merge_range_t *) = new_range;
      range->start = new_range->end;
    }

  /* Cut off at log HEAD. */
  if (range->end > log->head_rev)
    {
      svn_merge_range_t *new_range
        = apr_pmemdup(ranges->pool, range, sizeof(*range));
      new_range->start = log->head_rev;

      APR_ARRAY_PUSH(ranges, svn_merge_range_t *) = new_range;
      range->end = new_range->start;
    }
}

/* Return TRUE if PATH is either equal to, a parent of or sub-path of
 * CHANGED_PATH. */
static svn_boolean_t
is_relevant(const char *changed_path,
            const char *path)
{
  return  svn_dirent_is_ancestor(changed_path, path)
       || svn_dirent_is_ancestor(path, changed_path);
}

/* Return TRUE if PATH is either equal to, a parent of or sub-path of
 * SUB_TREE.  Ignore REVISION and BATON but keep it for a unified signature
 * to be used with filter_ranges. */
static svn_boolean_t
in_subtree(const char *changed_path,
           const char *sub_tree,
           svn_revnum_t revision,
           const void *baton)
{
  return svn_dirent_is_ancestor(sub_tree, changed_path);
}

/* Return TRUE if
 * - CHANGED_PATH is is either equal to or a sub-node of PATH, and
 * - CHNAGED_PATH is outside the sub-tree given as BATON.
 * Ignore REVISION. */
static svn_boolean_t
below_path_outside_subtree(const char *changed_path,
                           const char *path,
                           svn_revnum_t revision,
                           const void *baton)
{
  const char *subtree = baton;

  /* Is this a change _below_ PATH but not within SUBTREE? */
  return   !svn_dirent_is_ancestor(subtree, changed_path)
        && svn_dirent_is_ancestor(path, changed_path)
        && strcmp(path, changed_path);
}

/* Baton struct to be used with change_outside_all_subtree_ranges. */
typedef struct change_outside_baton_t
{
  /* Maps FS path to revision range lists. */
  apr_hash_t *sibling_ranges;

  /* Pool for temporary allocations.
   * Baton users may clear this at will. */
  apr_pool_t *iterpool;
} change_outside_baton_t;

/* Comparison function comparing range *LHS to revision *RHS. */
static int
compare_range_rev(const void *lhs,
                  const void *rhs)
{
  const svn_merge_range_t *range = *(const svn_merge_range_t * const *)lhs;
  svn_revnum_t revision = *(const svn_revnum_t *)rhs;

  if (range->start >= revision)
    return 1;

  return range->end < revision ? 1 : 0;
}

/* Return TRUE if CHANGED_PATH is either equal to or a sub-node of PATH,
 * CHNAGED_PATH@REVISION is not covered by BATON->SIBLING_RANGES. */
static svn_boolean_t
change_outside_all_subtree_ranges(const char *changed_path,
                                  const char *path,
                                  svn_revnum_t revision,
                                  const void *baton)
{
  const change_outside_baton_t *b = baton;
  svn_boolean_t missing = TRUE;
  apr_size_t len;
  svn_rangelist_t *ranges;

  /* Don't collect changes outside the subtree starting at PARENT_PATH. */
  if (!svn_dirent_is_ancestor(path, changed_path))
    return FALSE;

  svn_pool_clear(b->iterpool);

  /* All branches that contain CHANGED_PATH, i.e. match either it or one
   * of its parents, must mention REVISION in their mergeinfo. */
  for (len = strlen(changed_path);
       !svn_fspath__is_root(changed_path, len);
       len = strlen(changed_path))
    {
      ranges = apr_hash_get(b->sibling_ranges, changed_path, len);
      if (ranges)
        {
          /* If any of the matching branches does not list REVISION
           * as already merged, we found an "outside" change. */
          if (!svn_sort__array_lookup(ranges, &revision, NULL,
                                      compare_range_rev))
            return TRUE;

          /* Mergeinfo for this path has been found. */
          missing = FALSE;
        }

      changed_path = svn_fspath__dirname(changed_path, b->iterpool);
    }

  /* Record, if no mergeinfo has been found for this CHANGED_PATH. */
  return missing;
}

/* In LOG, scan the revisions given in RANGES and return the revision /
 * ranges that are relevant to PATH with respect to the CHANGE_RELEVANT
 * criterion using BATON.  Keep revisions that lie outside what is covered
 * by LOG. Allocate the result in RESULT_POOL. */
static svn_rangelist_t *
filter_ranges(svn_min__log_t *log,
              const char *path,
              svn_rangelist_t *ranges,
              svn_boolean_t (*change_relavent)(const char *,
                                               const char *,
                                               svn_revnum_t,
                                               const void *),
              const void *baton,
              apr_pool_t *result_pool)
{
  svn_rangelist_t *result;
  int i, k, l;

  /* Auto-complete parameters. */
  if (!SVN_IS_VALID_REVNUM(log->first_rev))
    return svn_rangelist_dup(ranges, result_pool);

  result = apr_array_make(result_pool, 0, ranges->elt_size);
  for (i = 0; i < ranges->nelts; ++i)
    {
      /* Next revision range to scan. */
      svn_merge_range_t range
        = *APR_ARRAY_IDX(ranges, i, const svn_merge_range_t *);
      restrict_range(log, &range, result);

      /* Find the range start and scan the range linearly. */
      ++range.start;
      for (k = svn_sort__bsearch_lower_bound(log->entries, &range.start,
                                             compare_rev_log_entry);
           k < log->entries->nelts;
           ++k)
        {
          const log_entry_t *entry = APR_ARRAY_IDX(log->entries, k,
                                                  const log_entry_t *);
          if (entry->revision > range.end)
            break;

          /* Skip revisions no relevant to PATH. */
          if (!is_relevant(entry->common_base, path))
            continue;

          /* Look for any changed path that meets the filter criterion. */
          for (l = 0; l < entry->paths->nelts; ++l)
            {
              const char *changed_path
                = APR_ARRAY_IDX(entry->paths, l, const char *);

              if (change_relavent(changed_path, path, entry->revision, baton))
                {
                  append_rev_to_ranges(result, entry->revision,
                                       range.inheritable);
                  break;
                }
            }
        }
    }

  return result;
}

svn_rangelist_t *
svn_min__operative(svn_min__log_t *log,
                   const char *path,
                   svn_rangelist_t *ranges,
                   apr_pool_t *result_pool)
{
  return filter_ranges(log, path, ranges, in_subtree, NULL, result_pool);
}

svn_rangelist_t *
svn_min__operative_outside_subtree(svn_min__log_t *log,
                                   const char *path,
                                   const char *subtree,
                                   svn_rangelist_t *ranges,
                                   apr_pool_t *result_pool)
{
  return filter_ranges(log, path, ranges, below_path_outside_subtree,
                       subtree, result_pool);
}

svn_rangelist_t *
svn_min__operative_outside_all_subtrees(svn_min__log_t *log,
                                        const char *path,
                                        svn_rangelist_t *ranges,
                                        apr_hash_t *sibling_ranges,
                                        apr_pool_t *result_pool,
                                        apr_pool_t *scratch_pool)
{
  svn_rangelist_t *result;
  change_outside_baton_t baton;
  baton.sibling_ranges = sibling_ranges;
  baton.iterpool = svn_pool_create(scratch_pool);

  result = filter_ranges(log, path, ranges, change_outside_all_subtree_ranges,
                         &baton, result_pool);
  svn_pool_destroy(baton.iterpool);

  return result;
}

svn_revnum_t
svn_min__find_deletion(svn_min__log_t *log,
                       const char *path,
                       svn_revnum_t start_rev,
                       svn_revnum_t end_rev,
                       apr_pool_t *scratch_pool)
{
  svn_revnum_t latest = SVN_INVALID_REVNUM;

  deletion_t *to_find = apr_pcalloc(scratch_pool, sizeof(*to_find));
  to_find->path = path;
  to_find->revision = end_rev;

  /* Auto-complete parameters. */
  if (!SVN_IS_VALID_REVNUM(start_rev))
    start_rev = log->head_rev;

  /* Walk up the tree and find the latest deletion of PATH or any of
   * its parents. */
  while (!svn_fspath__is_root(to_find->path, strlen(to_find->path)))
    {
      int i;
      for (i = svn_sort__bsearch_lower_bound(log->deletions, &to_find,
                                             deletion_order);
           i < log->deletions->nelts;
           ++i)
        {
          const deletion_t *deletion = APR_ARRAY_IDX(log->deletions, i,
                                                     const deletion_t *);
          if (strcmp(deletion->path, to_find->path))
            break;
          if (deletion->revision > start_rev)
            break;

          latest = deletion->revision;
          to_find->revision = deletion->revision;
        }

      to_find->path = svn_fspath__dirname(to_find->path, scratch_pool);
    }

  return latest;
}

apr_array_header_t *
svn_min__find_deletions(svn_min__log_t *log,
                        const char *path,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  apr_array_header_t *result = apr_array_make(result_pool, 0,
                                              sizeof(svn_revnum_t));
  int source, dest;

  deletion_t *to_find = apr_pcalloc(scratch_pool, sizeof(*to_find));
  to_find->path = path;
  to_find->revision = 0;

  /* Find deletions for PATH and its parents. */
  if (!svn_fspath__is_root(to_find->path, strlen(to_find->path)))
    {
      int i;
      for (i = svn_sort__bsearch_lower_bound(log->deletions, &to_find,
                                             deletion_order);
           i < log->deletions->nelts;
           ++i)
        {
          const deletion_t *deletion = APR_ARRAY_IDX(log->deletions, i,
                                                     const deletion_t *);
          if (strcmp(deletion->path, to_find->path))
            break;

          APR_ARRAY_PUSH(result, svn_revnum_t) = deletion->revision;
        }

      to_find->path = svn_fspath__dirname(to_find->path, scratch_pool);
    }

  /* Remove any duplicates (unlikely but possible). */
  svn_sort__array(result, svn_sort_compare_revisions);
  for (source = 1, dest = 0; source < result->nelts; ++source)
    {
      svn_revnum_t source_rev = APR_ARRAY_IDX(result, source, svn_revnum_t);
      svn_revnum_t dest_rev = APR_ARRAY_IDX(result, dest, svn_revnum_t);
      if (source_rev != dest_rev)
        {
          ++dest_rev;
          APR_ARRAY_IDX(result, dest, svn_revnum_t) = source_rev;
        }
    }

  if (result->nelts)
    result->nelts = dest + 1;

  return result;
}

/* Starting at REVISION, scan LOG for the next (in REVISION or older) copy
 * that creates PATH explicitly or implicitly by creating a parent of it.
 * Return the copy operation found or NULL if none exists.  Use SCRATCH_POOL
 * for temporary allocations. */
static const svn_min__copy_t *
next_copy(svn_min__log_t *log,
          const char *path,
          svn_revnum_t revision,
          apr_pool_t *scratch_pool)
{
  const svn_min__copy_t *copy = NULL;
  int idx;

  svn_min__copy_t *to_find = apr_pcalloc(scratch_pool, sizeof(*to_find));
  to_find->path = path;
  to_find->revision = revision;

  idx = svn_sort__bsearch_lower_bound(log->copies, &to_find, copy_order);
  if (idx < log->copies->nelts)
    {
      /* Found an exact match? */
      copy = APR_ARRAY_IDX(log->copies, idx, const svn_min__copy_t *);
      if (copy->revision != revision || strcmp(copy->path, path))
        copy = NULL;
    }

  if (!copy && idx > 0)
    {
      /* No exact match. The predecessor may be the closest copy. */
      copy = APR_ARRAY_IDX(log->copies, idx - 1, const svn_min__copy_t *);
      if (strcmp(copy->path, path))
        copy = NULL;
    }

  /* Mabye, the parent folder got copied later, i.e. is the closest copy.
     We implicitly recurse up the tree. */
  if (!svn_fspath__is_root(to_find->path, strlen(to_find->path)))
    {
      const svn_min__copy_t *parent_copy;
      to_find->path = svn_fspath__dirname(to_find->path, scratch_pool);

      parent_copy = next_copy(log, to_find->path, revision, scratch_pool);
      if (!copy)
        copy = parent_copy;
      else if (parent_copy && parent_copy->revision > copy->revision)
        copy = parent_copy;
    }

  return copy;
}

svn_revnum_t
svn_min__find_copy(svn_min__log_t *log,
                   const char *path,
                   svn_revnum_t start_rev,
                   svn_revnum_t end_rev,
                   apr_pool_t *scratch_pool)
{
  const svn_min__copy_t *copy;

  /* Auto-complete parameters. */
  if (!SVN_IS_VALID_REVNUM(start_rev))
    start_rev = log->head_rev;

  /* The actual lookup. */
  copy = next_copy(log, path, start_rev, scratch_pool);
  if (copy && copy->revision >= end_rev)
    return copy->revision;

  return SVN_NO_ERROR;
}

apr_array_header_t *
svn_min__get_copies(svn_min__log_t *log,
                    const char *path,
                    svn_revnum_t start_rev,
                    svn_revnum_t end_rev,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  apr_array_header_t *result = apr_array_make(result_pool, 0,
                                              sizeof(svn_min__copy_t *));
  const svn_min__copy_t **copies = (void *)log->copies_by_source->elts;
  int idx;

  /* Find all sub-tree copies, including PATH. */
  svn_min__copy_t *to_find = apr_pcalloc(scratch_pool, sizeof(*to_find));
  to_find->copyfrom_path = path;
  to_find->copyfrom_revision = end_rev;

  for (idx = svn_sort__bsearch_lower_bound(log->copies_by_source,
                                           &to_find,
                                           copy_by_source_order);
          (idx < log->copies->nelts)
       && svn_dirent_is_ancestor(path, copies[idx]->copyfrom_path);
       ++idx)
    {
      if (copies[idx]->copyfrom_revision <= start_rev)
        APR_ARRAY_PUSH(result, const svn_min__copy_t *) = copies[idx];
    }
 
  /* Find all parent copies. */
  while (!svn_fspath__is_root(to_find->copyfrom_path,
                              strlen(to_find->copyfrom_path)))
    {
      to_find->copyfrom_path = svn_fspath__dirname(to_find->copyfrom_path,
                                                   scratch_pool);

      for (idx = svn_sort__bsearch_lower_bound(log->copies_by_source,
                                               &to_find,
                                               copy_by_source_order);
              (idx < log->copies->nelts)
           && !strcmp(copies[idx]->copyfrom_path, to_find->copyfrom_path)
           && (copies[idx]->copyfrom_revision <= start_rev);
           ++idx)
        {
          APR_ARRAY_PUSH(result, const svn_min__copy_t *) = copies[idx];
        }
    }

  return result;
}

/* A history segment.  Simply a FS path plus the revision range that it is
 * part of the history of the node. */
typedef struct segment_t
{
  /* FS path at which the node lives in this segment */
  const char *path;

  /* Revision that it appears in or that the history was truncated to. */
  svn_revnum_t start;

  /* Revision from which the node was copied to the next segment or the
   * revision that the history was truncated to. */
  svn_revnum_t end;
} segment_t;

apr_array_header_t *
svn_min__get_history(svn_min__log_t *log,
                     const char *path,
                     svn_revnum_t start_rev,
                     svn_revnum_t end_rev,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  segment_t *segment;
  const svn_min__copy_t *copy;
  apr_array_header_t *result = apr_array_make(result_pool, 16,
                                              sizeof(segment_t *));

  /* Auto-complete parameters. */
  if (!SVN_IS_VALID_REVNUM(start_rev))
    start_rev = log->head_rev;

  /* Simply follow all copies, each time adding a segment from "here" to
   * the next copy. */
  for (copy = next_copy(log, path, start_rev, scratch_pool);
       copy && start_rev >= end_rev;
       copy = next_copy(log, path, start_rev, scratch_pool))
    {
      segment = apr_pcalloc(result_pool, sizeof(*segment));
      segment->start = MAX(end_rev, copy->revision);
      segment->end = start_rev;
      segment->path = apr_pstrdup(result_pool, path);

      APR_ARRAY_PUSH(result, segment_t *) = segment;

      start_rev = copy->copyfrom_revision;
      path = svn_fspath__join(copy->copyfrom_path,
                              svn_fspath__skip_ancestor(copy->path, path),
                              scratch_pool);
    }

  /* The final segment has no copy-from. */
  if (start_rev >= end_rev)
    {
      segment = apr_pcalloc(result_pool, sizeof(*segment));
      segment->start = end_rev;
      segment->end = start_rev;
      segment->path = apr_pstrdup(result_pool, path);

      APR_ARRAY_PUSH(result, segment_t *) = segment;
    }

  return result;
}

apr_array_header_t *
svn_min__intersect_history(apr_array_header_t *lhs,
                           apr_array_header_t *rhs,
                           apr_pool_t *result_pool)
{
  apr_array_header_t *result = apr_array_make(result_pool, 16,
                                              sizeof(segment_t *));

  int lhs_idx = 0;
  int rhs_idx = 0;

  /* Careful: the segments are ordered latest to oldest. */
  while (lhs_idx < lhs->nelts && rhs_idx < rhs->nelts)
    {
      segment_t *lhs_segment = APR_ARRAY_IDX(lhs, lhs_idx, segment_t *);
      segment_t *rhs_segment = APR_ARRAY_IDX(rhs, rhs_idx, segment_t *);

      /* Skip non-overlapping revision segments */
      if (lhs_segment->start > rhs_segment->end)
        {
          ++lhs_idx;
          continue;
        }
      else if (lhs_segment->end < rhs_segment->start)
        {
          ++rhs_idx;
          continue;
        }

      /* Revision ranges overlap. Also the same path? */
      if (!strcmp(lhs_segment->path, rhs_segment->path))
        {
          segment_t *segment = apr_pcalloc(result_pool, sizeof(*segment));
          segment->start = MAX(lhs_segment->start, rhs_segment->start);
          segment->end = MIN(lhs_segment->end, rhs_segment->end);
          segment->path = apr_pstrdup(result_pool, lhs_segment->path);

          APR_ARRAY_PUSH(result, segment_t *) = segment;
        }

      /* The segment that starts earlier may overlap with another one.
         If they should start at the same rev, the next iteration will
         skip the respective other segment. */
      if (lhs_segment->start > rhs_segment->start)
        ++lhs_idx;
      else
        ++rhs_idx;
    }

   return result;
}

svn_rangelist_t *
svn_min__history_ranges(apr_array_header_t *history,
                        apr_pool_t *result_pool)
{
  svn_rangelist_t *result = apr_array_make(result_pool, history->nelts,
                                           sizeof(svn_merge_range_t *));

  int i;
  for (i = 0; i < history->nelts; ++i)
    {
      const segment_t *segment = APR_ARRAY_IDX(history, i, segment_t *);

      /* Convert to merge ranges.  Note that start+1 is the first rev
         actually in that range. */
      svn_merge_range_t *range = apr_pcalloc(result_pool, sizeof(*range));
      range->start = MAX(0, segment->start - 1);
      range->end = segment->end;
      range->inheritable = TRUE;

      APR_ARRAY_PUSH(result, svn_merge_range_t *) = range;
    }

  return result;
}
