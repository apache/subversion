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

#include "svn_props.h"
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

/* Assert that two integers are equal. Return an error if not. */
#define ASSERT_INT_EQ(a, b) \
  do { \
    if ((a) != (b)) \
      return svn_error_createf(SVN_ERR_TEST_FAILED, NULL, \
                               "failed: ASSERT_INT_EQ(" #a ", " #b ") " \
                               "-> (%d == %d)", a, b); \
  } while (0)

/* Assert that two strings are equal or both null. Return an error if not. */
#define ASSERT_STR_EQ(a, b) \
  SVN_TEST_STRING_ASSERT(a, b)

/* Assert that two version_t's are equal or both null. Return an error if not. */
static svn_error_t *
compare_version(const svn_wc_conflict_version_t *actual,
                const svn_wc_conflict_version_t *expected)
{
  if (actual == NULL && expected == NULL)
    return SVN_NO_ERROR;

  SVN_TEST_ASSERT(actual && expected);
  ASSERT_STR_EQ(actual->repos_url,      expected->repos_url);
  ASSERT_INT_EQ((int)actual->peg_rev,   (int)expected->peg_rev);
  ASSERT_STR_EQ(actual->path_in_repos,  expected->path_in_repos);
  ASSERT_INT_EQ(actual->node_kind,      expected->node_kind);
  return SVN_NO_ERROR;
}

/* Assert that two conflict descriptions contain exactly the same data
 * (including names of temporary files), or are both NULL.  Return an
 * error if not. */
static svn_error_t *
compare_conflict(const svn_wc_conflict_description2_t *actual,
                 const svn_wc_conflict_description2_t *expected)
{
  if (actual == NULL && expected == NULL)
    return SVN_NO_ERROR;

  SVN_TEST_ASSERT(actual && expected);

  ASSERT_INT_EQ(actual->kind,           expected->kind);
  ASSERT_STR_EQ(actual->local_abspath,  expected->local_abspath);
  ASSERT_INT_EQ(actual->node_kind,      expected->node_kind);
  ASSERT_STR_EQ(actual->property_name,  expected->property_name);
  ASSERT_INT_EQ(actual->is_binary,      expected->is_binary);
  ASSERT_STR_EQ(actual->mime_type,      expected->mime_type);
  ASSERT_INT_EQ(actual->action,         expected->action);
  ASSERT_INT_EQ(actual->reason,         expected->reason);
  ASSERT_STR_EQ(actual->base_abspath,   expected->base_abspath);
  ASSERT_STR_EQ(actual->their_abspath,  expected->their_abspath);
  ASSERT_STR_EQ(actual->my_abspath,     expected->my_abspath);
  ASSERT_STR_EQ(actual->merged_file,    expected->merged_file);
  ASSERT_INT_EQ(actual->operation,      expected->operation);
  SVN_ERR(compare_version(actual->src_left_version,
                          expected->src_left_version));
  SVN_ERR(compare_version(actual->src_right_version,
                          expected->src_right_version));
  return SVN_NO_ERROR;
}

/* Assert that a file contains the expected data.  Return an
 * error if not. */
static svn_error_t *
compare_file_content(const char *file_abspath,
                     const char *expected_val,
                     apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *actual_val;

  SVN_ERR(svn_stringbuf_from_file2(&actual_val, file_abspath, scratch_pool));
  ASSERT_STR_EQ(actual_val->data, expected_val);
  return SVN_NO_ERROR;
}

/* Assert that ACTUAL and EXPECTED both represent the same property
 * conflict, or are both NULL.  Return an error if not.
 *
 * Compare the property values found in files named by
 * ACTUAL->base_abspath, ACTUAL->my_abspath, ACTUAL->merged_file
 * with EXPECTED_BASE_VAL, EXPECTED_MY_VAL, EXPECTED_THEIR_VAL
 * respectively, ignoring the corresponding fields in EXPECTED. */
