/*
 * op-depth-test.c :  test layered tree changes
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

/* To avoid warnings... */
#define SVN_DEPRECATED

#include <apr_pools.h>
#include <apr_general.h>

#include "svn_types.h"
#include "svn_hash.h"
#include "svn_io.h"
#include "svn_dirent_uri.h"
#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_hash.h"
#include "svn_sorts.h"

#include "utils.h"

#include "private/svn_dep_compat.h"
#include "private/svn_sorts_private.h"
#include "private/svn_sqlite.h"
#include "private/svn_wc_private.h"
#include "../../libsvn_wc/wc.h"
#include "../../libsvn_wc/wc_db.h"
#include "../../libsvn_wc/workqueue.h"
#include "../../libsvn_wc/conflicts.h"
#define SVN_WC__I_AM_WC_DB
#include "../../libsvn_wc/wc_db_private.h"

#include "../svn_test.h"

#include "wc-test-queries.h"

WC_TEST_QUERIES_SQL_DECLARE_STATEMENTS(op_depth_statements);

/* Compare strings, like strcmp but either or both may be NULL which
 * compares equal to NULL and not equal to any non-NULL string. */
static int
strcmp_null(const char *s1, const char *s2)
{
  if (s1 && s2)
    return strcmp(s1, s2);
  else if (s1 || s2)
    return 1;
  else
    return 0;
}


/* ---------------------------------------------------------------------- */
/* Reading the WC DB */

static svn_error_t *
open_wc_db(svn_sqlite__db_t **sdb,
           const char *wc_root_abspath,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__db_util_open_db(sdb, wc_root_abspath, "wc.db",
                                  svn_sqlite__mode_readwrite,
                                  FALSE /* exclusive */, 0 /* timeout */,
                                  op_depth_statements,
                                  result_pool, scratch_pool));
  return SVN_NO_ERROR;
}


/* ---------------------------------------------------------------------- */
/* Functions for comparing expected and found WC DB data. */

/* Some of the fields from a NODES table row. */
typedef struct nodes_row_t {
    int op_depth;
    const char *local_relpath;
    const char *presence;
    svn_revnum_t repo_revnum;
    const char *repo_relpath;
    svn_boolean_t file_external;
    const char *moved_to;
    svn_boolean_t moved_here;
    const char *props;  /* comma-separated list of prop names */
} nodes_row_t;

/* Tree conflict details */
typedef struct tree_conflict_info
{
  svn_wc_conflict_action_t action;
  svn_wc_conflict_reason_t reason;
  const char *delete_path;
  svn_boolean_t conflicted_fb; /* fallback for reason, action and path 0 */
} tree_conflict_info;

/* What conflicts are on a path. */
typedef struct conflict_info_t {
    const char *local_relpath;
    svn_boolean_t text_conflicted;
    svn_boolean_t prop_conflicted;

    tree_conflict_info tc;
} conflict_info_t;

/* Macro for filling in the REPO_* fields of a non-base NODES_ROW_T
 * that has no copy-from info. */
#define NO_COPY_FROM SVN_INVALID_REVNUM, NULL, FALSE
#define MOVED_HERE FALSE, NULL, TRUE
#define NOT_MOVED  FALSE, NULL, FALSE
#define FILE_EXTERNAL TRUE

/* Return a comma-separated list of the prop names in PROPS, in lexically
 * ascending order, or NULL if PROPS is empty or NULL.  (Here, we don't
 * care about the difference between 'has no props' and 'can't have props',
 * and we choose to represent both of those as NULL.) */
static const char *
props_hash_to_text(apr_hash_t *props, apr_pool_t *pool)
{
  apr_array_header_t *props_sorted;
  svn_stringbuf_t *str;
  int i;

  if (! props)
    return NULL;

  str = svn_stringbuf_create_empty(pool);
  props_sorted = svn_sort__hash(props, svn_sort_compare_items_lexically, pool);
  for (i = 0; i < props_sorted->nelts; i++)
    {
      const svn_sort__item_t *item
        = &APR_ARRAY_IDX(props_sorted, i, svn_sort__item_t);

      if (str->len)
        svn_stringbuf_appendbyte(str, ',');
      svn_stringbuf_appendcstr(str, item->key);
    }
  return str->len ? str->data : NULL;
}

/* Return a human-readable string representing ROW. With a tiny bit of editting
   this can be used to create expected results */
static const char *
print_row(const nodes_row_t *row,
          apr_pool_t *result_pool)
{
  const char *relpath_str, *presence_str;
  const char *file_external_str, *moved_here_str, *moved_to_str, *props;

  if (row == NULL)
    return "(null)";

  relpath_str = apr_psprintf(result_pool, "\"%s\",", row->local_relpath);
  presence_str = apr_psprintf(result_pool, "\"%s\",", row->presence);
  if (row->moved_to)
    moved_to_str = apr_psprintf(result_pool, ", \"%s\"", row->moved_to);
  else
    moved_to_str = "";

  if (row->moved_here && !row->file_external && !row->moved_to)
    moved_here_str = ", MOVED_HERE";
  else if (row->moved_to)
    moved_here_str = ", TRUE";
  else
    moved_here_str = "";

  if (row->file_external)
    file_external_str = ", FILE_EXTERNAL";
  else if (row->moved_to || row->props)
    file_external_str = ", FALSE";
  else
    file_external_str = "";

  if (row->props)
    props = apr_psprintf(result_pool, ", p=(%s)", row->props);
  else
    props = "";

  if (row->repo_revnum == SVN_INVALID_REVNUM)
    return apr_psprintf(result_pool, "%d, %-20s%-15s NO_COPY_FROM%s%s%s%s",
                        row->op_depth, relpath_str, presence_str,
                        file_external_str, moved_here_str, moved_to_str,
                        props);
  else
    return apr_psprintf(result_pool, "%d, %-20s%-15s %d, \"%s\"%s%s%s%s",
                        row->op_depth, relpath_str, presence_str,
                        (int)row->repo_revnum, row->repo_relpath,
                        file_external_str, moved_here_str, moved_to_str,
                        props);
}
/* A baton to pass through svn_hash_diff() to compare_nodes_rows(). */
typedef struct comparison_baton_t {
    apr_hash_t *expected_hash;  /* Maps "OP_DEPTH PATH" to nodes_row_t. */
    apr_hash_t *found_hash;     /* Maps "OP_DEPTH PATH" to nodes_row_t. */
    apr_pool_t *scratch_pool;
    svn_error_t *errors;        /* Chain of errors found in comparison. */
} comparison_baton_t;

/* Compare two hash entries indexed by KEY, in the two hashes in BATON.
 * Append an error message to BATON->errors if they differ or are not both
 * present.
 *
 * Implements svn_hash_diff_func_t. */
static svn_error_t *
compare_nodes_rows(const void *key, apr_ssize_t klen,
                   enum svn_hash_diff_key_status status,
                   void *baton)
{
  comparison_baton_t *b = baton;
  nodes_row_t *expected = apr_hash_get(b->expected_hash, key, klen);
  nodes_row_t *found = apr_hash_get(b->found_hash, key, klen);

  if (! expected)
    {
      b->errors = svn_error_createf(
                    SVN_ERR_TEST_FAILED, b->errors,
                    "found   {%s}",
                    print_row(found, b->scratch_pool));
    }
  else if (! found)
    {
      b->errors = svn_error_createf(
                    SVN_ERR_TEST_FAILED, b->errors,
                    "expected {%s}",
                    print_row(expected, b->scratch_pool));
    }
  else if (expected->repo_revnum != found->repo_revnum
           || (strcmp_null(expected->repo_relpath, found->repo_relpath) != 0)
           || (strcmp_null(expected->presence, found->presence) != 0)
           || (expected->file_external != found->file_external)
           || (expected->moved_here != found->moved_here)
           || (expected->moved_to && !found->moved_to)
           || (!expected->moved_to && found->moved_to)
           || (expected->moved_to
               && strcmp(expected->moved_to, found->moved_to))
           || (expected->props != NULL
               && strcmp_null(expected->props, found->props) != 0))
    {
      b->errors = svn_error_createf(
                    SVN_ERR_TEST_FAILED, b->errors,
                    "expected {%s}; found {%s}",
                    print_row(expected, b->scratch_pool),
                    print_row(found, b->scratch_pool));
    }

  /* Don't terminate the comparison: accumulate all differences. */
  return SVN_NO_ERROR;
}


/* Examine the WC DB for paths ROOT_PATH and below, and check that their
 * rows in the 'NODES' table (only those at op_depth > 0) match EXPECTED_ROWS
 * (which is terminated by a row of null fields).
 *
 * Return a chain of errors describing any and all mismatches. */
static svn_error_t *
check_db_rows(svn_test__sandbox_t *b,
              const char *root_path,
              const nodes_row_t *expected_rows)
{
  svn_sqlite__db_t *sdb;
  int i;
  svn_sqlite__stmt_t *stmt;

  svn_boolean_t have_row;
  apr_hash_t *found_hash = apr_hash_make(b->pool);
  apr_hash_t *expected_hash = apr_hash_make(b->pool);
  comparison_baton_t comparison_baton;

  comparison_baton.expected_hash = expected_hash;
  comparison_baton.found_hash = found_hash;
  comparison_baton.scratch_pool = b->pool;
  comparison_baton.errors = NULL;

  /* Fill ACTUAL_HASH with data from the WC DB. */
  SVN_ERR(open_wc_db(&sdb, b->wc_abspath, b->pool, b->pool));
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_NODES_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", (apr_int64_t)1 /* wc_id */,
                                        root_path));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *key;
      nodes_row_t *row = apr_palloc(b->pool, sizeof(*row));
      apr_hash_t *props_hash;

      row->op_depth = svn_sqlite__column_int(stmt, 0);
      row->presence = svn_sqlite__column_text(stmt, 1, b->pool);
      row->local_relpath = svn_sqlite__column_text(stmt, 2, b->pool);
      row->repo_revnum = svn_sqlite__column_revnum(stmt, 3);
      row->repo_relpath = svn_sqlite__column_text(stmt, 4, b->pool);
      row->file_external = !svn_sqlite__column_is_null(stmt, 5);
      row->moved_to = svn_sqlite__column_text(stmt, 7, b->pool);
      row->moved_here = svn_sqlite__column_boolean(stmt, 8);
      SVN_ERR(svn_sqlite__column_properties(&props_hash, stmt, 9,
                                            b->pool, b->pool));
      row->props = props_hash_to_text(props_hash, b->pool);

      if (row->file_external && svn_sqlite__column_is_null(stmt, 6))
        comparison_baton.errors
          = svn_error_createf(SVN_ERR_TEST_FAILED, comparison_baton.errors,
                              "incomplete {%s}", print_row(row, b->pool));

      key = apr_psprintf(b->pool, "%d %s", row->op_depth, row->local_relpath);
      apr_hash_set(found_hash, key, APR_HASH_KEY_STRING, row);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  /* Fill EXPECTED_HASH with data from EXPECTED_ROWS. */
  for (i = 0; expected_rows[i].local_relpath != NULL; i++)
    {
      const char *key;
      const nodes_row_t *row = &expected_rows[i];

      key = apr_psprintf(b->pool, "%d %s", row->op_depth, row->local_relpath);
      apr_hash_set(expected_hash, key, APR_HASH_KEY_STRING, row);
    }

  /* Compare EXPECTED_HASH with ACTUAL_HASH and return any errors. */
  SVN_ERR(svn_hash_diff(expected_hash, found_hash,
                        compare_nodes_rows, &comparison_baton, b->pool));
  SVN_ERR(svn_sqlite__close(sdb));
  return comparison_baton.errors;
}

#define EDIT_EDIT_TC {svn_wc_conflict_reason_edited, \
                      svn_wc_conflict_action_edit, \
                      NULL, TRUE}
#define NO_TC { 0 }
static const char *
print_conflict(const conflict_info_t *row,
               apr_pool_t *result_pool)
{
  const char *tc_text;

  if (!row->tc.reason && !row->tc.action && !row->tc.delete_path)
    {
      if (row->tc.conflicted_fb)
        tc_text = "EDIT_EDIT_TC";
      else
        tc_text = "NO_TC";
    }
  else
    {
      const char *action;
      const char *reason;
      const char *path;

#define CASE_ENUM_STRVAL(x, y) case y: x = #y; break
      switch(row->tc.action)
        {
          CASE_ENUM_STRVAL(action, svn_wc_conflict_action_edit);
          CASE_ENUM_STRVAL(action, svn_wc_conflict_action_add);
          CASE_ENUM_STRVAL(action, svn_wc_conflict_action_delete);
          CASE_ENUM_STRVAL(action, svn_wc_conflict_action_replace);
          default:
            SVN_ERR_MALFUNCTION_NO_RETURN();
        }
      switch(row->tc.reason)
        {
          CASE_ENUM_STRVAL(reason, svn_wc_conflict_reason_edited);
          CASE_ENUM_STRVAL(reason, svn_wc_conflict_reason_obstructed);
          CASE_ENUM_STRVAL(reason, svn_wc_conflict_reason_deleted);
          CASE_ENUM_STRVAL(reason, svn_wc_conflict_reason_missing);
          CASE_ENUM_STRVAL(reason, svn_wc_conflict_reason_unversioned);
          CASE_ENUM_STRVAL(reason, svn_wc_conflict_reason_added);
          CASE_ENUM_STRVAL(reason, svn_wc_conflict_reason_replaced);
          CASE_ENUM_STRVAL(reason, svn_wc_conflict_reason_moved_away);
          CASE_ENUM_STRVAL(reason, svn_wc_conflict_reason_moved_here);
          default:
            SVN_ERR_MALFUNCTION_NO_RETURN();
        }

      if (row->tc.delete_path)
        path = apr_psprintf(result_pool, ", \"%s\"", row->tc.delete_path);
      else
        path = "";

      tc_text = apr_psprintf(result_pool, "{%s, %s%s}", action,
                             reason, path);
    }

  return apr_psprintf(result_pool, "\"%s\", %s, %s, %s",
                      row->local_relpath,
                      row->text_conflicted ? "TRUE" : "FALSE",
                      row->prop_conflicted ? "TRUE" : "FALSE",
                      tc_text);
}

static svn_boolean_t
tree_conflicts_match(const tree_conflict_info *expected,
                     const tree_conflict_info *actual)
{
    if (expected->action != actual->action)
      return FALSE;
    else if (expected->reason != actual->reason)
      return FALSE;
    else if (strcmp_null(expected->delete_path, actual->delete_path) != 0)
      return FALSE;
    else if (expected->conflicted_fb != actual->conflicted_fb)
      return FALSE;

    return TRUE;
}

static svn_error_t *
compare_conflict_info(const void *key, apr_ssize_t klen,
                      enum svn_hash_diff_key_status status,
                      void *baton)
{
  comparison_baton_t *b = baton;
  conflict_info_t *expected = apr_hash_get(b->expected_hash, key, klen);
  conflict_info_t *found = apr_hash_get(b->found_hash, key, klen);

  if (! expected)
    {
      b->errors = svn_error_createf(
                    SVN_ERR_TEST_FAILED, b->errors,
                    "found   {%s}",
                    print_conflict(found, b->scratch_pool));
    }
  else if (! found)
    {
      b->errors = svn_error_createf(
                    SVN_ERR_TEST_FAILED, b->errors,
                    "expected {%s}",
                    print_conflict(expected, b->scratch_pool));
    }
  else if (expected->text_conflicted != found->text_conflicted
           || expected->prop_conflicted != found->prop_conflicted
           || !tree_conflicts_match(&expected->tc, &found->tc))
    {
      b->errors = svn_error_createf(
                    SVN_ERR_TEST_FAILED, b->errors,
                    "expected {%s}; found {%s}",
                    print_conflict(expected, b->scratch_pool),
                    print_conflict(found, b->scratch_pool));
    }

  /* Don't terminate the comparison: accumulate all differences. */
  return SVN_NO_ERROR;
}

static svn_error_t *
check_db_conflicts(svn_test__sandbox_t *b,
                   const char *root_path,
                   const conflict_info_t *expected_conflicts)
{
  svn_sqlite__db_t *sdb;
  int i;
  svn_sqlite__stmt_t *stmt;

  svn_boolean_t have_row;
  apr_hash_t *found_hash = apr_hash_make(b->pool);
  apr_hash_t *expected_hash = apr_hash_make(b->pool);
  apr_pool_t *iterpool = svn_pool_create(b->pool);
  apr_hash_index_t *hi;
  comparison_baton_t comparison_baton;

  comparison_baton.expected_hash = expected_hash;
  comparison_baton.found_hash = found_hash;
  comparison_baton.scratch_pool = b->pool;
  comparison_baton.errors = NULL;

  /* Fill ACTUAL_HASH with data from the WC DB. */
  SVN_ERR(open_wc_db(&sdb, b->wc_abspath, b->pool, b->pool));
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_ACTUAL_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", (apr_int64_t)1 /* wc_id */,
                                        root_path));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      conflict_info_t *row = apr_pcalloc(b->pool, sizeof(*row));

      row->local_relpath = svn_sqlite__column_text(stmt, 0, b->pool);

      svn_hash_sets(found_hash, row->local_relpath, row);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));
  SVN_ERR(svn_sqlite__close(sdb));

  for (hi = apr_hash_first(b->pool, found_hash); hi; hi = apr_hash_next(hi))
    {
      svn_skel_t *conflict;
      conflict_info_t *info = apr_hash_this_val(hi);
      const char *local_abspath;
      svn_boolean_t tree_conflicted;

      svn_pool_clear(iterpool);

      local_abspath = svn_dirent_join(b->wc_abspath, info->local_relpath,
                                      iterpool);

      SVN_ERR(svn_wc__db_read_conflict(&conflict, NULL, NULL,
                                       b->wc_ctx->db, local_abspath,
                                       iterpool, iterpool));

      SVN_TEST_ASSERT(conflict != NULL);

      SVN_ERR(svn_wc__conflict_read_info(NULL, NULL,
                                         &info->text_conflicted,
                                         &info->prop_conflicted,
                                         &tree_conflicted,
                                         b->wc_ctx->db, local_abspath,
                                         conflict,
                                         iterpool, iterpool));

      if (tree_conflicted)
        {
          const char *move_src_abspath;
          SVN_ERR(svn_wc__conflict_read_tree_conflict(&info->tc.reason,
                                                      &info->tc.action,
                                                      &move_src_abspath,
                                                      b->wc_ctx->db,
                                                      local_abspath,
                                                      conflict,
                                                      b->pool, iterpool));

          if (move_src_abspath)
            info->tc.delete_path =
                svn_dirent_skip_ancestor(b->wc_abspath, move_src_abspath);

          if (!info->tc.reason
              && !info->tc.action
              && !info->tc.delete_path)
            {
              info->tc.conflicted_fb = TRUE;
            }
        }
    }

  /* Fill EXPECTED_HASH with data from EXPECTED_ROWS. */
  if (expected_conflicts)
    for (i = 0; expected_conflicts[i].local_relpath != NULL; i++)
      {
        const conflict_info_t *row = &expected_conflicts[i];

        svn_hash_sets(expected_hash, row->local_relpath, row);
      }

  /* Compare EXPECTED_HASH with ACTUAL_HASH and return any errors. */
  SVN_ERR(svn_hash_diff(expected_hash, found_hash,
                        compare_conflict_info, &comparison_baton, b->pool));
  return comparison_baton.errors;
}

static svn_error_t *
verify_db_callback(void *baton,
                   const char *wc_abspath,
                   const char *local_relpath,
                   int op_depth,
                   int id,
                   const char *msg,
                   apr_pool_t *scratch_pool)
{
  if (op_depth >= 0)
    return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                "Verify: %s: %s (%d): SV%04d %s",
                wc_abspath, local_relpath, op_depth, id, msg);
  else
    return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                "DB-VRFY: %s: %s: SV%04d %s",
                wc_abspath, local_relpath, id, msg);
}

static svn_error_t *
verify_db(svn_test__sandbox_t *b)
{
  SVN_ERR(svn_wc__db_verify_db_full(b->wc_ctx->db, b->wc_abspath,
                                    verify_db_callback, NULL, b->pool));

  return SVN_NO_ERROR;
}


/* ---------------------------------------------------------------------- */
/* The test functions */

/* Definition of a copy sub-test and its expected results. */
struct copy_subtest_t
{
  /* WC-relative or repo-relative source and destination paths. */
  const char *from_path;
  const char *to_path;
  /* All the expected nodes table rows within the destination sub-tree.
   * Terminated by an all-zero row. */
  nodes_row_t expected[20];
};

#define source_everything   "A/B"

#define source_base_file    "A/B/lambda"
#define source_base_dir     "A/B/E"

#define source_added_file   "A/B/file-added"
#define source_added_dir    "A/B/D-added"
#define source_added_dir2   "A/B/D-added/D2"

#define source_copied_file  "A/B/lambda-copied"
#define source_copied_dir   "A/B/E-copied"

/* Check that all kinds of WC-to-WC copies give correct op_depth results:
 * create a Greek tree, make copies in it, and check the resulting DB rows. */
static svn_error_t *
wc_wc_copies(svn_test__sandbox_t *b)
{
  SVN_ERR(sbox_add_and_commit_greek_tree(b));

  /* Create the various kinds of source node which will be copied */

  SVN_ERR(sbox_file_write(b, source_added_file, "New file"));
  SVN_ERR(sbox_wc_add(b, source_added_file));
  SVN_ERR(sbox_wc_mkdir(b, source_added_dir));
  SVN_ERR(sbox_wc_mkdir(b, source_added_dir2));

  SVN_ERR(sbox_wc_copy(b, source_base_file, source_copied_file));
  SVN_ERR(sbox_wc_copy(b, source_base_dir, source_copied_dir));

  /* Delete some nodes so that we can test copying onto these paths */

  SVN_ERR(sbox_wc_delete(b, "A/D/gamma"));
  SVN_ERR(sbox_wc_delete(b, "A/D/G"));

  /* Test copying various things */

  {
    struct copy_subtest_t subtests[] =
      {
        /* base file */
        { source_base_file, "A/C/copy1", {
            { 3, "",                "normal",   1, source_base_file }
          } },

        /* base dir */
        { source_base_dir, "A/C/copy2", {
            { 3, "",                "normal",   1, source_base_dir },
            { 3, "alpha",           "normal",   1, "A/B/E/alpha" },
            { 3, "beta",            "normal",   1, "A/B/E/beta" }
          } },

        /* added file */
        { source_added_file, "A/C/copy3", {
            { 3, "",                "normal",   NO_COPY_FROM }
          } },

        /* added dir */
        { source_added_dir, "A/C/copy4", {
            { 3, "",                "normal",   NO_COPY_FROM },
            { 4, "D2",              "normal",   NO_COPY_FROM }
          } },

        /* copied file */
        { source_copied_file, "A/C/copy5", {
            { 3, "",                "normal",   1, source_base_file }
          } },

        /* copied dir */
        { source_copied_dir, "A/C/copy6", {
            { 3, "",                "normal",   1, source_base_dir },
            { 3, "alpha",           "normal",   1, "A/B/E/alpha" },
            { 3, "beta",            "normal",   1, "A/B/E/beta" }
          } },

        /* copied tree with everything in it */
        { source_everything, "A/C/copy7", {
            { 3, "",                "normal",   1, source_everything },
            { 3, "lambda",          "normal",   1, "A/B/lambda" },
            { 3, "E",               "normal",   1, "A/B/E" },
            { 3, "E/alpha",         "normal",   1, "A/B/E/alpha" },
            { 3, "E/beta",          "normal",   1, "A/B/E/beta" },
            { 3, "F",               "normal",   1, "A/B/F" },
            /* Each add is an op_root */
            { 4, "file-added",      "normal",   NO_COPY_FROM },
            { 4, "D-added",         "normal",   NO_COPY_FROM },
            { 5, "D-added/D2",      "normal",   NO_COPY_FROM },
            /* Each copied-copy subtree is an op_root */
            { 4, "lambda-copied",   "normal",   1, source_base_file },
            { 4, "E-copied",        "normal",   1, source_base_dir },
            { 4, "E-copied/alpha",  "normal",   1, "A/B/E/alpha" },
            { 4, "E-copied/beta",   "normal",   1, "A/B/E/beta" }
          } },

        /* dir onto a schedule-delete file */
        { source_base_dir, "A/D/gamma", {
            { 0, "",                "normal",   1, "A/D/gamma" },
            { 3, "",                "normal",   1, source_base_dir },
            { 3, "alpha",           "normal",   1, "A/B/E/alpha" },
            { 3, "beta",            "normal",   1, "A/B/E/beta" }
          } },

        /* file onto a schedule-delete dir */
        { source_base_file, "A/D/G", {
            { 0, "",                "normal",   1, "A/D/G" },
            { 0, "pi",              "normal",   1, "A/D/G/pi" },
            { 0, "rho",             "normal",   1, "A/D/G/rho" },
            { 0, "tau",             "normal",   1, "A/D/G/tau" },
            { 3, "",                "normal",   1, source_base_file },
            { 3, "pi",              "base-deleted",   NO_COPY_FROM },
            { 3, "rho",             "base-deleted",   NO_COPY_FROM },
            { 3, "tau",             "base-deleted",   NO_COPY_FROM }
          } },

        { 0 }
      };
    struct copy_subtest_t *subtest;

    /* Fix up the expected->local_relpath fields in the subtest data to be
     * relative to the WC root rather than to the copy destination dir. */
    for (subtest = subtests; subtest->from_path; subtest++)
      {
        nodes_row_t *row;
        for (row = &subtest->expected[0]; row->local_relpath; row++)
          row->local_relpath = svn_dirent_join(subtest->to_path,
                                               row->local_relpath, b->pool);
      }

    /* Perform each subtest in turn. */
    for (subtest = subtests; subtest->from_path; subtest++)
      {
        SVN_ERR(sbox_wc_copy(b, subtest->from_path, subtest->to_path));
        SVN_ERR(check_db_rows(b, subtest->to_path, subtest->expected));
      }
  }

  return SVN_NO_ERROR;
}

/* Check that all kinds of repo-to-WC copies give correct op_depth results:
 * create a Greek tree, make copies in it, and check the resulting DB rows. */
static svn_error_t *
repo_wc_copies(svn_test__sandbox_t *b)
{
  SVN_ERR(sbox_add_and_commit_greek_tree(b));

  /* Delete some nodes so that we can test copying onto these paths */

  SVN_ERR(sbox_wc_delete(b, "A/B/lambda"));
  SVN_ERR(sbox_wc_delete(b, "A/D/gamma"));
  SVN_ERR(sbox_wc_delete(b, "A/D/G"));
  SVN_ERR(sbox_wc_delete(b, "A/D/H"));

  /* Test copying various things */

  {
    struct copy_subtest_t subtests[] =
      {
        /* file onto nothing */
        { "iota", "A/C/copy1", {
            { 3, "",                "normal",       1, "iota" },
          } },

        /* dir onto nothing */
        { "A/B/E", "A/C/copy2", {
            { 3, "",                "normal",       1, "A/B/E" },
            { 3, "alpha",           "normal",       1, "A/B/E/alpha" },
            { 3, "beta",            "normal",       1, "A/B/E/beta" },
          } },

        /* file onto a schedule-delete file */
        { "iota", "A/B/lambda", {
            { 0, "",                "normal",       1, "A/B/lambda" },
            { 3, "",                "normal",       1, "iota" },
          } },

        /* dir onto a schedule-delete dir */
        { "A/B/E", "A/D/G", {
            { 0, "",                "normal",       1, "A/D/G" },
            { 0, "pi",              "normal",       1, "A/D/G/pi" },
            { 0, "rho",             "normal",       1, "A/D/G/rho" },
            { 0, "tau",             "normal",       1, "A/D/G/tau" },
            { 3, "",                "normal",       1, "A/B/E" },
            { 3, "pi",              "base-deleted", NO_COPY_FROM },
            { 3, "rho",             "base-deleted", NO_COPY_FROM },
            { 3, "tau",             "base-deleted", NO_COPY_FROM },
            { 3, "alpha",           "normal",       1, "A/B/E/alpha" },
            { 3, "beta",            "normal",       1, "A/B/E/beta" },
          } },

        /* dir onto a schedule-delete file */
        { "A/B/E", "A/D/gamma", {
            { 0, "",                "normal",       1, "A/D/gamma" },
            { 3, "",                "normal",       1, "A/B/E" },
            { 3, "alpha",           "normal",       1, "A/B/E/alpha" },
            { 3, "beta",            "normal",       1, "A/B/E/beta" },
          } },

        /* file onto a schedule-delete dir */
        { "iota", "A/D/H", {
            { 0, "",                "normal",       1, "A/D/H" },
            { 0, "chi",             "normal",       1, "A/D/H/chi" },
            { 0, "psi",             "normal",       1, "A/D/H/psi" },
            { 0, "omega",           "normal",       1, "A/D/H/omega" },
            { 3, "",                "normal",       1, "iota" },
            { 3, "chi",             "base-deleted", NO_COPY_FROM },
            { 3, "psi",             "base-deleted", NO_COPY_FROM },
            { 3, "omega",           "base-deleted", NO_COPY_FROM },
          } },

        { 0 }
      };
    struct copy_subtest_t *subtest;
    svn_client_ctx_t *ctx;

    /* Fix up the expected->local_relpath fields in the subtest data to be
     * relative to the WC root rather than to the copy destination dir. */
    for (subtest = subtests; subtest->from_path; subtest++)
      {
        nodes_row_t *row;
        for (row = &subtest->expected[0]; row->local_relpath; row++)
          row->local_relpath = svn_dirent_join(subtest->to_path,
                                               row->local_relpath, b->pool);
      }

    /* Perform each copy. */
    SVN_ERR(svn_test__create_client_ctx(&ctx, b, b->pool));
    for (subtest = subtests; subtest->from_path; subtest++)
      {
        svn_opt_revision_t rev = { svn_opt_revision_number, { 1 } };
        svn_client_copy_source_t source;
        apr_array_header_t *sources
          = apr_array_make(b->pool, 0, sizeof(svn_client_copy_source_t *));

        source.path = svn_path_url_add_component2(b->repos_url,
                                                  subtest->from_path,
                                                  b->pool);
        source.revision = &rev;
        source.peg_revision = &rev;
        APR_ARRAY_PUSH(sources, svn_client_copy_source_t *) = &source;
        SVN_ERR(svn_client_copy6(sources,
                                 sbox_wc_path(b, subtest->to_path),
                                 FALSE, FALSE, FALSE,
                                 NULL, NULL, NULL, ctx, b->pool));
      }

    /* Check each result. */
    for (subtest = subtests; subtest->from_path; subtest++)
      {
        SVN_ERR(check_db_rows(b, subtest->to_path, subtest->expected));
      }
  }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_wc_wc_copies(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "wc_wc_copies", opts, pool));

  return wc_wc_copies(&b);
}

static svn_error_t *
test_reverts(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  nodes_row_t no_node_rows_expected[] = { { 0 } };

  SVN_ERR(svn_test__sandbox_create(&b, "reverts", opts, pool));

  SVN_ERR(wc_wc_copies(&b));


    /* Implement revert tests below, now that we have a wc with lots of
     copy-changes */

  SVN_ERR(sbox_wc_revert(&b, "A/B/D-added", svn_depth_infinity));
  SVN_ERR(check_db_rows(&b, "A/B/D-added", no_node_rows_expected));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_deletes(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "deletes", opts, pool));
  SVN_ERR(sbox_add_and_commit_greek_tree(&b));

  SVN_ERR(sbox_file_write(&b,     "A/B/E/new-file", "New file"));
  SVN_ERR(sbox_wc_add(&b, "A/B/E/new-file"));
  {
    nodes_row_t rows[] = {
      { 4, "A/B/E/new-file", "normal", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B/E/new-file", rows));
  }

  SVN_ERR(sbox_wc_delete(&b, "A/B/E/alpha"));
  {
    nodes_row_t rows[] = {
      { 0, "A/B/E/alpha", "normal",       1, "A/B/E/alpha" },
      { 4, "A/B/E/alpha", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B/E/alpha", rows));
  }

  SVN_ERR(sbox_wc_delete(&b, "A/B/F"));
  {
    nodes_row_t rows[] = {
      { 0, "A/B/F", "normal",       1, "A/B/F" },
      { 3, "A/B/F", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B/F", rows));
  }

  SVN_ERR(sbox_wc_delete(&b, "A/B"));
  {
    nodes_row_t rows[] = {
      { 0, "A/B",         "normal",       1, "A/B",        },
      { 2, "A/B/lambda",  "base-deleted", NO_COPY_FROM },
      { 0, "A/B/lambda",  "normal",       1, "A/B/lambda", },
      { 2, "A/B",         "base-deleted", NO_COPY_FROM },
      { 0, "A/B/E",       "normal",       1, "A/B/E",      },
      { 2, "A/B/E",       "base-deleted", NO_COPY_FROM },
      { 0, "A/B/E/alpha", "normal",       1, "A/B/E/alpha" },
      { 2, "A/B/E/alpha", "base-deleted", NO_COPY_FROM },
      { 0, "A/B/E/beta",  "normal",       1, "A/B/E/beta" },
      { 2, "A/B/E/beta",  "base-deleted", NO_COPY_FROM },
      { 0, "A/B/F",       "normal",       1, "A/B/F",      },
      { 2, "A/B/F",       "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B", rows));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_adds(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "adds", opts, pool));
  SVN_ERR(sbox_add_and_commit_greek_tree(&b));

  /* add file */
  SVN_ERR(sbox_file_write(&b, "new-file", "New file"));
  SVN_ERR(sbox_wc_add(&b, "new-file"));
  {
    nodes_row_t rows[] = {
      { 1, "new-file",    "normal",       NO_COPY_FROM     },
      { 0 } };
    SVN_ERR(check_db_rows(&b, "new-file", rows));
  }

  /* add dir */
  SVN_ERR(sbox_wc_mkdir(&b, "new-dir"));
  SVN_ERR(sbox_wc_mkdir(&b, "new-dir/D2"));
  {
    nodes_row_t rows[] = {
      { 1, "new-dir",     "normal",       NO_COPY_FROM     },
      { 2, "new-dir/D2",  "normal",       NO_COPY_FROM     },
      { 0 } };
    SVN_ERR(check_db_rows(&b, "new-dir", rows));
  }

  /* replace file */
  SVN_ERR(sbox_wc_delete(&b, "iota"));
  SVN_ERR(sbox_file_write(&b, "iota", "New iota file"));
  SVN_ERR(sbox_wc_add(&b, "iota"));
  {
    nodes_row_t rows[] = {
      { 0, "iota",        "normal",       1, "iota"        },
      { 1, "iota",        "normal",       NO_COPY_FROM     },
      { 0 } };
    SVN_ERR(check_db_rows(&b, "iota", rows));
  }

  /* replace dir */
  SVN_ERR(sbox_wc_delete(&b, "A/B/E"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/E"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/E/D2"));
  {
    nodes_row_t rows[] = {
      { 0, "A/B/E",       "normal",       1, "A/B/E"       },
      { 0, "A/B/E/alpha", "normal",       1, "A/B/E/alpha" },
      { 0, "A/B/E/beta",  "normal",       1, "A/B/E/beta"  },
      { 3, "A/B/E",       "normal",       NO_COPY_FROM     },
      { 4, "A/B/E/D2",    "normal",       NO_COPY_FROM     },
      { 3, "A/B/E/alpha", "base-deleted", NO_COPY_FROM     },
      { 3, "A/B/E/beta",  "base-deleted", NO_COPY_FROM     },
      { 0 } };
    SVN_ERR(check_db_rows(&b, "A/B/E", rows));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_adds_change_kind(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "test_adds_change_kind", opts, pool));
  SVN_ERR(sbox_add_and_commit_greek_tree(&b));

  /* replace dir with file */
  SVN_ERR(sbox_wc_delete(&b, "A/B/E"));
  SVN_ERR(sbox_file_write(&b, "A/B/E", "New E file"));
  SVN_ERR(sbox_wc_add(&b, "A/B/E"));
  {
    nodes_row_t rows[] = {
      { 0, "A/B/E",       "normal",       1, "A/B/E"       },
      { 0, "A/B/E/alpha", "normal",       1, "A/B/E/alpha" },
      { 0, "A/B/E/beta",  "normal",       1, "A/B/E/beta"  },
      { 3, "A/B/E",       "normal",       NO_COPY_FROM     },
      { 3, "A/B/E/alpha", "base-deleted", NO_COPY_FROM     },
      { 3, "A/B/E/beta",  "base-deleted", NO_COPY_FROM     },
      { 0 } };
    SVN_ERR(check_db_rows(&b, "A/B/E", rows));
  }

  /* replace file with dir */
  SVN_ERR(sbox_wc_delete(&b, "iota"));
  SVN_ERR(sbox_wc_mkdir(&b, "iota"));
  SVN_ERR(sbox_wc_mkdir(&b, "iota/D2"));
  {
    nodes_row_t rows[] = {
      { 0, "iota",        "normal",       1, "iota"        },
      { 1, "iota",        "normal",       NO_COPY_FROM     },
      { 2, "iota/D2",     "normal",       NO_COPY_FROM     },
      { 0 } };
    SVN_ERR(check_db_rows(&b, "iota", rows));
  }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_delete_of_copies(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "deletes_of_copies", opts, pool));
  SVN_ERR(sbox_add_and_commit_greek_tree(&b));
  SVN_ERR(sbox_wc_copy(&b, "A/B", "A/B-copied"));

  SVN_ERR(sbox_wc_delete(&b, "A/B-copied/E"));
  {
    nodes_row_t rows[] = {
      { 2, "A/B-copied/E",       "normal",       1, "A/B/E" },
      { 2, "A/B-copied/E/alpha", "normal",       1, "A/B/E/alpha" },
      { 2, "A/B-copied/E/beta",  "normal",       1, "A/B/E/beta" },
      { 3, "A/B-copied/E",       "base-deleted", NO_COPY_FROM },
      { 3, "A/B-copied/E/alpha", "base-deleted", NO_COPY_FROM },
      { 3, "A/B-copied/E/beta",  "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B-copied/E", rows));
  }

  SVN_ERR(sbox_wc_copy(&b, "A/D/G", "A/B-copied/E"));
  {
    nodes_row_t rows[] = {
      { 2, "A/B-copied/E",       "normal",       1, "A/B/E" },
      { 2, "A/B-copied/E/alpha", "normal",       1, "A/B/E/alpha" },
      { 2, "A/B-copied/E/beta",  "normal",       1, "A/B/E/beta" },
      { 3, "A/B-copied/E",       "normal",       1, "A/D/G" },
      { 3, "A/B-copied/E/alpha", "base-deleted", NO_COPY_FROM },
      { 3, "A/B-copied/E/beta",  "base-deleted", NO_COPY_FROM },
      { 3, "A/B-copied/E/pi",    "normal",       1, "A/D/G/pi" },
      { 3, "A/B-copied/E/rho",   "normal",       1, "A/D/G/rho" },
      { 3, "A/B-copied/E/tau",   "normal",       1, "A/D/G/tau" },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B-copied/E", rows));
  }

  SVN_ERR(sbox_wc_delete(&b, "A/B-copied/E/rho"));
  {
    nodes_row_t rows[] = {
      { 2, "A/B-copied/E",       "normal",       1, "A/B/E" },
      { 2, "A/B-copied/E/alpha", "normal",       1, "A/B/E/alpha" },
      { 2, "A/B-copied/E/beta",  "normal",       1, "A/B/E/beta" },
      { 3, "A/B-copied/E",       "normal",       1, "A/D/G" },
      { 3, "A/B-copied/E/alpha", "base-deleted", NO_COPY_FROM },
      { 3, "A/B-copied/E/beta",  "base-deleted", NO_COPY_FROM },
      { 3, "A/B-copied/E/pi",    "normal",       1, "A/D/G/pi" },
      { 3, "A/B-copied/E/rho",   "normal",       1, "A/D/G/rho" },
      { 3, "A/B-copied/E/tau",   "normal",       1, "A/D/G/tau" },
      { 4, "A/B-copied/E/rho",   "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B-copied/E", rows));
  }

  SVN_ERR(sbox_wc_delete(&b, "A/B-copied/E"));
  {
    nodes_row_t rows[] = {
      { 2, "A/B-copied/E",       "normal",       1, "A/B/E" },
      { 2, "A/B-copied/E/alpha", "normal",       1, "A/B/E/alpha" },
      { 2, "A/B-copied/E/beta",  "normal",       1, "A/B/E/beta" },
      { 3, "A/B-copied/E",       "base-deleted", NO_COPY_FROM },
      { 3, "A/B-copied/E/alpha", "base-deleted", NO_COPY_FROM },
      { 3, "A/B-copied/E/beta",  "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B-copied/E", rows));
  }

  SVN_ERR(sbox_wc_copy(&b, "A/B", "A/B-copied/E"));

  SVN_ERR(sbox_wc_delete(&b, "A/B-copied/E/F"));
  {
    nodes_row_t rows[] = {
      { 3, "A/B-copied/E/F", "normal",       1, "A/B/F" },
      { 4, "A/B-copied/E/F", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B-copied/E/F", rows));
  }

  SVN_ERR(sbox_wc_delete(&b, "A/B-copied"));
  {
    nodes_row_t rows[] = { { 0 } };
    SVN_ERR(check_db_rows(&b, "A/B-copied", rows));
  }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_delete_with_base(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "deletes_with_base", opts, pool));
  SVN_ERR(sbox_add_and_commit_greek_tree(&b));
  SVN_ERR(sbox_wc_delete(&b, "A/B/E/beta"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_delete(&b, "A/B/E"));
  {
    nodes_row_t rows[] = {
      { 0, "A/B/E",       "normal",        1, "A/B/E"},
      { 0, "A/B/E/alpha", "normal",        1, "A/B/E/alpha"},
      { 0, "A/B/E/beta",  "not-present",   2, "A/B/E/beta"},
      { 3, "A/B/E",       "base-deleted",  NO_COPY_FROM},
      { 3, "A/B/E/alpha", "base-deleted",  NO_COPY_FROM},
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B/E", rows));
  }

  SVN_ERR(sbox_wc_copy(&b, "A/B/F", "A/B/E"));
  SVN_ERR(sbox_wc_copy(&b, "A/mu",  "A/B/E/alpha"));
  SVN_ERR(sbox_wc_copy(&b, "A/mu",  "A/B/E/beta"));
  {
    nodes_row_t rows[] = {
      { 0, "A/B/E",       "normal",        1, "A/B/E"},
      { 0, "A/B/E/alpha", "normal",        1, "A/B/E/alpha"},
      { 0, "A/B/E/beta",  "not-present",   2, "A/B/E/beta"},
      { 3, "A/B/E",       "base-deleted",  NO_COPY_FROM},
      { 3, "A/B/E/alpha", "base-deleted",  NO_COPY_FROM},
      { 3, "A/B/E",       "normal",        1, "A/B/F"},
      { 4, "A/B/E/alpha", "normal",        1, "A/mu"},
      { 4, "A/B/E/beta",  "normal",        1, "A/mu"},
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B/E", rows));
  }

  SVN_ERR(sbox_wc_delete(&b, "A/B/E"));
  {
    nodes_row_t rows[] = {
      { 0, "A/B/E",       "normal",        1, "A/B/E"},
      { 0, "A/B/E/alpha", "normal",        1, "A/B/E/alpha"},
      { 0, "A/B/E/beta",  "not-present",   2, "A/B/E/beta"},
      { 3, "A/B/E",       "base-deleted",  NO_COPY_FROM},
      { 3, "A/B/E/alpha", "base-deleted",  NO_COPY_FROM},
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B/E", rows));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_repo_wc_copies(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "repo_wc_copies", opts, pool));

  return repo_wc_copies(&b);
}

static svn_error_t *
test_delete_with_update(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "delete_with_update", opts, pool));
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_delete(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  {
    nodes_row_t rows[] = {
      { 0, "A",       "normal",        1, "A"},
      { 1, "A",       "normal",        NO_COPY_FROM},
      { 2, "A/B",     "normal",        NO_COPY_FROM},
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A", rows));
  }
  SVN_ERR(sbox_wc_update(&b, "", 2));
  {
    nodes_row_t rows[] = {
      { 0, "A",       "normal",        2, "A"},
      { 0, "A/B",     "normal",        2, "A/B"},
      { 0, "A/B/C",   "normal",        2, "A/B/C"},
      { 1, "A",       "normal",        NO_COPY_FROM},
      { 1, "A/B",     "base-deleted",  NO_COPY_FROM},
      { 1, "A/B/C",   "base-deleted",  NO_COPY_FROM},
      { 2, "A/B",     "normal",        NO_COPY_FROM},
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A", rows));
  }
  SVN_ERR(sbox_wc_resolved(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  {
    nodes_row_t rows[] = {
      { 0, "A",       "normal",        1, "A"},
      { 1, "A",       "normal",        NO_COPY_FROM},
      { 2, "A/B",     "normal",        NO_COPY_FROM},
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A", rows));
  }

  return SVN_NO_ERROR;
}


static svn_error_t *
insert_dirs(svn_test__sandbox_t *b,
            nodes_row_t *nodes)
{
  svn_sqlite__db_t *sdb;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(open_wc_db(&sdb, b->wc_abspath, b->pool, b->pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_DELETE_NODES));
  SVN_ERR(svn_sqlite__step_done(stmt));

  while(nodes->local_relpath)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_INSERT_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "sdssrs",
                                nodes->local_relpath,
                                nodes->op_depth,
                                nodes->presence,
                                nodes->repo_relpath,
                                nodes->repo_revnum,
                                nodes->local_relpath[0]
                                  ? svn_relpath_dirname(nodes->local_relpath,
                                                        b->pool)
                                  : NULL));

      if (nodes->moved_to)
        SVN_ERR(svn_sqlite__bind_text(stmt, 7, nodes->moved_to));
      if (nodes->moved_here)
        SVN_ERR(svn_sqlite__bind_int(stmt, 8, 1));
      if (nodes->props)
        {
          int i;
          apr_hash_t *props = apr_hash_make(b->pool);
          apr_array_header_t *names = svn_cstring_split(nodes->props, ",",
                                                        TRUE, b->pool);

          for (i = 0; i < names->nelts; i++)
            {
              const char *name = APR_ARRAY_IDX(names, i, const char *);
              svn_hash_sets(props, name, svn_string_create(name, b->pool));
            }

          SVN_ERR(svn_sqlite__bind_properties(stmt, 9, props, b->pool));
        }
      else if (nodes->repo_relpath
               && strcmp(nodes->presence, "normal") == 0)
        {
          SVN_ERR(svn_sqlite__bind_text(stmt, 9, "()"));
        }

      /* File externals? */

      SVN_ERR(svn_sqlite__step_done(stmt));
      ++nodes;
    }

  SVN_ERR(svn_sqlite__close(sdb));

  return SVN_NO_ERROR;
}

static apr_int64_t count_rows(nodes_row_t *rows)
{
  nodes_row_t *first = rows;
  while(rows->local_relpath)
    ++rows;
  return rows - first;
}

static svn_error_t *
base_dir_insert_remove(svn_test__sandbox_t *b,
                       const char *local_relpath,
                       svn_revnum_t revision,
                       nodes_row_t *before,
                       nodes_row_t *added)
{
  nodes_row_t *after;
  const char *dir_abspath = sbox_wc_path(b, local_relpath);
  int i;
  apr_int64_t num_before = count_rows(before), num_added = count_rows(added);

  SVN_ERR(insert_dirs(b, before));

  SVN_ERR(svn_wc__db_base_add_directory(b->wc_ctx->db, dir_abspath,
                                        dir_abspath,
                                        local_relpath, b->repos_url,
                                        "not-even-a-uuid", revision,
                                        apr_hash_make(b->pool), revision,
                                        0, NULL, NULL, svn_depth_infinity,
                                        NULL, FALSE, NULL, NULL, NULL, NULL,
                                        b->pool));

  after = apr_palloc(b->pool, sizeof(*after) * (apr_size_t)(num_before + num_added + 1));
  for (i = 0; i < num_before; ++i)
    after[i] = before[i];
  for (i = 0; i < num_added; ++i)
    after[num_before+i] = added[i];
  after[num_before+num_added].local_relpath = NULL;

  SVN_ERR(check_db_rows(b, "", after));

  SVN_ERR(svn_wc__db_base_remove(b->wc_ctx->db, dir_abspath,
                                 FALSE, FALSE, FALSE,
                                 SVN_INVALID_REVNUM,
                                 NULL, NULL, b->pool));
  SVN_ERR(svn_wc__wq_run(b->wc_ctx->db, dir_abspath,
                         NULL, NULL, b->pool));

  SVN_ERR(check_db_rows(b, "", before));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_base_dir_insert_remove(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "base_dir_insert_remove", opts, pool));

  {
    /* /  normal                     /    normal
       A  normal                     A    normal
                                     A/B  normal
    */
    nodes_row_t before[] = {
      { 0, "",  "normal", 2, "" },
      { 0, "A", "normal", 2, "A" },
      { 0 }
    };
    nodes_row_t added[] = {
      { 0, "A/B", "normal", 2, "A/B" },
      { 0 }
    };
    SVN_ERR(base_dir_insert_remove(&b, "A/B", 2, before, added));
  }
  {
    /* /  normal                      /    normal
       A  normal  base-del            A    normal  base-del
                                      A/B  normal  base-del
    */
    nodes_row_t before[] = {
      { 0, "",  "normal",       2, "" },
      { 0, "A", "normal",       2, "A" },
      { 1, "A", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t added[] = {
      { 0, "A/B", "normal",       2, "A/B" },
      { 1, "A/B", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(base_dir_insert_remove(&b, "A/B", 2, before, added));
  }
  {
    /* /  normal                       /    normal
       A  normal  normal               A    normal  normal
                                       A/B  normal  base-del
     */
    nodes_row_t before[] = {
      { 0, "",  "normal", 2, "" },
      { 0, "A", "normal", 2, "A" },
      { 1, "A", "normal", 1, "X" },
      { 0 }
    };
    nodes_row_t added[] = {
      { 0, "A/B", "normal",       2, "A/B" },
      { 1, "A/B", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(base_dir_insert_remove(&b, "A/B", 2, before, added));
  }
  {
    /* /    normal                     /      normal
       A    normal  normal             A      normal  normal
       A/B  normal  not-pres           A/B    normal  not-pres
                                       A/B/C  normal  base-del
     */
    nodes_row_t before[] = {
      { 0, "",    "normal",      2, "" },
      { 0, "A",   "normal",      2, "A" },
      { 0, "A/B", "normal",      2, "A/B" },
      { 1, "A",   "normal",      1, "X" },
      { 1, "A/B", "not-present", NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t added[] = {
      { 0, "A/B/C", "normal",       2, "A/B/C" },
      { 1, "A/B/C", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(base_dir_insert_remove(&b, "A/B/C", 2, before, added));
  }
  {
    /* /    normal                      /    normal
       A    normal  normal              A    normal  normal
       A/B          normal              A/B  normal  normal
     */
    nodes_row_t before[] = {
      { 0, "",    "normal", 2, "" },
      { 0, "A",   "normal", 2, "A" },
      { 1, "A",   "normal", 1, "X" },
      { 1, "A/B", "normal", NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t added[] = {
      { 0, "A/B", "normal",       2, "A/B" },
      { 0 }
    };
    SVN_ERR(base_dir_insert_remove(&b, "A/B", 2, before, added));
  }
  {
    /* /    normal                       /    normal
       A    normal  normal               A    normal  normal
       A/B          not-pres             A/B  normal  not-pres
     */
    nodes_row_t before[] = {
      { 0, "",    "normal",      2, "" },
      { 0, "A",   "normal",      2, "A" },
      { 1, "A",   "normal",      1, "X" },
      { 1, "A/B", "not-present", NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t added[] = {
      { 0, "A/B", "normal",       2, "A/B" },
      { 0 }
    };
    SVN_ERR(base_dir_insert_remove(&b, "A/B", 2, before, added));
  }
  {
    /* /    normal                       /    normal
       A    normal  normal               A    normal  normal
       A/B                  normal       A/B  normal  base-del  normal
     */
    nodes_row_t before[] = {
      { 0, "",    "normal",      2, "" },
      { 0, "A",   "normal",      2, "A" },
      { 1, "A",   "normal",      1, "X" },
      { 2, "A/B", "normal",      1, "Y" },
      { 0 }
    };
    nodes_row_t added[] = {
      { 0, "A/B", "normal",       2, "A/B" },
      { 1, "A/B", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(base_dir_insert_remove(&b, "A/B", 2, before, added));
  }
  {
    /* /      normal                          /      normal
       A      normal  normal                  A      normal  normal
       A/B    normal  base-del  normal        A/B    normal  base-del  normal
       A/B/C                    normal        A/B/C  normal  base-del  normal
     */
    nodes_row_t before[] = {
      { 0, "",    "normal",       2, "" },
      { 0, "A",   "normal",       2, "A" },
      { 0, "A/B", "normal",       2, "A/B" },
      { 1, "A",   "normal",       1, "X" },
      { 1, "A/B", "base-deleted", NO_COPY_FROM },
      { 2, "A/B", "normal",       1, "Y" },
      { 0 }
    };
    nodes_row_t added[] = {
      { 0, "A/B/C", "normal",       2, "A/B/C" },
      { 1, "A/B/C", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(base_dir_insert_remove(&b, "A/B/C", 2, before, added));
  }
  {
    /* /      normal                          /      normal
       A      normal  normal                  A      normal  normal
       A/B    normal  not-pres  normal        A/B    normal  not-pres  normal
       A/B/C                    normal        A/B/C  normal  base-del  normal
     */
    nodes_row_t before[] = {
      { 0, "",      "normal",      2, "" },
      { 0, "A",     "normal",      2, "A" },
      { 0, "A/B",   "normal",      2, "A/B" },
      { 1, "A",     "normal",      1, "X" },
      { 1, "A/B",   "not-present", NO_COPY_FROM },
      { 2, "A/B",   "normal",      1, "Y" },
      { 2, "A/B/C", "normal",      NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t added[] = {
      { 0, "A/B/C", "normal",       2, "A/B/C" },
      { 1, "A/B/C", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(base_dir_insert_remove(&b, "A/B/C", 2, before, added));
  }
  {
    /*  /      normal                         /
        A      normal  normal                 A      normal  normal
        A/B    normal  base-del  normal       A/B    normal  base-del  normal
        A/B/C                    not-pres     A/B/C  normal  base-del  not-pres
     */
    nodes_row_t before[] = {
      { 0, "",      "normal",       2, "" },
      { 0, "A",     "normal",       2, "A" },
      { 0, "A/B",   "normal",       2, "A/B" },
      { 1, "A",     "normal",       1, "X" },
      { 1, "A/B",   "base-deleted", NO_COPY_FROM },
      { 2, "A/B",   "normal",       1, "Y" },
      { 2, "A/B/C", "not-present",  NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t added[] = {
      { 0, "A/B/C", "normal",       2, "A/B/C" },
      { 1, "A/B/C", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(base_dir_insert_remove(&b, "A/B/C", 2, before, added));
  }
  {
    /*  /      normal                         /
        A      normal  normal                 A      normal  normal
        A/B    normal  not-pres  normal       A/B    normal  not-pres  normal
        A/B/C                    not-pres     A/B/C  normal  base-del  not-pres
     */
    nodes_row_t before[] = {
      { 0, "",      "normal",      2, "" },
      { 0, "A",     "normal",      2, "A" },
      { 0, "A/B",   "normal",      2, "A/B" },
      { 1, "A",     "normal",      1, "X" },
      { 1, "A/B",   "not-present", NO_COPY_FROM },
      { 2, "A/B",   "normal",      1, "Y" },
      { 2, "A/B/C", "not-present", NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t added[] = {
      { 0, "A/B/C", "normal",       2, "A/B/C" },
      { 1, "A/B/C", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(base_dir_insert_remove(&b, "A/B/C", 2, before, added));
  }
  {
    /*  /      norm                       /
        A      norm  norm                 A      norm  norm
        A/B    norm  not-p  norm          A/B    norm  not-p  norm
        A/B/C                     norm    A/B/C  norm  b-del        norm
     */
    nodes_row_t before[] = {
      { 0, "",      "normal",      2, "" },
      { 0, "A",     "normal",      2, "A" },
      { 0, "A/B",   "normal",      2, "A/B" },
      { 1, "A",     "normal",      1, "X" },
      { 1, "A/B",   "not-present", NO_COPY_FROM },
      { 2, "A/B",   "normal",      1, "Y" },
      { 3, "A/B/C", "normal",      NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t added[] = {
      { 0, "A/B/C", "normal",       2, "A/B/C" },
      { 1, "A/B/C", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(base_dir_insert_remove(&b, "A/B/C", 2, before, added));
  }
  {
    /* /      norm                     /        norm
       A      norm                     A        norm
       A/B    norm                     A/B      norm
       A/B/C  norm  -  -  norm         A/B/C    norm   -  -  norm
                                       A/B/C/D  norm   -  -  b-del
    */
    nodes_row_t before[] = {
      { 0, "",      "normal", 2, "" },
      { 0, "A",     "normal", 2, "A" },
      { 0, "A/B",   "normal", 2, "A/B" },
      { 0, "A/B/C", "normal", 2, "A/B/C" },
      { 3, "A/B/C", "normal", NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t added[] = {
      { 0, "A/B/C/D", "normal",       2, "A/B/C/D" },
      { 3, "A/B/C/D", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(base_dir_insert_remove(&b, "A/B/C/D", 2, before, added));
  }
  {
    /* /      norm                     /        norm
       A      norm                     A        norm
       A/B    norm                     A/B      norm
       A/B/C  norm  -  -  norm         A/B/C    norm   -  -  norm
       A/B/C/D                  norm   A/B/C/D  norm   -  -  b-del  norm
    */
    nodes_row_t before[] = {
      { 0, "",        "normal", 2, "" },
      { 0, "A",       "normal", 2, "A" },
      { 0, "A/B",     "normal", 2, "A/B" },
      { 0, "A/B/C",   "normal", 2, "A/B/C" },
      { 3, "A/B/C",   "normal", NO_COPY_FROM },
      { 4, "A/B/C/D", "normal", NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t added[] = {
      { 0, "A/B/C/D", "normal",       2, "A/B/C/D" },
      { 3, "A/B/C/D", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(base_dir_insert_remove(&b, "A/B/C/D", 2, before, added));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_db_make_copy(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "make_copy", opts, pool));

  {
    /*  /           norm        -
        A           norm        -
        A/B         norm        -       norm
        A/B/C       norm        -       base-del    norm
        A/F         norm        -       norm
        A/F/G       norm        -       norm
        A/F/H       norm        -       not-pres
        A/F/E       norm        -       base-del
        A/X         norm        -
        A/X/Y       incomplete  -
    */
    nodes_row_t before[] = {
      { 0, "",      "normal",       2, "" },
      { 0, "A",     "normal",       2, "A" },
      { 0, "A/B",   "normal",       2, "A/B" },
      { 0, "A/B/C", "normal",       2, "A/B/C" },
      { 0, "A/F",   "normal",       2, "A/F" },
      { 0, "A/F/G", "normal",       2, "A/F/G" },
      { 0, "A/F/H", "normal",       2, "A/F/H" },
      { 0, "A/F/E", "normal",       2, "A/F/E" },
      { 0, "A/X",   "normal",       2, "A/X" },
      { 0, "A/X/Y", "incomplete",   2, "A/X/Y" },
      { 2, "A/B",   "normal",       NO_COPY_FROM },
      { 2, "A/B/C", "base-deleted", NO_COPY_FROM },
      { 3, "A/B/C", "normal",       NO_COPY_FROM },
      { 2, "A/F",   "normal",       1, "S2" },
      { 2, "A/F/G", "normal",       1, "S2/G" },
      { 2, "A/F/H", "not-present",  1, "S2/H" },
      { 2, "A/F/E", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    /*  /           norm        -
        A           norm        norm
        A/B         norm        norm        norm
        A/B/C       norm        norm        base-del    norm
        A/F         norm        norm        norm
        A/F/G       norm        norm        norm
        A/F/H       norm        norm        not-pres
        A/F/E       norm        norm        base-del
        A/X         norm        norm
        A/X/Y       incomplete  incomplete
    */
    nodes_row_t after[] = {
      { 0, "",      "normal",       2, "" },
      { 0, "A",     "normal",       2, "A" },
      { 0, "A/B",   "normal",       2, "A/B" },
      { 0, "A/B/C", "normal",       2, "A/B/C" },
      { 0, "A/F",   "normal",       2, "A/F" },
      { 0, "A/F/G", "normal",       2, "A/F/G" },
      { 0, "A/F/H", "normal",       2, "A/F/H" },
      { 0, "A/F/E", "normal",       2, "A/F/E" },
      { 0, "A/X",   "normal",       2, "A/X" },
      { 0, "A/X/Y", "incomplete",   2, "A/X/Y" },
      { 1, "A",     "normal",       2, "A" },
      { 1, "A/B",   "normal",       2, "A/B" },
      { 1, "A/B/C", "normal",       2, "A/B/C" },
      { 1, "A/F",   "normal",       2, "A/F" },
      { 1, "A/F/G", "normal",       2, "A/F/G" },
      { 1, "A/F/H", "normal",       2, "A/F/H" },
      { 1, "A/F/E", "normal",       2, "A/F/E" },
      { 1, "A/X",   "normal",       2, "A/X" },
      { 1, "A/X/Y", "incomplete",   2, "A/X/Y" },
      { 2, "A/B",   "normal",       NO_COPY_FROM },
      { 2, "A/B",   "normal",       NO_COPY_FROM },
      { 2, "A/B/C", "base-deleted", NO_COPY_FROM },
      { 2, "A/F",   "normal",       1, "S2" },
      { 2, "A/F/E", "base-deleted", NO_COPY_FROM },
      { 2, "A/F/G", "normal",       1, "S2/G" },
      { 2, "A/F/H", "not-present",  1, "S2/H" },
      { 3, "A/B/C", "normal",       NO_COPY_FROM },
      { 0 }
    };

    SVN_ERR(insert_dirs(&b, before));
    SVN_ERR(svn_wc__db_op_make_copy(b.wc_ctx->db, sbox_wc_path(&b, "A"), 
                                    NULL, NULL, pool));

    SVN_ERR(check_db_rows(&b, "", after));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_wc_move(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "wc_move", opts, pool));
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A/B/C", "A/B/C-move"));
  {
    nodes_row_t rows[] = {
      { 0, "",           "normal",       1, "" },
      { 0, "A",          "normal",       1, "A" },
      { 0, "A/B",        "normal",       1, "A/B" },
      { 0, "A/B/C",      "normal",       1, "A/B/C"},
      { 3, "A/B/C",      "base-deleted", NO_COPY_FROM, "A/B/C-move" },
      { 3, "A/B/C-move", "normal",       1, "A/B/C", MOVED_HERE },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  SVN_ERR(sbox_wc_move(&b, "A/B", "A/B-move"));
  {
    nodes_row_t rows[] = {
      { 0, "",                "normal",       1, "" },
      { 0, "A",               "normal",       1, "A" },
      { 0, "A/B",             "normal",       1, "A/B"},
      { 0, "A/B/C",           "normal",       1, "A/B/C"},
      { 2, "A/B",             "base-deleted", NO_COPY_FROM, "A/B-move" },
      { 2, "A/B/C",           "base-deleted", NO_COPY_FROM},
      { 2, "A/B-move",        "normal",       1, "A/B", MOVED_HERE },
      { 2, "A/B-move/C",      "normal",       1, "A/B/C", MOVED_HERE },
      { 3, "A/B-move/C",      "base-deleted", NO_COPY_FROM, "A/B-move/C-move" },
      { 3, "A/B-move/C-move", "normal",       1, "A/B/C", MOVED_HERE },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_mixed_rev_copy(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "mixed_rev_copy", opts, pool));
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_copy(&b, "A", "X"));
  {
    nodes_row_t rows[] = {
      { 1, "X",     "normal",       1, "A" },
      { 1, "X/B",   "not-present",  2, "A/B" },
      { 2, "X/B",   "normal",       2, "A/B" },
      { 2, "X/B/C", "not-present",  3, "A/B/C" },
      { 3, "X/B/C", "normal",       3, "A/B/C" },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "X", rows));
  }

  SVN_ERR(sbox_wc_copy(&b, "A/B", "X/Y"));
  {
    nodes_row_t rows[] = {
      { 1, "X",     "normal",       1, "A" },
      { 1, "X/B",   "not-present",  2, "A/B" },
      { 2, "X/B",   "normal",       2, "A/B" },
      { 2, "X/B/C", "not-present",  3, "A/B/C" },
      { 3, "X/B/C", "normal",       3, "A/B/C" },
      { 2, "X/Y",   "normal",       2, "A/B" },
      { 2, "X/Y/C", "not-present",  3, "A/B/C" },
      { 3, "X/Y/C", "normal",       3, "A/B/C" },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "X", rows));
  }

  SVN_ERR(sbox_wc_delete(&b, "X/B/C"));
  {
    nodes_row_t rows[] = {
      { 1, "X",     "normal",       1, "A" },
      { 1, "X/B",   "not-present",  2, "A/B" },
      { 2, "X/B",   "normal",       2, "A/B" },
      { 2, "X/B/C", "not-present",  3, "A/B/C" },
      { 2, "X/Y",   "normal",       2, "A/B" },
      { 2, "X/Y/C", "not-present",  3, "A/B/C" },
      { 3, "X/Y/C", "normal",       3, "A/B/C" },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "X", rows));
  }

  SVN_ERR(sbox_wc_delete(&b, "X"));
  SVN_ERR(sbox_wc_update(&b, "A/B/C", 0));
  {
    nodes_row_t rows[] = {
      { 0, "",      "normal",       0, "" },
      { 0, "A",     "normal",       1, "A" },
      { 0, "A/B",   "normal",       2, "A/B" },
      { 0, "A/B/C", "not-present",  0, "A/B/C" },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  SVN_ERR(sbox_wc_copy(&b, "A", "X"));
  {
    nodes_row_t rows[] = {
      { 1, "X",     "normal",       1, "A" },
      { 1, "X/B",   "not-present",  2, "A/B" },
      { 2, "X/B",   "normal",       2, "A/B" },
      { 2, "X/B/C", "not-present",  0, "A/B/C" },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "X", rows));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_delete_of_replace(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "delete_of_replace", opts, pool));
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/F"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/F/K"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/G"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/G/K"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_copy(&b, "A", "X"));
  SVN_ERR(sbox_wc_move(&b, "X/B/C/F", "X/B/C/H"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 2));

  SVN_ERR(sbox_wc_delete(&b, "A/B"));
  SVN_ERR(sbox_wc_copy(&b, "X/B", "A/B"));
  {
    nodes_row_t rows[] = {
      { 0, "A",         "normal",       2, "A" },
      { 0, "A/B",       "normal",       2, "A/B" },
      { 0, "A/B/C",     "normal",       2, "A/B/C" },
      { 0, "A/B/C/F",   "normal",       2, "A/B/C/F" },
      { 0, "A/B/C/F/K", "normal",       2, "A/B/C/F/K" },
      { 0, "A/B/C/G",   "normal",       2, "A/B/C/G" },
      { 0, "A/B/C/G/K", "normal",       2, "A/B/C/G/K" },
      { 2, "A/B",       "normal",       2, "X/B" },
      { 2, "A/B/C",     "normal",       2, "X/B/C" },
      { 2, "A/B/C/F",   "base-deleted", NO_COPY_FROM },
      { 2, "A/B/C/F/K", "base-deleted", NO_COPY_FROM },
      { 2, "A/B/C/G",   "normal",       2, "X/B/C/G" },
      { 2, "A/B/C/G/K", "normal",       2, "X/B/C/G/K" },
      { 2, "A/B/C/H",   "normal",       2, "X/B/C/H" },
      { 2, "A/B/C/H/K", "normal",       2, "X/B/C/H/K" },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A", rows));
  }

  SVN_ERR(sbox_wc_delete(&b, "A/B"));
  {
    nodes_row_t rows[] = {
      { 0, "A",         "normal",       2, "A" },
      { 0, "A/B",       "normal",       2, "A/B" },
      { 0, "A/B/C",     "normal",       2, "A/B/C" },
      { 0, "A/B/C/F",   "normal",       2, "A/B/C/F" },
      { 0, "A/B/C/F/K", "normal",       2, "A/B/C/F/K" },
      { 0, "A/B/C/G",   "normal",       2, "A/B/C/G" },
      { 0, "A/B/C/G/K", "normal",       2, "A/B/C/G/K" },
      { 2, "A/B",       "base-deleted", NO_COPY_FROM },
      { 2, "A/B/C",     "base-deleted", NO_COPY_FROM },
      { 2, "A/B/C/F",   "base-deleted", NO_COPY_FROM },
      { 2, "A/B/C/F/K", "base-deleted", NO_COPY_FROM },
      { 2, "A/B/C/G",   "base-deleted", NO_COPY_FROM },
      { 2, "A/B/C/G/K", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A", rows));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_del_replace_not_present(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "del_replace_not_present", opts, pool));
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/X"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/Y"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/Z"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_copy(&b, "A", "X"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/B/W"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_update(&b, "", 2));
  SVN_ERR(sbox_wc_update(&b, "A/B/X", 0));
  SVN_ERR(sbox_wc_update(&b, "A/B/Y", 0));
  SVN_ERR(sbox_wc_update(&b, "X/B/W", 0));
  SVN_ERR(sbox_wc_update(&b, "X/B/Y", 0));
  SVN_ERR(sbox_wc_update(&b, "X/B/Z", 0));

  SVN_ERR(sbox_wc_delete(&b, "A"));
  {
    nodes_row_t rows[] = {
      { 0, "A",         "normal",       2, "A" },
      { 0, "A/B",       "normal",       2, "A/B" },
      { 0, "A/B/X",     "not-present",  0, "A/B/X" },
      { 0, "A/B/Y",     "not-present",  0, "A/B/Y" },
      { 0, "A/B/Z",     "normal",       2, "A/B/Z" },
      { 1, "A",         "base-deleted", NO_COPY_FROM },
      { 1, "A/B",       "base-deleted", NO_COPY_FROM },
      { 1, "A/B/Z",     "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A", rows));
  }

  SVN_ERR(sbox_wc_copy(&b, "X", "A"));
  {
    nodes_row_t rows[] = {
      { 0, "A",         "normal",       2, "A" },
      { 0, "A/B",       "normal",       2, "A/B" },
      { 0, "A/B/X",     "not-present",  0, "A/B/X" },
      { 0, "A/B/Y",     "not-present",  0, "A/B/Y" },
      { 0, "A/B/Z",     "normal",       2, "A/B/Z" },
      { 1, "A",         "normal",       2, "X" },
      { 1, "A/B",       "normal",       2, "X/B" },
      { 1, "A/B/W",     "not-present",  0, "X/B/W" },
      { 1, "A/B/X",     "normal",       2, "X/B/X" },
      { 1, "A/B/Y",     "not-present",  0, "X/B/Y" },
      { 1, "A/B/Z",     "not-present",  0, "X/B/Z" },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A", rows));
  }

  SVN_ERR(sbox_wc_delete(&b, "A"));
  {
    nodes_row_t rows[] = {
      { 0, "A",         "normal",       2, "A" },
      { 0, "A/B",       "normal",       2, "A/B" },
      { 0, "A/B/X",     "not-present",  0, "A/B/X" },
      { 0, "A/B/Y",     "not-present",  0, "A/B/Y" },
      { 0, "A/B/Z",     "normal",       2, "A/B/Z" },
      { 1, "A",         "base-deleted", NO_COPY_FROM },
      { 1, "A/B",       "base-deleted", NO_COPY_FROM },
      { 1, "A/B/Z",     "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A", rows));
  }

  return SVN_NO_ERROR;
}

typedef struct actual_row_t {
  const char *local_relpath;
  const char *changelist;
} actual_row_t;

static svn_error_t *
insert_actual(svn_test__sandbox_t *b,
              actual_row_t *actual)
{
  svn_sqlite__db_t *sdb;
  svn_sqlite__stmt_t *stmt;

  if (!actual)
    return SVN_NO_ERROR;

  SVN_ERR(open_wc_db(&sdb, b->wc_abspath, b->pool, b->pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_DELETE_ACTUAL));
  SVN_ERR(svn_sqlite__step_done(stmt));

  while(actual->local_relpath)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_INSERT_ACTUAL));
      SVN_ERR(svn_sqlite__bindf(stmt, "sss",
                                actual->local_relpath,
                                actual->local_relpath[0]
                                  ? svn_relpath_dirname(actual->local_relpath,
                                                        b->pool)
                                  : NULL,
                                actual->changelist));
      SVN_ERR(svn_sqlite__step_done(stmt));
      if (actual->changelist)
        {
          SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                            STMT_ENSURE_EMPTY_PRISTINE));
          SVN_ERR(svn_sqlite__step_done(stmt));
          SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_NODES_SET_FILE));
          SVN_ERR(svn_sqlite__bindf(stmt, "s", actual->local_relpath));
          SVN_ERR(svn_sqlite__step_done(stmt));
        }
      ++actual;
    }
  SVN_ERR(svn_sqlite__close(sdb));

  return SVN_NO_ERROR;
}

static svn_error_t *
check_db_actual(svn_test__sandbox_t* b, actual_row_t *rows)
{
  svn_sqlite__db_t *sdb;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_hash_t *path_hash = apr_hash_make(b->pool);

  if (!rows)
    return SVN_NO_ERROR;

  while(rows->local_relpath)
    {
      apr_hash_set(path_hash, rows->local_relpath, APR_HASH_KEY_STRING,
                   (void*)1);
      ++rows;
    }

  SVN_ERR(open_wc_db(&sdb, b->wc_abspath, b->pool, b->pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_ALL_ACTUAL));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *local_relpath = svn_sqlite__column_text(stmt, 0, b->pool);
      if (!apr_hash_get(path_hash, local_relpath, APR_HASH_KEY_STRING))
        return svn_error_createf(SVN_ERR_TEST_FAILED, svn_sqlite__reset(stmt),
                                 "actual '%s' unexpected", local_relpath);
      apr_hash_set(path_hash, local_relpath, APR_HASH_KEY_STRING, NULL);
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  if (apr_hash_count(path_hash))
    {
      const char *local_relpath
        = apr_hash_this_key(apr_hash_first(b->pool, path_hash));
      return svn_error_createf(SVN_ERR_TEST_FAILED, svn_sqlite__reset(stmt),
                               "actual '%s' expected", local_relpath);
    }

  SVN_ERR(svn_sqlite__reset(stmt));
  SVN_ERR(svn_sqlite__close(sdb));

  return SVN_NO_ERROR;
}

static svn_error_t *
revert(svn_test__sandbox_t *b,
       const char *local_relpath,
       svn_depth_t depth,
       nodes_row_t *before_nodes,
       nodes_row_t *after_nodes,
       actual_row_t *before_actual,
       actual_row_t *after_actual)
{
  const char *local_abspath = sbox_wc_path(b, local_relpath);
  svn_error_t *err;

  if (!before_actual)
    {
      actual_row_t actual[] = { { 0 } };
      SVN_ERR(insert_actual(b, actual));
    }

  SVN_ERR(insert_dirs(b, before_nodes));
  SVN_ERR(insert_actual(b, before_actual));
  SVN_ERR(check_db_rows(b, "", before_nodes));
  SVN_ERR(check_db_actual(b, before_actual));
  err = svn_wc__db_op_revert(b->wc_ctx->db, local_abspath, depth, FALSE,
                             b->pool, b->pool);
  if (err)
    {
      /* If db_op_revert returns an error the DB should be unchanged so
         verify and return a verification error if a change is detected
         or the revert error if unchanged. */
      err = svn_error_compose_create(check_db_rows(b, "", before_nodes), err);
      err = svn_error_compose_create(check_db_actual(b, before_actual), err);
      return err;
    }
  SVN_ERR(check_db_rows(b, "", after_nodes));
  SVN_ERR(check_db_actual(b, after_actual));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_op_revert(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  svn_error_t *err;

  SVN_ERR(svn_test__sandbox_create(&b, "test_op_revert", opts, pool));

  {
    nodes_row_t before[] = {
      { 0, "",    "normal", 4, "" },
      { 0, "A",   "normal", 4, "A" },
      { 2, "A/B", "normal", NO_COPY_FROM },
      { 0 },
    };
    nodes_row_t after[] = {
      { 0, "",    "normal", 4, "" },
      { 0, "A",   "normal", 4, "A" },
      { 0 },
    };
    actual_row_t before_actual1[] = {
      { "A", NULL },
      { "A/B", NULL },
      { 0 }
    };
    actual_row_t after_actual1[] = {
      { "A", NULL },
      { 0 }
    };
    actual_row_t before_actual2[] = {
      { "A/B", NULL },
      { "A/B/C", NULL },
      { 0 }
    };
    actual_row_t after_actual2[] = {
      { "A/B", NULL },
      { 0 }
    };
    actual_row_t before_actual3[] = {
      { "", NULL },
      { "A", NULL },
      { "A/B", NULL },
      { 0 }
    };
    actual_row_t after_actual3[] = {
      { "", NULL },
      { "A/B", NULL },
      { 0 }
    };
    actual_row_t before_actual4[] = {
      { "", NULL },
      { "A/B", NULL },
      { 0 }
    };
    actual_row_t after_actual4[] = {
      { "A/B", NULL },
      { 0 }
    };
    actual_row_t common_actual5[] = {
      { "A/B", NULL },
      { "A/B/C", NULL },
      { 0 }
    };
    actual_row_t common_actual6[] = {
      { "A/B", NULL },
      { "A/B/C", NULL },
      { "A/B/C/D", NULL },
      { 0 }
    };
    SVN_ERR(revert(&b, "A/B", svn_depth_empty,
                   before, after, NULL, NULL));
    SVN_ERR(revert(&b, "A/B", svn_depth_empty,
                   before, after, before_actual1, after_actual1));
    SVN_ERR(revert(&b, "A/B/C", svn_depth_empty,
                   before, before, before_actual2, after_actual2));
    SVN_ERR(revert(&b, "A", svn_depth_empty,
                   before, before, before_actual3, after_actual3));
    SVN_ERR(revert(&b, "", svn_depth_empty,
                   before, before, before_actual4, after_actual4));
    err = revert(&b, "A/B", svn_depth_empty,
                 before, before, common_actual5, common_actual5);
    SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_INVALID_OPERATION_DEPTH);
    err = revert(&b, "A/B/C", svn_depth_empty,
                 before, before, common_actual6, common_actual6);
    SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_INVALID_OPERATION_DEPTH);
  }

  {
    nodes_row_t common[] = {
      { 0, "",        "normal", 4, "" },
      { 0, "A",       "normal", 4, "A" },
      { 0, "P",       "normal", 4, "P" },
      { 0, "P/Q",     "normal", 4, "P/Q" },
      { 1, "P",       "normal", 3, "V" },
      { 1, "P/Q",     "normal", 3, "V/Q" },
      { 2, "A/B",     "normal", 2, "X/B" },
      { 2, "A/B/C",   "normal", 2, "X/B/C" },
      { 2, "A/B/C/D", "normal", 2, "X/B/C/D" },
      { 1, "X",       "normal", NO_COPY_FROM },
      { 2, "X/Y",     "normal", NO_COPY_FROM },
      { 0 },
    };
    actual_row_t common_actual[] = {
      { "A/B/C/D", NULL },
      { "A/B/C", NULL },
      { "A/B", NULL },
      { "P", NULL },
      { "X", NULL },
      { 0 }
    };
    actual_row_t actual1[] = {
      { "A/B/C", NULL },
      { "A/B", NULL },
      { "P", NULL },
      { "X", NULL },
      { 0 }
    };
    actual_row_t actual2[] = {
      { "A/B/C/D", NULL },
      { "A/B", NULL },
      { "P", NULL },
      { "X", NULL },
      { 0 }
    };

    SVN_ERR(revert(&b, "A/B/C/D", svn_depth_empty,
                   common, common, NULL, NULL));
    SVN_ERR(revert(&b, "A/B/C/D", svn_depth_empty,
                   common, common, common_actual, actual1));

    SVN_ERR(revert(&b, "A/B/C", svn_depth_empty,
                   common, common, NULL, NULL));
    SVN_ERR(revert(&b, "A/B/C", svn_depth_empty,
                   common, common, common_actual, actual2));

    err = revert(&b, "A/B", svn_depth_empty,
                 common, common, NULL, NULL);
    SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_INVALID_OPERATION_DEPTH);
    err = revert(&b, "A/B", svn_depth_empty,
                 common, common, common_actual, common_actual);
    SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_INVALID_OPERATION_DEPTH);

    err = revert(&b, "P", svn_depth_empty,
                 common, common, NULL, NULL);
    SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_INVALID_OPERATION_DEPTH);
    err = revert(&b, "P", svn_depth_empty,
                 common, common, common_actual, common_actual);
    SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_INVALID_OPERATION_DEPTH);

    err = revert(&b, "X", svn_depth_empty,
                 common, common, NULL, NULL);
    SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_INVALID_OPERATION_DEPTH);
    err = revert(&b, "X", svn_depth_empty,
                 common, common, common_actual, common_actual);
    SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_INVALID_OPERATION_DEPTH);
  }

  {
    nodes_row_t before[] = {
      { 0, "",        "normal", 4, "" },
      { 0, "A",       "normal", 4, "A" },
      { 0, "A/B",     "normal", 4, "A/B" },
      { 0, "A/B/C",   "normal", 4, "A/B/C" },
      { 3, "A/B/C",   "base-deleted", NO_COPY_FROM },
      { 0 },
    };
    nodes_row_t after[] = {
      { 0, "",        "normal", 4, "" },
      { 0, "A",       "normal", 4, "A" },
      { 0, "A/B",     "normal", 4, "A/B" },
      { 0, "A/B/C",   "normal", 4, "A/B/C" },
      { 0 },
    };
    actual_row_t before_actual[] = {
      { "A/B", NULL },
      { "A/B/C", NULL },
      { 0 }
    };
    actual_row_t after_actual[] = {
      { "A/B", NULL },
      { 0 }
    };
    SVN_ERR(revert(&b, "A/B/C", svn_depth_empty,
                   before, after, NULL, NULL));
    SVN_ERR(revert(&b, "A/B/C", svn_depth_empty,
                   before, after, before_actual, after_actual));
  }

  {
    nodes_row_t before[] = {
      { 0, "",    "normal", 4, "" },
      { 1, "A",   "normal", 2, "X" },
      { 1, "A/B", "normal", 2, "X/B" },
      { 2, "A/B", "base-deleted", NO_COPY_FROM },
      { 0 },
    };
    nodes_row_t after[] = {
      { 0, "",    "normal", 4, "" },
      { 1, "A",   "normal", 2, "X" },
      { 1, "A/B", "normal", 2, "X/B" },
      { 0 },
    };
    actual_row_t before_actual[] = {
      { "A", NULL },
      { "A/B", NULL },
      { 0 }
    };
    actual_row_t after_actual[] = {
      { "A", NULL },
      { 0 }
    };
    SVN_ERR(revert(&b, "A/B", svn_depth_empty,
                   before, after, NULL, NULL));
    SVN_ERR(revert(&b, "A/B", svn_depth_empty,
                   before, after, before_actual, after_actual));
  }

  {
    nodes_row_t before[] = {
      { 0, "",    "normal", 4, "" },
      { 0, "A",   "normal", 4, "A" },
      { 0, "A/B", "normal", 4, "A/B" },
      { 1, "A",   "normal", 2, "X" },
      { 1, "A/B", "normal", 2, "X/B" },
      { 2, "A/B", "base-deleted", NO_COPY_FROM },
      { 0 },
    };
    nodes_row_t after[] = {
      { 0, "",    "normal", 4, "" },
      { 0, "A",   "normal", 4, "A" },
      { 0, "A/B", "normal", 4, "A/B" },
      { 1, "A",   "normal", 2, "X" },
      { 1, "A/B", "normal", 2, "X/B" },
      { 0 },
    };
    actual_row_t before_actual[] = {
      { "A", NULL },
      { "A/B", NULL },
      { 0 },
    };
    actual_row_t after_actual[] = {
      { "A", NULL },
      { 0 },
    };
    SVN_ERR(revert(&b, "A/B", svn_depth_empty,
                   before, after, NULL, NULL));
    SVN_ERR(revert(&b, "A/B", svn_depth_empty,
                   before, after, before_actual, after_actual));
  }

  {
    nodes_row_t before[] = {
      { 0, "",        "normal", 4, "" },
      { 0, "A",       "normal", 4, "A" },
      { 0, "A/B",     "normal", 4, "A/B" },
      { 0, "A/B/C",   "normal", 4, "A/B/C" },
      { 2, "A/B",     "base-deleted", NO_COPY_FROM },
      { 2, "A/B/C",   "base-deleted", NO_COPY_FROM },
      { 2, "A/B/C/D", "base-deleted", NO_COPY_FROM },
      { 0 },
    };
    nodes_row_t after[] = {
      { 0, "",        "normal", 4, "" },
      { 0, "A",       "normal", 4, "A" },
      { 0, "A/B",     "normal", 4, "A/B" },
      { 0, "A/B/C",   "normal", 4, "A/B/C" },
      { 3, "A/B/C",   "base-deleted", NO_COPY_FROM },
      { 3, "A/B/C/D", "base-deleted", NO_COPY_FROM },
      { 0 },
    };
    SVN_ERR(revert(&b, "A/B", svn_depth_empty,
                   before, after, NULL, NULL));
  }

  {
    nodes_row_t before[] = {
      { 0, "",        "normal", 4, "" },
      { 0, "A",       "normal", 4, "A" },
      { 0, "A/B",     "normal", 4, "A/B" },
      { 0, "A/B/C",   "normal", 4, "A/B/C" },
      { 1, "A",       "normal", NO_COPY_FROM },
      { 1, "A/B",     "base-deleted", NO_COPY_FROM },
      { 1, "A/B/C",   "base-deleted", NO_COPY_FROM },
      { 2, "A/B",     "normal", NO_COPY_FROM },
      { 3, "A/B/C",   "normal", NO_COPY_FROM },
      { 0 },
    };
    nodes_row_t after1[] = {
      { 0, "",        "normal", 4, "" },
      { 0, "A",       "normal", 4, "A" },
      { 0, "A/B",     "normal", 4, "A/B" },
      { 0, "A/B/C",   "normal", 4, "A/B/C" },
      { 1, "A",       "normal", NO_COPY_FROM },
      { 1, "A/B",     "base-deleted", NO_COPY_FROM },
      { 1, "A/B/C",   "base-deleted", NO_COPY_FROM },
      { 2, "A/B",     "normal", NO_COPY_FROM },
      { 0 },
    };
    nodes_row_t after2[] = {
      { 0, "",        "normal", 4, "" },
      { 0, "A",       "normal", 4, "A" },
      { 0, "A/B",     "normal", 4, "A/B" },
      { 0, "A/B/C",   "normal", 4, "A/B/C" },
      { 1, "A",       "normal", NO_COPY_FROM },
      { 1, "A/B",     "base-deleted", NO_COPY_FROM },
      { 1, "A/B/C",   "base-deleted", NO_COPY_FROM },
      { 0 },
    };
    SVN_ERR(revert(&b, "A/B/C", svn_depth_empty,
                   before, after1, NULL, NULL));
    SVN_ERR(revert(&b, "A/B", svn_depth_empty,
                   after1, after2, NULL, NULL));
  }

  {
    nodes_row_t before[] = {
      { 0, "",        "normal", 4, "" },
      { 0, "A",       "normal", 4, "A" },
      { 0, "A/B",     "normal", 4, "A/B" },
      { 0, "A/B/C",   "normal", 4, "A/B/C" },
      { 0, "A/B/C/D", "normal", 4, "A/B/C/D" },
      { 2, "A/B",     "normal", NO_COPY_FROM },
      { 2, "A/B/C",   "base-deleted", NO_COPY_FROM },
      { 2, "A/B/C/D", "base-deleted", NO_COPY_FROM },
      { 0 },
    };
    nodes_row_t after[] = {
      { 0, "",        "normal", 4, "" },
      { 0, "A",       "normal", 4, "A" },
      { 0, "A/B",     "normal", 4, "A/B" },
      { 0, "A/B/C",   "normal", 4, "A/B/C" },
      { 0, "A/B/C/D", "normal", 4, "A/B/C/D" },
      { 3, "A/B/C",   "base-deleted", NO_COPY_FROM },
      { 3, "A/B/C/D", "base-deleted", NO_COPY_FROM },
      { 0 },
    };
    SVN_ERR(revert(&b, "A/B", svn_depth_empty,
                   before, after, NULL, NULL));
  }

  {
    nodes_row_t common[] = {
      { 0, "",        "normal", 4, "" },
      { 0, "A",       "normal", 4, "A" },
      { 0, "A/B",     "normal", 4, "A/B" },
      { 0, "A/B/C",   "normal", 4, "A/B/C" },
      { 0, "A/B/C/D", "normal", 4, "A/B/C/D" },
      { 1, "A",       "normal", 2, "X/Y" },
      { 1, "A/B",     "normal", 2, "X/Y/B" },
      { 1, "A/B/C",   "base-deleted", NO_COPY_FROM },
      { 1, "A/B/C/D", "base-deleted", NO_COPY_FROM },
      { 0 },
    };
    err = revert(&b, "A", svn_depth_empty,
                 common, common, NULL, NULL);
    SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_INVALID_OPERATION_DEPTH);
  }

  {
    nodes_row_t before[] = {
      { 0, "",        "normal", 4, "" },
      { 0, "A",       "normal", 4, "A" },
      { 0, "A/B",     "normal", 4, "A/B" },
      { 0, "A/B/C",   "normal", 4, "A/B/C" },
      { 1, "A",       "normal", NO_COPY_FROM },
      { 1, "A/B",     "base-deleted", NO_COPY_FROM },
      { 1, "A/B/C",   "base-deleted", NO_COPY_FROM },
      { 2, "A/B",     "normal", NO_COPY_FROM },
      { 0 },
    };
    nodes_row_t after1[] = {
      { 0, "",        "normal", 4, "" },
      { 0, "A",       "normal", 4, "A" },
      { 0, "A/B",     "normal", 4, "A/B" },
      { 0, "A/B/C",   "normal", 4, "A/B/C" },
      { 0 },
    };
    nodes_row_t after2[] = {
      { 0, "",        "normal", 4, "" },
      { 0, "A",       "normal", 4, "A" },
      { 0, "A/B",     "normal", 4, "A/B" },
      { 0, "A/B/C",   "normal", 4, "A/B/C" },
      { 1, "A",       "normal", NO_COPY_FROM },
      { 1, "A/B",     "base-deleted", NO_COPY_FROM },
      { 1, "A/B/C",   "base-deleted", NO_COPY_FROM },
      { 0 },
    };
    SVN_ERR(revert(&b, "", svn_depth_infinity,
                   before, after1, NULL, NULL));
    SVN_ERR(revert(&b, "A", svn_depth_infinity,
                   before, after1, NULL, NULL));
    SVN_ERR(revert(&b, "A/B", svn_depth_infinity,
                   before, after2, NULL, NULL));
    SVN_ERR(revert(&b, "A/B/C", svn_depth_empty,
                   before, before, NULL, NULL));
  }

  {
    nodes_row_t before[] = {
      { 0, "",      "normal", 4, "" },
      { 0, "A",     "normal", 4, "A" },
      { 0, "A/B",   "normal", 4, "A/B" },
      { 1, "A",     "normal", 2, "X" },
      { 1, "A/B",   "normal", 2, "X/B" },
      { 1, "A/B/C", "normal", 2, "X/B/C" },
      { 2, "A/B",   "base-deleted", NO_COPY_FROM },
      { 2, "A/B/C", "base-deleted", NO_COPY_FROM },
      { 0 },
    };
    nodes_row_t after1[] = {
      { 0, "",      "normal", 4, "" },
      { 0, "A",     "normal", 4, "A" },
      { 0, "A/B",   "normal", 4, "A/B" },
      { 1, "A",     "normal", 2, "X" },
      { 1, "A/B",   "normal", 2, "X/B" },
      { 1, "A/B/C", "normal", 2, "X/B/C" },
      { 3, "A/B/C", "base-deleted", NO_COPY_FROM },
      { 0 },
    };
    nodes_row_t after2[] = {
      { 0, "",      "normal", 4, "" },
      { 0, "A",     "normal", 4, "A" },
      { 0, "A/B",   "normal", 4, "A/B" },
      { 1, "A",     "normal", 2, "X" },
      { 1, "A/B",   "normal", 2, "X/B" },
      { 1, "A/B/C", "normal", 2, "X/B/C" },
      { 0 },
    };
    SVN_ERR(revert(&b, "A/B", svn_depth_empty,
                   before, after1, NULL, NULL));
    SVN_ERR(revert(&b, "A/B", svn_depth_infinity,
                   before, after2, NULL, NULL));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_op_revert_changelist(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "test_op_revert_changelist", opts, pool));

  {
    nodes_row_t before[] = {
      { 0, "",    "normal", 4, "" },
      { 0, "A",   "normal", 4, "A" },
      { 2, "A/f", "normal", NO_COPY_FROM },
      { 0 },
    };
    nodes_row_t after[] = {
      { 0, "",    "normal", 4, "" },
      { 0, "A",   "normal", 4, "A" },
      { 0 },
    };
    actual_row_t before_actual[] = {
      { "A/f", "qq" },
      { 0 },
    };
    actual_row_t after_actual[] = {
      { 0 },
    };
    SVN_ERR(revert(&b, "A/f", svn_depth_empty,
                   before, after, before_actual, after_actual));
    SVN_ERR(revert(&b, "A/f", svn_depth_infinity,
                   before, after, before_actual, after_actual));
    SVN_ERR(revert(&b, "", svn_depth_infinity,
                   before, after, before_actual, after_actual));
  }

  {
    nodes_row_t before[] = {
      { 0, "",    "normal", 4, "" },
      { 0, "A",   "normal", 4, "A" },
      { 0, "A/f", "normal", 4, "A/f" },
      { 2, "A/f", "base-deleted", NO_COPY_FROM },
      { 0 },
    };
    nodes_row_t after[] = {
      { 0, "",    "normal", 4, "" },
      { 0, "A",   "normal", 4, "A" },
      { 0, "A/f", "normal", 4, "A/f" },
      { 0 },
    };
    actual_row_t common_actual[] = {
      { "A/f", "qq" },
      { 0 },
    };
    SVN_ERR(revert(&b, "A/f", svn_depth_empty,
                   before, after, common_actual, common_actual));
    SVN_ERR(revert(&b, "A/f", svn_depth_infinity,
                   before, after, common_actual, common_actual));
    SVN_ERR(revert(&b, "", svn_depth_infinity,
                   before, after, common_actual, common_actual));
  }

  {
    nodes_row_t before[] = {
      { 0, "",    "normal", 4, "" },
      { 0, "A",   "normal", 4, "A" },
      { 0, "A/f", "normal", 4, "A/f" },
      { 0 },
    };
    nodes_row_t after[] = {
      { 0, "",    "normal", 4, "" },
      { 0, "A",   "normal", 4, "A" },
      { 0, "A/f", "normal", 4, "A/f" },
      { 0 },
    };
    actual_row_t common_actual[] = {
      { "A/f", "qq" },
      { 0 },
    };
    SVN_ERR(revert(&b, "A/f", svn_depth_empty,
                   before, after, common_actual, common_actual));
    SVN_ERR(revert(&b, "A/f", svn_depth_infinity,
                   before, after, common_actual, common_actual));
    SVN_ERR(revert(&b, "", svn_depth_infinity,
                   before, after, common_actual, common_actual));
  }

  return SVN_NO_ERROR;
}

/* Check that the (const char *) keys of HASH are exactly the
 * EXPECTED_NUM strings in EXPECTED_STRINGS.  Return an error if not. */
static svn_error_t *
check_hash_keys(apr_hash_t *hash,
                int expected_num,
                const char **expected_strings,
                apr_pool_t *scratch_pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  int i;
  apr_hash_index_t *hi;

  for (i = 0; i < expected_num; i++)
    {
      const char *name = expected_strings[i];

      if (apr_hash_get(hash, name, APR_HASH_KEY_STRING))
        apr_hash_set(hash, name, APR_HASH_KEY_STRING, NULL);
      else
        err = svn_error_compose_create(
                err, svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                       _("Expected, not found: '%s'"), name));
    }
  for (hi = apr_hash_first(scratch_pool, hash); hi;
       hi = apr_hash_next(hi))
    {
      const char *name = apr_hash_this_key(hi);
      err = svn_error_compose_create(
              err, svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     _("Found, not expected: '%s'"), name));
    }
  return err;
}

/* Check that the (const char *) keys of APR_HASH are exactly the
 * strings in (const char *[]) C_ARRAY.  Return an error if not. */
#define CHECK_HASH(apr_hash, c_array, scratch_pool) \
  check_hash_keys(apr_hash, sizeof(c_array) / sizeof(c_array[0]), \
                  c_array, scratch_pool)

/* Check that the basenames of the (const char *) paths in ARRAY are exactly
 * the EXPECTED_NUM strings in EXPECTED_STRINGS.  Return an error if not. */
static svn_error_t *
check_array_strings(const apr_array_header_t *array,
                    int expected_num,
                    const char **expected_strings,
                    apr_pool_t *scratch_pool)
{
  int i;
  apr_hash_t *hash = apr_hash_make(scratch_pool);

  for (i = 0; i < array->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(array, i, const char *);

      apr_hash_set(hash, svn_path_basename(path, scratch_pool),
                   APR_HASH_KEY_STRING, "");
    }

  return check_hash_keys(hash, expected_num, expected_strings, scratch_pool);
}

/* Check that the basenames of the (const char *) paths in APR_ARRAY are
 * exactly the strings in (const char *[]) C_ARRAY. Return an error if not. */
#define CHECK_ARRAY(apr_array, c_array, scratch_pool) \
  check_array_strings(apr_array, sizeof(c_array) / sizeof(c_array[0]), \
                      c_array, scratch_pool)


/* The purpose of this test is to check whether a child of a deleted-and-
 * replaced directory is reported by various "list the children" APIs. */
static svn_error_t *
test_children_of_replaced_dir(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  const apr_array_header_t *children_array;
  apr_hash_t *children_hash, *conflicts_hash;
  const char *A_abspath;
  const char *working_children_exc_hidden[] = { "G", "H", "I", "J", "K", "L" };
  const char *working_children_inc_hidden[] = { "G", "H", "I", "J", "K", "L" };
  const char *all_children_inc_hidden[] = { "F", "G", "H", "I", "J", "K", "L" };

  /*
   * F - base only
   * G - base, working (from copy of X; schedule-delete)
   * H - base, working (from copy of X)
   * I - working only (from copy of X)
   * J - working only (schedule-add)
   * K - working only (from copy of X; schedule-delete)
   * L - base, working (not in copy; schedule-add)
   */

  SVN_ERR(svn_test__sandbox_create(&b, "children_of_replaced_dir", opts, pool));
  A_abspath = svn_dirent_join(b.wc_abspath, "A", pool);

  /* Set up the base state as revision 1. */
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/F"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/G"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/H"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/L"));
  SVN_ERR(sbox_wc_mkdir(&b, "X"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/G"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/H"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/I"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/K"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  /* Replace A with a copy of X. */
  SVN_ERR(sbox_wc_delete(&b, "A"));
  SVN_ERR(sbox_wc_copy(&b, "X", "A"));

  /* Make other local mods. */
  SVN_ERR(sbox_wc_delete(&b, "A/G"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/J"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/L"));

  /* Test several variants of "list the children of 'A'". */

  SVN_ERR(svn_wc__db_read_children(&children_array, b.wc_ctx->db, A_abspath,
                                   pool, pool));
  SVN_ERR(CHECK_ARRAY(children_array, all_children_inc_hidden, pool));

  SVN_ERR(svn_wc__db_read_children_of_working_node(
            &children_array, b.wc_ctx->db, A_abspath, pool, pool));
  SVN_ERR(CHECK_ARRAY(children_array, working_children_inc_hidden, pool));

  SVN_ERR(svn_wc__db_read_children(&children_array, b.wc_ctx->db, A_abspath,
                                   pool, pool));
  SVN_ERR(CHECK_ARRAY(children_array, all_children_inc_hidden, pool));

  /* I am not testing svn_wc__node_get_children(show_hidden=FALSE) because
   * I'm not sure what result we should expect if a certain child path is a
   * child of a deleted-and-replaced dir (so should be included) and is also
   * a 'hidden' child of the working dir (so should be excluded). */

  SVN_ERR(svn_wc__node_get_children_of_working_node(
            &children_array, b.wc_ctx, A_abspath,
            pool, pool));
  SVN_ERR(CHECK_ARRAY(children_array, working_children_exc_hidden, pool));

  SVN_ERR(svn_wc__db_read_children_info(&children_hash, &conflicts_hash,
                                        b.wc_ctx->db, A_abspath,
                                        FALSE /* base_tree_only */,
                                        pool, pool));
  SVN_ERR(CHECK_HASH(children_hash, all_children_inc_hidden, pool));

  /* We don't yet have a svn_wc__db_read_children_info2() to test. */

  return SVN_NO_ERROR;
}

static svn_error_t *
do_delete(svn_test__sandbox_t *b,
          const char *local_relpath,
          nodes_row_t *before,
          nodes_row_t *after,
          actual_row_t *actual_before,
          actual_row_t *actual_after)
{
  const char *local_abspath = sbox_wc_path(b, local_relpath);

  SVN_ERR(insert_dirs(b, before));
  SVN_ERR(insert_actual(b, actual_before));
  SVN_ERR(check_db_rows(b, "", before));
  SVN_ERR(check_db_actual(b, actual_before));
  SVN_ERR(svn_wc__db_op_delete(b->wc_ctx->db, local_abspath, NULL,
                               TRUE /* delete_dir_externals */,
                               NULL /* conflict */, NULL /* work_item */,
                               NULL, NULL /* cancellation */,
                               NULL, NULL /* notification */,
                               b->pool));
  SVN_ERR(check_db_rows(b, "", after));
  SVN_ERR(check_db_actual(b, actual_after));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_op_delete(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  SVN_ERR(svn_test__sandbox_create(&b, "op_delete", opts, pool));

  {
    nodes_row_t before1[] = {
      { 0, "",    "normal",       5, "" },
      { 0, "A",   "normal",       5, "A" },
      { 0, "A/B", "normal",       5, "A/B" },
      { 0 }
    };
    nodes_row_t before2[] = {
      { 0, "",    "normal",       5, "" },
      { 0, "A",   "normal",       5, "A" },
      { 0, "A/B", "normal",       5, "A/B" },
      { 1, "A",   "normal",       NO_COPY_FROM },
      { 2, "A/B", "normal",       NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t after[] = {
      { 0, "",    "normal",       5, "" },
      { 0, "A",   "normal",       5, "A" },
      { 0, "A/B", "normal",       5, "A/B" },
      { 1, "A",   "base-deleted", NO_COPY_FROM },
      { 1, "A/B", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(do_delete(&b, "A", before1, after, NULL, NULL));
    SVN_ERR(do_delete(&b, "A", before2, after, NULL, NULL));
  }

  {
    nodes_row_t before[] = {
      { 0, "",      "normal",       5, "" },
      { 0, "A",     "normal",       5, "A" },
      { 2, "A/B",   "normal",       3, "X/B" },
      { 2, "A/B/C", "normal",       3, "X/B/C" },
      { 0 }
    };
    nodes_row_t after[] = {
      { 0, "",    "normal",       5, "" },
      { 0, "A",   "normal",       5, "A" },
      { 0 }
    };
    SVN_ERR(do_delete(&b, "A/B", before, after, NULL, NULL));
  }

  {
    nodes_row_t before[] = {
      { 0, "",      "normal",       5, "" },
      { 0, "A",     "normal",       5, "A" },
      { 0, "A/B",   "normal",       5, "A/B" },
      { 0, "A/B/C", "normal",       5, "A/B/C" },
      { 1, "A",     "normal",       3, "X" },
      { 1, "A/B",   "normal",       3, "X/B" },
      { 1, "A/B/C", "base-deleted", NO_COPY_FROM },
      { 1, "A/B/D", "normal",       3, "X/B/D" },
      { 0 }
    };
    nodes_row_t after1[] = {
      { 0, "",      "normal",       5, "" },
      { 0, "A",     "normal",       5, "A" },
      { 0, "A/B",   "normal",       5, "A/B" },
      { 0, "A/B/C", "normal",       5, "A/B/C" },
      { 1, "A",     "normal",       3, "X" },
      { 1, "A/B",   "normal",       3, "X/B" },
      { 1, "A/B/C", "base-deleted", NO_COPY_FROM },
      { 1, "A/B/D", "normal",       3, "X/B/D" },
      { 2, "A/B",   "base-deleted", NO_COPY_FROM },
      { 2, "A/B/D", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t after2[] = {
      { 0, "",      "normal",       5, "" },
      { 0, "A",     "normal",       5, "A" },
      { 0, "A/B",   "normal",       5, "A/B" },
      { 0, "A/B/C", "normal",       5, "A/B/C" },
      { 1, "A",     "base-deleted", NO_COPY_FROM },
      { 1, "A/B",   "base-deleted", NO_COPY_FROM },
      { 1, "A/B/C", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(do_delete(&b, "A/B", before, after1, NULL, NULL));
    SVN_ERR(do_delete(&b, "A", before, after2, NULL, NULL));
  }

  {
    nodes_row_t before[] = {
      { 0, "",        "normal",       5, "" },
      { 0, "A",       "normal",       5, "A" },
      { 0, "A/B",     "normal",       5, "A/B" },
      { 0, "A/B/C",   "normal",       5, "A/B/C" },
      { 3, "A/B/C",   "normal",       3, "X" },
      { 3, "A/B/C/D", "normal",       3, "X/D" },
      { 4, "A/B/C/D", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t after[] = {
      { 0, "",        "normal",       5, "" },
      { 0, "A",       "normal",       5, "A" },
      { 0, "A/B",     "normal",       5, "A/B" },
      { 0, "A/B/C",   "normal",       5, "A/B/C" },
      { 1, "A",       "base-deleted", NO_COPY_FROM },
      { 1, "A/B",     "base-deleted", NO_COPY_FROM },
      { 1, "A/B/C",   "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(do_delete(&b, "A", before, after, NULL, NULL));
  }

  {
    nodes_row_t state1[] = {
      { 0, "",        "normal", 5, "" },
      { 0, "A",       "normal", 5, "A" },
      { 0, "A/B",     "normal", 5, "A/B" },
      { 0, "A/B/C",   "normal", 5, "A/B/C" },
      { 0, "A/B/C/D", "normal", 5, "A/B/C" },
      { 4, "A/B/C/X", "normal", NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t state2[] = {
      { 0, "",        "normal",       5, "" },
      { 0, "A",       "normal",       5, "A" },
      { 0, "A/B",     "normal",       5, "A/B" },
      { 0, "A/B/C",   "normal",       5, "A/B/C" },
      { 0, "A/B/C/D", "normal",       5, "A/B/C" },
      { 4, "A/B/C/D", "base-deleted", NO_COPY_FROM },
      { 4, "A/B/C/X", "normal",       NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t state3[] = {
      { 0, "",        "normal",       5, "" },
      { 0, "A",       "normal",       5, "A" },
      { 0, "A/B",     "normal",       5, "A/B" },
      { 0, "A/B/C",   "normal",       5, "A/B/C" },
      { 0, "A/B/C/D", "normal",       5, "A/B/C" },
      { 2, "A/B",     "base-deleted", NO_COPY_FROM },
      { 2, "A/B/C",   "base-deleted", NO_COPY_FROM },
      { 2, "A/B/C/D", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    nodes_row_t state4[] = {
      { 0, "",        "normal",       5, "" },
      { 0, "A",       "normal",       5, "A" },
      { 0, "A/B",     "normal",       5, "A/B" },
      { 0, "A/B/C",   "normal",       5, "A/B/C" },
      { 0, "A/B/C/D", "normal",       5, "A/B/C" },
      { 1, "A",       "base-deleted", NO_COPY_FROM },
      { 1, "A/B",     "base-deleted", NO_COPY_FROM },
      { 1, "A/B/C",   "base-deleted", NO_COPY_FROM },
      { 1, "A/B/C/D", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(do_delete(&b, "A/B/C/D", state1, state2, NULL, NULL));
    SVN_ERR(do_delete(&b, "A/B", state2, state3, NULL, NULL));
    SVN_ERR(do_delete(&b, "A", state3, state4, NULL, NULL));
  }

  {
    nodes_row_t before[] = {
      { 0, "",    "normal", 5, "" },
      { 0, "A",   "normal", 5, "" },
      { 0, "A/f", "normal", 5, "" },
      { 2, "A/B", "normal", 5, "" },
      { 0 }
    };
    nodes_row_t after[] = {
      { 0, "",    "normal", 5, "" },
      { 0, "A",   "normal", 5, "" },
      { 0, "A/f", "normal", 5, "" },
      { 1, "A",   "base-deleted", NO_COPY_FROM},
      { 1, "A/f", "base-deleted", NO_COPY_FROM},
      { 0 }
    };
    actual_row_t before_actual[] = {
      { "A",     NULL },
      { "A/f",   "qq" },
      { "A/B",   NULL },
      { "A/B/C", NULL },
      { 0 },
    };
    actual_row_t after_actual[] = {
      { "A/f", "qq" },
      { 0 },
    };
    SVN_ERR(do_delete(&b, "A", before, after, before_actual, after_actual));
  }

  {
    nodes_row_t before[] = {
      { 0, "",      "normal",       5, "" },
      { 0, "A",     "normal",       5, "A" },
      { 0, "A/B",   "normal",       5, "A/B" },
      { 0, "A/B/f", "normal",       5, "A/B/f" },
      { 0, "A/B/g", "normal",       5, "A/B/g" },
      { 1, "A",     "normal",       4, "A" },
      { 1, "A/B",   "normal",       4, "A/B" },
      { 1, "A/B/f", "normal",       4, "A/B/f" },
      { 1, "A/B/g", "base-deleted", NO_COPY_FROM},
      { 0 }
    };
    nodes_row_t after[] = {
      { 0, "",      "normal",       5, "" },
      { 0, "A",     "normal",       5, "A" },
      { 0, "A/B",   "normal",       5, "A/B" },
      { 0, "A/B/f", "normal",       5, "A/B/f" },
      { 0, "A/B/g", "normal",       5, "A/B/g" },
      { 1, "A",     "normal",       4, "A" },
      { 1, "A/B",   "normal",       4, "A/B" },
      { 1, "A/B/f", "normal",       4, "A/B/f" },
      { 1, "A/B/g", "base-deleted", NO_COPY_FROM},
      { 2, "A/B",   "base-deleted", NO_COPY_FROM},
      { 2, "A/B/f", "base-deleted", NO_COPY_FROM},
      { 0 }
    };
    SVN_ERR(do_delete(&b, "A/B", before, after, NULL, NULL));
  }

  return SVN_NO_ERROR;
}

/* The purpose of this test is to check what happens if a deleted child is
   replaced by the same nodes. */
static svn_error_t *
test_child_replace_with_same_origin(const svn_test_opts_t *opts,
                                    apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "child_replace_with_same", opts, pool));

  /* Set up the base state as revision 1. */
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_copy(&b, "A", "X"));

  {
    nodes_row_t rows[] = {
      {1, "X",       "normal",           1, "A"},
      {1, "X/B",     "normal",           1, "A/B"},
      {1, "X/B/C",   "normal",           1, "A/B/C"},
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "X", rows));
  }

  SVN_ERR(sbox_wc_delete(&b, "X/B"));
  {
    nodes_row_t rows[] = {
      {1, "X",       "normal",           1, "A"},
      {1, "X/B",     "normal",           1, "A/B"},
      {1, "X/B/C",   "normal",           1, "A/B/C"},

      {2, "X/B",     "base-deleted",     NO_COPY_FROM },
      {2, "X/B/C",   "base-deleted",     NO_COPY_FROM },

      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "X", rows));
  }

  SVN_ERR(sbox_wc_copy(&b, "A/B", "X/B"));
  {
    /* The revisions match what was here, so for an optimal commit
       this should have exactly the same behavior as reverting X/B.

       Another copy would be fine, as that is really what the user
       did. */
    nodes_row_t rows[] = {
      {1, "X",       "normal",           1, "A"},
      {1, "X/B",     "normal",           1, "A/B"},
      {1, "X/B/C",   "normal",           1, "A/B/C"},

      /* We either expect this */
      {2, "X/B",     "normal",           1, "A/B" },
      {2, "X/B/C",   "normal",           1, "A/B/C" },

      /* Or we expect that op_depth 2 does not exist */

      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "X", rows));
  }

  return SVN_NO_ERROR;
}

/* The purpose of this test is to check what happens below a shadowed update,
   in a few scenarios */
static svn_error_t *
test_shadowed_update(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "shadowed_update", opts, pool));

  /* Set up the base state as revision 1. */
  SVN_ERR(sbox_file_write(&b, "iota", "This is iota"));
  SVN_ERR(sbox_wc_add(&b, "iota"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  /* And create two trees in r2 */
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));

  SVN_ERR(sbox_wc_mkdir(&b, "K"));
  SVN_ERR(sbox_wc_mkdir(&b, "K/L"));
  SVN_ERR(sbox_wc_mkdir(&b, "K/L/M"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  /* And change something in r3 */
  SVN_ERR(sbox_file_write(&b, "iota", "This is a new iota"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  /* And delete C & M */
  SVN_ERR(sbox_wc_delete(&b, "A/B/C"));
  SVN_ERR(sbox_wc_delete(&b, "K/L/M"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  /* And now create the shadowed situation */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  SVN_ERR(sbox_wc_copy(&b, "A", "A_tmp"));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_move(&b, "A_tmp", "A"));

  SVN_ERR(sbox_wc_mkdir(&b, "K"));
  SVN_ERR(sbox_wc_mkdir(&b, "K/L"));
  SVN_ERR(sbox_wc_mkdir(&b, "K/L/M"));

  /* Verify situation before update */
  {
    nodes_row_t rows[] = {
      {0, "",        "normal",           1, ""},
      {0, "iota",    "normal",           1, "iota"},

      {1, "A",       "normal",           2, "A"},
      {1, "A/B",     "normal",           2, "A/B"},
      {1, "A/B/C",   "normal",           2, "A/B/C"},

      {1, "K",       "normal",           NO_COPY_FROM},
      {2, "K/L",     "normal",           NO_COPY_FROM},
      {3, "K/L/M",   "normal",           NO_COPY_FROM},
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  /* And now bring in A and K below the local information */
  SVN_ERR(sbox_wc_update(&b, "", 3));

  {
    nodes_row_t rows[] = {

      {0, "",        "normal",           3, ""},
      {0, "iota",    "normal",           3, "iota"},

      {0, "A",       "normal",           3, "A"},
      {0, "A/B",     "normal",           3, "A/B"},
      {0, "A/B/C",   "normal",           3, "A/B/C"},

      {1, "A",       "normal",           2, "A"},
      {1, "A/B",     "normal",           2, "A/B"},
      {1, "A/B/C",   "normal",           2, "A/B/C"},

      {0, "K",       "normal",           3, "K"},
      {0, "K/L",     "normal",           3, "K/L"},
      {0, "K/L/M",   "normal",           3, "K/L/M"},

      {1, "K",       "normal",           NO_COPY_FROM},
      {1, "K/L",     "base-deleted",     NO_COPY_FROM},
      {1, "K/L/M",   "base-deleted",     NO_COPY_FROM},

      {2, "K/L",     "normal",           NO_COPY_FROM},
      {3, "K/L/M",   "normal",           NO_COPY_FROM},

      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  /* Update again to remove C and M */
  SVN_ERR(sbox_wc_resolved(&b, "A"));
  SVN_ERR(sbox_wc_resolved(&b, "K"));
  SVN_ERR(sbox_wc_update(&b, "", 4));

  {
    nodes_row_t rows[] = {

      {0, "",        "normal",           4, ""},
      {0, "iota",    "normal",           4, "iota"},

      {0, "A",       "normal",           4, "A"},
      {0, "A/B",     "normal",           4, "A/B"},

      {1, "A",       "normal",           2, "A"},
      {1, "A/B",     "normal",           2, "A/B"},
      {1, "A/B/C",   "normal",           2, "A/B/C"},

      {0, "K",       "normal",           4, "K"},
      {0, "K/L",     "normal",           4, "K/L"},

      {1, "K",       "normal",           NO_COPY_FROM},
      {1, "K/L",     "base-deleted",     NO_COPY_FROM},

      {2, "K/L",     "normal",           NO_COPY_FROM},
      {3, "K/L/M",   "normal",           NO_COPY_FROM},

      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  /* Update again to bring C and M back */
  SVN_ERR(sbox_wc_resolved(&b, "A"));
  SVN_ERR(sbox_wc_resolved(&b, "K"));
  SVN_ERR(sbox_wc_update(&b, "", 3));

  SVN_ERR(sbox_wc_delete(&b, "K/L/M"));
  {
    nodes_row_t rows[] = {
      {0, "",        "normal",           3, ""},
      {0, "iota",    "normal",           3, "iota"},

      {0, "A",       "normal",           3, "A"},
      {0, "A/B",     "normal",           3, "A/B"},
      {0, "A/B/C",   "normal",           3, "A/B/C"},

      {1, "A",       "normal",           2, "A"},
      {1, "A/B",     "normal",           2, "A/B"},
      {1, "A/B/C",   "normal",           2, "A/B/C"},

      {0, "K",       "normal",           3, "K"},
      {0, "K/L",     "normal",           3, "K/L"},
      {0, "K/L/M",   "normal",           3, "K/L/M"},

      {1, "K",       "normal",           NO_COPY_FROM},
      {1, "K/L",     "base-deleted",     NO_COPY_FROM},
      {1, "K/L/M",   "base-deleted",     NO_COPY_FROM},

      {2, "K/L",     "normal",           NO_COPY_FROM},

      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  /* Resolve conflict on K and go back to r1 */
  SVN_ERR(sbox_wc_revert(&b, "K", svn_depth_infinity));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_mkdir(&b, "K"));
  SVN_ERR(sbox_wc_mkdir(&b, "K/L"));

  SVN_ERR(sbox_wc_update(&b, "", 3));

  {
    nodes_row_t rows[] = {

      {0, "K",       "normal",           3, "K"},
      {0, "K/L",     "normal",           3, "K/L"},
      {0, "K/L/M",   "normal",           3, "K/L/M"},

      {1, "K",       "normal",           NO_COPY_FROM},
      {1, "K/L",     "base-deleted",     NO_COPY_FROM},
      {1, "K/L/M",   "base-deleted",     NO_COPY_FROM},

      {2, "K/L",     "normal",           NO_COPY_FROM},

      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "K", rows));
  }

  /* Update the shadowed K/L/M to r4 where they do not exit */
  SVN_ERR(sbox_wc_resolved(&b, "K"));
  SVN_ERR(sbox_wc_update(&b, "K/L/M", 4));
  SVN_ERR(sbox_wc_resolved(&b, "A"));
  SVN_ERR(sbox_wc_update(&b, "A/B/C", 4));

  {
    nodes_row_t rows[] = {

      {0, "",        "normal",           3, ""},
      {0, "iota",    "normal",           3, "iota"},

      {0, "A",       "normal",           3, "A"},
      {0, "A/B",     "normal",           3, "A/B"},
      {0, "A/B/C",   "not-present",      4, "A/B/C"},

      {1, "A",       "normal",           2, "A"},
      {1, "A/B",     "normal",           2, "A/B"},
      {1, "A/B/C",   "normal",           2, "A/B/C"},

      {0, "K",       "normal",           3, "K"},
      {0, "K/L",     "normal",           3, "K/L"},
      {0, "K/L/M",   "not-present",      4, "K/L/M"},

      {1, "K",       "normal",           NO_COPY_FROM},
      {1, "K/L",     "base-deleted",     NO_COPY_FROM},

      {2, "K/L",     "normal",           NO_COPY_FROM},

      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }


  return SVN_NO_ERROR;
}

/* The purpose of this test is to check what happens below a shadowed update,
   in a few scenarios */
static svn_error_t *
test_copy_of_deleted(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "copy_of_deleted", opts, pool));
  SVN_ERR(sbox_add_and_commit_greek_tree(&b));

  /* Recreate the test scenario from copy_tests.py copy_wc_url_with_server_excluded */

  /* Delete A/B */
  SVN_ERR(sbox_wc_delete(&b, "A/B"));

  /* A/no not-present but in HEAD */
  SVN_ERR(sbox_wc_copy(&b, "A/mu", "A/no"));
  SVN_ERR(sbox_wc_commit(&b, "A/no"));
  SVN_ERR(sbox_wc_update(&b, "A/no", 1));

  /* A/mu not-present and not in HEAD */
  SVN_ERR(sbox_wc_delete(&b, "A/mu"));
  SVN_ERR(sbox_wc_commit(&b, "A/mu"));

  /* A/D excluded */
  SVN_ERR(sbox_wc_exclude(&b, "A/D"));

  /* This should have created this structure */
  {
    nodes_row_t rows[] = {

      {0, "A",           "normal",           1, "A"},
      {0, "A/B",         "normal",           1, "A/B"},
      {0, "A/B/E",       "normal",           1, "A/B/E"},
      {0, "A/B/E/alpha", "normal",           1, "A/B/E/alpha"},
      {0, "A/B/E/beta",  "normal",           1, "A/B/E/beta"},
      {0, "A/B/F",       "normal",           1, "A/B/F"},
      {0, "A/B/lambda",  "normal",           1, "A/B/lambda"},
      {0, "A/C",         "normal",           1, "A/C"},
      {0, "A/D",         "excluded",         1, "A/D"},
      {0, "A/mu",        "not-present",      3, "A/mu"},
      {0, "A/no",        "not-present",      1, "A/no"},

      {2, "A/B",         "base-deleted",     NO_COPY_FROM},
      {2, "A/B/E",       "base-deleted",     NO_COPY_FROM},
      {2, "A/B/E/alpha", "base-deleted",     NO_COPY_FROM},
      {2, "A/B/E/beta",  "base-deleted",     NO_COPY_FROM},
      {2, "A/B/lambda",  "base-deleted",     NO_COPY_FROM},
      {2, "A/B/F",       "base-deleted",     NO_COPY_FROM},

      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A", rows));
  }

  SVN_ERR(sbox_wc_copy(&b, "A", "A_copied"));

  /* I would expect this behavior, as this copies all layers where possible
     instead of just constructing a top level layer with not-present nodes
     whenever we find a deletion. */
  {
    nodes_row_t rows[] = {

      {1, "A_copied",           "normal",           1, "A"},
      {1, "A_copied/B",         "normal",           1, "A/B"},
      {1, "A_copied/B/E",       "normal",           1, "A/B/E"},
      {1, "A_copied/B/E/alpha", "normal",           1, "A/B/E/alpha"},
      {1, "A_copied/B/E/beta",  "normal",           1, "A/B/E/beta"},
      {1, "A_copied/B/F",       "normal",           1, "A/B/F"},
      {1, "A_copied/B/lambda",  "normal",           1, "A/B/lambda"},
      {1, "A_copied/C",         "normal",           1, "A/C"},
      {1, "A_copied/D",         "excluded",         1, "A/D"},
      {1, "A_copied/mu",        "not-present",      3, "A/mu"},
      {1, "A_copied/no",        "not-present",      1, "A/no"},

      {2, "A_copied/B",         "base-deleted",     NO_COPY_FROM},
      {2, "A_copied/B/E",       "base-deleted",     NO_COPY_FROM},
      {2, "A_copied/B/E/alpha", "base-deleted",     NO_COPY_FROM},
      {2, "A_copied/B/E/beta",  "base-deleted",     NO_COPY_FROM},
      {2, "A_copied/B/lambda",  "base-deleted",     NO_COPY_FROM},
      {2, "A_copied/B/F",       "base-deleted",     NO_COPY_FROM},

      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A_copied", rows));
  }

  return SVN_NO_ERROR;
}

/* Part of issue #3702, #3865 */
static svn_error_t *
test_case_rename(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  apr_hash_t *dirents;

  SVN_ERR(svn_test__sandbox_create(&b, "case_rename", opts, pool));
  SVN_ERR(sbox_add_and_commit_greek_tree(&b));

  SVN_ERR(sbox_wc_move(&b, "A", "a"));
  SVN_ERR(sbox_wc_move(&b, "iota", "iotA"));

  SVN_ERR(svn_io_get_dirents3(&dirents, sbox_wc_path(&b, ""), TRUE, pool, pool));

  /* A shouldn't be there, but a should */
  SVN_TEST_ASSERT(apr_hash_get(dirents, "a", APR_HASH_KEY_STRING));
  SVN_TEST_ASSERT(apr_hash_get(dirents, "A", APR_HASH_KEY_STRING) == NULL);
  /* iota shouldn't be there, but iotA should */
  SVN_TEST_ASSERT(apr_hash_get(dirents, "iotA", APR_HASH_KEY_STRING));
  SVN_TEST_ASSERT(apr_hash_get(dirents, "iota", APR_HASH_KEY_STRING) == NULL);

  return SVN_NO_ERROR;
}

static svn_error_t *
commit_file_external(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "commit_file_external", opts, pool));
  SVN_ERR(sbox_file_write(&b, "f", "this is f\n"));
  SVN_ERR(sbox_wc_add(&b, "f"));
  SVN_ERR(sbox_wc_propset(&b, "svn:externals", "^/f g", ""));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_file_write(&b, "g", "this is f\nmodified via g\n"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 2));

  {
    nodes_row_t rows[] = {
      { 0, "",  "normal",       2, "" },
      { 0, "f", "normal",       2, "f" },
      { 0, "g", "normal",       2, "f", TRUE },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
revert_file_externals(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "revert_file_externals", opts, pool));
  SVN_ERR(sbox_file_write(&b, "f", "this is f\n"));
  SVN_ERR(sbox_wc_add(&b, "f"));
  SVN_ERR(sbox_wc_propset(&b, "svn:externals", "^/f g", ""));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_propset(&b, "svn:externals", "^/f h", ""));
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_propset(&b, "svn:externals", "^/f g", "A"));
  {
    nodes_row_t rows[] = {
      { 0, "",    "normal", 1, "" },
      { 0, "f",   "normal", 1, "f" },
      { 0, "g",   "normal", 1, "f", TRUE },
      { 1, "A",   "normal", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  SVN_ERR(sbox_wc_update(&b, "", 1));
  {
    nodes_row_t rows[] = {
      { 0, "",    "normal",      1, "" },
      { 0, "f",   "normal",      1, "f" },
      { 1, "A",   "normal",      NO_COPY_FROM },
      { 0, "h",   "normal",      1, "f", TRUE },
      { 0, "A/g", "normal",      1, "f", TRUE },

      { 0, "g",   "not-present", 0, "g"},
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  SVN_ERR(sbox_wc_revert(&b, "", svn_depth_infinity));
  {
    nodes_row_t rows[] = {
      { 0, "",    "normal",      1, "" },
      { 0, "f",   "normal",      1, "f" },
      { 0, "h",   "normal",      1, "f", TRUE },
      { 0, "A/g", "normal",      1, "f", TRUE },

      { 0, "g",   "not-present", 0, "g"},
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  SVN_ERR(sbox_wc_update(&b, "", 1));
  {
    nodes_row_t rows[] = {
      { 0, "",    "normal",      1, "" },
      { 0, "f",   "normal",      1, "f" },
      { 0, "g",   "normal",      1, "f", TRUE },

      { 0, "h",   "not-present", 0, "h"},
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  SVN_ERR(sbox_wc_update(&b, "", 1));
  {
    nodes_row_t rows[] = {
      { 0, "",    "normal",      1, "" },
      { 0, "f",   "normal",      1, "f" },
      { 0, "g",   "normal",      1, "f", TRUE },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_file_externals(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "copy_file_externals", opts, pool));
  SVN_ERR(sbox_file_write(&b, "f", "this is f\n"));
  SVN_ERR(sbox_wc_add(&b, "f"));
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_propset(&b, "svn:externals", "^/f g", "A"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_propset(&b, "svn:externals", "^/f g", "A/B"));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  {
    nodes_row_t rows[] = {
      { 0, "",      "normal", 1, "" },
      { 0, "f",     "normal", 1, "f" },
      { 0, "A",     "normal", 1, "A" },
      { 2, "A/B",   "normal", NO_COPY_FROM },
      { 0, "A/g",   "normal", 1, "f", TRUE },
      { 0, "A/B/g", "normal", 1, "f", TRUE },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  SVN_ERR(sbox_wc_copy(&b, "A", "X"));
  {
    nodes_row_t rows[] = {
      { 0, "",      "normal", 1, "" },
      { 0, "f",     "normal", 1, "f" },
      { 0, "A",     "normal", 1, "A" },
      { 2, "A/B",   "normal", NO_COPY_FROM },
      { 0, "A/g",   "normal", 1, "f", TRUE },
      { 0, "A/B/g", "normal", 1, "f", TRUE },
      { 1, "X",     "normal", 1, "A" },
      { 2, "X/B",   "normal", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  SVN_ERR(sbox_wc_update(&b, "", 1));
  {
    nodes_row_t rows[] = {
      { 0, "",      "normal", 1, "" },
      { 0, "f",     "normal", 1, "f" },
      { 0, "A",     "normal", 1, "A" },
      { 2, "A/B",   "normal", NO_COPY_FROM },
      { 0, "A/g",   "normal", 1, "f", TRUE },
      { 0, "A/B/g", "normal", 1, "f", TRUE },
      { 1, "X",     "normal", 1, "A" },
      { 2, "X/B",   "normal", NO_COPY_FROM },
      { 0, "X/g",   "normal", 1, "f", TRUE },
      { 0, "X/B/g", "normal", 1, "f", TRUE },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  SVN_ERR(sbox_wc_delete(&b, "X"));
  {
    nodes_row_t rows[] = {
      { 0, "",      "normal", 1, "" },
      { 0, "f",     "normal", 1, "f" },
      { 0, "A",     "normal", 1, "A" },
      { 2, "A/B",   "normal", NO_COPY_FROM },
      { 0, "A/g",   "normal", 1, "f", TRUE },
      { 0, "A/B/g", "normal", 1, "f", TRUE },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  SVN_ERR(sbox_wc_update(&b, "", 1));
  {
    nodes_row_t rows[] = {
      { 0, "",      "normal", 1, "" },
      { 0, "f",     "normal", 1, "f" },
      { 0, "A",     "normal", 1, "A" },
      { 2, "A/B",   "normal", NO_COPY_FROM },
      { 0, "A/g",   "normal", 1, "f", TRUE },
      { 0, "A/B/g", "normal", 1, "f", TRUE },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "", rows));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_wc_wc_server_excluded(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  nodes_row_t before[] = {
    {0, "",      "normal",  1, ""},
    {0, "A",     "normal",  1, "A"},
    {0, "A/B",   "normal",  1, "A/B"},
    {0, "A/B/E", "server-excluded",  1, "A/B/E"},
    {0}
  };
  nodes_row_t after[] = {
    {0, "",      "normal",  1, ""},
    {0, "A",     "normal",      1, "A"},
    {0, "A/B",   "normal",      1, "A/B"},
    {0, "A/B/E", "server-excluded",      1, "A/B/E"},
    {1, "X",     "normal",      1, "A"},
    {1, "X/B",   "normal",      1, "A/B"},
    {1, "X/B/E", "incomplete",  1, "A/B/E"},
    {0}
  };
  svn_error_t *err;

  SVN_ERR(svn_test__sandbox_create(&b, "copy_wc_wc_server_excluded", opts, pool));
  SVN_ERR(insert_dirs(&b, before));
  SVN_ERR(check_db_rows(&b, "", before));
  SVN_ERR(sbox_disk_mkdir(&b, "A"));
  err = sbox_wc_copy(&b, "A", "X");
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_PATH_UNEXPECTED_STATUS);
  SVN_ERR(check_db_rows(&b, "", after));

  return SVN_NO_ERROR;
}

/* Issue 4040 */
static svn_error_t *
incomplete_switch(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "incomplete_switch", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/D"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_copy(&b, "A", "X"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_copy(&b, "A", "X/A"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_delete(&b, "X/A"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  {
    /* Interrupted switch from A@1 to X@3 */
    nodes_row_t before[] = {
      {0, "",      "incomplete", 3, "X"},
      {0, "A",     "incomplete", 3, "X/A"},
      {0, "A/B",   "incomplete", 3, "X/A/B"},
      {0, "A/B/C", "incomplete", 3, "X/A/B/C"},
      {0, "B",     "normal",     1, "A/B"},
      {0, "B/C",   "normal",     1, "A/B/C"},
      {0, "B/C/D", "normal",     1, "A/B/C/D"},
      {0}
    };

    nodes_row_t after_update[] = {
      {0, "",      "normal", 4, "X"},
      {0, "B",     "normal", 4, "A/B"},
      {0, "B/C",   "normal", 4, "A/B/C"},
      {0, "B/C/D", "normal", 4, "A/B/C/D"},
      {0}
    };

    SVN_ERR(insert_dirs(&b, before));
    SVN_ERR(svn_io_remove_dir2(sbox_wc_path(&b, "A/B/C/D"), FALSE,
                               NULL, NULL, pool));
    SVN_ERR(check_db_rows(&b, "", before));
    SVN_ERR(sbox_wc_update(&b, "", 4));
    SVN_ERR(check_db_rows(&b, "", after_update));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
nested_moves_child_first(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "nested_moves_child_first", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  {
    nodes_row_t nodes[] = {
      {0, "",      "normal", 1, ""},
      {0, "A",     "normal", 1, "A"},
      {0, "A/B",   "normal", 1, "A/B"},
      {0, "A/B/C", "normal", 1, "A/B/C"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  SVN_ERR(sbox_wc_move(&b, "A/B/C", "A/B/C2"));
  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",       1, ""},
      {0, "A",      "normal",       1, "A"},
      {0, "A/B",    "normal",       1, "A/B"},
      {0, "A/B/C",  "normal",       1, "A/B/C"},
      {3, "A/B/C",  "base-deleted", NO_COPY_FROM, "A/B/C2"},
      {3, "A/B/C2", "normal",       1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  SVN_ERR(sbox_wc_move(&b, "A/B", "A/B2"));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "A/B/C",   "normal",       1, "A/B/C"},
      {2, "A/B",     "base-deleted", NO_COPY_FROM, "A/B2"},
      {2, "A/B/C",   "base-deleted", NO_COPY_FROM},
      {2, "A/B2",    "normal",       1, "A/B",   MOVED_HERE},
      {2, "A/B2/C",  "normal",       1, "A/B/C", MOVED_HERE},
      {3, "A/B2/C",  "base-deleted", NO_COPY_FROM, "A/B2/C2"},
      {3, "A/B2/C2", "normal",       1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "A/B/C",   "normal",       1, "A/B/C"},
      {1, "A",       "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",   "base-deleted", NO_COPY_FROM},
      {1, "A2",      "normal",       1, "A",     MOVED_HERE},
      {1, "A2/B",    "normal",       1, "A/B",   MOVED_HERE},
      {1, "A2/B/C",  "normal",       1, "A/B/C", MOVED_HERE},
      {2, "A2/B",    "base-deleted", NO_COPY_FROM, "A2/B2"},
      {2, "A2/B/C",  "base-deleted", NO_COPY_FROM},
      {2, "A2/B2",   "normal",       1, "A/B",   MOVED_HERE},
      {2, "A2/B2/C", "normal",       1, "A/B/C", MOVED_HERE},
      {3, "A2/B2/C", "base-deleted", NO_COPY_FROM, "A2/B2/C2"},
      {3, "A2/B2/C2","normal",       1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Revert should leave the A to A2 move */
  SVN_ERR(sbox_wc_revert(&b, "A2/B2", svn_depth_infinity));
  SVN_ERR(sbox_wc_revert(&b, "A2/B", svn_depth_infinity));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "A/B/C",   "normal",       1, "A/B/C"},
      {1, "A",       "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",   "base-deleted", NO_COPY_FROM},
      {1, "A2",      "normal",       1, "A",     MOVED_HERE},
      {1, "A2/B",    "normal",       1, "A/B",   MOVED_HERE},
      {1, "A2/B/C",  "normal",       1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
nested_moves_child_last(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "nested_moves_child_last", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  {
    nodes_row_t nodes[] = {
      {0, "",      "normal", 1, ""},
      {0, "A",     "normal", 1, "A"},
      {0, "A/B",   "normal", 1, "A/B"},
      {0, "A/B/C", "normal", 1, "A/B/C"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "A/B/C",   "normal",       1, "A/B/C"},
      {1, "A",       "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",   "base-deleted", NO_COPY_FROM},
      {1, "A2",      "normal",       1, "A",     MOVED_HERE},
      {1, "A2/B",    "normal",       1, "A/B",   MOVED_HERE},
      {1, "A2/B/C",  "normal",       1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  SVN_ERR(sbox_wc_move(&b, "A2/B", "A2/B2"));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "A/B/C",   "normal",       1, "A/B/C"},
      {1, "A",       "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",   "base-deleted", NO_COPY_FROM},
      {1, "A2",      "normal",       1, "A",     MOVED_HERE},
      {1, "A2/B",    "normal",       1, "A/B",   MOVED_HERE},
      {1, "A2/B/C",  "normal",       1, "A/B/C", MOVED_HERE},
      {2, "A2/B",    "base-deleted", NO_COPY_FROM, "A2/B2"},
      {2, "A2/B/C",  "base-deleted", NO_COPY_FROM},
      {2, "A2/B2",   "normal",       1, "A/B",   MOVED_HERE},
      {2, "A2/B2/C", "normal",       1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  SVN_ERR(sbox_wc_move(&b, "A2/B2/C", "A2/B2/C2"));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "A/B/C",   "normal",       1, "A/B/C"},
      {1, "A",       "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",   "base-deleted", NO_COPY_FROM},
      {1, "A2",      "normal",       1, "A",     MOVED_HERE},
      {1, "A2/B",    "normal",       1, "A/B",   MOVED_HERE},
      {1, "A2/B/C",  "normal",       1, "A/B/C", MOVED_HERE},
      {2, "A2/B",    "base-deleted", NO_COPY_FROM, "A2/B2"},
      {2, "A2/B/C",  "base-deleted", NO_COPY_FROM},
      {2, "A2/B2",   "normal",       1, "A/B",   MOVED_HERE},
      {2, "A2/B2/C", "normal",       1, "A/B/C", MOVED_HERE},
      {3, "A2/B2/C", "base-deleted", NO_COPY_FROM, "A2/B2/C2"},
      {3, "A2/B2/C2","normal",       1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Revert should leave the A to A2 move */
  SVN_ERR(sbox_wc_revert(&b, "A2/B2", svn_depth_infinity));
  SVN_ERR(sbox_wc_revert(&b, "A2/B", svn_depth_infinity));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "A/B/C",   "normal",       1, "A/B/C"},
      {1, "A",       "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",   "base-deleted", NO_COPY_FROM},
      {1, "A2",      "normal",       1, "A",     MOVED_HERE},
      {1, "A2/B",    "normal",       1, "A/B",   MOVED_HERE},
      {1, "A2/B/C",  "normal",       1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_in_copy(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_in_copy", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_copy(&b, "A", "A2"));

  {
    nodes_row_t nodes[] = {
      {0, "",     "normal", 1, ""},
      {0, "A",    "normal", 1, "A"},
      {0, "A/B",  "normal", 1, "A/B"},
      {1, "A2",   "normal", 1, "A"},
      {1, "A2/B", "normal", 1, "A/B"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  SVN_ERR(sbox_wc_move(&b, "A2/B", "A2/B2"));
  {
    nodes_row_t nodes[] = {
      {0, "",      "normal",       1, ""},
      {0, "A",     "normal",       1, "A"},
      {0, "A/B",   "normal",       1, "A/B"},
      {1, "A2",    "normal",       1, "A"},
      {1, "A2/B",  "normal",       1, "A/B"},
      {2, "A2/B",  "base-deleted", NO_COPY_FROM, "A2/B2"},
      {2, "A2/B2", "normal",       1, "A/B", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_in_replace(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_in_replace", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "X"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/B"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_delete(&b, "A"));
  SVN_ERR(sbox_wc_copy(&b, "X", "A"));

  {
    nodes_row_t nodes[] = {
      {0, "",    "normal", 1, ""},
      {0, "A",   "normal", 1, "A"},
      {0, "A/B", "normal", 1, "A/B"},
      {0, "X",   "normal", 1, "X"},
      {0, "X/B", "normal", 1, "X/B"},
      {1, "A",   "normal", 1, "X"},
      {1, "A/B", "normal", 1, "X/B"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  SVN_ERR(sbox_wc_move(&b, "A/B", "A/B2"));
  {
    nodes_row_t nodes[] = {
      {0, "",     "normal",       1, ""},
      {0, "A",    "normal",       1, "A"},
      {0, "A/B",  "normal",       1, "A/B"},
      {0, "X",    "normal",       1, "X"},
      {0, "X/B",  "normal",       1, "X/B"},
      {1, "A",    "normal",       1, "X"},
      {1, "A/B",  "normal",       1, "X/B"},
      {2, "A/B",  "base-deleted", NO_COPY_FROM, "A/B2"},
      {2, "A/B2", "normal",       1, "X/B", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_a_move(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "copy_a_move", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  {
    nodes_row_t nodes[] = {
      {0, "",      "normal", 1, ""},
      {0, "A",     "normal", 1, "A"},
      {0, "A/B",   "normal", 1, "A/B"},
      {0, "A/B/C", "normal", 1, "A/B/C"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  SVN_ERR(sbox_wc_move(&b, "A/B/C", "A/C2"));
  {
    nodes_row_t nodes[] = {
      {0, "",      "normal",       1, ""},
      {0, "A",     "normal",       1, "A"},
      {0, "A/B",   "normal",       1, "A/B"},
      {0, "A/B/C", "normal",       1, "A/B/C"},
      {2, "A/C2",  "normal",       1, "A/B/C", MOVED_HERE},
      {3, "A/B/C", "base-deleted", NO_COPY_FROM, "A/C2"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  /* Copying a move doesn't copy any moved-to/here artifacts, which
     means that moving inside a copy is not the same as copying
     something that contains a move?  Is this behaviour correct? */
  SVN_ERR(sbox_wc_copy(&b, "A", "A2"));
  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",       1, ""},
      {0, "A",      "normal",       1, "A"},
      {0, "A/B",    "normal",       1, "A/B"},
      {0, "A/B/C",  "normal",       1, "A/B/C"},
      {2, "A/C2",   "normal",       1, "A/B/C", MOVED_HERE},
      {3, "A/B/C",  "base-deleted", NO_COPY_FROM, "A/C2"},
      {1, "A2",     "normal",       1, "A"},
      {1, "A2/B",   "normal",       1, "A/B"},
      {1, "A2/B/C", "normal",       1, "A/B/C"},
      {2, "A2/C2",  "normal",       1, "A/B/C"},   /* MOVED_HERE? */
      {3, "A2/B/C", "base-deleted", NO_COPY_FROM}, /* "A2/C2"? */
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_to_swap(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_to_swap", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "X"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/Y"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A/B", "X/B"));
  SVN_ERR(sbox_wc_move(&b, "X/Y", "A/Y"));

  {
    nodes_row_t nodes[] = {
      {0, "",    "normal",       1, ""},
      {0, "A",   "normal",       1, "A"},
      {0, "A/B", "normal",       1, "A/B"},
      {0, "X",   "normal",       1, "X"},
      {0, "X/Y", "normal",       1, "X/Y"},
      {2, "A/B", "base-deleted", NO_COPY_FROM, "X/B"},
      {2, "A/Y", "normal",       1, "X/Y", MOVED_HERE},
      {2, "X/Y", "base-deleted", NO_COPY_FROM, "A/Y"},
      {2, "X/B", "normal",       1, "A/B", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_move(&b, "A", "A2"));

  {
    nodes_row_t nodes[] = {
      {0, "",     "normal",       1, ""},
      {0, "A",    "normal",       1, "A"},
      {0, "A/B",  "normal",       1, "A/B"},
      {0, "X",    "normal",       1, "X"},
      {0, "X/Y",  "normal",       1, "X/Y"},
      {1, "A",    "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",  "base-deleted", NO_COPY_FROM},
      {1, "A2",   "normal",       1, "A", MOVED_HERE},
      {1, "A2/B", "normal",       1, "A/B", MOVED_HERE},
      {2, "A2/B", "base-deleted", NO_COPY_FROM, "X/B"},
      {2, "A2/Y", "normal",       1, "X/Y", MOVED_HERE},
      {2, "X/Y",  "base-deleted", NO_COPY_FROM, "A2/Y"},
      {2, "X/B",  "normal",       1, "A/B", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_move(&b, "X", "A"));

  {
    nodes_row_t nodes[] = {
      {0, "",     "normal",       1, ""},
      {0, "A",    "normal",       1, "A"},
      {0, "A/B",  "normal",       1, "A/B"},
      {0, "X",    "normal",       1, "X"},
      {0, "X/Y",  "normal",       1, "X/Y"},
      {1, "A",    "normal",       1, "X", FALSE, "A2", TRUE},
      {1, "A/B",  "base-deleted", NO_COPY_FROM},
      {1, "A/Y",  "normal",       1, "X/Y", MOVED_HERE},
      {1, "A2",   "normal",       1, "A", MOVED_HERE},
      {1, "A2/B", "normal",       1, "A/B", MOVED_HERE},
      {1, "X",    "base-deleted", NO_COPY_FROM, "A"},
      {1, "X/Y",  "base-deleted", NO_COPY_FROM},
      {2, "A/B",  "normal",       1, "A/B", MOVED_HERE},
      {2, "A/Y",  "base-deleted", NO_COPY_FROM, "A2/Y"},
      {2, "A2/B", "base-deleted", NO_COPY_FROM, "A/B"},
      {2, "A2/Y", "normal",       1, "X/Y", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_move(&b, "A2", "X"));

  {
    nodes_row_t nodes[] = {
      {0, "",    "normal",       1, ""},
      {0, "A",   "normal",       1, "A"},
      {0, "A/B", "normal",       1, "A/B"},
      {0, "X",   "normal",       1, "X"},
      {0, "X/Y", "normal",       1, "X/Y"},
      {1, "A",   "normal",       1, "X",   FALSE, "X", TRUE},
      {1, "A/Y", "normal",       1, "X/Y", MOVED_HERE},
      {1, "A/B", "base-deleted", NO_COPY_FROM},
      {1, "X",   "normal",       1, "A",   FALSE, "A", TRUE},
      {1, "X/B", "normal",       1, "A/B", MOVED_HERE},
      {1, "X/Y", "base-deleted", NO_COPY_FROM},
      {2, "A/Y", "base-deleted", NO_COPY_FROM, "X/Y"},
      {2, "X/B", "base-deleted", NO_COPY_FROM, "A/B"},
      {2, "A/B", "normal",       1, "A/B", MOVED_HERE},
      {2, "X/Y", "normal",       1, "X/Y", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Revert and try in different order */
  SVN_ERR(sbox_wc_revert(&b, "", svn_depth_infinity));

  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  SVN_ERR(sbox_wc_move(&b, "X", "A"));
  SVN_ERR(sbox_wc_move(&b, "A2", "X"));

  {
    nodes_row_t nodes[] = {
      {0, "",    "normal",       1, ""},
      {0, "A",   "normal",       1, "A"},
      {0, "A/B", "normal",       1, "A/B"},
      {0, "X",   "normal",       1, "X"},
      {0, "X/Y", "normal",       1, "X/Y"},
      {1, "A",   "normal",       1, "X",   FALSE, "X", TRUE},
      {1, "A/Y", "normal",       1, "X/Y", MOVED_HERE},
      {1, "A/B", "base-deleted", NO_COPY_FROM},
      {1, "X",   "normal",       1, "A",   FALSE, "A", TRUE},
      {1, "X/B", "normal",       1, "A/B", MOVED_HERE},
      {1, "X/Y", "base-deleted", NO_COPY_FROM},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_move(&b, "A/Y", "X/Y"));
  SVN_ERR(sbox_wc_move(&b, "X/B", "A/B"));

  {
    nodes_row_t nodes[] = {
      {0, "",    "normal",       1, ""},
      {0, "A",   "normal",       1, "A"},
      {0, "A/B", "normal",       1, "A/B"},
      {0, "X",   "normal",       1, "X"},
      {0, "X/Y", "normal",       1, "X/Y"},
      {1, "A",   "normal",       1, "X",   FALSE, "X", TRUE},
      {1, "A/Y", "normal",       1, "X/Y", MOVED_HERE},
      {1, "A/B", "base-deleted", NO_COPY_FROM},
      {1, "X",   "normal",       1, "A",   FALSE, "A", TRUE},
      {1, "X/B", "normal",       1, "A/B", MOVED_HERE},
      {1, "X/Y", "base-deleted", NO_COPY_FROM},
      {2, "A/Y", "base-deleted", NO_COPY_FROM, "X/Y"},
      {2, "X/B", "base-deleted", NO_COPY_FROM, "A/B"},
      {2, "A/B", "normal",       1, "A/B", MOVED_HERE},
      {2, "X/Y", "normal",       1, "X/Y", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* And move this last bit back and check if the db state is restored */
  SVN_ERR(sbox_wc_move(&b, "A/B", "X/B"));
  SVN_ERR(sbox_wc_move(&b, "X/Y", "A/Y"));

  {
    /* Exact the same as before the initial moves */
    nodes_row_t nodes[] = {
      {0, "",    "normal",       1, ""},
      {0, "A",   "normal",       1, "A"},
      {0, "A/B", "normal",       1, "A/B"},
      {0, "X",   "normal",       1, "X"},
      {0, "X/Y", "normal",       1, "X/Y"},
      {1, "A",   "normal",       1, "X",   FALSE, "X", TRUE},
      {1, "A/Y", "normal",       1, "X/Y", MOVED_HERE},
      {1, "A/B", "base-deleted", NO_COPY_FROM},
      {1, "X",   "normal",       1, "A",   FALSE, "A", TRUE},
      {1, "X/B", "normal",       1, "A/B", MOVED_HERE},
      {1, "X/Y", "base-deleted", NO_COPY_FROM},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* And try to undo the rest */
  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  SVN_ERR(sbox_wc_move(&b, "X", "A"));
  SVN_ERR(sbox_wc_move(&b, "A2", "X"));

  {
    nodes_row_t nodes[] = {
      {0, "",    "normal",       1, ""},
      {0, "A",   "normal",       1, "A"},
      {0, "A/B", "normal",       1, "A/B"},
      {0, "X",   "normal",       1, "X"},
      {0, "X/Y", "normal",       1, "X/Y"},

      {0}
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
revert_nested_move(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  nodes_row_t nodes_A_moved[] = {
    {0, "",       "normal",       1, ""},
    {0, "A",      "normal",       1, "A"},
    {0, "A/B",    "normal",       1, "A/B"},
    {0, "A/B/C",  "normal",       1, "A/B/C"},
    {1, "A",      "base-deleted", NO_COPY_FROM, "A2"},
    {1, "A/B",    "base-deleted", NO_COPY_FROM},
    {1, "A/B/C",  "base-deleted", NO_COPY_FROM},
    {1, "A2",     "normal",       1, "A",     MOVED_HERE},
    {1, "A2/B",   "normal",       1, "A/B",   MOVED_HERE},
    {1, "A2/B/C", "normal",       1, "A/B/C", MOVED_HERE},
    {0}
  };
  nodes_row_t nodes_AB_moved[] = {
    {0, "",        "normal",       1, ""},
    {0, "A",       "normal",       1, "A"},
    {0, "A/B",     "normal",       1, "A/B"},
    {0, "A/B/C",   "normal",       1, "A/B/C"},
    {1, "A",       "base-deleted", NO_COPY_FROM, "A2"},
    {1, "A/B",     "base-deleted", NO_COPY_FROM},
    {1, "A/B/C",   "base-deleted", NO_COPY_FROM},
    {1, "A2",      "normal",       1, "A",     MOVED_HERE},
    {1, "A2/B",    "normal",       1, "A/B",   MOVED_HERE},
    {1, "A2/B/C",  "normal",       1, "A/B/C", MOVED_HERE},
    {2, "A2/B",    "base-deleted", NO_COPY_FROM, "A2/B2"},
    {2, "A2/B/C",  "base-deleted", NO_COPY_FROM},
    {2, "A2/B2",   "normal",       1, "A/B",   MOVED_HERE},
    {2, "A2/B2/C", "normal",       1, "A/B/C", MOVED_HERE},
    {0}
  };
  nodes_row_t nodes_ABC_moved[] = {
    {0, "",         "normal",       1, ""},
    {0, "A",        "normal",       1, "A"},
    {0, "A/B",      "normal",       1, "A/B"},
    {0, "A/B/C",    "normal",       1, "A/B/C"},
    {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
    {1, "A/B",      "base-deleted", NO_COPY_FROM},
    {1, "A/B/C",    "base-deleted", NO_COPY_FROM},
    {1, "A2",       "normal",       1, "A",     MOVED_HERE},
    {1, "A2/B",     "normal",       1, "A/B",   MOVED_HERE},
    {1, "A2/B/C",   "normal",       1, "A/B/C", MOVED_HERE},
    {2, "A2/B",     "base-deleted", NO_COPY_FROM, "A2/B2"},
    {2, "A2/B/C",   "base-deleted", NO_COPY_FROM},
    {2, "A2/B2",    "normal",       1, "A/B",   MOVED_HERE},
    {2, "A2/B2/C",  "normal",       1, "A/B/C", MOVED_HERE},
    {3, "A2/B2/C",  "base-deleted", NO_COPY_FROM, "A2/B2/C2"},
    {3, "A2/B2/C2", "normal",       1, "A/B/C", MOVED_HERE},
    {0}
  };
  nodes_row_t nodes_AB_moved_C_copied[] = {
    {0, "",         "normal",       1, ""},
    {0, "A",        "normal",       1, "A"},
    {0, "A/B",      "normal",       1, "A/B"},
    {0, "A/B/C",    "normal",       1, "A/B/C"},
    {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
    {1, "A/B",      "base-deleted", NO_COPY_FROM},
    {1, "A/B/C",    "base-deleted", NO_COPY_FROM},
    {1, "A2",       "normal",       1, "A",     MOVED_HERE},
    {1, "A2/B",     "normal",       1, "A/B",   MOVED_HERE},
    {1, "A2/B/C",   "normal",       1, "A/B/C", MOVED_HERE},
    {2, "A2/B",     "base-deleted", NO_COPY_FROM, "A2/B2"},
    {2, "A2/B/C",   "base-deleted", NO_COPY_FROM},
    {2, "A2/B2",    "normal",       1, "A/B",   MOVED_HERE},
    {2, "A2/B2/C",  "normal",       1, "A/B/C", MOVED_HERE},
    {3, "A2/B2/C2", "normal",       1, "A/B/C"},
    {0}
  };
  nodes_row_t nodes_AC_moved_B_copied[] = {
    {0, "",         "normal",       1, ""},
    {0, "A",        "normal",       1, "A"},
    {0, "A/B",      "normal",       1, "A/B"},
    {0, "A/B/C",    "normal",       1, "A/B/C"},
    {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
    {1, "A/B",      "base-deleted", NO_COPY_FROM},
    {1, "A/B/C",    "base-deleted", NO_COPY_FROM},
    {1, "A2",       "normal",       1, "A",     MOVED_HERE},
    {1, "A2/B",     "normal",       1, "A/B",   MOVED_HERE},
    {1, "A2/B/C",   "normal",       1, "A/B/C", MOVED_HERE},
    {2, "A2/B2",    "normal",       1, "A/B"},
    {2, "A2/B2/C",  "normal",       1, "A/B/C"},
    {3, "A2/B2/C",  "base-deleted", NO_COPY_FROM, "A2/B2/C2"},
    {3, "A2/B2/C2", "normal",       1, "A/B/C", MOVED_HERE},
    {0}
  };

  SVN_ERR(svn_test__sandbox_create(&b, "revert_nested_move", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  SVN_ERR(check_db_rows(&b, "", nodes_A_moved));

  SVN_ERR(sbox_wc_move(&b, "A2/B", "A2/B2"));
  SVN_ERR(check_db_rows(&b, "", nodes_AB_moved));

  SVN_ERR(sbox_wc_move(&b, "A2/B2/C", "A2/B2/C2"));
  SVN_ERR(check_db_rows(&b, "", nodes_ABC_moved));

  SVN_ERR(sbox_wc_revert(&b, "A2/B", svn_depth_infinity));
  SVN_ERR(sbox_wc_revert(&b, "A2/B2", svn_depth_infinity));
  SVN_ERR(check_db_rows(&b, "", nodes_A_moved));

  SVN_ERR(sbox_wc_move(&b, "A2/B", "A2/B2"));
  SVN_ERR(sbox_wc_move(&b, "A2/B2/C", "A2/B2/C2"));
  SVN_ERR(check_db_rows(&b, "", nodes_ABC_moved));

  SVN_ERR(sbox_wc_revert(&b, "A2/B2/C", svn_depth_empty));
  SVN_ERR(check_db_rows(&b, "", nodes_AB_moved_C_copied));
  SVN_ERR(sbox_wc_revert(&b, "A2/B2/C2", svn_depth_infinity));
  SVN_ERR(check_db_rows(&b, "", nodes_AB_moved));

  SVN_ERR(sbox_wc_move(&b, "A2/B2/C", "A2/B2/C2"));
  SVN_ERR(check_db_rows(&b, "", nodes_ABC_moved));

  SVN_ERR(sbox_wc_revert(&b, "A2/B2/C", svn_depth_infinity));
  SVN_ERR(check_db_rows(&b, "", nodes_AB_moved_C_copied));
  SVN_ERR(sbox_wc_revert(&b, "A2/B2/C2", svn_depth_infinity));
  SVN_ERR(check_db_rows(&b, "", nodes_AB_moved));

  SVN_ERR(sbox_wc_revert(&b, "A2/B", svn_depth_infinity));
  SVN_ERR(sbox_wc_revert(&b, "A2/B2", svn_depth_infinity));
  SVN_ERR(check_db_rows(&b, "", nodes_A_moved));

  /* Check moves in reverse order */
  SVN_ERR(sbox_wc_revert(&b, "", svn_depth_infinity));
  SVN_ERR(sbox_wc_move(&b, "A/B/C", "A/B/C2"));
  SVN_ERR(sbox_wc_move(&b, "A/B", "A/B2"));
  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  SVN_ERR(check_db_rows(&b, "", nodes_ABC_moved));

  SVN_ERR(sbox_wc_revert(&b, "A2/B", svn_depth_infinity));
  SVN_ERR(check_db_rows(&b, "", nodes_AC_moved_B_copied));

  return SVN_NO_ERROR;
}

static svn_error_t *
move_on_move(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_on_move", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "X"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/B"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A/B", "B2"));
  SVN_ERR(sbox_wc_delete(&b, "A"));
  SVN_ERR(sbox_wc_copy(&b, "X", "A"));

  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       1, "A"},
      {0, "A/B",      "normal",       1, "A/B"},
      {0, "X",        "normal",       1, "X"},
      {0, "X/B",      "normal",       1, "X/B"},
      {1, "B2",       "normal",       1, "A/B", MOVED_HERE},
      {1, "A",        "normal",       1, "X"},
      {1, "A/B",      "normal",       1, "X/B", FALSE, "B2"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_move(&b, "A/B", "B3"));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       1, "A"},
      {0, "A/B",      "normal",       1, "A/B"},
      {0, "X",        "normal",       1, "X"},
      {0, "X/B",      "normal",       1, "X/B"},
      {1, "B2",       "normal",       1, "A/B",   MOVED_HERE},
      {1, "B3",       "normal",       1, "X/B",   MOVED_HERE},
      {1, "A",        "normal",       1, "X"},
      {1, "A/B",      "normal",       1, "X/B", FALSE, "B2"},
      {2, "A/B",      "base-deleted", NO_COPY_FROM, "B3"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_on_move2(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_on_move2", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "X"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/B"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  SVN_ERR(sbox_wc_delete(&b, "A"));
  SVN_ERR(sbox_wc_copy(&b, "X", "A"));

  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       1, "A"},
      {0, "A/B",      "normal",       1, "A/B"},
      {0, "X",        "normal",       1, "X"},
      {0, "X/B",      "normal",       1, "X/B"},
      {1, "A2",       "normal",       1, "A",   MOVED_HERE},
      {1, "A2/B",     "normal",       1, "A/B", MOVED_HERE},
      {1, "A",        "normal",       1, "X", FALSE, "A2"},
      {1, "A/B",      "normal",       1, "X/B"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_move(&b, "A/B", "B3"));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       1, "A"},
      {0, "A/B",      "normal",       1, "A/B"},
      {0, "X",        "normal",       1, "X"},
      {0, "X/B",      "normal",       1, "X/B"},
      {1, "A2",       "normal",       1, "A",   MOVED_HERE},
      {1, "A2/B",     "normal",       1, "A/B", MOVED_HERE},
      {1, "B3",       "normal",       1, "X/B", MOVED_HERE},
      {1, "A",        "normal",       1, "X", FALSE, "A2"},
      {1, "A/B",      "normal",       1, "X/B"},
      {2, "A/B",      "base-deleted", NO_COPY_FROM, "B3"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_added(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_added", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  SVN_ERR(sbox_wc_mkdir(&b, "A2/B/C2"));

  /* Both A2/B/C and A2/B/C2 are simple adds inside the move.  It
     doesn't seem right for A2/B/C to be marked moved_here. */
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       1, "A"},
      {0, "A/B",      "normal",       1, "A/B"},
      {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",      "base-deleted", NO_COPY_FROM},
      {1, "A2",       "normal",       1, "A",   MOVED_HERE},
      {1, "A2/B",     "normal",       1, "A/B", MOVED_HERE},
      {3, "A2/B/C",   "normal",       NO_COPY_FROM},
      {3, "A2/B/C2",  "normal",       NO_COPY_FROM},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

/* Test the result of 'update' when the incoming changes are inside a
 * directory that is locally moved. */
static svn_error_t *
move_update(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_update", opts, pool));

  /* r1: Create files 'f', 'h' */
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_file_write(&b, "A/B/f", "r1 content\n"));
  SVN_ERR(sbox_file_write(&b, "A/B/h", "r1 content\n"));
  SVN_ERR(sbox_wc_add(&b, "A/B/f"));
  SVN_ERR(sbox_wc_add(&b, "A/B/h"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  /* r2: Modify 'f' */
  SVN_ERR(sbox_file_write(&b, "A/B/f", "r1 content\nr2 content\n"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  /* r3: Delete 'h', add 'g' */
  SVN_ERR(sbox_file_write(&b, "A/B/g", "r3 content\n"));
  SVN_ERR(sbox_wc_add(&b, "A/B/g"));
  SVN_ERR(sbox_wc_delete(&b, "A/B/h"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  /* r4: Add a new subtree 'X' */
  SVN_ERR(sbox_wc_mkdir(&b, "X"));
  SVN_ERR(sbox_file_write(&b, "X/f", "r4 content\n"));
  SVN_ERR(sbox_file_write(&b, "X/g", "r4 content\n"));
  SVN_ERR(sbox_file_write(&b, "X/h", "r4 content\n"));
  SVN_ERR(sbox_wc_add(&b, "X/f"));
  SVN_ERR(sbox_wc_add(&b, "X/g"));
  SVN_ERR(sbox_wc_add(&b, "X/h"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  /* r5: Add a subtree 'A/B/C' */
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_update(&b, "", 1));

  /* A is single-revision so A2 is a single-revision copy */
  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       1, "A"},
      {0, "A/B",      "normal",       1, "A/B"},
      {0, "A/B/f",    "normal",       1, "A/B/f"},
      {0, "A/B/h",    "normal",       1, "A/B/h"},
      {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/f",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/h",    "base-deleted", NO_COPY_FROM},
      {1, "A2",       "normal",       1, "A", MOVED_HERE},
      {1, "A2/B",     "normal",       1, "A/B", MOVED_HERE},
      {1, "A2/B/f",   "normal",       1, "A/B/f", MOVED_HERE},
      {1, "A2/B/h",   "normal",       1, "A/B/h", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Update causes a tree-conflict on A due to incoming text-change. */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       2, ""},
      {0, "A",        "normal",       2, "A"},
      {0, "A/B",      "normal",       2, "A/B"},
      {0, "A/B/f",    "normal",       2, "A/B/f"},
      {0, "A/B/h",    "normal",       2, "A/B/h"},
      {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/f",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/h",    "base-deleted", NO_COPY_FROM},
      {1, "A2",       "normal",       1, "A", MOVED_HERE},
      {1, "A2/B",     "normal",       1, "A/B", MOVED_HERE},
      {1, "A2/B/f",   "normal",       1, "A/B/f", MOVED_HERE},
      {1, "A2/B/h",   "normal",       1, "A/B/h", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Resolve should update the move. */
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       2, ""},
      {0, "A",        "normal",       2, "A"},
      {0, "A/B",      "normal",       2, "A/B"},
      {0, "A/B/f",    "normal",       2, "A/B/f"},
      {0, "A/B/h",    "normal",       2, "A/B/h"},
      {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/f",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/h",    "base-deleted", NO_COPY_FROM},
      {1, "A2",       "normal",       2, "A", MOVED_HERE},
      {1, "A2/B",     "normal",       2, "A/B", MOVED_HERE},
      {1, "A2/B/f",   "normal",       2, "A/B/f", MOVED_HERE},
      {1, "A2/B/h",   "normal",       2, "A/B/h", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Update causes a tree-conflict on due to incoming add. */
  SVN_ERR(sbox_wc_update(&b, "", 3));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       3, ""},
      {0, "A",        "normal",       3, "A"},
      {0, "A/B",      "normal",       3, "A/B"},
      {0, "A/B/f",    "normal",       3, "A/B/f"},
      {0, "A/B/g",    "normal",       3, "A/B/g"},
      {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/f",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/g",    "base-deleted", NO_COPY_FROM},
      {1, "A2",       "normal",       2, "A", MOVED_HERE},
      {1, "A2/B",     "normal",       2, "A/B", MOVED_HERE},
      {1, "A2/B/f",   "normal",       2, "A/B/f", MOVED_HERE},
      {1, "A2/B/h",   "normal",       2, "A/B/h", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       3, ""},
      {0, "A",        "normal",       3, "A"},
      {0, "A/B",      "normal",       3, "A/B"},
      {0, "A/B/f",    "normal",       3, "A/B/f"},
      {0, "A/B/g",    "normal",       3, "A/B/g"},
      {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/f",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/g",    "base-deleted", NO_COPY_FROM},
      {1, "A2",       "normal",       3, "A", MOVED_HERE},
      {1, "A2/B",     "normal",       3, "A/B", MOVED_HERE},
      {1, "A2/B/f",   "normal",       3, "A/B/f", MOVED_HERE},
      {1, "A2/B/g",   "normal",       3, "A/B/g", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_delete(&b, "A2/B"));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       3, ""},
      {0, "A",        "normal",       3, "A"},
      {0, "A/B",      "normal",       3, "A/B"},
      {0, "A/B/f",    "normal",       3, "A/B/f"},
      {0, "A/B/g",    "normal",       3, "A/B/g"},
      {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/f",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/g",    "base-deleted", NO_COPY_FROM},
      {1, "A2",       "normal",       3, "A", MOVED_HERE},
      {1, "A2/B",     "normal",       3, "A/B", MOVED_HERE},
      {1, "A2/B/f",   "normal",       3, "A/B/f", MOVED_HERE},
      {1, "A2/B/g",   "normal",       3, "A/B/g", MOVED_HERE},
      {2, "A2/B",     "base-deleted", NO_COPY_FROM},
      {2, "A2/B/f",   "base-deleted", NO_COPY_FROM},
      {2, "A2/B/g",   "base-deleted", NO_COPY_FROM},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "", 2));
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       2, ""},
      {0, "A",        "normal",       2, "A"},
      {0, "A/B",      "normal",       2, "A/B"},
      {0, "A/B/f",    "normal",       2, "A/B/f"},
      {0, "A/B/h",    "normal",       2, "A/B/h"},
      {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/f",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/h",    "base-deleted", NO_COPY_FROM},
      {1, "A2",       "normal",       2, "A", MOVED_HERE},
      {1, "A2/B",     "normal",       2, "A/B", MOVED_HERE},
      {1, "A2/B/f",   "normal",       2, "A/B/f", MOVED_HERE},
      {1, "A2/B/h",   "normal",       2, "A/B/h", MOVED_HERE},
      {2, "A2/B",     "base-deleted", NO_COPY_FROM},
      {2, "A2/B/f",   "base-deleted", NO_COPY_FROM},
      {2, "A2/B/h",   "base-deleted", NO_COPY_FROM},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "X", 4));
  SVN_ERR(sbox_wc_copy(&b, "X", "A2/B"));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       2, ""},
      {0, "A",        "normal",       2, "A"},
      {0, "A/B",      "normal",       2, "A/B"},
      {0, "A/B/f",    "normal",       2, "A/B/f"},
      {0, "A/B/h",    "normal",       2, "A/B/h"},
      {0, "X",        "normal",       4, "X"},
      {0, "X/f",      "normal",       4, "X/f"},
      {0, "X/g",      "normal",       4, "X/g"},
      {0, "X/h",      "normal",       4, "X/h"},
      {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/f",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/h",    "base-deleted", NO_COPY_FROM},
      {1, "A2",       "normal",       2, "A", MOVED_HERE},
      {1, "A2/B",     "normal",       2, "A/B", MOVED_HERE},
      {1, "A2/B/f",   "normal",       2, "A/B/f", MOVED_HERE},
      {1, "A2/B/h",   "normal",       2, "A/B/h", MOVED_HERE},
      {2, "A2/B",     "normal",       4, "X"},
      {2, "A2/B/f",   "normal",       4, "X/f"},
      {2, "A2/B/g",   "normal",       4, "X/g"},
      {2, "A2/B/h",   "normal",       4, "X/h"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "", 4));
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       4, ""},
      {0, "A",        "normal",       4, "A"},
      {0, "A/B",      "normal",       4, "A/B"},
      {0, "A/B/f",    "normal",       4, "A/B/f"},
      {0, "A/B/g",    "normal",       4, "A/B/g"},
      {0, "X",        "normal",       4, "X"},
      {0, "X/f",      "normal",       4, "X/f"},
      {0, "X/g",      "normal",       4, "X/g"},
      {0, "X/h",      "normal",       4, "X/h"},
      {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/f",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/g",    "base-deleted", NO_COPY_FROM},
      {1, "A2",       "normal",       4, "A", MOVED_HERE},
      {1, "A2/B",     "normal",       4, "A/B", MOVED_HERE},
      {1, "A2/B/f",   "normal",       4, "A/B/f", MOVED_HERE},
      {1, "A2/B/g",   "normal",       4, "A/B/g", MOVED_HERE},
      {2, "A2/B",     "normal",       4, "X"},
      {2, "A2/B/f",   "normal",       4, "X/f"},
      {2, "A2/B/g",   "normal",       4, "X/g"},
      {2, "A2/B/h",   "normal",       4, "X/h"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "", 5));
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       5, ""},
      {0, "A",        "normal",       5, "A"},
      {0, "A/B",      "normal",       5, "A/B"},
      {0, "A/B/f",    "normal",       5, "A/B/f"},
      {0, "A/B/g",    "normal",       5, "A/B/g"},
      {0, "A/B/C",    "normal",       5, "A/B/C"},
      {0, "X",        "normal",       5, "X"},
      {0, "X/f",      "normal",       5, "X/f"},
      {0, "X/g",      "normal",       5, "X/g"},
      {0, "X/h",      "normal",       5, "X/h"},
      {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/f",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/g",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",    "base-deleted", NO_COPY_FROM},
      {1, "A2",       "normal",       5, "A", MOVED_HERE},
      {1, "A2/B",     "normal",       5, "A/B", MOVED_HERE},
      {1, "A2/B/f",   "normal",       5, "A/B/f", MOVED_HERE},
      {1, "A2/B/g",   "normal",       5, "A/B/g", MOVED_HERE},
      {1, "A2/B/C",   "normal",       5, "A/B/C", MOVED_HERE},
      {2, "A2/B",     "normal",       4, "X"},
      {2, "A2/B/f",   "normal",       4, "X/f"},
      {2, "A2/B/g",   "normal",       4, "X/g"},
      {2, "A2/B/h",   "normal",       4, "X/h"},
      {2, "A2/B/C",   "base-deleted", NO_COPY_FROM},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }


  return SVN_NO_ERROR;
}

static svn_error_t *
check_moved_to(apr_array_header_t *moved_tos,
               int i,
               int op_depth,
               const char *local_relpath)
{
  struct svn_wc__db_moved_to_t *moved_to;

  if (i >= moved_tos->nelts)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "moved-to %d not found", i);

  moved_to = APR_ARRAY_IDX(moved_tos, i, struct svn_wc__db_moved_to_t *);

  if (moved_to->op_depth != op_depth
      || strcmp(moved_to->local_relpath, local_relpath))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "expected: {%d '%s'} found[%d]: {%d '%s'}",
                             op_depth, local_relpath, i,
                             moved_to->op_depth, moved_to->local_relpath);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_scan_delete(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  const char *moved_to_abspath, *moved_to_op_root_abspath;
  apr_array_header_t *moved_tos;

  SVN_ERR(svn_test__sandbox_create(&b, "scan_delete", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A2"));
  SVN_ERR(sbox_wc_mkdir(&b, "A2/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "C2"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A2", "X"));
  SVN_ERR(sbox_wc_move(&b, "X/B", "Z"));
  SVN_ERR(sbox_wc_move(&b, "A/B", "X/B"));
  SVN_ERR(sbox_wc_move(&b, "X/B/C", "Y"));
  SVN_ERR(sbox_wc_move(&b, "C2", "X/B/C"));

  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       1, "A"},
      {0, "A/B",      "normal",       1, "A/B"},
      {0, "A/B/C",    "normal",       1, "A/B/C"},
      {0, "A2",       "normal",       1, "A2"},
      {0, "A2/B",     "normal",       1, "A2/B"},
      {0, "C2",       "normal",       1, "C2"},
      {1, "A2",       "base-deleted", NO_COPY_FROM, "X"},
      {1, "A2/B",     "base-deleted", NO_COPY_FROM},
      {1, "Z",        "normal",       1, "A2/B", MOVED_HERE},
      {1, "X",        "normal",       1, "A2", MOVED_HERE},
      {1, "X/B",      "normal",       1, "A2/B", MOVED_HERE},
      {2, "A/B",      "base-deleted", NO_COPY_FROM, "X/B"},
      {2, "A/B/C",    "base-deleted", NO_COPY_FROM},
      {2, "X/B",      "normal",       1, "A/B", FALSE, "Z", TRUE},
      {2, "X/B/C",    "normal",       1, "A/B/C", MOVED_HERE},
      {1, "Y",        "normal",       1, "A/B/C", MOVED_HERE},
      {1, "C2",       "base-deleted", NO_COPY_FROM, "X/B/C"},
      {3, "X/B/C",    "normal",       1, "C2", FALSE, "Y", TRUE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(svn_wc__db_scan_deletion(NULL, &moved_to_abspath,
                                   NULL, &moved_to_op_root_abspath,
                                   b.wc_ctx->db, sbox_wc_path(&b, "C2"),
                                   pool, pool));
  SVN_TEST_STRING_ASSERT(moved_to_abspath, sbox_wc_path(&b, "X/B/C"));
  SVN_TEST_STRING_ASSERT(moved_to_op_root_abspath, sbox_wc_path(&b, "X/B/C"));

  SVN_ERR(svn_wc__db_scan_deletion(NULL, &moved_to_abspath,
                                   NULL, &moved_to_op_root_abspath,
                                   b.wc_ctx->db, sbox_wc_path(&b, "A/B"),
                                   pool, pool));
  SVN_TEST_STRING_ASSERT(moved_to_abspath, sbox_wc_path(&b, "X/B"));
  SVN_TEST_STRING_ASSERT(moved_to_op_root_abspath, sbox_wc_path(&b, "X/B"));

  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A/B/C"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 2, "X/B/C"));
  SVN_ERR(check_moved_to(moved_tos, 1, 3, "Y"));
  SVN_TEST_ASSERT(moved_tos->nelts == 2);

  SVN_ERR(svn_wc__db_scan_deletion(NULL, &moved_to_abspath,
                                   NULL, &moved_to_op_root_abspath,
                                   b.wc_ctx->db, sbox_wc_path(&b, "A/B/C"),
                                   pool, pool));
  SVN_TEST_STRING_ASSERT(moved_to_abspath, sbox_wc_path(&b, "X/B/C"));
  SVN_TEST_STRING_ASSERT(moved_to_op_root_abspath, sbox_wc_path(&b, "X/B"));

  SVN_ERR(svn_wc__db_scan_deletion(NULL, &moved_to_abspath,
                                   NULL, &moved_to_op_root_abspath,
                                   b.wc_ctx->db, sbox_wc_path(&b, "A2"),
                                   pool, pool));
  SVN_TEST_STRING_ASSERT(moved_to_abspath, sbox_wc_path(&b, "X"));
  SVN_TEST_STRING_ASSERT(moved_to_op_root_abspath, sbox_wc_path(&b, "X"));

  SVN_ERR(svn_wc__db_scan_deletion(NULL, &moved_to_abspath,
                                   NULL, &moved_to_op_root_abspath,
                                   b.wc_ctx->db, sbox_wc_path(&b, "A2/B"),
                                   pool, pool));
  SVN_TEST_STRING_ASSERT(moved_to_abspath, sbox_wc_path(&b, "X/B"));
  SVN_TEST_STRING_ASSERT(moved_to_op_root_abspath, sbox_wc_path(&b, "X"));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_follow_moved_to(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  apr_array_header_t *moved_tos;

  SVN_ERR(svn_test__sandbox_create(&b, "follow_moved_to", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A1"));
  SVN_ERR(sbox_wc_mkdir(&b, "A1/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A1/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A1/B/C/D"));
  SVN_ERR(sbox_wc_mkdir(&b, "A1/B/C/D/E"));
  SVN_ERR(sbox_wc_mkdir(&b, "A2"));
  SVN_ERR(sbox_wc_mkdir(&b, "A2/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A2/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A2/B/C/D"));
  SVN_ERR(sbox_wc_mkdir(&b, "A2/B/C/D/E"));
  SVN_ERR(sbox_wc_mkdir(&b, "A3"));
  SVN_ERR(sbox_wc_mkdir(&b, "A3/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A3/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A3/B/C/D"));
  SVN_ERR(sbox_wc_mkdir(&b, "A3/B/C/D/E"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  {
    nodes_row_t nodes[] = {
      {0, "",         "normal", 1, ""},
      {0, "A1",       "normal", 1, "A1"},
      {0, "A1/B",     "normal", 1, "A1/B"},
      {0, "A1/B/C",   "normal", 1, "A1/B/C"},
      {0, "A1/B/C/D", "normal", 1, "A1/B/C/D"},
      {0, "A1/B/C/D/E", "normal", 1, "A1/B/C/D/E"},
      {0, "A2",       "normal", 1, "A2"},
      {0, "A2/B",     "normal", 1, "A2/B"},
      {0, "A2/B/C",   "normal", 1, "A2/B/C"},
      {0, "A2/B/C/D", "normal", 1, "A2/B/C/D"},
      {0, "A2/B/C/D/E", "normal", 1, "A2/B/C/D/E"},
      {0, "A3",       "normal", 1, "A3"},
      {0, "A3/B",     "normal", 1, "A3/B"},
      {0, "A3/B/C",   "normal", 1, "A3/B/C"},
      {0, "A3/B/C/D", "normal", 1, "A3/B/C/D"},
      {0, "A3/B/C/D/E", "normal", 1, "A3/B/C/D/E"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_move(&b, "A1", "X"));
  SVN_ERR(sbox_wc_move(&b, "A2", "A1"));
  SVN_ERR(sbox_wc_move(&b, "A3", "A2"));
  SVN_ERR(sbox_wc_move(&b, "X", "A3"));
  SVN_ERR(sbox_wc_move(&b, "A1/B", "X"));
  SVN_ERR(sbox_wc_move(&b, "A2/B", "A1/B"));
  SVN_ERR(sbox_wc_move(&b, "A3/B", "A2/B"));
  SVN_ERR(sbox_wc_move(&b, "X", "A3/B"));
  SVN_ERR(sbox_wc_move(&b, "A1/B/C/D", "X"));
  SVN_ERR(sbox_wc_move(&b, "A2/B/C/D", "A1/B/C/D"));
  SVN_ERR(sbox_wc_move(&b, "A3/B/C/D", "A2/B/C/D"));
  SVN_ERR(sbox_wc_move(&b, "X", "A3/B/C/D"));
  SVN_ERR(sbox_wc_move(&b, "A1/B/C/D/E", "X"));
  SVN_ERR(sbox_wc_move(&b, "A2/B/C/D/E", "A1/B/C/D/E"));
  SVN_ERR(sbox_wc_move(&b, "A3/B/C/D/E", "A2/B/C/D/E"));
  SVN_ERR(sbox_wc_move(&b, "X", "A3/B/C/D/E"));

  {
    nodes_row_t nodes[] = {
      {0, "",         "normal", 1, ""},
      {0, "A1",       "normal", 1, "A1"},
      {0, "A1/B",     "normal", 1, "A1/B"},
      {0, "A1/B/C",   "normal", 1, "A1/B/C"},
      {0, "A1/B/C/D", "normal", 1, "A1/B/C/D"},
      {0, "A1/B/C/D/E", "normal", 1, "A1/B/C/D/E"},
      {0, "A2",       "normal", 1, "A2"},
      {0, "A2/B",     "normal", 1, "A2/B"},
      {0, "A2/B/C",   "normal", 1, "A2/B/C"},
      {0, "A2/B/C/D", "normal", 1, "A2/B/C/D"},
      {0, "A2/B/C/D/E", "normal", 1, "A2/B/C/D/E"},
      {0, "A3",       "normal", 1, "A3"},
      {0, "A3/B",     "normal", 1, "A3/B"},
      {0, "A3/B/C",   "normal", 1, "A3/B/C"},
      {0, "A3/B/C/D", "normal", 1, "A3/B/C/D"},
      {0, "A3/B/C/D/E", "normal", 1, "A3/B/C/D/E"},

      {1, "A1",       "normal", 1, "A2", FALSE, "A3", TRUE},
      {1, "A1/B",     "normal", 1, "A2/B", MOVED_HERE},
      {1, "A1/B/C",   "normal", 1, "A2/B/C", MOVED_HERE},
      {1, "A1/B/C/D", "normal", 1, "A2/B/C/D", MOVED_HERE},
      {1, "A1/B/C/D/E", "normal", 1, "A2/B/C/D/E", MOVED_HERE},

      {1, "A2",       "normal", 1, "A3", FALSE, "A1", TRUE},
      {1, "A2/B",     "normal", 1, "A3/B", MOVED_HERE},
      {1, "A2/B/C",   "normal", 1, "A3/B/C", MOVED_HERE},
      {1, "A2/B/C/D", "normal", 1, "A3/B/C/D", MOVED_HERE},
      {1, "A2/B/C/D/E", "normal", 1, "A3/B/C/D/E", MOVED_HERE},

      {1, "A3",       "normal", 1, "A1", FALSE, "A2", TRUE},
      {1, "A3/B",     "normal", 1, "A1/B", MOVED_HERE},
      {1, "A3/B/C",   "normal", 1, "A1/B/C", MOVED_HERE},
      {1, "A3/B/C/D", "normal", 1, "A1/B/C/D", MOVED_HERE},
      {1, "A3/B/C/D/E", "normal", 1, "A1/B/C/D/E", MOVED_HERE},

      {2, "A1/B",     "normal", 1, "A3/B", FALSE, "A3/B", TRUE},
      {2, "A1/B/C",   "normal", 1, "A3/B/C", MOVED_HERE},
      {2, "A1/B/C/D", "normal", 1, "A3/B/C/D", MOVED_HERE},
      {2, "A1/B/C/D/E", "normal", 1, "A3/B/C/D/E", MOVED_HERE},

      {2, "A2/B",     "normal", 1, "A1/B", FALSE, "A1/B", TRUE},
      {2, "A2/B/C",   "normal", 1, "A1/B/C", MOVED_HERE},
      {2, "A2/B/C/D", "normal", 1, "A1/B/C/D", MOVED_HERE},
      {2, "A2/B/C/D/E", "normal", 1, "A1/B/C/D/E", MOVED_HERE},

      {2, "A3/B",     "normal", 1, "A2/B", FALSE, "A2/B", TRUE},
      {2, "A3/B/C",   "normal", 1, "A2/B/C", MOVED_HERE},
      {2, "A3/B/C/D", "normal", 1, "A2/B/C/D", MOVED_HERE},
      {2, "A3/B/C/D/E", "normal", 1, "A2/B/C/D/E", MOVED_HERE},

      {4, "A1/B/C/D",   "normal", 1, "A1/B/C/D", FALSE, "A3/B/C/D", TRUE},
      {4, "A1/B/C/D/E", "normal", 1, "A1/B/C/D/E", MOVED_HERE},

      {4, "A2/B/C/D",   "normal", 1, "A2/B/C/D", FALSE, "A1/B/C/D", TRUE},
      {4, "A2/B/C/D/E", "normal", 1, "A2/B/C/D/E", MOVED_HERE},

      {4, "A3/B/C/D",   "normal", 1, "A3/B/C/D", FALSE, "A2/B/C/D", TRUE},
      {4, "A3/B/C/D/E", "normal", 1, "A3/B/C/D/E", MOVED_HERE},

      {5, "A1/B/C/D/E", "normal", 1, "A2/B/C/D/E", FALSE, "A3/B/C/D/E", TRUE},
      {5, "A2/B/C/D/E", "normal", 1, "A3/B/C/D/E", FALSE, "A1/B/C/D/E", TRUE},
      {5, "A3/B/C/D/E", "normal", 1, "A1/B/C/D/E", FALSE, "A2/B/C/D/E", TRUE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* A1->A3, A3/B->A2/B, A2/B/C/D->A1/B/C/D, A1/B/C/D/E->A3/B/C/D/E */
  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A1"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 1, "A3"));
  SVN_TEST_ASSERT(moved_tos->nelts == 1);

  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A1/B"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 1, "A3/B"));
  SVN_ERR(check_moved_to(moved_tos, 1, 2, "A2/B"));
  SVN_TEST_ASSERT(moved_tos->nelts == 2);

  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A1/B/C"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 1, "A3/B/C"));
  SVN_ERR(check_moved_to(moved_tos, 1, 2, "A2/B/C"));
  SVN_TEST_ASSERT(moved_tos->nelts == 2);

  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A1/B/C/D"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 1, "A3/B/C/D"));
  SVN_ERR(check_moved_to(moved_tos, 1, 2, "A2/B/C/D"));
  SVN_ERR(check_moved_to(moved_tos, 2, 4, "A1/B/C/D"));
  SVN_TEST_ASSERT(moved_tos->nelts == 3);

  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A1/B/C/D/E"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 1, "A3/B/C/D/E"));
  SVN_ERR(check_moved_to(moved_tos, 1, 2, "A2/B/C/D/E"));
  SVN_ERR(check_moved_to(moved_tos, 2, 4, "A1/B/C/D/E"));
  SVN_ERR(check_moved_to(moved_tos, 3, 5, "A3/B/C/D/E"));
  SVN_TEST_ASSERT(moved_tos->nelts == 4);

  SVN_ERR(sbox_wc_delete(&b, "A3/B/C/D/E"));
  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A1/B/C/D/E"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 1, "A3/B/C/D/E"));
  SVN_ERR(check_moved_to(moved_tos, 1, 2, "A2/B/C/D/E"));
  SVN_ERR(check_moved_to(moved_tos, 2, 4, "A1/B/C/D/E"));
  SVN_TEST_ASSERT(moved_tos->nelts == 3);

  return SVN_NO_ERROR;
}

static svn_error_t *
mixed_rev_move(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  apr_array_header_t *moved_tos;

  SVN_ERR(svn_test__sandbox_create(&b, "mixed_rev_move", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/D"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/D/E"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  {
    nodes_row_t nodes[] = {
      {0, "",      "normal", 0, ""},
      {0, "A",     "normal", 1, "A"},
      {0, "A/B",   "normal", 2, "A/B"},
      {0, "A/B/C", "normal", 3, "A/B/C"},
      {0, "A/D",   "normal", 2, "A/D"},
      {0, "A/D/E", "normal", 3, "A/D/E"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* We don't allow mixed-rev move in 1.8 and the command line client
     will return an error, but for compatibility with 1.7 move has an
     allow_mixed_revisions=TRUE flag which is being used here so the
     move transforms automatically into copy+delete.  This test was
     written before that transforming was implemented so still expects
     some move information. */
  SVN_ERR(sbox_wc_move(&b, "A", "X"));

  {
    nodes_row_t nodes[] = {
      {0, "",      "normal",       0, ""},
      {0, "A",     "normal",       1, "A"},
      {0, "A/B",   "normal",       2, "A/B"},
      {0, "A/B/C", "normal",       3, "A/B/C"},
      {0, "A/D",   "normal",       2, "A/D"},
      {0, "A/D/E", "normal",       3, "A/D/E"},
      {1, "A",     "base-deleted", NO_COPY_FROM, "X"},
      {1, "A/B",   "base-deleted", NO_COPY_FROM},
      {1, "A/B/C", "base-deleted", NO_COPY_FROM},
      {1, "A/D",   "base-deleted", NO_COPY_FROM},
      {1, "A/D/E", "base-deleted", NO_COPY_FROM},
      {1, "X",     "normal",       1, "A", MOVED_HERE},
      {1, "X/B",   "not-present",  2, "A/B"},
      {1, "X/D",   "not-present",  2, "A/D"},
      {2, "X/B",   "normal",       2, "A/B"},
      {2, "X/B/C", "not-present",  3, "A/B/C"},
      {2, "X/D",   "normal",       2, "A/D"},
      {2, "X/D/E", "not-present",  3, "A/D/E"},
      {3, "X/B/C", "normal",       3, "A/B/C"},
      {3, "X/D/E", "normal",       3, "A/D/E"},

      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* ### These values PASS but I'm not sure they are correct. */
  /* A/B/C doesn't exist as X/B/C at op depth 1, but is reported */
  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A/B/C"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 1, "X/B/C"));
  SVN_TEST_ASSERT(moved_tos->nelts == 1);

  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A/B"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 1, "X/B"));
  SVN_TEST_ASSERT(moved_tos->nelts == 1);

  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 1, "X"));
  SVN_TEST_ASSERT(moved_tos->nelts == 1);


  /* This move doesn't record moved-to */
  SVN_ERR(sbox_wc_move(&b, "X/B", "X/Y"));

  {
    nodes_row_t nodes[] = {
      {0, "",      "normal",       0, ""},
      {0, "A",     "normal",       1, "A"},
      {0, "A/B",   "normal",       2, "A/B"},
      {0, "A/B/C", "normal",       3, "A/B/C"},
      {0, "A/D",   "normal",       2, "A/D"},
      {0, "A/D/E", "normal",       3, "A/D/E"},
      {1, "A",     "base-deleted", NO_COPY_FROM, "X"},
      {1, "A/B",   "base-deleted", NO_COPY_FROM},
      {1, "A/B/C", "base-deleted", NO_COPY_FROM},
      {1, "A/D",   "base-deleted", NO_COPY_FROM},
      {1, "A/D/E", "base-deleted", NO_COPY_FROM},
      {1, "X",     "normal",       1, "A", MOVED_HERE},
      {1, "X/B",   "not-present",  2, "A/B"},
      {1, "X/D",   "not-present",  2, "A/D"},
      {2, "X/D",   "normal",       2, "A/D"},
      {2, "X/D/E", "not-present",  3, "A/D/E"},
      {2, "X/Y",   "normal",       2, "A/B"},
      {2, "X/Y/C", "not-present",  NO_COPY_FROM},
      {3, "X/Y/C", "normal",       3, "A/B/C"},
      {3, "X/D/E", "normal",       3, "A/D/E"},

      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* A/B/C still doesn't exist as X/B/C at op depth 1 */
  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A/B/C"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 1, "X/B/C"));
  SVN_TEST_ASSERT(moved_tos->nelts == 1);

  /* A/B doesn't exist exist as X/B and the move to Y can't be tracked in
     the current scheme */
  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A/B"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 1, "X/B"));
  SVN_TEST_ASSERT(moved_tos->nelts == 1);

  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 1, "X"));
  SVN_TEST_ASSERT(moved_tos->nelts == 1);


  SVN_ERR(sbox_wc_mkdir(&b, "Z"));
  SVN_ERR(sbox_wc_commit(&b, "Z")); /* r4 */

  SVN_ERR(sbox_wc_update(&b, "", 4));

  {
    nodes_row_t nodes[] = {
      {0, "",      "normal",       4, ""},
      {0, "A",     "normal",       4, "A"},
      {0, "A/B",   "normal",       4, "A/B"},
      {0, "A/B/C", "normal",       4, "A/B/C"},
      {0, "A/D",   "normal",       4, "A/D"},
      {0, "A/D/E", "normal",       4, "A/D/E"},
      {1, "A",     "base-deleted", NO_COPY_FROM, "X"},
      {1, "A/B",   "base-deleted", NO_COPY_FROM},
      {1, "A/B/C", "base-deleted", NO_COPY_FROM},
      {1, "A/D",   "base-deleted", NO_COPY_FROM},
      {1, "A/D/E", "base-deleted", NO_COPY_FROM},
      /* X is expanded on update. The not-present nodes are now here */
      {1, "X",     "normal",       4, "A", MOVED_HERE},
      {1, "X/B",   "normal",       4, "A/B", MOVED_HERE},
      {1, "X/B/C", "normal",       4, "A/B/C", MOVED_HERE},
      {1, "X/D",   "normal",       4, "A/D", MOVED_HERE},
      {1, "X/D/E", "normal",       4, "A/D/E", MOVED_HERE},
      {2, "X/D",   "normal",       2, "A/D"},
      {2, "X/D/E", "not-present",  3, "A/D/E"},
      {2, "X/Y",   "normal",       2, "A/B"},
      {2, "X/Y/C", "not-present",  NO_COPY_FROM},
      {3, "X/D/E", "normal",       3, "A/D/E"},
      {3, "X/Y/C", "normal",       3, "A/B/C"},

      {0, "Z",     "normal",       4, "Z"},
      {0}
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A/B/C"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 1, "X/B/C"));
  SVN_TEST_ASSERT(moved_tos->nelts == 1);

  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A/B"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 1, "X/B"));
  SVN_TEST_ASSERT(moved_tos->nelts == 1);

  SVN_ERR(svn_wc__db_follow_moved_to(&moved_tos, b.wc_ctx->db,
                                     sbox_wc_path(&b, "A"), pool, pool));
  SVN_ERR(check_moved_to(moved_tos, 0, 1, "X"));
  SVN_TEST_ASSERT(moved_tos->nelts == 1);

  {
    conflict_info_t conflicts[] = {
      { "X/D", FALSE, FALSE, {0 /* ### Needs fixing */} },
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }

  return SVN_NO_ERROR;
}

/* Test the result of 'update' when the incoming changes are inside a
 * directory that is locally moved. */
static svn_error_t *
update_prop_mod_into_moved(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "update_prop_mod_into_moved", opts, pool));

  /* r1: Create files 'f', 'h' */
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_file_write(&b, "A/B/f", "r1 content\n"));
  SVN_ERR(sbox_file_write(&b, "A/B/h", "r1 content\n"));
  SVN_ERR(sbox_wc_add(&b, "A/B/f"));
  SVN_ERR(sbox_wc_add(&b, "A/B/h"));
  SVN_ERR(sbox_wc_propset(&b, "pd", "f1", "A/B/f"));
  SVN_ERR(sbox_wc_propset(&b, "pn", "f1", "A/B/f"));
  SVN_ERR(sbox_wc_propset(&b, "pm", "f1", "A/B/f"));
  SVN_ERR(sbox_wc_propset(&b, "p", "h1", "A/B/h"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  /* r2: Modify 'f'. Delete prop 'pd', modify prop 'pm', add prop 'pa',
   * leave prop 'pn' unchanged. */
  SVN_ERR(sbox_file_write(&b, "A/B/f", "r1 content\nr2 content\n"));
  SVN_ERR(sbox_wc_propset(&b, "pd", NULL, "A/B/f"));
  SVN_ERR(sbox_wc_propset(&b, "pm", "f2", "A/B/f"));
  SVN_ERR(sbox_wc_propset(&b, "pa", "f2", "A/B/f"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  /* r3: Delete 'h', add 'g' */
  SVN_ERR(sbox_file_write(&b, "A/B/g", "r3 content\n"));
  SVN_ERR(sbox_wc_add(&b, "A/B/g"));
  SVN_ERR(sbox_wc_propset(&b, "p", "g3", "A/B/g"));
  SVN_ERR(sbox_wc_delete(&b, "A/B/h"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_update(&b, "", 1));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       1, "A"},
      {0, "A/B",      "normal",       1, "A/B"},
      {0, "A/B/f",    "normal",       1, "A/B/f", NOT_MOVED,  "pd,pm,pn"},
      {0, "A/B/h",    "normal",       1, "A/B/h", NOT_MOVED,  "p"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* A is single-revision so A2 is a single-revision copy */
  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       1, "A"},
      {0, "A/B",      "normal",       1, "A/B"},
      {0, "A/B/f",    "normal",       1, "A/B/f", NOT_MOVED,  "pd,pm,pn"},
      {0, "A/B/h",    "normal",       1, "A/B/h", NOT_MOVED,  "p"},
      {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/f",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/h",    "base-deleted", NO_COPY_FROM},
      {1, "A2",       "normal",       1, "A", MOVED_HERE},
      {1, "A2/B",     "normal",       1, "A/B", MOVED_HERE},
      {1, "A2/B/f",   "normal",       1, "A/B/f", MOVED_HERE, "pd,pm,pn"},
      {1, "A2/B/h",   "normal",       1, "A/B/h", MOVED_HERE, "p"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Update causes a tree-conflict on A due to incoming text-change. */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       2, ""},
      {0, "A",        "normal",       2, "A"},
      {0, "A/B",      "normal",       2, "A/B"},
      {0, "A/B/f",    "normal",       2, "A/B/f", NOT_MOVED,  "pa,pm,pn"},
      {0, "A/B/h",    "normal",       2, "A/B/h", NOT_MOVED,  "p"},
      {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/f",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/h",    "base-deleted", NO_COPY_FROM},
      {1, "A2",       "normal",       1, "A", MOVED_HERE},
      {1, "A2/B",     "normal",       1, "A/B", MOVED_HERE},
      {1, "A2/B/f",   "normal",       1, "A/B/f", MOVED_HERE, "pd,pm,pn"},
      {1, "A2/B/h",   "normal",       1, "A/B/h", MOVED_HERE, "p"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  {
    conflict_info_t conflicts[] = {
      { "A", FALSE, FALSE, {svn_wc_conflict_action_edit,
                            svn_wc_conflict_reason_moved_away, "A"}},
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }

  /* Resolve should update the move. */
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));

  SVN_ERR(check_db_conflicts(&b, "", NULL));

  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       2, ""},
      {0, "A",        "normal",       2, "A"},
      {0, "A/B",      "normal",       2, "A/B"},
      {0, "A/B/f",    "normal",       2, "A/B/f", NOT_MOVED,  "pa,pm,pn"},
      {0, "A/B/h",    "normal",       2, "A/B/h", NOT_MOVED,  "p"},
      {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/f",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/h",    "base-deleted", NO_COPY_FROM},
      {1, "A2",       "normal",       2, "A", MOVED_HERE},
      {1, "A2/B",     "normal",       2, "A/B", MOVED_HERE},
      {1, "A2/B/f",   "normal",       2, "A/B/f", MOVED_HERE, "pa,pm,pn"},
      {1, "A2/B/h",   "normal",       2, "A/B/h", MOVED_HERE, "p"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
nested_move_update(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "nested_move_update", opts, pool));

  /* r1: Create file 'f' */
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_file_write(&b, "A/B/C/f", "r1 content\n"));
  SVN_ERR(sbox_wc_add(&b, "A/B/C/f"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  /* r2: Modify 'f' */
  SVN_ERR(sbox_file_write(&b, "A/B/C/f", "r1 content\nr2 content\n"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  /* r3: Create 'X' */
  SVN_ERR(sbox_wc_mkdir(&b, "X"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  SVN_ERR(sbox_wc_move(&b, "A2/B/C", "A2/B/C2"));
  {
    nodes_row_t nodes[] = {
      {0, "",          "normal",       1, ""},
      {0, "A",         "normal",       1, "A"},
      {0, "A/B",       "normal",       1, "A/B"},
      {0, "A/B/C",     "normal",       1, "A/B/C"},
      {0, "A/B/C/f",   "normal",       1, "A/B/C/f"},
      {1, "A",         "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",       "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/f",   "base-deleted", NO_COPY_FROM},
      {1, "A2",        "normal",       1, "A", MOVED_HERE},
      {1, "A2/B",      "normal",       1, "A/B", MOVED_HERE},
      {1, "A2/B/C",    "normal",       1, "A/B/C", MOVED_HERE},
      {1, "A2/B/C/f",  "normal",       1, "A/B/C/f", MOVED_HERE},
      {3, "A2/B/C",    "base-deleted", NO_COPY_FROM, "A2/B/C2"},
      {3, "A2/B/C/f",  "base-deleted", NO_COPY_FROM},
      {3, "A2/B/C2",   "normal",       1, "A/B/C", MOVED_HERE},
      {3, "A2/B/C2/f", "normal",       1, "A/B/C/f", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "", 2));

  {
    nodes_row_t nodes[] = {
      {0, "",          "normal",       2, ""},
      {0, "A",         "normal",       2, "A"},
      {0, "A/B",       "normal",       2, "A/B"},
      {0, "A/B/C",     "normal",       2, "A/B/C"},
      {0, "A/B/C/f",   "normal",       2, "A/B/C/f"},
      {1, "A",         "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",       "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/f",   "base-deleted", NO_COPY_FROM},
      {1, "A2",        "normal",       1, "A", MOVED_HERE},
      {1, "A2/B",      "normal",       1, "A/B", MOVED_HERE},
      {1, "A2/B/C",    "normal",       1, "A/B/C", MOVED_HERE},
      {1, "A2/B/C/f",  "normal",       1, "A/B/C/f", MOVED_HERE},
      {3, "A2/B/C",    "base-deleted", NO_COPY_FROM, "A2/B/C2"},
      {3, "A2/B/C/f",  "base-deleted", NO_COPY_FROM},
      {3, "A2/B/C2",   "normal",       1, "A/B/C", MOVED_HERE},
      {3, "A2/B/C2/f", "normal",       1, "A/B/C/f", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Following the A->A2 move should raise a tree-conflict on A2/B/C,
     resolving that may require an explicit resolve. */
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(sbox_wc_resolve(&b, "A2/B/C", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(check_db_conflicts(&b, "", NULL /* no conflicts */));
  {
    nodes_row_t nodes[] = {
      {0, "",          "normal",       2, ""},
      {0, "A",         "normal",       2, "A"},
      {0, "A/B",       "normal",       2, "A/B"},
      {0, "A/B/C",     "normal",       2, "A/B/C"},
      {0, "A/B/C/f",   "normal",       2, "A/B/C/f"},
      {1, "A",         "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",       "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/f",   "base-deleted", NO_COPY_FROM},
      {1, "A2",        "normal",       2, "A", MOVED_HERE},
      {1, "A2/B",      "normal",       2, "A/B", MOVED_HERE},
      {1, "A2/B/C",    "normal",       2, "A/B/C", MOVED_HERE},
      {1, "A2/B/C/f",  "normal",       2, "A/B/C/f", MOVED_HERE},
      {3, "A2/B/C",    "base-deleted", NO_COPY_FROM, "A2/B/C2"},
      {3, "A2/B/C/f",  "base-deleted", NO_COPY_FROM},
      {3, "A2/B/C2",   "normal",       2, "A/B/C", MOVED_HERE},
      {3, "A2/B/C2/f", "normal",       2, "A/B/C/f", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Update A to r3 brings no changes but updates the revisions. */
  SVN_ERR(sbox_wc_update(&b, "A", 3));
  SVN_ERR(check_db_conflicts(&b, "", NULL /* no conflicts */));

  {
    nodes_row_t nodes[] = {
      {0, "",          "normal",       2, ""},
      {0, "A",         "normal",       3, "A"},
      {0, "A/B",       "normal",       3, "A/B"},
      {0, "A/B/C",     "normal",       3, "A/B/C"},
      {0, "A/B/C/f",   "normal",       3, "A/B/C/f"},
      {1, "A",         "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",       "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/f",   "base-deleted", NO_COPY_FROM},
      {1, "A2",        "normal",       3, "A", MOVED_HERE},
      {1, "A2/B",      "normal",       3, "A/B", MOVED_HERE},
      {1, "A2/B/C",    "normal",       3, "A/B/C", MOVED_HERE},
      {1, "A2/B/C/f",  "normal",       3, "A/B/C/f", MOVED_HERE},
      {3, "A2/B/C",    "base-deleted", NO_COPY_FROM, "A2/B/C2"},
      {3, "A2/B/C/f",  "base-deleted", NO_COPY_FROM},
      {3, "A2/B/C2",   "normal",       3, "A/B/C", MOVED_HERE},
      {3, "A2/B/C2/f", "normal",       3, "A/B/C/f", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
nested_move_commit(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "nested_move_commit", opts, pool));

  /* r1: Create file 'f' */
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_file_write(&b, "A/B/C/f", "r1 content\n"));
  SVN_ERR(sbox_wc_add(&b, "A/B/C/f"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A/B/C", "C2"));

  {
    const char *moved_to;
    const char *expected_to;
    SVN_ERR(svn_wc__db_scan_deletion(NULL, NULL, NULL, &moved_to,
                                     b.wc_ctx->db, sbox_wc_path(&b, "A/B/C"),
                                     pool, pool));

    expected_to = sbox_wc_path(&b, "C2");

    if (strcmp(moved_to, expected_to) != 0)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "Expected moved to %s, but was %s",
                                 expected_to, moved_to);
  }
  {
    const char *moved_from;
    const char *expected_from;
    SVN_ERR(svn_wc__db_scan_moved(&moved_from, NULL, NULL, NULL,
                                  b.wc_ctx->db, sbox_wc_path(&b, "C2"),
                                  pool, pool));

    expected_from = sbox_wc_path(&b, "A/B/C");

    if (strcmp(moved_from, expected_from) != 0)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "Expected moved from %s, but was %s",
                                 expected_from, moved_from);
  }

  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  {
    nodes_row_t nodes[] = {
      {0, "",          "normal",       1, ""},
      {0, "A",         "normal",       1, "A"},
      {0, "A/B",       "normal",       1, "A/B"},
      {0, "A/B/C",     "normal",       1, "A/B/C"},
      {0, "A/B/C/f",   "normal",       1, "A/B/C/f"},
      {1, "A",         "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",       "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/f",   "base-deleted", NO_COPY_FROM},
      {1, "A2",        "normal",       1, "A", MOVED_HERE},
      {1, "A2/B",      "normal",       1, "A/B", MOVED_HERE},
      {1, "A2/B/C",    "normal",       1, "A/B/C", MOVED_HERE},
      {1, "A2/B/C/f",  "normal",       1, "A/B/C/f", MOVED_HERE},
      {3, "A2/B/C",    "base-deleted", NO_COPY_FROM, "C2"},
      {3, "A2/B/C/f",  "base-deleted", NO_COPY_FROM},
      {1, "C2",        "normal",       1, "A/B/C", MOVED_HERE},
      {1, "C2/f",      "normal",       1, "A/B/C/f", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  {
    const char *moved_to;
    const char *expected_to;
    SVN_ERR(svn_wc__db_scan_deletion(NULL, NULL, NULL, &moved_to,
                                     b.wc_ctx->db, sbox_wc_path(&b, "A/B/C"),
                                     pool, pool));

    /* A/B/C is part of the A->A2 move. */
    expected_to = sbox_wc_path(&b, "A2");
    if (strcmp(moved_to, expected_to) != 0)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "Expected moved to %s, but was %s",
                                 expected_to, moved_to);

    SVN_ERR(svn_wc__db_scan_deletion(NULL, NULL, NULL, &moved_to,
                                     b.wc_ctx->db, sbox_wc_path(&b, "A2/B/C"),
                                     pool, pool));

    /* A2/B/C is the A2/B/C->C2 move. */
    expected_to = sbox_wc_path(&b, "C2");
    if (strcmp(moved_to, expected_to) != 0)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "Expected moved to %s, but was %s",
                                 expected_to, moved_to);
  }
  {
    const char *moved_from;
    const char *expected_from;
    SVN_ERR(svn_wc__db_scan_moved(&moved_from, NULL, NULL, NULL,
                                  b.wc_ctx->db, sbox_wc_path(&b, "C2"),
                                  pool, pool));

    /* C2 is the A2/B/C->C2 move. */
    expected_from = sbox_wc_path(&b, "A2/B/C");
    if (strcmp(moved_from, expected_from) != 0)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "Expected moved from %s, but was %s",
                                 expected_from, moved_from);
  }

  {
    apr_array_header_t *targets = apr_array_make(pool, 2, sizeof(const char *));

    APR_ARRAY_PUSH(targets, const char *) = sbox_wc_path(&b, "A");
    APR_ARRAY_PUSH(targets, const char *) = sbox_wc_path(&b, "A2");

    SVN_ERR(sbox_wc_commit_ex(&b, targets, svn_depth_empty));
  }

  {
    nodes_row_t nodes[] = {
      {0, "",          "normal",       1, ""},
      {0, "A",         "not-present",  2, "A"},
      {0, "A2",        "normal",       2, "A2"},
      {0, "A2/B",      "normal",       2, "A2/B"},
      {0, "A2/B/C",    "normal",       2, "A2/B/C"},
      {0, "A2/B/C/f",  "normal",       2, "A2/B/C/f"},
      {3, "A2/B/C",    "base-deleted", NO_COPY_FROM, "C2"},
      {3, "A2/B/C/f",  "base-deleted", NO_COPY_FROM},

      /* These need to have their copyfrom information updated */
      {1, "C2",        "normal",       2, "A2/B/C", MOVED_HERE},
      {1, "C2/f",      "normal",       2, "A2/B/C/f", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  {
    const char *moved_to;
    const char *expected_to;
    SVN_ERR(svn_wc__db_scan_deletion(NULL, NULL, NULL, &moved_to,
                                     b.wc_ctx->db, sbox_wc_path(&b, "A2/B/C"),
                                     pool, pool));

    expected_to = sbox_wc_path(&b, "C2");

    if (strcmp(moved_to, expected_to) != 0)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "Expected moved to %s, but was %s",
                                 expected_to, moved_to);
  }

  {
    const char *moved_from;
    const char *expected_from;
    SVN_ERR(svn_wc__db_scan_moved(&moved_from, NULL, NULL, NULL,
                                  b.wc_ctx->db, sbox_wc_path(&b, "C2"),
                                  pool, pool));

    expected_from = sbox_wc_path(&b, "A2/B/C");

    if (strcmp(moved_from, expected_from) != 0)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "Expected moved from %s, but was %s",
                                 expected_from, moved_from);
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
nested_move_update2(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "nested_move_update2", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "P"));
  SVN_ERR(sbox_wc_mkdir(&b, "P/Q"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "X"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  SVN_ERR(sbox_wc_move(&b, "P", "A"));
  SVN_ERR(sbox_wc_move(&b, "A2", "P"));
  SVN_ERR(sbox_wc_move(&b, "A/Q", "A/Q2"));
  SVN_ERR(sbox_wc_move(&b, "P/B", "P/B2"));

  {
    nodes_row_t nodes[] = {
      {0, "",          "normal",       1, ""},
      {0, "A",         "normal",       1, "A"},
      {0, "A/B",       "normal",       1, "A/B"},
      {0, "P",         "normal",       1, "P"},
      {0, "P/Q",       "normal",       1, "P/Q"},
      {1, "A",         "normal",       1, "P", FALSE, "P", TRUE},
      {1, "A/B",       "base-deleted", NO_COPY_FROM},
      {1, "A/Q",       "normal",       1, "P/Q", MOVED_HERE},
      {1, "P",         "normal",       1, "A", FALSE, "A", TRUE},
      {1, "P/Q",       "base-deleted", NO_COPY_FROM},
      {1, "P/B",       "normal",       1, "A/B", MOVED_HERE},
      {2, "A/Q",       "base-deleted", NO_COPY_FROM, "A/Q2"},
      {2, "A/Q2",      "normal",       1, "P/Q", MOVED_HERE},
      {2, "P/B",       "base-deleted", NO_COPY_FROM, "P/B2"},
      {2, "P/B2",      "normal",       1, "A/B", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Update A bumps revisions but only for moves originating in A.  In
     particular A/Q to A/Q2 does not get bumped. */
  SVN_ERR(sbox_wc_update(&b, "A", 2));
  {
    nodes_row_t nodes[] = {
      {0, "",          "normal",       1, ""},
      {0, "A",         "normal",       2, "A"},
      {0, "A/B",       "normal",       2, "A/B"},
      {0, "P",         "normal",       1, "P"},
      {0, "P/Q",       "normal",       1, "P/Q"},
      {1, "A",         "normal",       1, "P", FALSE, "P", TRUE},
      {1, "A/B",       "base-deleted", NO_COPY_FROM},
      {1, "A/Q",       "normal",       1, "P/Q", MOVED_HERE},
      {1, "P",         "normal",       2, "A", FALSE, "A", TRUE},
      {1, "P/Q",       "base-deleted", NO_COPY_FROM},
      {1, "P/B",       "normal",       2, "A/B", MOVED_HERE},
      {2, "A/Q",       "base-deleted", NO_COPY_FROM, "A/Q2"},
      {2, "A/Q2",      "normal",       1, "P/Q", MOVED_HERE},
      {2, "P/B",       "base-deleted", NO_COPY_FROM, "P/B2"},
      {2, "P/B2",      "normal",       2, "A/B", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
check_tree_conflict_repos_path(svn_test__sandbox_t *b,
                               const char *wc_path,
                               const char *repos_path1,
                               const char *repos_path2)
{
  svn_skel_t *conflict;
  svn_wc_operation_t operation;
  const apr_array_header_t *locations;
  svn_boolean_t text_conflicted, prop_conflicted, tree_conflicted;

  SVN_ERR(svn_wc__db_read_conflict(&conflict, NULL, NULL,
                                   b->wc_ctx->db, sbox_wc_path(b, wc_path),
                                   b->pool, b->pool));

  SVN_TEST_ASSERT(conflict != NULL);

  SVN_ERR(svn_wc__conflict_read_info(&operation, &locations,
                                     &text_conflicted, &prop_conflicted,
                                     &tree_conflicted,
                                     b->wc_ctx->db,  b->wc_abspath,
                                     conflict, b->pool, b->pool));

  SVN_ERR_ASSERT(tree_conflicted);

  if (repos_path1)
    {
      svn_wc_conflict_version_t *version
        = APR_ARRAY_IDX(locations, 0, svn_wc_conflict_version_t *);

      SVN_TEST_ASSERT(version != NULL);

      SVN_TEST_STRING_ASSERT(version->path_in_repos, repos_path1);
    }

  if (repos_path2)
    {
      svn_wc_conflict_version_t *version
        = APR_ARRAY_IDX(locations, 1, svn_wc_conflict_version_t *);

      SVN_TEST_ASSERT(version != NULL);

      SVN_TEST_STRING_ASSERT(version->path_in_repos, repos_path2);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_update_conflicts(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_update_conflicts", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "X"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/A/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/A/B/C/D"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "X/A/B/C/D/E"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/A/B/F"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_switch(&b, "", "/X", svn_depth_infinity));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  SVN_ERR(sbox_wc_move(&b, "A2/B/C", "A2/B/C2"));
  SVN_ERR(sbox_file_write(&b, "A2/B/F", "obstruction\n"));

  {
    nodes_row_t nodes[] = {
      {0, "",          "normal",       1, "X"},
      {0, "A",         "normal",       1, "X/A"},
      {0, "A/B",       "normal",       1, "X/A/B"},
      {0, "A/B/C",     "normal",       1, "X/A/B/C"},
      {0, "A/B/C/D",   "normal",       1, "X/A/B/C/D"},
      {1, "A",         "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",       "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D",   "base-deleted", NO_COPY_FROM},
      {1, "A2",        "normal",       1, "X/A", MOVED_HERE},
      {1, "A2/B",      "normal",       1, "X/A/B", MOVED_HERE},
      {1, "A2/B/C",    "normal",       1, "X/A/B/C", MOVED_HERE},
      {1, "A2/B/C/D",  "normal",       1, "X/A/B/C/D", MOVED_HERE},
      {3, "A2/B/C",    "base-deleted", NO_COPY_FROM, "A2/B/C2"},
      {3, "A2/B/C/D",  "base-deleted", NO_COPY_FROM},
      {3, "A2/B/C2",   "normal",       1, "X/A/B/C", MOVED_HERE},
      {3, "A2/B/C2/D", "normal",       1, "X/A/B/C/D", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "A", 2));
  SVN_ERR(check_tree_conflict_repos_path(&b, "A", "X/A", "X/A"));
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",           "normal",       1, "X"},
      {0, "A",          "normal",       2, "X/A"},
      {0, "A/B",        "normal",       2, "X/A/B"},
      {0, "A/B/C",      "normal",       2, "X/A/B/C"},
      {0, "A/B/C/D",    "normal",       2, "X/A/B/C/D"},
      {0, "A/B/C/D/E",  "normal",       2, "X/A/B/C/D/E"},
      {0, "A/B/F",      "normal",       2, "X/A/B/F"},
      {1, "A",          "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",        "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D/E",  "base-deleted", NO_COPY_FROM},
      {1, "A/B/F",      "base-deleted", NO_COPY_FROM},
      {1, "A2",         "normal",       2, "X/A", MOVED_HERE},
      {1, "A2/B",       "normal",       2, "X/A/B", MOVED_HERE},
      {1, "A2/B/C",     "normal",       2, "X/A/B/C", MOVED_HERE},
      {1, "A2/B/C/D",   "normal",       2, "X/A/B/C/D", MOVED_HERE},
      {1, "A2/B/C/D/E", "normal",       2, "X/A/B/C/D/E", MOVED_HERE},
      {1, "A2/B/F",     "normal",       2, "X/A/B/F", MOVED_HERE},
      {3, "A2/B/C",     "base-deleted", NO_COPY_FROM, "A2/B/C2"},
      {3, "A2/B/C/D",   "base-deleted", NO_COPY_FROM},
      {3, "A2/B/C/D/E", "base-deleted", NO_COPY_FROM},
      {3, "A2/B/C2",    "normal",       1, "X/A/B/C", MOVED_HERE},
      {3, "A2/B/C2/D",  "normal",       1, "X/A/B/C/D", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(check_tree_conflict_repos_path(&b, "A2/B/C", "X/A/B/C", "X/A/B/C"));
  SVN_ERR(check_tree_conflict_repos_path(&b, "A2/B/F", NULL, "X/A/B/F"));

  return SVN_NO_ERROR;
}

static svn_error_t *
move_update_delete_mods(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_update_delete_mods", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/D"));
  SVN_ERR(sbox_file_write(&b, "A/B/C/f", "r1 content\n"));
  SVN_ERR(sbox_wc_add(&b, "A/B/C/f"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_delete(&b, "A/B/C"));
  SVN_ERR(sbox_wc_delete(&b, "A/B/D"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A/B", "B2"));
  SVN_ERR(sbox_file_write(&b, "B2/C/f", "modified content\n"));
  SVN_ERR(sbox_wc_delete(&b, "B2/D"));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "A/B/C",   "normal",       1, "A/B/C"},
      {0, "A/B/C/f", "normal",       1, "A/B/C/f"},
      {0, "A/B/D",   "normal",       1, "A/B/D"},
      {2, "A/B",     "base-deleted",  NO_COPY_FROM, "B2"},
      {2, "A/B/C",   "base-deleted",  NO_COPY_FROM},
      {2, "A/B/C/f", "base-deleted",  NO_COPY_FROM},
      {2, "A/B/D",   "base-deleted",  NO_COPY_FROM},
      {1, "B2",      "normal",       1, "A/B", MOVED_HERE},
      {1, "B2/C",    "normal",       1, "A/B/C", MOVED_HERE},
      {1, "B2/C/f",  "normal",       1, "A/B/C/f", MOVED_HERE},
      {1, "B2/D",    "normal",       1, "A/B/D", MOVED_HERE},
      {2, "B2/D",    "base-deleted", NO_COPY_FROM},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "A", 2));
  SVN_ERR(sbox_wc_resolve(&b, "A/B", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       2, "A"},
      {0, "A/B",     "normal",       2, "A/B"},
      {2, "A/B",     "base-deleted",  NO_COPY_FROM, "B2"},
      {1, "B2",      "normal",       2, "A/B", MOVED_HERE},
      {2, "B2/C",    "normal",       1, "A/B/C"},
      {2, "B2/C/f",  "normal",       1, "A/B/C/f"},
      {0}
    };
    conflict_info_t conflicts[] = {
      {"B2/C", FALSE, FALSE, {svn_wc_conflict_action_delete,
                              svn_wc_conflict_reason_edited}},
      {"B2/D", FALSE, FALSE, {svn_wc_conflict_action_delete,
                              svn_wc_conflict_reason_deleted}},
      { 0 }
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }

  SVN_ERR(check_tree_conflict_repos_path(&b, "B2/C", "A/B/C", "A/B/C"));
  SVN_ERR(check_tree_conflict_repos_path(&b, "B2/D", "A/B/D", "A/B/D"));

  return SVN_NO_ERROR;
}

static svn_error_t *
nested_moves2(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "nested_moves2", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A/A/A/A"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "X"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_move(&b, "A/A/A/A/A/A", "C"));
  SVN_ERR(sbox_wc_move(&b, "A/A/A/A", "D"));
  SVN_ERR(sbox_wc_move(&b, "A/A", "E"));

  {
    nodes_row_t nodes[] = {
      {0, "",            "normal",       0, ""},
      {0, "X",           "normal",       2, "X"},
      {0, "A",           "normal",       1, "A"},
      {0, "A/A",         "normal",       1, "A/A"},
      {0, "A/A/A",       "normal",       1, "A/A/A"},
      {0, "A/A/A/A",     "normal",       1, "A/A/A/A"},
      {0, "A/A/A/A/A",   "normal",       1, "A/A/A/A/A"},
      {0, "A/A/A/A/A/A", "normal",       1, "A/A/A/A/A/A"},
      {2, "A/A",         "base-deleted", NO_COPY_FROM, "E"},
      {2, "A/A/A",       "base-deleted", NO_COPY_FROM},
      {2, "A/A/A/A",     "base-deleted", NO_COPY_FROM},
      {2, "A/A/A/A/A",   "base-deleted", NO_COPY_FROM},
      {2, "A/A/A/A/A/A", "base-deleted", NO_COPY_FROM},
      {1, "E",           "normal",       1, "A/A", MOVED_HERE},
      {1, "E/A",         "normal",       1, "A/A/A", MOVED_HERE},
      {1, "E/A/A",       "normal",       1, "A/A/A/A", MOVED_HERE},
      {1, "E/A/A/A",     "normal",       1, "A/A/A/A/A", MOVED_HERE},
      {1, "E/A/A/A/A",   "normal",       1, "A/A/A/A/A/A", MOVED_HERE},
      {3, "E/A/A",       "base-deleted", NO_COPY_FROM, "D"},
      {3, "E/A/A/A",     "base-deleted", NO_COPY_FROM},
      {3, "E/A/A/A/A",   "base-deleted", NO_COPY_FROM},
      {1, "D",           "normal",       1, "A/A/A/A", MOVED_HERE},
      {1, "D/A",         "normal",       1, "A/A/A/A/A", MOVED_HERE},
      {1, "D/A/A",       "normal",       1, "A/A/A/A/A/A", MOVED_HERE},
      {3, "D/A/A",       "base-deleted", NO_COPY_FROM, "C"},
      {1, "C",           "normal",       1, "A/A/A/A/A/A", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "A", 2));
  {
    nodes_row_t nodes[] = {
      {0, "",            "normal",       0, ""},
      {0, "X",           "normal",       2, "X"},
      {0, "A",           "normal",       2, "A"},
      {0, "A/A",         "normal",       2, "A/A"},
      {0, "A/A/A",       "normal",       2, "A/A/A"},
      {0, "A/A/A/A",     "normal",       2, "A/A/A/A"},
      {0, "A/A/A/A/A",   "normal",       2, "A/A/A/A/A"},
      {0, "A/A/A/A/A/A", "normal",       2, "A/A/A/A/A/A"},
      {2, "A/A",         "base-deleted", NO_COPY_FROM, "E"},
      {2, "A/A/A",       "base-deleted", NO_COPY_FROM},
      {2, "A/A/A/A",     "base-deleted", NO_COPY_FROM},
      {2, "A/A/A/A/A",   "base-deleted", NO_COPY_FROM},
      {2, "A/A/A/A/A/A", "base-deleted", NO_COPY_FROM},
      {1, "E",           "normal",       2, "A/A", MOVED_HERE},
      {1, "E/A",         "normal",       2, "A/A/A", MOVED_HERE},
      {1, "E/A/A",       "normal",       2, "A/A/A/A", MOVED_HERE},
      {1, "E/A/A/A",     "normal",       2, "A/A/A/A/A", MOVED_HERE},
      {1, "E/A/A/A/A",   "normal",       2, "A/A/A/A/A/A", MOVED_HERE},
      {3, "E/A/A",       "base-deleted", NO_COPY_FROM, "D"},
      {3, "E/A/A/A",     "base-deleted", NO_COPY_FROM},
      {3, "E/A/A/A/A",   "base-deleted", NO_COPY_FROM},
      {1, "D",           "normal",       2, "A/A/A/A", MOVED_HERE},
      {1, "D/A",         "normal",       2, "A/A/A/A/A", MOVED_HERE},
      {1, "D/A/A",       "normal",       2, "A/A/A/A/A/A", MOVED_HERE},
      {3, "D/A/A",       "base-deleted", NO_COPY_FROM, "C"},
      {1, "C",           "normal",       2, "A/A/A/A/A/A", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_in_delete(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_in_delete", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/D"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/D/E"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "X"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A/B/C", "C2"));
  SVN_ERR(sbox_wc_delete(&b, "A/B"));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "A/B/C",   "normal",       1, "A/B/C"},
      {2, "A/B",     "base-deleted", NO_COPY_FROM},
      {2, "A/B/C",   "base-deleted", NO_COPY_FROM, "C2"},
      {1, "C2",      "normal",       1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "", 2));
  SVN_ERR(sbox_wc_resolve(&b, "A/B", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(sbox_wc_resolve(&b, "A/B/C", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       2, ""},
      {0, "A",       "normal",       2, "A"},
      {0, "A/B",     "normal",       2, "A/B"},
      {0, "A/B/C",   "normal",       2, "A/B/C"},
      {0, "A/B/C/D", "normal",       2, "A/B/C/D"},
      {2, "A/B",     "base-deleted", NO_COPY_FROM},
      {2, "A/B/C",   "base-deleted", NO_COPY_FROM, "C2"},
      {2, "A/B/C/D", "base-deleted", NO_COPY_FROM},
      {1, "C2",      "normal",       2, "A/B/C", MOVED_HERE},
      {1, "C2/D",    "normal",       2, "A/B/C/D", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "", 3));
  {
    nodes_row_t nodes[] = {
      {0, "",          "normal",       3, ""},
      {0, "A",         "normal",       3, "A"},
      {0, "A/B",       "normal",       3, "A/B"},
      {0, "A/B/C",     "normal",       3, "A/B/C"},
      {0, "A/B/C/D",   "normal",       3, "A/B/C/D"},
      {0, "A/B/C/D/E", "normal",       3, "A/B/C/D/E"},

      {1, "C2",        "normal",       2, "A/B/C", MOVED_HERE},
      {1, "C2/D",      "normal",       2, "A/B/C/D", MOVED_HERE},

      {2, "A/B",              "base-deleted", NO_COPY_FROM},
      {2, "A/B/C",            "base-deleted", NO_COPY_FROM, "C2"},
      {2, "A/B/C/D",          "base-deleted", NO_COPY_FROM},
      {2, "A/B/C/D/E",        "base-deleted", NO_COPY_FROM},

      {0}
    };
    conflict_info_t conflicts[] = {
      {"A/B", FALSE, FALSE, {svn_wc_conflict_action_edit,
                             svn_wc_conflict_reason_deleted}},
      {0}
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(sbox_wc_revert(&b, "A/B", svn_depth_empty));
  {
    nodes_row_t nodes[] = {
      {0, "",          "normal",       3, ""},
      {0, "A",         "normal",       3, "A"},
      {0, "A/B",       "normal",       3, "A/B"},
      {0, "A/B/C",     "normal",       3, "A/B/C"},
      {0, "A/B/C/D",   "normal",       3, "A/B/C/D"},
      {0, "A/B/C/D/E", "normal",       3, "A/B/C/D/E"},
      {3, "A/B/C",     "base-deleted", NO_COPY_FROM, "C2"},
      {3, "A/B/C/D",   "base-deleted", NO_COPY_FROM},
      {3, "A/B/C/D/E", "base-deleted", NO_COPY_FROM},
      {1, "C2",        "normal",       2, "A/B/C", MOVED_HERE},
      {1, "C2/D",      "normal",       2, "A/B/C/D", MOVED_HERE},
      {0}
    };
    conflict_info_t conflicts[] = {
      {"A/B/C", FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "A/B/C"}},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
    /* Where did this conflict come from? */
    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }

  /* Revert should have left a tree-conflict (or broken the move). */
  SVN_ERR(sbox_wc_resolve(&b, "A/B/C", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",          "normal",       3, ""},
      {0, "A",         "normal",       3, "A"},
      {0, "A/B",       "normal",       3, "A/B"},
      {0, "A/B/C",     "normal",       3, "A/B/C"},
      {0, "A/B/C/D",   "normal",       3, "A/B/C/D"},
      {0, "A/B/C/D/E", "normal",       3, "A/B/C/D/E"},
      {3, "A/B/C",     "base-deleted", NO_COPY_FROM, "C2"},
      {3, "A/B/C/D",   "base-deleted", NO_COPY_FROM},
      {3, "A/B/C/D/E", "base-deleted", NO_COPY_FROM},
      {1, "C2",        "normal",       3, "A/B/C", MOVED_HERE},
      {1, "C2/D",      "normal",       3, "A/B/C/D", MOVED_HERE},
      {1, "C2/D/E",    "normal",       3, "A/B/C/D/E", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
    SVN_ERR(check_db_conflicts(&b, "", NULL));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
switch_move(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "switch_move", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/D"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/D/E"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_copy(&b, "A", "X"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "X/B/D/E/F"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_switch(&b, "", "/A", svn_depth_infinity));
  SVN_ERR(sbox_wc_update(&b, "", 2));

  SVN_ERR(sbox_wc_move(&b, "B/C", "C2"));
  SVN_ERR(sbox_wc_move(&b, "B/D", "D2"));
  SVN_ERR(sbox_wc_move(&b, "D2/E", "D2/E2"));
  {
    nodes_row_t nodes[] = {
      {0, "",      "normal",       2, "A"},
      {0, "B",     "normal",       2, "A/B"},
      {0, "B/C",   "normal",       2, "A/B/C"},
      {0, "B/D",   "normal",       2, "A/B/D"},
      {0, "B/D/E", "normal",       2, "A/B/D/E"},
      {2, "B/C",   "base-deleted", NO_COPY_FROM, "C2"},
      {2, "B/D",   "base-deleted", NO_COPY_FROM, "D2"},
      {2, "B/D/E", "base-deleted", NO_COPY_FROM},
      {1, "C2",    "normal",       2, "A/B/C", MOVED_HERE},
      {1, "D2",    "normal",       2, "A/B/D", MOVED_HERE},
      {1, "D2/E",  "normal",       2, "A/B/D/E", MOVED_HERE},
      {2, "D2/E",  "base-deleted", NO_COPY_FROM, "D2/E2"},
      {2, "D2/E2", "normal",       2, "A/B/D/E", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Switch "bumps" revisions and paths and raises conflicts just like
     update. */
  SVN_ERR(sbox_wc_switch(&b, "", "/X", svn_depth_infinity));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       3, "X"},
      {0, "B",       "normal",       3, "X/B"},
      {0, "B/C",     "normal",       3, "X/B/C"},
      {0, "B/D",     "normal",       3, "X/B/D"},
      {0, "B/D/E",   "normal",       3, "X/B/D/E"},
      {0, "B/D/E/F", "normal",       3, "X/B/D/E/F"},
      {2, "B/C",     "base-deleted", NO_COPY_FROM, "C2"},
      {2, "B/D",     "base-deleted", NO_COPY_FROM, "D2"},
      {2, "B/D/E",   "base-deleted", NO_COPY_FROM},
      {2, "B/D/E/F", "base-deleted", NO_COPY_FROM},
      {1, "C2",      "normal",       3, "X/B/C", MOVED_HERE},
      {1, "D2",      "normal",       2, "A/B/D", MOVED_HERE},
      {1, "D2/E",    "normal",       2, "A/B/D/E", MOVED_HERE},
      {2, "D2/E",    "base-deleted", NO_COPY_FROM, "D2/E2"},
      {2, "D2/E2",   "normal",       2, "A/B/D/E", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Conflicts from switch are resolved just like those from update. */
  SVN_ERR(sbox_wc_resolve(&b, "B/D", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       3, "X"},
      {0, "B",       "normal",       3, "X/B"},
      {0, "B/C",     "normal",       3, "X/B/C"},
      {0, "B/D",     "normal",       3, "X/B/D"},
      {0, "B/D/E",   "normal",       3, "X/B/D/E"},
      {0, "B/D/E/F", "normal",       3, "X/B/D/E/F"},
      {2, "B/C",     "base-deleted", NO_COPY_FROM, "C2"},
      {2, "B/D",     "base-deleted", NO_COPY_FROM, "D2"},
      {2, "B/D/E",   "base-deleted", NO_COPY_FROM},
      {2, "B/D/E/F", "base-deleted", NO_COPY_FROM},
      {1, "C2",      "normal",       3, "X/B/C", MOVED_HERE},
      {1, "D2",      "normal",       3, "X/B/D", MOVED_HERE},
      {1, "D2/E",    "normal",       3, "X/B/D/E", MOVED_HERE},
      {1, "D2/E/F",  "normal",       3, "X/B/D/E/F", MOVED_HERE},
      {2, "D2/E",    "base-deleted", NO_COPY_FROM, "D2/E2"},
      {2, "D2/E/F",  "base-deleted", NO_COPY_FROM},
      {2, "D2/E2",   "normal",       2, "A/B/D/E", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_resolve(&b, "D2/E", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       3, "X"},
      {0, "B",       "normal",       3, "X/B"},
      {0, "B/C",     "normal",       3, "X/B/C"},
      {0, "B/D",     "normal",       3, "X/B/D"},
      {0, "B/D/E",   "normal",       3, "X/B/D/E"},
      {0, "B/D/E/F", "normal",       3, "X/B/D/E/F"},
      {2, "B/C",     "base-deleted", NO_COPY_FROM, "C2"},
      {2, "B/D",     "base-deleted", NO_COPY_FROM, "D2"},
      {2, "B/D/E",   "base-deleted", NO_COPY_FROM},
      {2, "B/D/E/F", "base-deleted", NO_COPY_FROM},
      {1, "C2",      "normal",       3, "X/B/C", MOVED_HERE},
      {1, "D2",      "normal",       3, "X/B/D", MOVED_HERE},
      {1, "D2/E",    "normal",       3, "X/B/D/E", MOVED_HERE},
      {1, "D2/E/F",  "normal",       3, "X/B/D/E/F", MOVED_HERE},
      {2, "D2/E",    "base-deleted", NO_COPY_FROM, "D2/E2"},
      {2, "D2/E/F",  "base-deleted", NO_COPY_FROM},
      {2, "D2/E2",   "normal",       3, "X/B/D/E", MOVED_HERE},
      {2, "D2/E2/F", "normal",       3, "X/B/D/E/F", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_replace(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_replace", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "B"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "B/X"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A", "X"));
  SVN_ERR(sbox_wc_move(&b, "B", "A"));
  SVN_ERR(sbox_wc_move(&b, "X", "B"));
  {
    nodes_row_t nodes[] = {
      {0, "",  "normal",       1, ""},
      {0, "A", "normal",       1, "A"},
      {0, "B", "normal",       1, "B"},
      {1, "A", "normal",       1, "B", FALSE, "B", TRUE},
      {1, "B", "normal",       1, "A", FALSE, "A", TRUE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "", 2));
  {
    nodes_row_t nodes[] = {
      {0, "",    "normal",       2, ""},
      {0, "A",   "normal",       2, "A"},
      {0, "B",   "normal",       2, "B"},
      {0, "B/X", "normal",       2, "B/X"},
      {1, "A",   "normal",       1, "B", FALSE, "B", TRUE},
      {1, "B",   "normal",       2, "A", FALSE, "A", TRUE},
      {1, "B/X", "base-deleted", NO_COPY_FROM},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_resolve(&b, "B", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",    "normal",       2, ""},
      {0, "A",   "normal",       2, "A"},
      {0, "B",   "normal",       2, "B"},
      {0, "B/X", "normal",       2, "B/X"},
      {1, "A",   "normal",       2, "B", FALSE, "B", TRUE},
      {1, "A/X", "normal",       2, "B/X", MOVED_HERE},
      {1, "B",   "normal",       2, "A", FALSE, "A", TRUE},
      {1, "B/X", "base-deleted", NO_COPY_FROM},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
layered_moved_to(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  svn_error_t *err;

  SVN_ERR(svn_test__sandbox_create(&b, "layered_moved_to", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/D"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/D/E"));
  SVN_ERR(sbox_wc_mkdir(&b, "C"));
  SVN_ERR(sbox_wc_mkdir(&b, "C/D"));
  SVN_ERR(sbox_wc_mkdir(&b, "C/D/E"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_propset(&b, "property", "value", "A/B/C/D/E"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_propset(&b, "property", "value", "C/D/E"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "P"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_propset(&b, "property2", "value", "A/B/C/D/E"));
  SVN_ERR(sbox_wc_propset(&b, "property2", "value", "C/D/E"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A", "X"));
  SVN_ERR(sbox_wc_move(&b, "X/B/C/D/E", "E2"));
  SVN_ERR(sbox_wc_delete(&b, "X/B/C"));
  SVN_ERR(sbox_wc_move(&b, "C", "X/B/C"));
  SVN_ERR(sbox_wc_move(&b, "X/B/C/D/E", "E3"));
  {
    nodes_row_t nodes[] = {
      {0, "",            "normal",       1, ""},
      {0, "A",           "normal",       1, "A"},
      {0, "A/B",         "normal",       1, "A/B"},
      {0, "A/B/C",       "normal",       1, "A/B/C"},
      {0, "A/B/C/D",     "normal",       1, "A/B/C/D"},
      {0, "A/B/C/D/E",   "normal",       1, "A/B/C/D/E"},
      {0, "C",           "normal",       1, "C"},
      {0, "C/D",         "normal",       1, "C/D"},
      {0, "C/D/E",       "normal",       1, "C/D/E"},
      {1, "A",           "base-deleted", NO_COPY_FROM, "X"},
      {1, "A/B",         "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",       "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D/E",   "base-deleted", NO_COPY_FROM},
      {1, "C",           "base-deleted", NO_COPY_FROM, "X/B/C"},
      {1, "C/D",         "base-deleted", NO_COPY_FROM},
      {1, "C/D/E",       "base-deleted", NO_COPY_FROM},
      {1, "X",           "normal",       1, "A", MOVED_HERE},
      {1, "X/B",         "normal",       1, "A/B", MOVED_HERE},
      {1, "X/B/C",       "normal",       1, "A/B/C", MOVED_HERE},
      {1, "X/B/C/D",     "normal",       1, "A/B/C/D", MOVED_HERE},
      {1, "X/B/C/D/E",   "normal",       1, "A/B/C/D/E", MOVED_HERE},
      {3, "X/B/C",       "normal",       1, "C", MOVED_HERE},
      {3, "X/B/C/D",     "normal",       1, "C/D", MOVED_HERE},
      {3, "X/B/C/D/E",   "normal",       1, "C/D/E", FALSE, "E2", TRUE},
      {5, "X/B/C/D/E",   "base-deleted", NO_COPY_FROM, "E3"},
      {1, "E2",          "normal",       1, "A/B/C/D/E", MOVED_HERE},
      {1, "E3",          "normal",       1, "C/D/E", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "A", 2));
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(sbox_wc_resolve(&b, "X/B/C", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(sbox_wc_resolve(&b, "X/B/C/D/E", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",            "normal",       1, ""},
      {0, "A",           "normal",       2, "A"},
      {0, "A/B",         "normal",       2, "A/B"},
      {0, "A/B/C",       "normal",       2, "A/B/C"},
      {0, "A/B/C/D",     "normal",       2, "A/B/C/D"},
      {0, "A/B/C/D/E",   "normal",       2, "A/B/C/D/E"},
      {0, "C",           "normal",       1, "C"},
      {0, "C/D",         "normal",       1, "C/D"},
      {0, "C/D/E",       "normal",       1, "C/D/E"},
      {1, "A",           "base-deleted", NO_COPY_FROM, "X"},
      {1, "A/B",         "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",       "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D/E",   "base-deleted", NO_COPY_FROM},
      {1, "C",           "base-deleted", NO_COPY_FROM, "X/B/C"},
      {1, "C/D",         "base-deleted", NO_COPY_FROM},
      {1, "C/D/E",       "base-deleted", NO_COPY_FROM},
      {1, "X",           "normal",       2, "A", MOVED_HERE},
      {1, "X/B",         "normal",       2, "A/B", MOVED_HERE},
      {1, "X/B/C",       "normal",       2, "A/B/C", MOVED_HERE},
      {1, "X/B/C/D",     "normal",       2, "A/B/C/D", MOVED_HERE},
      {1, "X/B/C/D/E",   "normal",       2, "A/B/C/D/E", MOVED_HERE},
      {3, "X/B/C",       "normal",       1, "C", MOVED_HERE},
      {3, "X/B/C/D",     "normal",       1, "C/D", MOVED_HERE},
      {3, "X/B/C/D/E",   "normal",       1, "C/D/E", FALSE, "E2", TRUE},
      {5, "X/B/C/D/E",   "base-deleted", NO_COPY_FROM, "E3"},
      {1, "E2",          "normal",       2, "A/B/C/D/E", MOVED_HERE},
      {1, "E3",          "normal",       1, "C/D/E", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "C", 3));
  SVN_ERR(sbox_wc_resolve(&b, "C", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(sbox_wc_resolve(&b, "X/B/C/D/E", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",            "normal",       1, ""},
      {0, "A",           "normal",       2, "A"},
      {0, "A/B",         "normal",       2, "A/B"},
      {0, "A/B/C",       "normal",       2, "A/B/C"},
      {0, "A/B/C/D",     "normal",       2, "A/B/C/D"},
      {0, "A/B/C/D/E",   "normal",       2, "A/B/C/D/E"},
      {0, "C",           "normal",       3, "C"},
      {0, "C/D",         "normal",       3, "C/D"},
      {0, "C/D/E",       "normal",       3, "C/D/E"},
      {1, "A",           "base-deleted", NO_COPY_FROM, "X"},
      {1, "A/B",         "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",       "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D/E",   "base-deleted", NO_COPY_FROM},
      {1, "C",           "base-deleted", NO_COPY_FROM, "X/B/C"},
      {1, "C/D",         "base-deleted", NO_COPY_FROM},
      {1, "C/D/E",       "base-deleted", NO_COPY_FROM},
      {1, "X",           "normal",       2, "A", MOVED_HERE},
      {1, "X/B",         "normal",       2, "A/B", MOVED_HERE},
      {1, "X/B/C",       "normal",       2, "A/B/C", MOVED_HERE},
      {1, "X/B/C/D",     "normal",       2, "A/B/C/D", MOVED_HERE},
      {1, "X/B/C/D/E",   "normal",       2, "A/B/C/D/E", MOVED_HERE},
      {3, "X/B/C",       "normal",       3, "C", MOVED_HERE},
      {3, "X/B/C/D",     "normal",       3, "C/D", MOVED_HERE},
      {3, "X/B/C/D/E",   "normal",       3, "C/D/E", FALSE, "E2", TRUE},
      {5, "X/B/C/D/E",   "base-deleted", NO_COPY_FROM, "E3"},
      {1, "E2",          "normal",       2, "A/B/C/D/E", MOVED_HERE},
      {1, "E3",          "normal",       3, "C/D/E", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* An update with no text/property/tree changes in A, just a revision bump. */
  SVN_ERR(sbox_wc_update(&b, "A", 4));
  {
    nodes_row_t nodes[] = {
      {0, "",            "normal",       1, ""},
      {0, "A",           "normal",       4, "A"},
      {0, "A/B",         "normal",       4, "A/B"},
      {0, "A/B/C",       "normal",       4, "A/B/C"},
      {0, "A/B/C/D",     "normal",       4, "A/B/C/D"},
      {0, "A/B/C/D/E",   "normal",       4, "A/B/C/D/E"},
      {0, "C",           "normal",       3, "C"},
      {0, "C/D",         "normal",       3, "C/D"},
      {0, "C/D/E",       "normal",       3, "C/D/E"},
      {1, "A",           "base-deleted", NO_COPY_FROM, "X"},
      {1, "A/B",         "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",       "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D/E",   "base-deleted", NO_COPY_FROM},
      {1, "C",           "base-deleted", NO_COPY_FROM, "X/B/C"},
      {1, "C/D",         "base-deleted", NO_COPY_FROM},
      {1, "C/D/E",       "base-deleted", NO_COPY_FROM},
      {1, "X",           "normal",       4, "A", MOVED_HERE},
      {1, "X/B",         "normal",       4, "A/B", MOVED_HERE},
      {1, "X/B/C",       "normal",       4, "A/B/C", MOVED_HERE},
      {1, "X/B/C/D",     "normal",       4, "A/B/C/D", MOVED_HERE},
      {1, "X/B/C/D/E",   "normal",       4, "A/B/C/D/E", MOVED_HERE},
      {3, "X/B/C",       "normal",       3, "C", MOVED_HERE},
      {3, "X/B/C/D",     "normal",       3, "C/D", MOVED_HERE},
      {3, "X/B/C/D/E",   "normal",       3, "C/D/E", FALSE, "E2", TRUE},
      {5, "X/B/C/D/E",   "base-deleted", NO_COPY_FROM, "E3"},
      {1, "E2",          "normal",       4, "A/B/C/D/E", MOVED_HERE},
      {1, "E3",          "normal",       3, "C/D/E", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Update for conflicts on A and C */
  SVN_ERR(sbox_wc_update(&b, "", 5));
  {
    nodes_row_t nodes[] = {
      {0, "",            "normal",       5, ""},
      {0, "A",           "normal",       5, "A"},
      {0, "A/B",         "normal",       5, "A/B"},
      {0, "A/B/C",       "normal",       5, "A/B/C"},
      {0, "A/B/C/D",     "normal",       5, "A/B/C/D"},
      {0, "A/B/C/D/E",   "normal",       5, "A/B/C/D/E"},
      {0, "P",           "normal",       5, "P"},
      {0, "C",           "normal",       5, "C"},
      {0, "C/D",         "normal",       5, "C/D"},
      {0, "C/D/E",       "normal",       5, "C/D/E"},
      {1, "A",           "base-deleted", NO_COPY_FROM, "X"},
      {1, "A/B",         "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",       "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D/E",   "base-deleted", NO_COPY_FROM},
      {1, "C",           "base-deleted", NO_COPY_FROM, "X/B/C"},
      {1, "C/D",         "base-deleted", NO_COPY_FROM},
      {1, "C/D/E",       "base-deleted", NO_COPY_FROM},
      {1, "X",           "normal",       4, "A", MOVED_HERE},
      {1, "X/B",         "normal",       4, "A/B", MOVED_HERE},
      {1, "X/B/C",       "normal",       4, "A/B/C", MOVED_HERE},
      {1, "X/B/C/D",     "normal",       4, "A/B/C/D", MOVED_HERE},
      {1, "X/B/C/D/E",   "normal",       4, "A/B/C/D/E", MOVED_HERE},
      {3, "X/B/C",       "normal",       3, "C", MOVED_HERE},
      {3, "X/B/C/D",     "normal",       3, "C/D", MOVED_HERE},
      {3, "X/B/C/D/E",   "normal",       3, "C/D/E", FALSE, "E2", TRUE},
      {5, "X/B/C/D/E",   "base-deleted", NO_COPY_FROM, "E3"},
      {1, "E2",          "normal",       4, "A/B/C/D/E", MOVED_HERE},
      {1, "E3",          "normal",       3, "C/D/E", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Partially resolve A */
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(sbox_wc_resolve(&b, "X/B/C", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));

  /* Cannot resolve C */
  err = sbox_wc_resolve(&b, "C", svn_depth_empty,
                        svn_wc_conflict_choose_mine_conflict);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE);

  /* Complete resolving A and then resolve C */
  SVN_ERR(sbox_wc_resolve(&b, "X/B/C/D/E", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(sbox_wc_resolve(&b, "C", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));

  {
    nodes_row_t nodes[] = {
      {0, "",            "normal",       5, ""},
      {0, "A",           "normal",       5, "A"},
      {0, "A/B",         "normal",       5, "A/B"},
      {0, "A/B/C",       "normal",       5, "A/B/C"},
      {0, "A/B/C/D",     "normal",       5, "A/B/C/D"},
      {0, "A/B/C/D/E",   "normal",       5, "A/B/C/D/E"},
      {0, "P",           "normal",       5, "P"},
      {0, "C",           "normal",       5, "C"},
      {0, "C/D",         "normal",       5, "C/D"},
      {0, "C/D/E",       "normal",       5, "C/D/E"},
      {1, "A",           "base-deleted", NO_COPY_FROM, "X"},
      {1, "A/B",         "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",       "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D/E",   "base-deleted", NO_COPY_FROM},
      {1, "C",           "base-deleted", NO_COPY_FROM, "X/B/C"},
      {1, "C/D",         "base-deleted", NO_COPY_FROM},
      {1, "C/D/E",       "base-deleted", NO_COPY_FROM},
      {1, "X",           "normal",       5, "A", MOVED_HERE},
      {1, "X/B",         "normal",       5, "A/B", MOVED_HERE},
      {1, "X/B/C",       "normal",       5, "A/B/C", MOVED_HERE},
      {1, "X/B/C/D",     "normal",       5, "A/B/C/D", MOVED_HERE},
      {1, "X/B/C/D/E",   "normal",       5, "A/B/C/D/E", MOVED_HERE},
      {3, "X/B/C",       "normal",       5, "C", MOVED_HERE},
      {3, "X/B/C/D",     "normal",       5, "C/D", MOVED_HERE},
      {3, "X/B/C/D/E",   "normal",       5, "C/D/E", FALSE, "E2", TRUE},
      {5, "X/B/C/D/E",   "base-deleted", NO_COPY_FROM, "E3"},
      {1, "E2",          "normal",       5, "A/B/C/D/E", MOVED_HERE},
      {1, "E3",          "normal",       3, "C/D/E", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
update_within_move(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  svn_error_t *err;

  SVN_ERR(svn_test__sandbox_create(&b, "update_within_move", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/D"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A", "X"));
  SVN_ERR(sbox_wc_update(&b, "A/B/C", 2));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "A/B/C",   "normal",       2, "A/B/C"},
      {0, "A/B/C/D", "normal",       2, "A/B/C/D"},
      {1, "A",       "base-deleted", NO_COPY_FROM, "X"},
      {1, "A/B",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",   "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D", "base-deleted", NO_COPY_FROM},
      {1, "X",       "normal",       1, "A", MOVED_HERE},
      {1, "X/B",     "normal",       1, "A/B", MOVED_HERE},
      {1, "X/B/C",   "normal",       1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Can't resolve mixed-revision source to mine-conflict. */
  err = sbox_wc_resolve(&b, "A", svn_depth_empty,
                        svn_wc_conflict_choose_mine_conflict);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE);

  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_merged));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "A/B/C",   "normal",       2, "A/B/C"},
      {0, "A/B/C/D", "normal",       2, "A/B/C/D"},
      {1, "A",       "base-deleted", NO_COPY_FROM},
      {1, "A/B",     "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",   "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D", "base-deleted", NO_COPY_FROM},
      {1, "X",       "normal",       1, "A"},
      {1, "X/B",     "normal",       1, "A/B"},
      {1, "X/B/C",   "normal",       1, "A/B/C"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }


  return SVN_NO_ERROR;
}

static svn_error_t *
commit_moved_descendant(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  SVN_ERR(svn_test__sandbox_create(&b, "commit_moved_descendant", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A/A/A/A"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_copy(&b, "A", "A_copied"));
  SVN_ERR(sbox_wc_move(&b, "A/A/A", "AAA_moved"));
  SVN_ERR(sbox_wc_delete(&b, "A/A"));
  SVN_ERR(sbox_wc_copy(&b, "A_copied/A", "A/A"));

  /* And now I want to commit AAA_moved (the entire move), but not
     the replacement of A/A */

  /* For now, just start committing directly */
  /* ### This fails, because A/A/A is not collected by the commit
         harvester (it doesn't need committing, but our move filter
         blocks on it) */
  SVN_ERR(sbox_wc_commit(&b, ""));

  /* It would be nicer if we could just do a: */
  /* SVN_ERR(sbox_wc_commit(&b, "AAA_moved")); */
  /* Which then includes the delete half of the move, when it is
     shadowed, like in this case. The commit processing doesn't
     support this yet though*/

   {
    nodes_row_t nodes[] = {
      {0, "",                 "normal",       0, ""},
      {0, "A",                "normal",       1, "A"},
      {0, "A/A",              "normal",       2, "A/A"},
      {0, "A/A/A",            "normal",       2, "A/A/A"},
      {0, "A/A/A/A",          "normal",       2, "A/A/A/A"},
      {0, "A/A/A/A/A",        "normal",       2, "A/A/A/A/A"},
      {0, "A/A/A/A/A/A",      "normal",       2, "A/A/A/A/A/A"},
      {0, "A_copied",         "normal",       2, "A_copied"},
      {0, "A_copied/A",       "normal",       2, "A_copied/A"},
      {0, "A_copied/A/A",     "normal",       2, "A_copied/A/A"},
      {0, "A_copied/A/A/A",   "normal",       2, "A_copied/A/A/A"},
      {0, "A_copied/A/A/A/A", "normal",       2, "A_copied/A/A/A/A"},
      {0, "A_copied/A/A/A/A/A","normal",       2, "A_copied/A/A/A/A/A"},
      {0, "AAA_moved",        "normal",       2, "AAA_moved"},
      {0, "AAA_moved/A",      "normal",       2, "AAA_moved/A"},
      {0, "AAA_moved/A/A",    "normal",       2, "AAA_moved/A/A"},
      {0, "AAA_moved/A/A/A",  "normal",       2, "AAA_moved/A/A/A"},

      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
commit_moved_away_descendant(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  SVN_ERR(svn_test__sandbox_create(&b, "commit_moved_away_descendant",
                                   opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A/A/A/A"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_copy(&b, "A", "A_copied"));
  SVN_ERR(sbox_wc_move(&b, "A/A/A", "AAA_moved"));
  SVN_ERR(sbox_wc_delete(&b, "A/A"));
  SVN_ERR(sbox_wc_copy(&b, "A_copied/A", "A/A"));

  {
    nodes_row_t nodes[] = {
      {0, "",                   "normal",       0, ""},
      {0, "A",                  "normal",       1, "A"},
      {0, "A/A",                "normal",       1, "A/A"},
      {0, "A/A/A",              "normal",       1, "A/A/A"},
      {0, "A/A/A/A",            "normal",       1, "A/A/A/A"},
      {0, "A/A/A/A/A",          "normal",       1, "A/A/A/A/A"},
      {0, "A/A/A/A/A/A",        "normal",       1, "A/A/A/A/A/A"},
      {1, "A_copied",           "normal",       1, "A"},
      {1, "A_copied/A",         "normal",       1, "A/A"},
      {1, "A_copied/A/A",       "normal",       1, "A/A/A"},
      {1, "A_copied/A/A/A",     "normal",       1, "A/A/A/A"},
      {1, "A_copied/A/A/A/A",   "normal",       1, "A/A/A/A/A"},
      {1, "A_copied/A/A/A/A/A", "normal",       1, "A/A/A/A/A/A"},
      {1, "AAA_moved",          "normal",       1, "A/A/A", MOVED_HERE},
      {1, "AAA_moved/A",        "normal",       1, "A/A/A/A", MOVED_HERE},
      {1, "AAA_moved/A/A",      "normal",       1, "A/A/A/A/A", MOVED_HERE},
      {1, "AAA_moved/A/A/A",    "normal",       1, "A/A/A/A/A/A", MOVED_HERE},
      {2, "A/A",                "normal",       1, "A/A"},
      {2, "A/A/A",              "normal",       1, "A/A/A", FALSE, "AAA_moved"},
      {2, "A/A/A/A",            "normal",       1, "A/A/A/A"},
      {2, "A/A/A/A/A",          "normal",       1, "A/A/A/A/A"},
      {2, "A/A/A/A/A/A",        "normal",       1, "A/A/A/A/A/A"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* And now I want to make sure that I can't commit A, without also
     committing AAA_moved, as that would break the move*/
  SVN_ERR(sbox_wc_commit(&b, "A"));

  {
    nodes_row_t nodes[] = {
      {0, "",                   "normal",       0, ""},
      {0, "A",                  "normal",       1, "A"},
      {0, "A/A",                "normal",       2, "A/A"},
      {0, "A/A/A",              "normal",       2, "A/A/A"},
      {0, "A/A/A/A",            "normal",       2, "A/A/A/A"},
      {0, "A/A/A/A/A",          "normal",       2, "A/A/A/A/A"},
      {0, "A/A/A/A/A/A",        "normal",       2, "A/A/A/A/A/A"},
      {1, "A_copied",           "normal",       1, "A"},
      {1, "A_copied/A",         "normal",       1, "A/A"},
      {1, "A_copied/A/A",       "normal",       1, "A/A/A"},
      {1, "A_copied/A/A/A",     "normal",       1, "A/A/A/A"},
      {1, "A_copied/A/A/A/A",   "normal",       1, "A/A/A/A/A"},
      {1, "A_copied/A/A/A/A/A", "normal",       1, "A/A/A/A/A/A"},
      {1, "AAA_moved",          "normal",       1, "A/A/A"},
      {1, "AAA_moved/A",        "normal",       1, "A/A/A/A"},
      {1, "AAA_moved/A/A",      "normal",       1, "A/A/A/A/A"},
      {1, "AAA_moved/A/A/A",    "normal",       1, "A/A/A/A/A/A"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                          "The commit should have failed");

  /*return SVN_NO_ERROR;*/
}

static svn_error_t *
finite_move_update_bump(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  svn_error_t *err;
  SVN_ERR(svn_test__sandbox_create(&b, "finite_move_update_bump",
                                   opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "P"));
  SVN_ERR(sbox_wc_mkdir(&b, "P/Q"));
  SVN_ERR(sbox_file_write(&b, "P/Q/f", "r1 content\n"));
  SVN_ERR(sbox_wc_add(&b, "P/Q/f"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "X"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A/B", "B2"));
  SVN_ERR(sbox_wc_update(&b, "A/B/C", 2));
  SVN_ERR(check_tree_conflict_repos_path(&b, "A/B", NULL, NULL));
  err = sbox_wc_resolve(&b, "A/B", svn_depth_empty,
                        svn_wc_conflict_choose_mine_conflict);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE);
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "A/B/C",   "normal",       2, "A/B/C"},
      {0, "P",       "normal",       1, "P"},
      {0, "P/Q",     "normal",       1, "P/Q"},
      {0, "P/Q/f",   "normal",       1, "P/Q/f"},
      {2, "A/B",     "base-deleted", NO_COPY_FROM, "B2"},
      {2, "A/B/C",   "base-deleted", NO_COPY_FROM},
      {1, "B2",      "normal",       1, "A/B", MOVED_HERE},
      {1, "B2/C",    "normal",       1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_revert(&b, "", svn_depth_infinity));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_move(&b, "A/B", "B2"));
  SVN_ERR(sbox_wc_move(&b, "P/Q", "Q2"));
  SVN_ERR(sbox_wc_update_depth(&b, "A/B", 2, svn_depth_files, FALSE));
  SVN_ERR(sbox_wc_update_depth(&b, "P/Q", 2, svn_depth_files, FALSE));
  {
    conflict_info_t conflicts[] = {
      {"A/B", FALSE, FALSE, {svn_wc_conflict_action_edit,
                             svn_wc_conflict_reason_moved_away, "A/B"}},
      {"P/Q", FALSE, FALSE, {svn_wc_conflict_action_edit,
                             svn_wc_conflict_reason_moved_away, "P/Q"}},
      {0}
    };
    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }

  SVN_ERR(check_tree_conflict_repos_path(&b, "A/B", "A/B", "A/B"));
  SVN_ERR(check_tree_conflict_repos_path(&b, "P/Q", "P/Q", "P/Q"));
  err = sbox_wc_resolve(&b, "A/B", svn_depth_empty,
                        svn_wc_conflict_choose_mine_conflict);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE);

  /* sbox_wc_resolve() obtains a lock on the target path, so now it
     will apply the change on the target */
  SVN_ERR(sbox_wc_resolve(&b, "P/Q", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       2, "A/B"},
      {0, "A/B/C",   "normal",       1, "A/B/C"},
      {0, "P",       "normal",       1, "P"},
      {0, "P/Q",     "normal",       2, "P/Q"},
      {0, "P/Q/f",   "normal",       2, "P/Q/f"},
      {2, "A/B",     "base-deleted", NO_COPY_FROM, "B2"},
      {2, "A/B/C",   "base-deleted", NO_COPY_FROM},
      {2, "P/Q",     "base-deleted", NO_COPY_FROM, "Q2"},
      {2, "P/Q/f",   "base-deleted", NO_COPY_FROM},
      {1, "B2",      "normal",       1, "A/B", MOVED_HERE},
      {1, "B2/C",    "normal",       1, "A/B/C", MOVED_HERE},
      {1, "Q2",      "normal",       2, "P/Q", MOVED_HERE},
      {1, "Q2/f",    "normal",       2, "P/Q/f", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_revert(&b, "", svn_depth_infinity));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_move(&b, "A/B", "B2"));
  SVN_ERR(sbox_wc_move(&b, "P", "P2"));
  SVN_ERR(sbox_wc_update_depth(&b, "A/B", 2, svn_depth_immediates, FALSE));
  SVN_ERR(sbox_wc_update_depth(&b, "P", 2, svn_depth_immediates, FALSE));
  {
    conflict_info_t conflicts[] = {
      {"A/B", FALSE, FALSE, {svn_wc_conflict_action_edit,
                             svn_wc_conflict_reason_moved_away, "A/B"}},
      {"P",   FALSE, FALSE, {svn_wc_conflict_action_edit,
                             svn_wc_conflict_reason_moved_away, "P"}},
      {0}
    };
    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(check_tree_conflict_repos_path(&b, "P", "P", "P"));
  SVN_ERR(check_tree_conflict_repos_path(&b, "A/B", "A/B", "A/B"));
  err = sbox_wc_resolve(&b, "P", svn_depth_empty,
                        svn_wc_conflict_choose_mine_conflict);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE);
  SVN_ERR(sbox_wc_resolve(&b, "A/B", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       2, "A/B"},
      {0, "A/B/C",   "normal",       2, "A/B/C"},
      {0, "P",       "normal",       2, "P"},
      {0, "P/Q",     "normal",       2, "P/Q"},
      {0, "P/Q/f",   "normal",       1, "P/Q/f"},
      {2, "A/B",     "base-deleted", NO_COPY_FROM, "B2"},
      {2, "A/B/C",   "base-deleted", NO_COPY_FROM},
      {1, "P",       "base-deleted", NO_COPY_FROM, "P2"},
      {1, "P/Q",     "base-deleted", NO_COPY_FROM},
      {1, "P/Q/f",   "base-deleted", NO_COPY_FROM},
      {1, "B2",      "normal",       2, "A/B", MOVED_HERE},
      {1, "B2/C",    "normal",       2, "A/B/C", MOVED_HERE},
      {1, "P2",      "normal",       1, "P", MOVED_HERE},
      {1, "P2/Q",    "normal",       1, "P/Q", MOVED_HERE},
      {1, "P2/Q/f",  "normal",       1, "P/Q/f", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_revert(&b, "", svn_depth_infinity));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_move(&b, "A/B/C", "C2"));
  SVN_ERR(sbox_wc_move(&b, "P/Q", "Q2"));
  SVN_ERR(sbox_wc_update_depth(&b, "A/B/C", 2, svn_depth_empty, FALSE));
  SVN_ERR(sbox_wc_update_depth(&b, "P/Q", 2, svn_depth_empty, FALSE));
  {
    conflict_info_t conflicts[] = {
      {"A/B/C", FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "A/B/C"}},
      {"P/Q",   FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "P/Q"}},

      {0}
    };
    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(check_tree_conflict_repos_path(&b, "A/B/C", "A/B/C", "A/B/C"));
  SVN_ERR(check_tree_conflict_repos_path(&b, "P/Q", "P/Q", "P/Q"));
  err = sbox_wc_resolve(&b, "P/Q", svn_depth_empty,
                        svn_wc_conflict_choose_mine_conflict);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE);
  SVN_ERR(sbox_wc_resolve(&b, "A/B/C", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "A/B/C",   "normal",       2, "A/B/C"},
      {0, "P",       "normal",       1, "P"},
      {0, "P/Q",     "normal",       2, "P/Q"},
      {0, "P/Q/f",   "normal",       1, "P/Q/f"},
      {3, "A/B/C",   "base-deleted", NO_COPY_FROM, "C2"},
      {2, "P/Q",     "base-deleted", NO_COPY_FROM, "Q2"},
      {2, "P/Q/f",   "base-deleted", NO_COPY_FROM},
      {1, "C2",      "normal",       2, "A/B/C", MOVED_HERE},
      {1, "Q2",      "normal",       1, "P/Q", MOVED_HERE},
      {1, "Q2/f",    "normal",       1, "P/Q/f", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_away_delete_update(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  SVN_ERR(svn_test__sandbox_create(&b, "move_away_delete_update",
                                   opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "P"));
  SVN_ERR(sbox_wc_mkdir(&b, "P/Q"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_delete(&b, "A/B"));
  SVN_ERR(sbox_wc_delete(&b, "P/Q"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_move(&b, "A/B/C", "C2"));
  SVN_ERR(sbox_wc_move(&b, "P/Q", "Q2"));

  /* Update to r2 removes the move sources and clears moved_here from
     the move destinations. */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  {
    nodes_row_t nodes[] = {
      {0, "",   "normal", 2, ""},
      {0, "A",  "normal", 2, "A"},
      {0, "P",  "normal", 2, "P"},
      {1, "C2", "normal", 1, "A/B/C", MOVED_HERE},
      {1, "Q2", "normal", 1, "P/Q"},

      {2, "A/B",              "normal",       1, "A/B"},
      {2, "A/B/C",            "normal",       1, "A/B/C"},
      {3, "A/B/C",            "base-deleted", NO_COPY_FROM, "C2"},
      {0}
    };
    conflict_info_t conflicts[] = {
      {"A/B", FALSE, FALSE, {svn_wc_conflict_action_delete,
                             svn_wc_conflict_reason_edited}},
      {"P/Q", FALSE, FALSE, {svn_wc_conflict_action_delete,
                             svn_wc_conflict_reason_moved_away, "P/Q"}},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_not_present_variants(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  SVN_ERR(svn_test__sandbox_create(&b, "move_not_present_variants",
                                   opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "B"));
  SVN_ERR(sbox_wc_mkdir(&b, "B/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "C"));
  SVN_ERR(sbox_wc_mkdir(&b, "C/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "D"));
  SVN_ERR(sbox_wc_mkdir(&b, "D/B"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_delete(&b, "A/B"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_delete(&b, "B/B"));
  SVN_ERR(sbox_wc_update(&b, "C/B", 0));
  SVN_ERR(sbox_wc_exclude(&b, "D/B"));

  SVN_ERR(sbox_wc_copy(&b, "A", "cA"));
  SVN_ERR(sbox_wc_copy(&b, "B", "cB"));
  SVN_ERR(sbox_wc_copy(&b, "C", "cC"));
  SVN_ERR(sbox_wc_copy(&b, "D", "cD"));

  SVN_ERR(sbox_wc_copy(&b, "cA", "ccA"));
  SVN_ERR(sbox_wc_copy(&b, "cB", "ccB"));
  SVN_ERR(sbox_wc_copy(&b, "cC", "ccC"));
  SVN_ERR(sbox_wc_copy(&b, "cD", "ccD"));

  {
    nodes_row_t nodes[] = {
      {0, "",           "normal",       1, ""},

      /* Copy of a deleted + committed node */
      {0, "A",          "normal",       1, "A"},
      {0, "A/B",        "not-present",  2, "A/B"},

      {1, "cA",         "normal",       1, "A"},
      {1, "cA/B",       "not-present",  2, "A/B"},

      {1, "ccA",        "normal",       1, "A"},
      {1, "ccA/B",      "not-present",  2, "A/B"},

      /* Copy of a local deleted node */
      {0, "B",          "normal",       1, "B"},
      {0, "B/B",        "normal",       1, "B/B"},
      {2, "B/B",        "base-deleted", NO_COPY_FROM},

      {1, "cB",         "normal",       1, "B",},
      {1, "cB/B",       "normal",       1, "B/B"},
      {2, "cB/B",       "base-deleted", NO_COPY_FROM},

      {1, "ccB",        "normal",       1, "B"},
      {1, "ccB/B",      "normal",       1, "B/B"},
      {2, "ccB/B",      "base-deleted", NO_COPY_FROM},

      /* Copy of a to r0 updated node */
      {0, "C",          "normal",       1, "C"},
      {0, "C/B",        "not-present",  0, "C/B"},

      {1, "cC",         "normal",       1, "C"},
      {1, "cC/B",       "not-present",  0, "C/B"},

      {1, "ccC",        "normal",       1, "C"},
      {1, "ccC/B",      "not-present",  0, "C/B"},

      /* Copy of an excluded node */
      {0, "D",          "normal",       1, "D"},
      {0, "D/B",        "excluded",     1, "D/B"},

      {1, "cD",         "normal",       1, "D"},
      {1, "cD/B",       "excluded",     1, "D/B"},

      {1, "ccD",        "normal",       1, "D"},
      {1, "ccD/B",      "excluded",     1, "D/B"},

      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_revert(&b, "", svn_depth_infinity));
  SVN_ERR(sbox_wc_delete(&b, "B/B"));

  /* And now do the same thing with moves */

  SVN_ERR(sbox_wc_move(&b, "A", "mA"));
  SVN_ERR(sbox_wc_move(&b, "B", "mB"));
  SVN_ERR(sbox_wc_move(&b, "C", "mC"));
  SVN_ERR(sbox_wc_move(&b, "D", "mD"));

  SVN_ERR(sbox_wc_move(&b, "mA", "mmA"));
  SVN_ERR(sbox_wc_move(&b, "mB", "mmB"));
  SVN_ERR(sbox_wc_move(&b, "mC", "mmC"));
  SVN_ERR(sbox_wc_move(&b, "mD", "mmD"));

  {
    nodes_row_t nodes[] = {
      {0, "",           "normal",       1, ""},

      /* Move of a deleted + committed node */
      {0, "A",          "normal",       1, "A"},
      {0, "A/B",        "not-present",  2, "A/B"},
      {1, "A",          "base-deleted", NO_COPY_FROM, "mmA"},

      {1, "mmA",        "normal",       1, "A", MOVED_HERE},
      {1, "mmA/B",      "not-present",  2, "A/B", MOVED_HERE},


      /* Move of a local deleted node */
      {0, "B",          "normal",       1, "B"},
      {0, "B/B",        "normal",       1, "B/B"},
      {1, "B",          "base-deleted", NO_COPY_FROM, "mmB"},
      {1, "B/B",        "base-deleted", NO_COPY_FROM},

      {1, "mmB",        "normal",       1, "B", MOVED_HERE},
      {1, "mmB/B",      "normal",       1, "B/B", MOVED_HERE},
      {2, "mmB/B",      "base-deleted", NO_COPY_FROM},

      /* Move of a to r0 updated node */
      {0, "C",          "normal",       1, "C"},
      {0, "C/B",        "not-present",  0, "C/B"},
      {1, "C",          "base-deleted", NO_COPY_FROM, "mmC"},

      {1, "mmC",        "normal",       1, "C", MOVED_HERE},
      {1, "mmC/B",      "not-present",  0, "C/B", MOVED_HERE},

      /* Move of an excluded node */
      {0, "D",          "normal",       1, "D",},
      {0, "D/B",        "excluded",     1, "D/B", },
      {1, "D",          "base-deleted", NO_COPY_FROM, "mmD"},

      {1, "mmD",        "normal",       1, "D", MOVED_HERE},
      {1, "mmD/B",      "excluded",     1, "D/B", MOVED_HERE},

      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* And move everything back */
  SVN_ERR(sbox_wc_move(&b, "mmA", "A"));
  SVN_ERR(sbox_wc_move(&b, "mmB", "B"));
  SVN_ERR(sbox_wc_move(&b, "mmC", "C"));
  SVN_ERR(sbox_wc_move(&b, "mmD", "D"));

  {
    nodes_row_t nodes[] = {
      {0, "",           "normal",       1, ""},

      /* deleted + committed node */
      {0, "A",          "normal",       1, "A"},
      {0, "A/B",        "not-present",  2, "A/B"},

      /* local deleted node */
      {0, "B",          "normal",       1, "B"},
      {0, "B/B",        "normal",       1, "B/B"},
      {2, "B/B",        "base-deleted", NO_COPY_FROM},

      /* To r0 updated node */
      {0, "C",          "normal",       1, "C"},
      {0, "C/B",        "not-present",  0, "C/B"},

      /* Move of an excluded node */
      {0, "D",          "normal",       1, "D",},
      {0, "D/B",        "excluded",     1, "D/B", },

      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
update_child_under_add(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  svn_error_t *err;
  nodes_row_t nodes[] = {
    {0, "",        "normal",      1, ""},
    {0, "A",       "normal",      1, "A"},
    {0, "A/B",     "not-present", 0, "A/B"},
    {2, "A/B",     "normal",      NO_COPY_FROM},
    {3, "A/B/C",   "normal",      NO_COPY_FROM},
    {4, "A/B/C/D", "normal",      NO_COPY_FROM},
    {0}
  };

  SVN_ERR(svn_test__sandbox_create(&b, "update_child_under_add",
                                   opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/D"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_update(&b, "A/B", 0));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/D"));
  SVN_ERR(check_db_rows(&b, "", nodes));

  /* A/B/C/D is skipped as it has no base node parent */
  SVN_ERR(sbox_wc_update(&b, "A/B/C/D", 1));
  SVN_ERR(check_db_rows(&b, "", nodes));

  /* A/B/C should be skipped as it has a not-present base node parent */
  err = sbox_wc_update(&b, "A/B/C", 1);
  svn_error_clear(err); /* Allow any error and always check NODES. */
  SVN_ERR(check_db_rows(&b, "", nodes));

  return SVN_NO_ERROR;
}

static svn_error_t *
delete_over_moved_away(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "delete_over_moved_away",
                                   opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));

  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A/B", "B"));
  SVN_ERR(sbox_wc_delete(&b, "A"));

  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",         1, ""},

      {0, "A",      "normal",         1, "A"},
      {1, "A",      "base-deleted",   NO_COPY_FROM},
      {0, "A/B",    "normal",         1, "A/B"},
      {1, "A/B",    "base-deleted",   NO_COPY_FROM, "B"},
      {0, "A/B/C",  "normal",         1, "A/B/C"},
      {1, "A/B/C",  "base-deleted",   NO_COPY_FROM},

      {1, "B",      "normal",         1, "A/B", MOVED_HERE},
      {1, "B/C",    "normal",         1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Now replace A with a similar tree */
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));

  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",         1, ""},

      {0, "A",      "normal",         1, "A"},
      {1, "A",      "normal",         NO_COPY_FROM},
      {0, "A/B",    "normal",         1, "A/B"},
      {1, "A/B",    "base-deleted",   NO_COPY_FROM, "B"},
      {2, "A/B",    "normal",         NO_COPY_FROM},
      {0, "A/B/C",  "normal",         1, "A/B/C"},
      {1, "A/B/C",  "base-deleted",   NO_COPY_FROM},
      {3, "A/B/C",  "normal",         NO_COPY_FROM},

      {1, "B",      "normal",         1, "A/B", MOVED_HERE},
      {1, "B/C",    "normal",         1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* And delete the new A */
  SVN_ERR(sbox_wc_delete(&b, "A"));

  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",         1, ""},

      {0, "A",      "normal",         1, "A"},
      {1, "A",      "base-deleted",   NO_COPY_FROM},
      {0, "A/B",    "normal",         1, "A/B"},
      /* And here the moved-to information is lost */
      {1, "A/B",    "base-deleted",   NO_COPY_FROM, "B"},
      {0, "A/B/C",  "normal",         1, "A/B/C"},
      {1, "A/B/C",  "base-deleted",   NO_COPY_FROM},

      /* But the moved-here is still there */
      {1, "B",      "normal",         1, "A/B", MOVED_HERE},
      {1, "B/C",    "normal",         1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
movedto_opdepth(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "movedto_opdepth",
                                   opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A/B/C", "C"));

  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",         1, ""},

      {0, "A",      "normal",         1, "A"},
      {0, "A/B",    "normal",         1, "A/B"},
      {0, "A/B/C",  "normal",         1, "A/B/C"},
      {3, "A/B/C",  "base-deleted",   NO_COPY_FROM, "C"},

      {1, "C",      "normal",         1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* And now the moved_to information has to switch op-depths */
  SVN_ERR(sbox_wc_delete(&b, "A/B"));
  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",         1, ""},

      {0, "A",      "normal",         1, "A"},
      {0, "A/B",    "normal",         1, "A/B"},
      {0, "A/B/C",  "normal",         1, "A/B/C"},

      {2, "A/B",    "base-deleted",   NO_COPY_FROM},
      {2, "A/B/C",  "base-deleted",   NO_COPY_FROM, "C"},

      {1, "C",      "normal",         1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* And again */
  SVN_ERR(sbox_wc_delete(&b, "A"));
  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",         1, ""},

      {0, "A",      "normal",         1, "A"},
      {0, "A/B",    "normal",         1, "A/B"},
      {0, "A/B/C",  "normal",         1, "A/B/C"},

      {1, "A",      "base-deleted",   NO_COPY_FROM},
      {1, "A/B",    "base-deleted",   NO_COPY_FROM},
      {1, "A/B/C",  "base-deleted",   NO_COPY_FROM, "C"},

      {1, "C",      "normal",         1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* And now stay at the depth of A */
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));

  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",         1, ""},

      {0, "A",      "normal",         1, "A"},
      {0, "A/B",    "normal",         1, "A/B"},
      {0, "A/B/C",  "normal",         1, "A/B/C"},

      {1, "A",      "normal",         NO_COPY_FROM},
      {1, "A/B",    "base-deleted",   NO_COPY_FROM},
      {1, "A/B/C",  "base-deleted",   NO_COPY_FROM, "C"},

      {2, "A/B",    "normal",         NO_COPY_FROM},

      {1, "C",      "normal",         1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* And see if it can jump back to B again? */
  SVN_ERR(sbox_wc_delete(&b, "A"));
  SVN_ERR(sbox_wc_revert(&b, "A", svn_depth_empty));

  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",         1, ""},

      {0, "A",      "normal",         1, "A"},
      {0, "A/B",    "normal",         1, "A/B"},
      {0, "A/B/C",  "normal",         1, "A/B/C"},

      {2, "A/B",    "base-deleted",   NO_COPY_FROM},
      {2, "A/B/C",  "base-deleted",   NO_COPY_FROM, "C"},

      {1, "C",      "normal",         1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* And can we bump it back to C itself? */
  SVN_ERR(sbox_wc_revert(&b, "A", svn_depth_immediates));
  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",         1, ""},

      {0, "A",      "normal",         1, "A"},
      {0, "A/B",    "normal",         1, "A/B"},
      {0, "A/B/C",  "normal",         1, "A/B/C"},

      {3, "A/B/C",  "base-deleted",   NO_COPY_FROM, "C"},

      {1, "C",      "normal",         1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
new_basemove(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "new_basemove",
                                   opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  /* We keep track of moved children of copies */
  SVN_ERR(sbox_wc_copy(&b, "A", "Copy"));
  SVN_ERR(sbox_wc_move(&b, "Copy/B/C", "C"));

  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",         1, ""},

      {0, "A",        "normal",         1, "A"},
      {0, "A/B",      "normal",         1, "A/B"},
      {0, "A/B/C",    "normal",         1, "A/B/C"},

      {1, "Copy",     "normal",         1, "A"},
      {1, "Copy/B",   "normal",         1, "A/B"},
      {1, "Copy/B/C", "normal",         1, "A/B/C"},

      {3, "Copy/B/C", "base-deleted",   NO_COPY_FROM, "C"},

      /* C is a copy of A/B/C */
      {1, "C",        "normal",         1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  {
    apr_array_header_t *targets = apr_array_make(pool, 1, sizeof(char *));
    APR_ARRAY_PUSH(targets, const char*) = sbox_wc_path(&b, "Copy");

    SVN_ERR(sbox_wc_commit_ex(&b, targets, svn_depth_empty));
  }

  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",         1, ""},

      {0, "A",        "normal",         1, "A"},
      {0, "A/B",      "normal",         1, "A/B"},
      {0, "A/B/C",    "normal",         1, "A/B/C"},

      {0, "Copy",     "normal",         2, "Copy"},
      {0, "Copy/B",   "normal",         2, "Copy/B"},
      {0, "Copy/B/C", "normal",         2, "Copy/B/C"},

      {3, "Copy/B/C", "base-deleted",   NO_COPY_FROM, "C"},

      /* And this node is now a copy of Copy/B/C at r2 */
      {1, "C",        "normal",         2, "Copy/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_back(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_back", opts, pool));

  /* X just so we don't always test with local_relpath == repos_path */
  SVN_ERR(sbox_wc_mkdir(&b, "X"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/A/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/A/B/D"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/E"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_switch(&b, "", "/X", svn_depth_infinity));

  SVN_ERR(sbox_wc_move(&b, "A/B", "A/B2"));
  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",       1, "X"},
      {0, "A",      "normal",       1, "X/A"},
      {0, "A/B",    "normal",       1, "X/A/B"},
      {0, "A/B/C",  "normal",       1, "X/A/B/C"},
      {0, "A/B/D",  "normal",       1, "X/A/B/D"},
      {0, "E",      "normal",       1, "X/E"},
      {2, "A/B",    "base-deleted", NO_COPY_FROM, "A/B2"},
      {2, "A/B/C",  "base-deleted", NO_COPY_FROM},
      {2, "A/B/D",  "base-deleted", NO_COPY_FROM},
      {2, "A/B2",   "normal",       1, "X/A/B", MOVED_HERE},
      {2, "A/B2/C", "normal",       1, "X/A/B/C", MOVED_HERE},
      {2, "A/B2/D", "normal",       1, "X/A/B/D", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  SVN_ERR(sbox_wc_move(&b, "A/B2", "A/B"));
  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",       1, "X"},
      {0, "A",      "normal",       1, "X/A"},
      {0, "A/B",    "normal",       1, "X/A/B"},
      {0, "A/B/C",  "normal",       1, "X/A/B/C"},
      {0, "A/B/D",  "normal",       1, "X/A/B/D"},
      {0, "E",      "normal",       1, "X/E"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_move(&b, "A/B", "A/B2"));
  SVN_ERR(sbox_wc_move(&b, "A/B2/C", "A/B2/C2"));
  SVN_ERR(sbox_wc_move(&b, "A/B2/D", "D2"));
  SVN_ERR(sbox_wc_move(&b, "E", "A/B2/E2"));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, "X"},
      {0, "A",       "normal",       1, "X/A"},
      {0, "A/B",     "normal",       1, "X/A/B"},
      {0, "A/B/C",   "normal",       1, "X/A/B/C"},
      {0, "A/B/D",   "normal",       1, "X/A/B/D"},
      {0, "E",       "normal",       1, "X/E"},
      {1, "D2",      "normal",       1, "X/A/B/D", MOVED_HERE},
      {1, "E",       "base-deleted", NO_COPY_FROM, "A/B2/E2"},
      {2, "A/B",     "base-deleted", NO_COPY_FROM, "A/B2"},
      {2, "A/B/C",   "base-deleted", NO_COPY_FROM},
      {2, "A/B/D",   "base-deleted", NO_COPY_FROM},
      {2, "A/B2",    "normal",       1, "X/A/B", MOVED_HERE},
      {2, "A/B2/C",  "normal",       1, "X/A/B/C", MOVED_HERE},
      {2, "A/B2/D",  "normal",       1, "X/A/B/D", MOVED_HERE},
      {3, "A/B2/C",  "base-deleted", NO_COPY_FROM, "A/B2/C2"},
      {3, "A/B2/D",  "base-deleted", NO_COPY_FROM, "D2"},
      {3, "A/B2/C2", "normal",       1, "X/A/B/C", MOVED_HERE},
      {3, "A/B2/E2", "normal",       1, "X/E", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  SVN_ERR(sbox_wc_move(&b, "A/B2", "A/B"));
  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",       1, "X"},
      {0, "A",      "normal",       1, "X/A"},
      {0, "A/B",    "normal",       1, "X/A/B"},
      {0, "A/B/C",  "normal",       1, "X/A/B/C"},
      {0, "A/B/D",  "normal",       1, "X/A/B/D"},
      {0, "E",      "normal",       1, "X/E"},
      {1, "D2",     "normal",       1, "X/A/B/D", MOVED_HERE},
      {1, "E",      "base-deleted", NO_COPY_FROM, "A/B/E2"},
      {3, "A/B/C",  "base-deleted", NO_COPY_FROM, "A/B/C2"},
      {3, "A/B/D",  "base-deleted", NO_COPY_FROM, "D2"},
      {3, "A/B/C2", "normal",       1, "X/A/B/C", MOVED_HERE},
      {3, "A/B/E2", "normal",       1, "X/E", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_update_subtree(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  svn_error_t *err;

  SVN_ERR(svn_test__sandbox_create(&b, "move_update_subtree", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/D"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "X"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 3));
  SVN_ERR(sbox_wc_copy(&b, "A", "P"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  /* Subtree update is like an interrupted update, it leaves a
     mixed-revision move source. */
  SVN_ERR(sbox_wc_move(&b, "A/B", "A/B2"));
  SVN_ERR(sbox_wc_update(&b, "A/B/C", 2));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "A/B/C",   "normal",       2, "A/B/C"},
      {0, "A/B/C/D", "normal",       2, "A/B/C/D"},
      {2, "A/B",     "base-deleted", NO_COPY_FROM, "A/B2"},
      {2, "A/B/C",   "base-deleted", NO_COPY_FROM},
      {2, "A/B/C/D", "base-deleted", NO_COPY_FROM},
      {2, "A/B2",    "normal",       1, "A/B", MOVED_HERE},
      {2, "A/B2/C",  "normal",       1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Resolve fails because of the mixed-revision. */
  err = sbox_wc_resolve(&b, "A/B", svn_depth_empty,
                        svn_wc_conflict_choose_mine_conflict);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE);

  /* Update to single-revision to allow resolve; this requires update
     while the tree-conflict on A/B is present. */
  SVN_ERR(sbox_wc_update(&b, "A/B", 2));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       2, "A/B"},
      {0, "A/B/C",   "normal",       2, "A/B/C"},
      {0, "A/B/C/D", "normal",       2, "A/B/C/D"},
      {2, "A/B",     "base-deleted", NO_COPY_FROM, "A/B2"},
      {2, "A/B/C",   "base-deleted", NO_COPY_FROM},
      {2, "A/B/C/D", "base-deleted", NO_COPY_FROM},
      {2, "A/B2",    "normal",       1, "A/B", MOVED_HERE},
      {2, "A/B2/C",  "normal",       1, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Now resolve is possible. */
  SVN_ERR(sbox_wc_resolve(&b, "A/B", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       1, "A"},
      {0, "A/B",      "normal",       2, "A/B"},
      {0, "A/B/C",    "normal",       2, "A/B/C"},
      {0, "A/B/C/D",  "normal",       2, "A/B/C/D"},
      {2, "A/B",      "base-deleted", NO_COPY_FROM, "A/B2"},
      {2, "A/B/C",    "base-deleted", NO_COPY_FROM},
      {2, "A/B/C/D",  "base-deleted", NO_COPY_FROM},
      {2, "A/B2",     "normal",       2, "A/B", MOVED_HERE},
      {2, "A/B2/C",   "normal",       2, "A/B/C", MOVED_HERE},
      {2, "A/B2/C/D", "normal",       2, "A/B/C/D", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Subtree update that only bumps. */
  SVN_ERR(sbox_wc_update(&b, "A/B/C", 3));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       1, "A"},
      {0, "A/B",      "normal",       2, "A/B"},
      {0, "A/B/C",    "normal",       3, "A/B/C"},
      {0, "A/B/C/D",  "normal",       3, "A/B/C/D"},
      {2, "A/B",      "base-deleted", NO_COPY_FROM, "A/B2"},
      {2, "A/B/C",    "base-deleted", NO_COPY_FROM},
      {2, "A/B/C/D",  "base-deleted", NO_COPY_FROM},
      {2, "A/B2",     "normal",       2, "A/B", MOVED_HERE},
      {2, "A/B2/C",   "normal",       2, "A/B/C", MOVED_HERE},
      {2, "A/B2/C/D", "normal",       2, "A/B/C/D", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Resolve fails because of the mixed-revision. */
  err = sbox_wc_resolve(&b, "A/B", svn_depth_empty,
                        svn_wc_conflict_choose_mine_conflict);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE);

  /* Update allowed while tree-conflict is present. */
  SVN_ERR(sbox_wc_update(&b, "A/B", 3));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       1, "A"},
      {0, "A/B",      "normal",       3, "A/B"},
      {0, "A/B/C",    "normal",       3, "A/B/C"},
      {0, "A/B/C/D",  "normal",       3, "A/B/C/D"},
      {2, "A/B",      "base-deleted", NO_COPY_FROM, "A/B2"},
      {2, "A/B/C",    "base-deleted", NO_COPY_FROM},
      {2, "A/B/C/D",  "base-deleted", NO_COPY_FROM},
      {2, "A/B2",     "normal",       2, "A/B", MOVED_HERE},
      {2, "A/B2/C",   "normal",       2, "A/B/C", MOVED_HERE},
      {2, "A/B2/C/D", "normal",       2, "A/B/C/D", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Now resolve works. */
  SVN_ERR(sbox_wc_resolve(&b, "A/B", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       1, "A"},
      {0, "A/B",      "normal",       3, "A/B"},
      {0, "A/B/C",    "normal",       3, "A/B/C"},
      {0, "A/B/C/D",  "normal",       3, "A/B/C/D"},
      {2, "A/B",      "base-deleted", NO_COPY_FROM, "A/B2"},
      {2, "A/B/C",    "base-deleted", NO_COPY_FROM},
      {2, "A/B/C/D",  "base-deleted", NO_COPY_FROM},
      {2, "A/B2",     "normal",       3, "A/B", MOVED_HERE},
      {2, "A/B2/C",   "normal",       3, "A/B/C", MOVED_HERE},
      {2, "A/B2/C/D", "normal",       3, "A/B/C/D", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Partial switch of source. */
  SVN_ERR(sbox_wc_switch(&b, "A", "/P", svn_depth_immediates));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       4, "P"},
      {0, "A/B",      "normal",       4, "P/B"},
      {0, "A/B/C",    "normal",       3, "A/B/C"},
      {0, "A/B/C/D",  "normal",       3, "A/B/C/D"},
      {2, "A/B",      "base-deleted", NO_COPY_FROM, "A/B2"},
      {2, "A/B/C",    "base-deleted", NO_COPY_FROM},
      {2, "A/B/C/D",  "base-deleted", NO_COPY_FROM},
      {2, "A/B2",     "normal",       3, "A/B", MOVED_HERE},
      {2, "A/B2/C",   "normal",       3, "A/B/C", MOVED_HERE},
      {2, "A/B2/C/D", "normal",       3, "A/B/C/D", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Resolve fails because of the subtree-switch. */
  err = sbox_wc_resolve(&b, "A/B", svn_depth_empty,
                        svn_wc_conflict_choose_mine_conflict);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE);

  /* Switch works while tree-conflict is present. */
  SVN_ERR(sbox_wc_switch(&b, "A", "/P", svn_depth_infinity));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       4, "P"},
      {0, "A/B",      "normal",       4, "P/B"},
      {0, "A/B/C",    "normal",       4, "P/B/C"},
      {0, "A/B/C/D",  "normal",       4, "P/B/C/D"},
      {2, "A/B",      "base-deleted", NO_COPY_FROM, "A/B2"},
      {2, "A/B/C",    "base-deleted", NO_COPY_FROM},
      {2, "A/B/C/D",  "base-deleted", NO_COPY_FROM},
      {2, "A/B2",     "normal",       3, "A/B", MOVED_HERE},
      {2, "A/B2/C",   "normal",       3, "A/B/C", MOVED_HERE},
      {2, "A/B2/C/D", "normal",       3, "A/B/C/D", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Now resolve works. */
  SVN_ERR(sbox_wc_resolve(&b, "A/B", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       4, "P"},
      {0, "A/B",      "normal",       4, "P/B"},
      {0, "A/B/C",    "normal",       4, "P/B/C"},
      {0, "A/B/C/D",  "normal",       4, "P/B/C/D"},
      {2, "A/B",      "base-deleted", NO_COPY_FROM, "A/B2"},
      {2, "A/B/C",    "base-deleted", NO_COPY_FROM},
      {2, "A/B/C/D",  "base-deleted", NO_COPY_FROM},
      {2, "A/B2",     "normal",       4, "P/B", MOVED_HERE},
      {2, "A/B2/C",   "normal",       4, "P/B/C", MOVED_HERE},
      {2, "A/B2/C/D", "normal",       4, "P/B/C/D", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_parent_into_child(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_parent_into_child", opts, pool));
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A/B", "B2"));
  SVN_ERR(sbox_wc_move(&b, "A", "B2/A"));
  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",       1, ""},
      {0, "A",      "normal",       1, "A"},
      {0, "A/B",    "normal",       1, "A/B"},
      {1, "A",      "base-deleted", NO_COPY_FROM, "B2/A"},
      {1, "A/B",    "base-deleted", NO_COPY_FROM},
      {1, "B2",     "normal",       1, "A/B", MOVED_HERE},
      {2, "B2/A",   "normal",       1, "A", MOVED_HERE},
      {2, "B2/A/B", "normal",       1, "A/B", MOVED_HERE},
      {3, "B2/A/B", "base-deleted", NO_COPY_FROM, "B2"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_move(&b, "B2", "A"));
  {
    nodes_row_t nodes[] = {
      {0, "",      "normal",       1, ""},
      {0, "A",     "normal",       1, "A"},
      {0, "A/B",   "normal",       1, "A/B"},
      {1, "A",     "normal",       1, "A/B", FALSE, "A/A", TRUE},
      {1, "A/B",   "base-deleted", NO_COPY_FROM},
      {2, "A/A",   "normal",       1, "A", MOVED_HERE},
      {2, "A/A/B", "normal",       1, "A/B", MOVED_HERE},
      {3, "A/A/B", "base-deleted", NO_COPY_FROM, "A"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_move(&b, "A/A", "A/B"));
  {
    nodes_row_t nodes[] = {
      {0, "",      "normal",       1, ""},
      {0, "A",     "normal",       1, "A"},
      {0, "A/B",   "normal",       1, "A/B"},
      {1, "A",     "normal",       1, "A/B", FALSE, "A/B", TRUE},
      {1, "A/B",   "base-deleted", NO_COPY_FROM},
      {2, "A/B",   "normal",       1, "A", MOVED_HERE},
      {2, "A/B/B", "normal",       1, "A/B", MOVED_HERE},
      {3, "A/B/B", "base-deleted", NO_COPY_FROM, "A"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_depth_expand(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_depth_expand", opts, pool));
  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/A/A"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 0));

  SVN_ERR(sbox_wc_update_depth(&b, "", 1, svn_depth_immediates, TRUE));
  SVN_ERR(sbox_wc_update_depth(&b, "A", 1, svn_depth_immediates, TRUE));
  /* Make A/B not present */
  SVN_ERR(sbox_wc_update_depth(&b, "A/B", 0, svn_depth_immediates, TRUE));

  SVN_ERR(sbox_wc_move(&b, "A", "C"));
  SVN_ERR(sbox_wc_mkdir(&b, "C/A/A")); /* Local addition obstruction */
  SVN_ERR(sbox_wc_copy(&b, "C/A", "C/B")); /* Copied obstruction */

  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",       1, "" },

      {0, "A",      "normal",       1, "A" },
      {1, "A",      "base-deleted", NO_COPY_FROM, "C" },
      {0, "A/A",    "normal",       1, "A/A" },
      {1, "A/A",    "base-deleted", NO_COPY_FROM },
      {0, "A/B",    "not-present",  0, "A/B" },

      {1, "C",      "normal",       1, "A", MOVED_HERE },

      {1, "C/A",    "normal",       1, "A/A", MOVED_HERE },
      {3, "C/A/A",  "normal",       NO_COPY_FROM },

      {1, "C/B",    "not-present",  0, "A/B", MOVED_HERE},
      {2, "C/B",    "normal",       1, "A/A" },
      {3, "C/B/A",  "normal",       NO_COPY_FROM },
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update_depth(&b, "", 1, svn_depth_infinity, TRUE));

  /* And now verify that there are no not-present nodes left and a
     consistent working copy */
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, "" },

      {0, "A",       "normal",       1, "A" },
      {0, "A/A",     "normal",       1, "A/A" },
      {0, "A/A/A",   "normal",       1, "A/A/A" },
      {0, "A/A/A/A", "normal",       1, "A/A/A/A" },
      {0, "A/B",     "normal",       1, "A/B" },
      {0, "A/B/A",   "normal",       1, "A/B/A" },
      {0, "A/B/A/A", "normal",       1, "A/B/A/A" },

      {1, "A",       "base-deleted", NO_COPY_FROM, "C" },
      {1, "A/A",     "base-deleted", NO_COPY_FROM },
      {1, "A/A/A",   "base-deleted", NO_COPY_FROM },
      {1, "A/B",     "base-deleted", NO_COPY_FROM },
      {1, "A/B/A",   "base-deleted", NO_COPY_FROM },
      {1, "A/B/A/A", "base-deleted", NO_COPY_FROM },
      {1, "A/A/A/A", "base-deleted", NO_COPY_FROM },

      {1, "C",       "normal",       1, "A", MOVED_HERE },
      {1, "C/A",     "normal",       1, "A/A", MOVED_HERE },
      {1, "C/B",     "not-present",  0, "A/B", MOVED_HERE},

      {2, "C/B",     "normal",       1, "A/A" },

      {3, "C/A/A",   "normal",       NO_COPY_FROM },
      {3, "C/B/A",   "normal",       NO_COPY_FROM },

      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* This used to cause a segfault. Then it asserted in a different place */
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));

  /* And now verify that there are no not-present nodes left and a
     consistent working copy */
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, "" },

      {0, "A",       "normal",       1, "A" },
      {0, "A/A",     "normal",       1, "A/A" },
      {0, "A/A/A",   "normal",       1, "A/A/A" },
      {0, "A/A/A/A", "normal",       1, "A/A/A/A" },
      {0, "A/B",     "normal",       1, "A/B" },
      {0, "A/B/A",   "normal",       1, "A/B/A" },
      {0, "A/B/A/A", "normal",       1, "A/B/A/A" },

      {1, "A",       "base-deleted", NO_COPY_FROM, "C" },
      {1, "A/A",     "base-deleted", NO_COPY_FROM },
      {1, "A/A/A",   "base-deleted", NO_COPY_FROM },
      {1, "A/B",     "base-deleted", NO_COPY_FROM },
      {1, "A/B/A",   "base-deleted", NO_COPY_FROM },
      {1, "A/B/A/A", "base-deleted", NO_COPY_FROM },
      {1, "A/A/A/A", "base-deleted", NO_COPY_FROM },


      {1, "C",       "normal",       1, "A", MOVED_HERE },
      {1, "C/A",     "normal",       1, "A/A", MOVED_HERE },
      {1, "C/A/A",   "normal",       1, "A/A/A", MOVED_HERE },
      {1, "C/A/A/A", "normal",       1, "A/A/A/A", MOVED_HERE },
      {1, "C/B",     "normal",       1, "A/B", MOVED_HERE },
      {1, "C/B/A",   "normal",       1, "A/B/A", MOVED_HERE },
      {1, "C/B/A/A", "normal",       1, "A/B/A/A", MOVED_HERE },

      {2, "C/B",     "normal",       1, "A/A" },
      {2, "C/B/A",   "base-deleted", NO_COPY_FROM },
      {2, "C/B/A/A", "base-deleted", NO_COPY_FROM },

      {3, "C/A/A",   "normal",       NO_COPY_FROM },
      {3, "C/A/A/A", "base-deleted", NO_COPY_FROM },
      {3, "C/B/A",   "normal",       NO_COPY_FROM },

      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_retract(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_retract", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A/D"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/A/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/A/D"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_delete(&b, "A/A/A/A"));
  SVN_ERR(sbox_wc_delete(&b, "A/A/A/C"));
  SVN_ERR(sbox_wc_delete(&b, "A/A/A/D"));
  SVN_ERR(sbox_wc_delete(&b, "A/B/A/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_move(&b, "A/B/A/D", "D"));
  SVN_ERR(sbox_wc_delete(&b, "A/B"));
  SVN_ERR(sbox_wc_move(&b, "A/A", "A/B"));

  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, "" },

      {0, "A",       "normal",       1, "A" },
      {0, "A/A",     "normal",       1, "A/A" },
      {0, "A/A/A",   "normal",       1, "A/A/A" },
      {0, "A/A/A/A", "normal",       1, "A/A/A/A" },
      {0, "A/A/A/C", "normal",       1, "A/A/A/C" },
      {0, "A/A/A/D", "normal",       1, "A/A/A/D" },

      {2, "A/A",     "base-deleted", NO_COPY_FROM, "A/B"},
      {2, "A/A/A",   "base-deleted", NO_COPY_FROM },
      {2, "A/A/A/A", "base-deleted", NO_COPY_FROM },
      {2, "A/A/A/C", "base-deleted", NO_COPY_FROM },
      {2, "A/A/A/D", "base-deleted", NO_COPY_FROM },

      {0, "A/B",     "normal",       1, "A/B" },
      {0, "A/B/A",   "normal",       1, "A/B/A" },
      {0, "A/B/A/A", "normal",       1, "A/B/A/A" },
      {0, "A/B/A/C", "normal",       1, "A/B/A/C" },
      {0, "A/B/A/D", "normal",       1, "A/B/A/D" },

      {2, "A/B",     "normal",       1, "A/A", MOVED_HERE },
      {2, "A/B/A",   "normal",       1, "A/A/A", MOVED_HERE },
      {2, "A/B/A/A", "normal",       1, "A/A/A/A", MOVED_HERE },
      {2, "A/B/A/C", "normal",       1, "A/A/A/C", MOVED_HERE },
      {2, "A/B/A/D", "normal",       1, "A/A/A/D", FALSE, "D", TRUE },

      {1, "D",       "normal",       1, "A/B/A/D", MOVED_HERE },

      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "", 2));

  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       2, "" },

      {0, "A",       "normal",       2, "A" },
      {0, "A/A",     "normal",       2, "A/A" },
      {0, "A/A/A",   "normal",       2, "A/A/A" },

      {2, "A/A",     "base-deleted", NO_COPY_FROM, "A/B"},
      {2, "A/A/A",   "base-deleted", NO_COPY_FROM },

      {0, "A/B",     "normal",       2, "A/B" },
      {0, "A/B/A",   "normal",       2, "A/B/A" },
      {0, "A/B/A/A", "normal",       2, "A/B/A/A" },
      {0, "A/B/A/D", "normal",       2, "A/B/A/D" },

      {2, "A/B",     "normal",       1, "A/A", MOVED_HERE },
      {2, "A/B/A",   "normal",       1, "A/A/A", MOVED_HERE },
      {2, "A/B/A/A", "normal",       1, "A/A/A/A", MOVED_HERE },
      {2, "A/B/A/C", "normal",       1, "A/A/A/C", MOVED_HERE },
      {2, "A/B/A/D", "normal",       1, "A/A/A/D", FALSE, "D", TRUE },

      {1, "D",       "normal",       1, "A/B/A/D", MOVED_HERE },

      {0}
    };
    conflict_info_t conflicts[] = {
      {"A/A", FALSE, FALSE, {svn_wc_conflict_action_edit,
                             svn_wc_conflict_reason_moved_away, "A/A"}},
      {"A/B", FALSE, FALSE, {svn_wc_conflict_action_edit,
                             svn_wc_conflict_reason_replaced}},
      { 0 },
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }


  SVN_ERR(sbox_wc_resolve(&b, "A/A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));

  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       2, "" },

      {0, "A",       "normal",       2, "A" },
      {0, "A/A",     "normal",       2, "A/A" },
      {0, "A/A/A",   "normal",       2, "A/A/A" },

      {0, "A/B",     "normal",       2, "A/B" },
      {0, "A/B/A",   "normal",       2, "A/B/A" },
      {0, "A/B/A/A", "normal",       2, "A/B/A/A" },
      {0, "A/B/A/D", "normal",       2, "A/B/A/D" },

      {2, "A/A",     "base-deleted", NO_COPY_FROM, "A/B"},
      {2, "A/A/A",   "base-deleted", NO_COPY_FROM },

      {2, "A/B",     "normal",       2, "A/A", MOVED_HERE },
      {2, "A/B/A",   "normal",       2, "A/A/A", MOVED_HERE },
      {2, "A/B/A/A", "base-deleted", NO_COPY_FROM }, /* ### MISSING! */
      {2, "A/B/A/D", "base-deleted", NO_COPY_FROM, "D" }, /* ### MISSING! */

      /* Still conflicted */
      {1, "D",       "normal",       1, "A/B/A/D", MOVED_HERE },

      {4, "A/B/A/C", "normal",       1, "A/A/A/C"},


      {0}
    };
    conflict_info_t conflicts[] = {
      {"A/B",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                                 svn_wc_conflict_reason_replaced}},
      {"A/B/A/C", FALSE, FALSE, {svn_wc_conflict_action_delete,
                                 svn_wc_conflict_reason_edited}},
      {0}
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }

  /* ### TODO: Resolve via which specific target? */
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity,
                          svn_wc_conflict_choose_mine_conflict));

  {
    nodes_row_t nodes[] = {

      {1, "D",       "normal",       1, "A/B/A/D", MOVED_HERE },

      {0}
    };
    SVN_ERR(check_db_rows(&b, "D", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_delete_file_externals(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_delete_file_externals", opts,
                                   pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_file_write(&b, "f", "New file"));
  SVN_ERR(sbox_wc_add(&b, "f"));
  SVN_ERR(sbox_wc_propset(&b, "svn:externals", "^/f B/P/g", "A"));
  SVN_ERR(sbox_wc_propset(&b, "svn:externals", "^/f Q/g\n^/f g", "A/B"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "f",       "normal",       1, "f"},
      {0, "A/B/g",   "normal",       1, "f", TRUE},
      {0, "A/B/P/g", "normal",       1, "f", TRUE},
      {0, "A/B/Q/g", "normal",       1, "f", TRUE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Delete removes the file external rows. */
  SVN_ERR(sbox_wc_delete(&b, "A"));
  {
    nodes_row_t nodes[] = {
      {0, "",    "normal",       1, ""},
      {0, "A",   "normal",       1, "A"},
      {0, "A/B", "normal",       1, "A/B"},
      {0, "f",   "normal",       1, "f"},
      {1, "A",   "base-deleted", NO_COPY_FROM},
      {1, "A/B", "base-deleted", NO_COPY_FROM},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Revert doesn't restore the file external rows... */
  SVN_ERR(sbox_wc_revert(&b, "A", svn_depth_infinity));
  {
    nodes_row_t nodes[] = {
      {0, "",    "normal",       1, ""},
      {0, "A",   "normal",       1, "A"},
      {0, "A/B", "normal",       1, "A/B"},
      {0, "f",   "normal",       1, "f"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  /* ... but update does. */
  SVN_ERR(sbox_wc_update(&b, "", 1));
  {
    nodes_row_t nodes[] = {
      {0, "",        "normal",       1, ""},
      {0, "A",       "normal",       1, "A"},
      {0, "A/B",     "normal",       1, "A/B"},
      {0, "f",       "normal",       1, "f"},
      {0, "A/B/g",   "normal",       1, "f", TRUE},
      {0, "A/B/P/g", "normal",       1, "f", TRUE},
      {0, "A/B/Q/g", "normal",       1, "f", TRUE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Move removes the file external rows. */
  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  {
    nodes_row_t nodes[] = {
      {0, "",     "normal",       1, ""},
      {0, "A",    "normal",       1, "A"},
      {0, "A/B",  "normal",       1, "A/B"},
      {0, "f",    "normal",       1, "f"},
      {1, "A",    "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",  "base-deleted", NO_COPY_FROM},
      {1, "A2",   "normal",       1, "A", MOVED_HERE},
      {1, "A2/B", "normal",       1, "A/B", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  /* Update adds file external rows to the copy. */
  SVN_ERR(sbox_wc_update(&b, "", 1));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       1, ""},
      {0, "A",        "normal",       1, "A"},
      {0, "A/B",      "normal",       1, "A/B"},
      {0, "f",        "normal",       1, "f"},
      {1, "A",        "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",      "base-deleted", NO_COPY_FROM},
      {1, "A2",       "normal",       1, "A", MOVED_HERE},
      {1, "A2/B",     "normal",       1, "A/B", MOVED_HERE},
      {0, "A2/B/g",   "normal",       1, "f", TRUE},
      {0, "A2/B/P/g", "normal",       1, "f", TRUE},
      {0, "A2/B/Q/g", "normal",       1, "f", TRUE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
update_with_tree_conflict(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "update_with_tree_conflict", opts,
                                   pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_move(&b, "A", "A2"));

  SVN_ERR(sbox_wc_update_depth(&b, "A", 2, svn_depth_empty, FALSE));
  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",       1, ""},
      {0, "A",      "normal",       2, "A"},
      {0, "A/B",    "normal",       1, "A/B"},
      {1, "A",      "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",    "base-deleted", NO_COPY_FROM},
      {1, "A2",     "normal",       1, "A", MOVED_HERE},
      {1, "A2/B",   "normal",       1, "A/B", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "A", 2));
  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",       1, ""},
      {0, "A",      "normal",       2, "A"},
      {0, "A/B",    "normal",       2, "A/B"},
      {0, "A/B/C",  "normal",       2, "A/B/C"},
      {1, "A",      "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",  "base-deleted", NO_COPY_FROM},
      {1, "A2",     "normal",       1, "A", MOVED_HERE},
      {1, "A2/B",   "normal",       1, "A/B", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Second update overwrote the existing tree-conflict and that
     causes the move-update to assert. */
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_infinity,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",       1, ""},
      {0, "A",      "normal",       2, "A"},
      {0, "A/B",    "normal",       2, "A/B"},
      {0, "A/B/C",  "normal",       2, "A/B/C"},
      {1, "A",      "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",  "base-deleted", NO_COPY_FROM},
      {1, "A2",     "normal",       2, "A", MOVED_HERE},
      {1, "A2/B",   "normal",       2, "A/B", MOVED_HERE},
      {1, "A2/B/C", "normal",       2, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Same again but second update is A/B rather than A which triggers
     the problem through open_root rather than open_directory. */
  SVN_ERR(sbox_wc_revert(&b, "", svn_depth_infinity));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_move(&b, "A", "A2"));
  SVN_ERR(sbox_wc_update_depth(&b, "A", 2, svn_depth_empty, FALSE));
  SVN_ERR(sbox_wc_update(&b, "A/B", 2));
  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",       1, ""},
      {0, "A",      "normal",       2, "A"},
      {0, "A/B",    "normal",       2, "A/B"},
      {0, "A/B/C",  "normal",       2, "A/B/C"},
      {1, "A",      "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",  "base-deleted", NO_COPY_FROM},
      {1, "A2",     "normal",       1, "A", MOVED_HERE},
      {1, "A2/B",   "normal",       1, "A/B", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_infinity,
                          svn_wc_conflict_choose_mine_conflict));
  {
    nodes_row_t nodes[] = {
      {0, "",       "normal",       1, ""},
      {0, "A",      "normal",       2, "A"},
      {0, "A/B",    "normal",       2, "A/B"},
      {0, "A/B/C",  "normal",       2, "A/B/C"},
      {1, "A",      "base-deleted", NO_COPY_FROM, "A2"},
      {1, "A/B",    "base-deleted", NO_COPY_FROM},
      {1, "A/B/C",  "base-deleted", NO_COPY_FROM},
      {1, "A2",     "normal",       2, "A", MOVED_HERE},
      {1, "A2/B",   "normal",       2, "A/B", MOVED_HERE},
      {1, "A2/B/C", "normal",       2, "A/B/C", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_update_parent_replace(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_update_parent_replace", opts,
                                   pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_delete(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_move(&b, "A/B/C", "A/C"));

  /* Update breaks the move and leaves a conflict. */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       2, ""},
      {0, "A",        "normal",       2, "A"},
      {0, "A/B",      "normal",       2, "A/B"},

      {2, "A/C",      "normal",       1, "A/B/C", MOVED_HERE},

      {2, "A/B",      "normal",       1, "A/B"},
      {2, "A/B/C",    "normal",       1, "A/B/C", FALSE},

      {3, "A/B/C",    "base-deleted", NO_COPY_FROM, "A/C"},

      {0}
    };
    conflict_info_t conflicts[] = {
      {"A/B", FALSE, FALSE, {svn_wc_conflict_action_replace,
                             svn_wc_conflict_reason_edited}},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }

  SVN_ERR(sbox_wc_resolve(&b, "A/B", svn_depth_infinity,
                          svn_wc_conflict_choose_merged));

  {
    nodes_row_t nodes[] = {
      {0, "",         "normal",       2, ""},
      {0, "A",        "normal",       2, "A"},
      {0, "A/B",      "normal",       2, "A/B"},
      {2, "A/C",      "normal",       1, "A/B/C", MOVED_HERE},
      {2, "A/B",      "normal",       1, "A/B"},
      {2, "A/B/C",    "normal",       1, "A/B/C", FALSE},
      {3, "A/B/C",    "base-deleted", NO_COPY_FROM, "A/C"},

      {0}
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
    SVN_ERR(check_db_conflicts(&b, "", NULL));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_mixed_rev_mods(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "copy_mixed_rev_mods", opts,
                                   pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_update(&b, "A/B", 2));
  SVN_ERR(sbox_wc_delete(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));

  {
    nodes_row_t nodes[] = {
      {0, "",      "normal",       1, ""},
      {0, "A",     "normal",       1, "A"},
      {0, "A/B",   "normal",       2, "A/B"},
      {0, "A/B/C", "normal",       2, "A/B/C"},
      {2, "A/B",   "normal",       NO_COPY_FROM},
      {2, "A/B/C", "base-deleted", NO_COPY_FROM},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_copy(&b, "A", "X"));
  {
    nodes_row_t nodes[] = {
      {1, "X",   "normal",      1, "A"},
      {1, "X/B", "not-present", 2, "A/B"},
      {2, "X/B", "normal",      NO_COPY_FROM},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "X", nodes));
  }

  SVN_ERR(sbox_wc_commit(&b, "X"));
  {
    nodes_row_t nodes[] = {
      {0, "X",   "normal", 3, "X"},
      {0, "X/B", "normal", 3, "X/B"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "X", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_child_to_parent_revert(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_child_to_parent_revert", opts,
                                   pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_commit(&b, ""));


  SVN_ERR(sbox_wc_move(&b, "A/B", "B"));
  SVN_ERR(sbox_wc_delete(&b, "A"));

  /* Verify that the move is still recorded correctly */
  {
    nodes_row_t nodes[] = {
      {0, "",    "normal", 0, ""},
      {0, "A",   "normal", 1, "A"},
      {0, "A/B", "normal", 1, "A/B"},

      {1, "A",   "base-deleted", NO_COPY_FROM},
      {1, "A/B", "base-deleted", NO_COPY_FROM, "B"},

      {1, "B", "normal", 1, "A/B", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_revert(&b, "A", svn_depth_infinity));

  /* Verify that the move is now just a copy */
  {
    nodes_row_t nodes[] = {
      {0, "",    "normal", 0, ""},
      {0, "A",   "normal", 1, "A"},
      {0, "A/B", "normal", 1, "A/B"},

      {1, "B", "normal", 1, "A/B"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }


  return SVN_NO_ERROR;
}

static svn_error_t *
move_delete_intermediate(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_delete_intermediate", opts,
                                   pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "B"));
  SVN_ERR(sbox_wc_mkdir(&b, "B/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "B/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "B/A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "C"));
  SVN_ERR(sbox_wc_mkdir(&b, "C/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "C/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "C/A/A/A"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_move(&b, "A/A/A", "AAA_1"));

  SVN_ERR(sbox_wc_delete(&b, "A"));
  SVN_ERR(sbox_wc_move(&b, "B", "A"));

  SVN_ERR(sbox_wc_move(&b, "A/A/A", "AAA_2"));

  SVN_ERR(sbox_wc_delete(&b, "A/A"));
  SVN_ERR(sbox_wc_move(&b, "C/A", "A/A"));

  SVN_ERR(sbox_wc_move(&b, "A/A/A", "AAA_3"));

  /* Verify that the move is still recorded correctly */
  {
    nodes_row_t nodes[] = {

      {0, "",           "normal",       0, ""},

      {1, "AAA_1",      "normal",       1, "A/A/A",             MOVED_HERE},
      {1, "AAA_1/A",    "normal",       1, "A/A/A/A",           MOVED_HERE},
      {1, "AAA_2",      "normal",       1, "B/A/A",             MOVED_HERE},
      {1, "AAA_2/A",    "normal",       1, "B/A/A/A",           MOVED_HERE},
      {1, "AAA_3",      "normal",       1, "C/A/A",             MOVED_HERE},
      {1, "AAA_3/A",    "normal",       1, "C/A/A/A",           MOVED_HERE},

      {0, "A",          "normal",       1, "A"},
      {0, "A/A",        "normal",       1, "A/A"},
      {0, "A/A/A",      "normal",       1, "A/A/A"},
      {0, "A/A/A/A",    "normal",       1, "A/A/A/A"},

      {1, "A",          "normal",       1, "B",                 MOVED_HERE},
      {1, "A/A",        "normal",       1, "B/A",               MOVED_HERE},
      {1, "A/A/A",      "normal",       1, "B/A/A", FALSE, "AAA_1",   TRUE},
      {1, "A/A/A/A",    "normal",       1, "B/A/A/A",           MOVED_HERE},

      {2, "A/A",        "normal",       1, "C/A", MOVED_HERE},
      {2, "A/A/A",      "normal",       1, "C/A/A", FALSE, "AAA_2",   TRUE},
      {2, "A/A/A/A",    "normal",       1, "C/A/A/A",           MOVED_HERE},

      {3, "A/A/A",      "base-deleted", NO_COPY_FROM,      "AAA_3"},
      {3, "A/A/A/A",    "base-deleted", NO_COPY_FROM},

      {0, "B",          "normal",       1, "B"},
      {0, "B/A",        "normal",       1, "B/A"},
      {0, "B/A/A",      "normal",       1, "B/A/A"},
      {0, "B/A/A/A",    "normal",       1, "B/A/A/A"},

      {1, "B",          "base-deleted", NO_COPY_FROM, "A"},
      {1, "B/A",        "base-deleted", NO_COPY_FROM},
      {1, "B/A/A",      "base-deleted", NO_COPY_FROM},
      {1, "B/A/A/A",    "base-deleted", NO_COPY_FROM},

      {0, "C",          "normal",       1, "C"},
      {0, "C/A",        "normal",       1, "C/A"},
      {0, "C/A/A",      "normal",       1, "C/A/A"},
      {0, "C/A/A/A",    "normal",       1, "C/A/A/A"},

      {2, "C/A",        "base-deleted", NO_COPY_FROM, "A/A"},
      {2, "C/A/A",      "base-deleted", NO_COPY_FROM},
      {2, "C/A/A/A",    "base-deleted", NO_COPY_FROM},

      {0},
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Ok, now we are in the very ugly case where A/A/A is moved away 3 times */

  /* Let's delete A */
  SVN_ERR(sbox_wc_delete(&b, "A"));

  /* AAA_1, AAA_2 and AAA_3 should still be moves after deleting A */
  {
    nodes_row_t nodes[] = {

      {0, "",           "normal",       0, ""},

      {1, "AAA_1",      "normal",       1, "A/A/A",             MOVED_HERE},
      {1, "AAA_1/A",    "normal",       1, "A/A/A/A",           MOVED_HERE},

      {1, "AAA_2",      "normal",       1, "B/A/A",             MOVED_HERE},
      {1, "AAA_2/A",    "normal",       1, "B/A/A/A",           MOVED_HERE},

      {1, "AAA_3",      "normal",       1, "C/A/A",             MOVED_HERE},
      {1, "AAA_3/A",    "normal",       1, "C/A/A/A",           MOVED_HERE},

      {0, "A",          "normal",       1, "A"},
      {0, "A/A",        "normal",       1, "A/A"},
      {0, "A/A/A",      "normal",       1, "A/A/A"},
      {0, "A/A/A/A",    "normal",       1, "A/A/A/A"},

      {1, "A",          "base-deleted", NO_COPY_FROM},
      {1, "A/A",        "base-deleted", NO_COPY_FROM},
      {1, "A/A/A",      "base-deleted", NO_COPY_FROM, "AAA_1"},
      {1, "A/A/A/A",    "base-deleted", NO_COPY_FROM},

      {0, "B",          "normal",       1, "B"},
      {0, "B/A",        "normal",       1, "B/A"},
      {0, "B/A/A",      "normal",       1, "B/A/A"},
      {0, "B/A/A/A",    "normal",       1, "B/A/A/A"},

      {1, "B",          "base-deleted", NO_COPY_FROM},
      {1, "B/A",        "base-deleted", NO_COPY_FROM},
      {1, "B/A/A",      "base-deleted", NO_COPY_FROM, "AAA_2"},
      {1, "B/A/A/A",    "base-deleted", NO_COPY_FROM},

      {0, "C",          "normal",       1, "C"},
      {0, "C/A",        "normal",       1, "C/A"},
      {0, "C/A/A",      "normal",       1, "C/A/A"},
      {0, "C/A/A/A",    "normal",       1, "C/A/A/A"},

      {2, "C/A",        "base-deleted", NO_COPY_FROM},
      {2, "C/A/A",      "base-deleted", NO_COPY_FROM, "AAA_3"},
      {2, "C/A/A/A",    "base-deleted", NO_COPY_FROM},

      {0},
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_revert_intermediate(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_revert_intermediate", opts,
                                   pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "B"));
  SVN_ERR(sbox_wc_mkdir(&b, "B/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "B/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "B/A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "C"));
  SVN_ERR(sbox_wc_mkdir(&b, "C/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "C/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "C/A/A/A"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_move(&b, "A/A/A", "AAA_1"));

  SVN_ERR(sbox_wc_delete(&b, "A"));
  SVN_ERR(sbox_wc_move(&b, "B", "A"));

  SVN_ERR(sbox_wc_move(&b, "A/A/A", "AAA_2"));

  SVN_ERR(sbox_wc_delete(&b, "A/A"));
  SVN_ERR(sbox_wc_move(&b, "C/A", "A/A"));

  SVN_ERR(sbox_wc_move(&b, "A/A/A", "AAA_3"));

  /* Verify that the move is still recorded correctly */
  {
    nodes_row_t nodes[] = {

      {0, "",           "normal",       0, ""},

      {1, "AAA_1",      "normal",       1, "A/A/A",             MOVED_HERE},
      {1, "AAA_1/A",    "normal",       1, "A/A/A/A",           MOVED_HERE},
      {1, "AAA_2",      "normal",       1, "B/A/A",             MOVED_HERE},
      {1, "AAA_2/A",    "normal",       1, "B/A/A/A",           MOVED_HERE},
      {1, "AAA_3",      "normal",       1, "C/A/A",             MOVED_HERE},
      {1, "AAA_3/A",    "normal",       1, "C/A/A/A",           MOVED_HERE},

      {0, "A",          "normal",       1, "A"},
      {0, "A/A",        "normal",       1, "A/A"},
      {0, "A/A/A",      "normal",       1, "A/A/A"},
      {0, "A/A/A/A",    "normal",       1, "A/A/A/A"},

      {1, "A",          "normal",       1, "B",                 MOVED_HERE},
      {1, "A/A",        "normal",       1, "B/A",               MOVED_HERE},
      {1, "A/A/A",      "normal",       1, "B/A/A", FALSE, "AAA_1",   TRUE},
      {1, "A/A/A/A",    "normal",       1, "B/A/A/A",           MOVED_HERE},

      {2, "A/A",        "normal",       1, "C/A", MOVED_HERE},
      {2, "A/A/A",      "normal",       1, "C/A/A", FALSE, "AAA_2",   TRUE},
      {2, "A/A/A/A",    "normal",       1, "C/A/A/A",           MOVED_HERE},

      {3, "A/A/A",      "base-deleted", NO_COPY_FROM,      "AAA_3"},
      {3, "A/A/A/A",    "base-deleted", NO_COPY_FROM},

      {0, "B",          "normal",       1, "B"},
      {0, "B/A",        "normal",       1, "B/A"},
      {0, "B/A/A",      "normal",       1, "B/A/A"},
      {0, "B/A/A/A",    "normal",       1, "B/A/A/A"},

      {1, "B",          "base-deleted", NO_COPY_FROM, "A"},
      {1, "B/A",        "base-deleted", NO_COPY_FROM},
      {1, "B/A/A",      "base-deleted", NO_COPY_FROM},
      {1, "B/A/A/A",    "base-deleted", NO_COPY_FROM},

      {0, "C",          "normal",       1, "C"},
      {0, "C/A",        "normal",       1, "C/A"},
      {0, "C/A/A",      "normal",       1, "C/A/A"},
      {0, "C/A/A/A",    "normal",       1, "C/A/A/A"},

      {2, "C/A",        "base-deleted", NO_COPY_FROM, "A/A"},
      {2, "C/A/A",      "base-deleted", NO_COPY_FROM},
      {2, "C/A/A/A",    "base-deleted", NO_COPY_FROM},

      {0},
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Ok, now we are in the very ugly case where A/A/A is moved away 3 times */

  /* Let's revert A */
  SVN_ERR(sbox_wc_revert(&b, "A", svn_depth_infinity));

  /* AAA_1 should now be a copy, but AAA_2 and AAA_3 should still be moves,
     but now from the original location instead of from "A/A/A" */
  {
    nodes_row_t nodes[] = {

      {0, "",           "normal",       0, ""},

      {1, "AAA_1",      "normal",       1, "A/A/A",},
      {1, "AAA_1/A",    "normal",       1, "A/A/A/A"},
      {1, "AAA_2",      "normal",       1, "B/A/A",             MOVED_HERE},
      {1, "AAA_2/A",    "normal",       1, "B/A/A/A",           MOVED_HERE},
      {1, "AAA_3",      "normal",       1, "C/A/A",             MOVED_HERE},
      {1, "AAA_3/A",    "normal",       1, "C/A/A/A",           MOVED_HERE},

      {0, "A",          "normal",       1, "A"},
      {0, "A/A",        "normal",       1, "A/A"},
      {0, "A/A/A",      "normal",       1, "A/A/A"},
      {0, "A/A/A/A",    "normal",       1, "A/A/A/A"},

      {0, "B",          "normal",       1, "B"},
      {0, "B/A",        "normal",       1, "B/A"},
      {0, "B/A/A",      "normal",       1, "B/A/A"},
      {0, "B/A/A/A",    "normal",       1, "B/A/A/A"},

      {1, "B",          "base-deleted", NO_COPY_FROM},
      {1, "B/A",        "base-deleted", NO_COPY_FROM},
      {1, "B/A/A",      "base-deleted", NO_COPY_FROM, "AAA_2"},
      {1, "B/A/A/A",    "base-deleted", NO_COPY_FROM},

      {0, "C",          "normal",       1, "C"},
      {0, "C/A",        "normal",       1, "C/A"},
      {0, "C/A/A",      "normal",       1, "C/A/A"},
      {0, "C/A/A/A",    "normal",       1, "C/A/A/A"},

      {2, "C/A",        "base-deleted", NO_COPY_FROM},
      {2, "C/A/A",      "base-deleted", NO_COPY_FROM, "AAA_3"},
      {2, "C/A/A/A",    "base-deleted", NO_COPY_FROM},

      {0},
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_replace_ancestor_with_child(const svn_test_opts_t *opts, apr_pool_t *pool)
{
    svn_test__sandbox_t b;

    SVN_ERR(svn_test__sandbox_create(&b, "move_replace_ancestor_with_child", opts,
        pool));

    SVN_ERR(sbox_wc_mkdir(&b, "A"));
    SVN_ERR(sbox_wc_mkdir(&b, "A/A"));

    SVN_ERR(sbox_wc_commit(&b, ""));
    SVN_ERR(sbox_wc_update(&b, "", 1));

    SVN_ERR(sbox_wc_move(&b, "A", "A2"));

    {
      nodes_row_t nodes[] = {

        { 0, "",            "normal",       1, "" },

        { 0, "A",           "normal",       1, "A"},
        { 0, "A/A",         "normal",       1, "A/A" },

        { 1, "A",           "base-deleted", NO_COPY_FROM , "A2"},
        { 1, "A/A",         "base-deleted", NO_COPY_FROM },

        { 1, "A2",          "normal",       1, "A",     MOVED_HERE },
        { 1, "A2/A",        "normal",       1, "A/A",   MOVED_HERE },

        { 0 },
      };
      SVN_ERR(check_db_rows(&b, "", nodes));
    }

    SVN_ERR(sbox_wc_move(&b, "A2/A", "A"));

    {
      nodes_row_t nodes[] = {
        { 0, "",            "normal",       1, "" },

        { 0, "A",           "normal",       1, "A"},
        { 0, "A/A",         "normal",       1, "A/A" },

        { 1, "A",           "normal",       1, "A/A", FALSE, "A2", TRUE },
        { 1, "A/A",         "base-deleted", NO_COPY_FROM },

        { 1, "A2",          "normal",       1, "A",     MOVED_HERE },
        { 1, "A2/A",        "normal",       1, "A/A",   MOVED_HERE },

        { 2, "A2/A",        "base-deleted", NO_COPY_FROM, "A" },
        { 0 },
      };
      SVN_ERR(check_db_rows(&b, "", nodes));
    }

    /* ### This currently fails with an assertion in maintainer mode */
    SVN_ERR(sbox_wc_delete(&b, "A2"));

    {
      nodes_row_t nodes[] = {
        { 0, "",            "normal",       1, "" },

        { 0, "A",           "normal",       1, "A"},
        { 0, "A/A",         "normal",       1, "A/A" },

        { 1, "A",           "normal",       1, "A/A", MOVED_HERE },
        { 1, "A/A",         "base-deleted", NO_COPY_FROM, "A" },

        { 0 },
      };
      SVN_ERR(check_db_rows(&b, "", nodes));
    }

    SVN_ERR(sbox_wc_commit(&b, "A"));

    return SVN_NO_ERROR;
}

static svn_error_t *
move_twice_within_delete(const svn_test_opts_t *opts, apr_pool_t *pool)
{
    svn_test__sandbox_t b;

    SVN_ERR(svn_test__sandbox_create(&b, "move_twice_within_delete", opts,
        pool));

    SVN_ERR(sbox_wc_mkdir(&b, "A"));
    SVN_ERR(sbox_wc_mkdir(&b, "A/A"));
    SVN_ERR(sbox_wc_mkdir(&b, "A/A/A"));

    SVN_ERR(sbox_wc_commit(&b, ""));
    SVN_ERR(sbox_wc_update(&b, "", 1));

    SVN_ERR(sbox_wc_mkdir(&b, "B"));
    SVN_ERR(sbox_wc_move(&b, "A", "B/A"));
    SVN_ERR(sbox_wc_move(&b, "B/A/A", "B/AA"));
    SVN_ERR(sbox_wc_move(&b, "B/AA/A", "AA"));

    {
      nodes_row_t nodes[] = {

        { 0, "",          "normal",       1, "" },

        { 0, "A",         "normal",       1, "A" },
        { 0, "A/A",       "normal",       1, "A/A" },
        { 0, "A/A/A",     "normal",       1, "A/A/A" },

        { 1, "A",         "base-deleted", NO_COPY_FROM, "B/A" },
        { 1, "A/A",       "base-deleted", NO_COPY_FROM },
        { 1, "A/A/A",     "base-deleted", NO_COPY_FROM },

        { 1, "AA",        "normal",       1, "A/A/A", MOVED_HERE },

        { 1, "B",         "normal",       NO_COPY_FROM },
        { 2, "B/A",       "normal",       1, "A",       MOVED_HERE },
        { 2, "B/A/A",     "normal",       1, "A/A",     MOVED_HERE },
        { 2, "B/A/A/A",   "normal",       1, "A/A/A",   MOVED_HERE },

        { 3, "B/A/A",     "base-deleted", NO_COPY_FROM, "B/AA" },
        { 3, "B/A/A/A",   "base-deleted", NO_COPY_FROM },

        { 2, "B/AA",      "normal",       1, "A/A", MOVED_HERE},
        { 2, "B/AA/A",    "normal",       1, "A/A/A", MOVED_HERE },

        { 3, "B/AA/A",    "base-deleted", NO_COPY_FROM, "AA" },

        { 0 },
      };
      SVN_ERR(check_db_rows(&b, "", nodes));
    }

    SVN_ERR(sbox_wc_delete(&b, "B"));

    {
      nodes_row_t nodes[] = {
        { 0, "",        "normal", 1, "" },

        { 0, "A",       "normal", 1, "A" },
        { 0, "A/A",     "normal", 1, "A/A" },
        { 0, "A/A/A",   "normal", 1, "A/A/A" },

        { 1, "A",       "base-deleted", NO_COPY_FROM },
        { 1, "A/A",     "base-deleted", NO_COPY_FROM },
        { 1, "A/A/A",   "base-deleted", NO_COPY_FROM, "AA" },

        { 1, "AA", "normal", 1, "A/A/A", MOVED_HERE },

        { 0 },
      };
        SVN_ERR(check_db_rows(&b, "", nodes));
    }

    return SVN_NO_ERROR;
}

/* Helper function for 4 move4 tests */
static svn_error_t *
init_move4(svn_test__sandbox_t *sandbox,
           const char *test_name,
           const svn_test_opts_t *opts,
           svn_boolean_t move_away,
           apr_pool_t *pool)
{
  SVN_ERR(svn_test__sandbox_create(sandbox, test_name, opts, pool));

  SVN_ERR(sbox_wc_mkdir(sandbox, "A"));
  SVN_ERR(sbox_wc_mkdir(sandbox, "A/A"));
  SVN_ERR(sbox_wc_mkdir(sandbox, "A/A/A"));
  SVN_ERR(sbox_wc_mkdir(sandbox, "A/A/A/A"));

  SVN_ERR(sbox_wc_mkdir(sandbox, "B"));
  SVN_ERR(sbox_wc_mkdir(sandbox, "B/A"));
  SVN_ERR(sbox_wc_mkdir(sandbox, "B/A/A"));
  SVN_ERR(sbox_wc_mkdir(sandbox, "B/A/A/A"));

  SVN_ERR(sbox_wc_mkdir(sandbox, "C"));
  SVN_ERR(sbox_wc_mkdir(sandbox, "C/A"));
  SVN_ERR(sbox_wc_mkdir(sandbox, "C/A/A"));
  SVN_ERR(sbox_wc_mkdir(sandbox, "C/A/A/A"));

  SVN_ERR(sbox_wc_mkdir(sandbox, "D"));
  SVN_ERR(sbox_wc_mkdir(sandbox, "D/A"));
  SVN_ERR(sbox_wc_mkdir(sandbox, "D/A/A"));
  SVN_ERR(sbox_wc_mkdir(sandbox, "D/A/A/A"));

  SVN_ERR(sbox_wc_commit(sandbox, "")); /* r1 */

  if (strstr(test_name, "_edit_"))
    {
      SVN_ERR(sbox_wc_propset(sandbox, "key", "value", "A/A/A"));
      SVN_ERR(sbox_wc_propset(sandbox, "key", "value", "B/A/A"));
      SVN_ERR(sbox_wc_propset(sandbox, "key", "value", "C/A/A"));
      SVN_ERR(sbox_wc_propset(sandbox, "key", "value", "D/A/A"));
      SVN_ERR(sbox_wc_propset(sandbox, "key", "value", "A/A/A/A"));
      SVN_ERR(sbox_wc_propset(sandbox, "key", "value", "B/A/A/A"));
      SVN_ERR(sbox_wc_propset(sandbox, "key", "value", "C/A/A/A"));
      SVN_ERR(sbox_wc_propset(sandbox, "key", "value", "D/A/A/A"));
    }
  else if (strstr(test_name, "_delete_"))
    {
      SVN_ERR(sbox_wc_delete(sandbox, "A/A/A/A"));
      SVN_ERR(sbox_wc_delete(sandbox, "B/A/A/A"));
      SVN_ERR(sbox_wc_delete(sandbox, "C/A/A/A"));
      SVN_ERR(sbox_wc_delete(sandbox, "D/A/A/A"));
    }
  else if (strstr(test_name, "_add_"))
    {
      SVN_ERR(sbox_wc_mkdir(sandbox, "A/A/A/NEW"));
      SVN_ERR(sbox_wc_mkdir(sandbox, "B/A/A/NEW"));
      SVN_ERR(sbox_wc_mkdir(sandbox, "C/A/A/NEW"));
      SVN_ERR(sbox_wc_mkdir(sandbox, "D/A/A/NEW"));
    }
  else if (strstr(test_name, "_replace_"))
    {
      SVN_ERR(sbox_wc_delete(sandbox, "A/A/A/A"));
      SVN_ERR(sbox_wc_delete(sandbox, "B/A/A/A"));
      SVN_ERR(sbox_wc_delete(sandbox, "C/A/A/A"));
      SVN_ERR(sbox_wc_delete(sandbox, "D/A/A/A"));
      SVN_ERR(sbox_file_write(sandbox, "A/A/A/A", "A"));
      SVN_ERR(sbox_file_write(sandbox, "B/A/A/A", "A"));
      SVN_ERR(sbox_file_write(sandbox, "C/A/A/A", "A"));
      SVN_ERR(sbox_file_write(sandbox, "D/A/A/A", "A"));
      SVN_ERR(sbox_wc_add(sandbox, "A/A/A/A"));
      SVN_ERR(sbox_wc_add(sandbox, "B/A/A/A"));
      SVN_ERR(sbox_wc_add(sandbox, "C/A/A/A"));
      SVN_ERR(sbox_wc_add(sandbox, "D/A/A/A"));
    }
  else if (strstr(test_name, "_delself_"))
    {
      SVN_ERR(sbox_wc_delete(sandbox, "A/A/A"));
      SVN_ERR(sbox_wc_delete(sandbox, "B/A/A"));
      SVN_ERR(sbox_wc_delete(sandbox, "C/A/A"));
      SVN_ERR(sbox_wc_delete(sandbox, "D/A/A"));
    }
  else if (strstr(test_name, "_replaceself_"))
    {
      SVN_ERR(sbox_wc_delete(sandbox, "A/A/A"));
      SVN_ERR(sbox_wc_delete(sandbox, "B/A/A"));
      SVN_ERR(sbox_wc_delete(sandbox, "C/A/A"));
      SVN_ERR(sbox_wc_delete(sandbox, "D/A/A"));
      SVN_ERR(sbox_file_write(sandbox, "A/A/A", "A"));
      SVN_ERR(sbox_file_write(sandbox, "B/A/A", "A"));
      SVN_ERR(sbox_file_write(sandbox, "C/A/A", "A"));
      SVN_ERR(sbox_file_write(sandbox, "D/A/A", "A"));
      SVN_ERR(sbox_wc_add(sandbox, "A/A/A"));
      SVN_ERR(sbox_wc_add(sandbox, "B/A/A"));
      SVN_ERR(sbox_wc_add(sandbox, "C/A/A"));
      SVN_ERR(sbox_wc_add(sandbox, "D/A/A"));
    }

  SVN_ERR(sbox_wc_commit(sandbox, ""));
  SVN_ERR(sbox_wc_update(sandbox, "", 1));

  SVN_ERR(sbox_wc_move(sandbox, "A/A/A", "AAA_1"));

  if (move_away)
    SVN_ERR(sbox_wc_move(sandbox, "A", "A_moved"));
  else
    SVN_ERR(sbox_wc_delete(sandbox, "A"));

  SVN_ERR(sbox_wc_move(sandbox, "B", "A"));

  SVN_ERR(sbox_wc_move(sandbox, "A/A/A", "AAA_2"));

  if (move_away)
    SVN_ERR(sbox_wc_move(sandbox, "A/A", "BA_moved"));
  else
    SVN_ERR(sbox_wc_delete(sandbox, "A/A"));

  SVN_ERR(sbox_wc_move(sandbox, "C/A", "A/A"));

  SVN_ERR(sbox_wc_move(sandbox, "A/A/A", "AAA_3"));

  SVN_ERR(sbox_wc_move(sandbox, "D/A/A", "A/A/A"));

  return SVN_NO_ERROR;
}

static svn_error_t *
del4_update_edit_AAA(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(init_move4(&b, "del4_update_edit_AAA", opts, FALSE, pool));

  {
    nodes_row_t nodes[] = {

      { 0, "A/A/A",     "normal",    1, "A/A/A" },
      { 1, "A/A/A",     "normal",    1, "B/A/A", FALSE, "AAA_1", TRUE },
      { 2, "A/A/A",     "normal",    1, "C/A/A", FALSE, "AAA_2", TRUE },
      { 3, "A/A/A",     "normal",    1, "D/A/A", FALSE, "AAA_3", TRUE },

      { 0, "A/A/A/A",   "normal",    1, "A/A/A/A" },
      { 1, "A/A/A/A",   "normal",    1, "B/A/A/A", FALSE, NULL, TRUE },
      { 2, "A/A/A/A",   "normal",    1, "C/A/A/A", FALSE, NULL, TRUE },
      { 3, "A/A/A/A",   "normal",    1, "D/A/A/A", FALSE, NULL, TRUE },

      { 0 },
    };

    SVN_ERR(check_db_rows(&b, "A/A/A", nodes));
  }

  /* Update and resolve via mine strategy */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));

  {
    nodes_row_t nodes[] = {
      {0, "",                 "normal",       2, ""},
      {0, "A",                "normal",       2, "A"},
      {0, "A/A",              "normal",       2, "A/A"},
      {0, "A/A/A",            "normal",       2, "A/A/A", NOT_MOVED, "key"},
      {0, "A/A/A/A",          "normal",       2, "A/A/A/A", NOT_MOVED, "key"},
      {0, "B",                "normal",       2, "B"},
      {0, "B/A",              "normal",       2, "B/A"},
      {0, "B/A/A",            "normal",       2, "B/A/A", NOT_MOVED, "key"},
      {0, "B/A/A/A",          "normal",       2, "B/A/A/A", NOT_MOVED, "key"},
      {0, "C",                "normal",       2, "C"},
      {0, "C/A",              "normal",       2, "C/A"},
      {0, "C/A/A",            "normal",       2, "C/A/A", NOT_MOVED, "key"},
      {0, "C/A/A/A",          "normal",       2, "C/A/A/A", NOT_MOVED, "key"},
      {0, "D",                "normal",       2, "D"},
      {0, "D/A",              "normal",       2, "D/A"},
      {0, "D/A/A",            "normal",       2, "D/A/A", NOT_MOVED, "key"},
      {0, "D/A/A/A",          "normal",       2, "D/A/A/A", NOT_MOVED, "key"},

      {1, "A",                "normal",       2, "B", MOVED_HERE},
      {1, "A/A",              "normal",       2, "B/A", MOVED_HERE},
      {1, "A/A/A",            "normal",       2, "B/A/A", FALSE, "AAA_1", TRUE, "key"},
      {1, "A/A/A/A",          "normal",       2, "B/A/A/A", FALSE, NULL, TRUE, "key"},
      {1, "AAA_1",            "normal",       2, "A/A/A", MOVED_HERE, "key"},
      {1, "AAA_1/A",          "normal",       2, "A/A/A/A", MOVED_HERE, "key"},
      {1, "AAA_2",            "normal",       2, "B/A/A", MOVED_HERE, "key"},
      {1, "AAA_2/A",          "normal",       2, "B/A/A/A", MOVED_HERE, "key"},
      {1, "AAA_3",            "normal",       2, "C/A/A", MOVED_HERE, "key"},
      {1, "AAA_3/A",          "normal",       2, "C/A/A/A", MOVED_HERE, "key"},
      {1, "B",                "base-deleted", NO_COPY_FROM, "A"},
      {1, "B/A",              "base-deleted", NO_COPY_FROM},
      {1, "B/A/A",            "base-deleted", NO_COPY_FROM},
      {1, "B/A/A/A",          "base-deleted", NO_COPY_FROM},

      {2, "A/A",              "normal",       2, "C/A", MOVED_HERE},
      {2, "A/A/A",            "normal",       2, "C/A/A", FALSE, "AAA_2", TRUE, "key"},
      {2, "A/A/A/A",          "normal",       2, "C/A/A/A", FALSE, NULL, TRUE,  "key"},
      {2, "C/A",              "base-deleted", NO_COPY_FROM, "A/A"},
      {2, "C/A/A",            "base-deleted", NO_COPY_FROM},
      {2, "C/A/A/A",          "base-deleted", NO_COPY_FROM},

      {3, "A/A/A",            "normal",       2, "D/A/A", FALSE, "AAA_3", TRUE, "key"},
      {3, "A/A/A/A",          "normal",       2, "D/A/A/A", FALSE, NULL, TRUE,  "key"},
      {3, "D/A/A",            "base-deleted", NO_COPY_FROM, "A/A/A"},
      {3, "D/A/A/A",          "base-deleted", NO_COPY_FROM},

      { 0 },
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
    SVN_ERR(check_db_conflicts(&b, "", NULL));
  }

  /* Go back to start position */
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));

  {
    nodes_row_t nodes[] = {
      {0, "",                 "normal",       1, ""},
      {0, "A",                "normal",       1, "A"},
      {0, "A/A",              "normal",       1, "A/A"},
      {0, "A/A/A",            "normal",       1, "A/A/A", NOT_MOVED},
      {0, "A/A/A/A",          "normal",       1, "A/A/A/A", NOT_MOVED},
      {0, "B",                "normal",       1, "B"},
      {0, "B/A",              "normal",       1, "B/A"},
      {0, "B/A/A",            "normal",       1, "B/A/A", NOT_MOVED},
      {0, "B/A/A/A",          "normal",       1, "B/A/A/A", NOT_MOVED},
      {0, "C",                "normal",       1, "C"},
      {0, "C/A",              "normal",       1, "C/A"},
      {0, "C/A/A",            "normal",       1, "C/A/A", NOT_MOVED},
      {0, "C/A/A/A",          "normal",       1, "C/A/A/A", NOT_MOVED},
      {0, "D",                "normal",       1, "D"},
      {0, "D/A",              "normal",       1, "D/A"},
      {0, "D/A/A",            "normal",       1, "D/A/A", NOT_MOVED},
      {0, "D/A/A/A",          "normal",       1, "D/A/A/A", NOT_MOVED},

      {1, "A",                "normal",       1, "B", MOVED_HERE},
      {1, "A/A",              "normal",       1, "B/A", MOVED_HERE},
      {1, "A/A/A",            "normal",       1, "B/A/A", FALSE, "AAA_1", TRUE},
      {1, "A/A/A/A",          "normal",       1, "B/A/A/A", FALSE, NULL, TRUE},
      {1, "AAA_1",            "normal",       1, "A/A/A", MOVED_HERE},
      {1, "AAA_1/A",          "normal",       1, "A/A/A/A", MOVED_HERE},
      {1, "AAA_2",            "normal",       1, "B/A/A", MOVED_HERE},
      {1, "AAA_2/A",          "normal",       1, "B/A/A/A", MOVED_HERE},
      {1, "AAA_3",            "normal",       1, "C/A/A", MOVED_HERE},
      {1, "AAA_3/A",          "normal",       1, "C/A/A/A", MOVED_HERE},
      {1, "B",                "base-deleted", NO_COPY_FROM, "A"},
      {1, "B/A",              "base-deleted", NO_COPY_FROM},
      {1, "B/A/A",            "base-deleted", NO_COPY_FROM},
      {1, "B/A/A/A",          "base-deleted", NO_COPY_FROM},

      {2, "A/A",              "normal",       1, "C/A", MOVED_HERE},
      {2, "A/A/A",            "normal",       1, "C/A/A", FALSE, "AAA_2", TRUE},
      {2, "A/A/A/A",          "normal",       1, "C/A/A/A", FALSE, NULL, TRUE},
      {2, "C/A",              "base-deleted", NO_COPY_FROM, "A/A"},
      {2, "C/A/A",            "base-deleted", NO_COPY_FROM},
      {2, "C/A/A/A",          "base-deleted", NO_COPY_FROM},

      {3, "A/A/A",            "normal",       1, "D/A/A", FALSE, "AAA_3", TRUE},
      {3, "A/A/A/A",          "normal",       1, "D/A/A/A", FALSE, NULL, TRUE},
      {3, "D/A/A",            "base-deleted", NO_COPY_FROM, "A/A/A"},
      {3, "D/A/A/A",          "base-deleted", NO_COPY_FROM},

      { 0 },
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
    SVN_ERR(check_db_conflicts(&b, "", NULL));
  }

  /* Update and resolve via their strategy */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  {
    conflict_info_t conflicts[] = {
      {"A",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_replaced}},
      {"B",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "B"}},
      {"C/A",   FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "C/A"}},
      {"D/A/A", FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "D/A/A"}},
      {0},
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  /* This breaks the move A/A/A -> AAA_1 */
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty, svn_wc_conflict_choose_merged));
  /* This breaks the move B -> A */
  SVN_ERR(sbox_wc_resolve(&b, "B", svn_depth_empty, svn_wc_conflict_choose_merged));
  /* This breaks the move C/A/A -> A/A */
  SVN_ERR(sbox_wc_resolve(&b, "C/A", svn_depth_empty, svn_wc_conflict_choose_merged));
  /* This breaks the move from D/A/A -> A/A/A */
  SVN_ERR(sbox_wc_resolve(&b, "D/A/A", svn_depth_empty, svn_wc_conflict_choose_merged));

  {
    nodes_row_t nodes[] = {
      {0, "",          "normal",       2, ""},
      {0, "A",         "normal",       2, "A"},
      {0, "A/A",       "normal",       2, "A/A"},
      {0, "A/A/A",     "normal",       2, "A/A/A", NOT_MOVED, "key"},
      {0, "A/A/A/A",   "normal",       2, "A/A/A/A", NOT_MOVED, "key"},
      {0, "B",         "normal",       2, "B"},
      {0, "B/A",       "normal",       2, "B/A"},
      {0, "B/A/A",     "normal",       2, "B/A/A", NOT_MOVED, "key"},
      {0, "B/A/A/A",   "normal",       2, "B/A/A/A", NOT_MOVED, "key"},
      {0, "C",         "normal",       2, "C"},
      {0, "C/A",       "normal",       2, "C/A"},
      {0, "C/A/A",     "normal",       2, "C/A/A", NOT_MOVED, "key"},
      {0, "C/A/A/A",   "normal",       2, "C/A/A/A", NOT_MOVED, "key"},
      {0, "D",         "normal",       2, "D"},
      {0, "D/A",       "normal",       2, "D/A"},
      {0, "D/A/A",     "normal",       2, "D/A/A", NOT_MOVED, "key"},
      {0, "D/A/A/A",   "normal",       2, "D/A/A/A", NOT_MOVED, "key"},
      {1, "A",         "normal",       1, "B"},
      {1, "A/A",       "normal",       1, "B/A"},
      {1, "A/A/A",     "normal",       1, "B/A/A", FALSE},
      {1, "A/A/A/A",   "normal",       1, "B/A/A/A"},
      {1, "AAA_1",     "normal",       1, "A/A/A"},
      {1, "AAA_1/A",   "normal",       1, "A/A/A/A"},
      {1, "AAA_2",     "normal",       1, "B/A/A", MOVED_HERE},
      {1, "AAA_2/A",   "normal",       1, "B/A/A/A", MOVED_HERE},
      {1, "AAA_3",     "normal",       1, "C/A/A", MOVED_HERE},
      {1, "AAA_3/A",   "normal",       1, "C/A/A/A", MOVED_HERE},
      {1, "B",         "base-deleted", NO_COPY_FROM},
      {1, "B/A",       "base-deleted", NO_COPY_FROM},
      {1, "B/A/A",     "base-deleted", NO_COPY_FROM},
      {1, "B/A/A/A",   "base-deleted", NO_COPY_FROM},
      {2, "A/A",       "normal",       1, "C/A"},
      {2, "A/A/A",     "normal",       1, "C/A/A", FALSE, "AAA_2"},
      {2, "A/A/A/A",   "normal",       1, "C/A/A/A"},
      {2, "C/A",       "base-deleted", NO_COPY_FROM},
      {2, "C/A/A",     "base-deleted", NO_COPY_FROM},
      {2, "C/A/A/A",   "base-deleted", NO_COPY_FROM},
      {3, "A/A/A",     "normal",       1, "D/A/A", FALSE, "AAA_3"},
      {3, "A/A/A/A",   "normal",       1, "D/A/A/A"},
      {3, "D/A/A",     "base-deleted", NO_COPY_FROM},
      {3, "D/A/A/A",   "base-deleted", NO_COPY_FROM},

      { 0 },
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
  }


  return SVN_NO_ERROR;
}

static svn_error_t *
del4_update_delete_AAA(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(init_move4(&b, "del4_update_delete_AAA", opts, FALSE, pool));

  /* Update and resolve via mine strategy */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  {
    conflict_info_t conflicts[] = {
      {"A",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_replaced}},
      {"B",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "B"}},
      {"C/A",   FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "C/A"}},
      {"D/A/A", FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "D/A/A"}},
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
  /* Go back to start position */
  SVN_ERR(sbox_wc_update(&b, "", 1));
  {
    conflict_info_t conflicts[] = {
      {"A",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_replaced}},
      {"B",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "B"}},
      {"C/A",   FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "C/A"}},
      {"D/A/A", FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "D/A/A"}},
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
  /* Update and resolve via their strategy */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  {
    conflict_info_t conflicts[] = {
      {"A", FALSE, FALSE, {svn_wc_conflict_action_edit, svn_wc_conflict_reason_replaced}},
      {"B", FALSE, FALSE, {svn_wc_conflict_action_edit, svn_wc_conflict_reason_moved_away, "B"}},
      {"C/A", FALSE, FALSE, {svn_wc_conflict_action_edit, svn_wc_conflict_reason_moved_away, "C/A"}},
      {"D/A/A", FALSE, FALSE, {svn_wc_conflict_action_edit, svn_wc_conflict_reason_moved_away, "D/A/A"}},
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_merged));

  return SVN_NO_ERROR;
}

static svn_error_t *
del4_update_add_AAA(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(init_move4(&b, "del4_update_add_AAA", opts, FALSE, pool));

  /* Update and resolve via mine strategy */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  {
    conflict_info_t conflicts[] = {
      {"A", FALSE, FALSE, {svn_wc_conflict_action_edit, svn_wc_conflict_reason_replaced}},
      {"B", FALSE, FALSE, {svn_wc_conflict_action_edit, svn_wc_conflict_reason_moved_away, "B"}},
      {"C/A", FALSE, FALSE, {svn_wc_conflict_action_edit, svn_wc_conflict_reason_moved_away, "C/A"}},
      {"D/A/A", FALSE, FALSE, {svn_wc_conflict_action_edit, svn_wc_conflict_reason_moved_away, "D/A/A"}},
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
  /* Go back to start position */
  SVN_ERR(sbox_wc_update(&b, "", 1));
  {
    conflict_info_t conflicts[] = {
      {"A",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_replaced}},
      {"B",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "B"}},
      {"C/A",   FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "C/A"}},
      {"D/A/A", FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "D/A/A"}},
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
  /* Update and resolve via their strategy */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  {
    conflict_info_t conflicts[] = {
      {"A",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_replaced}},
      {"B",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "B"}},
      {"C/A",   FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "C/A"}},
      {"D/A/A", FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "D/A/A"}},
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_merged));

  return SVN_NO_ERROR;
}

static svn_error_t *
del4_update_replace_AAA(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(init_move4(&b, "del4_update_replace_AAA", opts, FALSE, pool));

  /* Update and resolve via mine strategy */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  {
    conflict_info_t conflicts[] = {
      {"A",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_replaced}},
      {"B",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "B"}},
      {"C/A",   FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "C/A"}},
      {"D/A/A", FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "D/A/A"}},
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
  /* Go back to start position */
  SVN_ERR(sbox_wc_update(&b, "", 1));
  {
    conflict_info_t conflicts[] = {
      {"A",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_replaced}},
      {"B",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "B"}},
      {"C/A",   FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "C/A"}},
      {"D/A/A", FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "D/A/A"}},
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
  /* Update and resolve via their strategy */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  {
    conflict_info_t conflicts[] = {
      {"A",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_replaced}},
      {"B",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "B"}},
      {"C/A",   FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "C/A"}},
      {"D/A/A", FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "D/A/A"}},
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_merged));

  return SVN_NO_ERROR;
}

static svn_error_t *
del4_update_delself_AAA(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(init_move4(&b, "del4_update_delself_AAA", opts, FALSE, pool));

  /* Update and resolve via mine strategy */
  SVN_ERR(sbox_wc_update(&b, "", 2));

  {
    conflict_info_t conflicts[] = {
      {"A",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_replaced}},
      {"B",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "B"}},
      {"C/A",   FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "C/A"}},
      {"D/A/A", FALSE, FALSE, {svn_wc_conflict_action_delete,
                               svn_wc_conflict_reason_moved_away, "D/A/A"}},
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }


  /* Resolve a few conflicts manually */
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(sbox_wc_resolve(&b, "B", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(sbox_wc_resolve(&b, "C/A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));

  {
    nodes_row_t nodes[] = {
      {0, "",           "normal",       2, ""},
      {0, "A",          "normal",       2, "A"},
      {0, "A/A",        "normal",       2, "A/A"},
      {0, "B",          "normal",       2, "B"},
      {0, "B/A",        "normal",       2, "B/A"},
      {0, "C",          "normal",       2, "C"},
      {0, "C/A",        "normal",       2, "C/A"},
      {0, "D",          "normal",       2, "D"},
      {0, "D/A",        "normal",       2, "D/A"},
      {1, "A",          "normal",       2, "B", MOVED_HERE},
      {1, "A/A",        "normal",       2, "B/A", MOVED_HERE},
      {1, "AAA_1",      "normal",       1, "A/A/A"},
      {1, "AAA_1/A",    "normal",       1, "A/A/A/A"},
      {1, "AAA_2",      "normal",       1, "B/A/A"},
      {1, "AAA_2/A",    "normal",       1, "B/A/A/A"},
      {1, "AAA_3",      "normal",       1, "C/A/A"},
      {1, "AAA_3/A",    "normal",       1, "C/A/A/A"},
      {1, "B",          "base-deleted", NO_COPY_FROM, "A"},
      {1, "B/A",        "base-deleted", NO_COPY_FROM},
      {2, "A/A",        "normal",       2, "C/A", MOVED_HERE},
      {2, "C/A",        "base-deleted", NO_COPY_FROM, "A/A"},
      {3, "A/A/A",      "normal",       1, "D/A/A"},
      {3, "A/A/A/A",    "normal",       1, "D/A/A/A"},

      { 0 },
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
  }
  {
    conflict_info_t conflicts[] = {
      /* Not resolved yet */
      {"D/A/A", FALSE, FALSE, {svn_wc_conflict_action_delete,
                               svn_wc_conflict_reason_moved_away, "D/A/A"}},

      /* New */
      {"A/A",   FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_deleted}},
      {"A/A/A", FALSE, FALSE, {svn_wc_conflict_action_delete,
                               svn_wc_conflict_reason_moved_away, "A/A/A"}},

      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }

  /* These can only be resolved to merged, as the merge is already broken
     (because the move source is gone): incoming delete on moved_away */
  SVN_ERR(sbox_wc_resolve(&b, "D/A/A", svn_depth_empty,
                          svn_wc_conflict_choose_merged));
  SVN_ERR(sbox_wc_resolve(&b, "A/A/A", svn_depth_empty,
                          svn_wc_conflict_choose_merged));

  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
  /* Go back to start position */
  SVN_ERR(sbox_wc_update(&b, "", 1));
  {
    conflict_info_t conflicts[] = {
      {"A", FALSE, FALSE,   { svn_wc_conflict_action_edit,
                              svn_wc_conflict_reason_replaced}},
      {"B", FALSE, FALSE,   { svn_wc_conflict_action_edit,
                              svn_wc_conflict_reason_moved_away, "B"}},
      {"C/A", FALSE, FALSE, { svn_wc_conflict_action_edit,
                              svn_wc_conflict_reason_moved_away, "C/A"}},
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(sbox_wc_resolve(&b, "B", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(sbox_wc_resolve(&b, "C/A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    conflict_info_t conflicts[] = {
      {"A/A",  FALSE, FALSE,  { svn_wc_conflict_action_edit,
                                svn_wc_conflict_reason_deleted}},
      {"A/A/A", FALSE, FALSE, { svn_wc_conflict_action_add,
                                svn_wc_conflict_reason_added}},
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(sbox_wc_resolve(&b, "A/A/A", svn_depth_empty,
                          svn_wc_conflict_choose_merged));
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity,
                          svn_wc_conflict_choose_mine_conflict));
  /* Update and resolve via their strategy */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_merged));

  return SVN_NO_ERROR;
}

static svn_error_t *
del4_update_replaceself_AAA(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(init_move4(&b, "del4_update_replaceself_AAA", opts, FALSE, pool));

  /* Update and resolve via mine strategy */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
  /* Go back to start position */
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
  /* Update and resolve via their strategy */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_merged));

  return SVN_NO_ERROR;
}


static svn_error_t *
move4_update_edit_AAA(const svn_test_opts_t *opts, apr_pool_t *pool)
{
    svn_test__sandbox_t b;

    SVN_ERR(init_move4(&b, "move4_update_edit_AAA", opts, TRUE, pool));

    {
      nodes_row_t nodes[] = {

        { 0, "A/A/A",     "normal",    1, "A/A/A" },
        { 1, "A/A/A",     "normal",    1, "B/A/A", FALSE, NULL /*"AAA_1"*/, TRUE },
        { 2, "A/A/A",     "normal",    1, "C/A/A", FALSE, NULL /*"AAA_2"*/, TRUE },
        { 3, "A/A/A",     "normal",    1, "D/A/A", FALSE, "AAA_3", TRUE },

        { 0, "A/A/A/A",   "normal",    1, "A/A/A/A" },
        { 1, "A/A/A/A",   "normal",    1, "B/A/A/A", FALSE, NULL, TRUE },
        { 2, "A/A/A/A",   "normal",    1, "C/A/A/A", FALSE, NULL, TRUE },
        { 3, "A/A/A/A",   "normal",    1, "D/A/A/A", FALSE, NULL, TRUE },


        { 0 },
      };

      SVN_ERR(check_db_rows(&b, "A/A/A", nodes));
    }

    /* Update and resolve via mine strategy */
    SVN_ERR(sbox_wc_update(&b, "", 2));
    SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));

    {
      nodes_row_t nodes[] = {

        { 0, "A/A/A",     "normal",  2, "A/A/A", NOT_MOVED,            "key" },
        { 1, "A/A/A",     "normal",  2, "B/A/A", FALSE, NULL /*"AAA_1"*/, TRUE, "key" },
        { 2, "A/A/A",     "normal",  2, "C/A/A", FALSE, NULL /*"AAA_2"*/, TRUE, "key" },
        { 3, "A/A/A",     "normal",  2, "D/A/A", FALSE, "AAA_3", TRUE, "key" },

        { 0, "A/A/A/A",   "normal",  2, "A/A/A/A", NOT_MOVED,          "key" },
        { 1, "A/A/A/A",   "normal",  2, "B/A/A/A", FALSE, NULL,  TRUE, "key" },
        { 2, "A/A/A/A",   "normal",  2, "C/A/A/A", FALSE, NULL,  TRUE, "key" },
        { 3, "A/A/A/A",   "normal",  2, "D/A/A/A", FALSE, NULL,  TRUE, "key" },

        { 0 },
      };
        SVN_ERR(check_db_rows(&b, "A/A/A", nodes));
    }

    /* Go back to start position */
    SVN_ERR(sbox_wc_update(&b, "", 1));
    SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
    /* Update and resolve via their strategy */
    SVN_ERR(sbox_wc_update(&b, "", 2));
    SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_merged));

    {
      nodes_row_t nodes[] = {

        { 0, "A/A/A",     "normal",       2, "A/A/A", NOT_MOVED, "key" },
        { 1, "A/A/A",     "normal",       1, "B/A/A" },
        { 2, "A/A/A",     "normal",       1, "C/A/A" },
        { 3, "A/A/A",     "normal",       1, "D/A/A", FALSE, "AAA_3"},

        { 0, "A/A/A/A",   "normal",       2, "A/A/A/A", NOT_MOVED, "key" },
        { 1, "A/A/A/A",   "normal",       1, "B/A/A/A" },
        { 2, "A/A/A/A",   "normal",       1, "C/A/A/A" },
        { 3, "A/A/A/A",   "normal",       1, "D/A/A/A" },

        { 0 },
      };

      SVN_ERR(check_db_rows(&b, "A/A/A", nodes));
    }

    return SVN_NO_ERROR;
}

static svn_error_t *
move4_update_delete_AAA(const svn_test_opts_t *opts, apr_pool_t *pool)
{
    svn_test__sandbox_t b;

    SVN_ERR(init_move4(&b, "move4_update_delete_AAA", opts, TRUE, pool));

    /* Update and resolve via mine strategy */
    SVN_ERR(sbox_wc_update(&b, "", 2));
    SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
    /* Go back to start position */
    SVN_ERR(sbox_wc_update(&b, "", 1));
    SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
    /* Update and resolve via their strategy */
    SVN_ERR(sbox_wc_update(&b, "", 2));
    SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_merged));

    return SVN_NO_ERROR;
}

static svn_error_t *
move4_update_add_AAA(const svn_test_opts_t *opts, apr_pool_t *pool)
{
    svn_test__sandbox_t b;

    SVN_ERR(init_move4(&b, "move4_update_add_AAA", opts, TRUE, pool));

    /* Update and resolve via mine strategy */
    SVN_ERR(sbox_wc_update(&b, "", 2));
    SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
    /* Go back to start position */
    SVN_ERR(sbox_wc_update(&b, "", 1));
    SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
    /* Update and resolve via their strategy */
    SVN_ERR(sbox_wc_update(&b, "", 2));
    SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_merged));

    return SVN_NO_ERROR;
}

static svn_error_t *
move4_update_replace_AAA(const svn_test_opts_t *opts, apr_pool_t *pool)
{
    svn_test__sandbox_t b;

    SVN_ERR(init_move4(&b, "move4_update_replace_AAA", opts, TRUE, pool));

    /* Update and resolve via mine strategy */
    SVN_ERR(sbox_wc_update(&b, "", 2));
    SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
    /* Go back to start position */
    SVN_ERR(sbox_wc_update(&b, "", 1));
    SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
    /* Update and resolve via their strategy */
    SVN_ERR(sbox_wc_update(&b, "", 2));
    SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_merged));

    return SVN_NO_ERROR;
}

static svn_error_t *
move4_update_delself_AAA(const svn_test_opts_t *opts, apr_pool_t *pool)
{
    svn_test__sandbox_t b;

    SVN_ERR(init_move4(&b, "move4_update_delself_AAA", opts, TRUE, pool));

    /* Update and resolve via mine strategy */
    SVN_ERR(sbox_wc_update(&b, "", 2));

    {
      nodes_row_t nodes[] = {

        {1, "A_moved",        "normal",       1, "A", MOVED_HERE},
        {1, "A_moved/A",      "normal",       1, "A/A", MOVED_HERE},
        {1, "A_moved/A/A",    "normal",       1, "A/A/A", MOVED_HERE},
        {3, "A_moved/A/A",    "base-deleted", NO_COPY_FROM, "AAA_1"},
        {1, "A_moved/A/A/A",  "normal",       1, "A/A/A/A", MOVED_HERE},
        {3, "A_moved/A/A/A",  "base-deleted", NO_COPY_FROM},

        { 0 },
      };
        SVN_ERR(check_db_rows(&b, "A_moved", nodes));
    }

    /* Resolve a few conflicts manually */
    SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
        svn_wc_conflict_choose_mine_conflict));

    {
      nodes_row_t nodes[] = {
        {0, "",               "normal",       2, ""},
        {0, "A",              "normal",       2, "A"},
        {0, "A/A",            "normal",       2, "A/A"},
        {0, "B",              "normal",       2, "B"},
        {0, "B/A",            "normal",       2, "B/A"},
        {0, "C",              "normal",       2, "C"},
        {0, "C/A",            "normal",       2, "C/A"},
        {0, "D",              "normal",       2, "D"},
        {0, "D/A",            "normal",       2, "D/A"},
        {1, "A",              "normal",       1, "B", FALSE, "A_moved", TRUE},
        {1, "A/A",            "normal",       1, "B/A", MOVED_HERE},
        {1, "A/A/A",          "normal",       1, "B/A/A", MOVED_HERE},
        {1, "A/A/A/A",        "normal",       1, "B/A/A/A", MOVED_HERE},
        {1, "AAA_1",          "normal",       1, "A/A/A"},
        {1, "AAA_1/A",        "normal",       1, "A/A/A/A"},
        {1, "AAA_2",          "normal",       1, "B/A/A", MOVED_HERE},
        {1, "AAA_2/A",        "normal",       1, "B/A/A/A", MOVED_HERE},
        {1, "AAA_3",          "normal",       1, "C/A/A", MOVED_HERE},
        {1, "AAA_3/A",        "normal",       1, "C/A/A/A", MOVED_HERE},
        {1, "A_moved",        "normal",       2, "A", MOVED_HERE},
        {1, "A_moved/A",      "normal",       2, "A/A", MOVED_HERE},
        {1, "B",              "base-deleted", NO_COPY_FROM, "A"},
        {1, "B/A",            "base-deleted", NO_COPY_FROM},
        {1, "BA_moved",       "normal",       1, "B/A", MOVED_HERE},
        {1, "BA_moved/A",     "normal",       1, "B/A/A", MOVED_HERE},
        {1, "BA_moved/A/A",   "normal",       1, "B/A/A/A", MOVED_HERE},
        {2, "A/A",            "normal",       1, "C/A", FALSE, "BA_moved", TRUE},
        {2, "A/A/A",          "normal",       1, "C/A/A", MOVED_HERE},
        {2, "A/A/A/A",        "normal",       1, "C/A/A/A", MOVED_HERE},
        {2, "BA_moved/A",     "base-deleted", NO_COPY_FROM, "AAA_2"},
        {2, "BA_moved/A/A",   "base-deleted", NO_COPY_FROM},
        {2, "C/A",            "base-deleted", NO_COPY_FROM, "A/A"},
        {3, "A/A/A",          "normal",       1, "D/A/A", FALSE, "AAA_3"},
        {3, "A/A/A/A",        "normal",       1, "D/A/A/A"},

        { 0 },
      };
      conflict_info_t conflicts[] = {
        {"A_moved/A/A", FALSE, FALSE, {svn_wc_conflict_action_delete,
                                       svn_wc_conflict_reason_moved_away, "A_moved/A/A"}},
        {"B",           FALSE, FALSE, {svn_wc_conflict_action_edit,
                                       svn_wc_conflict_reason_moved_away, "B"}},
        {"C/A",         FALSE, FALSE, {svn_wc_conflict_action_edit,
                                       svn_wc_conflict_reason_moved_away, "C/A"}},
        {"D/A/A",       FALSE, FALSE, {svn_wc_conflict_action_delete,
                                       svn_wc_conflict_reason_moved_away, "D/A/A"}},

        { 0 },
      };

      SVN_ERR(check_db_rows(&b, "", nodes));
      SVN_ERR(check_db_conflicts(&b, "", conflicts));
    }

    SVN_ERR(sbox_wc_resolve(&b, "B", svn_depth_empty,
        svn_wc_conflict_choose_mine_conflict));
    SVN_ERR(sbox_wc_resolve(&b, "C/A", svn_depth_empty,
        svn_wc_conflict_choose_mine_conflict));


    /* ### These can currently only be resolved to merged ???? */
    SVN_ERR(sbox_wc_resolve(&b, "D/A/A", svn_depth_empty,
                            svn_wc_conflict_choose_merged));
    SVN_ERR(sbox_wc_resolve(&b, "A/A/A", svn_depth_empty,
                            svn_wc_conflict_choose_merged));
    SVN_ERR(sbox_wc_resolve(&b, "A_moved/A/A", svn_depth_empty,
                            svn_wc_conflict_choose_merged));
    SVN_ERR(sbox_wc_resolve(&b, "A/A", svn_depth_empty,
                            svn_wc_conflict_choose_mine_conflict));
    SVN_ERR(sbox_wc_resolve(&b, "BA_moved/A", svn_depth_empty,
                            svn_wc_conflict_choose_merged));
    SVN_ERR(check_db_conflicts(&b, "", NULL));
    /* Go back to start position */
    SVN_ERR(sbox_wc_update(&b, "", 1));
    {
      nodes_row_t nodes[] = {
        {0, "",               "normal",       1, ""},
        {0, "A",              "normal",       1, "A"},
        {0, "A/A",            "normal",       1, "A/A"},
        {0, "A/A/A",          "normal",       1, "A/A/A"},
        {0, "A/A/A/A",        "normal",       1, "A/A/A/A"},
        {0, "B",              "normal",       1, "B"},
        {0, "B/A",            "normal",       1, "B/A"},
        {0, "B/A/A",          "normal",       1, "B/A/A"},
        {0, "B/A/A/A",        "normal",       1, "B/A/A/A"},
        {0, "C",              "normal",       1, "C"},
        {0, "C/A",            "normal",       1, "C/A"},
        {0, "C/A/A",          "normal",       1, "C/A/A"},
        {0, "C/A/A/A",        "normal",       1, "C/A/A/A"},
        {0, "D",              "normal",       1, "D"},
        {0, "D/A",            "normal",       1, "D/A"},
        {0, "D/A/A",          "normal",       1, "D/A/A"},
        {0, "D/A/A/A",        "normal",       1, "D/A/A/A"},
        {1, "A",              "normal",       2, "B", FALSE, "A_moved", TRUE},
        {1, "A/A",            "normal",       2, "B/A", MOVED_HERE},
        {1, "A/A/A",          "base-deleted", NO_COPY_FROM},
        {1, "A/A/A/A",        "base-deleted", NO_COPY_FROM},
        {1, "A_moved",        "normal",       2, "A", MOVED_HERE},
        {1, "A_moved/A",      "normal",       2, "A/A", MOVED_HERE},
        {1, "AAA_1",          "normal",       1, "A/A/A"},
        {1, "AAA_1/A",        "normal",       1, "A/A/A/A"},
        {1, "AAA_2",          "normal",       1, "B/A/A"},
        {1, "AAA_2/A",        "normal",       1, "B/A/A/A"},
        {1, "AAA_3",          "normal",       1, "C/A/A"},
        {1, "AAA_3/A",        "normal",       1, "C/A/A/A"},
        {1, "B",              "base-deleted", NO_COPY_FROM, "A"},
        {1, "B/A",            "base-deleted", NO_COPY_FROM},
        {1, "B/A/A",          "base-deleted", NO_COPY_FROM},
        {1, "B/A/A/A",        "base-deleted", NO_COPY_FROM},
        {1, "BA_moved",       "normal",       2, "B/A", MOVED_HERE},
        {2, "A/A",            "normal",       2, "C/A", FALSE, "BA_moved", TRUE},
        {2, "C/A",            "base-deleted", NO_COPY_FROM, "A/A"},
        {2, "C/A/A",          "base-deleted", NO_COPY_FROM},
        {2, "C/A/A/A",        "base-deleted", NO_COPY_FROM},
        {3, "A/A/A",          "normal",       1, "D/A/A"},
        {3, "A/A/A/A",        "normal",       1, "D/A/A/A"},

        { 0 },
      };

      conflict_info_t conflicts[] = {
        {"A", FALSE, FALSE,   { svn_wc_conflict_action_edit,
                                svn_wc_conflict_reason_replaced}},
        {"B", FALSE, FALSE,   { svn_wc_conflict_action_edit,
                                svn_wc_conflict_reason_moved_away, "B"}},
        {"C/A", FALSE, FALSE, { svn_wc_conflict_action_edit,
                                svn_wc_conflict_reason_moved_away, "C/A"}},
        {0}
      };

      SVN_ERR(check_db_rows(&b, "", nodes));
      SVN_ERR(check_db_conflicts(&b, "", conflicts));
    }

  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(sbox_wc_resolve(&b, "B", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(sbox_wc_resolve(&b, "C/A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    conflict_info_t conflicts[] = {
      {"A/A",  FALSE, FALSE,  { svn_wc_conflict_action_edit,
                                svn_wc_conflict_reason_moved_away, "A/A"}},
      {"A/A/A", FALSE, FALSE, { svn_wc_conflict_action_add,
                                svn_wc_conflict_reason_added}},
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(sbox_wc_resolve(&b, "A/A/A", svn_depth_empty,
                          svn_wc_conflict_choose_merged));
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
  /* Update and resolve via their strategy */
  SVN_ERR(sbox_wc_update(&b, "", 2));
  SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_merged));

  return SVN_NO_ERROR;
}

static svn_error_t *
move4_update_replaceself_AAA(const svn_test_opts_t *opts, apr_pool_t *pool)
{
    svn_test__sandbox_t b;

    SVN_ERR(init_move4(&b, "move4_update_replaceself_AAA", opts, TRUE, pool));

    /* Update and resolve via mine strategy */
    SVN_ERR(sbox_wc_update(&b, "", 2));
    SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
    /* Go back to start position */
    SVN_ERR(sbox_wc_update(&b, "", 1));
    SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_mine_conflict));
    /* Update and resolve via their strategy */
    SVN_ERR(sbox_wc_update(&b, "", 2));
    SVN_ERR(sbox_wc_resolve(&b, "", svn_depth_infinity, svn_wc_conflict_choose_merged));

    return SVN_NO_ERROR;
}

static svn_error_t *
simple_move_bump(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "simple_move_bump", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));

  SVN_ERR(sbox_wc_propset(&b, "old_A", "val", "A"));
  SVN_ERR(sbox_wc_propset(&b, "old_B", "val", "A/B"));

  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_propset(&b, "new_A", "val", "A"));
  SVN_ERR(sbox_wc_propset(&b, "new_B", "val", "A/B"));

  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A/B", "A/B_mv"));
  SVN_ERR(sbox_wc_move(&b, "A", "A_mv"));

  {
    nodes_row_t nodes[] = {

      { 0, "",          "normal",       1, ""},
      { 0, "A",         "normal",       1, "A", NOT_MOVED, "old_A"},
      { 0, "A/B",       "normal",       1, "A/B", NOT_MOVED, "old_B"},

      { 1, "A",         "base-deleted", NO_COPY_FROM, "A_mv"},
      { 1, "A/B",       "base-deleted", NO_COPY_FROM},

      { 1, "A_mv",      "normal",       1, "A", MOVED_HERE, "old_A" },
      { 1, "A_mv/B",    "normal",       1, "A/B", MOVED_HERE, "old_B" },

      { 2, "A_mv/B",    "base-deleted", NO_COPY_FROM, "A_mv/B_mv" },
      { 2, "A_mv/B_mv", "normal",       1, "A/B", FALSE, NULL, TRUE, "old_B" },

      { 0 },
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "", 2));

  /* Expect the A tree to be updated */
  {
    nodes_row_t nodes[] = {

      { 0, "",          "normal",       2, ""},
      { 0, "A",         "normal",       2, "A", NOT_MOVED, "new_A,old_A"},
      { 0, "A/B",       "normal",       2, "A/B", NOT_MOVED, "new_B,old_B"},

      { 1, "A",         "base-deleted", NO_COPY_FROM, "A_mv"},
      { 1, "A/B",       "base-deleted", NO_COPY_FROM},

      { 1, "A_mv",      "normal",       1, "A", MOVED_HERE, "old_A" },
      { 1, "A_mv/B",    "normal",       1, "A/B", MOVED_HERE, "old_B" },

      { 2, "A_mv/B",    "base-deleted", NO_COPY_FROM, "A_mv/B_mv" },
      { 2, "A_mv/B_mv", "normal",       1, "A/B", FALSE, NULL, TRUE, "old_B" },

      { 0 },
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));

  {
    nodes_row_t nodes[] = {

      { 0, "",          "normal",       2, ""},
      { 0, "A",         "normal",       2, "A", NOT_MOVED, "new_A,old_A"},
      { 0, "A/B",       "normal",       2, "A/B", NOT_MOVED, "new_B,old_B"},

      { 1, "A",         "base-deleted", NO_COPY_FROM, "A_mv"},
      { 1, "A/B",       "base-deleted", NO_COPY_FROM},

      { 1, "A_mv",      "normal",       2, "A", MOVED_HERE, "new_A,old_A" },
      { 1, "A_mv/B",    "normal",       2, "A/B", MOVED_HERE, "new_B,old_B" },

      { 2, "A_mv/B",    "base-deleted", NO_COPY_FROM, "A_mv/B_mv" },
      { 2, "A_mv/B_mv", "normal",       1, "A/B", FALSE, NULL, TRUE, "old_B" },

      { 0 },
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_resolve(&b, "A_mv/B", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));

  {
    nodes_row_t nodes[] = {

      { 0, "",          "normal",       2, ""},
      { 0, "A",         "normal",       2, "A", NOT_MOVED, "new_A,old_A"},
      { 0, "A/B",       "normal",       2, "A/B", NOT_MOVED, "new_B,old_B"},

      { 1, "A",         "base-deleted", NO_COPY_FROM, "A_mv"},
      { 1, "A/B",       "base-deleted", NO_COPY_FROM},

      { 1, "A_mv",      "normal",       2, "A", MOVED_HERE, "new_A,old_A" },
      { 1, "A_mv/B",    "normal",       2, "A/B", MOVED_HERE, "new_B,old_B" },

      { 2, "A_mv/B",    "base-deleted", NO_COPY_FROM, "A_mv/B_mv" },
      { 2, "A_mv/B_mv", "normal",       2, "A/B", FALSE, NULL, TRUE, "new_B,old_B" },

      { 0 },
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
movedhere_extract_retract(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "movedhere_extract_retract",
                                   opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B1"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B2"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B3"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/C1"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/C2"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/C3"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/D1"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/D2"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/D3"));

  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_propset(&b, "k", "v", "A/B1"));
  SVN_ERR(sbox_wc_propset(&b, "k", "v", "A/B2"));
  SVN_ERR(sbox_wc_propset(&b, "k", "v", "A/B3"));
  SVN_ERR(sbox_wc_delete(&b, "A/C1"));
  SVN_ERR(sbox_wc_delete(&b, "A/C2"));
  SVN_ERR(sbox_wc_delete(&b, "A/C3"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/E1"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/E2"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/E3"));

  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "A", "Z"));

  SVN_ERR(sbox_wc_delete(&b, "Z/B1"));
  SVN_ERR(sbox_wc_delete(&b, "Z/C1"));
  SVN_ERR(sbox_wc_delete(&b, "Z/D1"));

  SVN_ERR(sbox_wc_move(&b, "Z/B2", "B2"));
  SVN_ERR(sbox_wc_move(&b, "Z/C2", "C2"));
  SVN_ERR(sbox_wc_move(&b, "Z/D2", "D2"));

  SVN_ERR(sbox_wc_mkdir(&b, "Z/B2"));
  SVN_ERR(sbox_wc_mkdir(&b, "Z/C2"));
  SVN_ERR(sbox_wc_mkdir(&b, "Z/D2"));
  SVN_ERR(sbox_wc_mkdir(&b, "Z/E2"));

  SVN_ERR(sbox_wc_update(&b, "", 2));
  {
    conflict_info_t conflicts[] = {
      {"A",    FALSE, FALSE, {svn_wc_conflict_action_edit,
                              svn_wc_conflict_reason_moved_away, "A"}},
      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  {
    conflict_info_t conflicts[] = {
      {"Z/B1", FALSE, FALSE, {svn_wc_conflict_action_edit,
                              svn_wc_conflict_reason_deleted}},
      {"Z/B2", FALSE, FALSE, {svn_wc_conflict_action_edit,
                              svn_wc_conflict_reason_moved_away, "Z/B2"}},
      {"Z/C1", FALSE, FALSE, {svn_wc_conflict_action_delete,
                              svn_wc_conflict_reason_deleted}},
      {"Z/C2", FALSE, FALSE, {svn_wc_conflict_action_delete,
                              svn_wc_conflict_reason_moved_away, "Z/C2"}},
      {"Z/E2", FALSE, FALSE, {svn_wc_conflict_action_add,
                              svn_wc_conflict_reason_added}},

      {0}
    };

    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }
  SVN_ERR(sbox_wc_resolve(&b, "Z/B1", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(sbox_wc_resolve(&b, "Z/B2", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));

  SVN_ERR(sbox_wc_resolve(&b, "Z/C1", svn_depth_empty,
                          svn_wc_conflict_choose_merged));
  SVN_ERR(sbox_wc_resolve(&b, "Z/C2", svn_depth_empty,
                          svn_wc_conflict_choose_merged));

  SVN_ERR(sbox_wc_resolve(&b, "Z/E2", svn_depth_empty,
                          svn_wc_conflict_choose_merged));

  SVN_ERR(check_db_conflicts(&b, "", NULL));
  {
    nodes_row_t nodes[] = {

      {0, "",       "normal",       2, ""},
      {0, "A",      "normal",       2, "A"},
      {0, "A/B1",   "normal",       2, "A/B1", FALSE, NULL, FALSE, "k"},
      {0, "A/B2",   "normal",       2, "A/B2", FALSE, NULL, FALSE, "k"},
      {0, "A/B3",   "normal",       2, "A/B3", FALSE, NULL, FALSE, "k"},
      {0, "A/D1",   "normal",       2, "A/D1"},
      {0, "A/D2",   "normal",       2, "A/D2"},
      {0, "A/D3",   "normal",       2, "A/D3"},
      {0, "A/E1",   "normal",       2, "A/E1"},
      {0, "A/E2",   "normal",       2, "A/E2"},
      {0, "A/E3",   "normal",       2, "A/E3"},

      {1, "A",      "base-deleted", NO_COPY_FROM, "Z"},
      {1, "A/B1",   "base-deleted", NO_COPY_FROM},
      {1, "A/B2",   "base-deleted", NO_COPY_FROM},
      {1, "A/B3",   "base-deleted", NO_COPY_FROM},

      {1, "A/D1",   "base-deleted", NO_COPY_FROM},
      {1, "A/D2",   "base-deleted", NO_COPY_FROM},
      {1, "A/D3",   "base-deleted", NO_COPY_FROM},

      {1, "A/E1",   "base-deleted", NO_COPY_FROM},
      {1, "A/E2",   "base-deleted", NO_COPY_FROM},
      {1, "A/E3",   "base-deleted", NO_COPY_FROM},

      {1, "B2",     "normal",       2, "A/B2", MOVED_HERE, "k"},
      {1, "C2",     "normal",       1, "A/C2"},
      {1, "D2",     "normal",       1, "A/D2", MOVED_HERE},

      {1, "Z",      "normal",       2, "A", MOVED_HERE},
      {1, "Z/B1",   "normal",       2, "A/B1", MOVED_HERE, "k"},
      {1, "Z/B2",   "normal",       2, "A/B2", MOVED_HERE, "k"},
      {1, "Z/B3",   "normal",       2, "A/B3", MOVED_HERE, "k"},
      {1, "Z/D1",   "normal",       2, "A/D1", MOVED_HERE},
      {1, "Z/D2",   "normal",       2, "A/D2", MOVED_HERE},
      {1, "Z/D3",   "normal",       2, "A/D3", MOVED_HERE},
      {1, "Z/E1",   "normal",       2, "A/E1", MOVED_HERE},
      {1, "Z/E2",   "normal",       2, "A/E2", MOVED_HERE},
      {1, "Z/E3",   "normal",       2, "A/E3", MOVED_HERE},

      {2, "Z/B2",   "normal",       NO_COPY_FROM, "B2"},
      {2, "Z/C2",   "normal",       NO_COPY_FROM},
      {2, "Z/D2",   "normal",       NO_COPY_FROM, "D2"},
      {2, "Z/E2",   "normal",       NO_COPY_FROM},

      {2, "Z/B1",   "base-deleted", NO_COPY_FROM},
      {2, "Z/D1",   "base-deleted", NO_COPY_FROM},

      { 0 },
    };
    SVN_ERR(check_db_rows(&b, "", nodes));

    SVN_ERR(check_db_conflicts(&b, "", NULL));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
repo_wc_copy(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  const char *new_repos_dir;
  const char *new_repos_url;

  SVN_ERR(svn_test__sandbox_create(&b, "repo_wc_copy",
                                   opts, pool));
  SVN_ERR(sbox_add_and_commit_greek_tree(&b));

  SVN_ERR(sbox_wc_copy_url(&b,
                           svn_path_url_add_component2(b.repos_url, "A/B",
                                                       pool),
                           -1, "AA"));

  {
    nodes_row_t nodes[] = {

      {1, "AA/lambda",   "normal", 1, "A/B/lambda"},
      {1, "AA",          "normal", 1, "A/B"},
      {1, "AA/E/beta",   "normal", 1, "A/B/E/beta"},
      {1, "AA/E/alpha",  "normal", 1, "A/B/E/alpha"},
      {1, "AA/F",        "normal", 1, "A/B/F"},
      {1, "AA/E",        "normal", 1, "A/B/E"},

      { 0 },
    };
    SVN_ERR(check_db_rows(&b, "AA", nodes));
  }

  new_repos_dir = apr_pstrcat(pool, b.repos_dir, "-2", SVN_VA_NULL);
  new_repos_url = apr_pstrcat(pool, b.repos_url, "-2", SVN_VA_NULL);

  svn_test_add_dir_cleanup(new_repos_dir);

  SVN_ERR(svn_io_remove_dir2(new_repos_dir, TRUE, NULL, NULL, pool));
  SVN_ERR(svn_io_copy_dir_recursively(b.repos_dir,
                                      svn_dirent_dirname(new_repos_dir, pool),
                                      svn_dirent_basename(new_repos_dir, pool),
                                      FALSE, NULL, NULL, pool));

  SVN_ERR(sbox_wc_relocate(&b, new_repos_url));

  /* This produced an invalid copy in Subversion <= 1.8.8.
     Status would show all descendants as incomplete */
  SVN_ERR(sbox_wc_copy_url(&b,
                           svn_path_url_add_component2(b.repos_url, "A/B",
                                                       pool),
                           -1, "BB"));

  {
    nodes_row_t nodes[] = {

      {1, "BB/lambda",   "normal", 1, "A/B/lambda"},
      {1, "BB",          "normal", 1, "A/B"},
      {1, "BB/E/beta",   "normal", 1, "A/B/E/beta"},
      {1, "BB/E/alpha",  "normal", 1, "A/B/E/alpha"},
      {1, "BB/F",        "normal", 1, "A/B/F"},
      {1, "BB/E",        "normal", 1, "A/B/E"},

      { 0 },
    };
    SVN_ERR(check_db_rows(&b, "BB", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
break_move_in_delete(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "break_move_in_delete", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "X"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/Y"));
  SVN_ERR(sbox_wc_mkdir(&b, "X/Y/Z"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_propset(&b, "key", "value", "X/Y/Z"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "X/Y/Z", "A/Z"));
  SVN_ERR(sbox_wc_delete(&b, "X"));
  {
    nodes_row_t nodes[] = {
      {0, "",      "normal",       1, ""},
      {0, "A",     "normal",       1, "A"},
      {0, "A/B",   "normal",       1, "A/B"},
      {0, "X",     "normal",       1, "X"},
      {0, "X/Y",   "normal",       1, "X/Y"},
      {0, "X/Y/Z", "normal",       1, "X/Y/Z"},
      {1, "X",     "base-deleted", NO_COPY_FROM},
      {1, "X/Y",   "base-deleted", NO_COPY_FROM},
      {1, "X/Y/Z", "base-deleted", NO_COPY_FROM, "A/Z"},
      {2, "A/Z",   "normal",       1, "X/Y/Z", MOVED_HERE},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(sbox_wc_update(&b, "", 2));
  {
    nodes_row_t nodes[] = {
      {0, "",      "normal",       2, ""},
      {0, "A",     "normal",       2, "A"},
      {0, "A/B",   "normal",       2, "A/B"},
      {0, "X",     "normal",       2, "X"},
      {0, "X/Y",   "normal",       2, "X/Y"},
      {0, "X/Y/Z", "normal",       2, "X/Y/Z"},
      {1, "X",     "base-deleted", NO_COPY_FROM},
      {1, "X/Y",   "base-deleted", NO_COPY_FROM},
      {1, "X/Y/Z", "base-deleted", NO_COPY_FROM, "A/Z"},
      {2, "A/Z",   "normal",       1, "X/Y/Z", MOVED_HERE},
      {0}
    };
    conflict_info_t conflicts1[] = {
      {"X",     FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_deleted}},
      {0}
    };
    conflict_info_t conflicts2[] = {
      {"X/Y/Z", FALSE, FALSE, {svn_wc_conflict_action_edit,
                               svn_wc_conflict_reason_moved_away, "X"}},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
    SVN_ERR(check_db_conflicts(&b, "", conflicts1));
    SVN_ERR(sbox_wc_resolve(&b, "X", svn_depth_empty,
                            svn_wc_conflict_choose_mine_conflict));
    SVN_ERR(check_db_rows(&b, "", nodes));
    SVN_ERR(check_db_conflicts(&b, "", conflicts2));
  }

  SVN_ERR(sbox_wc_resolved(&b, "X/Y/Z"));
  {
    nodes_row_t nodes[] = {
      {0, "",      "normal",       2, ""},
      {0, "A",     "normal",       2, "A"},
      {0, "A/B",   "normal",       2, "A/B"},
      {0, "X",     "normal",       2, "X"},
      {0, "X/Y",   "normal",       2, "X/Y"},
      {0, "X/Y/Z", "normal",       2, "X/Y/Z"},
      {1, "X",     "base-deleted", NO_COPY_FROM},
      {1, "X/Y",   "base-deleted", NO_COPY_FROM},
      {1, "X/Y/Z", "base-deleted", NO_COPY_FROM},
      {2, "A/Z",   "normal",       1, "X/Y/Z"},
      {0}
    };
    SVN_ERR(check_db_rows(&b, "", nodes));
    SVN_ERR(check_db_conflicts(&b, "", NULL));
  }

  return SVN_NO_ERROR;
}


static svn_error_t *
nested_move_delete(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "nested_move_delete", opts, pool));

  SVN_ERR(sbox_add_and_commit_greek_tree(&b));

  SVN_ERR(sbox_wc_mkdir(&b, "A/Z"));
  SVN_ERR(sbox_wc_move(&b, "A/B/lambda", "A/Z/lambda"));

  SVN_ERR(sbox_wc_delete(&b, "A/B"));

  {
    nodes_row_t nodes_AB[] = {
      {0, "A/B",          "normal",       1, "A/B"},
      {2, "A/B",          "base-deleted", NO_COPY_FROM},
      {0, "A/B/E",        "normal",       1, "A/B/E"},
      {2, "A/B/E",        "base-deleted", NO_COPY_FROM},
      {0, "A/B/E/alpha",  "normal",       1, "A/B/E/alpha"},
      {2, "A/B/E/alpha",  "base-deleted", NO_COPY_FROM},
      {0, "A/B/E/beta",   "normal",       1, "A/B/E/beta"},
      {2, "A/B/E/beta",   "base-deleted", NO_COPY_FROM},
      {0, "A/B/F",        "normal",       1, "A/B/F"},
      {2, "A/B/F",        "base-deleted", NO_COPY_FROM},
      {0, "A/B/lambda",   "normal",       1, "A/B/lambda"},
      {2, "A/B/lambda",   "base-deleted", NO_COPY_FROM, "A/Z/lambda"},
      {0}
    };
    nodes_row_t nodes_AZ[] = {
      {2, "A/Z",          "normal",       NO_COPY_FROM},
      {3, "A/Z/lambda",   "normal",       1, "A/B/lambda", MOVED_HERE },
      {0}
    };

    SVN_ERR(check_db_rows(&b, "A/B", nodes_AB));
    SVN_ERR(check_db_rows(&b, "A/Z", nodes_AZ));
  }

  SVN_ERR(sbox_wc_move(&b, "A", "A_moved"));

  {
    nodes_row_t nodes[] = {
      {0, "",                   "normal",       0, ""},
      {0, "A",                  "normal",       1, "A"},
      {1, "A",                  "base-deleted", NO_COPY_FROM, "A_moved"},
      {0, "A/B",                "normal",       1, "A/B"},
      {1, "A/B",                "base-deleted", NO_COPY_FROM},
      {0, "A/B/E",              "normal",       1, "A/B/E"},
      {1, "A/B/E",              "base-deleted", NO_COPY_FROM},
      {0, "A/B/E/alpha",        "normal",       1, "A/B/E/alpha"},
      {1, "A/B/E/alpha",        "base-deleted", NO_COPY_FROM},
      {0, "A/B/E/beta",         "normal",       1, "A/B/E/beta"},
      {1, "A/B/E/beta",         "base-deleted", NO_COPY_FROM},
      {0, "A/B/F",              "normal",       1, "A/B/F"},
      {1, "A/B/F",              "base-deleted", NO_COPY_FROM},
      {0, "A/B/lambda",         "normal",       1, "A/B/lambda"},
      {1, "A/B/lambda",         "base-deleted", NO_COPY_FROM},
      {0, "A/C",                "normal",       1, "A/C"},
      {1, "A/C",                "base-deleted", NO_COPY_FROM},
      {0, "A/D",                "normal",       1, "A/D"},
      {1, "A/D",                "base-deleted", NO_COPY_FROM},
      {0, "A/D/G",              "normal",       1, "A/D/G"},
      {1, "A/D/G",              "base-deleted", NO_COPY_FROM},
      {0, "A/D/G/pi",           "normal",       1, "A/D/G/pi"},
      {1, "A/D/G/pi",           "base-deleted", NO_COPY_FROM},
      {0, "A/D/G/rho",          "normal",       1, "A/D/G/rho"},
      {1, "A/D/G/rho",          "base-deleted", NO_COPY_FROM},
      {0, "A/D/G/tau",          "normal",       1, "A/D/G/tau"},
      {1, "A/D/G/tau",          "base-deleted", NO_COPY_FROM},
      {0, "A/D/H",              "normal",       1, "A/D/H"},
      {1, "A/D/H",              "base-deleted", NO_COPY_FROM},
      {0, "A/D/H/chi",          "normal",       1, "A/D/H/chi"},
      {1, "A/D/H/chi",          "base-deleted", NO_COPY_FROM},
      {0, "A/D/H/omega",        "normal",       1, "A/D/H/omega"},
      {1, "A/D/H/omega",        "base-deleted", NO_COPY_FROM},
      {0, "A/D/H/psi",          "normal",       1, "A/D/H/psi"},
      {1, "A/D/H/psi",          "base-deleted", NO_COPY_FROM},
      {0, "A/D/gamma",          "normal",       1, "A/D/gamma"},
      {1, "A/D/gamma",          "base-deleted", NO_COPY_FROM},
      {0, "A/mu",               "normal",       1, "A/mu"},
      {1, "A/mu",               "base-deleted", NO_COPY_FROM},
      {1, "A_moved",            "normal",       1, "A", MOVED_HERE},
      {1, "A_moved/B",          "normal",       1, "A/B", MOVED_HERE},
      {2, "A_moved/B",          "base-deleted", NO_COPY_FROM},
      {2, "A_moved/B/E",        "base-deleted", NO_COPY_FROM},
      {1, "A_moved/B/E",        "normal",       1, "A/B/E", MOVED_HERE},
      {1, "A_moved/B/E/alpha",  "normal",       1, "A/B/E/alpha", MOVED_HERE},
      {2, "A_moved/B/E/alpha",  "base-deleted", NO_COPY_FROM},
      {1, "A_moved/B/E/beta",   "normal",       1, "A/B/E/beta", MOVED_HERE},
      {2, "A_moved/B/E/beta",   "base-deleted", NO_COPY_FROM},
      {1, "A_moved/B/F",        "normal",       1, "A/B/F", MOVED_HERE},
      {2, "A_moved/B/F",        "base-deleted", NO_COPY_FROM},
      {1, "A_moved/B/lambda",   "normal",       1, "A/B/lambda", MOVED_HERE},
      {2, "A_moved/B/lambda",   "base-deleted", NO_COPY_FROM, "A_moved/Z/lambda"},
      {1, "A_moved/C",          "normal",       1, "A/C", MOVED_HERE},
      {1, "A_moved/D",          "normal",       1, "A/D", MOVED_HERE},
      {1, "A_moved/D/G",        "normal",       1, "A/D/G", MOVED_HERE},
      {1, "A_moved/D/G/pi",     "normal",       1, "A/D/G/pi", MOVED_HERE},
      {1, "A_moved/D/G/rho",    "normal",       1, "A/D/G/rho", MOVED_HERE},
      {1, "A_moved/D/G/tau",    "normal",       1, "A/D/G/tau", MOVED_HERE},
      {1, "A_moved/D/H",        "normal",       1, "A/D/H", MOVED_HERE},
      {1, "A_moved/D/H/chi",    "normal",       1, "A/D/H/chi", MOVED_HERE},
      {1, "A_moved/D/H/omega",  "normal",       1, "A/D/H/omega", MOVED_HERE},
      {1, "A_moved/D/H/psi",    "normal",       1, "A/D/H/psi", MOVED_HERE},
      {1, "A_moved/D/gamma",    "normal",       1, "A/D/gamma", MOVED_HERE},
      {2, "A_moved/Z",          "normal",       NO_COPY_FROM},
      {3, "A_moved/Z/lambda",   "normal",       1, "A/B/lambda", MOVED_HERE},
      {1, "A_moved/mu",         "normal",       1, "A/mu", MOVED_HERE},
      {0, "iota", "normal",  1, "iota"},

      {0}
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_within_mixed_move(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_within_mixed_move", opts, pool));

  SVN_ERR(sbox_add_and_commit_greek_tree(&b));

  SVN_ERR(sbox_wc_delete(&b, "iota"));
  SVN_ERR(sbox_wc_commit(&b, ""));

  /* Make A mixed revision */
  SVN_ERR(sbox_wc_update(&b, "A/B/E", 2));

  /* Single rev moves.. ok */
  SVN_ERR(sbox_wc_move(&b, "A/D", "A/D_mv"));
  SVN_ERR(sbox_wc_move(&b, "A/C", "C_mv"));

  {
    nodes_row_t nodes[] = {
      {0, "",                 "normal",       0, ""},
      {0, "A",                "normal",       1, "A"},
      {0, "A/B",              "normal",       1, "A/B"},
      {0, "A/B/E",            "normal",       2, "A/B/E"},
      {0, "A/B/E/alpha",      "normal",       2, "A/B/E/alpha"},
      {0, "A/B/E/beta",       "normal",       2, "A/B/E/beta"},
      {0, "A/B/F",            "normal",       1, "A/B/F"},
      {0, "A/B/lambda",       "normal",       1, "A/B/lambda"},
      {0, "A/C",              "normal",       1, "A/C"},
      {0, "A/D",              "normal",       1, "A/D"},
      {0, "A/D/G",            "normal",       1, "A/D/G"},
      {0, "A/D/G/pi",         "normal",       1, "A/D/G/pi"},
      {0, "A/D/G/rho",        "normal",       1, "A/D/G/rho"},
      {0, "A/D/G/tau",        "normal",       1, "A/D/G/tau"},
      {0, "A/D/gamma",        "normal",       1, "A/D/gamma"},
      {0, "A/D/H",            "normal",       1, "A/D/H"},
      {0, "A/D/H/chi",        "normal",       1, "A/D/H/chi"},
      {0, "A/D/H/omega",      "normal",       1, "A/D/H/omega"},
      {0, "A/D/H/psi",        "normal",       1, "A/D/H/psi"},
      {0, "A/mu",             "normal",       1, "A/mu"},
      {0, "iota",             "not-present",  2, "iota"},
      {1, "C_mv",             "normal",       1, "A/C", MOVED_HERE},
      {2, "A/C",              "base-deleted", NO_COPY_FROM, "C_mv"},
      {2, "A/D",              "base-deleted", NO_COPY_FROM, "A/D_mv"},
      {2, "A/D/G",            "base-deleted", NO_COPY_FROM},
      {2, "A/D/G/pi",         "base-deleted", NO_COPY_FROM},
      {2, "A/D/G/rho",        "base-deleted", NO_COPY_FROM},
      {2, "A/D/G/tau",        "base-deleted", NO_COPY_FROM},
      {2, "A/D/gamma",        "base-deleted", NO_COPY_FROM},
      {2, "A/D/H",            "base-deleted", NO_COPY_FROM},
      {2, "A/D/H/chi",        "base-deleted", NO_COPY_FROM},
      {2, "A/D/H/omega",      "base-deleted", NO_COPY_FROM},
      {2, "A/D/H/psi",        "base-deleted", NO_COPY_FROM},
      {2, "A/D_mv",           "normal",       1, "A/D", MOVED_HERE},
      {2, "A/D_mv/G",         "normal",       1, "A/D/G", MOVED_HERE},
      {2, "A/D_mv/G/pi",      "normal",       1, "A/D/G/pi", MOVED_HERE},
      {2, "A/D_mv/G/rho",     "normal",       1, "A/D/G/rho", MOVED_HERE},
      {2, "A/D_mv/G/tau",     "normal",       1, "A/D/G/tau", MOVED_HERE},
      {2, "A/D_mv/gamma",     "normal",       1, "A/D/gamma", MOVED_HERE},
      {2, "A/D_mv/H",         "normal",       1, "A/D/H", MOVED_HERE},
      {2, "A/D_mv/H/chi",     "normal",       1, "A/D/H/chi", MOVED_HERE},
      {2, "A/D_mv/H/omega",   "normal",       1, "A/D/H/omega", MOVED_HERE},
      {2, "A/D_mv/H/psi",     "normal",       1, "A/D/H/psi", MOVED_HERE},
      {0}
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  /* Mixed rev move... breaks recordings "A/D" -> "A/D_mv"  */
  SVN_ERR(sbox_wc_move(&b, "A", "A_mv"));

  {
    nodes_row_t nodes[] = {
      {0, "",                 "normal",       0, ""},
      {0, "A",                "normal",       1, "A"},
      {0, "A/B",              "normal",       1, "A/B"},
      {0, "A/B/E",            "normal",       2, "A/B/E"},
      {0, "A/B/E/alpha",      "normal",       2, "A/B/E/alpha"},
      {0, "A/B/E/beta",       "normal",       2, "A/B/E/beta"},
      {0, "A/B/F",            "normal",       1, "A/B/F"},
      {0, "A/B/lambda",       "normal",       1, "A/B/lambda"},
      {0, "A/C",              "normal",       1, "A/C"},
      {0, "A/D",              "normal",       1, "A/D"},
      {0, "A/D/G",            "normal",       1, "A/D/G"},
      {0, "A/D/G/pi",         "normal",       1, "A/D/G/pi"},
      {0, "A/D/G/rho",        "normal",       1, "A/D/G/rho"},
      {0, "A/D/G/tau",        "normal",       1, "A/D/G/tau"},
      {0, "A/D/gamma",        "normal",       1, "A/D/gamma"},
      {0, "A/D/H",            "normal",       1, "A/D/H"},
      {0, "A/D/H/chi",        "normal",       1, "A/D/H/chi"},
      {0, "A/D/H/omega",      "normal",       1, "A/D/H/omega"},
      {0, "A/D/H/psi",        "normal",       1, "A/D/H/psi"},
      {0, "A/mu",             "normal",       1, "A/mu"},
      {0, "iota",             "not-present",  2, "iota"},
      {1, "A",                "base-deleted", NO_COPY_FROM },
      {1, "A/B",              "base-deleted", NO_COPY_FROM },
      {1, "A/B/E",            "base-deleted", NO_COPY_FROM },
      {1, "A/B/E/alpha",      "base-deleted", NO_COPY_FROM },
      {1, "A/B/E/beta",       "base-deleted", NO_COPY_FROM },
      {1, "A/B/F",            "base-deleted", NO_COPY_FROM },
      {1, "A/B/lambda",       "base-deleted", NO_COPY_FROM },
      {1, "A/C",              "base-deleted", NO_COPY_FROM, "C_mv"},
      {1, "A/D",              "base-deleted", NO_COPY_FROM, "A/D_mv" },
      {1, "A/D/G",            "base-deleted", NO_COPY_FROM },
      {1, "A/D/G/pi",         "base-deleted", NO_COPY_FROM },
      {1, "A/D/G/rho",        "base-deleted", NO_COPY_FROM },
      {1, "A/D/G/tau",        "base-deleted", NO_COPY_FROM },
      {1, "A/D/gamma",        "base-deleted", NO_COPY_FROM },
      {1, "A/D/H",            "base-deleted", NO_COPY_FROM },
      {1, "A/D/H/chi",        "base-deleted", NO_COPY_FROM },
      {1, "A/D/H/omega",      "base-deleted", NO_COPY_FROM },
      {1, "A/D/H/psi",        "base-deleted", NO_COPY_FROM },
      {1, "A/mu",             "base-deleted", NO_COPY_FROM },
      {1, "A_mv",             "normal",       1, "A"},
      {1, "A_mv/B",           "normal",       1, "A/B"},
      {1, "A_mv/B/E",         "not-present",  2, "A/B/E"},
      {1, "A_mv/B/F",         "normal",       1, "A/B/F"},
      {1, "A_mv/B/lambda",    "normal",       1, "A/B/lambda"},
      {1, "A_mv/C",           "normal",       1, "A/C"},
      {1, "A_mv/D",           "normal",       1, "A/D"},
      {1, "A_mv/D/G",         "normal",       1, "A/D/G"},
      {1, "A_mv/D/G/pi",      "normal",       1, "A/D/G/pi"},
      {1, "A_mv/D/G/rho",     "normal",       1, "A/D/G/rho"},
      {1, "A_mv/D/G/tau",     "normal",       1, "A/D/G/tau"},
      {1, "A_mv/D/gamma",     "normal",       1, "A/D/gamma"},
      {1, "A_mv/D/H",         "normal",       1, "A/D/H"},
      {1, "A_mv/D/H/chi",     "normal",       1, "A/D/H/chi"},
      {1, "A_mv/D/H/omega",   "normal",       1, "A/D/H/omega"},
      {1, "A_mv/D/H/psi",     "normal",       1, "A/D/H/psi"},
      {1, "A_mv/mu",          "normal",       1, "A/mu"},
      {1, "C_mv",             "normal",       1, "A/C", MOVED_HERE},
      {2, "A_mv/C",           "base-deleted", NO_COPY_FROM },
      {2, "A_mv/D",           "base-deleted", NO_COPY_FROM },
      {2, "A_mv/D/G",         "base-deleted", NO_COPY_FROM },
      {2, "A_mv/D/G/pi",      "base-deleted", NO_COPY_FROM },
      {2, "A_mv/D/G/rho",     "base-deleted", NO_COPY_FROM },
      {2, "A_mv/D/G/tau",     "base-deleted", NO_COPY_FROM },
      {2, "A_mv/D/gamma",     "base-deleted", NO_COPY_FROM },
      {2, "A_mv/D/H",         "base-deleted", NO_COPY_FROM },
      {2, "A_mv/D/H/chi",     "base-deleted", NO_COPY_FROM },
      {2, "A_mv/D/H/omega",   "base-deleted", NO_COPY_FROM },
      {2, "A_mv/D/H/psi",     "base-deleted", NO_COPY_FROM },
      {2, "A_mv/D_mv",        "normal",       1, "A/D", MOVED_HERE},
      {2, "A_mv/D_mv/G",      "normal",       1, "A/D/G", MOVED_HERE},
      {2, "A_mv/D_mv/G/pi",   "normal",       1, "A/D/G/pi", MOVED_HERE},
      {2, "A_mv/D_mv/G/rho",  "normal",       1, "A/D/G/rho", MOVED_HERE},
      {2, "A_mv/D_mv/G/tau",  "normal",       1, "A/D/G/tau", MOVED_HERE},
      {2, "A_mv/D_mv/gamma",  "normal",       1, "A/D/gamma", MOVED_HERE},
      {2, "A_mv/D_mv/H",      "normal",       1, "A/D/H", MOVED_HERE},
      {2, "A_mv/D_mv/H/chi",  "normal",       1, "A/D/H/chi", MOVED_HERE},
      {2, "A_mv/D_mv/H/omega","normal",       1, "A/D/H/omega", MOVED_HERE},
      {2, "A_mv/D_mv/H/psi",  "normal",       1, "A/D/H/psi", MOVED_HERE},
      {3, "A_mv/B/E",         "normal",       2, "A/B/E"},
      {3, "A_mv/B/E/alpha",   "normal",       2, "A/B/E/alpha"},
      {3, "A_mv/B/E/beta",    "normal",       2, "A/B/E/beta"},

      {0}
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_edit_obstruction(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_edit_obstruction", opts, pool));

  SVN_ERR(sbox_add_and_commit_greek_tree(&b));

  SVN_ERR(sbox_file_write(&b, "A/B/E/alpha", "Update alpha"));
  SVN_ERR(sbox_wc_propset(&b, "a", "b", "A/B/F"));
  SVN_ERR(sbox_wc_commit(&b, "")); /* r2 */

  SVN_ERR(sbox_wc_update(&b, "", 1));

  /* Simple move */
  SVN_ERR(sbox_wc_move(&b, "A", "A_mv"));
  SVN_ERR(sbox_wc_update(&b, "", 2));
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));

  {
    nodes_row_t nodes[] = {
      {1, "A_mv",             "normal",       2, "A", MOVED_HERE},
      {1, "A_mv/B",           "normal",       2, "A/B", MOVED_HERE},
      {1, "A_mv/B/E",         "normal",       2, "A/B/E", MOVED_HERE},
      {1, "A_mv/B/E/alpha",   "normal",       2, "A/B/E/alpha", MOVED_HERE},
      {1, "A_mv/B/E/beta",    "normal",       2, "A/B/E/beta", MOVED_HERE},
      {1, "A_mv/B/F",         "normal",       2, "A/B/F", MOVED_HERE, "a"},
      {1, "A_mv/B/lambda",    "normal",       2, "A/B/lambda", MOVED_HERE},
      {1, "A_mv/C",           "normal",       2, "A/C", MOVED_HERE},
      {1, "A_mv/D",           "normal",       2, "A/D", MOVED_HERE},
      {1, "A_mv/D/G",         "normal",       2, "A/D/G", MOVED_HERE},
      {1, "A_mv/D/G/pi",      "normal",       2, "A/D/G/pi", MOVED_HERE},
      {1, "A_mv/D/G/rho",     "normal",       2, "A/D/G/rho", MOVED_HERE},
      {1, "A_mv/D/G/tau",     "normal",       2, "A/D/G/tau", MOVED_HERE},
      {1, "A_mv/D/gamma",     "normal",       2, "A/D/gamma", MOVED_HERE},
      {1, "A_mv/D/H",         "normal",       2, "A/D/H", MOVED_HERE},
      {1, "A_mv/D/H/chi",     "normal",       2, "A/D/H/chi", MOVED_HERE},
      {1, "A_mv/D/H/omega",   "normal",       2, "A/D/H/omega", MOVED_HERE},
      {1, "A_mv/D/H/psi",     "normal",       2, "A/D/H/psi", MOVED_HERE},
      {1, "A_mv/mu",          "normal",       2, "A/mu", MOVED_HERE},
      {0}
    };

    SVN_ERR(check_db_rows(&b, "A_mv", nodes));
    SVN_ERR(check_db_conflicts(&b, "", NULL));
  }

  /* Now do the same thing with local obstructions on the edited nodes */
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_revert(&b, "", svn_depth_infinity));
  SVN_ERR(sbox_wc_move(&b, "A", "A_mv"));

  SVN_ERR(svn_io_remove_file2(sbox_wc_path(&b, "A_mv/B/E/alpha"), FALSE, pool));
  SVN_ERR(svn_io_dir_make(sbox_wc_path(&b, "A_mv/B/E/alpha"), APR_OS_DEFAULT,
                          pool));
  SVN_ERR(svn_io_dir_remove_nonrecursive(sbox_wc_path(&b, "A_mv/B/F"), pool));
  SVN_ERR(sbox_file_write(&b, "A_mv/B/F", "F file"));

  SVN_ERR(sbox_wc_update(&b, "", 2));
  SVN_ERR(sbox_wc_resolve(&b, "A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));

  {
    nodes_row_t nodes[] = {
      {1, "A_mv",             "normal",       2, "A", MOVED_HERE},
      {1, "A_mv/B",           "normal",       2, "A/B", MOVED_HERE},
      {1, "A_mv/B/E",         "normal",       2, "A/B/E", MOVED_HERE},
      {1, "A_mv/B/E/alpha",   "normal",       2, "A/B/E/alpha", MOVED_HERE},
      {1, "A_mv/B/E/beta",    "normal",       2, "A/B/E/beta", MOVED_HERE},
      {1, "A_mv/B/F",         "normal",       2, "A/B/F", MOVED_HERE, "a"},
      {1, "A_mv/B/lambda",    "normal",       2, "A/B/lambda", MOVED_HERE},
      {1, "A_mv/C",           "normal",       2, "A/C", MOVED_HERE},
      {1, "A_mv/D",           "normal",       2, "A/D", MOVED_HERE},
      {1, "A_mv/D/G",         "normal",       2, "A/D/G", MOVED_HERE},
      {1, "A_mv/D/G/pi",      "normal",       2, "A/D/G/pi", MOVED_HERE},
      {1, "A_mv/D/G/rho",     "normal",       2, "A/D/G/rho", MOVED_HERE},
      {1, "A_mv/D/G/tau",     "normal",       2, "A/D/G/tau", MOVED_HERE},
      {1, "A_mv/D/gamma",     "normal",       2, "A/D/gamma", MOVED_HERE},
      {1, "A_mv/D/H",         "normal",       2, "A/D/H", MOVED_HERE},
      {1, "A_mv/D/H/chi",     "normal",       2, "A/D/H/chi", MOVED_HERE},
      {1, "A_mv/D/H/omega",   "normal",       2, "A/D/H/omega", MOVED_HERE},
      {1, "A_mv/D/H/psi",     "normal",       2, "A/D/H/psi", MOVED_HERE},
      {1, "A_mv/mu",          "normal",       2, "A/mu", MOVED_HERE},
      {0}
    };
    conflict_info_t conflicts[] = {
      {"A_mv/B/E/alpha", FALSE, FALSE, {svn_wc_conflict_action_edit,
                                        svn_wc_conflict_reason_obstructed}},
      {"A_mv/B/F",       FALSE, FALSE, {svn_wc_conflict_action_edit,
                                        svn_wc_conflict_reason_obstructed}},
      {0}
    };

    SVN_ERR(check_db_rows(&b, "A_mv", nodes));
    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
move_deep_bump(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "move_deep_bump", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "B"));
  SVN_ERR(sbox_wc_mkdir(&b, "B/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "B/B/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "B/B/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "B/B/A/A/A"));
  SVN_ERR(sbox_wc_mkdir(&b, "C"));
  SVN_ERR(sbox_wc_mkdir(&b, "C/C"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "Z"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_mkdir(&b, "B/B/A/A/A/A"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));

  SVN_ERR(sbox_wc_move(&b, "B/B/A", "B/B/B"));
  SVN_ERR(sbox_wc_move(&b, "B/B/B/A", "C/C/A"));

  /* This can't bump C/C/A as that is outside the lock range
     so we expect a tree conflict.

     This used to cause a node not found during bumping
     because B/B/B/A doesn't have a BASE node */
  SVN_ERR(sbox_wc_update(&b, "B/B", 2));

  {
    nodes_row_t nodes[] = {
      {0, "",                 "normal",       1, ""},
      {0, "B",                "normal",       1, "B"},
      {0, "B/B",              "normal",       2, "B/B"},
      {0, "B/B/A",            "normal",       2, "B/B/A"},
      {0, "B/B/A/A",          "normal",       2, "B/B/A/A"},
      {0, "B/B/A/A/A",        "normal",       2, "B/B/A/A/A"},
      {0, "C",                "normal",       1, "C"},
      {0, "C/C",              "normal",       1, "C/C"},
      {3, "B/B/A",            "base-deleted", NO_COPY_FROM, "B/B/B"},
      {3, "B/B/A/A",          "base-deleted", NO_COPY_FROM},
      {3, "B/B/A/A/A",        "base-deleted", NO_COPY_FROM},
      {3, "B/B/B",            "normal",       2, "B/B/A", MOVED_HERE},
      {3, "B/B/B/A",          "normal",       2, "B/B/A/A", MOVED_HERE},
      {3, "B/B/B/A/A",        "normal",       2, "B/B/A/A/A", MOVED_HERE},
      {3, "C/C/A",            "normal",       1, "B/B/A/A", MOVED_HERE},
      {3, "C/C/A/A",          "normal",       1, "B/B/A/A/A", MOVED_HERE},
      {4, "B/B/B/A",          "base-deleted", NO_COPY_FROM, "C/C/A"},
      {4, "B/B/B/A/A",        "base-deleted", NO_COPY_FROM},
      {0}
    };
    conflict_info_t conflicts[] = {
      {"B/B/B/A", FALSE, FALSE, {svn_wc_conflict_action_edit,
                                 svn_wc_conflict_reason_moved_away, "B/B/B/A"}},
      {0}
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
    SVN_ERR(check_db_conflicts(&b, "", conflicts));
  }

  SVN_ERR(sbox_wc_resolve(&b, "B/B/B/A", svn_depth_empty,
                          svn_wc_conflict_choose_mine_conflict));
  SVN_ERR(check_db_conflicts(&b, "", NULL));

  return SVN_NO_ERROR;
}

static svn_error_t *
make_copy_mixed(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "make_copy_mixed", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/D"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/E"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/F"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/G"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/G/H"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/G/I"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/G/J"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/K"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/K/L"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/K/M"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/N"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/N/O"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/N/P"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/N/Q"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/R"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/R/S"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/R/S/T"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_propset(&b, "k", "r2", ""));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_propset(&b, "k", "r3", ""));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_propset(&b, "k", "r4", ""));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_propset(&b, "k", "r5", ""));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_update(&b, "", 5));
  SVN_ERR(sbox_wc_update(&b, "A", 4));
  SVN_ERR(sbox_wc_update(&b, "A/B", 3));
  SVN_ERR(sbox_wc_update(&b, "A/B/C", 2));
  SVN_ERR(sbox_wc_update(&b, "A/B/K", 1));
  SVN_ERR(sbox_wc_update(&b, "A/N/O", 3));

  SVN_ERR(sbox_wc_delete(&b, "A/B/C/F"));
  SVN_ERR(sbox_wc_delete(&b, "A/B/G/J"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/G/J"));

  SVN_ERR(sbox_wc_update(&b, "A/N/P", 1));
  SVN_ERR(sbox_wc_update(&b, "A/N/Q", 1));
  SVN_ERR(sbox_wc_delete(&b, "A/N/P"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/N/P"));
  SVN_ERR(sbox_wc_move(&b, "A/N/Q", "Q"));
  SVN_ERR(sbox_wc_move(&b, "A/B/G/H", "H"));

  /* And something that can't be represented */
  SVN_ERR(sbox_wc_update(&b, "A/B/C/E", 1));
  SVN_ERR(sbox_wc_move(&b, "A/B/C/E", "E"));

  {
    nodes_row_t nodes[] = {
      {0, "",             "normal",       5, "", NOT_MOVED, "k"},
      {0, "A",            "normal",       4, "A"},
      {0, "A/B",          "normal",       3, "A/B"},
      {0, "A/B/C",        "normal",       2, "A/B/C"},
      {0, "A/B/C/D",      "normal",       2, "A/B/C/D"},
      {0, "A/B/C/E",      "normal",       1, "A/B/C/E"},
      {0, "A/B/C/F",      "normal",       2, "A/B/C/F"},
      {0, "A/B/G",        "normal",       3, "A/B/G"},
      {0, "A/B/G/H",      "normal",       3, "A/B/G/H"},
      {0, "A/B/G/I",      "normal",       3, "A/B/G/I"},
      {0, "A/B/G/J",      "normal",       3, "A/B/G/J"},
      {0, "A/B/K",        "normal",       1, "A/B/K"},
      {0, "A/B/K/L",      "normal",       1, "A/B/K/L"},
      {0, "A/B/K/M",      "normal",       1, "A/B/K/M"},
      {0, "A/N",          "normal",       4, "A/N"},
      {0, "A/N/O",        "normal",       3, "A/N/O"},
      {0, "A/N/P",        "normal",       1, "A/N/P"},
      {0, "A/N/Q",        "normal",       1, "A/N/Q"},
      {0, "A/R",          "normal",       4, "A/R"},
      {0, "A/R/S",        "normal",       4, "A/R/S"},
      {0, "A/R/S/T",      "normal",       4, "A/R/S/T"},
      {1, "E",            "normal",       1, "A/B/C/E", MOVED_HERE},
      {1, "H",            "normal",       3, "A/B/G/H", MOVED_HERE},
      {1, "Q",            "normal",       1, "A/N/Q", MOVED_HERE},
      {3, "A/N/P",        "normal",       NO_COPY_FROM},
      {3, "A/N/Q",        "base-deleted", NO_COPY_FROM, "Q"},
      {4, "A/B/C/E",      "base-deleted", NO_COPY_FROM, "E"},
      {4, "A/B/C/F",      "base-deleted", NO_COPY_FROM},
      {4, "A/B/G/H",      "base-deleted", NO_COPY_FROM, "H"},
      {4, "A/B/G/J",      "normal",       NO_COPY_FROM},

      {0}
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(svn_wc__db_op_make_copy(b.wc_ctx->db, sbox_wc_path(&b, "A"),
                                  NULL, NULL, pool));

  {
    nodes_row_t nodes[] = {
      {0, "",             "normal",       5, "", NOT_MOVED, "k"},
      {0, "A",            "normal",       4, "A"},
      {0, "A/B",          "normal",       3, "A/B"},
      {0, "A/B/C",        "normal",       2, "A/B/C"},
      {0, "A/B/C/D",      "normal",       2, "A/B/C/D"},
      {0, "A/B/C/E",      "normal",       1, "A/B/C/E"},
      {0, "A/B/C/F",      "normal",       2, "A/B/C/F"},
      {0, "A/B/G",        "normal",       3, "A/B/G"},
      {0, "A/B/G/H",      "normal",       3, "A/B/G/H"},
      {0, "A/B/G/I",      "normal",       3, "A/B/G/I"},
      {0, "A/B/G/J",      "normal",       3, "A/B/G/J"},
      {0, "A/B/K",        "normal",       1, "A/B/K"},
      {0, "A/B/K/L",      "normal",       1, "A/B/K/L"},
      {0, "A/B/K/M",      "normal",       1, "A/B/K/M"},
      {0, "A/N",          "normal",       4, "A/N"},
      {0, "A/N/O",        "normal",       3, "A/N/O"},
      {0, "A/N/P",        "normal",       1, "A/N/P"},
      {0, "A/N/Q",        "normal",       1, "A/N/Q"},
      {0, "A/R",          "normal",       4, "A/R"},
      {0, "A/R/S",        "normal",       4, "A/R/S"},
      {0, "A/R/S/T",      "normal",       4, "A/R/S/T"},
      {1, "A",            "normal",       4, "A"},
      {1, "A/B",          "not-present",  3, "A/B"},
      {1, "A/B/C",        "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/D",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/C/E",      "base-deleted", NO_COPY_FROM, "E"},
      {1, "A/B/C/F",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/G",        "base-deleted", NO_COPY_FROM},
      {1, "A/B/G/H",      "base-deleted", NO_COPY_FROM, "H"},
      {1, "A/B/G/I",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/G/J",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/K",        "base-deleted", NO_COPY_FROM},
      {1, "A/B/K/L",      "base-deleted", NO_COPY_FROM},
      {1, "A/B/K/M",      "base-deleted", NO_COPY_FROM},
      {1, "A/N",          "normal",       4, "A/N"},
      {1, "A/N/O",        "not-present",  3, "A/N/O"},
      {1, "A/N/P",        "not-present",  1, "A/N/P"},
      {1, "A/N/Q",        "not-present",  1, "A/N/Q", FALSE, "Q"},
      {1, "A/R",          "normal",       4, "A/R"},
      {1, "A/R/S",        "normal",       4, "A/R/S"},
      {1, "A/R/S/T",      "normal",       4, "A/R/S/T"},
      {1, "E",            "normal",       1, "A/B/C/E", MOVED_HERE},
      {1, "H",            "normal",       3, "A/B/G/H", MOVED_HERE},
      {1, "Q",            "normal",       1, "A/N/Q", MOVED_HERE},
      {2, "A/B",          "normal",       3, "A/B"},
      {2, "A/B/C",        "not-present",  2, "A/B/C"},
      {2, "A/B/G",        "normal",       3, "A/B/G"},
      {2, "A/B/G/H",      "normal",       3, "A/B/G/H"},
      {2, "A/B/G/I",      "normal",       3, "A/B/G/I"},
      {2, "A/B/G/J",      "normal",       3, "A/B/G/J"},
      {2, "A/B/K",        "not-present",  1, "A/B/K"},
      {3, "A/B/C",        "normal",       2, "A/B/C"},
      {3, "A/B/C/D",      "normal",       2, "A/B/C/D"},
      {3, "A/B/C/E",      "not-present",  1, "A/B/C/E"},
      {3, "A/B/C/F",      "normal",       2, "A/B/C/F"},
      {3, "A/B/K",        "normal",       1, "A/B/K"},
      {3, "A/B/K/L",      "normal",       1, "A/B/K/L"},
      {3, "A/B/K/M",      "normal",       1, "A/B/K/M"},
      {3, "A/N/O",        "normal",       3, "A/N/O"},
      {3, "A/N/P",        "normal",       NO_COPY_FROM},
      {4, "A/B/C/F",      "base-deleted", NO_COPY_FROM},
      {4, "A/B/G/H",      "base-deleted", NO_COPY_FROM},
      {4, "A/B/G/J",      "normal",       NO_COPY_FROM},

      {0}
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(verify_db(&b));

  return SVN_NO_ERROR;
}

static svn_error_t *
make_copy_and_delete_mixed(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "make_copy_and_del_mixed", opts, pool));

  SVN_ERR(sbox_wc_mkdir(&b, "A"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/D"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/E"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/C/F"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/G"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/G/H"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/G/I"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/G/J"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/K"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/K/L"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/K/M"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/N"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/N/O"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/N/P"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/N/Q"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/R"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/R/S"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/R/S/T"));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_update(&b, "", 1));
  SVN_ERR(sbox_wc_propset(&b, "k", "r2", ""));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_propset(&b, "k", "r3", ""));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_propset(&b, "k", "r4", ""));
  SVN_ERR(sbox_wc_commit(&b, ""));
  SVN_ERR(sbox_wc_propset(&b, "k", "r5", ""));
  SVN_ERR(sbox_wc_commit(&b, ""));

  SVN_ERR(sbox_wc_update(&b, "", 5));
  SVN_ERR(sbox_wc_update(&b, "A", 4));
  SVN_ERR(sbox_wc_update(&b, "A/B", 3));
  SVN_ERR(sbox_wc_update(&b, "A/B/C", 2));
  SVN_ERR(sbox_wc_update(&b, "A/B/K", 1));
  SVN_ERR(sbox_wc_update(&b, "A/N/O", 3));

  SVN_ERR(sbox_wc_delete(&b, "A/B/C/F"));
  SVN_ERR(sbox_wc_delete(&b, "A/B/G/J"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/B/G/J"));

  SVN_ERR(sbox_wc_update(&b, "A/N/P", 1));
  SVN_ERR(sbox_wc_update(&b, "A/N/Q", 1));
  SVN_ERR(sbox_wc_delete(&b, "A/N/P"));
  SVN_ERR(sbox_wc_mkdir(&b, "A/N/P"));
  SVN_ERR(sbox_wc_move(&b, "A/N/Q", "Q"));
  SVN_ERR(sbox_wc_move(&b, "A/B/G/H", "H"));

  /* And something that can't be represented */
  SVN_ERR(sbox_wc_update(&b, "A/B/C/E", 1));
  SVN_ERR(sbox_wc_move(&b, "A/B/C/E", "E"));

  {
    nodes_row_t nodes[] = {
      {0, "",             "normal",       5, "", NOT_MOVED, "k"},
      {0, "A",            "normal",       4, "A"},
      {0, "A/B",          "normal",       3, "A/B"},
      {0, "A/B/C",        "normal",       2, "A/B/C"},
      {0, "A/B/C/D",      "normal",       2, "A/B/C/D"},
      {0, "A/B/C/E",      "normal",       1, "A/B/C/E"},
      {0, "A/B/C/F",      "normal",       2, "A/B/C/F"},
      {0, "A/B/G",        "normal",       3, "A/B/G"},
      {0, "A/B/G/H",      "normal",       3, "A/B/G/H"},
      {0, "A/B/G/I",      "normal",       3, "A/B/G/I"},
      {0, "A/B/G/J",      "normal",       3, "A/B/G/J"},
      {0, "A/B/K",        "normal",       1, "A/B/K"},
      {0, "A/B/K/L",      "normal",       1, "A/B/K/L"},
      {0, "A/B/K/M",      "normal",       1, "A/B/K/M"},
      {0, "A/N",          "normal",       4, "A/N"},
      {0, "A/N/O",        "normal",       3, "A/N/O"},
      {0, "A/N/P",        "normal",       1, "A/N/P"},
      {0, "A/N/Q",        "normal",       1, "A/N/Q"},
      {0, "A/R",          "normal",       4, "A/R"},
      {0, "A/R/S",        "normal",       4, "A/R/S"},
      {0, "A/R/S/T",      "normal",       4, "A/R/S/T"},
      {1, "E",            "normal",       1, "A/B/C/E", MOVED_HERE},
      {1, "H",            "normal",       3, "A/B/G/H", MOVED_HERE},
      {1, "Q",            "normal",       1, "A/N/Q", MOVED_HERE},
      {3, "A/N/P",        "normal",       NO_COPY_FROM},
      {3, "A/N/Q",        "base-deleted", NO_COPY_FROM, "Q"},
      {4, "A/B/C/E",      "base-deleted", NO_COPY_FROM, "E"},
      {4, "A/B/C/F",      "base-deleted", NO_COPY_FROM},
      {4, "A/B/G/H",      "base-deleted", NO_COPY_FROM, "H"},
      {4, "A/B/G/J",      "normal",       NO_COPY_FROM},

      {0}
    };

    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(svn_wc__db_base_remove(b.wc_ctx->db, sbox_wc_path(&b, "A"),
                                 TRUE, TRUE, FALSE, 99,
                                 NULL, NULL, pool));

  {
    nodes_row_t nodes[] = {
      {0, "",             "normal",       5, "", NOT_MOVED, "k"},
      {0, "A",            "not-present",  99, "A"},
      {1, "A",            "normal",       4, "A"},
      {1, "A/B",          "not-present",  3, "A/B"},
      {1, "A/N",          "normal",       4, "A/N"},
      {1, "A/N/O",        "not-present",  3, "A/N/O"},
      {1, "A/N/P",        "not-present",  1, "A/N/P"},
      {1, "A/N/Q",        "not-present",  1, "A/N/Q", FALSE},
      {1, "A/R",          "normal",       4, "A/R"},
      {1, "A/R/S",        "normal",       4, "A/R/S"},
      {1, "A/R/S/T",      "normal",       4, "A/R/S/T"},
      {1, "E",            "normal",       1, "A/B/C/E"},
      {1, "H",            "normal",       3, "A/B/G/H", MOVED_HERE},
      {1, "Q",            "normal",       1, "A/N/Q"},
      {2, "A/B",          "normal",       3, "A/B"},
      {2, "A/B/C",        "not-present",  2, "A/B/C"},
      {2, "A/B/G",        "normal",       3, "A/B/G"},
      {2, "A/B/G/H",      "normal",       3, "A/B/G/H"},
      {2, "A/B/G/I",      "normal",       3, "A/B/G/I"},
      {2, "A/B/G/J",      "normal",       3, "A/B/G/J"},
      {3, "A/B/C",        "normal",       2, "A/B/C"},
      {3, "A/B/C/D",      "normal",       2, "A/B/C/D"},
      {3, "A/B/C/E",      "not-present",  1, "A/B/C/E"},
      {3, "A/B/C/F",      "normal",       2, "A/B/C/F"},
      {2, "A/B/K",        "not-present",  1, "A/B/K"},
      {3, "A/B/K",        "normal",       1, "A/B/K"},
      {3, "A/B/K/L",      "normal",       1, "A/B/K/L"},
      {3, "A/B/K/M",      "normal",       1, "A/B/K/M"},
      {3, "A/N/O",        "normal",       3, "A/N/O"},
      {3, "A/N/P",        "normal",       NO_COPY_FROM},
      {4, "A/B/C/F",      "base-deleted", NO_COPY_FROM},
      {4, "A/B/G/H",      "base-deleted", NO_COPY_FROM, "H"},
      {4, "A/B/G/J",      "normal",       NO_COPY_FROM},

      {0}
    };

    /* This currently fails because Q and E are still marked as moved,
       while there is nothing to be moved. */
    SVN_ERR(check_db_rows(&b, "", nodes));
  }

  SVN_ERR(verify_db(&b));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_global_commit(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "global_commit", opts, pool));

  {
    nodes_row_t before[] = {
      { 0, "",        "normal",       2, "" },
      { 0, "A",       "normal",       2, "A" },
      { 0, "A/B",     "normal",       2, "A/B" },
      { 0, "A/B/C",   "normal",       2, "A/B/C" },
      { 0, "A/B/D",   "normal",       2, "A/B/D" },
      { 0, "A/B/D/E", "normal",       2, "A/B/D/E" },
      { 0, "A/F",     "normal",       2, "A/F" },
      { 0, "A/F/G",   "normal",       2, "A/F/G" },
      { 0, "A/F/H",   "normal",       2, "A/F/H" },
      { 0, "A/F/E",   "normal",       2, "A/F/E" },
      { 0, "A/X",     "normal",       2, "A/X" },
      { 0, "A/X/Y",   "incomplete",   2, "A/X/Y" },
      { 1, "C",       "normal",       2, "A/B/C", MOVED_HERE},
      { 1, "E",       "normal",       2, "A/B/D/E", MOVED_HERE},
      { 2, "A/B",     "normal",       3, "some", MOVED_HERE },
      { 2, "A/B/C",   "base-deleted", NO_COPY_FROM, "C" },
      { 2, "A/B/D",   "normal",       3, "some/D", MOVED_HERE},
      { 2, "A/B/D/E", "not-present",  3, "some/D/E", FALSE, "E", TRUE},
      { 3, "A/B/C",   "normal",       NO_COPY_FROM},
      { 2, "A/F",     "normal",       1, "S2" },
      { 2, "A/F/G",   "normal",       1, "S2/G" },
      { 2, "A/F/H",   "not-present",  1, "S2/H" },
      { 2, "A/F/E",   "base-deleted", NO_COPY_FROM },
      { 1, "some",    "normal",       3, "some", FALSE, "A/B"},
      { 0 }
    };
    SVN_ERR(insert_dirs(&b, before));
    SVN_ERR(check_db_rows(&b, "", before)); /* Check move insertion logic */
    SVN_ERR(verify_db(&b));
  }

  /* This should break the moves */
  SVN_ERR(svn_wc__db_global_commit(b.wc_ctx->db,
                                   sbox_wc_path(&b, "A/B"),
                                   5, 5, 700, "me", NULL, NULL,
                                   FALSE, FALSE, NULL, pool));
  {
    nodes_row_t after[] = {
      { 0, "",        "normal",       2, "" },
      { 0, "A",       "normal",       2, "A" },
      { 0, "A/B",     "normal",       5, "A/B" },
      { 0, "A/B/D",   "normal",       5, "A/B/D"},
      { 0, "A/B/D/E", "not-present",  5, "A/B/D/E"},
      { 0, "A/F",     "normal",       2, "A/F" },
      { 0, "A/F/G",   "normal",       2, "A/F/G" },
      { 0, "A/F/H",   "normal",       2, "A/F/H" },
      { 0, "A/F/E",   "normal",       2, "A/F/E" },
      { 0, "A/X",     "normal",       2, "A/X" },
      { 0, "A/X/Y",   "incomplete",   2, "A/X/Y" },
      { 1, "C",       "normal",       2, "A/B/C"},
      { 1, "E",       "normal",       2, "A/B/D/E"},
      { 1, "some",    "normal",       3, "some"},
      { 3, "A/B/C",   "normal",       NO_COPY_FROM},
      { 2, "A/F",     "normal",       1, "S2" },
      { 2, "A/F/G",   "normal",       1, "S2/G" },
      { 2, "A/F/H",   "not-present",  1, "S2/H" },
      { 2, "A/F/E",   "base-deleted", NO_COPY_FROM },
      { 0 }
    };

    SVN_ERR(check_db_rows(&b, "", after));
    SVN_ERR(verify_db(&b));
  }

  SVN_ERR(svn_wc__db_global_commit(b.wc_ctx->db,
                                   sbox_wc_path(&b, "A/F"),
                                   6, 6, 800, "me", NULL, NULL,
                                   FALSE, FALSE, NULL, pool));

  {
    nodes_row_t after[] = {
      { 0, "",        "normal",       2, "" },
      { 0, "A",       "normal",       2, "A" },
      { 0, "A/B",     "normal",       5, "A/B" },
      { 0, "A/B/D",   "normal",       5, "A/B/D"},
      { 0, "A/B/D/E", "not-present",  5, "A/B/D/E"},
      { 0, "A/F",     "normal",       6, "A/F" },
      { 0, "A/F/G",   "normal",       6, "A/F/G" },
      { 0, "A/F/H",   "not-present",  6, "A/F/H" },
      { 0, "A/X",     "normal",       2, "A/X" },
      { 0, "A/X/Y",   "incomplete",   2, "A/X/Y" },
      { 1, "C",       "normal",       2, "A/B/C"},
      { 1, "E",       "normal",       2, "A/B/D/E"},
      { 1, "some",    "normal",       3, "some"},
      { 3, "A/B/C",   "normal",       NO_COPY_FROM },
      { 0 }
    };

    SVN_ERR(check_db_rows(&b, "", after));
    SVN_ERR(verify_db(&b));
  }

  SVN_ERR(svn_wc__db_global_commit(b.wc_ctx->db,
                                   sbox_wc_path(&b, "A/B/C"),
                                   7, 7, 900, "me", NULL, NULL,
                                   FALSE, FALSE, NULL, pool));

  {
    nodes_row_t after[] = {
      { 0, "",        "normal",       2, "" },
      { 0, "A",       "normal",       2, "A" },
      { 0, "A/B",     "normal",       5, "A/B" },
      { 0, "A/B/C",   "normal",       7, "A/B/C"},
      { 0, "A/B/D",   "normal",       5, "A/B/D"},
      { 0, "A/B/D/E", "not-present",  5, "A/B/D/E"},
      { 0, "A/F",     "normal",       6, "A/F" },
      { 0, "A/F/G",   "normal",       6, "A/F/G" },
      { 0, "A/F/H",   "not-present",  6, "A/F/H" },
      { 0, "A/X",     "normal",       2, "A/X" },
      { 0, "A/X/Y",   "incomplete",   2, "A/X/Y" },
      { 1, "some",    "normal",       3, "some"},
      { 1, "E",       "normal",       2, "A/B/D/E"},
      { 1, "C",       "normal",       2, "A/B/C"},
      { 0 }
    };

    SVN_ERR(check_db_rows(&b, "", after));
    SVN_ERR(verify_db(&b));
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_global_commit_switched(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;

  SVN_ERR(svn_test__sandbox_create(&b, "global_commit_switched", opts, pool));
  {
    nodes_row_t before[] = {
      { 0, "",          "normal",       2, "" },
      { 0, "A",         "normal",       2, "A" },
      /* A/B is switched... The libsvn_client layer tries to prevent this,
                             because it has such an unexpected behavior. */
      { 0, "A/B",       "normal",       2, "N/B" },
      { 0, "A/B/C",     "normal",       2, "N/B/C" },
      { 0, "A/B/C/D",   "normal",       2, "N/B/C/D" },
      { 0, "A/B/C/E",   "normal",       2, "N/B/C/E" },
      { 2, "A/B",       "normal",       3, "Z/B" },
      { 2, "A/B/C",     "normal",       3, "Z/B/C" },
      { 2, "A/B/C/D",   "normal",       3, "Z/B/C/D" },
      { 2, "A/B/C/E",   "base-deleted", NO_COPY_FROM },
      /* not-present nodes have an 'uninteresting path',
         which doesn't have to be as implied by ancestor at same depth */
      { 2, "A/B/C/F",   "not-present",  3, "ZZ-Z-Z_ZZ_Z_Z" },
      { 2, "A/B/C/G",   "normal",       3, "Z/B/C/G" },
      { 2, "A/B/C/G/H", "normal",       3, "Z/B/C/G/H" },

      { 3, "A/B/C",     "normal",       4, "Q/C" },
      { 3, "A/B/C/D",   "base-deleted", NO_COPY_FROM },
      { 3, "A/B/C/G",   "normal",       4, "Q/C/G" },
      { 3, "A/B/C/G/H", "base-deleted", NO_COPY_FROM },

      { 4, "A/B/C/F",   "normal",       NO_COPY_FROM },
      { 5, "A/B/C/G/H", "normal",       NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(insert_dirs(&b, before));
    SVN_ERR(verify_db(&b));
  }

  SVN_ERR(svn_wc__db_global_commit(b.wc_ctx->db,
                                   sbox_wc_path(&b, "A/B"),
                                   7, 7, 12, "me", NULL, NULL,
                                   FALSE, FALSE, NULL, pool));

  {
    nodes_row_t after[] = {
      { 0, "",          "normal",       2, "" },
      { 0, "A",         "normal",       2, "A" },
      /* The commit is applied as A/B, because the path is calculated from A,
         and not the shadowed node at A/B. (Fixed in r1663991) */
      { 0, "A/B",       "normal",       7, "A/B" },
      { 0, "A/B/C",     "normal",       7, "A/B/C" },
      { 0, "A/B/C/D",   "normal",       7, "A/B/C/D" },
      /* Even calculated path of not-present is fixed */
      { 0, "A/B/C/F",   "not-present",  7, "A/B/C/F" },
      { 0, "A/B/C/G",   "normal",       7, "A/B/C/G" },
      { 0, "A/B/C/G/H", "normal",       7, "A/B/C/G/H" },

      /* The higher layers are unaffected */
      { 3, "A/B/C",     "normal",       4, "Q/C" },
      { 3, "A/B/C/D",   "base-deleted", NO_COPY_FROM },
      { 3, "A/B/C/G",   "normal",       4, "Q/C/G" },
      { 3, "A/B/C/G/H", "base-deleted", NO_COPY_FROM },

      { 4, "A/B/C/F",   "normal",       NO_COPY_FROM },
      { 5, "A/B/C/G/H", "normal",       NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(verify_db(&b));
    SVN_ERR(check_db_rows(&b, "", after));
  }

  SVN_ERR(svn_wc__db_global_commit(b.wc_ctx->db,
                                   sbox_wc_path(&b, "A/B/C"),
                                   8, 8, 12, "me", NULL, NULL,
                                   FALSE, FALSE, NULL, pool));

  {
    nodes_row_t after[] = {
      { 0, "",          "normal",       2, "" },
      { 0, "A",         "normal",       2, "A" },
      { 0, "A/B",       "normal",       7, "A/B" },
      /* Base deleted and not-present are now gone */
      { 0, "A/B/C",     "normal",       8, "A/B/C" },
      { 0, "A/B/C/G",   "normal",       8, "A/B/C/G" },

      { 4, "A/B/C/F",   "normal",       NO_COPY_FROM },
      { 5, "A/B/C/G/H", "normal",       NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(verify_db(&b));
    SVN_ERR(check_db_rows(&b, "", after));
  }

  return SVN_NO_ERROR;
}

/* ---------------------------------------------------------------------- */
/* The list of test functions */

static int max_threads = 4;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(test_wc_wc_copies,
                       "test_wc_wc_copies"),
    SVN_TEST_OPTS_PASS(test_reverts,
                       "test_reverts"),
    SVN_TEST_OPTS_PASS(test_deletes,
                       "test_deletes"),
    SVN_TEST_OPTS_PASS(test_delete_of_copies,
                       "test_delete_of_copies"),
    SVN_TEST_OPTS_PASS(test_delete_with_base,
                       "test_delete_with_base"),
    SVN_TEST_OPTS_PASS(test_adds,
                       "test_adds"),
    SVN_TEST_OPTS_PASS(test_repo_wc_copies,
                       "test_repo_wc_copies"),
    SVN_TEST_OPTS_PASS(test_delete_with_update,
                       "test_delete_with_update"),
    SVN_TEST_OPTS_PASS(test_adds_change_kind,
                       "test_adds_change_kind"),
    SVN_TEST_OPTS_PASS(test_base_dir_insert_remove,
                       "test_base_dir_insert_remove"),
    SVN_TEST_OPTS_PASS(test_db_make_copy,
                       "test_db_make_copy"),
    SVN_TEST_OPTS_PASS(test_wc_move,
                       "test_wc_move"),
    SVN_TEST_OPTS_PASS(test_mixed_rev_copy,
                        "test_mixed_rev_copy"),
    SVN_TEST_OPTS_PASS(test_delete_of_replace,
                       "test_delete_of_replace"),
    SVN_TEST_OPTS_PASS(test_del_replace_not_present,
                       "test_del_replace_not_present"),
    SVN_TEST_OPTS_PASS(test_op_revert,
                       "test_op_revert"),
    SVN_TEST_OPTS_PASS(test_op_revert_changelist,
                       "test_op_revert_changelist"),
    SVN_TEST_OPTS_PASS(test_children_of_replaced_dir,
                       "test_children_of_replaced_dir"),
    SVN_TEST_OPTS_PASS(test_op_delete,
                       "test_op_delete"),
    SVN_TEST_OPTS_PASS(test_child_replace_with_same_origin,
                       "test_child_replace_with_same"),
    SVN_TEST_OPTS_PASS(test_shadowed_update,
                       "test_shadowed_update"),
    SVN_TEST_OPTS_PASS(test_copy_of_deleted,
                       "test_copy_of_deleted (issue #3873)"),
#ifndef DARWIN
    SVN_TEST_OPTS_PASS(test_case_rename,
                       "test_case_rename on case (in)sensitive system"),
#else
    /* apr doesn't implement APR_FILEPATH_TRUENAME for MAC OS yet */
    SVN_TEST_OPTS_XFAIL(test_case_rename,
                        "test_case_rename on case (in)sensitive system"),
#endif
    SVN_TEST_OPTS_PASS(commit_file_external,
                       "commit_file_external (issue #4002)"),
    SVN_TEST_OPTS_PASS(revert_file_externals,
                       "revert_file_externals"),
    SVN_TEST_OPTS_PASS(copy_file_externals,
                       "copy_file_externals"),
    SVN_TEST_OPTS_PASS(copy_wc_wc_server_excluded,
                       "test_wc_wc_copy_server_excluded"),
    SVN_TEST_OPTS_PASS(incomplete_switch,
                       "incomplete_switch (issue 4040)"),
    SVN_TEST_OPTS_PASS(nested_moves_child_first,
                       "nested_moves_child_first"),
    SVN_TEST_OPTS_PASS(nested_moves_child_last,
                       "nested_moves_child_last"),
    SVN_TEST_OPTS_PASS(move_in_copy,
                       "move_in_copy"),
    SVN_TEST_OPTS_PASS(move_in_replace,
                       "move_in_replace"),
    SVN_TEST_OPTS_PASS(copy_a_move,
                       "copy_a_move"),
    SVN_TEST_OPTS_PASS(move_to_swap,
                       "move_to_swap"),
    SVN_TEST_OPTS_PASS(revert_nested_move,
                       "revert_nested_move"),
    SVN_TEST_OPTS_PASS(move_on_move,
                       "move_on_move"),
    SVN_TEST_OPTS_PASS(move_on_move2,
                       "move_on_move2"),
    SVN_TEST_OPTS_PASS(move_added,
                       "move_added"),
    SVN_TEST_OPTS_PASS(move_update,
                       "move_update"),
    SVN_TEST_OPTS_PASS(test_scan_delete,
                       "scan_delete"),
    SVN_TEST_OPTS_PASS(test_follow_moved_to,
                       "follow_moved_to"),
    SVN_TEST_OPTS_WIMP(mixed_rev_move,
                       "mixed_rev_move",
                       "needs different libsvn_wc entry point"),
    SVN_TEST_OPTS_PASS(update_prop_mod_into_moved,
                       "update_prop_mod_into_moved"),
    SVN_TEST_OPTS_PASS(nested_move_update,
                       "nested_move_update"),
    SVN_TEST_OPTS_PASS(nested_move_commit,
                       "nested_move_commit (issue 4291)"),
    SVN_TEST_OPTS_PASS(nested_move_update2,
                       "nested_move_update2"),
    SVN_TEST_OPTS_PASS(move_update_conflicts,
                       "move_update_conflicts"),
    SVN_TEST_OPTS_PASS(move_update_delete_mods,
                       "move_update_delete_mods"),
    SVN_TEST_OPTS_PASS(nested_moves2,
                       "nested_moves2"),
    SVN_TEST_OPTS_PASS(move_in_delete,
                       "move_in_delete (issue 4303)"),
    SVN_TEST_OPTS_PASS(switch_move,
                       "switch_move"),
    SVN_TEST_OPTS_PASS(move_replace,
                       "move_replace"),
    SVN_TEST_OPTS_PASS(layered_moved_to,
                       "layered_moved_to"),
    SVN_TEST_OPTS_PASS(update_within_move,
                       "update_within_move"),
    SVN_TEST_OPTS_PASS(commit_moved_descendant,
                       "commit_moved_descendant"),
    SVN_TEST_OPTS_XFAIL(commit_moved_away_descendant,
                        "commit_moved_away_descendant"),
    SVN_TEST_OPTS_PASS(finite_move_update_bump,
                       "finite_move_update_bump"),
    SVN_TEST_OPTS_PASS(move_away_delete_update,
                       "move_away_delete_update"),
    SVN_TEST_OPTS_PASS(move_not_present_variants,
                       "move_not_present_variants"),
    SVN_TEST_OPTS_PASS(update_child_under_add,
                       "update_child_under_add (issue 4111)"),
    SVN_TEST_OPTS_PASS(delete_over_moved_away,
                       "delete_over_moved_away"),
    SVN_TEST_OPTS_PASS(movedto_opdepth,
                       "moved_to op_depth"),
    SVN_TEST_OPTS_PASS(new_basemove,
                       "new_basemove"),
    SVN_TEST_OPTS_PASS(move_back,
                       "move_back (issue 4302)"),
    SVN_TEST_OPTS_PASS(move_update_subtree,
                       "move_update_subtree (issue 4232)"),
    SVN_TEST_OPTS_PASS(move_parent_into_child,
                       "move_parent_into_child (issue 4333)"),
    SVN_TEST_OPTS_PASS(move_depth_expand,
                       "move depth expansion"),
    SVN_TEST_OPTS_XFAIL(move_retract,
                       "move retract (issue 4336)"),
    SVN_TEST_OPTS_XFAIL(move_delete_file_externals,
                       "move/delete file externals (issue 4293)"),
    SVN_TEST_OPTS_PASS(update_with_tree_conflict,
                       "update with tree conflict (issue 4347)"),
    SVN_TEST_OPTS_PASS(move_update_parent_replace,
                       "move update with replaced parent (issue 4388)"),
    SVN_TEST_OPTS_PASS(copy_mixed_rev_mods,
                       "copy mixed-rev with mods"),
    SVN_TEST_OPTS_PASS(move_child_to_parent_revert,
                       "move child to parent and revert (issue 4436)"),
    SVN_TEST_OPTS_PASS(move_delete_intermediate,
                       "move more than once, delete intermediate"),
    SVN_TEST_OPTS_XFAIL(move_revert_intermediate,
                       "move more than once, revert intermediate"),
    SVN_TEST_OPTS_PASS(move_replace_ancestor_with_child,
                       "move replace ancestor with child"),
    SVN_TEST_OPTS_PASS(move_twice_within_delete,
                       "move twice and then delete"),
    SVN_TEST_OPTS_PASS(del4_update_edit_AAA,
                       "del4: edit AAA"),
    SVN_TEST_OPTS_XFAIL(del4_update_delete_AAA,
                       "del4: delete AAA"),
    SVN_TEST_OPTS_XFAIL(del4_update_add_AAA,
                       "del4: add AAA"),
    SVN_TEST_OPTS_XFAIL(del4_update_replace_AAA,
                       "del4: replace AAA"),
    SVN_TEST_OPTS_PASS(del4_update_delself_AAA,
                       "del4: delete self AAA"),
    SVN_TEST_OPTS_XFAIL(del4_update_replaceself_AAA,
                       "del4: replace self AAA"),
    SVN_TEST_OPTS_PASS(move4_update_edit_AAA,
                       "move4: edit AAA"),
    SVN_TEST_OPTS_XFAIL(move4_update_delete_AAA,
                       "move4: delete AAA"),
    SVN_TEST_OPTS_XFAIL(move4_update_add_AAA,
                       "move4: add AAA"),
    SVN_TEST_OPTS_XFAIL(move4_update_replace_AAA,
                       "move4: replace AAA"),
    SVN_TEST_OPTS_PASS(move4_update_delself_AAA,
                       "move4: delete self AAA"),
    SVN_TEST_OPTS_XFAIL(move4_update_replaceself_AAA,
                       "move4: replace self AAA"),
    SVN_TEST_OPTS_PASS(simple_move_bump,
                       "simple move bump"),
    SVN_TEST_OPTS_PASS(movedhere_extract_retract,
                       "movedhere extract retract"),
    SVN_TEST_OPTS_PASS(repo_wc_copy,
                       "repo_wc_copy"),
    SVN_TEST_OPTS_PASS(break_move_in_delete,
                       "break move in delete (issue 4491)"),
    SVN_TEST_OPTS_PASS(nested_move_delete,
                       "nested move delete"),
    SVN_TEST_OPTS_XFAIL(move_within_mixed_move,
                        "move within mixed move"),
    SVN_TEST_OPTS_PASS(move_edit_obstruction,
                       "move edit obstruction"),
    SVN_TEST_OPTS_PASS(move_deep_bump,
                       "move deep bump"),
    SVN_TEST_OPTS_PASS(make_copy_mixed,
                       "make a copy of a mixed revision tree"),
    SVN_TEST_OPTS_PASS(make_copy_and_delete_mixed,
                       "make a copy of a mixed revision tree and del"),
    SVN_TEST_OPTS_PASS(test_global_commit,
                       "test global commit"),
    SVN_TEST_OPTS_PASS(test_global_commit_switched,
                       "test global commit switched"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
