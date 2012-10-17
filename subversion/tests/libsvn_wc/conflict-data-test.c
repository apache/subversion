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
 * conflict-data-test.c -- test the storage of tree conflict data
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
#include "utils.h"
#include "../svn_test.h"
#include "../../libsvn_wc/tree_conflicts.h"
#include "../../libsvn_wc/wc.h"
#include "../../libsvn_wc/wc_db.h"
#include "../../libsvn_wc/conflicts.h"

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

/* Raise a test error if EXPECTED and ACTUAL differ. */
static svn_error_t *
compare_version(const svn_wc_conflict_version_t *expected,
                const svn_wc_conflict_version_t *actual)
{
  SVN_TEST_STRING_ASSERT(expected->repos_url, actual->repos_url);
  SVN_TEST_ASSERT(expected->peg_rev == actual->peg_rev);
  SVN_TEST_STRING_ASSERT(expected->path_in_repos, actual->path_in_repos);
  SVN_TEST_ASSERT(expected->node_kind == actual->node_kind);
  return SVN_NO_ERROR;
}

/* Raise a test error if EXPECTED and ACTUAL differ or if ACTUAL is NULL. */
static svn_error_t *
compare_conflict(const svn_wc_conflict_description2_t *expected,
                 const svn_wc_conflict_description2_t *actual)
{
  SVN_TEST_ASSERT(actual != NULL);

  SVN_TEST_STRING_ASSERT(expected->local_abspath, actual->local_abspath);
  SVN_TEST_ASSERT(expected->node_kind == actual->node_kind);
  SVN_TEST_ASSERT(expected->kind == actual->kind);
  SVN_TEST_STRING_ASSERT(expected->property_name, actual->property_name);
  SVN_TEST_ASSERT(expected->is_binary == actual->is_binary);
  SVN_TEST_STRING_ASSERT(expected->mime_type, actual->mime_type);
  SVN_TEST_ASSERT(expected->action == actual->action);
  SVN_TEST_ASSERT(expected->reason == actual->reason);
  SVN_TEST_STRING_ASSERT(expected->base_abspath, actual->base_abspath);
  SVN_TEST_STRING_ASSERT(expected->their_abspath, actual->their_abspath);
  SVN_TEST_STRING_ASSERT(expected->my_abspath, actual->my_abspath);
  SVN_TEST_STRING_ASSERT(expected->merged_file, actual->merged_file);
  SVN_TEST_ASSERT(expected->operation == actual->operation);
  SVN_ERR(compare_version(expected->src_left_version,
                          actual->src_left_version));
  SVN_ERR(compare_version(expected->src_right_version,
                          actual->src_right_version));
  return SVN_NO_ERROR;
}

/* Create and return a tree conflict description */
static svn_wc_conflict_description2_t *
tree_conflict_create(const char *local_abspath,
                     svn_node_kind_t node_kind,
                     svn_wc_operation_t operation,
                     svn_wc_conflict_action_t action,
                     svn_wc_conflict_reason_t reason,
                     const char *left_repo,
                     const char *left_path,
                     svn_revnum_t left_revnum,
                     svn_node_kind_t left_kind,
                     const char *right_repo,
                     const char *right_path,
                     svn_revnum_t right_revnum,
                     svn_node_kind_t right_kind,
                     apr_pool_t *result_pool)
{
  svn_wc_conflict_version_t *left, *right;
  svn_wc_conflict_description2_t *conflict;

  left = svn_wc_conflict_version_create(left_repo, left_path, left_revnum,
                                        left_kind, result_pool);
  right = svn_wc_conflict_version_create(right_repo, right_path, right_revnum,
                                         right_kind, result_pool);
  conflict = svn_wc_conflict_description_create_tree2(
                    local_abspath, node_kind, operation,
                    left, right, result_pool);
  conflict->action = action;
  conflict->reason = reason;
  return conflict;
}

static svn_error_t *
test_deserialize_tree_conflict(apr_pool_t *pool)
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
test_serialize_tree_conflict_data(apr_pool_t *pool)
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

