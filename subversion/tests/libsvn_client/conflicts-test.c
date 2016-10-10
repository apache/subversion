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

struct info_baton
{
  svn_client_info2_t *info;
  apr_pool_t *result_pool;
};

/* Implements svn_client_info_receiver2_t */
static svn_error_t *
info_func(void *baton, const char *abspath_or_url,
          const svn_client_info2_t *info,
          apr_pool_t *scratch_pool)
{
  struct info_baton *ib = baton;

  ib->info = svn_client_info2_dup(info, ib->result_pool);

  return SVN_NO_ERROR;
}

/* A helper function which checks offered conflict resolution options. */
static svn_error_t *
assert_conflict_options(const apr_array_header_t *actual,
                        const svn_client_conflict_option_id_t *expected,
                        apr_pool_t *pool)
{
  svn_stringbuf_t *actual_str = svn_stringbuf_create_empty(pool);
  svn_stringbuf_t *expected_str = svn_stringbuf_create_empty(pool);
  int i;

  for (i = 0; i < actual->nelts; i++)
    {
      svn_client_conflict_option_t *opt;
      svn_client_conflict_option_id_t id;

      opt = APR_ARRAY_IDX(actual, i, svn_client_conflict_option_t *);

      if (i > 0)
        svn_stringbuf_appendcstr(actual_str, ", ");

      id = svn_client_conflict_option_get_id(opt);
      svn_stringbuf_appendcstr(actual_str, apr_itoa(pool, id));
    }

  for (i = 0; expected[i] >= 0; i++)
    {
      if (i > 0)
        svn_stringbuf_appendcstr(expected_str, ", ");

      svn_stringbuf_appendcstr(expected_str, apr_itoa(pool, expected[i]));
    }

  SVN_TEST_STRING_ASSERT(actual_str->data, expected_str->data);

  return SVN_NO_ERROR;
}

