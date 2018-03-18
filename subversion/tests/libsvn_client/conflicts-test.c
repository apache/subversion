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
#include "svn_props.h"

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
static const char *branch2_path = "A_branch2";
static const char *new_file_name = "newfile.txt";
static const char *new_file_name_branch = "newfile-on-branch.txt";
static const char *deleted_file_name = "mu";
static const char *deleted_dir_name = "B";
static const char *deleted_dir_child = "lambda";
static const char *new_dir_name = "newdir";

/* File property content. */
static const char *propval_trunk = "This is a property on the trunk.";
static const char *propval_branch = "This is a property on the branch.";
static const char *propval_different = "This is a different property value.";

/* File content. */
static const char *modified_file_content =
                        "This is a modified file\n";
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
  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_added_file_text_merge,
      svn_client_conflict_option_incoming_added_file_replace_and_merge,
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
      svn_client_conflict_option_incoming_added_file_text_merge,
      svn_client_conflict_option_incoming_added_file_replace_and_merge,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

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

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_add_ignore,
      svn_client_conflict_option_incoming_added_dir_merge,
      svn_client_conflict_option_incoming_added_dir_replace,
      svn_client_conflict_option_incoming_added_dir_replace_and_merge,
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
      svn_client_conflict_option_incoming_add_ignore,
      svn_client_conflict_option_incoming_added_dir_merge,
      svn_client_conflict_option_incoming_added_dir_replace,
      svn_client_conflict_option_incoming_added_dir_replace_and_merge,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

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

  new_file_path = svn_relpath_join(branch_path,
                                   svn_relpath_join(new_dir_name,
                                                    new_file_name, b->pool),
                                   b->pool);

  /* Ensure that the file has the expected status. */
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
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* The file should now have a text conflict. */
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

/* Same test as above, but with an additional file change on the trunk. */
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

  new_file_path = svn_relpath_join(branch_path,
                                   svn_relpath_join(new_dir_name,
                                                    new_file_name, b->pool),
                                   b->pool);

  /* Ensure that the file has the expected status. */
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
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* The file should now have a text conflict. */
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
  /* ### Shouldn't there be a property conflict? The trunk wins. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_file_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_trunk);

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

  /* There should now be an 'add vs add' conflict on the new file. */
  new_file_path = svn_relpath_join(branch_path,
                                   svn_relpath_join(new_dir_name,
                                                    new_file_name, b->pool),
                                   b->pool);

  /* Ensure that the file has the expected status. */
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
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* We should now have a text conflict in the file. */
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
  /* ### Shouldn't there be a property conflict? The trunk wins. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_file_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_trunk);

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

  SVN_ERR(svn_test__sandbox_create(
            b, "merge_incoming_added_dir_replace_and_merge2", opts, pool));

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
create_wc_with_incoming_delete_file_merge_conflict(svn_test__sandbox_t *b,
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
test_merge_incoming_delete_file_ignore(const svn_test_opts_t *opts,
                                       apr_pool_t *pool)
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

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_delete_file_ignore",
                                   opts, pool));

  SVN_ERR(create_wc_with_incoming_delete_file_merge_conflict(b, FALSE, FALSE));

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
test_merge_incoming_delete_file_accept(const svn_test_opts_t *opts,
                                        apr_pool_t *pool)
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

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_delete_file_accept",
                                   opts, pool));

  SVN_ERR(create_wc_with_incoming_delete_file_merge_conflict(b, FALSE, FALSE));

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

  SVN_ERR(create_wc_with_incoming_delete_file_merge_conflict(b, TRUE, FALSE));

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
create_wc_with_incoming_delete_file_update_conflict(svn_test__sandbox_t *b,
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

/* Test 'incoming delete ignore' option. */
static svn_error_t *
test_update_incoming_delete_file_ignore(const svn_test_opts_t *opts,
                                        apr_pool_t *pool)
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

  SVN_ERR(svn_test__sandbox_create(b, "update_incoming_delete_file_ignore",
                                   opts, pool));

  SVN_ERR(create_wc_with_incoming_delete_file_update_conflict(b, FALSE));

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
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_added);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(status->copied);
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
test_update_incoming_delete_file_accept(const svn_test_opts_t *opts,
                                        apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  const char *deleted_path;
  svn_client_conflict_t *conflict;
  svn_node_kind_t node_kind;

  SVN_ERR(svn_test__sandbox_create(b, "update_incoming_delete_file_accept",
                                   opts, pool));

  SVN_ERR(create_wc_with_incoming_delete_file_update_conflict(b, FALSE));

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

  /* Ensure that the deleted file is gone. */
  SVN_ERR(svn_io_check_path(sbox_wc_path(b, deleted_path), &node_kind,
                            b->pool));
  SVN_TEST_ASSERT(node_kind == svn_node_none);

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

  SVN_ERR(create_wc_with_incoming_delete_file_update_conflict(b, TRUE));

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

  SVN_ERR(create_wc_with_incoming_delete_file_merge_conflict(b, TRUE, TRUE));

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
  apr_array_header_t *options;
  svn_client_conflict_option_t *option;
  apr_array_header_t *possible_moved_to_abspaths;

  SVN_ERR(svn_test__sandbox_create(b, "merge_incoming_move_dir", opts, pool));

  SVN_ERR(create_wc_with_incoming_delete_dir_conflict(b, TRUE, FALSE, FALSE,
                                                      FALSE));

  deleted_path = svn_relpath_join(branch_path, deleted_dir_name, b->pool);
  moved_to_path = svn_relpath_join(branch_path, new_dir_name, b->pool);

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, deleted_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));

  /* Check possible move destinations for the directory. */
  SVN_ERR(svn_client_conflict_tree_get_resolution_options(&options, conflict,
                                                          ctx, b->pool,
                                                          b->pool));
  option = svn_client_conflict_option_find_by_id(
             options, svn_client_conflict_option_incoming_move_dir_merge);
  SVN_TEST_ASSERT(option != NULL);

  SVN_ERR(svn_client_conflict_option_get_moved_to_abspath_candidates(
            &possible_moved_to_abspaths, option, b->pool, b->pool));

  /* The resolver finds two possible destinations for the moved folder:
   *
   *   Possible working copy destinations for moved-away 'A_branch/B' are:
   *    (1): 'A_branch/newdir'
   *    (2): 'A/newdir'
   *   Only one destination can be a move; the others are copies.
   */
  SVN_TEST_INT_ASSERT(possible_moved_to_abspaths->nelts, 2);
  SVN_TEST_STRING_ASSERT(
    APR_ARRAY_IDX(possible_moved_to_abspaths, 0, const char *),
    sbox_wc_path(b, moved_to_path));
  SVN_TEST_STRING_ASSERT(
    APR_ARRAY_IDX(possible_moved_to_abspaths, 1, const char *),
    sbox_wc_path(b, svn_relpath_join(trunk_path, new_dir_name, b->pool)));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_client_conflict_option_set_moved_to_abspath(option, 0,
                                                          ctx, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve(conflict, option, ctx, b->pool));

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

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_local_move_file_text_merge,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts, pool));
  }

  /* Resolve the tree conflict by applying the incoming edit to the local
   * move destination "mu-moved". */
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_local_move_file_text_merge,
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

  /* This used to fail around r1764234. The conflict resolver was
   * unable to detect the move, and didn't offer the
   * svn_client_conflict_option_incoming_move_file_text_merge option. */
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