/* Test WC-DB-level conflict APIs. Especially tree conflicts. */
static svn_error_t *
test_read_write_tree_conflicts(const svn_test_opts_t *opts,
                               apr_pool_t *pool)
{
  svn_test__sandbox_t sbox;

  const char *parent_abspath;
  const char *child1_abspath, *child2_abspath;
  svn_wc_conflict_description2_t *conflict1, *conflict2;

  SVN_ERR(svn_test__sandbox_create(&sbox, "read_write_tree_conflicts", opts, pool));
  parent_abspath = svn_dirent_join(sbox.wc_abspath, "A", pool);
  SVN_ERR(svn_wc__db_op_add_directory(sbox.wc_ctx->db, parent_abspath, NULL,
                                      pool));
  child1_abspath = svn_dirent_join(parent_abspath, "foo", pool);
  child2_abspath = svn_dirent_join(parent_abspath, "bar", pool);

  conflict1 = tree_conflict_create(child1_abspath, svn_node_file,
                                   svn_wc_operation_merge,
                                   svn_wc_conflict_action_delete,
                                   svn_wc_conflict_reason_edited,
                                   "dummy://localhost", "path/to/foo",
                                   51, svn_node_file,
                                   "dummy://localhost", "path/to/foo",
                                   52, svn_node_none,
                                   pool);

  conflict2 = tree_conflict_create(child2_abspath, svn_node_dir,
                                   svn_wc_operation_merge,
                                   svn_wc_conflict_action_replace,
                                   svn_wc_conflict_reason_edited,
                                   "dummy://localhost", "path/to/bar",
                                   51, svn_node_dir,
                                   "dummy://localhost", "path/to/bar",
                                   52, svn_node_file,
                                   pool);

  /* Write */
  SVN_ERR(svn_wc__add_tree_conflict(sbox.wc_ctx, /*child1_abspath,*/
                                    conflict1, pool));
  SVN_ERR(svn_wc__add_tree_conflict(sbox.wc_ctx, /*child2_abspath,*/
                                    conflict2, pool));

  /* Query (conflict1 through WC-DB API, conflict2 through WC API) */
  {
    svn_boolean_t text_c, prop_c, tree_c;

    SVN_ERR(svn_wc__internal_conflicted_p(&text_c, &prop_c, &tree_c,
                                          sbox.wc_ctx->db, child1_abspath, pool));
    SVN_TEST_ASSERT(tree_c);
    SVN_TEST_ASSERT(! text_c && ! prop_c);

    SVN_ERR(svn_wc_conflicted_p3(&text_c, &prop_c, &tree_c,
                                 sbox.wc_ctx, child2_abspath, pool));
    SVN_TEST_ASSERT(tree_c);
    SVN_TEST_ASSERT(! text_c && ! prop_c);
  }

  /* Read conflicts back */
  {
    const svn_wc_conflict_description2_t *read_conflict;

    SVN_ERR(svn_wc__get_tree_conflict(&read_conflict, sbox.wc_ctx,
                                      child1_abspath, pool, pool));
    SVN_ERR(compare_conflict(conflict1, read_conflict));

    SVN_ERR(svn_wc__get_tree_conflict(&read_conflict, sbox.wc_ctx,
                                      child2_abspath, pool, pool));
    SVN_ERR(compare_conflict(conflict2, read_conflict));
  }

  /* Read many */
  {
    const apr_array_header_t *victims;

    SVN_ERR(svn_wc__db_read_conflict_victims(&victims,
                                             sbox.wc_ctx->db, parent_abspath,
                                             pool, pool));
    SVN_TEST_ASSERT(victims->nelts == 2);
  }

  /* ### TODO: to test...
   * svn_wc__db_read_conflicts
   * svn_wc__node_get_conflict_info
   * svn_wc__del_tree_conflict
   */

  return SVN_NO_ERROR;
}