static svn_error_t *
assert_tree_conflict_options(svn_client_conflict_t *conflict,
                             svn_client_ctx_t *ctx,
                             const svn_client_conflict_option_id_t *expected,
                             apr_pool_t *pool)
{
  apr_array_header_t *actual;

  SVN_ERR(svn_client_conflict_tree_get_resolution_options(&actual, conflict,
                                                          ctx, pool, pool));
  SVN_ERR(assert_conflict_options(actual, expected, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
assert_prop_conflict_options(svn_client_conflict_t *conflict,
                             svn_client_ctx_t *ctx,
                             const svn_client_conflict_option_id_t *expected,
                             apr_pool_t *pool)
{
  apr_array_header_t *actual;

  SVN_ERR(svn_client_conflict_prop_get_resolution_options(&actual, conflict,
                                                          ctx, pool, pool));
  SVN_ERR(assert_conflict_options(actual, expected, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
assert_text_conflict_options(svn_client_conflict_t *conflict,
                             svn_client_ctx_t *ctx,
                             const svn_client_conflict_option_id_t *expected,
                             apr_pool_t *pool)
{
  apr_array_header_t *actual;

  SVN_ERR(svn_client_conflict_text_get_resolution_options(&actual, conflict,
                                                          ctx, pool, pool));
  SVN_ERR(assert_conflict_options(actual, expected, pool));

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
static const char *new_file_name_branch = "newfile-on-branch.txt";
static const char *deleted_file_name = "mu";
static const char *deleted_dir_name = "B";
static const char *deleted_dir_child = "lambda";
static const char *new_dir_name = "newdir";

/* File property content. */
static const char *propval_trunk = "This is a property on the trunk.";
static const char *propval_branch = "This is a property on the branch.";

/* File content. */
static const char *modified_file_on_branch_content =
                        "This is a modified file on the branch\n";
static const char *added_file_on_branch_content =
                        "This is a file added on the branch\n";
static const char *modified_file_in_working_copy_content =
                        "This is a modified file in the working copy\n";

/* A helper function which prepares a working copy for the tests below. */
static svn_error_t *
create_wc_with_file_add_vs_file_add_merge_conflict(svn_test__sandbox_t *b,
                                                   svn_boolean_t do_switch)
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

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));

  opt_rev.kind = svn_opt_revision_head;
  opt_rev.value.number = SVN_INVALID_REVNUM;
  trunk_url = apr_pstrcat(b->pool, b->repos_url, "/", trunk_path, SVN_VA_NULL);

  if (do_switch)
    {
      svn_revnum_t result_rev;

      /* This should raise an "incoming add vs local add" conflict. */
      SVN_ERR(svn_client_switch3(&result_rev, sbox_wc_path(b, branch_path),
                                 trunk_url, &opt_rev, &opt_rev,
                                 svn_depth_infinity, TRUE, TRUE, FALSE, FALSE,
                                 ctx, b->pool));

      opt_rev.kind = svn_opt_revision_head;
    }
  else
    {
      SVN_ERR(sbox_wc_commit(b, ""));
      SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));

      /* Run a merge from the trunk to the branch.
       * This should raise an "incoming add vs local obstruction" conflict. */
      SVN_ERR(svn_client_merge_peg5(trunk_url, NULL, &opt_rev,
                                    sbox_wc_path(b, branch_path),
                                    svn_depth_infinity,
                                    FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                                    NULL, ctx, b->pool));

      opt_rev.kind = svn_opt_revision_working;
    }

  /* Ensure that the file has the expected status. */
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_file_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(status->conflicted);
  if (do_switch)
    {
      SVN_TEST_ASSERT(status->node_status == svn_wc_status_replaced);
      SVN_TEST_ASSERT(status->text_status == svn_wc_status_modified);
      SVN_TEST_ASSERT(status->prop_status == svn_wc_status_modified);
    }
  else
    {
      SVN_TEST_ASSERT(status->node_status == svn_wc_status_normal);
      SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
      SVN_TEST_ASSERT(status->prop_status == svn_wc_status_normal);
    }
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
  if (do_switch)
    SVN_TEST_ASSERT(svn_client_conflict_get_local_change(conflict) ==
                    svn_wc_conflict_reason_added);
  else
    SVN_TEST_ASSERT(svn_client_conflict_get_local_change(conflict) ==
                    svn_wc_conflict_reason_obstructed);
  SVN_TEST_ASSERT(svn_client_conflict_get_incoming_change(conflict) ==
                  svn_wc_conflict_action_add);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_merge_incoming_added_file_ignore(const svn_test_opts_t *opts,
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

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_added_file_ignore",
                                   opts, pool));

  SVN_ERR(create_wc_with_file_add_vs_file_add_merge_conflict(b, FALSE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_file_path = svn_relpath_join(branch_path, new_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_add_ignore, ctx, b->pool));

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
test_merge_incoming_added_file_text_merge(const svn_test_opts_t *opts,
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

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_added_file_text_merge",
                                   opts, pool));

  SVN_ERR(create_wc_with_file_add_vs_file_add_merge_conflict(b, FALSE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_file_path = svn_relpath_join(branch_path, new_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict,
            svn_client_conflict_option_incoming_added_file_text_merge,
            ctx, b->pool));

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
test_merge_incoming_added_file_replace(const svn_test_opts_t *opts,
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

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_added_file_replace",
                                   opts, pool));

  SVN_ERR(create_wc_with_file_add_vs_file_add_merge_conflict(b, FALSE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_file_path = svn_relpath_join(branch_path, new_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict,
            svn_client_conflict_option_incoming_added_file_replace,
            ctx, b->pool));

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
test_merge_incoming_added_file_replace_and_merge(const svn_test_opts_t *opts,
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

  SVN_ERR(svn_test__sandbox_create(
            b, "merge_incoming_added_file_replace_and_merge", opts, pool));

  SVN_ERR(create_wc_with_file_add_vs_file_add_merge_conflict(b, FALSE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_file_path = svn_relpath_join(branch_path, new_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(
    svn_client_conflict_tree_resolve_by_id(
      conflict,
      svn_client_conflict_option_incoming_added_file_replace_and_merge,
      ctx, b->pool));

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

/* A helper function which prepares a working copy for the tests below. */
static svn_error_t *
create_wc_with_file_add_vs_file_add_update_conflict(svn_test__sandbox_t *b)
{
  static const char *new_file_path;
  svn_client_ctx_t *ctx;
  svn_opt_revision_t opt_rev;
  svn_client_status_t *status;
  struct status_baton sb;
  svn_client_conflict_t *conflict;
  svn_boolean_t tree_conflicted;

  SVN_ERR(sbox_add_and_commit_greek_tree(b));

  /* Add and commit a new file. */
  new_file_path = svn_relpath_join(trunk_path, new_file_name, b->pool);
  SVN_ERR(sbox_file_write(b, new_file_path,
                          "This is a new file on the trunk\n"));
  SVN_ERR(sbox_wc_add(b, new_file_path));
  SVN_ERR(sbox_wc_propset(b, "prop", propval_trunk, new_file_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  /* Back-date the WC. */
  SVN_ERR(sbox_wc_update(b, "", 1));

  /* Add a file which occupies the same path but has different content
   * and properties. */
  SVN_ERR(sbox_file_write(b, new_file_path,
                          /* NB: Ensure that the file content's length differs!
                           * Tests are run without sleep for timestamps. */
                          "This is a new file on the branch\n"));
  SVN_ERR(sbox_wc_add(b, new_file_path));
  SVN_ERR(sbox_wc_propset(b, "prop", propval_branch, new_file_path));

  /* Update the WC.
   * This should raise an "incoming add vs local add" tree conflict because
   * the sbox test code runs updates with 'adds_as_modifications == FALSE'. */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));

  /* Ensure that the file has the expected status. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  opt_rev.kind = svn_opt_revision_head;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_file_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_replaced);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_modified);
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
                  svn_wc_conflict_reason_added);
  SVN_TEST_ASSERT(svn_client_conflict_get_incoming_change(conflict) ==
                  svn_wc_conflict_action_add);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_update_incoming_added_file_ignore(const svn_test_opts_t *opts,
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

  SVN_ERR(svn_test__sandbox_create(b, "update_incoming_added_file_ignore",
                                   opts, pool));

  SVN_ERR(create_wc_with_file_add_vs_file_add_update_conflict(b));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_file_path = svn_relpath_join(trunk_path, new_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_add_ignore, ctx,
            b->pool));

  /* Ensure that the file has the expected status. */
  opt_rev.kind = svn_opt_revision_head;
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
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_modified);
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
test_update_incoming_added_file_replace(const svn_test_opts_t *opts,
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

  SVN_ERR(svn_test__sandbox_create(b, "update_incoming_added_file_replace",
                                   opts, pool));

  SVN_ERR(create_wc_with_file_add_vs_file_add_update_conflict(b));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_file_path = svn_relpath_join(trunk_path, new_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_added_file_replace,
            ctx, b->pool));

  /* Ensure that the file has the expected status. */
  opt_rev.kind = svn_opt_revision_head;
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
  SVN_TEST_STRING_ASSERT(propval->data, propval_trunk);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_switch_incoming_added_file_ignore(const svn_test_opts_t *opts,
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

  SVN_ERR(svn_test__sandbox_create(b, "switch_incoming_added_file_ignore",
                                   opts, pool));

  SVN_ERR(create_wc_with_file_add_vs_file_add_merge_conflict(b, TRUE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_file_path = svn_relpath_join(branch_path, new_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_add_ignore, ctx,
            b->pool));

  /* Ensure that the file has the expected status. */
  opt_rev.kind = svn_opt_revision_head;
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
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_modified);
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

/* 
 * The following tests verify resolution of "incoming dir add vs.
 * local dir obstruction upon merge" tree conflicts.
 */

/* A helper function which prepares a working copy for the tests below. */
static svn_error_t *
create_wc_with_dir_add_vs_dir_add_merge_conflict(
  svn_test__sandbox_t *b,
  svn_boolean_t file_change_on_trunk,
  svn_boolean_t with_move,
  svn_boolean_t file_change_on_branch)
{
  static const char *new_dir_path;
  static const char *new_file_path;
  svn_client_ctx_t *ctx;
  static const char *trunk_url;
  svn_opt_revision_t opt_rev;
  svn_client_status_t *status;
  struct status_baton sb;
  svn_client_conflict_t *conflict;
  svn_boolean_t tree_conflicted;
  const char *move_src_path;

  SVN_ERR(sbox_add_and_commit_greek_tree(b));

  /* Create a branch of node "A". */
  SVN_ERR(sbox_wc_copy(b, trunk_path, branch_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  /* Add new directories on trunk and the branch which occupy the same path
   * but have different content and properties. */
  if (with_move)
    {
      /* History starts at ^/newdir.orig, outside of ^/A (the "trunk").
       * Then a move to ^/A/newdir causes a collision. */
      move_src_path = apr_pstrcat(b->pool, new_dir_name, ".orig", SVN_VA_NULL);
      new_dir_path = move_src_path;
    }
  else
    {
      new_dir_path = svn_relpath_join(trunk_path, new_dir_name, b->pool);
      move_src_path = NULL;
    }

  SVN_ERR(sbox_wc_mkdir(b, new_dir_path));
  new_file_path = svn_relpath_join(new_dir_path, new_file_name, b->pool);
  SVN_ERR(sbox_file_write(b, new_file_path,
                          "This is a new file on the trunk\n"));
  SVN_ERR(sbox_wc_add(b, new_file_path));
  SVN_ERR(sbox_wc_propset(b, "prop", propval_trunk, new_file_path));
  SVN_ERR(sbox_wc_commit(b, ""));
  if (file_change_on_trunk)
    {
      SVN_ERR(sbox_file_write(b, new_file_path,
                              "This is a change to the new file"
                              "on the trunk\n"));
      SVN_ERR(sbox_wc_commit(b, ""));
    }
  if (with_move)
    {
      /* Now move the new directory to the colliding path. */
      new_dir_path = svn_relpath_join(trunk_path, new_dir_name, b->pool);
      SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
      sbox_wc_move(b, move_src_path, new_dir_path);
      SVN_ERR(sbox_wc_commit(b, ""));
    }
  new_dir_path = svn_relpath_join(branch_path, new_dir_name, b->pool);
  SVN_ERR(sbox_wc_mkdir(b, new_dir_path));
  new_file_path = svn_relpath_join(branch_path,
                                   svn_relpath_join(new_dir_name,
                                                    new_file_name, b->pool),
                                   b->pool);
  SVN_ERR(sbox_file_write(b, new_file_path,
                          /* NB: Ensure that the file content's length
                           * differs between the two branches! Tests are
                           * run with sleep for timestamps disabled. */
                          "This is a new file on the branch\n"));
  SVN_ERR(sbox_wc_add(b, new_file_path));
  SVN_ERR(sbox_wc_propset(b, "prop", propval_branch, new_file_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  if (file_change_on_branch)
    {
      SVN_ERR(sbox_file_write(b, new_file_path,
                              "This is a change to the new file "
                              "on the branch\n"));
      SVN_ERR(sbox_wc_commit(b, ""));
    }

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

  /* Ensure that the directory has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_dir_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
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
test_merge_incoming_added_dir_ignore(const svn_test_opts_t *opts,
                                     apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  const char *new_dir_path;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_added_dir_ignore",
                                   opts, pool));

  SVN_ERR(create_wc_with_dir_add_vs_dir_add_merge_conflict(b, FALSE, FALSE,
                                                           FALSE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_dir_path = svn_relpath_join(branch_path, new_dir_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_add_ignore, ctx,
            b->pool));

  /* Ensure that the directory has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_dir_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));

  /* The directory should not be in conflict. */
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  return SVN_NO_ERROR;
}

/* This test currently fails to meet expectations. Our merge code doesn't
 * support a merge of files which were added in the same revision as their
 * parent directory and were not modified since. */
static svn_error_t *
test_merge_incoming_added_dir_merge(const svn_test_opts_t *opts,
                                    apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  const char *new_dir_path;
  const char *new_file_path;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  const svn_string_t *propval;
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_added_dir_merge",
                                   opts, pool));

  SVN_ERR(create_wc_with_dir_add_vs_dir_add_merge_conflict(b, FALSE, FALSE,
                                                           FALSE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_dir_path = svn_relpath_join(branch_path, new_dir_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict,
            svn_client_conflict_option_incoming_added_dir_merge, ctx,
            b->pool));

  /* Ensure that the directory has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_dir_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));

  /* The directory should not be in conflict. */
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  /* XFAIL: Currently, no text conflict is raised since the file is not merged.
   * We should have a text conflict in the file. */
  new_file_path = svn_relpath_join(branch_path,
                                   svn_relpath_join(new_dir_name,
                                                    new_file_name, b->pool),
                                   b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  /* Verify the file's merged property value. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_file_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_trunk);

  return SVN_NO_ERROR;
}

/* Same test as above, but with an additional file change on the trunk
 * which makes resolution work as expected. */
static svn_error_t *
test_merge_incoming_added_dir_merge2(const svn_test_opts_t *opts,
                                     apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  const char *new_dir_path;
  const char *new_file_path;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  const svn_string_t *propval;
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_added_dir_merge2",
                                   opts, pool));

  SVN_ERR(create_wc_with_dir_add_vs_dir_add_merge_conflict(b, TRUE, FALSE,
                                                           FALSE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_dir_path = svn_relpath_join(branch_path, new_dir_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict,
            svn_client_conflict_option_incoming_added_dir_merge,
            ctx, b->pool));

  /* Ensure that the directory has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_dir_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));

  /* The directory should not be in conflict. */
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  /* We should have a text conflict in the file. */
  new_file_path = svn_relpath_join(branch_path,
                                   svn_relpath_join(new_dir_name,
                                                    new_file_name, b->pool),
                                   b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  /* Verify the file's merged property value. */
  /* ### Shouldn't there be a property conflict? The branch wins. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_file_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_branch);

  return SVN_NO_ERROR;
}

/* Same test as above, but with an additional move operation on the trunk. */
static svn_error_t *
test_merge_incoming_added_dir_merge3(const svn_test_opts_t *opts,
                                     apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  const char *new_dir_path;
  const char *new_file_path;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  const svn_string_t *propval;
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_added_dir_merge3",
                                   opts, pool));

  SVN_ERR(create_wc_with_dir_add_vs_dir_add_merge_conflict(b, TRUE, TRUE,
                                                           FALSE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_dir_path = svn_relpath_join(branch_path, new_dir_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict,
            svn_client_conflict_option_incoming_added_dir_merge,
            ctx, b->pool));

  /* Ensure that the directory has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_dir_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));

  /* The directory should not be in conflict. */
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  /* We should have a text conflict in the file. */
  new_file_path = svn_relpath_join(branch_path,
                                   svn_relpath_join(new_dir_name,
                                                    new_file_name, b->pool),
                                   b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  /* Verify the file's merged property value. */
  /* ### Shouldn't there be a property conflict? The branch wins. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_file_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_branch);

  /* XFAIL: Currently, no subtree mergeinfo is created.
   *
   * Verify the directory's subtree mergeinfo. It should mention both
   * location segments of ^/A/newdir's history, shouldn't it? Like this:
   *
   *   /A/newdir:2-6
   *   /newdir.orig:4
   *
   * ### /newdir.orig was created in r3 and moved to /A/newdir in r5.
   * ### Should the second line say "/newdir.orig:3-4" instead? */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_dir_path),
                           "svn:mergeinfo", b->pool, b->pool));
  SVN_TEST_ASSERT(propval != NULL);
  SVN_TEST_STRING_ASSERT(propval->data,
                         apr_psprintf(b->pool, "/%s:2-6\n/%s:4",
                                      svn_relpath_join(trunk_path,
                                                       new_dir_name,
                                                       b->pool),
                                      apr_pstrcat(b->pool,
                                                  new_dir_name, ".orig",
                                                  SVN_VA_NULL)));
  return SVN_NO_ERROR;
}

static svn_error_t *
test_merge_incoming_added_dir_replace(const svn_test_opts_t *opts,
                                      apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  const char *new_dir_path;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_added_dir_replace",
                                   opts, pool));

  SVN_ERR(create_wc_with_dir_add_vs_dir_add_merge_conflict(b, FALSE, FALSE,
                                                           FALSE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_dir_path = svn_relpath_join(branch_path, new_dir_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict,
            svn_client_conflict_option_incoming_added_dir_replace,
            ctx, b->pool));

  /* Ensure that the directory has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_dir_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_replaced);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));

  /* The directory should not be in conflict. */
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  return SVN_NO_ERROR;
}

/* This test currently fails to meet expectations. Our merge code doesn't
 * support a merge of files which were added in the same revision as their
 * parent directory and were not modified since. */
static svn_error_t *
test_merge_incoming_added_dir_replace_and_merge(const svn_test_opts_t *opts,
                                                apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  const char *new_dir_path;
  const char *new_file_path;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(svn_test__sandbox_create(b,
                                   "merge_incoming_added_dir_replace_and_merge",
                                   opts, pool));

  SVN_ERR(create_wc_with_dir_add_vs_dir_add_merge_conflict(b, FALSE, FALSE,
                                                           FALSE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_dir_path = svn_relpath_join(branch_path, new_dir_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict,
            svn_client_conflict_option_incoming_added_dir_replace_and_merge,
            ctx, b->pool));

  /* Ensure that the directory has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_dir_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_replaced);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));

  /* The directory should not be in conflict. */
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  /* We should have a text conflict in the file. */
  new_file_path = svn_relpath_join(branch_path,
                                   svn_relpath_join(new_dir_name,
                                                    new_file_name, b->pool),
                                   b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  return SVN_NO_ERROR;
}

/* Same test as above, but with an additional file change on the branch
 * which makes resolution work as expected. */
static svn_error_t *
test_merge_incoming_added_dir_replace_and_merge2(const svn_test_opts_t *opts,
                                                 apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  const char *new_dir_path;
  const char *new_file_path;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(svn_test__sandbox_create(b,
                                   "merge_incoming_added_dir_replace_and_merge",
                                   opts, pool));

  SVN_ERR(create_wc_with_dir_add_vs_dir_add_merge_conflict(b, FALSE, FALSE,
                                                           TRUE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_dir_path = svn_relpath_join(branch_path, new_dir_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict,
            svn_client_conflict_option_incoming_added_dir_replace_and_merge,
            ctx, b->pool));

  /* Ensure that the directory has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_dir_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_replaced);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));

  /* The directory should not be in conflict. */
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  /* We should have a text conflict in the file. */
  new_file_path = svn_relpath_join(branch_path,
                                   svn_relpath_join(new_dir_name,
                                                    new_file_name, b->pool),
                                   b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  return SVN_NO_ERROR;
}

/* A helper function which prepares a working copy for the tests below. */
static svn_error_t *
create_wc_with_incoming_delete_merge_conflict(svn_test__sandbox_t *b,
                                              svn_boolean_t move,
                                              svn_boolean_t do_switch)
{
  svn_client_ctx_t *ctx;
  static const char *trunk_url;
  svn_opt_revision_t opt_rev;
  const char *deleted_path;

  SVN_ERR(sbox_add_and_commit_greek_tree(b));

  /* Create a branch of node "A". */
  SVN_ERR(sbox_wc_copy(b, trunk_path, branch_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  if (move)
    {
      const char *move_target_path;

      /* Move a file on the trunk. */
      deleted_path = svn_relpath_join(trunk_path, deleted_file_name, b->pool);
      move_target_path = svn_relpath_join(trunk_path, new_file_name, b->pool);
      SVN_ERR(sbox_wc_move(b, deleted_path, move_target_path));
      SVN_ERR(sbox_wc_commit(b, ""));
    }
  else
    {
      /* Delete a file on the trunk. */
      deleted_path = svn_relpath_join(trunk_path, deleted_file_name, b->pool);
      SVN_ERR(sbox_wc_delete(b, deleted_path));
      SVN_ERR(sbox_wc_commit(b, ""));
    }

  /* Modify a file on the branch. */
  deleted_path = svn_relpath_join(branch_path, deleted_file_name, b->pool);
  SVN_ERR(sbox_file_write(b, deleted_path, modified_file_on_branch_content));

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  opt_rev.kind = svn_opt_revision_head;
  opt_rev.value.number = SVN_INVALID_REVNUM;
  trunk_url = apr_pstrcat(b->pool, b->repos_url, "/", trunk_path,
                          SVN_VA_NULL);
  if (do_switch)
    {
      /* Switch the branch working copy to trunk. */
      svn_revnum_t result_rev;

      /* This should raise an "incoming delete vs local edit" tree conflict. */
      SVN_ERR(svn_client_switch3(&result_rev, sbox_wc_path(b, branch_path),
                                 trunk_url, &opt_rev, &opt_rev,
                                 svn_depth_infinity,
                                 TRUE, FALSE, FALSE, FALSE, ctx, b->pool));
    }
  else
    {
      /* Commit modifcation and run a merge from the trunk to the branch. */
      SVN_ERR(sbox_wc_commit(b, ""));
      SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
      /* This should raise an "incoming delete vs local edit" tree conflict. */
      SVN_ERR(svn_client_merge_peg5(trunk_url, NULL, &opt_rev,
                                    sbox_wc_path(b, branch_path),
                                    svn_depth_infinity,
                                    FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                                    NULL, ctx, b->pool));
    }

  return SVN_NO_ERROR;
}

/* Test 'incoming delete ignore' option. */
static svn_error_t *
test_merge_incoming_delete_ignore(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  const char *deleted_path;
  svn_client_conflict_t *conflict;
  svn_boolean_t tree_conflicted;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_delete_ignore",
                                   opts, pool));

  SVN_ERR(create_wc_with_incoming_delete_merge_conflict(b, FALSE, FALSE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  deleted_path = svn_relpath_join(branch_path, deleted_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, deleted_path),
                                  ctx, b->pool, b->pool));

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_delete_ignore,
      svn_client_conflict_option_incoming_delete_accept,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_delete_ignore,
      svn_client_conflict_option_incoming_delete_accept,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_delete_ignore,
            ctx, b->pool));

  /* Ensure that the deleted file has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, deleted_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);
  
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, deleted_path),
                                  ctx, b->pool, b->pool));

  /* The file should not be in conflict. */
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  return SVN_NO_ERROR;
}

/* Test 'incoming delete accept' option. */
static svn_error_t *
test_merge_incoming_delete_accept(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  const char *deleted_path;
  svn_client_conflict_t *conflict;
  svn_boolean_t tree_conflicted;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_delete_accept",
                                   opts, pool));

  SVN_ERR(create_wc_with_incoming_delete_merge_conflict(b, FALSE, FALSE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  deleted_path = svn_relpath_join(branch_path, deleted_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, deleted_path),
                                  ctx, b->pool, b->pool));

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_delete_ignore,
      svn_client_conflict_option_incoming_delete_accept,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_delete_ignore,
      svn_client_conflict_option_incoming_delete_accept,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_delete_accept,
            ctx, b->pool));

  /* Ensure that the deleted file has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, deleted_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_deleted);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, deleted_path),
                                  ctx, b->pool, b->pool));

  /* The file should not be in conflict. */
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  return SVN_NO_ERROR;
}

/* Test 'incoming move file text merge' option for merge. */
static svn_error_t *
test_merge_incoming_move_file_text_merge(const svn_test_opts_t *opts,
                                         apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  const char *deleted_path;
  const char *new_file_path;
  svn_client_conflict_t *conflict;
  svn_boolean_t tree_conflicted;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  svn_stringbuf_t *buf;
  svn_node_kind_t kind;

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_move_file_text_merge",
                                   opts, pool));

  SVN_ERR(create_wc_with_incoming_delete_merge_conflict(b, TRUE, FALSE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  deleted_path = svn_relpath_join(branch_path, deleted_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, deleted_path),
                                  ctx, b->pool, b->pool));

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_delete_ignore,
      svn_client_conflict_option_incoming_delete_accept,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_move_file_text_merge,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_move_file_text_merge,
            ctx, b->pool));

  /* Ensure that the deleted file has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, deleted_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_deleted);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  new_file_path = svn_relpath_join(branch_path, new_file_name, b->pool);
  SVN_TEST_STRING_ASSERT(status->moved_to_abspath,
                         sbox_wc_path(b, new_file_path));

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, deleted_path),
                                  ctx, b->pool, b->pool));

  /* The file should not be in conflict. */
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  /* Ensure that the moved file has the expected status. */
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
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_added);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_STRING_ASSERT(status->moved_from_abspath,
                         sbox_wc_path(b, deleted_path));
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* Ensure that the original file was removed. */
  SVN_ERR(svn_io_check_path(sbox_wc_path(b, deleted_path), &kind, b->pool));
  SVN_TEST_ASSERT(kind == svn_node_none);

  /* Ensure that the moved file has the expected content. */
  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, new_file_path),
                                   b->pool));
  SVN_TEST_STRING_ASSERT(buf->data, modified_file_on_branch_content);

  return SVN_NO_ERROR;
}