static svn_error_t *
test_merge_incoming_move_dir_with_moved_file(const svn_test_opts_t *opts,
                                             apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  svn_opt_revision_t opt_rev;
  svn_client_conflict_t *conflict;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;

  SVN_ERR(svn_test__sandbox_create(
            b, "merge_incoming_move_dir_with_moved_file", opts, pool));

  SVN_ERR(sbox_add_and_commit_greek_tree(b));
  /* Create a copy of node "A". */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  SVN_ERR(sbox_wc_copy(b, "A", "A1"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "trunk", move a file and then move the dir containing the file. */
  SVN_ERR(sbox_wc_move(b, "A/B/lambda", "A/B/lambda-moved"));
  SVN_ERR(sbox_wc_move(b, "A/B", "A/B-moved"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "branch", edit the file. */
  SVN_ERR(sbox_file_write(b, "A1/B/lambda", "New branch content.\n"));
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

  /* We should have a tree conflict on the dir. */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A1/B"), ctx,
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
      svn_client_conflict_option_incoming_move_dir_merge,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts, pool));
  }

  /* Resolve the tree conflict by moving the local directory and merging. */
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_move_dir_merge,
            ctx, pool));

  /* The dir should not be in conflict. */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A1/B"),
                                  ctx, pool, pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, pool, pool));
  SVN_TEST_ASSERT(!text_conflicted);
  SVN_TEST_INT_ASSERT(props_conflicted->nelts, 0);
  SVN_TEST_ASSERT(!tree_conflicted);

  /* Ensure that the move source dir has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, "A1/B"),
                             &opt_rev, svn_depth_empty, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, pool));
  status = sb.status;
  SVN_TEST_INT_ASSERT(status->kind, svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_INT_ASSERT(status->node_status, svn_wc_status_deleted);
  SVN_TEST_INT_ASSERT(status->text_status, svn_wc_status_normal);
  SVN_TEST_INT_ASSERT(status->prop_status, svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_STRING_ASSERT(status->moved_from_abspath, NULL);
  SVN_TEST_STRING_ASSERT(status->moved_to_abspath,
                         sbox_wc_path(b, "A1/B-moved"));

  /* Ensure that the move destination dir has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, "A1/B-moved"),
                             &opt_rev, svn_depth_empty, TRUE, TRUE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, pool));
  status = sb.status;
  SVN_TEST_INT_ASSERT(status->kind, svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_INT_ASSERT(status->node_status, svn_wc_status_added);
  SVN_TEST_INT_ASSERT(status->text_status, svn_wc_status_normal);
  SVN_TEST_INT_ASSERT(status->prop_status, svn_wc_status_none);
  SVN_TEST_ASSERT(status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_STRING_ASSERT(status->moved_from_abspath,
                         sbox_wc_path(b, "A1/B"));
  SVN_TEST_STRING_ASSERT(status->moved_to_abspath, NULL);

  /* We should have another tree conflict on the moved-away file. */
  SVN_ERR(svn_client_conflict_get(&conflict,
                                  sbox_wc_path(b, "A1/B-moved/lambda"),
                                  ctx, pool, pool));
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

  /* ### Need to test resolving the conflict on "A1/B-moved/lambda". */

  return SVN_NO_ERROR;
}

