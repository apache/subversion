/*
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

/*
 * tree-conflict-data-test.c -- test the storage of tree conflict data
 */

#include <stdio.h>
#include <string.h>
#include <apr_hash.h>
#include <apr_tables.h>

#include "svn_pools.h"
#include "svn_hash.h"
#include "svn_types.h"
#include "svn_wc.h"
#include "private/svn_wc_private.h"
#include "../svn_test.h"
#include "../../libsvn_wc/tree_conflicts.h"

/* A quick way to create error messages.  */
static svn_error_t *
fail(apr_pool_t *pool, const char *fmt, ...)
{
  va_list ap;
  char *msg;

  va_start(ap, fmt);
  msg = apr_pvsprintf(pool, fmt, ap);
  va_end(ap);

  return svn_error_create(SVN_ERR_TEST_FAILED, 0, msg);
}

static svn_error_t *
test_read_tree_conflict(apr_pool_t *pool)
{
  const svn_wc_conflict_description2_t *conflict;
  svn_wc_conflict_description2_t *exp_conflict;
  const char *tree_conflict_data;
  const char *local_abspath;
  const svn_skel_t *skel;

  tree_conflict_data = "(conflict Foo.c file update deleted edited "
                        "(version 0  2 -1 0  0 ) (version 0  2 -1 0  0 ))";

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "Foo.c", pool));
  exp_conflict = svn_wc_conflict_description_create_tree2(
                        local_abspath, svn_node_file, svn_wc_operation_update,
                        NULL, NULL, pool);
  exp_conflict->action = svn_wc_conflict_action_delete;
  exp_conflict->reason = svn_wc_conflict_reason_edited;

  skel = svn_skel__parse(tree_conflict_data, strlen(tree_conflict_data), pool);
  SVN_ERR(svn_wc__deserialize_conflict(&conflict, skel, "", pool, pool));

  if ((conflict->node_kind != exp_conflict->node_kind) ||
      (conflict->action    != exp_conflict->action) ||
      (conflict->reason    != exp_conflict->reason) ||
      (conflict->operation != exp_conflict->operation) ||
      (strcmp(conflict->local_abspath, exp_conflict->local_abspath) != 0))
    return fail(pool, "Unexpected tree conflict");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_write_tree_conflict(apr_pool_t *pool)
{
  svn_wc_conflict_description2_t *conflict;
  const char *tree_conflict_data;
  apr_hash_t *conflicts;
  const char *expected;
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "Foo.c", pool));

  conflict = svn_wc_conflict_description_create_tree2(
                    local_abspath, svn_node_file, svn_wc_operation_update,
                    NULL, NULL, pool);
  conflict->action = svn_wc_conflict_action_delete;
  conflict->reason = svn_wc_conflict_reason_edited;

  conflicts = apr_hash_make(pool);
  apr_hash_set(conflicts, conflict->local_abspath, APR_HASH_KEY_STRING,
               conflict);

  expected = "((conflict Foo.c file update deleted edited "
               "(version 0  2 -1 0  0 ) (version 0  2 -1 0  0 )))";

  SVN_ERR(svn_wc__write_tree_conflicts(&tree_conflict_data, conflicts, pool));

  if (strcmp(expected, tree_conflict_data) != 0)
    return fail(pool, "Unexpected text from tree conflict\n"
                      "  Expected: %s\n"
                      "  Actual:   %s\n", expected, tree_conflict_data);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_write_2_tree_conflicts(apr_pool_t *pool)
{
  svn_wc_conflict_description2_t *conflict1, *conflict2;
  apr_hash_t *conflicts;
  const char *tree_conflict_data;
  const char *expected1;
  const char *expected2;
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "Foo.c", pool));
  conflict1 = svn_wc_conflict_description_create_tree2(
                    local_abspath, svn_node_file, svn_wc_operation_update,
                    NULL, NULL, pool);
  conflict1->action = svn_wc_conflict_action_delete;
  conflict1->reason = svn_wc_conflict_reason_edited;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "Bar.h", pool));
  conflict2 = svn_wc_conflict_description_create_tree2(
                    local_abspath, svn_node_file, svn_wc_operation_update,
                    NULL, NULL, pool);
  conflict2->action = svn_wc_conflict_action_edit;
  conflict2->reason = svn_wc_conflict_reason_deleted;

  conflicts = apr_hash_make(pool);
  apr_hash_set(conflicts, conflict1->local_abspath, APR_HASH_KEY_STRING,
               conflict1);
  apr_hash_set(conflicts, conflict2->local_abspath, APR_HASH_KEY_STRING,
               conflict2);

  /* We don't know the order the hash will spit out the data, so just test
     for both possibilities. */
  expected1 = "((conflict Foo.c file update deleted edited "
                 "(version 0  2 -1 0  0 ) (version 0  2 -1 0  0 )) "
               "(conflict Bar.h file update edited deleted "
                 "(version 0  2 -1 0  0 ) (version 0  2 -1 0  0 )))";
  expected2 = "((conflict Bar.h file update edited deleted "
                 "(version 0  2 -1 0  0 ) (version 0  2 -1 0  0 )) "
               "(conflict Foo.c file update deleted edited "
                 "(version 0  2 -1 0  0 ) (version 0  2 -1 0  0 )))";

  SVN_ERR(svn_wc__write_tree_conflicts(&tree_conflict_data, conflicts, pool));

  if (strcmp(expected1, tree_conflict_data) != 0
        && strcmp(expected2, tree_conflict_data) != 0)
    return fail(pool, "Unexpected text from tree conflict\n"
                      "  Expected: %s\n"
                      "         OR %s\n"
                      "  Actual:   %s\n", expected1, expected2,
                                          tree_conflict_data);

  return SVN_NO_ERROR;
}

