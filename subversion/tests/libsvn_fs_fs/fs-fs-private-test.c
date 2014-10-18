/* fs-fs-private-test.c --- tests FSFS's private API
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

#include <stdlib.h>
#include <string.h>

#include "../svn_test.h"

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_fs.h"

#include "private/svn_string_private.h"
#include "private/svn_fs_fs_private.h"

#include "../svn_test_fs.h"



/* Utility functions */

/* Create a repo under REPO_NAME using OPTS.  Allocate the repository in
 * RESULT_POOL and return it in *REPOS.  Set *REV to the revision containing
 * the Greek tree addition.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
create_greek_repo(svn_repos_t **repos,
                  svn_revnum_t *rev,
                  const svn_test_opts_t *opts,
                  const char *repo_name,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  /* Create a filesystem */
  SVN_ERR(svn_test__create_repos(repos, repo_name, opts, result_pool));
  fs = svn_repos_fs(*repos);

  /* Add the Greek tree */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, scratch_pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, scratch_pool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, scratch_pool));
  SVN_ERR(svn_fs_commit_txn(NULL, rev, txn, scratch_pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(*rev));

  return SVN_NO_ERROR;
}


/* ------------------------------------------------------------------------ */

#define REPO_NAME "get-repo-stats-test"

static svn_error_t *
verify_representation_stats(const svn_fs_fs__representation_stats_t *stats,
                            apr_int64_t expected_count)
{
  /* Small items, no packing (but inefficiency due to packing attempt). */
  SVN_TEST_ASSERT(stats->total.count == expected_count);
  SVN_TEST_ASSERT(   stats->total.packed_size >= 10 * expected_count
                  && stats->total.packed_size <= 1000 * expected_count);
  SVN_TEST_ASSERT(   stats->total.packed_size >= stats->total.expanded_size
                  && stats->total.packed_size <= 2 * stats->total.expanded_size);
  SVN_TEST_ASSERT(   stats->total.overhead_size >= 5 * expected_count
                  && stats->total.overhead_size <= 100 * expected_count);

  /* Rep sharing has no effect on the Greek tree. */
  SVN_TEST_ASSERT(stats->total.count == stats->uniques.count);
  SVN_TEST_ASSERT(stats->total.packed_size == stats->uniques.packed_size);
  SVN_TEST_ASSERT(stats->total.expanded_size == stats->uniques.expanded_size);
  SVN_TEST_ASSERT(stats->total.overhead_size == stats->uniques.overhead_size);

  SVN_TEST_ASSERT(stats->shared.count == 0);
  SVN_TEST_ASSERT(stats->shared.packed_size == 0);
  SVN_TEST_ASSERT(stats->shared.expanded_size == 0);
  SVN_TEST_ASSERT(stats->shared.overhead_size == 0);

  /* No rep sharing. */
  SVN_TEST_ASSERT(stats->references == stats->total.count);
  SVN_TEST_ASSERT(stats->expanded_size == stats->total.expanded_size);

  return SVN_NO_ERROR;
}

static svn_error_t *
verify_node_stats(const svn_fs_fs__node_stats_t *node_stats,
                  apr_int64_t expected_count)
{
  SVN_TEST_ASSERT(node_stats->count == expected_count);
  SVN_TEST_ASSERT(   node_stats->size > 100 * node_stats->count
                  && node_stats->size < 1000 * node_stats->count);

  return SVN_NO_ERROR;
}