static svn_error_t *
test_merge_incoming_file_move_new_line_of_history(const svn_test_opts_t *opts,
                                                  apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  svn_opt_revision_t opt_rev;
  svn_client_conflict_t *conflict;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;

  SVN_ERR(svn_test__sandbox_create(
            b, "merge_incoming_file_move_new_line_of_history", opts, pool));

  SVN_ERR(sbox_add_and_commit_greek_tree(b));
  /* Create a copy of node "A". */
  SVN_ERR(sbox_wc_copy(b, "A", "A1"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "trunk", move the file. */
  SVN_ERR(sbox_wc_move(b, "A/mu", "A/mu-moved"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "trunk", change the line of history of the moved file by
   * replacing it. */
  SVN_ERR(sbox_wc_delete(b, "A/mu-moved"));
  SVN_ERR(sbox_file_write(b, "A/mu-moved", "x"));
  SVN_ERR(sbox_wc_add(b, "A/mu-moved"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "trunk", move the replaced file. */
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

  /* The svn_client_conflict_option_incoming_move_file_text_merge option
   * should not be available, as the "mu" file was actually deleted at
   * some point (and the remaining move is a part of the new line of
   * history). */
  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts, pool));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
run_test_update_incoming_dir_move_with_nested_file_move(
  const svn_test_opts_t *opts,
  svn_boolean_t move_parent,
  svn_boolean_t move_back,
  svn_boolean_t move_parent_twice,
  const char *sandbox_name,
  apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  const char *deleted_dir;
  const char *moved_dir;
  const char *deleted_file;
  const char *moved_file;
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  svn_stringbuf_t *buf;
  svn_node_kind_t kind;
  svn_opt_revision_t opt_rev;
  svn_client_status_t *status;
  struct status_baton sb;

  SVN_ERR(svn_test__sandbox_create(b, sandbox_name, opts, pool));
  SVN_ERR(sbox_add_and_commit_greek_tree(b));

  /* Move a directory on the trunk into another directory. */
  deleted_dir = svn_relpath_join(trunk_path, "B", b->pool);
  moved_dir = svn_relpath_join(trunk_path, "C/B", b->pool);
  SVN_ERR(sbox_wc_move(b, deleted_dir, moved_dir));

  /* Rename a file inside the moved directory. */
  deleted_file = svn_relpath_join(moved_dir, "lambda" , b->pool);
  moved_file = svn_relpath_join(moved_dir, "lambda-moved", b->pool);
  SVN_ERR(sbox_wc_move(b, deleted_file, moved_file));

  SVN_ERR(sbox_wc_commit(b, ""));

  if (move_parent)
    {
      /* Move the directory again. */
      SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
      deleted_dir = svn_relpath_join(trunk_path, "C/B", b->pool);
      moved_dir = svn_relpath_join(trunk_path, "D/H/B", b->pool);
      SVN_ERR(sbox_wc_move(b, deleted_dir, moved_dir));
      SVN_ERR(sbox_wc_commit(b, ""));

      if (move_back)
        {
          /* And back again. */
          SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
          deleted_dir = svn_relpath_join(trunk_path, "D/H/B", b->pool);
          moved_dir = svn_relpath_join(trunk_path, "C/B", b->pool);
          SVN_ERR(sbox_wc_move(b, deleted_dir, moved_dir));
          SVN_ERR(sbox_wc_commit(b, ""));
        }
      else if (move_parent_twice)
        {
          /* Move the directory again. */
          SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
          deleted_dir = svn_relpath_join(trunk_path, "D/H", b->pool);
          moved_dir = svn_relpath_join(trunk_path, "D/G/H", b->pool);
          SVN_ERR(sbox_wc_move(b, deleted_dir, moved_dir));
          SVN_ERR(sbox_wc_commit(b, ""));
          moved_dir = svn_relpath_join(trunk_path, "D/G/H/B", b->pool);
        }

      moved_file = svn_relpath_join(moved_dir, "lambda-moved", b->pool);
    }

  /* Update into the past. */
  SVN_ERR(sbox_wc_update(b, "", 1));

  /* Modify a file in the working copy. */
  deleted_file = svn_relpath_join(trunk_path, "B/lambda", b->pool);
  SVN_ERR(sbox_file_write(b, deleted_file, modified_file_content));

  /* Update to HEAD.
   * This should raise an "incoming move vs local edit" tree conflict. */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, pool));

  /* We should have a tree conflict in the directory "A/B". */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A/B"), ctx,
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
      svn_client_conflict_option_incoming_move_dir_merge,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts, pool));
  }

  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_move_dir_merge,
            ctx, pool));

  /* There should now be a tree conflict inside the moved directory,
   * signaling a missing file. */
  deleted_file = svn_relpath_join(moved_dir, "lambda" , b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, deleted_file),
                                  ctx, pool, pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, pool, pool));
  SVN_TEST_ASSERT(!text_conflicted);
  SVN_TEST_INT_ASSERT(props_conflicted->nelts, 0);
  SVN_TEST_ASSERT(tree_conflicted);
  SVN_TEST_ASSERT(svn_client_conflict_get_local_change(conflict) ==
                  svn_wc_conflict_reason_edited);
  SVN_TEST_ASSERT(svn_client_conflict_get_incoming_change(conflict) ==
                  svn_wc_conflict_action_delete);

  /* Make sure the file has the expected content. */
  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, deleted_file), pool));
  SVN_TEST_STRING_ASSERT(buf->data, modified_file_content);

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

  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_move_file_text_merge,
            ctx, pool));

  /* Ensure that the deleted file is gone. */
  SVN_ERR(svn_io_check_path(sbox_wc_path(b, deleted_file), &kind, b->pool));
  SVN_TEST_ASSERT(kind == svn_node_none);

  /* Ensure that the moved-target file has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, moved_file),
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

  /* The file should not be in conflict. */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, moved_file),
                                  ctx, b->pool, b->pool));

  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  /* Make sure the file has the expected content. */
  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, moved_file), pool));
  SVN_TEST_STRING_ASSERT(buf->data, modified_file_content);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_update_incoming_dir_move_with_nested_file_move(const svn_test_opts_t *opts,
                                                    apr_pool_t *pool)
{
  return run_test_update_incoming_dir_move_with_nested_file_move(
           opts, FALSE, FALSE, FALSE,
           "update_incoming_dir_move_with_nested_file_move", pool);
}

/* Same test as above, but with a moved parent directory. */
static svn_error_t *
test_update_incoming_dir_move_with_parent_move(
  const svn_test_opts_t *opts,
  apr_pool_t *pool)
{
  return run_test_update_incoming_dir_move_with_nested_file_move(
           opts, TRUE, FALSE, FALSE,
           "update_incoming_dir_move_with_parent_move", pool);
}

/* Same test as above, but with the parent directory moved back. */
static svn_error_t *
test_update_incoming_dir_move_with_parent_moved_back(
  const svn_test_opts_t *opts,
  apr_pool_t *pool)
{
  return run_test_update_incoming_dir_move_with_nested_file_move(
           opts, TRUE, TRUE, FALSE,
           "update_incoming_dir_move_with_parent_moved_back", pool);
}

