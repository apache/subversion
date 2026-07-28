/* fs-fs-sequential-test.c --- tests for the FSFS filesystem
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

#include <apr_pools.h>

#include "../svn_test.h"

#include "private/svn_cache.h"
#include "svn_fs.h"
#include "svn_pools.h"
#include "svn_hash.h"
#include "svn_props.h"

#include "../svn_test_fs.h"

static svn_error_t *
revprop_cache_pollution(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  int i;
  int n;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_hash_t *config;
  svn_cache__info_t *membuffer_cache_info;
  apr_uint64_t used_cache_size;
  const char *fs_name = "test-repo-revprop-cache-pollution";

  config = apr_hash_make(pool);
  svn_hash_sets(config, SVN_FS_CONFIG_FSFS_CACHE_REVPROPS, "1");

  {
    svn_fs_t *fs;
    SVN_ERR(svn_test__create_fs2(&fs, fs_name, opts, config, pool));

    /* Create 10 revisions. */
    for (i = 1; i < 10; ++i)
      {
        svn_fs_txn_t *txn;
        svn_fs_root_t *txn_root;
        svn_revnum_t new_rev = 0;

        svn_pool_clear(iterpool);

        SVN_ERR(svn_fs_begin_txn(&txn, fs, new_rev, iterpool));
        SVN_ERR(svn_fs_txn_root(&txn_root, txn, iterpool));
        SVN_ERR(svn_fs_make_dir(txn_root, apr_itoa(iterpool, i), iterpool));
        SVN_ERR(svn_fs_commit_txn(NULL, &new_rev, txn, iterpool));
        SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(new_rev));
      }
  }

  /* Clear membuffer cache. */
  SVN_ERR(svn_cache__membuffer_clear(svn_cache__get_global_membuffer_cache()));
  membuffer_cache_info = svn_cache__membuffer_get_global_info(pool);
  SVN_TEST_INT_ASSERT(membuffer_cache_info->used_size, 0);

  /* Read revision properties for all revisions. */
  {
    svn_fs_t *fs;

    SVN_ERR(svn_fs_open2(&fs, fs_name, config, pool, pool));
    for (i = 1; i < 10; ++i)
      {
        apr_hash_t *revprops;
        svn_pool_clear(iterpool);

        SVN_ERR(svn_fs_revision_proplist2(&revprops, fs, i, FALSE, iterpool,
                                          iterpool));
      }
  }

  membuffer_cache_info = svn_cache__membuffer_get_global_info(pool);
  SVN_TEST_ASSERT(membuffer_cache_info->used_size > 0);

  used_cache_size = membuffer_cache_info->used_size;

  /* Read revision properties for all revisions 50 times. */
  for (n = 0; n < 50; ++n)
    {
      svn_fs_t *fs;

      SVN_ERR(svn_fs_open2(&fs, fs_name, config, pool, pool));

      for (i = 1; i < 10; ++i)
        {
          apr_hash_t *revprops;
          svn_pool_clear(iterpool);

          SVN_ERR(svn_fs_revision_proplist2(&revprops, fs, i, FALSE, iterpool,
                                            iterpool));
        }
    }

  membuffer_cache_info = svn_cache__membuffer_get_global_info(pool);
  SVN_TEST_ASSERT(membuffer_cache_info->used_size <= used_cache_size);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* The test table.  */

static int max_threads = 1; /* Run tests sequentially. */

static struct svn_test_descriptor_t test_funcs[] = {
  SVN_TEST_NULL,
  SVN_TEST_OPTS_XFAIL(revprop_cache_pollution,
                      "cache pollution in revprop caching"),
  SVN_TEST_NULL
};

SVN_TEST_MAIN
