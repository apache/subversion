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

static svn_error_t *
verify_commit(const svn_commit_info_t *commit_info,
              void *baton,
              apr_pool_t *pool)
{
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(commit_info->revision));
  SVN_TEST_ASSERT(commit_info->repos_root != NULL);

  return SVN_NO_ERROR;
}

/* Create a GIT repository */
static svn_error_t *
create_git_repos(const char **repos_url,
                 const char *name,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  const char *fs_dir;
  svn_fs_t *fs;

  SVN_ERR(svn_dirent_get_absolute(&fs_dir, name, scratch_pool));
  SVN_ERR(svn_io_remove_dir2(fs_dir, TRUE, NULL, NULL, scratch_pool));
  svn_test_add_dir_cleanup(fs_dir);

  {
    apr_hash_t *fs_config = apr_hash_make(scratch_pool);
    svn_hash_sets(fs_config, SVN_FS_CONFIG_FS_TYPE, SVN_FS_TYPE_GIT);

    SVN_ERR(svn_fs_create2(&fs, fs_dir, fs_config,
                           scratch_pool, scratch_pool));
  }

  fs_dir = svn_dirent_join(fs_dir, "git", scratch_pool);

  SVN_ERR(svn_uri_get_file_url_from_dirent(repos_url, fs_dir, scratch_pool));

  *repos_url = apr_pstrcat(result_pool, "git+", *repos_url, SVN_VA_NULL);

  return SVN_NO_ERROR;
}

