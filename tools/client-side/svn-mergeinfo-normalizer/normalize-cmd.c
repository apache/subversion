/*
 * normalize-cmd.c -- Elide mergeinfo from sub-nodes
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

static svn_error_t *
normalize(apr_array_header_t *wc_mergeinfo,
          svn_min__log_t *log,
          apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  int i;
  for (i = wc_mergeinfo->nelts - 1; i >= 0; --i)
    {
      const char *parent_path;
      const char *relpath;
      svn_mergeinfo_t parent_mergeinfo;
      svn_mergeinfo_t subtree_mergeinfo;

      if (svn_min__get_mergeinfo_pair(&parent_path, &relpath,
                                      &parent_mergeinfo, &subtree_mergeinfo,
                                      wc_mergeinfo, i))
        {
          svn_mergeinfo_t parent_mergeinfo_copy;
          svn_mergeinfo_t subtree_mergeinfo_copy;

          svn_pool_clear(iterpool);
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
            }
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_min__normalize(apr_getopt_t *os,
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
      svn_min__log_t *log;
      const char *url;
      const char *common_path;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_min__add_wc_info(baton, i, iterpool, subpool));

      /* scan working copy */
      svn_pool_clear(subpool);
      SVN_ERR(svn_min__read_mergeinfo(&wc_mergeinfo, cmd_baton, iterpool,
                                      subpool));

      /* fetch log */
      svn_pool_clear(subpool);
      common_path = svn_min__common_parent(wc_mergeinfo, subpool, subpool);
      SVN_ERR_ASSERT(*common_path == '/');
      url = svn_path_url_add_component2(cmd_baton->repo_root,
                                        common_path + 1,
                                        subpool);
      SVN_ERR(svn_min__log(&log, url, cmd_baton, iterpool, subpool));

      /* actual normalization */
      svn_pool_clear(subpool);
      SVN_ERR(normalize(wc_mergeinfo, log, subpool));

      /* write results to disk */
      svn_pool_clear(subpool);
      if (!cmd_baton->opt_state->dry_run)
        SVN_ERR(svn_min__write_mergeinfo(cmd_baton, wc_mergeinfo, subpool));
    }

  svn_pool_destroy(subpool);
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
