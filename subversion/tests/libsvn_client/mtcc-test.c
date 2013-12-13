/*
 * Regression tests for mtcc code in the libsvn_client library.
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

#define SVN_DEPRECATED

#include "svn_mergeinfo.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_repos.h"
#include "svn_subst.h"
#include "private/svn_wc_private.h"

#include "../svn_test.h"
#include "../svn_test_fs.h"
#include "../libsvn_wc/utils.h"


/* Create a repository with a filesystem based on OPTS in a subdir NAME,
 * commit the standard Greek tree as revision 1, and set *REPOS_URL to
 * the URL we will use to access it.
 *
 * ### This always returns a file: URL. We should upgrade this to use the
 *     test suite's specified URL scheme instead. */
static svn_error_t *
create_greek_repos(const char **repos_url,
                   const char *name,
                   const svn_test_opts_t *opts,
                   apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_revnum_t committed_rev;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  /* Create a filesytem and repository. */
  SVN_ERR(svn_test__create_repos(
              &repos, svn_test_data_path(name, pool), opts, pool));

  /* Prepare and commit a txn containing the Greek tree. */
  SVN_ERR(svn_fs_begin_txn2(&txn, svn_repos_fs(repos), 0 /* rev */,
                            0 /* flags */, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &committed_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(committed_rev));

  SVN_ERR(svn_uri_get_file_url_from_dirent(
              repos_url, svn_test_data_path(name, pool), pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
test_mkdir(const svn_test_opts_t *opts,
           apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  svn_client_mtcc_t *mtcc;
  svn_client_ctx_t *ctx;

  SVN_ERR(svn_test__sandbox_create(&b, "mtcc-mkdir", opts, pool));

  SVN_ERR(svn_client_create_context2(&ctx, NULL, pool));

  SVN_ERR(svn_client_mtcc_create(&mtcc, b.repos_url, 1, ctx, pool, pool));

  SVN_ERR(svn_client_mtcc_add_mkdir("A", mtcc, pool));
  SVN_ERR(svn_client_mtcc_add_mkdir("B", mtcc, pool));
  SVN_ERR(svn_client_mtcc_add_mkdir("A/C", mtcc, pool));
  SVN_ERR(svn_client_mtcc_add_mkdir("A/D", mtcc, pool));

  SVN_ERR(svn_client_mtcc_commit(apr_hash_make(pool), NULL, NULL, mtcc, pool));

  return SVN_NO_ERROR;
}

/* ========================================================================== */


int svn_test_max_threads = 3;

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(test_mkdir,
                       "test mtcc mkdir"),
    SVN_TEST_NULL
  };
 