/* A helper function which prepares a working copy for the tests below. */
static svn_error_t *
create_wc_with_incoming_delete_update_conflict(svn_test__sandbox_t *b,
                                               svn_boolean_t move)
{
  const char *deleted_path;

  SVN_ERR(sbox_add_and_commit_greek_tree(b));

  if (move)
    {
      const char *move_target_path;

      /* Move a file on the trunk. */
      deleted_path = svn_relpath_join(trunk_path, deleted_file_name, b->pool);
      move_target_path = svn_relpath_join(trunk_path, new_file_name, b->pool);
      SVN_ERR(sbox_wc_move(b, deleted_path, move_target_path));
      SVN_ERR(sbox_wc_commit(b, ""));
    }
  else
    {
      /* Delete a file on the trunk. */
      deleted_path = svn_relpath_join(trunk_path, deleted_file_name, b->pool);
      SVN_ERR(sbox_wc_delete(b, deleted_path));
      SVN_ERR(sbox_wc_commit(b, ""));
    }

  /* Update into the past. */
  SVN_ERR(sbox_wc_update(b, "", 1));

  /* Modify a file in the working copy. */
  deleted_path = svn_relpath_join(trunk_path, deleted_file_name, b->pool);
  SVN_ERR(sbox_file_write(b, deleted_path, modified_file_on_branch_content));

  /* Update to HEAD.
   * This should raise an "incoming delete vs local edit" tree conflict. */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));

  return SVN_NO_ERROR;
}

