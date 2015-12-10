/*
* Regression tests for logic in the libsvn_client library.
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

#include "svn_pools.h"
#include "svn_client.h"
#include "private/svn_client_mtcc.h"

#include "svn_repos.h"
#include "svn_subst.h"
#include "private/svn_sorts_private.h"
#include "private/svn_wc_private.h"
#include "svn_props.h"
#include "svn_hash.h"

#include "../svn_test.h"
#include "../svn_test_fs.h"


/* Create a GIT repository */
static svn_error_t *
create_git_repos(const char **repos_url,
                 const char *name,
                 apr_pool_t *pool)
{
  const char *fs_dir;
  svn_fs_t *fs;

  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(svn_dirent_get_absolute(&fs_dir, name, subpool));
  SVN_ERR(svn_io_remove_dir2(fs_dir, TRUE, NULL, NULL, subpool));
  svn_test_add_dir_cleanup(fs_dir);

  {
    apr_hash_t *fs_config = apr_hash_make(subpool);
    svn_hash_sets(fs_config, SVN_FS_CONFIG_FS_TYPE, SVN_FS_TYPE_GIT);

    SVN_ERR(svn_fs_create2(&fs, fs_dir, fs_config, subpool, subpool));
  }

  fs_dir = svn_dirent_join(fs_dir, "git", subpool);

  SVN_ERR(svn_uri_get_file_url_from_dirent(repos_url, fs_dir, subpool));

  *repos_url = apr_pstrcat(pool, "git+", *repos_url, SVN_VA_NULL);

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
test_git_mkdir(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  const char *repos_url;
  const char *wc_dir;
  svn_client_ctx_t *ctx;
  svn_client__mtcc_t *mtcc;
  const char *trunk_url;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(create_git_repos(&repos_url, "git-mkdir", subpool));

  SVN_ERR(svn_dirent_get_absolute(&wc_dir, "git-mkdir-wc", subpool));
  SVN_ERR(svn_io_remove_dir2(wc_dir, TRUE, NULL, NULL, subpool));
  svn_test_add_dir_cleanup(wc_dir);

  SVN_ERR(svn_client_create_context2(&ctx, NULL, pool));

  trunk_url = svn_path_url_add_component2(repos_url, "trunk", pool);
  //head_rev.kind = svn_opt_revision_head;
  //SVN_ERR(svn_client_checkout3(&rev, trunk_url,
  //                             wc_dir, &head_rev, &head_rev, svn_depth_infinity,
  //                             FALSE, FALSE, ctx, pool));
  //
  //
  //{
  //  apr_array_header_t *revs;
  //  apr_array_header_t *paths = apr_array_make(pool, 1, sizeof(const char *));
  //  APR_ARRAY_PUSH(paths, const char *) = wc_dir;
  //
  //  SVN_ERR(svn_client_update4(&revs, paths, &head_rev, svn_depth_infinity, FALSE,
  //                             FALSE, FALSE, FALSE, FALSE, ctx, pool));
  //}

  SVN_ERR(svn_client__mtcc_create(&mtcc, repos_url, 0, ctx, subpool, subpool));

  SVN_ERR(svn_client__mtcc_add_mkdir("trunk", mtcc, subpool));
  SVN_ERR(svn_client__mtcc_add_mkdir("trunk/A", mtcc, subpool));
  SVN_ERR(svn_client__mtcc_add_mkdir("trunk/A/E", mtcc, subpool));

  //SVN_ERR(svn_client__mtcc_add_add_file(
  //  "trunk/iota",
  //  svn_stream_from_string(svn_string_create("This is the file 'iota'\n",
  //                                           subpool),
  //                         subpool),
  //  NULL, mtcc, subpool));

  SVN_ERR(svn_client__mtcc_commit(apr_hash_make(subpool),
                                  NULL, NULL, mtcc, subpool));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_git_checkout(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  const char *repos_url;
  const char *wc_dir;
  svn_client_ctx_t *ctx;
  svn_client__mtcc_t *mtcc;
  const char *trunk_url;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(create_git_repos(&repos_url, "git-checkout-repos", subpool));

  SVN_ERR(svn_dirent_get_absolute(&wc_dir, "git-checkout-wc", subpool));
  SVN_ERR(svn_io_remove_dir2(wc_dir, TRUE, NULL, NULL, subpool));
  svn_test_add_dir_cleanup(wc_dir);

  SVN_ERR(svn_client_create_context2(&ctx, NULL, pool));

  trunk_url = svn_path_url_add_component2(repos_url, "trunk", pool);
  //head_rev.kind = svn_opt_revision_head;
  //SVN_ERR(svn_client_checkout3(&rev, trunk_url,
  //                             wc_dir, &head_rev, &head_rev, svn_depth_infinity,
  //                             FALSE, FALSE, ctx, pool));
  //
  //
  //{
  //  apr_array_header_t *revs;
  //  apr_array_header_t *paths = apr_array_make(pool, 1, sizeof(const char *));
  //  APR_ARRAY_PUSH(paths, const char *) = wc_dir;
  //
  //  SVN_ERR(svn_client_update4(&revs, paths, &head_rev, svn_depth_infinity, FALSE,
  //                             FALSE, FALSE, FALSE, FALSE, ctx, pool));
  //}

  SVN_ERR(svn_client__mtcc_create(&mtcc, repos_url, 0, ctx, subpool, subpool));

  SVN_ERR(svn_client__mtcc_add_mkdir("trunk", mtcc, subpool));

  SVN_ERR(svn_client__mtcc_add_add_file(
    "trunk/iota",
    svn_stream_from_string(svn_string_create("This is the file 'iota'\n",
                                             subpool),
                           subpool),
    NULL, mtcc, subpool));

  SVN_ERR(svn_client__mtcc_commit(apr_hash_make(subpool),
                                  NULL, NULL, mtcc, subpool));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* ========================================================================== */


static int max_threads = 3;

static struct svn_test_descriptor_t test_funcs[] =
{
  SVN_TEST_NULL,
  SVN_TEST_OPTS_PASS(test_git_mkdir,
                     "test git_mkdir"),
  SVN_TEST_OPTS_XFAIL(test_git_checkout,
                     "test git_checkout"),

  SVN_TEST_NULL
};

SVN_TEST_MAIN

