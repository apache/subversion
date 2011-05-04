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
  const char *expected;
  const char *local_abspath;
  svn_skel_t *skel;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "Foo.c", pool));

  conflict = svn_wc_conflict_description_create_tree2(
                    local_abspath, svn_node_file, svn_wc_operation_update,
                    NULL, NULL, pool);
  conflict->action = svn_wc_conflict_action_delete;
  conflict->reason = svn_wc_conflict_reason_edited;

  SVN_ERR(svn_wc__serialize_conflict(&skel, conflict, pool, pool));
  tree_conflict_data = svn_skel__unparse(skel, pool)->data;

  expected = "(conflict Foo.c file update deleted edited "
             "(version 0  2 -1 0  0 ) (version 0  2 -1 0  0 ))";

  if (strcmp(expected, tree_conflict_data) != 0)
    return fail(pool, "Unexpected text from tree conflict\n"
                      "  Expected: %s\n"
                      "  Actual:   %s\n", expected, tree_conflict_data);

  return SVN_NO_ERROR;
}

/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_read_tree_conflict,
                   "read 1 tree conflict"),
    SVN_TEST_PASS2(test_write_tree_conflict,
                   "write 1 tree conflict"),
    SVN_TEST_NULL
  };