/* Test 'incoming move file text merge' option for update. */
static svn_error_t *
test_update_incoming_move_file_text_merge(const svn_test_opts_t *opts,
                                          apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  const char *deleted_path;
  const char *new_file_path;
  svn_client_conflict_t *conflict;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  svn_node_kind_t node_kind;
  svn_stringbuf_t *buf;

  SVN_ERR(svn_test__sandbox_create(b, "update_incoming_move_file_text_merge",
                                   opts, pool));

  SVN_ERR(create_wc_with_incoming_delete_update_conflict(b, TRUE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  deleted_path = svn_relpath_join(trunk_path, deleted_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, deleted_path),
                                  ctx, b->pool, b->pool));

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_delete_ignore,
      svn_client_conflict_option_incoming_delete_accept,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_move_file_text_merge,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_move_file_text_merge,
            ctx, b->pool));

  /* Ensure that the deleted file is gone. */
  SVN_ERR(svn_io_check_path(sbox_wc_path(b, deleted_path), &node_kind,
                            b->pool));
  SVN_TEST_ASSERT(node_kind == svn_node_none);

  /* Ensure that the moved file has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  new_file_path = svn_relpath_join(trunk_path, new_file_name, b->pool);
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_file_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* Ensure that the moved file has the expected content. */
  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, new_file_path),
                                   b->pool));
  SVN_TEST_STRING_ASSERT(buf->data, modified_file_on_branch_content);

  return SVN_NO_ERROR;
}