static svn_error_t *
test_serialize_prop_conflict(const svn_test_opts_t *opts,
                             apr_pool_t *pool)
{
  svn_test__sandbox_t sbox;
  svn_skel_t *conflict_skel;
  svn_boolean_t complete;

  SVN_ERR(svn_test__sandbox_create(&sbox, "test_serialize_prop_conflict", opts, pool));

  conflict_skel = svn_wc__conflict_skel_create(pool);

  SVN_TEST_ASSERT(conflict_skel != NULL);
  SVN_TEST_ASSERT(svn_skel__list_length(conflict_skel) == 2);

  SVN_ERR(svn_wc__conflict_skel_is_complete(&complete, conflict_skel));
  SVN_TEST_ASSERT(!complete); /* Nothing set */

  {
    apr_hash_t *mine = apr_hash_make(pool);
    apr_hash_t *their_old = apr_hash_make(pool);
    apr_hash_t *theirs = apr_hash_make(pool);
    apr_hash_t *conflicts = apr_hash_make(pool);
    const char *marker_abspath;

    apr_hash_set(mine, "prop", APR_HASH_KEY_STRING,
                 svn_string_create("Mine", pool));

    apr_hash_set(their_old, "prop", APR_HASH_KEY_STRING,
                 svn_string_create("Their-Old", pool));

    apr_hash_set(theirs, "prop", APR_HASH_KEY_STRING,
                 svn_string_create("Theirs", pool));

    apr_hash_set(conflicts, "prop", APR_HASH_KEY_STRING, "");

    SVN_ERR(svn_io_open_unique_file3(NULL, &marker_abspath, sbox.wc_abspath,
                                     svn_io_file_del_on_pool_cleanup, pool,
                                     pool));

    SVN_ERR(svn_wc__conflict_skel_add_prop_conflict(conflict_skel,
                                                    sbox.wc_ctx->db,
                                                    sbox.wc_abspath,
                                                    marker_abspath,
                                                    mine, their_old,
                                                    theirs, conflicts,
                                                    pool, pool));
  }

  SVN_ERR(svn_wc__conflict_skel_is_complete(&complete, conflict_skel));
  SVN_TEST_ASSERT(!complete); /* Misses operation */

  SVN_ERR(svn_wc__conflict_skel_set_op_update(
                        conflict_skel,
                        svn_wc_conflict_version_create2("http://my-repos/svn",
                                                        "uuid", "trunk", 12,
                                                        svn_node_dir, pool),
                        pool, pool));

  SVN_ERR(svn_wc__conflict_skel_is_complete(&complete, conflict_skel));
  SVN_TEST_ASSERT(complete); /* Everything available */

  {
    apr_hash_t *mine;
    apr_hash_t *their_old;
    apr_hash_t *theirs;
    apr_hash_t *conflicts;
    const char *marker_abspath;
    svn_string_t *v;

    SVN_ERR(svn_wc__conflict_read_prop_conflict(&marker_abspath,
                                                &mine,
                                                &their_old,
                                                &theirs,
                                                &conflicts,
                                                sbox.wc_ctx->db,
                                                sbox.wc_abspath,
                                                conflict_skel,
                                                pool, pool));

    SVN_TEST_ASSERT(svn_dirent_is_ancestor(sbox.wc_abspath, marker_abspath));

    v = apr_hash_get(mine, "prop", APR_HASH_KEY_STRING);
    SVN_TEST_STRING_ASSERT(v->data, "Mine");

    v = apr_hash_get(their_old, "prop", APR_HASH_KEY_STRING);
    SVN_TEST_STRING_ASSERT(v->data, "Their-Old");

    v = apr_hash_get(theirs, "prop", APR_HASH_KEY_STRING);
    SVN_TEST_STRING_ASSERT(v->data, "Theirs");

    SVN_TEST_ASSERT(apr_hash_count(conflicts) == 1);
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_serialize_text_conflict(const svn_test_opts_t *opts,
                             apr_pool_t *pool)
{
  svn_test__sandbox_t sbox;
  svn_skel_t *conflict_skel;
  svn_boolean_t complete;

  SVN_ERR(svn_test__sandbox_create(&sbox, "test_serialize_text_conflict", opts, pool));

  conflict_skel = svn_wc__conflict_skel_create(pool);

  SVN_ERR(svn_wc__conflict_skel_add_text_conflict(
                  conflict_skel,
                  sbox.wc_ctx->db, sbox.wc_abspath,
                  svn_dirent_join(sbox.wc_abspath, "mine", pool),
                  svn_dirent_join(sbox.wc_abspath, "old-theirs", pool),
                  svn_dirent_join(sbox.wc_abspath, "theirs", pool),
                  pool, pool));

  SVN_ERR(svn_wc__conflict_skel_set_op_merge(
                        conflict_skel,
                        svn_wc_conflict_version_create2("http://my-repos/svn",
                                                        "uuid", "trunk", 12,
                                                        svn_node_dir, pool),
                        svn_wc_conflict_version_create2("http://my-repos/svn",
                                                        "uuid", "branch/my", 8,
                                                        svn_node_dir, pool),
                        pool, pool));

  SVN_ERR(svn_wc__conflict_skel_is_complete(&complete, conflict_skel));
  SVN_TEST_ASSERT(complete); /* Everything available */

  {
    const char *mine_abspath;
    const char *old_their_abspath;
    const char *their_abspath;

    SVN_ERR(svn_wc__conflict_read_text_conflict(&mine_abspath,
                                                &old_their_abspath,
                                                &their_abspath,
                                                sbox.wc_ctx->db,
                                                sbox.wc_abspath,
                                                conflict_skel,
                                                pool, pool));

    SVN_TEST_STRING_ASSERT(
        svn_dirent_skip_ancestor(sbox.wc_abspath, mine_abspath),
        "mine");

    SVN_TEST_STRING_ASSERT(
        svn_dirent_skip_ancestor(sbox.wc_abspath, old_their_abspath),
        "old-theirs");

    SVN_TEST_STRING_ASSERT(
        svn_dirent_skip_ancestor(sbox.wc_abspath, their_abspath),
        "theirs");
  }

  {
    svn_wc_operation_t operation;
    svn_boolean_t text_conflicted;
    const apr_array_header_t *locs;
    SVN_ERR(svn_wc__conflict_read_info(&operation, &locs,
                                       &text_conflicted, NULL, NULL,
                                       sbox.wc_ctx->db, sbox.wc_abspath,
                                       conflict_skel, pool, pool));

    SVN_TEST_ASSERT(text_conflicted);
    SVN_TEST_ASSERT(operation == svn_wc_operation_merge);

    SVN_TEST_ASSERT(locs != NULL && locs->nelts == 2);
    SVN_TEST_ASSERT(APR_ARRAY_IDX(locs, 0, svn_wc_conflict_version_t*) != NULL);
    SVN_TEST_ASSERT(APR_ARRAY_IDX(locs, 1, svn_wc_conflict_version_t*) != NULL);
  }

  {
    const apr_array_header_t *markers;
    const char *old_their_abspath;
    const char *their_abspath;
    const char *mine_abspath;

    SVN_ERR(svn_wc__conflict_read_markers(&markers,
                                          sbox.wc_ctx->db, sbox.wc_abspath,
                                          conflict_skel, pool, pool));

    SVN_TEST_ASSERT(markers != NULL);
    SVN_TEST_ASSERT(markers->nelts == 3);

    old_their_abspath = APR_ARRAY_IDX(markers, 0, const char *);
    mine_abspath = APR_ARRAY_IDX(markers, 1, const char *);
    their_abspath = APR_ARRAY_IDX(markers, 2, const char *);

    SVN_TEST_STRING_ASSERT(
        svn_dirent_skip_ancestor(sbox.wc_abspath, mine_abspath),
        "mine");

    SVN_TEST_STRING_ASSERT(
        svn_dirent_skip_ancestor(sbox.wc_abspath, old_their_abspath),
        "old-theirs");

    SVN_TEST_STRING_ASSERT(
        svn_dirent_skip_ancestor(sbox.wc_abspath, their_abspath),
        "theirs");
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_serialize_tree_conflict(const svn_test_opts_t *opts,
                             apr_pool_t *pool)
{
  svn_test__sandbox_t sbox;
  svn_skel_t *conflict_skel;
  svn_boolean_t complete;

  SVN_ERR(svn_test__sandbox_create(&sbox, "test_serialize_tree_conflict", opts, pool));

  conflict_skel = svn_wc__conflict_skel_create(pool);

  SVN_ERR(svn_wc__conflict_skel_add_tree_conflict(
                              conflict_skel,
                              sbox.wc_ctx->db, sbox.wc_abspath,
                              svn_wc_conflict_reason_moved_away,
                              svn_wc_conflict_action_delete,
                              pool, pool));

  SVN_ERR(svn_wc__conflict_skel_set_op_switch(
                        conflict_skel,
                        svn_wc_conflict_version_create2("http://my-repos/svn",
                                                        "uuid", "trunk", 12,
                                                        svn_node_dir, pool),
                        pool, pool));

  SVN_ERR(svn_wc__conflict_skel_is_complete(&complete, conflict_skel));
  SVN_TEST_ASSERT(complete); /* Everything available */

  {
    svn_wc_conflict_reason_t local_change;
    svn_wc_conflict_action_t incoming_change;

    SVN_ERR(svn_wc__conflict_read_tree_conflict(&local_change,
                                                &incoming_change,
                                                sbox.wc_ctx->db,
                                                sbox.wc_abspath,
                                                conflict_skel,
                                                pool, pool));

    SVN_TEST_ASSERT(local_change == svn_wc_conflict_reason_moved_away);
    SVN_TEST_ASSERT(incoming_change == svn_wc_conflict_action_delete);
  }

  return SVN_NO_ERROR;
}

/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_deserialize_tree_conflict,
                   "deserialize tree conflict"),
    SVN_TEST_PASS2(test_serialize_tree_conflict_data,
                   "serialize tree conflict data"),
    SVN_TEST_OPTS_PASS(test_read_write_tree_conflicts,
                       "read and write tree conflict data"),
    SVN_TEST_OPTS_PASS(test_serialize_prop_conflict,
                       "read and write a property conflict"),
    SVN_TEST_OPTS_PASS(test_serialize_text_conflict,
                       "read and write a text conflict"),
    SVN_TEST_OPTS_PASS(test_serialize_tree_conflict,
                       "read and write a tree conflict"),
    SVN_TEST_NULL
  };

