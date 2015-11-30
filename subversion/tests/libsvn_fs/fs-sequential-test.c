/* fs-sequential-test.c --- tests for the filesystem
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
#include <apr_thread_proc.h>
#include <assert.h>

#include "../svn_test.h"

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_time.h"
#include "svn_string.h"
#include "svn_fs.h"
#include "svn_checksum.h"
#include "svn_mergeinfo.h"
#include "svn_props.h"
#include "svn_version.h"

#include "svn_private_config.h"
#include "private/svn_fs_util.h"
#include "private/svn_fs_private.h"
#include "private/svn_fspath.h"

#include "../svn_test_fs.h"

#include "../../libsvn_delta/delta.h"
#include "../../libsvn_fs/fs-loader.h"

#define SET_STR(ps, s) ((ps)->data = (s), (ps)->len = strlen(s))


/*-----------------------------------------------------------------*/

/** The actual fs-sequential-tests called by `make check` **/

/* Helper:  commit TXN, expecting either success or failure:
 *
 * If EXPECTED_CONFLICT is null, then the commit is expected to
 * succeed.  If it does succeed, set *NEW_REV to the new revision;
 * else return error.
 *
 * If EXPECTED_CONFLICT is non-null, it is either the empty string or
 * the expected path of the conflict.  If it is the empty string, any
 * conflict is acceptable.  If it is a non-empty string, the commit
 * must fail due to conflict, and the conflict path must match
 * EXPECTED_CONFLICT.  If they don't match, return error.
 *
 * If a conflict is expected but the commit succeeds anyway, return
 * error.  If the commit fails but does not provide an error, return
 * error.
 */
static svn_error_t *
test_commit_txn(svn_revnum_t *new_rev,
                svn_fs_txn_t *txn,
                const char *expected_conflict,
                apr_pool_t *pool)
{
  const char *conflict;
  svn_error_t *err;

  err = svn_fs_commit_txn(&conflict, new_rev, txn, pool);

  if (err && (err->apr_err == SVN_ERR_FS_CONFLICT))
    {
      svn_error_clear(err);
      if (! expected_conflict)
        {
          return svn_error_createf
            (SVN_ERR_FS_CONFLICT, NULL,
             "commit conflicted at '%s', but no conflict expected",
             conflict ? conflict : "(missing conflict info!)");
        }
      else if (conflict == NULL)
        {
          return svn_error_createf
            (SVN_ERR_FS_CONFLICT, NULL,
             "commit conflicted as expected, "
             "but no conflict path was returned ('%s' expected)",
             expected_conflict);
        }
      else if ((strcmp(expected_conflict, "") != 0)
               && (strcmp(conflict, expected_conflict) != 0))
        {
          return svn_error_createf
            (SVN_ERR_FS_CONFLICT, NULL,
             "commit conflicted at '%s', but expected conflict at '%s')",
             conflict, expected_conflict);
        }

      /* The svn_fs_commit_txn() API promises to set *NEW_REV to an
         invalid revision number in the case of a conflict.  */
      if (SVN_IS_VALID_REVNUM(*new_rev))
        {
          return svn_error_createf
            (SVN_ERR_FS_GENERAL, NULL,
             "conflicting commit returned valid new revision");
        }
    }
  else if (err)   /* commit may have succeeded, but always report an error */
    {
      if (SVN_IS_VALID_REVNUM(*new_rev))
        return svn_error_quick_wrap
          (err, "commit succeeded but something else failed");
      else
        return svn_error_quick_wrap
          (err, "commit failed due to something other than a conflict");
    }
  else            /* err == NULL, commit should have succeeded */
    {
      if (! SVN_IS_VALID_REVNUM(*new_rev))
        {
          return svn_error_create
            (SVN_ERR_FS_GENERAL, NULL,
             "commit failed but no error was returned");
        }

      if (expected_conflict)
        {
          return svn_error_createf
            (SVN_ERR_FS_GENERAL, NULL,
             "commit succeeded that was expected to fail at '%s'",
             expected_conflict);
        }
    }

  return SVN_NO_ERROR;
}

#if APR_HAS_THREADS
struct reopen_modify_baton_t {
  const char *fs_path;
  const char *txn_name;
  apr_pool_t *pool;
  svn_error_t *err;
};

static void * APR_THREAD_FUNC
reopen_modify_child(apr_thread_t *tid, void *data)
{
  struct reopen_modify_baton_t *baton = data;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *root;

  baton->err = svn_fs_open(&fs, baton->fs_path, NULL, baton->pool);
  if (!baton->err)
    baton->err = svn_fs_open_txn(&txn, fs, baton->txn_name, baton->pool);
  if (!baton->err)
    baton->err = svn_fs_txn_root(&root, txn, baton->pool);
  if (!baton->err)
    baton->err = svn_fs_change_node_prop(root, "A", "name",
                                         svn_string_create("value",
                                                           baton->pool),
                                         baton->pool);
  svn_pool_destroy(baton->pool);
  apr_thread_exit(tid, 0);
  return NULL;
}
#endif