#ifdef THIS_TEST_RAISES_MALFUNCTION
static svn_error_t *
test_write_invalid_tree_conflicts(apr_pool_t *pool)
{
  svn_wc_conflict_description2_t *conflict;
  apr_hash_t *conflicts;
  const char *tree_conflict_data;
  svn_error_t *err;
  const char *local_abspath;

  /* Configure so that we can test for errors caught by SVN_ERR_ASSERT. */
  svn_error_set_malfunction_handler(svn_error_raise_on_malfunction);

  conflicts = apr_hash_make(pool);

  /* node_kind */
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "Foo", pool));
  conflict = svn_wc_conflict_description_create_tree2(
                    local_abspath, svn_node_none, svn_wc_operation_update,
                    NULL, NULL, pool);
  conflict->action = svn_wc_conflict_action_delete;
  conflict->reason = svn_wc_conflict_reason_edited;

  apr_hash_set(conflicts, conflict->local_abspath, APR_HASH_KEY_STRING,
               conflict);

  err = svn_wc__write_tree_conflicts(&tree_conflict_data, conflicts, pool);
  if (err == SVN_NO_ERROR)
    return fail(pool,
                "Failed to detect invalid conflict node_kind");
  svn_error_clear(err);
  svn_hash__clear(conflicts, pool);

  /* operation */
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "Foo.c", pool));
  conflict = svn_wc_conflict_description_create_tree2(
                    local_abspath, svn_node_file, 99,
                    NULL, NULL, pool);
  conflict->action = svn_wc_conflict_action_delete;
  conflict->reason = svn_wc_conflict_reason_edited;

  apr_hash_set(conflicts, conflict->local_abspath, APR_HASH_KEY_STRING,
               conflict);

  err = svn_wc__write_tree_conflicts(&tree_conflict_data, conflicts, pool);
  if (err == SVN_NO_ERROR)
    return fail(pool,
                "Failed to detect invalid conflict operation");
  svn_error_clear(err);
  svn_hash__clear(conflicts, pool);

  /* action */
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "Foo.c", pool));
  conflict = svn_wc_conflict_description_create_tree2(
                    local_abspath, svn_node_file, svn_wc_operation_update,
                    NULL, NULL, pool);
  conflict->action = 99;
  conflict->reason = svn_wc_conflict_reason_edited;

  apr_hash_set(conflicts, conflict->local_abspath, APR_HASH_KEY_STRING,
               conflict);

  err = svn_wc__write_tree_conflicts(&tree_conflict_data, conflicts, pool);
  if (err == SVN_NO_ERROR)
    return fail(pool,
                "Failed to detect invalid conflict action");
  svn_error_clear(err);
  svn_hash__clear(conflicts, pool);

  /* reason */
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "Foo.c", pool));
  conflict = svn_wc_conflict_description_create_tree2(
                    local_abspath, svn_node_file, svn_wc_operation_update,
                    NULL, NULL, pool);
  conflict->action = svn_wc_conflict_action_delete;
  conflict->reason = 99;

  apr_hash_set(conflicts, conflict->local_abspath, APR_HASH_KEY_STRING,
               conflict);

  err = svn_wc__write_tree_conflicts(&tree_conflict_data, conflicts, pool);
  if (err == SVN_NO_ERROR)
    return fail(pool,
                "Failed to detect invalid conflict reason");
  svn_error_clear(err);
  svn_hash__clear(conflicts, pool);

  return SVN_NO_ERROR;
}
#endif


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_read_tree_conflict,
                   "read 1 tree conflict"),
    SVN_TEST_PASS2(test_write_tree_conflict,
                   "write 1 tree conflict"),
    SVN_TEST_PASS2(test_write_2_tree_conflicts,
                   "write 2 tree conflicts"),
#ifdef THIS_TEST_RAISES_MALFUNCTION
    SVN_TEST_PASS2(test_write_invalid_tree_conflicts,
                   "detect broken tree conflict data while writing"),
#endif
    SVN_TEST_NULL
  };

