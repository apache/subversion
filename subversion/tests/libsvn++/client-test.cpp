/*
 * util-test.c -- test the libsvn++ utilities
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

#include "../svn_test.h"
#include "../svn_test_fs.h"

#include "Client.h"
#include "Pool.h"

#include <sstream>
#include <iostream>

using namespace SVN;

static svn_error_t *
create_greek_repo(svn_repos_t **repos,
                  const char *repos_path,
                  const svn_test_opts_t *opts,
                  apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t committed_rev;

  SVN_ERR(svn_io_remove_dir2(repos_path, TRUE, NULL, NULL, pool));
  SVN_ERR(svn_test__create_repos(repos, repos_path, opts, pool));
  fs = svn_repos_fs(*repos);

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, *repos, &committed_rev, txn, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_get_version(apr_pool_t *p)
{
  Client client;

  Version v = client.getVersion();
  SVN_TEST_ASSERT(v.getTag() == SVN_VER_NUMTAG);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_cat(const svn_test_opts_t *opts,
         apr_pool_t *p)
{
  Pool pool;
  svn_repos_t *repos;
  const char *repos_url;
  const char *wc_path;
  const char *cwd;
  std::string iota_url;
  std::string iota_path;

  SVN_ERR(create_greek_repo(&repos, "test-cpp-client-repos", opts,
                            pool.pool()));
  SVN_ERR(svn_uri_get_file_url_from_dirent(&repos_url, "test-cpp-client-repos",
                                           pool.pool()));
  iota_url = svn_path_url_add_component2(repos_url, "iota", pool.pool());

  std::ostringstream stream;
  Client client;

  client.cat(stream, iota_url);
  SVN_TEST_ASSERT(stream.str() == "This is the file 'iota'.\n");

  SVN_ERR(svn_dirent_get_absolute(&cwd, "", pool.pool()));
  wc_path = svn_dirent_join(cwd, "test-cpp-client-wc", pool.pool());
  SVN_ERR(svn_io_remove_dir2(wc_path, TRUE, NULL, NULL, pool.pool()));
  client.checkout(repos_url, wc_path);

  iota_path = svn_dirent_join(wc_path, "iota", pool.pool());
  stream.clear();
  stream.str("");
  client.cat(stream, iota_path);
  SVN_TEST_ASSERT(stream.str() == "This is the file 'iota'.\n");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_checkout(const svn_test_opts_t *opts,
              apr_pool_t *ap)
{
  Pool pool;
  svn_repos_t *repos;
  const char *repos_url;
  const char *wc_path;
  const char *cwd;

  SVN_ERR(create_greek_repo(&repos, "test-cpp-client-repos", opts,
                            pool.pool()));
  SVN_ERR(svn_uri_get_file_url_from_dirent(&repos_url, "test-cpp-client-repos",
                                           pool.pool()));

  Client client;

  SVN_ERR(svn_dirent_get_absolute(&cwd, "", pool.pool()));
  wc_path = svn_dirent_join(cwd, "test-cpp-client-wc", pool.pool());
  SVN_ERR(svn_io_remove_dir2(wc_path, TRUE, NULL, NULL, pool.pool()));
  Revision result_rev = client.checkout(repos_url, wc_path);

  SVN_TEST_ASSERT(result_rev.revision()->value.number == 1);

  return SVN_NO_ERROR;
}

/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_get_version,
                   "test get client version"),
    SVN_TEST_OPTS_PASS(test_cat,
                       "test client cat"),
    SVN_TEST_OPTS_PASS(test_checkout,
                       "test client checkout"),
    SVN_TEST_NULL
  };