/* Same test as above, but with the parent directory moved twice. */
static svn_error_t *
test_update_incoming_dir_move_with_parent_moved_twice(
  const svn_test_opts_t *opts,
  apr_pool_t *pool)
{
  return run_test_update_incoming_dir_move_with_nested_file_move(
           opts, TRUE, FALSE, TRUE,
           "update_incoming_dir_move_with_parent_moved_twice", pool);
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

  /* Add a new file and commit. */
  new_file_path = svn_relpath_join(trunk_path, new_file_name, b->pool);
  SVN_ERR(sbox_file_write(b, new_file_path,
                          "This is a new file on the trunk\n"));
  SVN_ERR(sbox_wc_add(b, new_file_path));
  SVN_ERR(sbox_wc_propset(b, "prop", "propval", new_file_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  /* Update into the past. */
  SVN_ERR(sbox_wc_update(b, "", 1));

  /* Add a different file scheduled for commit. */
  new_file_path = svn_relpath_join(trunk_path, new_file_name, b->pool);
  SVN_ERR(sbox_file_write(b, new_file_path,
                          "This is a different new file on the trunk\n"));
  SVN_ERR(sbox_wc_add(b, new_file_path));
  SVN_ERR(sbox_wc_propset(b, "prop", propval_different, new_file_path));

  /* Update to HEAD.
   * This should raise an "incoming add vs local add" tree conflict. */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));

  opt_rev.kind = svn_opt_revision_head;
  opt_rev.value.number = SVN_INVALID_REVNUM;

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
test_update_incoming_added_file_text_merge(const svn_test_opts_t *opts,
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

  SVN_ERR(svn_test__sandbox_create(b, "update_incoming_added_file_text_merge",
                                   opts, pool));

  SVN_ERR(create_wc_with_file_add_vs_file_add_update_conflict(b));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));

  new_file_path = svn_relpath_join(trunk_path, new_file_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_file_path),
                                  ctx, b->pool, b->pool));

  /* Check available tree conflict resolution options. */
  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_added_file_text_merge,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts, pool));
  }

  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, pool));

  /* Check available tree conflict resolution options.
   * The list of options remains unchanged after get_details(). */
  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_added_file_text_merge,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts, pool));
  }

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

  /* Verify the merged property value. ### Should we have a prop conflict? */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_file_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_different);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_merge_incoming_move_file_prop_merge_conflict(const svn_test_opts_t *opts,
                                                  apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  svn_client_ctx_t *ctx;
  svn_opt_revision_t opt_rev;
  svn_client_conflict_t *conflict;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  const svn_string_t *base_propval;
  const svn_string_t *working_propval;
  const svn_string_t *incoming_old_propval;
  const svn_string_t *incoming_new_propval;

  SVN_ERR(svn_test__sandbox_create(
            b, "merge_incoming_move_file_prop_merge_conflict", opts, pool));

  SVN_ERR(sbox_add_and_commit_greek_tree(b));
  /* Add a file property. */
  SVN_ERR(sbox_wc_propset(b, "prop", "val-initial", "A/mu"));;
  SVN_ERR(sbox_wc_commit(b, ""));
  /* Create a copy of node "A". */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  SVN_ERR(sbox_wc_copy(b, "A", "A1"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "trunk", move the file and edit the property. */
  SVN_ERR(sbox_wc_move(b, "A/mu", "A/mu-moved"));
  SVN_ERR(sbox_wc_propset(b, "prop", "val-trunk", "A/mu-moved"));;
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "branch", edit the same property. */
  SVN_ERR(sbox_wc_propset(b, "prop", "val-branch", "A1/mu"));;
  SVN_ERR(sbox_wc_commit(b, ""));

  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  opt_rev.kind = svn_opt_revision_head;
  opt_rev.value.number = SVN_INVALID_REVNUM;
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, pool));

  /* Merge "trunk" to "branch". */
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

  /* We should now have a property conflict in the file "mu-moved". */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A1/mu-moved"),
                                  ctx, pool, pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, pool, pool));
  SVN_TEST_ASSERT(!text_conflicted);
  SVN_TEST_INT_ASSERT(props_conflicted->nelts, 1);
  SVN_TEST_STRING_ASSERT(APR_ARRAY_IDX(props_conflicted, 0, const char *),
                         "prop");
  SVN_TEST_ASSERT(!tree_conflicted);

  /* Check available property conflict resolution options. */
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

  /* Check conflicted property values. */
  SVN_ERR(svn_client_conflict_prop_get_propvals(&base_propval,
                                                &working_propval,
                                                &incoming_old_propval,
                                                &incoming_new_propval,
                                                conflict, "prop", pool));
  /* ### Is this the proper expectation for base_propval? */
  SVN_TEST_STRING_ASSERT(base_propval->data, "val-branch");
  SVN_TEST_STRING_ASSERT(working_propval->data, "val-branch");
  SVN_TEST_STRING_ASSERT(incoming_old_propval->data, "val-initial");
  SVN_TEST_STRING_ASSERT(incoming_new_propval->data, "val-trunk");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_merge_incoming_move_file_text_merge_keywords(const svn_test_opts_t *opts,
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
            b, "merge_incoming_move_file_text_merge_keywords", opts, pool));

  SVN_ERR(sbox_add_and_commit_greek_tree(b));
  /* Set svn:keywords on a file. */
  SVN_ERR(sbox_wc_propset(b, SVN_PROP_KEYWORDS, "Revision", "A/mu"));;
  SVN_ERR(sbox_wc_commit(b, ""));
  /* Create a copy of node "A". */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  SVN_ERR(sbox_wc_copy(b, "A", "A1"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "trunk", begin using keywords in the file and move it. */
  SVN_ERR(sbox_file_write(b, "A/mu", "$Revision$\n"));
  SVN_ERR(sbox_wc_move(b, "A/mu", "A/mu-moved"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "branch", edit the file and make it equal to what's in trunk. */
  SVN_ERR(sbox_file_write(b, "A1/mu", "$Revision$\n"));
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

  /* The file should no longer be in conflict, and should not have a
   * text conflict, because the contents are identical in "trunk" and
   * in the "branch". */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A1/mu-moved"),
                                  ctx, pool, pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, pool, pool));
  SVN_TEST_ASSERT(!text_conflicted);
  SVN_TEST_INT_ASSERT(props_conflicted->nelts, 0);
  SVN_TEST_ASSERT(!tree_conflicted);

  /* And it should have expected contents (with expanded keywords). */
  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, "A1/mu-moved"),
                                   pool));
  SVN_TEST_STRING_ASSERT(buf->data, "$Revision: 5 $\n");

  return SVN_NO_ERROR;
}

