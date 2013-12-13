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

static svn_error_t *
verify_mtcc_commit(svn_client_mtcc_t *mtcc,
                   apr_pool_t *pool)
{
  /* TODO: Verify actual commit, etc. */
  SVN_ERR(svn_client_mtcc_commit(NULL, NULL, NULL, mtcc, pool));

  return SVN_NO_ERROR;
};


/* Constructs a greek tree as revision 1 in the repository at repos_url */
static svn_error_t *
make_greek_tree(const char *repos_url,
                apr_pool_t *scratch_pool)
{
  svn_client_mtcc_t *mtcc;
  svn_client_ctx_t *ctx;
  apr_pool_t *subpool;
  int i;

  subpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_client_create_context2(&ctx, NULL, subpool));
  SVN_ERR(svn_client_mtcc_create(&mtcc, repos_url, 1, ctx, subpool, subpool));

  for (i = 0; svn_test__greek_tree_nodes[i].path; i++)
    {
      if (svn_test__greek_tree_nodes[i].contents)
        {
          SVN_ERR(svn_client_mtcc_add_add_file(
                                svn_test__greek_tree_nodes[i].path,
                                svn_stream_from_string(
                                    svn_string_create(
                                        svn_test__greek_tree_nodes[i].contents,
                                        subpool),
                                    subpool),
                                NULL /* src_checksum */,
                                mtcc, subpool));
        }
      else
        {
          SVN_ERR(svn_client_mtcc_add_mkdir(
                                svn_test__greek_tree_nodes[i].path,
                                mtcc, subpool));
        }
    }

  SVN_ERR(verify_mtcc_commit(mtcc, subpool));

  svn_pool_clear(subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
test_mkdir(const svn_test_opts_t *opts,
           apr_pool_t *pool)
{
  svn_client_mtcc_t *mtcc;
  svn_client_ctx_t *ctx;
  const char *repos_abspath;
  const char *repos_url;
  svn_repos_t* repos;

  repos_abspath = svn_test_data_path("mtcc-mkdir", pool);
  SVN_ERR(svn_dirent_get_absolute(&repos_abspath, repos_abspath, pool));
  SVN_ERR(svn_uri_get_file_url_from_dirent(&repos_url, repos_abspath, pool));
  SVN_ERR(svn_test__create_repos(&repos, repos_abspath, opts, pool));

  SVN_ERR(svn_client_create_context2(&ctx, NULL, pool));
  SVN_ERR(svn_client_mtcc_create(&mtcc, repos_url, 0, ctx, pool, pool));

  SVN_ERR(svn_client_mtcc_add_mkdir("branches", mtcc, pool));
  SVN_ERR(svn_client_mtcc_add_mkdir("trunk", mtcc, pool));
  SVN_ERR(svn_client_mtcc_add_mkdir("branches/1.x", mtcc, pool));
  SVN_ERR(svn_client_mtcc_add_mkdir("tags", mtcc, pool));
  SVN_ERR(svn_client_mtcc_add_mkdir("tags/1.0", mtcc, pool));
  SVN_ERR(svn_client_mtcc_add_mkdir("tags/1.1", mtcc, pool));

  SVN_ERR(verify_mtcc_commit(mtcc, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_mkgreek(const svn_test_opts_t *opts,
             apr_pool_t *pool)
{
  svn_client_mtcc_t *mtcc;
  svn_client_ctx_t *ctx;
  const char *repos_abspath;
  const char *repos_url;
  svn_repos_t* repos;

  repos_abspath = svn_test_data_path("mtcc-mkgreek", pool);
  SVN_ERR(svn_dirent_get_absolute(&repos_abspath, repos_abspath, pool));
  SVN_ERR(svn_uri_get_file_url_from_dirent(&repos_url, repos_abspath, pool));
  SVN_ERR(svn_test__create_repos(&repos, repos_abspath, opts, pool));

  SVN_ERR(make_greek_tree(repos_url, pool));

  SVN_ERR(svn_client_create_context2(&ctx, NULL, pool));
  SVN_ERR(svn_client_mtcc_create(&mtcc, repos_url, 1, ctx, pool, pool));

  SVN_ERR(svn_client_mtcc_add_copy("A", 1, "greek_A", mtcc, pool));

  SVN_ERR(verify_mtcc_commit(mtcc, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_swap(const svn_test_opts_t *opts,
          apr_pool_t *pool)
{
 svn_client_mtcc_t *mtcc;
  svn_client_ctx_t *ctx;
  const char *repos_abspath;
  const char *repos_url;
  svn_repos_t* repos;

  repos_abspath = svn_test_data_path("mtcc-swap", pool);
  SVN_ERR(svn_dirent_get_absolute(&repos_abspath, repos_abspath, pool));
  SVN_ERR(svn_uri_get_file_url_from_dirent(&repos_url, repos_abspath, pool));
  SVN_ERR(svn_test__create_repos(&repos, repos_abspath, opts, pool));

  SVN_ERR(make_greek_tree(repos_url, pool));

  SVN_ERR(svn_client_create_context2(&ctx, NULL, pool));
  SVN_ERR(svn_client_mtcc_create(&mtcc, repos_url, 1, ctx, pool, pool));

  SVN_ERR(svn_client_mtcc_add_move("A/B", "B", mtcc, pool));
  SVN_ERR(svn_client_mtcc_add_move("A/D", "A/B", mtcc, pool));
  SVN_ERR(svn_client_mtcc_add_copy("A/B", 1, "A/D", mtcc, pool));

  SVN_ERR(verify_mtcc_commit(mtcc, pool));

  return SVN_NO_ERROR;
}


/* ========================================================================== */


int svn_test_max_threads = 3;

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(test_mkdir,
                       "test mtcc mkdir"),
    SVN_TEST_OPTS_PASS(test_mkgreek,
                       "test making greek tree"),
    SVN_TEST_OPTS_PASS(test_swap,
                       "swapping some trees"),
    SVN_TEST_NULL
  };
 
