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

#include "private/svn_subr_private.h"
#include "private/svn_sorts_private.h"

#include "mergeinfo-normalizer.h"

#include "svn_private_config.h"


/*** Code. ***/

typedef struct log_entry_t
{
  svn_revnum_t revision;
  const char *common_base;
  apr_array_header_t *paths;
} log_entry_t;

struct svn_min__log_t
{
  apr_hash_t *unique_paths;

  svn_revnum_t first_rev;
  svn_revnum_t head_rev;
  apr_array_header_t *entries;
};

static const char *
internalize(apr_hash_t *unique_paths,
            const char *path,
            apr_ssize_t path_len)
{
  const char *result = apr_hash_get(unique_paths, path, path_len);
  if (result == NULL)
    {
      result = apr_pstrmemdup(apr_hash_pool_get(unique_paths), path, path_len);
      apr_hash_set(unique_paths, result, path_len, result);
    }

  return result;
}

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

  if (!log_entry->changed_paths || !apr_hash_count(log_entry->changed_paths))
    return SVN_NO_ERROR;

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
      path = internalize(log->unique_paths, path, apr_hash_this_key_len(hi));
      APR_ARRAY_PUSH(entry->paths, const char *) = path;
    }

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
                                       strlen(common_base));
    }

  APR_ARRAY_PUSH(log->entries, log_entry_t *) = entry;

  log->first_rev = log_entry->revision;
  if (log->head_rev == SVN_INVALID_REVNUM)
    log->head_rev = log_entry->revision;

  return SVN_NO_ERROR;
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_min__log(svn_min__log_t **log,
             const char *url,
             svn_min__cmd_baton_t *baton,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  svn_client_ctx_t *ctx = baton->ctx;
  svn_min__log_t *result;

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

  result = apr_pcalloc(result_pool, sizeof(*result));
  result->unique_paths = svn_hash__make(result_pool);
  result->first_rev = SVN_INVALID_REVNUM;
  result->head_rev = SVN_INVALID_REVNUM;
  result->entries = apr_array_make(result_pool, 1024, sizeof(log_entry_t *));

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

  svn_sort__array_reverse(result->entries, scratch_pool);
  *log = result;

  return SVN_NO_ERROR;
}

static void
append_rev_to_ranges(svn_rangelist_t *ranges,
                     svn_revnum_t revision,
                     svn_boolean_t inheritable,
                     apr_pool_t *result_pool)
{
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

  range = apr_pcalloc(result_pool, sizeof(*range));
  range->start = revision - 1;
  range->end = revision;
  range->inheritable = inheritable;

  APR_ARRAY_PUSH(ranges, svn_merge_range_t *) = range;
}

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

static void
restrict_range(svn_min__log_t *log,
               svn_merge_range_t *range,
               svn_rangelist_t *ranges,
               apr_pool_t *result_pool)
{
  if (range->start + 1 < log->first_rev)
    {
      svn_merge_range_t *new_range
        = apr_pmemdup(result_pool, range, sizeof(*range));
      new_range->end = MIN(new_range->end, log->first_rev - 1);

      APR_ARRAY_PUSH(ranges, svn_merge_range_t *) = new_range;
      range->start = new_range->end;
    }

  if (range->end > log->head_rev)
    {
      svn_merge_range_t *new_range
        = apr_pmemdup(result_pool, range, sizeof(*range));
      new_range->start = log->head_rev;

      APR_ARRAY_PUSH(ranges, svn_merge_range_t *) = new_range;
      range->end = new_range->start;
    }
}

static svn_boolean_t
is_relevant(const char *changed_path,
            const char *path,
            const void *baton)
{
  return  svn_dirent_is_ancestor(changed_path, path)
       || svn_dirent_is_ancestor(path, changed_path);
}

static svn_boolean_t
below_path_outside_subtree(const char *changed_path,
                           const char *path,
                           const void *baton)
{
  const char *subtree = baton;

  /* Is this a change _below_ PATH but not within SUBTREE? */
  return   !svn_dirent_is_ancestor(subtree, changed_path)
        && svn_dirent_is_ancestor(path, changed_path)
        && strcmp(path, changed_path);
}

static svn_rangelist_t *
filter_ranges(svn_min__log_t *log,
              const char *path,
              svn_rangelist_t *ranges,
              svn_boolean_t (*path_relavent)(const char*, const char *,
                                             const void *),
              const void *baton,
              apr_pool_t *result_pool)
{
  svn_rangelist_t *result;
  int i, k, l;

  if (!SVN_IS_VALID_REVNUM(log->first_rev))
    return svn_rangelist_dup(ranges, result_pool);

  result = apr_array_make(result_pool, 0, ranges->elt_size);
  for (i = 0; i < ranges->nelts; ++i)
    {
      svn_merge_range_t range
        = *APR_ARRAY_IDX(ranges, i, const svn_merge_range_t *);
      restrict_range(log, &range, result, result_pool);

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

          if (!is_relevant(entry->common_base, path, NULL))
            continue;

          for (l = 0; l < entry->paths->nelts; ++l)
            {
              const char *changed_path
                = APR_ARRAY_IDX(entry->paths, l, const char *);

              /* Is this a change _below_ PATH but not within SUBTREE? */
              if (path_relavent(changed_path, path, baton))
                {
                  append_rev_to_ranges(result, entry->revision,
                                       range.inheritable, result_pool);
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
  return filter_ranges(log, path, ranges, is_relevant, NULL, result_pool);
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

svn_error_t *
svn_min__print_log_stats(svn_min__log_t *log,
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