static svn_error_t *
compare_prop_conflict(const svn_wc_conflict_description2_t *actual,
                      const svn_wc_conflict_description2_t *expected,
                      const char *expected_base_val,
                      const char *expected_my_val,
                      const char *expected_their_val,
                      apr_pool_t *scratch_pool)
{
  if (actual == NULL && expected == NULL)
    return SVN_NO_ERROR;

  SVN_TEST_ASSERT(actual && expected);
  ASSERT_INT_EQ(actual->kind,   svn_wc_conflict_kind_property);
  ASSERT_INT_EQ(expected->kind, svn_wc_conflict_kind_property);

  ASSERT_STR_EQ(actual->local_abspath,  expected->local_abspath);
  ASSERT_INT_EQ(actual->node_kind,      expected->node_kind);
  ASSERT_STR_EQ(actual->property_name,  expected->property_name);
  ASSERT_INT_EQ(actual->action,         expected->action);
  ASSERT_INT_EQ(actual->reason,         expected->reason);
  ASSERT_INT_EQ(actual->operation,      expected->operation);
  SVN_ERR(compare_version(actual->src_left_version,
                          expected->src_left_version));
  SVN_ERR(compare_version(actual->src_right_version,
                          expected->src_right_version));

  SVN_ERR(compare_file_content(actual->base_abspath, expected_base_val,
                               scratch_pool));
  SVN_ERR(compare_file_content(actual->my_abspath, expected_my_val,
                               scratch_pool));
  /* Historical wart: for a prop conflict, 'theirs' is in the 'merged_file'
   * field, and the conflict artifact file is in the 'theirs_abspath' field. */
  SVN_ERR(compare_file_content(actual->merged_file, expected_their_val,
                               scratch_pool));
  /*ASSERT_STR_EQ(actual->theirs_abspath, conflict_artifact_file));*/

  /* These are 'undefined' for a prop conflict */
  /*ASSERT_INT_EQ(actual->is_binary, expected->is_binary);*/
  /*ASSERT_STR_EQ(actual->mime_type, expected->mime_type);*/

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

  left = svn_wc_conflict_version_create2(left_repo, NULL, left_path,
                                         left_revnum, left_kind, result_pool);
  right = svn_wc_conflict_version_create2(right_repo, NULL, right_path,
                                          right_revnum, right_kind,
                                          result_pool);
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
      (conflict->action != exp_conflict->action) ||
      (conflict->reason != exp_conflict->reason) ||
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
  child1_abspath = svn_dirent_join(parent_abspath, "foo", pool);
  child2_abspath = svn_dirent_join(parent_abspath, "bar", pool);
  SVN_ERR(sbox_wc_mkdir(&sbox, "A"));
  SVN_ERR(sbox_wc_mkdir(&sbox, "A/bar"));
  SVN_ERR(sbox_file_write(&sbox, "A/foo", ""));
  SVN_ERR(sbox_wc_add(&sbox, "A/foo"));

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
    SVN_ERR(compare_conflict(read_conflict, conflict1));

    SVN_ERR(svn_wc__get_tree_conflict(&read_conflict, sbox.wc_ctx,
                                      child2_abspath, pool, pool));
    SVN_ERR(compare_conflict(read_conflict, conflict2));
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
                        NULL /* wc_only */,
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
                              sbox.wc_ctx->db, sbox_wc_path(&sbox, "A/B"),
                              svn_wc_conflict_reason_moved_away,
                              svn_wc_conflict_action_delete,
                              sbox_wc_path(&sbox, "A/B"),
                              pool, pool));

  SVN_ERR(svn_wc__conflict_skel_set_op_switch(
                        conflict_skel,
                        svn_wc_conflict_version_create2("http://my-repos/svn",
                                                        "uuid", "trunk", 12,
                                                        svn_node_dir, pool),
                        NULL /* wc_only */,
                        pool, pool));

  SVN_ERR(svn_wc__conflict_skel_is_complete(&complete, conflict_skel));
  SVN_TEST_ASSERT(complete); /* Everything available */

  {
    svn_wc_conflict_reason_t reason;
    svn_wc_conflict_action_t action;
    const char *moved_away_op_root_abspath;

    SVN_ERR(svn_wc__conflict_read_tree_conflict(&reason,
                                                &action,
                                                &moved_away_op_root_abspath,
                                                sbox.wc_ctx->db,
                                                sbox.wc_abspath,
                                                conflict_skel,
                                                pool, pool));

    SVN_TEST_ASSERT(reason == svn_wc_conflict_reason_moved_away);
    SVN_TEST_ASSERT(action == svn_wc_conflict_action_delete);
    SVN_TEST_STRING_ASSERT(moved_away_op_root_abspath,
                           sbox_wc_path(&sbox, "A/B"));
  }

  return SVN_NO_ERROR;
}

