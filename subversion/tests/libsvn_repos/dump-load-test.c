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



/* Test dumping in the presence of the property PROP_NAME:PROP_VAL.
 * Return the dumped data in *DUMP_DATA_P (if DUMP_DATA_P is not null).
 * REPOS is an empty repository.
 * See svn_repos_dump_fs3() for START_REV, END_REV, NOTIFY_FUNC, NOTIFY_BATON.
 */
static svn_error_t *
test_dump_bad_props(svn_stringbuf_t **dump_data_p,
                    svn_repos_t *repos,
                    const char *prop_name,
                    const svn_string_t *prop_val,
                    svn_revnum_t start_rev,
                    svn_revnum_t end_rev,
                    svn_repos_notify_func_t notify_func,
                    void *notify_baton,
                    apr_pool_t *pool)
{
  const char *test_path = "/bar";
  svn_fs_t *fs = svn_repos_fs(repos);
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_revnum_t youngest_rev = 0;
  svn_stringbuf_t *dump_data = svn_stringbuf_create_empty(pool);
  svn_stream_t *stream = svn_stream_from_stringbuf(dump_data, pool);
  const char *expected_str;

  /* Revision 1:  Any commit will do, here  */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, youngest_rev, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_dir(txn_root, test_path , pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /* Revision 2:  Add the bad property */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, youngest_rev, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_change_node_prop(txn_root, test_path , prop_name, prop_val,
                                  pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  /* Test that a dump completes without error. */
  SVN_ERR(svn_repos_dump_fs4(repos, stream, start_rev, end_rev,
                             FALSE, FALSE, TRUE, TRUE,
                             notify_func, notify_baton,
                             NULL, NULL, NULL, NULL,
                             pool));
  svn_stream_close(stream);

  /* Check that the property appears in the dump data */
  expected_str = apr_psprintf(pool, "K %d\n%s\n"
                                    "V %d\n%s\n"
                                    "PROPS-END\n",
                              (int)strlen(prop_name), prop_name,
                              (int)prop_val->len, prop_val->data);
  SVN_TEST_ASSERT(strstr(dump_data->data, expected_str));

  if (dump_data_p)
    *dump_data_p = dump_data;
  return SVN_NO_ERROR;
}

/* Test loading in the presence of the property PROP_NAME:PROP_VAL.
 * Load data from DUMP_DATA.
 * REPOS is an empty repository.
 */
static svn_error_t *
test_load_bad_props(svn_stringbuf_t *dump_data,
                    svn_repos_t *repos,
                    const char *prop_name,
                    const svn_string_t *prop_val,
                    const char *parent_fspath,
                    svn_boolean_t validate_props,
                    svn_repos_notify_func_t notify_func,
                    void *notify_baton,
                    apr_pool_t *pool)
{
  const char *test_path = apr_psprintf(pool, "%s%s",
                                       parent_fspath ? parent_fspath : "",
                                       "/bar");
  svn_stream_t *stream = svn_stream_from_stringbuf(dump_data, pool);
  svn_fs_t *fs;
  svn_fs_root_t *rev_root;
  svn_revnum_t youngest_rev;
  svn_string_t *loaded_prop_val;

  SVN_ERR(svn_repos_load_fs6(repos, stream,
                             SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
                             svn_repos_load_uuid_default,
                             parent_fspath,
                             FALSE, FALSE, /*use_*_commit_hook*/
                             validate_props,
                             FALSE /*ignore_dates*/,
                             FALSE /*normalize_props*/,
                             notify_func, notify_baton,
                             NULL, NULL, /*cancellation*/
                             pool));
  svn_stream_close(stream);

  /* Check the loaded property */
  fs = svn_repos_fs(repos);
  SVN_ERR(svn_fs_youngest_rev(&youngest_rev, fs, pool));
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, youngest_rev, pool));
  SVN_ERR(svn_fs_node_prop(&loaded_prop_val,
                           rev_root, test_path, prop_name, pool));
  SVN_TEST_ASSERT(svn_string_compare(loaded_prop_val, prop_val));
  return SVN_NO_ERROR;
}