/* A helper function which prepares a working copy for the tests below. */
static svn_error_t *
create_wc_with_dir_add_vs_dir_add_update_conflict(
  svn_test__sandbox_t *b,
  svn_boolean_t unversioned_obstructions)
{
  static const char *new_dir_path;
  static const char *new_dir_child_path;
  static const char *new_file_path;
  static const char *new_file_child_path;
  svn_client_ctx_t *ctx;
  svn_opt_revision_t opt_rev;
  svn_client_status_t *status;
  struct status_baton sb;
  svn_client_conflict_t *conflict;
  svn_boolean_t tree_conflicted;

  SVN_ERR(sbox_add_and_commit_greek_tree(b));

  /* Add new directories on trunk and in the working copy which occupy
   * the same path but have different content and properties. */
  new_dir_path = svn_relpath_join(trunk_path, new_dir_name, b->pool);
  SVN_ERR(sbox_wc_mkdir(b, new_dir_path));
  SVN_ERR(sbox_wc_propset(b, "prop", propval_trunk, new_dir_path));
  new_file_path = svn_relpath_join(new_dir_path, new_file_name, b->pool);
  SVN_ERR(sbox_file_write(b, new_file_path,
                          "This is a new file on the trunk\n"));
  SVN_ERR(sbox_wc_add(b, new_file_path));
  SVN_ERR(sbox_wc_propset(b, "prop", propval_trunk, new_file_path));
  /* Create a directory and a file which will be obstructed during update. */
  new_dir_child_path = svn_relpath_join(new_dir_path, "dir_child", b->pool);
  SVN_ERR(sbox_wc_mkdir(b, new_dir_child_path));
  new_file_child_path = svn_relpath_join(new_dir_path, "file_child", b->pool);
  SVN_ERR(sbox_file_write(b, new_file_child_path,
                          "This is a child file on the trunk\n"));
  SVN_ERR(sbox_wc_add(b, new_file_child_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  /* Update back into the past. */
  SVN_ERR(sbox_wc_update(b, "", 1));

  new_dir_path = svn_relpath_join(trunk_path, new_dir_name, b->pool);
  SVN_ERR(sbox_wc_mkdir(b, new_dir_path));
  SVN_ERR(sbox_wc_propset(b, "prop", propval_different, new_dir_path));
  new_file_path = svn_relpath_join(trunk_path,
                                   svn_relpath_join(new_dir_name,
                                                    new_file_name, b->pool),
                                   b->pool);
  SVN_ERR(sbox_file_write(b, new_file_path,
                          /* NB: Ensure that the file content's length
                           * differs! Tests are run with sleep for
                           * timestamps disabled. */
                          "This is a different new file\n"));
  SVN_ERR(sbox_wc_add(b, new_file_path));
  SVN_ERR(sbox_wc_propset(b, "prop", propval_different, new_file_path));

  /* Add a file and a directory which obstruct incoming children. */
  SVN_ERR(sbox_file_write(b, new_dir_child_path,
                          "This is a new file on the trunk\n"));
  if (!unversioned_obstructions)
    {
      SVN_ERR(sbox_wc_mkdir(b, new_file_child_path));
      SVN_ERR(sbox_wc_add(b, new_dir_child_path));
    }
  else
    SVN_ERR(svn_io_dir_make(sbox_wc_path(b, new_file_child_path),
                            APR_OS_DEFAULT, b->pool));

  /* Update to the HEAD revision. 
   * This should raise an "incoming add vs local add" tree conflict. */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));

  /* Ensure that the directory has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_dir_path),
                             &opt_rev, svn_depth_empty, TRUE, FALSE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_replaced);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));

  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
      svn_client_conflict_option_incoming_add_ignore,
      svn_client_conflict_option_incoming_added_dir_merge,
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
      svn_client_conflict_option_incoming_add_ignore,
      svn_client_conflict_option_incoming_added_dir_merge,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

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
test_update_incoming_added_dir_ignore(const svn_test_opts_t *opts,
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

  SVN_ERR(svn_test__sandbox_create(b, "update_incoming_added_dir_ignore",
                                   opts, pool));

  SVN_ERR(create_wc_with_dir_add_vs_dir_add_update_conflict(b, FALSE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_dir_path = svn_relpath_join(trunk_path, new_dir_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_add_ignore, ctx,
            b->pool));

  /* Ensure that the directory has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_dir_path),
                             &opt_rev, svn_depth_empty, TRUE, FALSE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_replaced);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));

  /* Verify the added dir's property value.  */
  /* ### Shouldn't there be a property conflict? The local change wins. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_dir_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_different);

  /* The directory should not be in conflict. */
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  !tree_conflicted);

  /* Ensure that the newly added file has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  new_file_path = svn_relpath_join(trunk_path,
                                   svn_relpath_join(new_dir_name,
                                                    new_file_name, b->pool),
                                   b->pool);
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_file_path),
                             &opt_rev, svn_depth_empty, TRUE, FALSE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_added);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* Verify the added file's property value.  */
  /* ### Shouldn't there be a property conflict? The local change wins. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_file_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_different);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_update_incoming_added_dir_merge(const svn_test_opts_t *opts,
                                      apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  const char *new_dir_path;
  const char *new_dir_child_path;
  const char *new_file_path;
  const char *new_file_child_path;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  const svn_string_t *propval;
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(svn_test__sandbox_create(b, "update_incoming_added_dir_merge",
                                   opts, pool));

  SVN_ERR(create_wc_with_dir_add_vs_dir_add_update_conflict(b, FALSE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_dir_path = svn_relpath_join(trunk_path, new_dir_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_added_dir_merge, ctx,
            b->pool));

  /* Ensure that the directory has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_dir_path),
                             &opt_rev, svn_depth_empty, TRUE, FALSE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* Verify the added dir's property value.  */
  /* ### Shouldn't there be a property conflict? The local change wins. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_dir_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_different);
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

  /* Ensure that the newly added file has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  new_file_path = svn_relpath_join(trunk_path,
                                   svn_relpath_join(new_dir_name,
                                                    new_file_name, b->pool),
                                   b->pool);
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_file_path),
                             &opt_rev, svn_depth_empty, TRUE, FALSE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_conflicted);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_conflicted);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* Verify the added file's property value.  */
  /* ### Shouldn't there be a property conflict? The local change wins. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_file_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_different);

  /* Ensure that the obstructing added file child of newdir has the
   * expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  new_dir_child_path = svn_relpath_join(new_dir_path, "dir_child", b->pool);
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_dir_child_path),
                             &opt_rev, svn_depth_empty, TRUE, FALSE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_replaced);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* The file should be a tree conflict victim. */
  SVN_ERR(svn_client_conflict_get(&conflict,
                                  sbox_wc_path(b, new_dir_child_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  tree_conflicted);

  /* Ensure that the obstructing added dir child of newdir has the
   * expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  new_file_child_path = svn_relpath_join(new_dir_path, "file_child", b->pool);
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_file_child_path),
                             &opt_rev, svn_depth_empty, TRUE, FALSE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_replaced);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* The directory should be a tree conflict victim. */
  SVN_ERR(svn_client_conflict_get(&conflict,
                                  sbox_wc_path(b, new_file_child_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(!text_conflicted &&
                  props_conflicted->nelts == 0 &&
                  tree_conflicted);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_update_incoming_added_dir_merge2(const svn_test_opts_t *opts,
                                      apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  const char *new_dir_path;
  const char *new_dir_child_path;
  const char *new_file_path;
  const char *new_file_child_path;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  struct status_baton sb;
  struct svn_client_status_t *status;
  svn_opt_revision_t opt_rev;
  const svn_string_t *propval;
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(svn_test__sandbox_create(b, "update_incoming_added_dir_merge2",
                                   opts, pool));

  SVN_ERR(create_wc_with_dir_add_vs_dir_add_update_conflict(b, TRUE));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  new_dir_path = svn_relpath_join(trunk_path, new_dir_name, b->pool);
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, new_dir_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict, svn_client_conflict_option_incoming_added_dir_merge, ctx,
            b->pool));

  /* Ensure that the directory has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_dir_path),
                             &opt_rev, svn_depth_empty, TRUE, FALSE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* Verify the added dir's property value.  */
  /* ### Shouldn't there be a property conflict? The local change wins. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_dir_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_different);
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

  /* Ensure that the newly added file has the expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  new_file_path = svn_relpath_join(trunk_path,
                                   svn_relpath_join(new_dir_name,
                                                    new_file_name, b->pool),
                                   b->pool);
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_file_path),
                             &opt_rev, svn_depth_empty, TRUE, FALSE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_conflicted);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_conflicted);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_modified);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* Verify the added file's property value.  */
  /* ### Shouldn't there be a property conflict? The local change wins. */
  SVN_ERR(svn_wc_prop_get2(&propval, ctx->wc_ctx,
                           sbox_wc_path(b, new_file_path),
                           "prop", b->pool, b->pool));
  SVN_TEST_STRING_ASSERT(propval->data, propval_different);

  /* Ensure that the obstructing added file child of newdir has the
   * expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  new_dir_child_path = svn_relpath_join(new_dir_path, "dir_child", b->pool);
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_dir_child_path),
                             &opt_rev, svn_depth_empty, TRUE, FALSE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_dir);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_obstructed);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  /* Ensure that the obstructing added dir child of newdir has the
   * expected status. */
  opt_rev.kind = svn_opt_revision_working;
  sb.result_pool = b->pool;
  new_file_child_path = svn_relpath_join(new_dir_path, "file_child", b->pool);
  SVN_ERR(svn_client_status6(NULL, ctx, sbox_wc_path(b, new_file_child_path),
                             &opt_rev, svn_depth_empty, TRUE, FALSE,
                             TRUE, TRUE, FALSE, TRUE, NULL,
                             status_func, &sb, b->pool));
  status = sb.status;
  SVN_TEST_ASSERT(status->kind == svn_node_file);
  SVN_TEST_ASSERT(status->versioned);
  SVN_TEST_ASSERT(!status->conflicted);
  SVN_TEST_ASSERT(status->node_status == svn_wc_status_obstructed);
  SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
  SVN_TEST_ASSERT(status->prop_status == svn_wc_status_none);
  SVN_TEST_ASSERT(!status->copied);
  SVN_TEST_ASSERT(!status->switched);
  SVN_TEST_ASSERT(!status->file_external);
  SVN_TEST_ASSERT(status->moved_from_abspath == NULL);
  SVN_TEST_ASSERT(status->moved_to_abspath == NULL);

  return SVN_NO_ERROR;
}

/* Regression test for chrash fixed in r1780259. */
static svn_error_t *
test_cherry_pick_moved_file_with_propdel(const svn_test_opts_t *opts,
                                          apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  const char *vendor_url;
  svn_opt_revision_t peg_rev;
  apr_array_header_t *ranges_to_merge;
  svn_opt_revision_range_t merge_range;
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  svn_boolean_t tree_conflicted;

  SVN_ERR(svn_test__sandbox_create(b,
                                   "test_cherry_pick_moved_file_with_propdel",
                                   opts, pool));

  SVN_ERR(sbox_wc_mkdir(b, "A"));
  SVN_ERR(sbox_wc_mkdir(b, "A2"));
  SVN_ERR(sbox_wc_commit(b, "")); /* r1 */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));

  /* Let A/B/E act as a vendor branch of A2/E; A/B/E/lambda has a property. */
  SVN_ERR(sbox_wc_mkdir(b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(b, "A/B/E"));
  SVN_ERR(sbox_file_write(b, "A/B/E/lambda", "This is the file lambda.\n"));
  SVN_ERR(sbox_wc_add(b, "A/B/E/lambda"));
  SVN_ERR(sbox_wc_propset(b, "propname", "propval", "A/B/E/lambda"));
  SVN_ERR(sbox_wc_commit(b, "")); /* r2 */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  SVN_ERR(sbox_wc_copy(b, "A/B/E", "A2/E"));
  SVN_ERR(sbox_wc_commit(b, "")); /* r3 */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));

  /* Move vendor's E/lambda a level up and delete the property. */
  SVN_ERR(sbox_wc_move(b, "A/B/E/lambda", "A/B/lambda"));
  SVN_ERR(sbox_wc_propset(b, "propname", NULL /* propdel */, "A/B/lambda"));
  SVN_ERR(sbox_wc_commit(b, "")); /* r4 */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));

  /* Move vendor's lambda to a new subdirectory. */
  SVN_ERR(sbox_wc_mkdir(b, "A/B/newdir"));
  SVN_ERR(sbox_wc_move(b, "A/B/lambda", "A/B/newdir/lambda"));
  SVN_ERR(sbox_wc_commit(b, "")); /* r5 */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));

  /* Force a cherry-pick merge of A/B@5 to A2/E. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  vendor_url = apr_pstrcat(b->pool, b->repos_url, "/A/B", SVN_VA_NULL);
  peg_rev.kind = svn_opt_revision_number;
  peg_rev.value.number = 5;
  merge_range.start.kind = svn_opt_revision_number;
  merge_range.start.value.number = 4;
  merge_range.end.kind = svn_opt_revision_number;
  merge_range.end.value.number = 5;
  ranges_to_merge = apr_array_make(b->pool, 1,
                                   sizeof(svn_opt_revision_range_t *));
  APR_ARRAY_PUSH(ranges_to_merge, svn_opt_revision_range_t *) = &merge_range;
  /* This should raise a "local edit vs incoming delete or move" conflict. */
  SVN_ERR(svn_client_merge_peg5(vendor_url, ranges_to_merge, &peg_rev,
                                sbox_wc_path(b, "A2/E"), svn_depth_infinity,
                                TRUE, TRUE, FALSE, FALSE, FALSE, FALSE,
                                NULL, ctx, b->pool));

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A2/E/lambda"),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_get_conflicted(NULL, NULL, &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(tree_conflicted);
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

  /* Try to resolve the conflict. This crashed before r1780259 due to the
   * fact that a non-existent ancestor property was not accounted for. */
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict,
            svn_client_conflict_option_incoming_move_file_text_merge,
            ctx, b->pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_merge_incoming_move_file_text_merge_crlf(const svn_test_opts_t *opts,
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
            b, "merge_incoming_move_file_text_merge_crlf", opts, pool));

  SVN_ERR(sbox_add_and_commit_greek_tree(b));
  /* Edit the file to have CRLF line endings. */
  SVN_ERR(sbox_file_write(b, "A/mu", "Original content.\r\n"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* Create a copy of node "A". */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  SVN_ERR(sbox_wc_copy(b, "A", "A1"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "trunk", move the file. */
  SVN_ERR(sbox_wc_move(b, "A/mu", "A/mu-moved"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "branch", edit the file. */
  SVN_ERR(sbox_file_write(b, "A1/mu", "Modified content.\r\n"));
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

  /* The file should no longer be in conflict, and should not have a
   * text conflict, because the contents are identical in "trunk" and
   * in the "branch". */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A1/mu-moved"),
                                  ctx, pool, pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, pool, pool));
  SVN_TEST_ASSERT(!text_conflicted);
  SVN_TEST_INT_ASSERT(props_conflicted->nelts, 0);
  SVN_TEST_ASSERT(!tree_conflicted);

  /* And it should have expected contents. */
  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, "A1/mu-moved"),
                                   pool));
  SVN_TEST_STRING_ASSERT(buf->data, "Modified content.\r\n");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_merge_incoming_move_file_text_merge_native_eol(const svn_test_opts_t *opts,
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
            b, "merge_incoming_move_file_text_merge_native_eol", opts, pool));

  SVN_ERR(sbox_add_and_commit_greek_tree(b));
  /* Set svn:eol-style on a file and edit it. */
  SVN_ERR(sbox_wc_propset(b, SVN_PROP_EOL_STYLE, "native", "A/mu"));;
  SVN_ERR(sbox_file_write(b, "A/mu", "Original content.\n"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* Create a copy of node "A". */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  SVN_ERR(sbox_wc_copy(b, "A", "A1"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "trunk", move the file. */
  SVN_ERR(sbox_wc_move(b, "A/mu", "A/mu-moved"));
  SVN_ERR(sbox_wc_commit(b, ""));
  /* On "branch", edit the file. */
  SVN_ERR(sbox_file_write(b, "A1/mu", "Modified content.\n"));
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

  /* The file should no longer be in conflict, and should not have a
   * text conflict, because the contents are identical in "trunk" and
   * in the "branch". */
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A1/mu-moved"),
                                  ctx, pool, pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted,
                                             &props_conflicted,
                                             &tree_conflicted,
                                             conflict, pool, pool));
  SVN_TEST_ASSERT(!text_conflicted);
  SVN_TEST_INT_ASSERT(props_conflicted->nelts, 0);
  SVN_TEST_ASSERT(!tree_conflicted);

  /* And it should have expected contents. */
  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, "A1/mu-moved"),
                                   pool));
  SVN_TEST_STRING_ASSERT(buf->data, "Modified content." APR_EOL_STR);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_cherry_pick_post_move_edit(const svn_test_opts_t *opts,
                                apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));
  const char *trunk_url;
  svn_opt_revision_t peg_rev;
  apr_array_header_t *ranges_to_merge;
  svn_opt_revision_range_t merge_range;
  svn_client_ctx_t *ctx;
  svn_client_conflict_t *conflict;
  svn_boolean_t tree_conflicted;
  svn_stringbuf_t *buf;

  SVN_ERR(svn_test__sandbox_create(b,
                                   "test_cherry_pick_post_move_edit",
                                   opts, pool));

  SVN_ERR(sbox_add_and_commit_greek_tree(b)); /* r1 */
  /* Create a copy of node "A". */
  SVN_ERR(sbox_wc_copy(b, "A", "A1"));
  SVN_ERR(sbox_wc_commit(b, "")); /* r2 */
  /* On "trunk", move the file mu. */
  SVN_ERR(sbox_wc_move(b, "A/mu", "A/mu-moved"));
  SVN_ERR(sbox_wc_commit(b, "")); /* r3 */
  /* On "trunk", edit mu-moved. This will be r4. */
  SVN_ERR(sbox_file_write(b, "A/mu-moved", "Modified content." APR_EOL_STR));
  SVN_ERR(sbox_wc_commit(b, "")); /* r4 */
  /* On "trunk", edit mu-moved. This will be r5, which we'll cherry-pick. */
  SVN_ERR(sbox_file_write(b, "A/mu-moved",
                          "More modified content." APR_EOL_STR));
  SVN_ERR(sbox_wc_commit(b, "")); /* r5 */
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));

  /* Perform a cherry-pick merge of r5 from A to A1. */
  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  trunk_url = apr_pstrcat(b->pool, b->repos_url, "/A", SVN_VA_NULL);
  peg_rev.kind = svn_opt_revision_number;
  peg_rev.value.number = 5;
  merge_range.start.kind = svn_opt_revision_number;
  merge_range.start.value.number = 4;
  merge_range.end.kind = svn_opt_revision_number;
  merge_range.end.value.number = 5;
  ranges_to_merge = apr_array_make(b->pool, 1,
                                   sizeof(svn_opt_revision_range_t *));
  APR_ARRAY_PUSH(ranges_to_merge, svn_opt_revision_range_t *) = &merge_range;
  /* This should raise a "local delete or move vs incoming edit" conflict. */
  SVN_ERR(svn_client_merge_peg5(trunk_url, ranges_to_merge, &peg_rev,
                                sbox_wc_path(b, "A1"), svn_depth_infinity,
                                FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                                NULL, ctx, b->pool));

  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, "A1/mu-moved"),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_get_conflicted(NULL, NULL, &tree_conflicted,
                                             conflict, b->pool, b->pool));
  SVN_TEST_ASSERT(tree_conflicted);
  {
    svn_client_conflict_option_id_t expected_opts[] = {
      svn_client_conflict_option_postpone,
      svn_client_conflict_option_accept_current_wc_state,
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
      svn_client_conflict_option_local_move_file_text_merge,
      -1 /* end of list */
    };
    SVN_ERR(assert_tree_conflict_options(conflict, ctx, expected_opts,
                                         b->pool));
  }

  /* Try to resolve the conflict. */
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(
            conflict,
            svn_client_conflict_option_local_move_file_text_merge,
            ctx, b->pool));

  /* The node "A1/mu-moved" should no longer exist. */
  SVN_TEST_ASSERT_ERROR(svn_client_conflict_get(&conflict,
                                                sbox_wc_path(b, "A1/mu-moved"),
                                                ctx, pool, pool),
                        SVN_ERR_WC_PATH_NOT_FOUND);

  /* And "A1/mu" should have expected contents. */
  SVN_ERR(svn_stringbuf_from_file2(&buf, sbox_wc_path(b, "A1/mu"), pool));
  SVN_TEST_STRING_ASSERT(buf->data, "More modified content." APR_EOL_STR);

  return SVN_NO_ERROR;
}