/* Test 'incoming move file text merge' option for switch. */
static svn_error_t *
test_switch_incoming_move_file_text_merge(const svn_test_opts_t *opts,
                                          apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  const char *deleted_path;
  const char *new_file_path;
  svn_client_conflict_t *conflict;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  svn_node_kind_t node_kind;
  svn_stringbuf_t *buf;

  SVN_ERR(svn_test__sandbox_create(b, "switch_incoming_move_file_text_merge",
                                   opts, pool));

  SVN_ERR(create_wc_with_incoming_delete_merge_conflict(b, TRUE, TRUE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  deleted_path = svn_relpath_join(branch_path, deleted_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, deleted_path),
                                  ctx, b->pool, b->pool));

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_delete_ignore,
      svn_client_conflict_option_incoming_delete_accept,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_move_file_text_merge,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_move_file_text_merge,
            ctx, b->pool));

  /* Ensure that the deleted file is gone. */
  SVN_ERR(svn_io_check_path(sbox_wc_path(b, deleted_path), &node_kind,
                            b->pool));
  SVN_TEST_ASSERT(node_kind == svn_node_none);

  /* Ensure that the moved file has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  new_file_path = svn_relpath_join(branch_path, new_file_name, b->pool);
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_file_path),
                             &opt_rev, svn_depth_unknown, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* Ensure that the moved file has the expected content. */
  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, new_file_path),
                                   b->pool));
  SVN_TEST_STRING_ASSERT(buf->data, modified_file_on_branch_content);

  return SVN_NO_ERROR;
}

/* A helper function which prepares a working copy for the tests below. */
static svn_error_t *
create_wc_with_incoming_delete_dir_conflict(svn_test__sandbox_t *b,
                                            svn_boolean_t move,
                                            svn_boolean_t do_switch,
                                            svn_boolean_t local_edit,
                                            svn_boolean_t local_add)
{
  svn_client_ctx_t *ctx;
  static const char *trunk_url;
  svn_opt_revision_t opt_rev;
  const char *deleted_path;
  const char *deleted_child_path;
  const char *new_file_path;

  SVN_ERR(sbox_add_and_commit_greek_tree(b));

  /* Create a branch of node "A". */
  SVN_ERR(sbox_wc_copy(b, trunk_path, branch_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  /* On the trunk, add a file inside the dir about to be moved/deleted. */
  new_file_path = svn_relpath_join(trunk_path,
                                   svn_relpath_join(deleted_dir_name,
                                                    new_file_name, b->pool),
                                   b->pool);
  SVN_ERR(sbox_file_write(b, new_file_path,
                          "This is a new file on the trunk\n"));
  SVN_ERR(sbox_wc_add(b, new_file_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  if (move)
    {
      const char *move_target_path;

      /* Move a directory on the trunk. */
      deleted_path = svn_relpath_join(trunk_path, deleted_dir_name, b->pool);
      move_target_path = svn_relpath_join(trunk_path, new_dir_name, b->pool);
      SVN_ERR(sbox_wc_move(b, deleted_path, move_target_path));
      SVN_ERR(sbox_wc_commit(b, ""));
    }
  else
    {
      /* Delete a directory on the trunk. */
      deleted_path = svn_relpath_join(trunk_path, deleted_dir_name, b->pool);
      SVN_ERR(sbox_wc_delete(b, deleted_path));
      SVN_ERR(sbox_wc_commit(b, ""));
    }

  if (local_add)
    {
      const char *new_child_path;
      
      new_child_path = svn_relpath_join(branch_path,
                                        svn_relpath_join(deleted_dir_name,
                                                         new_file_name_branch,
                                                         b->pool),
                                        b->pool);
      /* Add new file on the branch. */
      SVN_ERR(sbox_file_write(b, new_child_path, added_file_on_branch_content));
      SVN_ERR(sbox_wc_add(b, new_child_path));
    }
  else
    {
      /* Modify a file on the branch. */
      deleted_child_path = svn_relpath_join(branch_path,
                                            svn_relpath_join(deleted_dir_name,
                                                             deleted_dir_child,
                                                             b->pool),
                                            b->pool);
      SVN_ERR(sbox_file_write(b, deleted_child_path,
                              modified_file_on_branch_content));
    }

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  opt_rev.kind = svn_opt_revision_head;
  opt_rev.value.number = SVN_INVALID_REVNUM;
  trunk_url = apr_pstrcat(b->pool, b->repos_url, "/", trunk_path,
                          SVN_VA_NULL);
  if (do_switch)
    {
      /* Switch the branch working copy to trunk. */
      svn_revnum_t result_rev;

      /* This should raise an "incoming delete vs local edit" tree conflict. */
      SVN_ERR(svn_client_switch3(&result_rev, sbox_wc_path(b, branch_path),
                                 trunk_url, &opt_rev, &opt_rev,
                                 svn_depth_infinity,
                                 TRUE, FALSE, FALSE, FALSE, ctx, b->pool));
    }
  else
    {
      /* Commit modification and run a merge from the trunk to the branch. */
      SVN_ERR(sbox_wc_commit(b, ""));
      SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));

      if (local_edit)
        {
          /* Modify the file in the working copy. */
          SVN_ERR(sbox_file_write(b, deleted_child_path,
                                  modified_file_in_working_copy_content));
        }

      /* This should raise an "incoming delete vs local edit" tree conflict. */
      SVN_ERR(svn_client_merge_peg5(trunk_url, NULL, &opt_rev,
                                    sbox_wc_path(b, branch_path),
                                    svn_depth_infinity,
                                    FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                                    NULL, ctx, b->pool));
    }

  return SVN_NO_ERROR;
}

/* Test 'incoming move dir merge' resolution option. */
static svn_error_t *
test_merge_incoming_move_dir(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  const char *deleted_path;
  const char *moved_to_path;
  const char *child_path;
  svn_client_conflict_t *conflict;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_stringbuf_t *buf;
  svn_opt_revision_t opt_rev;

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_move_dir", opts, pool));

  SVN_ERR(create_wc_with_incoming_delete_dir_conflict(b, TRUE, FALSE, FALSE,
                                                      FALSE));

  deleted_path = svn_relpath_join(branch_path, deleted_dir_name, b->pool);
  moved_to_path = svn_relpath_join(branch_path, new_dir_name, b->pool);

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, deleted_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_move_dir_merge,
            ctx, b->pool));

  /* Ensure that the moved-away directory has the expected status. */
  sb.result_pool = b->pool;
  opt_rev.kind = svn_opt_revision_working;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, deleted_path),
                             &opt_rev, svn_depth_empty, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_deleted);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_STRING_ASSERT(status->moved_to_abspath,
                         sbox_wc_path(b, moved_to_path));

  /* Ensure that the moved-here directory has the expected status. */
  sb.result_pool = b->pool;
  opt_rev.kind = svn_opt_revision_working;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, moved_to_path),
                             &opt_rev, svn_depth_empty, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_added);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_STRING_ASSERT(status->moved_from_abspath,
                         sbox_wc_path(b, deleted_path));
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* Ensure that the edited file has the expected content. */
  child_path = svn_relpath_join(moved_to_path, deleted_dir_child,
                                b->pool);
  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, child_path),
                                   b->pool));
  SVN_TEST_STRING_ASSERT(buf->data, modified_file_on_branch_content);

  return SVN_NO_ERROR;
}