/* A conflict resolver callback baton for test_prop_conflicts(). */
typedef struct test_prop_conflict_baton_t
{
  /* Sets of properties. */
  apr_hash_t *mine;
  apr_hash_t *their_old;
  apr_hash_t *theirs;
  /* The set of prop names in conflict. */
  apr_hash_t *conflicts;

  /* We use all the fields of DESC except the base/theirs/mine/merged paths. */
  svn_wc_conflict_description2_t *desc;

  int conflicts_seen;
} test_prop_conflict_baton_t;

/* Set *CONFLICT_SKEL_P to a new property conflict skel reflecting the
 * conflict details given in B. */
static svn_error_t *
create_prop_conflict_skel(svn_skel_t **conflict_skel_p,
                          svn_wc_context_t *wc_ctx,
                          const test_prop_conflict_baton_t *b,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_skel_t *conflict_skel = svn_wc__conflict_skel_create(result_pool);
  const char *marker_abspath;
  svn_boolean_t complete;

  SVN_ERR(svn_io_write_unique(&marker_abspath,
                              b->desc->local_abspath,
                              "conflict-artifact-file-content\n", 6,
                              svn_io_file_del_none, scratch_pool));

  SVN_ERR(svn_wc__conflict_skel_add_prop_conflict(conflict_skel,
                                                  wc_ctx->db,
                                                  b->desc->local_abspath,
                                                  marker_abspath,
                                                  b->mine, b->their_old,
                                                  b->theirs, b->conflicts,
                                                  result_pool, scratch_pool));

  switch (b->desc->operation)
    {
    case svn_wc_operation_update:
      SVN_ERR(svn_wc__conflict_skel_set_op_update(
                conflict_skel,
                b->desc->src_left_version, b->desc->src_right_version,
                result_pool, scratch_pool));
      break;
    case svn_wc_operation_switch:
      SVN_ERR(svn_wc__conflict_skel_set_op_switch(
                conflict_skel,
                b->desc->src_left_version, b->desc->src_right_version,
                result_pool, scratch_pool));
      break;
    case svn_wc_operation_merge:
      SVN_ERR(svn_wc__conflict_skel_set_op_merge(
                conflict_skel,
                b->desc->src_left_version, b->desc->src_right_version,
                result_pool, scratch_pool));
      break;
    default:
      SVN_ERR_MALFUNCTION();
    }

  SVN_ERR(svn_wc__conflict_skel_is_complete(&complete, conflict_skel));
  SVN_TEST_ASSERT(complete);
  *conflict_skel_p = conflict_skel;
  return SVN_NO_ERROR;
}

/* A conflict resolver callback for test_prop_conflicts(), that checks
 * that the conflict described to it matches the one described in BATON,
 * and also counts the number of times it is called. */
static svn_error_t *
prop_conflict_cb(svn_wc_conflict_result_t **result_p,
                 const svn_wc_conflict_description2_t *desc,
                 void *baton,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  test_prop_conflict_baton_t *b = baton;

  SVN_ERR(compare_prop_conflict(
            desc, b->desc,
            svn_prop_get_value(b->their_old, desc->property_name),
            svn_prop_get_value(b->mine, desc->property_name),
            svn_prop_get_value(b->theirs, desc->property_name),
            scratch_pool));
  b->conflicts_seen++;

  *result_p = svn_wc_create_conflict_result(svn_wc_conflict_choose_postpone,
                                            NULL /*merged_file*/, result_pool);
  return SVN_NO_ERROR;
}

/* Test for correct retrieval of property conflict descriptions from
 * the WC DB.
 *
 * Presently it tests just one prop conflict, and only during the
 * 'resolve' operation.  We should also test during the 'update'/
 * 'switch'/'merge' operations.
 */