static svn_error_t *
verify_large_change(const svn_fs_fs__large_change_info_t *change,
                    svn_revnum_t revision)
{
  if (change->revision == SVN_INVALID_REVNUM)
    {
      /* Unused entry due to the Greek tree being small. */
      SVN_TEST_ASSERT(change->path->len == 0);
      SVN_TEST_ASSERT(change->size == 0);
    }
  else if (strcmp(change->path->data, "/") == 0)
    {
      /* The root folder nodes are always there, i.e. aren't in the
       * Greek tree "do add" list. */
      SVN_TEST_ASSERT(   SVN_IS_VALID_REVNUM(change->revision)
                      && change->revision <= revision);
    }
  else
    {
      const struct svn_test__tree_entry_t *node;
      for (node = svn_test__greek_tree_nodes; node->path; node++)
        if (strcmp(node->path, change->path->data + 1) == 0)
          {
            SVN_TEST_ASSERT(change->revision == revision);

            /* When checking content sizes, keep in mind the optional
             * SVNDIFF overhead.*/
            if (node->contents)
              SVN_TEST_ASSERT(   change->size >= strlen(node->contents)
                              && change->size <= 12 + strlen(node->contents));

            return SVN_NO_ERROR;
          }

      SVN_TEST_ASSERT(!"Change is part of Greek tree");
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
verify_histogram(const svn_fs_fs__histogram_t *histogram)
{
  apr_int64_t sum_count = 0;
  apr_int64_t sum_size = 0;

  int i;
  for (i = 0; i < 64; ++i)
    {
      svn_fs_fs__histogram_line_t line = histogram->lines[i];

      if (i > 10 || i < 1)
        SVN_TEST_ASSERT(line.sum == 0 && line.count == 0);
      else
        SVN_TEST_ASSERT(   line.sum >= (line.count << (i-1))
                        && line.sum <= (line.count << i));

      sum_count += line.count;
      sum_size += line.sum;
    }

  SVN_TEST_ASSERT(histogram->total.count == sum_count);
  SVN_TEST_ASSERT(histogram->total.sum == sum_size);

  return SVN_NO_ERROR;
}

static svn_error_t *
get_repo_stats(const svn_test_opts_t *opts,
               apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_revnum_t rev;
  apr_size_t i;
  svn_fs_fs__stats_t *stats;
  svn_fs_fs__extension_info_t *extension_info;

  /* Bail (with success) on known-untestable scenarios */
  if (strcmp(opts->fs_type, "fsfs") != 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "this will test FSFS repositories only");

  /* Create a filesystem */
  SVN_ERR(create_greek_repo(&repos, &rev, opts, REPO_NAME, pool, pool));

  /* Gather statistics info on that repo. */
  SVN_ERR(svn_fs_fs__get_stats(&stats, svn_repos_fs(repos), NULL, NULL,
                               NULL, NULL, pool, pool));

  /* Check that the stats make sense. */
  SVN_TEST_ASSERT(stats->total_size > 1000 && stats->total_size < 10000);
  SVN_TEST_ASSERT(stats->revision_count == 2);
  SVN_TEST_ASSERT(stats->change_count == 20);
  SVN_TEST_ASSERT(stats->change_len > 500 && stats->change_len < 2000);

  /* Check representation stats. */
  SVN_ERR(verify_representation_stats(&stats->total_rep_stats, 20));
  SVN_ERR(verify_representation_stats(&stats->file_rep_stats, 12));
  SVN_ERR(verify_representation_stats(&stats->dir_rep_stats, 8));
  SVN_ERR(verify_representation_stats(&stats->file_prop_rep_stats, 0));
  SVN_ERR(verify_representation_stats(&stats->dir_prop_rep_stats, 0));

  /* Check node stats against rep stats. */
  SVN_ERR(verify_node_stats(&stats->total_node_stats, 22));
  SVN_ERR(verify_node_stats(&stats->file_node_stats, 12));
  SVN_ERR(verify_node_stats(&stats->dir_node_stats, 10));

  /* Check largest changes. */
  SVN_TEST_ASSERT(stats->largest_changes->count == 64);
  SVN_TEST_ASSERT(stats->largest_changes->min_size == 0);

  for (i = 0; i < stats->largest_changes->count; ++i)
    SVN_ERR(verify_large_change(stats->largest_changes->changes[i], rev));

  /* Check histograms. */
  SVN_ERR(verify_histogram(&stats->rep_size_histogram));
  SVN_ERR(verify_histogram(&stats->node_size_histogram));
  SVN_ERR(verify_histogram(&stats->added_rep_size_histogram));
  SVN_ERR(verify_histogram(&stats->added_node_size_histogram));
  SVN_ERR(verify_histogram(&stats->unused_rep_histogram));
  SVN_ERR(verify_histogram(&stats->file_histogram));
  SVN_ERR(verify_histogram(&stats->file_rep_histogram));
  SVN_ERR(verify_histogram(&stats->file_prop_histogram));
  SVN_ERR(verify_histogram(&stats->file_prop_rep_histogram));
  SVN_ERR(verify_histogram(&stats->dir_histogram));
  SVN_ERR(verify_histogram(&stats->dir_rep_histogram));
  SVN_ERR(verify_histogram(&stats->dir_prop_histogram));
  SVN_ERR(verify_histogram(&stats->dir_prop_rep_histogram));

  /* No file in the Greek tree has an externsion */
  SVN_TEST_ASSERT(apr_hash_count(stats->by_extension) == 1);
  extension_info = svn_hash_gets(stats->by_extension, "(none)");
  SVN_TEST_ASSERT(extension_info);

  SVN_ERR(verify_histogram(&extension_info->rep_histogram));
  SVN_ERR(verify_histogram(&extension_info->node_histogram));

  return SVN_NO_ERROR;
}

#undef REPO_NAME


/* The test table.  */

static int max_threads = 0;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(get_repo_stats,
                       "get statistics on a FSFS filesystem"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