/* Test 'incoming move dir merge' resolution option with local mods. */
static svn_error_t *
test_merge_incoming_move_dir2(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  const char *deleted_path;
  const char *moved_to_path;
  const char *child_path;
  svn_client_conflict_t *conflict;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_stringbuf_t *buf;
  svn_opt_revision_t opt_rev;

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_move_dir2", opts, pool));

  SVN_ERR(create_wc_with_incoming_delete_dir_conflict(b, TRUE, FALSE, TRUE,
                                                      FALSE));

  deleted_path = svn_relpath_join(branch_path, deleted_dir_name, b->pool);
  moved_to_path = svn_relpath_join(branch_path, new_dir_name, b->pool);

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, deleted_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_move_dir_merge,
            ctx, b->pool));

  /* Ensure that the moved-away directory has the expected status. */
  sb.result_pool = b->pool;
  opt_rev.kind = svn_opt_revision_working;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, deleted_path),
                             &opt_rev, svn_depth_empty, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_deleted);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_STRING_ASSERT(status->moved_to_abspath,
                         sbox_wc_path(b, moved_to_path));

  /* Ensure that the moved-here directory has the expected status. */
  sb.result_pool = b->pool;
  opt_rev.kind = svn_opt_revision_working;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, moved_to_path),
                             &opt_rev, svn_depth_empty, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_added);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_STRING_ASSERT(status->moved_from_abspath,
                         sbox_wc_path(b, deleted_path));
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* Ensure that the edited file has the expected content. */
  child_path = svn_relpath_join(moved_to_path, deleted_dir_child,
                                b->pool);
  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, child_path),
                                   b->pool));
  SVN_TEST_STRING_ASSERT(buf->data, modified_file_in_working_copy_content);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_merge_incoming_move_dir3(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  const char *deleted_path;
  const char *moved_to_path;
  const char *child_path;
  const char *child_url;
  svn_client_conflict_t *conflict;
  struct status_baton sb;
  struct info_baton ib;
  struct svn_client_status_t *status;
  svn_stringbuf_t *buf;
  svn_opt_revision_t opt_rev;

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_move_dir3", opts, pool));

  SVN_ERR(create_wc_with_incoming_delete_dir_conflict(b, TRUE, FALSE, FALSE,
                                                      TRUE));

  deleted_path = svn_relpath_join(branch_path, deleted_dir_name, b->pool);
  moved_to_path = svn_relpath_join(branch_path, new_dir_name, b->pool);

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, deleted_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_move_dir_merge,
            ctx, b->pool));

  /* Ensure that the moved-away directory has the expected status. */
  sb.result_pool = b->pool;
  opt_rev.kind = svn_opt_revision_working;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, deleted_path),
                             &opt_rev, svn_depth_empty, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_deleted);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_STRING_ASSERT(status->moved_to_abspath,
                         sbox_wc_path(b, moved_to_path));

  /* Ensure that the moved-here directory has the expected status. */
  sb.result_pool = b->pool;
  opt_rev.kind = svn_opt_revision_working;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, moved_to_path),
                             &opt_rev, svn_depth_empty, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_added);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_STRING_ASSERT(status->moved_from_abspath,
                         sbox_wc_path(b, deleted_path));
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* Ensure that the file added on the branch has the expected content. */
  child_path = svn_relpath_join(branch_path,
                                svn_relpath_join(new_dir_name,
                                                 new_file_name_branch,
                                                 b->pool),
                                b->pool);
  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, child_path),
                                   b->pool));
  SVN_TEST_STRING_ASSERT(buf->data, added_file_on_branch_content);

  /* Ensure that the file added on the branch has the expected status. */
  sb.result_pool = b->pool;
  opt_rev.kind = svn_opt_revision_working;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, child_path),
                             &opt_rev, svn_depth_empty, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* Ensure that the file added on the trunk has the expected content. */
  child_path = svn_relpath_join(trunk_path,
                                svn_relpath_join(new_dir_name,
                                                 new_file_name,
                                                 b->pool),
                                b->pool);
  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, child_path),
                                   b->pool));
  SVN_TEST_STRING_ASSERT(buf->data, "This is a new file on the trunk\n");

  /* Ensure that the file added on the trunk has the expected status. */
  sb.result_pool = b->pool;
  opt_rev.kind = svn_opt_revision_working;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, child_path),
                             &opt_rev, svn_depth_empty, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* Commit and make sure both files are present in the resulting revision. */
  SVN_ERR(sbox_wc_commit(b, ""));

  ib.result_pool = b->pool;
  opt_rev.kind = svn_opt_revision_head;

  /* The file added on the branch should be present. */
  child_url = apr_pstrcat(b->pool, b->repos_url, "/", branch_path, "/",
                          new_dir_name, "/", new_file_name_branch, SVN_VA_NULL);
  SVN_ERR(svn_client_info4(child_url, &opt_rev, &opt_rev, svn_depth_empty,
                           TRUE, TRUE, TRUE, NULL,
                           info_func, &ib, ctx, b->pool));

  /* The file added on the trunk should be present. */
  child_url = apr_pstrcat(b->pool, b->repos_url, "/", branch_path, "/",
                          new_dir_name, "/", new_file_name, SVN_VA_NULL);
  SVN_ERR(svn_client_info4(child_url, &opt_rev, &opt_rev, svn_depth_empty,
                           TRUE, TRUE, TRUE, NULL,
                           info_func, &ib, ctx, b->pool));

  return SVN_NO_ERROR;
}