static svn_error_t *
reopen_modify(const svn_test_opts_t *opts,
              apr_pool_t *pool)
{
#if APR_HAS_THREADS
  svn_fs_t *fs;
  svn_revnum_t head_rev = 0;
  svn_fs_root_t *root;
  svn_fs_txn_t *txn;
  const char *fs_path, *txn_name;
  svn_string_t *value;
  struct reopen_modify_baton_t baton;
  apr_status_t status, child_status;
  apr_threadattr_t *tattr;
  apr_thread_t *tid;

  /* Create test repository with greek tree. */
  fs_path = "test-reopen-modify";
  SVN_ERR(svn_test__create_fs(&fs, fs_path, opts, pool));
  SVN_ERR(svn_fs_begin_txn(&txn, fs, head_rev, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(root, pool));
  SVN_ERR(test_commit_txn(&head_rev, txn, NULL, pool));

  /* Create txn with changes. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, head_rev, pool));
  SVN_ERR(svn_fs_txn_name(&txn_name, txn, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_make_dir(root, "X", pool));

  /* In another thread: reopen fs and txn, and add more changes.  This
     works in BDB and FSX but in FSFS the txn_dir_cache becomes
     out-of-date and the thread's changes don't reach the revision. */
  baton.fs_path = fs_path;
  baton.txn_name = txn_name;
  baton.pool = svn_pool_create(pool);
  status = apr_threadattr_create(&tattr, pool);
  if (status)
    return svn_error_wrap_apr(status, _("Can't create threadattr"));
  status = apr_thread_create(&tid, tattr, reopen_modify_child, &baton, pool);
  if (status)
    return svn_error_wrap_apr(status, _("Can't create thread"));
  status = apr_thread_join(&child_status, tid);
  if (status)
    return svn_error_wrap_apr(status, _("Can't join thread"));
  if (baton.err)
    return svn_error_trace(baton.err);

  /* Commit */
  SVN_ERR(test_commit_txn(&head_rev, txn, NULL, pool));

  /* Check for change made by thread. */
  SVN_ERR(svn_fs_revision_root(&root, fs, head_rev, pool));
  SVN_ERR(svn_fs_node_prop(&value, root, "A", "name", pool));
  SVN_TEST_ASSERT(value && !strcmp(value->data, "value"));

  return SVN_NO_ERROR;
#else
  return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL, "no thread support");
#endif
}

/* Convenience wrapper around svn_fs_change_rev_prop2. */
static svn_error_t *
set_revprop(svn_fs_t *fs,
            svn_revnum_t revision,
            const char *value,
            apr_pool_t *scratch_pool)
{
  svn_string_t *content = svn_string_create(value, scratch_pool);
  SVN_ERR(svn_fs_change_rev_prop2(fs, revision, "prop", NULL, content,
                                  scratch_pool));

  return SVN_NO_ERROR;
}

/* Call svn_fs_revision_prop2 and verify that the property value matches
 * EXPECTED. */
static svn_error_t *
check_revprop(svn_fs_t *fs,
              svn_revnum_t revision,
              svn_boolean_t refresh,
              const char *expected,
              apr_pool_t *scratch_pool)
{
  svn_string_t *actual;
  SVN_ERR(svn_fs_revision_prop2(&actual, fs, revision, "prop", refresh,
                                scratch_pool, scratch_pool));
  SVN_TEST_STRING_ASSERT(actual->data, expected);

  return SVN_NO_ERROR;
}