static svn_error_t *
create_git_repos_greek(const char **repos_url,
                       const char *name,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_client_ctx_t *ctx;
  svn_client__mtcc_t *mtcc;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  const svn_test__tree_entry_t *ge;

  SVN_ERR(create_git_repos(repos_url, name, result_pool, iterpool));

  SVN_ERR(svn_client_create_context2(&ctx, NULL, scratch_pool));
  SVN_ERR(svn_test__init_auth_baton(&ctx->auth_baton, scratch_pool));

  SVN_ERR(svn_client__mtcc_create(&mtcc, *repos_url, 0, ctx,
                                  scratch_pool, iterpool));

  SVN_ERR(svn_client__mtcc_add_mkdir("trunk", mtcc, iterpool));

  for (ge = svn_test__greek_tree_nodes; ge->path; ge++)
    {
      const char *relpath;
      svn_pool_clear(iterpool);

      relpath = svn_relpath_join("trunk", ge->path, iterpool);

      if (!ge->contents)
        SVN_ERR(svn_client__mtcc_add_mkdir(relpath, mtcc, iterpool));
      else
        SVN_ERR(svn_client__mtcc_add_add_file(
                    relpath,
                    svn_stream_from_string(
                        svn_string_create(ge->contents,
                                          scratch_pool),
                        scratch_pool),
                    NULL,
                    mtcc, iterpool));
    }

  SVN_ERR(svn_client__mtcc_commit(apr_hash_make(iterpool),
                                  verify_commit, NULL,
                                  mtcc, iterpool));

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Implements svn_client_list_func2_t */
static svn_error_t *
ls_collect_names(void *baton,
                 const char *path,
                 const svn_dirent_t *dirent,
                 const svn_lock_t *lock,
                 const char *abs_path,
                 const char *external_parent_url,
                 const char *external_target,
                 apr_pool_t *scratch_pool)
{
  apr_hash_t *result = baton;

  path = apr_pstrdup(apr_hash_pool_get(result), path);
  svn_hash_sets(result, path, path);

  return SVN_NO_ERROR;
}

static svn_error_t *
ls_recursive(apr_hash_t **entries,
             const char *url_or_abspath,
             svn_client_ctx_t *ctx,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  svn_opt_revision_t head;
  apr_hash_t *result = apr_hash_make(result_pool);

  head.kind = svn_opt_revision_head;
  SVN_ERR(svn_client_list4(url_or_abspath, &head, &head,
                           NULL, svn_depth_infinity,
                           SVN_DIRENT_KIND, FALSE, FALSE,
                           ls_collect_names, result,
                           ctx, scratch_pool));
  *entries = result;
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

  SVN_ERR(create_git_repos(&repos_url, "git-mkdir", pool, subpool));

  SVN_ERR(svn_dirent_get_absolute(&wc_dir, "git-mkdir-wc", pool));
  SVN_ERR(svn_io_remove_dir2(wc_dir, TRUE, NULL, NULL, subpool));
  svn_test_add_dir_cleanup(wc_dir);

  svn_pool_clear(subpool);

  SVN_ERR(svn_client_create_context2(&ctx, NULL, pool));

  trunk_url = svn_path_url_add_component2(repos_url, "trunk", pool);
#if 0
  head_rev.kind = svn_opt_revision_head;
  SVN_ERR(svn_client_checkout3(&rev, trunk_url,
                               wc_dir, &head_rev, &head_rev, svn_depth_infinity,
                               FALSE, FALSE, ctx, pool));

  {
    apr_array_header_t *revs;
    apr_array_header_t *paths = apr_array_make(pool, 1, sizeof(const char *));
    APR_ARRAY_PUSH(paths, const char *) = wc_dir;

    SVN_ERR(svn_client_update4(&revs, paths, &head_rev, svn_depth_infinity, FALSE,
                               FALSE, FALSE, FALSE, FALSE, ctx, pool));
  }
#endif
  SVN_ERR(svn_client__mtcc_create(&mtcc, repos_url, 0, ctx, subpool, subpool));

  SVN_ERR(svn_client__mtcc_add_mkdir("trunk", mtcc, subpool));
  SVN_ERR(svn_client__mtcc_add_mkdir("trunk/A", mtcc, subpool));
  SVN_ERR(svn_client__mtcc_add_mkdir("trunk/A/E", mtcc, subpool));

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

static svn_error_t *
test_git_checkout(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  const char *repos_url;
  const char *wc_dir;
  svn_client_ctx_t *ctx;
  svn_opt_revision_t head_rev;
  svn_revnum_t rev;
  const char *trunk_url;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(create_git_repos_greek(&repos_url, "git-checkout-repos",
                                 pool, subpool));

  SVN_ERR(svn_dirent_get_absolute(&wc_dir, "git-checkout-wc", pool));
  SVN_ERR(svn_io_remove_dir2(wc_dir, TRUE, NULL, NULL, subpool));
  svn_test_add_dir_cleanup(wc_dir);

  SVN_ERR(svn_client_create_context2(&ctx, NULL, pool));
  trunk_url = svn_path_url_add_component2(repos_url, "trunk", pool);

  svn_pool_clear(subpool);

  head_rev.kind = svn_opt_revision_head;
  SVN_ERR(svn_client_checkout3(&rev, trunk_url,
                               wc_dir, &head_rev, &head_rev, svn_depth_infinity,
                               FALSE, FALSE, ctx, subpool));

  svn_pool_clear(subpool);
  {
    apr_array_header_t *revs;
    apr_array_header_t *paths = apr_array_make(pool, 1, sizeof(const char *));
    APR_ARRAY_PUSH(paths, const char *) = wc_dir;
  
    SVN_ERR(svn_client_update4(&revs, paths, &head_rev, svn_depth_infinity, FALSE,
                               FALSE, FALSE, FALSE, FALSE, ctx, subpool));
  }


  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}



static svn_error_t *
test_git_add_nodes(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  const char *repos_url;
  svn_client_ctx_t *ctx;
  svn_client__mtcc_t *mtcc;
  const char *trunk_url;
  apr_hash_t *names;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(create_git_repos_greek(&repos_url, "git-add-nodes-repos",
                                 pool, subpool));

  SVN_ERR(svn_client_create_context2(&ctx, NULL, pool));
  SVN_ERR(svn_test__init_auth_baton(&ctx->auth_baton, pool));

  trunk_url = svn_path_url_add_component2(repos_url, "trunk", pool);

  SVN_ERR(ls_recursive(&names, trunk_url, ctx, pool, subpool));
  SVN_TEST_INT_ASSERT(apr_hash_count(names), 21);

  SVN_ERR(svn_client__mtcc_create(&mtcc, trunk_url, 2, ctx, subpool, subpool));
  SVN_ERR(svn_client__mtcc_add_delete("A/D/H/chi", mtcc, subpool));
  SVN_ERR(svn_client__mtcc_add_mkdir("A/subdir", mtcc, subpool));
  SVN_ERR(svn_client__mtcc_add_add_file("A/new", 
                                        svn_stream_from_string(
                                          svn_string_create("new\n", subpool),
                                          subpool),
                                        NULL,
                                        mtcc, subpool));

  SVN_ERR(svn_client__mtcc_commit(apr_hash_make(subpool),
                                  verify_commit, NULL, mtcc, subpool));
  svn_pool_clear(subpool);

  SVN_ERR(ls_recursive(&names, trunk_url, ctx, pool, subpool));
  SVN_TEST_INT_ASSERT(apr_hash_count(names), 22);


  SVN_ERR(svn_client__mtcc_create(&mtcc,
                                  svn_path_url_add_component2(trunk_url, "A/D",
                                                              subpool),
                                  3, ctx, subpool, subpool));
  SVN_ERR(svn_client__mtcc_add_delete("G/tau", mtcc, subpool));
  SVN_ERR(svn_client__mtcc_add_mkdir("G/subdir", mtcc, subpool));
  SVN_ERR(svn_client__mtcc_add_add_file("G/subdir/new",
                                        svn_stream_from_string(
                                          svn_string_create("new\n", subpool),
                                          subpool),
                                        NULL,
                                        mtcc, subpool));
  SVN_ERR(svn_client__mtcc_add_update_file("H/psi",
                                           svn_stream_from_string(
                                             svn_string_create("updated\n",
                                                               subpool),
                                             subpool),
                                           NULL,
                                           svn_stream_from_string(
                                             svn_string_create(
                                                "This is the file 'pi'.\n",
                                                               subpool),
                                             subpool),
                                           NULL,
                                           mtcc, subpool));

  SVN_ERR(svn_client__mtcc_commit(apr_hash_make(subpool),
                                  verify_commit, NULL, mtcc, subpool));
  svn_pool_clear(subpool);

  SVN_ERR(ls_recursive(&names, trunk_url, ctx, pool, subpool));
  SVN_TEST_INT_ASSERT(apr_hash_count(names), 23);

  svn_pool_clear(subpool);

  SVN_ERR(svn_client__mtcc_create(&mtcc,
                                  svn_path_url_add_component2(trunk_url, "A",
                                                              subpool),
                                  4, ctx, subpool, subpool));
  SVN_ERR(svn_client__mtcc_add_copy("D", 2, "DD", mtcc, subpool));
  SVN_ERR(svn_client__mtcc_add_copy("D/G/rho", 2, "rho", mtcc, subpool));
  SVN_ERR(svn_client__mtcc_add_copy("D/G/rho", 2, "DD/rho", mtcc, subpool));

  SVN_ERR(svn_client__mtcc_commit(apr_hash_make(subpool),
                                  verify_commit, NULL, mtcc, subpool));

  svn_pool_clear(subpool);

  SVN_ERR(ls_recursive(&names, trunk_url, ctx, pool, subpool));
  SVN_TEST_INT_ASSERT(apr_hash_count(names), 35);

  svn_pool_clear(subpool);

  return SVN_NO_ERROR;
}

/* ========================================================================== */


static int max_threads = 3;

static struct svn_test_descriptor_t test_funcs[] =
{
  SVN_TEST_NULL,
  SVN_TEST_OPTS_PASS(test_git_mkdir,
                     "test git_mkdir"),
  SVN_TEST_OPTS_PASS(test_git_checkout,
                     "test git_checkout"),
  SVN_TEST_OPTS_PASS(test_git_add_nodes,
                     "test git_add_nodes"),

  SVN_TEST_NULL
};

SVN_TEST_MAIN