static svn_error_t *
test_prop_conflicts(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_test__sandbox_t sbox;
  svn_skel_t *conflict_skel;
  svn_error_t *err;
  const char *lock_abspath;
  test_prop_conflict_baton_t *b = apr_pcalloc(pool, sizeof(*b));
  svn_wc_conflict_description2_t *desc = apr_pcalloc(pool, sizeof(*desc));

  SVN_ERR(svn_test__sandbox_create(&sbox, "test_prop_conflicts", opts, pool));

  /* Describe a property conflict */
  b->mine = apr_hash_make(pool);
  b->their_old = apr_hash_make(pool);
  b->theirs = apr_hash_make(pool);
  b->conflicts = apr_hash_make(pool);
  svn_hash_sets(b->mine, "prop", svn_string_create("Mine", pool));
  svn_hash_sets(b->their_old, "prop", svn_string_create("Their-Old", pool));
  svn_hash_sets(b->theirs, "prop", svn_string_create("Theirs", pool));
  svn_hash_sets(b->conflicts, "prop", "");

  b->desc = desc;
  desc->local_abspath = sbox.wc_abspath;
  desc->kind = svn_wc_conflict_kind_property;
  desc->node_kind = svn_node_dir;
  desc->operation = svn_wc_operation_update;
  desc->action = svn_wc_conflict_action_edit;
  desc->reason = svn_wc_conflict_reason_edited;
  desc->mime_type = NULL;
  desc->is_binary = FALSE;
  desc->property_name = "prop";
  desc->src_left_version
    = svn_wc_conflict_version_create2(sbox.repos_url, "uuid",
                                      "trunk", 12, svn_node_dir, pool);
  desc->src_right_version = NULL;  /* WC only */

  b->conflicts_seen = 0;

  /* Record a conflict */
  {
    apr_pool_t *subpool = svn_pool_create(pool);
    SVN_ERR(create_prop_conflict_skel(&conflict_skel, sbox.wc_ctx, b,
                                      pool, subpool));
    svn_pool_clear(subpool);
    SVN_ERR(svn_wc__db_op_mark_conflict(sbox.wc_ctx->db,
                                        sbox.wc_abspath,
                                        conflict_skel, NULL, subpool));
    svn_pool_destroy(subpool);
  }

  /* Test the API for resolving the conflict: check that correct details
   * of the conflict are returned. */
  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, sbox.wc_ctx,
                                                 sbox.wc_abspath, pool, pool));
  err = svn_wc__resolve_conflicts(sbox.wc_ctx, sbox.wc_abspath,
                                  svn_depth_empty,
                                  FALSE /* resolve_text */,
                                  "" /* resolve_prop (ALL props) */,
                                  FALSE /* resolve_tree */,
                                  svn_wc_conflict_choose_unspecified,
                                  prop_conflict_cb, b,
                                  NULL, NULL, /* cancellation */
                                  NULL, NULL, /* notification */
                                  pool);

  SVN_ERR(svn_error_compose_create(err,
                                   svn_wc__release_write_lock(sbox.wc_ctx,
                                                              lock_abspath,
                                                              pool)));

  ASSERT_INT_EQ(b->conflicts_seen, 1);
  return SVN_NO_ERROR;
}