static svn_error_t *
revprop_refresh(const svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  svn_fs_t *fs, *fs2;
  int i;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_string_t *old_value, *new_value;
  apr_hash_t *config;

  if (!strcmp(opts->fs_type, "bdb"))
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "the BDB backend ignores the refresh option");

  /* That option is required to make this work with FSX. */
  config = apr_hash_make(pool);
  svn_hash_sets(config, SVN_FS_CONFIG_FSFS_CACHE_REVPROPS, "1");

  /* Build a repository with a few revisions in it. */
  SVN_ERR(svn_test__create_fs2(&fs, "test-repo-revprop-refresh", opts,
                               config, pool));
  SVN_ERR(svn_fs_open2(&fs2, "test-repo-revprop-refresh", config, pool,
                       pool));

  for (i = 1; i < 5; ++i)
    {
      svn_fs_txn_t *txn;
      svn_fs_root_t *txn_root;
      svn_revnum_t new_rev = 0;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_fs_begin_txn(&txn, fs, new_rev, iterpool));
      SVN_ERR(svn_fs_txn_root(&txn_root, txn, iterpool));
      SVN_ERR(svn_fs_make_dir(txn_root, apr_itoa(pool, i), iterpool));
      SVN_ERR(test_commit_txn(&new_rev, txn, NULL, iterpool));
    }

  /* The initial access sees the latest revprops - even without refresh. */
  SVN_ERR(set_revprop(fs, 0, "x0", pool));
  SVN_ERR(set_revprop(fs, 1, "x1", pool));
  SVN_ERR(set_revprop(fs, 2, "x2", pool));
  SVN_ERR(check_revprop(fs2, 0, FALSE, "x0", pool));
  SVN_ERR(check_revprop(fs2, 1, FALSE, "x1", pool));
  SVN_ERR(check_revprop(fs2, 2, FALSE, "x2", pool));

  /* With the REFRESH option set, revprop changes are immediately visible. */
  SVN_ERR(set_revprop(fs, 0, "y0", pool));
  SVN_ERR(set_revprop(fs, 1, "y1", pool));
  SVN_ERR(set_revprop(fs, 2, "y2", pool));
  SVN_ERR(check_revprop(fs2, 0, TRUE, "y0", pool));
  SVN_ERR(check_revprop(fs2, 1, TRUE, "y1", pool));
  SVN_ERR(check_revprop(fs2, 2, TRUE, "y2", pool));

  /* Without the REFRESH option set, revprop changes not always visible.
   * Our cache is large enough that we won't see any change.
   * But first we have to heat up our cache. */
  SVN_ERR(check_revprop(fs2, 0, FALSE, "y0", pool));
  SVN_ERR(check_revprop(fs2, 1, FALSE, "y1", pool));
  SVN_ERR(check_revprop(fs2, 2, FALSE, "y2", pool));
  SVN_ERR(set_revprop(fs, 0, "z0", pool));
  SVN_ERR(set_revprop(fs, 1, "z1", pool));
  SVN_ERR(set_revprop(fs, 2, "z2", pool));
  SVN_ERR(check_revprop(fs2, 0, FALSE, "y0", pool));
  SVN_ERR(check_revprop(fs2, 1, FALSE, "y1", pool));
  SVN_ERR(check_revprop(fs2, 2, FALSE, "y2", pool));

  /* An explicit refresh helps. */
  SVN_ERR(svn_fs_refresh_revision_props(fs2, pool));
  SVN_ERR(check_revprop(fs2, 0, FALSE, "z0", pool));
  SVN_ERR(check_revprop(fs2, 1, FALSE, "z1", pool));
  SVN_ERR(check_revprop(fs2, 2, FALSE, "z2", pool));

  /* A single REFRESH is enough to make *all* recent changes visible. */
  SVN_ERR(set_revprop(fs, 0, "t0", pool));
  SVN_ERR(set_revprop(fs, 1, "t1", pool));
  SVN_ERR(set_revprop(fs, 2, "t2", pool));
  SVN_ERR(check_revprop(fs2, 0, FALSE, "z0", pool));
  SVN_ERR(check_revprop(fs2, 1, TRUE, "t1", pool));
  SVN_ERR(check_revprop(fs2, 2, FALSE, "t2", pool));
  SVN_ERR(check_revprop(fs2, 0, FALSE, "t0", pool));

  /* A single revprop write is enough to make *all* recent changes visible. */
  SVN_ERR(set_revprop(fs, 0, "u0", pool));
  SVN_ERR(set_revprop(fs, 1, "u1", pool));
  SVN_ERR(set_revprop(fs, 2, "u2", pool));
  SVN_ERR(check_revprop(fs2, 0, FALSE, "t0", pool));
  SVN_ERR(set_revprop(fs2, 3, "a3", pool));
  SVN_ERR(check_revprop(fs2, 1, FALSE, "u1", pool));
  SVN_ERR(check_revprop(fs2, 2, FALSE, "u2", pool));
  SVN_ERR(check_revprop(fs2, 0, FALSE, "u0", pool));

  /* A revprop write is always visible to the writer. */
  SVN_ERR(check_revprop(fs, 0, FALSE, "u0", pool));
  SVN_ERR(check_revprop(fs, 1, FALSE, "u1", pool));
  SVN_ERR(check_revprop(fs, 2, FALSE, "u2", pool));
  SVN_ERR(check_revprop(fs2, 3, FALSE, "a3", pool));

  /* An atomic revprop write will always verify against the on-disk data. */
  SVN_ERR(set_revprop(fs, 0, "v0", pool));

  SVN_ERR(check_revprop(fs, 0, FALSE, "v0", pool));
  SVN_ERR(check_revprop(fs2, 0, FALSE, "u0", pool));

  old_value = svn_string_create("v0", pool);
  new_value = svn_string_create("b0", pool);
  SVN_ERR(svn_fs_change_rev_prop2(fs2, 0, "prop",
                                  (const svn_string_t * const *)&old_value,
                                  new_value, pool));

  SVN_ERR(check_revprop(fs, 0, FALSE, "v0", pool));
  SVN_ERR(check_revprop(fs2, 0, FALSE, "b0", pool));

  old_value = svn_string_create("v0", pool);
  new_value = svn_string_create("w0", pool);
  SVN_TEST_ASSERT_ERROR(svn_fs_change_rev_prop2(fs, 0, "prop",
                                  (const svn_string_t * const *)&old_value,
                                  new_value, pool),
                        SVN_ERR_FS_PROP_BASEVALUE_MISMATCH);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* The test table.  */

static int max_threads = 1;  /* Run tests sequentially. */

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(reopen_modify,
                       "test reopen and modify txn"),
    SVN_TEST_OPTS_PASS(revprop_refresh,
                       "refresh option in FS revprop API"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