/* A helper function which prepares a working copy for the tests below. */
static svn_error_t *
create_wc_with_incoming_delete_dir_conflict_across_branches(
  svn_test__sandbox_t *b)
{
  svn_client_ctx_t *ctx;
  const char *trunk_url;
  const char *branch_url;
  svn_opt_revision_t opt_rev;
  const char *deleted_path;
  const char *deleted_child_path;
  const char *move_target_path;

  SVN_ERR(sbox_add_and_commit_greek_tree(b));

  /* Create a branch of node "A". */
  SVN_ERR(sbox_wc_copy(b, trunk_path, branch_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  /* Create a second branch ("branch2") of the first branch. */
  SVN_ERR(sbox_wc_copy(b, branch_path, branch2_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  /* Move a directory on the trunk. */
  deleted_path = svn_relpath_join(trunk_path, deleted_dir_name, b->pool);
  move_target_path = svn_relpath_join(trunk_path, new_dir_name, b->pool);
  SVN_ERR(sbox_wc_move(b, deleted_path, move_target_path));
  SVN_ERR(sbox_wc_commit(b, ""));

  /* Modify a file in that directory on branch2. */
  deleted_child_path = svn_relpath_join(branch2_path,
                                        svn_relpath_join(deleted_dir_name,
                                                         deleted_dir_child,
                                                         b->pool),
                                        b->pool);
  SVN_ERR(sbox_file_write(b, deleted_child_path,
                          modified_file_on_branch_content));

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  opt_rev.kind = svn_opt_revision_head;
  opt_rev.value.number = SVN_INVALID_REVNUM;
  trunk_url = apr_pstrcat(b->pool, b->repos_url, "/", trunk_path,
                          SVN_VA_NULL);
  branch_url = apr_pstrcat(b->pool, b->repos_url, "/", branch_path,
                          SVN_VA_NULL);

  /* Commit modification and run a merge from the trunk to the branch.
   * This merge should not raise a conflict. */
  SVN_ERR(sbox_wc_commit(b, ""));
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));
  SVN_ERR(svn_client_merge_peg5(trunk_url, NULL, &opt_rev,
                                sbox_wc_path(b, branch_path),
                                svn_depth_infinity,
                                FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                                NULL, ctx, b->pool));

  /* Commit merge result end run a merge from branch to branch2. */
  SVN_ERR(sbox_wc_commit(b, ""));
  SVN_ERR(sbox_wc_update(b, "", SVN_INVALID_REVNUM));

  /* This should raise an "incoming delete vs local edit" tree conflict. */
  SVN_ERR(svn_client_merge_peg5(branch_url, NULL, &opt_rev,
                                sbox_wc_path(b, branch2_path),
                                svn_depth_infinity,
                                FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                                NULL, ctx, b->pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
test_merge_incoming_move_dir_across_branches(const svn_test_opts_t *opts,
                                             apr_pool_t *pool)
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
  apr_array_header_t *options;
  svn_client_conflict_option_t *option;
  apr_array_header_t *possible_moved_to_abspaths;

  SVN_ERR(svn_test__sandbox_create(b,
                                   "merge_incoming_move_dir accross branches",
                                   opts, pool));

  SVN_ERR(create_wc_with_incoming_delete_dir_conflict_across_branches(b));

  deleted_path = svn_relpath_join(branch2_path, deleted_dir_name, b->pool);
  moved_to_path = svn_relpath_join(branch2_path, new_dir_name, b->pool);

  SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
  SVN_ERR(svn_client_conflict_get(&conflict, sbox_wc_path(b, deleted_path),
                                  ctx, b->pool, b->pool));
  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, b->pool));

  SVN_ERR_ASSERT(svn_client_conflict_get_local_change(conflict) ==
                 svn_wc_conflict_reason_edited);

  /* Check possible move destinations for the directory. */
  SVN_ERR(svn_client_conflict_tree_get_resolution_options(&options, conflict,
                                                          ctx, b->pool,
                                                          b->pool));
  option = svn_client_conflict_option_find_by_id(
             options, svn_client_conflict_option_incoming_move_dir_merge);
  SVN_TEST_ASSERT(option != NULL);

  SVN_ERR(svn_client_conflict_option_get_moved_to_abspath_candidates(
            &possible_moved_to_abspaths, option, b->pool, b->pool));

  /* The resolver finds two possible destinations for the moved folder:
   *
   *   Possible working copy destinations for moved-away 'A_branch/B' are:
   *    (1): 'A_branch2/newdir'
   *    (2): 'A_branch/newdir'
   *   Only one destination can be a move; the others are copies.
   */
  SVN_TEST_INT_ASSERT(possible_moved_to_abspaths->nelts, 2);
  SVN_TEST_STRING_ASSERT(
    APR_ARRAY_IDX(possible_moved_to_abspaths, 0, const char *),
    sbox_wc_path(b, moved_to_path));
  SVN_TEST_STRING_ASSERT(
    APR_ARRAY_IDX(possible_moved_to_abspaths, 1, const char *),
    sbox_wc_path(b, svn_relpath_join(branch_path, new_dir_name, b->pool)));

  /* Resolve the tree conflict. */
  SVN_ERR(svn_client_conflict_option_set_moved_to_abspath(option, 0,
                                                          ctx, b->pool));
  SVN_ERR(svn_client_conflict_tree_resolve(conflict, option, ctx, b->pool));

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

/* ========================================================================== */


static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_file_text_merge,
                       "merge incoming add file text merge"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_file_replace_and_merge,
                       "merge incoming add file replace and merge"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_dir_ignore,
                       "merge incoming add dir ignore"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_dir_merge,
                       "merge incoming add dir merge"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_dir_merge2,
                       "merge incoming add dir merge with file change"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_dir_merge3,
                       "merge incoming add dir merge with move history"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_dir_replace,
                       "merge incoming add dir replace"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_dir_replace_and_merge,
                       "merge incoming add dir replace and merge"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_added_dir_replace_and_merge2,
                       "merge incoming add dir replace with file change"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_delete_file_ignore,
                       "merge incoming delete file ignore"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_delete_file_accept,
                       "merge incoming delete file accept"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_move_file_text_merge,
                       "merge incoming move file text merge"),
    SVN_TEST_OPTS_PASS(test_update_incoming_delete_file_ignore,
                       "update incoming delete file ignore"),
    SVN_TEST_OPTS_PASS(test_update_incoming_delete_file_accept,
                       "update incoming delete file accept"),
    SVN_TEST_OPTS_PASS(test_update_incoming_move_file_text_merge,
                       "update incoming move file text merge"),
    SVN_TEST_OPTS_PASS(test_switch_incoming_move_file_text_merge,
                       "switch incoming move file text merge"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_move_dir,
                       "merge incoming move dir"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_move_dir2,
                       "merge incoming move dir with local edit"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_move_dir3,
                       "merge incoming move dir with local add"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_delete_vs_local_delete,
                       "merge incoming delete vs local delete"),
    SVN_TEST_OPTS_PASS(test_merge_file_prop,
                       "merge file property"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_move_file_text_merge_conflict,
                       "merge incoming move file merge with text conflict"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_edit_file_moved_away,
                       "merge incoming edit for a moved-away working file"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_chained_move_local_edit,
                       "merge incoming chained move vs local edit"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_move_dir_with_moved_file,
                       "merge incoming moved dir with moved file"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_file_move_new_line_of_history,
                       "merge incoming file move with new line of history"),
    SVN_TEST_OPTS_PASS(test_update_incoming_dir_move_with_nested_file_move,
                       "update incoming dir move with nested file move"),
    SVN_TEST_OPTS_PASS(test_update_incoming_dir_move_with_parent_move,
                       "update incoming dir move with parent move"),
    SVN_TEST_OPTS_PASS(test_update_incoming_dir_move_with_parent_moved_back,
                       "update incoming dir move with parent moved back"),
    SVN_TEST_OPTS_PASS(test_update_incoming_dir_move_with_parent_moved_twice,
                       "update incoming dir move with parent moved twice"),
    SVN_TEST_OPTS_PASS(test_update_incoming_added_file_text_merge,
                       "update incoming add file text merge"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_move_file_prop_merge_conflict,
                       "merge incoming move file merge with prop conflict"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_move_file_text_merge_keywords,
                       "merge incoming move file merge with keywords"),
    SVN_TEST_OPTS_PASS(test_update_incoming_added_dir_ignore,
                       "update incoming add dir ignore"),
    SVN_TEST_OPTS_PASS(test_update_incoming_added_dir_merge,
                       "update incoming add dir merge"),
    SVN_TEST_OPTS_PASS(test_update_incoming_added_dir_merge2,
                       "update incoming add dir merge with obstructions"),
    SVN_TEST_OPTS_PASS(test_cherry_pick_moved_file_with_propdel,
                       "cherry-pick with moved file and propdel"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_move_file_text_merge_crlf,
                       "merge incoming move file merge with CRLF eols"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_move_file_text_merge_native_eol,
                       "merge incoming move file merge with native eols"),
    SVN_TEST_OPTS_XFAIL(test_cherry_pick_post_move_edit,
                        "cherry-pick edit from moved file"),
    SVN_TEST_OPTS_PASS(test_merge_incoming_move_dir_across_branches,
                        "merge incoming dir move across branches"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