static svn_error_t *
test_prop_conflict_resolving(const svn_test_opts_t *opts,
                             apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  svn_skel_t *conflict;
  const char *A_abspath;
  const char *marker_abspath;
  apr_hash_t *conflicted_props;
  apr_hash_t *props;
  const char *value;

  SVN_ERR(svn_test__sandbox_create(&b, "test_prop_resolving", opts, pool));
  SVN_ERR(sbox_wc_mkdir(&b, "A"));

  SVN_ERR(sbox_wc_propset(&b, "prop-1", "r1", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-2", "r1", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-3", "r1", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-4", "r1", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-5", "r1", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-6", "r1", "A"));

  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_propset(&b, "prop-1", "r2", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-2", "r2", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-3", "r2", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-4", NULL, "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-5", NULL, "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-7", "r2", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-8", "r2", "A"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_propset(&b, "prop-1", "mod", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-2", "mod", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-3", "mod", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-4", "mod", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-5", "mod", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-6", "mod", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-7", "mod", "A"));
  SVN_ERR(sbox_wc_propset(&b, "prop-8", "mod", "A"));

  SVN_ERR(sbox_wc_update(&b, "", 1));

  A_abspath = sbox_wc_path(&b, "A");
  SVN_ERR(svn_wc__db_read_conflict(&conflict, NULL, NULL,
                                   b.wc_ctx->db, A_abspath,
                                   pool, pool));

  /* We have tree conflicts... */
  SVN_TEST_ASSERT(conflict != NULL);

  SVN_ERR(svn_wc__conflict_read_prop_conflict(&marker_abspath,
                                              NULL, NULL, NULL,
                                              &conflicted_props,
                                              b.wc_ctx->db, A_abspath,
                                              conflict,
                                              pool, pool));

  SVN_TEST_ASSERT(conflicted_props != NULL);
  /* All properties but r6 are conflicted */
  SVN_TEST_ASSERT(apr_hash_count(conflicted_props) == 7);
  SVN_TEST_ASSERT(! svn_hash_gets(conflicted_props, "prop-6"));

  /* Let's resolve a few conflicts */
  SVN_ERR(sbox_wc_resolve_prop(&b, "A", "prop-1",
                               svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(sbox_wc_resolve_prop(&b, "A", "prop-2",
                               svn_wc_conflict_choose_theirs_conflict));
  SVN_ERR(sbox_wc_resolve_prop(&b, "A", "prop-3",
                               svn_wc_conflict_choose_merged));

  SVN_ERR(svn_wc__db_read_conflict(&conflict, NULL, NULL,
                                   b.wc_ctx->db, A_abspath,
                                   pool, pool));

  /* We have tree conflicts... */
  SVN_TEST_ASSERT(conflict != NULL);

  SVN_ERR(svn_wc__conflict_read_prop_conflict(&marker_abspath,
                                              NULL, NULL, NULL,
                                              &conflicted_props,
                                              b.wc_ctx->db, A_abspath,
                                              conflict,
                                              pool, pool));

  SVN_TEST_ASSERT(conflicted_props != NULL);
  SVN_TEST_ASSERT(apr_hash_count(conflicted_props) == 4);

  SVN_ERR(svn_wc__db_read_props(&props, b.wc_ctx->db, A_abspath,
                                pool, pool));

  value = svn_prop_get_value(props, "prop-1");
  SVN_TEST_STRING_ASSERT(value, "mod");
  value = svn_prop_get_value(props, "prop-2");
  SVN_TEST_STRING_ASSERT(value, "r1");
  value = svn_prop_get_value(props, "prop-3");
  SVN_TEST_STRING_ASSERT(value, "mod");
  
  return SVN_NO_ERROR;
}

static svn_error_t *
test_binary_file_conflict(const svn_test_opts_t *opts,
                          apr_pool_t *pool)
{
  svn_test__sandbox_t sbox;
  const apr_array_header_t *conflicts;
  svn_wc_conflict_description2_t *desc;

  SVN_ERR(svn_test__sandbox_create(&sbox, "test_binary_file_conflict", opts, pool));

  /* Create and add a binary file. */
  SVN_ERR(sbox_file_write(&sbox, "binary-file", "\xff\xff"));
  SVN_ERR(sbox_wc_add(&sbox, "binary-file"));
  SVN_ERR(sbox_wc_propset(&sbox, SVN_PROP_MIME_TYPE,
                          "application/octet-stream", "binary-file"));
  SVN_ERR(sbox_wc_commit(&sbox, "binary-file")); /* r1 */

  /* Make a change to the binary file. */
  SVN_ERR(sbox_file_write(&sbox, "binary-file", "\xfc\xfc\xfc\xfc\xfc\xfc"));
  SVN_ERR(sbox_wc_commit(&sbox, "binary-file")); /* r2 */

  /* Update back to r1, make a conflicting change to binary file. */
  SVN_ERR(sbox_wc_update(&sbox, "binary-file", 1));
  SVN_ERR(sbox_file_write(&sbox, "binary-file", "\xfd\xfd\xfd\xfd"));

  /* Update to HEAD and ensure the conflict is marked as binary. */
  SVN_ERR(sbox_wc_update(&sbox, "binary-file", 2));
  SVN_ERR(svn_wc__read_conflicts(&conflicts, NULL, sbox.wc_ctx->db,
                                 sbox_wc_path(&sbox, "binary-file"),
                                 FALSE /* create_tempfiles */,
                                 FALSE /* only_tree_conflict */,
                                 pool, pool));
  SVN_TEST_ASSERT(conflicts->nelts == 1);
  desc = APR_ARRAY_IDX(conflicts, 0, svn_wc_conflict_description2_t *);
  SVN_TEST_ASSERT(desc->is_binary);

  return SVN_NO_ERROR;
}


/* The test table.  */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
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
    SVN_TEST_OPTS_PASS(test_prop_conflicts,
                       "test prop conflicts"),
    SVN_TEST_OPTS_PASS(test_prop_conflict_resolving,
                       "test property conflict resolving"),
    SVN_TEST_OPTS_PASS(test_binary_file_conflict,
                       "test binary file conflict"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
