/*
 * Regression tests for the conflict resolver in the libsvn_client library.
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



#define SVN_DEPRECATED

#include "svn_client.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"

#include "../svn_test.h"
#include "../svn_test_fs.h"
#include "../libsvn_wc/utils.h"

struct status_baton
{
  svn_client_status_t *status;
  apr_pool_t *result_pool;
};

/* Implements svn_client_status_func_t */
static svn_error_t *
status_func(void *baton, const char *path,
            const svn_client_status_t *status,
            apr_pool_t *scratch_pool)
{
  struct status_baton *sb = baton;

  sb->status = svn_client_status_dup(status, sb->result_pool);

  return SVN_NO_ERROR;
}

/* 
 * The following tests verify resolution of "incoming file add vs.
 * local file obstruction upon merge" tree conflicts.
 */

/* Some paths we'll care about. */
static const char *trunk_path = "A";
static const char *branch_path = "A_branch";
static const char *new_file_name = "newfile.txt";

/* File property content. */
static const char *propval_trunk = "This is a property on the trunk.";
static const char *propval_branch = "This is a property on the branch.";

/* A helper function which prepares a working copy for the tests below. */
static svn_error_t *
create_wc_with_add_vs_add_upon_merge_conflict(svn_test__sandbox_t *b)
{
  static const char *new_file_path;
  svn_client_ctx_t *ctx;
  static const char *trunk_url;
  svn_opt_revision_t opt_rev;
  svn_client_status_t *status;
  struct status_baton sb;
  svn_client_conflict_t *conflict;
  svn_boolean_t tree_conflicted;

  SVN_ERR(sbox_add_and_commit_greek_tree(b));

  /* Create a branch of node "A". */
  SVN_ERR(sbox_wc_copy(b, trunk_path, branch_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  /* Add new files on trunk and the branch which occupy the same path
   * but have different content and properties. */
  new_file_path = svn_relpath_join(trunk_path, new_file_name, b->pool);
  SVN_ERR(sbox_file_write(b, new_file_path,
                          "This is a new file on the trunk\n"));
  SVN_ERR(sbox_wc_add(b, new_file_path));
  SVN_ERR(sbox_wc_propset(b, "prop", propval_trunk, new_file_path));
  SVN_ERR(sbox_wc_commit(b, ""));
  new_file_path = svn_relpath_join(branch_path, new_file_name, b->pool);
  SVN_ERR(sbox_file_write(b, new_file_path,
                          /* NB: Ensure that the file content's length
                           * differs between the two branches! Tests are
                           * run with sleep for timestamps disabled. */
                          "This is a new file on the branch\n"));
  SVN_ERR(sbox_wc_add(b, new_file_path));
  SVN_ERR(sbox_wc_propset(b, "prop", propval_branch, new_file_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  /* Run a merge from the trunk to the branch. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));

  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  trunk_url = apr_pstrcat(b->pool, b->repos_url, "/", trunk_path, SVN_VA_NULL);

  opt_rev.kind = svn_opt_revision_head;
  opt_rev.value.number = SVN_INVALID_REVNUM;
  /* This should raise an "incoming add vs local obstruction" tree conflict. */
  SVN_ERR(svn_client_merge_peg5(trunk_url, NULL, &opt_rev,
                                sbox_wc_path(b, branch_path),
                                svn_depth_infinity,
                                FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                                NULL, ctx, b->pool));

  /* Ensure that the file has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_file_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));

  /* Ensure that the expected tree conflict is present. */
  SVN_ERR(svn_client_conflict_get_conflicted(NULL, NULL, &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(tree_conflicted);
  SVN_TEST_ASSERT(svn_client_conflict_get_local_change(conflict) ==
                  svn_wc_conflict_reason_obstructed);
  SVN_TEST_ASSERT(svn_client_conflict_get_incoming_change(conflict) ==
                  svn_wc_conflict_action_add);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_option_merge_incoming_added_file_ignore(const svn_test_opts_t *opts,
                                              apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  const char *new_file_path;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  const svn_string_t *propval;

  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(svn_test__sandbox_create(b, "incoming_added_file_ignore",
                                   opts, pool));

  SVN_ERR(create_wc_with_add_vs_add_upon_merge_conflict(b));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_file_path = svn_relpath_join(branch_path, new_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict,
            svn_client_conflict_option_merge_incoming_added_file_ignore,
            b->pool));

  /* Ensure that the file has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_file_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));

  /* The file should not be in conflict. */
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  /* Verify the merged property value. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_file_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_branch);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_option_merge_incoming_added_file_text_merge(const svn_test_opts_t *opts,
                                                 apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  const char *new_file_path;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  const svn_string_t *propval;

  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(svn_test__sandbox_create(b, "incoming_added_file_text_merge",
                                   opts, pool));

  SVN_ERR(create_wc_with_add_vs_add_upon_merge_conflict(b));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_file_path = svn_relpath_join(branch_path, new_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict,
            svn_client_conflict_option_merge_incoming_added_file_text_merge,
            b->pool));

  /* Ensure that the file has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_file_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_conflicted);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_conflicted);
  /* ### Shouldn't there be a property conflict? The trunk wins. */
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));

  /* We should have a text conflict instead of a tree conflict. */
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  /* Verify the merged property value. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_file_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_trunk);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_option_merge_incoming_added_file_replace(const svn_test_opts_t *opts,
                                              apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  const char *new_file_path;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  const svn_string_t *propval;

  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(svn_test__sandbox_create(b, "incoming_added_file_replace",
                                   opts, pool));

  SVN_ERR(create_wc_with_add_vs_add_upon_merge_conflict(b));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_file_path = svn_relpath_join(branch_path, new_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict,
            svn_client_conflict_option_merge_incoming_added_file_replace,
            b->pool));

  /* Ensure that the file has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_file_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_replaced);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));

  /* The file should not be in conflict. */
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  /* Verify the merged property value. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_file_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_trunk);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_option_merge_incoming_added_file_replace_and_merge(
  const svn_test_opts_t *opts,
   apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  const char *new_file_path;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  const svn_string_t *propval;

  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(svn_test__sandbox_create(b, "incoming_added_file_replace_and_merge",
                                   opts, pool));

  SVN_ERR(create_wc_with_add_vs_add_upon_merge_conflict(b));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_file_path = svn_relpath_join(branch_path, new_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(
    svn_client_conflict_tree_resolve_by_id(
      conflict,
      svn_client_conflict_option_merge_incoming_added_file_replace_and_merge,
      b->pool));

  /* Ensure that the file has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_file_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_conflicted);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_conflicted);
  /* ### Shouldn't there be a property conflict? The trunk wins. */
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));

  /* We should have a text conflict instead of a tree conflict. */
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  /* Verify the merged property value. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_file_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_trunk);

  return SVN_NO_ERROR;
}

/* ========================================================================== */


static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(test_option_merge_incoming_added_file_ignore,
                       "test incoming add file ignore"),
    SVN_TEST_OPTS_PASS(test_option_merge_incoming_added_file_text_merge,
                       "test incoming add file text merge"),
    SVN_TEST_OPTS_PASS(test_option_merge_incoming_added_file_replace,
                       "test incoming add file replace"),
    SVN_TEST_OPTS_PASS(test_option_merge_incoming_added_file_replace_and_merge,
                       "test incoming add file replace and merge"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