/* Notification receiver for test_dump_r0_mergeinfo(). This does not
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
  const char *prop_name = "svn:mergeinfo";
  const svn_string_t *bad_mergeinfo = svn_string_create("/foo:0", pool);
  svn_repos_t *repos;

  SVN_ERR(svn_test__create_repos(&repos, "test-repo-dump-r0-mergeinfo",
                                 opts, pool));
  /* In order to exercise the
     functionality under test -- that is, in order for the dump to try to
     parse the mergeinfo it is dumping -- the dump must start from a
     revision greater than 1 and must take a notification callback. */
  SVN_ERR(test_dump_bad_props(NULL, repos,
                              prop_name, bad_mergeinfo,
                              2, SVN_INVALID_REVNUM,
                              dump_r0_mergeinfo_notifier, NULL,
                              pool));

  return SVN_NO_ERROR;
}

static void
load_r0_mergeinfo_notifier(void *baton,
                           const svn_repos_notify_t *notify,
                           apr_pool_t *scratch_pool)
{
  svn_boolean_t *had_mergeinfo_warning = baton;

  if (notify->action == svn_repos_notify_warning)
    {
      if (notify->warning == svn_repos_notify_warning_invalid_mergeinfo)
        {
          *had_mergeinfo_warning = TRUE;
        }
    }
}

/* Regression test for the 'load' part of issue #4476 "Mergeinfo
 * containing r0 makes svnsync and svnadmin dump fail".
 *
 * Bad mergeinfo should not prevent loading a backup, at least when we do not
 * require mergeinfo revision numbers or paths to be adjusted during loading.
 */
static svn_error_t *
test_load_r0_mergeinfo(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  const char *prop_name = "svn:mergeinfo";
  const svn_string_t *prop_val = svn_string_create("/foo:0", pool);
  svn_stringbuf_t *dump_data = svn_stringbuf_create_empty(pool);

  /* Produce a dump file containing bad mergeinfo */
  {
    svn_repos_t *repos;

    SVN_ERR(svn_test__create_repos(&repos, "test-repo-load-r0-mi-1",
                                   opts, pool));
    SVN_ERR(test_dump_bad_props(&dump_data, repos,
                                prop_name, prop_val,
                                SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
                                NULL, NULL, pool));
  }

  /* Test loading without validating properties: should warn and succeed */
  {
    svn_repos_t *repos;
    svn_boolean_t had_mergeinfo_warning = FALSE;

    SVN_ERR(svn_test__create_repos(&repos, "test-repo-load-r0-mi-2",
                                   opts, pool));

    /* Without changing revision numbers or paths */
    SVN_ERR(test_load_bad_props(dump_data, repos,
                                prop_name, prop_val,
                                NULL /*parent_dir*/, FALSE /*validate_props*/,
                                load_r0_mergeinfo_notifier, &had_mergeinfo_warning,
                                pool));
    SVN_TEST_ASSERT(had_mergeinfo_warning);

    /* With changing revision numbers and/or paths (by loading the same data
       again, on top of existing revisions, into subdirectory 'bar') */
    had_mergeinfo_warning = FALSE;
    SVN_ERR(test_load_bad_props(dump_data, repos,
                                prop_name, prop_val,
                                "/bar", FALSE /*validate_props*/,
                                load_r0_mergeinfo_notifier, &had_mergeinfo_warning,
                                pool));
    SVN_TEST_ASSERT(had_mergeinfo_warning);
  }

  /* Test loading with validating properties: should return an error */
  {
    svn_repos_t *repos;

    SVN_ERR(svn_test__create_repos(&repos, "test-repo-load-r0-mi-3",
                                   opts, pool));

    /* Without changing revision numbers or paths */
    SVN_TEST_ASSERT_ANY_ERROR(test_load_bad_props(dump_data, repos,
                                prop_name, prop_val,
                                NULL /*parent_dir*/, TRUE /*validate_props*/,
                                NULL, NULL,
                                pool));

    /* With changing revision numbers and/or paths (by loading the same data
       again, on top of existing revisions, into subdirectory 'bar') */
    SVN_TEST_ASSERT_ANY_ERROR(test_load_bad_props(dump_data, repos,
                                prop_name, prop_val,
                                "/bar", TRUE /*validate_props*/,
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
    SVN_TEST_OPTS_PASS(test_load_r0_mergeinfo,
                       "test loading with r0 mergeinfo"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