/* A helper function which prepares a working copy for the tests below. */
static svn_error_t *
create_wc_with_incoming_delete_vs_local_delete(svn_test__sandbox_t *b)
{
  svn_client_ctx_t *ctx;
  static const char *trunk_url;
  svn_opt_revision_t opt_rev;
  const char *copy_src_path;
  const char *copy_dst_name;
  const char *copy_dst_path;
  const char *deleted_file_path;

  SVN_ERR(sbox_add_and_commit_greek_tree(b));

  /* Create a branch of node "A". */
  SVN_ERR(sbox_wc_copy(b, trunk_path, branch_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  /* On the trunk, copy "mu" to "mu-copied". */
  copy_src_path = svn_relpath_join(trunk_path, deleted_file_name, b->pool);
  copy_dst_name = apr_pstrcat(b->pool, deleted_file_name, "-copied",
                              SVN_VA_NULL);
  copy_dst_path = svn_relpath_join(trunk_path, copy_dst_name, b->pool);
  SVN_ERR(sbox_wc_copy(b, copy_src_path, copy_dst_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  /* Merge the file copy to the branch. */
  trunk_url = apr_pstrcat(b->pool, b->repos_url, "/", trunk_path, SVN_VA_NULL);
  opt_rev.kind = svn_opt_revision_head;
  opt_rev.value.number = SVN_INVALID_REVNUM;
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  SVN_ERR(svn_client_merge_peg5(trunk_url, NULL, &opt_rev,
                                sbox_wc_path(b, branch_path),
                                svn_depth_infinity,
                                FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                                NULL, ctx, b->pool));
  SVN_ERR(sbox_wc_commit(b, ""));

  /* Now delete the copied file on the trunk. */
  deleted_file_path = svn_relpath_join(trunk_path, copy_dst_name, b->pool);
  SVN_ERR(sbox_wc_delete(b, deleted_file_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  /* Delete the corresponding file on the branch. */
  deleted_file_path = svn_relpath_join(branch_path, copy_dst_name,
                                       b->pool);
  SVN_ERR(sbox_wc_delete(b, deleted_file_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  /* Run a merge from the trunk to the branch.
   * This should raise an "incoming delete vs local delete" tree conflict. */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  SVN_ERR(svn_client_merge_peg5(trunk_url, NULL, &opt_rev,
                                sbox_wc_path(b, branch_path),
                                svn_depth_infinity,
                                FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                                NULL, ctx, b->pool));

  return SVN_NO_ERROR;
}

/* Test for the 'incoming delete vs local delete' bug fixed by r1751893. */
static svn_error_t *
test_merge_incoming_delete_vs_local_delete(const svn_test_opts_t *opts,
                                           apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  const char *copy_dst_name;
  const char *copy_dst_path;
  svn_client_conflict_t *conflict;
  svn_node_kind_t node_kind;

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_delete_vs_local_delete",
                                   opts, pool));

  SVN_ERR(create_wc_with_incoming_delete_vs_local_delete(b));

  copy_dst_name = apr_pstrcat(b->pool, deleted_file_name, "-copied",
                              SVN_VA_NULL);
  copy_dst_path = svn_relpath_join(branch_path, copy_dst_name, b->pool);

  /* Resolve the tree conflict. Before r1751893 there was an unintended error.*/
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, copy_dst_path),
                                  ctx, b->pool, b->pool));

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_delete_accept,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_delete_accept,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_delete_accept,
            ctx, b->pool));

  /* The file should be gone. */
  SVN_ERR(svn_io_check_path(sbox_wc_path(b, copy_dst_path), &node_kind,
                            b->pool));
  SVN_TEST_ASSERT(node_kind == svn_node_none);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_merge_file_prop(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  svn_opt_revision_t opt_rev;
  svn_client_conflict_t *conflict;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  apr_array_header_t *resolution_options;
  svn_client_conflict_option_t *option;
  const svn_string_t *propval;

  SVN_ERR(svn_test__sandbox_create(b, "merge_file_prop", opts, pool));

  SVN_ERR(sbox_add_and_commit_greek_tree(b));
  /* Create a copy of node "A". */
  SVN_ERR(sbox_wc_copy(b, "A", "A1"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* Commit conflicting file properties. */
  SVN_ERR(sbox_wc_propset(b, "prop", "val1", "A/mu"));
  SVN_ERR(sbox_wc_propset(b, "prop", "val2", "A1/mu"));
  SVN_ERR(sbox_wc_commit(b, ""));

  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  opt_rev.kind = svn_opt_revision_head;
  opt_rev.value.number = SVN_INVALID_REVNUM;
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, pool));

  /* Merge "A" to "A1". */
  SVN_ERR(svn_client_merge_peg5(svn_path_url_add_component2(b->repos_url, "A",
                                                            pool),
                                NULL, &opt_rev, sbox_wc_path(b, "A1"),
                                svn_depth_infinity,
                                FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                                NULL, ctx, pool));

  /* The file "mu" should have a property conflict. */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A1/mu"), ctx,
                                  pool, pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, pool, pool));
  SVN_TEST_ASSERT(!text_conflicted);
  SVN_TEST_INT_ASSERT(props_conflicted->nelts, 1);
  SVN_TEST_STRING_ASSERT(APR_ARRAY_IDX(props_conflicted, 0, const char *),
                         "prop");
  SVN_TEST_ASSERT(!tree_conflicted);

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_base_text,
      svn_client_conflict_option_incoming_text,
      svn_client_conflict_option_working_text,
      svn_client_conflict_option_incoming_text_where_conflicted,
      svn_client_conflict_option_working_text_where_conflicted,
      svn_client_conflict_option_merged_text,
      -1 /* end of list */
    };
    SVN_ERR(assert_prop_conflict_options(conflict, ctx, expected_opts, pool));
  }

  SVN_ERR(svn_client_conflict_prop_get_resolution_options(&resolution_options,
                                                          conflict, ctx,
                                                          pool, pool));
  option = svn_client_conflict_option_find_by_id(
             resolution_options,
             svn_client_conflict_option_merged_text);
  svn_client_conflict_option_set_merged_propval(
    option, svn_string_create("merged-val", pool));

  /* Resolve the conflict with a merged property value. */
  SVN_ERR(svn_client_conflict_prop_resolve(conflict, "prop", option,
                                           ctx, pool));
  /* The file should not be in conflict. */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A1/mu"), ctx,
                                  pool, pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, pool, pool));
  SVN_TEST_ASSERT(!text_conflicted);
  SVN_TEST_INT_ASSERT(props_conflicted->nelts, 0);
  SVN_TEST_ASSERT(!tree_conflicted);

  /* And it should have the expected property value. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx, sbox_wc_path(b, "A1/mu"),
                           "prop", pool, pool));
  SVN_TEST_STRING_ASSERT(propval->data, "merged-val");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_merge_incoming_move_file_text_merge_conflict(const svn_test_opts_t *opts,
                                                  apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  svn_opt_revision_t opt_rev;
  svn_client_conflict_t *conflict;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  const char *base_abspath;
  const char *working_abspath;
  const char *incoming_old_abspath;
  const char *incoming_new_abspath;
  svn_stringbuf_t *buf;

  SVN_ERR(svn_test__sandbox_create(
            b, "merge_incoming_move_file_text_merge_conflict", opts, pool));

  SVN_ERR(sbox_add_and_commit_greek_tree(b));
  /* Write initial file content. */
  SVN_ERR(sbox_file_write(b, "A/mu", "Initial content.\n"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* Create a copy of node "A". */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  SVN_ERR(sbox_wc_copy(b, "A", "A1"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "trunk", move the file and edit it. */
  SVN_ERR(sbox_wc_move(b, "A/mu", "A/mu-moved"));
  SVN_ERR(sbox_file_write(b, "A/mu-moved", "New trunk content.\n"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "branch", edit the file. */
  SVN_ERR(sbox_file_write(b, "A1/mu", "New branch content.\n"));
  SVN_ERR(sbox_wc_commit(b, ""));

  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  opt_rev.kind = svn_opt_revision_head;
  opt_rev.value.number = SVN_INVALID_REVNUM;
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, pool));

  /* Merge "A" to "A1". */
  SVN_ERR(svn_client_merge_peg5(svn_path_url_add_component2(b->repos_url, "A",
                                                            pool),
                                NULL, &opt_rev, sbox_wc_path(b, "A1"),
                                svn_depth_infinity,
                                FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                                NULL, ctx, pool));

  /* We should have a tree conflict in the file "mu". */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A1/mu"), ctx,
                                  pool, pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, pool, pool));
  SVN_TEST_ASSERT(!text_conflicted);
  SVN_TEST_INT_ASSERT(props_conflicted->nelts, 0);
  SVN_TEST_ASSERT(tree_conflicted);

  /* Check available tree conflict resolution options. */
  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_delete_ignore,
      svn_client_conflict_option_incoming_delete_accept,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts, pool));
  }

  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, pool));

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_move_file_text_merge,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts, pool));
  }

  /* Resolve the tree conflict by moving "mu" to "mu-moved". */
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_move_file_text_merge,
            ctx, pool));

  /* We should now have a text conflict in the file "mu-moved". */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A1/mu-moved"),
                                  ctx, pool, pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, pool, pool));
  SVN_TEST_ASSERT(text_conflicted);
  SVN_TEST_INT_ASSERT(props_conflicted->nelts, 0);
  SVN_TEST_ASSERT(!tree_conflicted);

  /* Check available text conflict resolution options. */
  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_base_text,
      svn_client_conflict_option_incoming_text,
      svn_client_conflict_option_working_text,
      svn_client_conflict_option_incoming_text_where_conflicted,
      svn_client_conflict_option_working_text_where_conflicted,
      svn_client_conflict_option_merged_text,
      -1 /* end of list */
    };
    SVN_ERR(assert_text_conflict_options(conflict, ctx, expected_opts, pool));
  }

  /* Check versions of the text-conflicted file. */
  SVN_ERR(svn_client_conflict_text_get_contents(&base_abspath,
                                                &working_abspath,
                                                &incoming_old_abspath,
                                                &incoming_new_abspath,
                                                conflict, pool, pool));

  SVN_TEST_ASSERT(base_abspath == NULL);

  SVN_ERR(svn_stringbuf_from_file2(&buf, incoming_old_abspath, pool));
  SVN_TEST_STRING_ASSERT(buf->data, "Initial content.\n");

  /* XFAIL: We merge from trunk (/A) to branch (/A1). The working version
   * should say "New branch content", and the incoming version should
   * say "New trunk content", not vice versa.
   */
  SVN_ERR(svn_stringbuf_from_file2(&buf, working_abspath, pool));
  SVN_TEST_STRING_ASSERT(buf->data, "New branch content.\n");

  SVN_ERR(svn_stringbuf_from_file2(&buf, incoming_new_abspath, pool));
  SVN_TEST_STRING_ASSERT(buf->data, "New trunk content.\n");

  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, "A1/mu-moved"),
                                   pool));
  SVN_TEST_STRING_ASSERT(buf->data,
                         "<<<<<<< .working\n"
                         "New branch content.\n"
                         "||||||| .old\n"
                         "Initial content.\n"
                         "=======\n"
                         "New trunk content.\n"
                         ">>>>>>> .new\n");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_merge_incoming_edit_file_moved_away(const svn_test_opts_t *opts,
                                         apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  svn_opt_revision_t opt_rev;
  svn_client_conflict_t *conflict;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  svn_stringbuf_t *buf;

  SVN_ERR(svn_test__sandbox_create(
            b, "merge_incoming_edit_file_moved_away", opts, pool));

  SVN_ERR(sbox_add_and_commit_greek_tree(b));
  /* Create a copy of node "A". */
  SVN_ERR(sbox_wc_copy(b, "A", "A1"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "trunk", edit the file. */
  SVN_ERR(sbox_file_write(b, "A/mu", "New trunk content.\n"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "branch", move the file. */
  SVN_ERR(sbox_wc_move(b, "A1/mu", "A1/mu-moved"));
  SVN_ERR(sbox_wc_commit(b, ""));

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, pool));

  /* Merge "trunk" to "branch". */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  opt_rev.kind = svn_opt_revision_head;
  opt_rev.value.number = SVN_INVALID_REVNUM;
  SVN_ERR(svn_client_merge_peg5(svn_path_url_add_component2(b->repos_url, "A",
                                                            pool),
                                NULL, &opt_rev, sbox_wc_path(b, "A1"),
                                svn_depth_infinity,
                                FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                                NULL, ctx, pool));

  /* We should have a tree conflict in the file "mu". */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A1/mu"), ctx,
                                  pool, pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, pool, pool));
  SVN_TEST_ASSERT(!text_conflicted);
  SVN_TEST_INT_ASSERT(props_conflicted->nelts, 0);
  SVN_TEST_ASSERT(tree_conflicted);

  /* Check available tree conflict resolution options. */
  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts, pool));
  }

  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, pool));

  /* XFAIL: We don't offer an option to apply incoming changes to move
   * destination in the remote edit vs local (to branch history) move
   * during merge. Not too sure if the currently expected
   * svn_client_conflict_option_update_move_destination is the proper
   * expectation, or if we need a separate option, but currently the
   * tree conflict resolver offers only the "postpone" and "mark as
   * resolved" option. */
  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_update_move_destination,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts, pool));
  }

  /* Resolve the tree conflict by applying the incoming edit to the local
   * move destination "mu-moved". */
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_update_move_destination,
            ctx, pool));

  /* The file should not be in conflict. */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A1/mu-moved"),
                                  ctx, pool, pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, pool, pool));
  SVN_TEST_ASSERT(!text_conflicted);
  SVN_TEST_INT_ASSERT(props_conflicted->nelts, 0);
  SVN_TEST_ASSERT(!tree_conflicted);

  /* And it should have the expected content. */
  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, "A1/mu-moved"),
                                   pool));
  SVN_TEST_STRING_ASSERT(buf->data, "New trunk content.\n");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_merge_incoming_chained_move_local_edit(const svn_test_opts_t *opts,
                                            apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  svn_opt_revision_t opt_rev;
  svn_client_conflict_t *conflict;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  svn_stringbuf_t *buf;

  SVN_ERR(svn_test__sandbox_create(
            b, "merge_incoming_chained_move_local_edit", opts, pool));

  SVN_ERR(sbox_add_and_commit_greek_tree(b));
  /* Create a copy of node "A". */
  SVN_ERR(sbox_wc_copy(b, "A", "A1"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "trunk", move the file. */
  SVN_ERR(sbox_wc_move(b, "A/mu", "A/mu-moved"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "trunk", move the file again. */
  SVN_ERR(sbox_wc_move(b, "A/mu-moved", "A/mu-moved-again"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "branch", edit the file. */
  SVN_ERR(sbox_file_write(b, "A1/mu", "New branch content.\n"));
  SVN_ERR(sbox_wc_commit(b, ""));

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, pool));

  /* Merge "trunk" to "branch". */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  opt_rev.kind = svn_opt_revision_head;
  opt_rev.value.number = SVN_INVALID_REVNUM;
  SVN_ERR(svn_client_merge_peg5(svn_path_url_add_component2(b->repos_url, "A",
                                                            pool),
                                NULL, &opt_rev, sbox_wc_path(b, "A1"),
                                svn_depth_infinity,
                                FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                                NULL, ctx, pool));

  /* We should have a tree conflict in the file "mu". */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A1/mu"), ctx,
                                  pool, pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, pool, pool));
  SVN_TEST_ASSERT(!text_conflicted);
  SVN_TEST_INT_ASSERT(props_conflicted->nelts, 0);
  SVN_TEST_ASSERT(tree_conflicted);

  /* Check available tree conflict resolution options. */
  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_delete_ignore,
      svn_client_conflict_option_incoming_delete_accept,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts, pool));
  }

  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, pool));

  /* XFAIL: The conflict resolver doesn't find the move, and doesn't offer
   * the svn_client_conflict_option_incoming_move_file_text_merge option. */
  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_move_file_text_merge,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts, pool));
  }

  /* Resolve the tree conflict by moving "mu" to "mu-moved-again". */
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_move_file_text_merge,
            ctx, pool));

  /* The file should not be in conflict. */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A1/mu"),
                                  ctx, pool, pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, pool, pool));
  SVN_TEST_ASSERT(!text_conflicted);
  SVN_TEST_INT_ASSERT(props_conflicted->nelts, 0);
  SVN_TEST_ASSERT(!tree_conflicted);

  /* The move destination should have the expected content. */
  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, "A1/mu-moved-again"),
                                   pool));
  SVN_TEST_STRING_ASSERT(buf->data, "New branch content.\n");

  return SVN_NO_ERROR;
}

