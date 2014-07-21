/* dump-load-test.c --- tests for dumping and loading repositories
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
#include <apr_pools.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "private/svn_repos_private.h"

#include "../svn_test.h"
#include "../svn_test_fs.h"



/* Notification receiver for test_dump_bad_mergeinfo(). This does not
   need to do anything, it just needs to exist.
 */
static void
dump_r0_mergeinfo_notifier(void *baton,
                           const svn_repos_notify_t *notify,
                           apr_pool_t *scratch_pool)
{
}

/* Regression test for the 'dump' part of issue #4476 "Mergeinfo
   containing r0 makes svnsync and svnadmin dump fail". */
static svn_error_t *
test_dump_r0_mergeinfo(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t youngest_rev = 0;
  const svn_string_t *bad_mergeinfo = svn_string_create("/foo:0", pool);

  SVN_ERR(svn_test__create_repos(&repos, "test-repo-dump-r0-mergeinfo",
                                 opts, pool));
  fs = svn_repos_fs(repos);

  /* Revision 1:  Any commit will do, here  */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, youngest_rev, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_dir(txn_root, "/bar", pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /* Revision 2:  Add bad mergeinfo */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, youngest_rev, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_change_node_prop(txn_root, "/bar", "svn:mergeinfo", bad_mergeinfo, pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /* Test that a dump completes without error. In order to exercise the
     functionality under test -- that is, in order for the dump to try to
     parse the mergeinfo it is dumping -- the dump must start from a
     revision greater than 1 and must take a notification callback. */
  {
    svn_stringbuf_t *stringbuf = svn_stringbuf_create_empty(pool);
    svn_stream_t *stream = svn_stream_from_stringbuf(stringbuf, pool);

    SVN_ERR(svn_repos_dump_fs3(repos, stream, 2, SVN_INVALID_REVNUM,
                               FALSE, FALSE,
                               dump_r0_mergeinfo_notifier, NULL,
                               NULL, NULL,
                               pool));
  }

  return SVN_NO_ERROR;
}

/* The test table.  */

static int max_threads = 4;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(test_dump_r0_mergeinfo,
                       "test dumping with r0 mergeinfo"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