/* ========================================================================== */


static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_file_ignore,
                       "merge incoming add file ignore"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_file_text_merge,
                       "merge incoming add file text merge"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_file_replace,
                       "merge incoming add file replace"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_file_replace_and_merge,
                       "merge incoming add file replace and merge"),
    SVN_TEST_OPTS_PASS(test_update_incoming_added_file_ignore,
                       "update incoming add file ignore"),
    SVN_TEST_OPTS_PASS(test_update_incoming_added_file_replace,
                       "update incoming add file replace"),
    SVN_TEST_OPTS_PASS(test_switch_incoming_added_file_ignore,
                       "switch incoming add file ignore"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_dir_ignore,
                       "merge incoming add dir ignore"),
    SVN_TEST_OPTS_XFAIL(test_merge_incoming_added_dir_merge,
                       "merge incoming add dir merge"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_dir_merge2,
                       "merge incoming add dir merge with file change"),
    SVN_TEST_OPTS_XFAIL(test_merge_incoming_added_dir_merge3,
                       "merge incoming add dir merge with move history"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_dir_replace,
                       "merge incoming add dir replace"),
    SVN_TEST_OPTS_XFAIL(test_merge_incoming_added_dir_replace_and_merge,
                       "merge incoming add dir replace and merge"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_dir_replace_and_merge2,
                       "merge incoming add dir replace with file change"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_delete_ignore,
                       "merge incoming delete ignore"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_delete_accept,
                       "merge incoming delete accept"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_move_file_text_merge,
                       "merge incoming move file text merge"),
    SVN_TEST_OPTS_PASS(test_update_incoming_move_file_text_merge,
                       "update incoming move file text merge"),
    SVN_TEST_OPTS_PASS(test_switch_incoming_move_file_text_merge,
                       "switch incoming move file text merge"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_move_dir, "merge incoming move dir"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_move_dir2,
                       "merge incoming move dir with local edit"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_move_dir3,
                       "merge incoming move dir with local add"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_delete_vs_local_delete,
                       "merge incoming delete vs local delete"),
    SVN_TEST_OPTS_PASS(test_merge_file_prop,
                       "merge file property"),
    SVN_TEST_OPTS_XFAIL(test_merge_incoming_move_file_text_merge_conflict,
                        "merge incoming move file merge with text conflict"),
    SVN_TEST_OPTS_XFAIL(test_merge_incoming_edit_file_moved_away,
                        "merge incoming edit for a moved-away working file"),
    SVN_TEST_OPTS_XFAIL(test_merge_incoming_chained_move_local_edit,
                        "merge incoming chained move vs local edit"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
