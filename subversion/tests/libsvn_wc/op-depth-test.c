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

#include <apr_pools.h>
#include <apr_general.h>

#include "svn_types.h"
#include "svn_io.h"
#include "svn_dirent_uri.h"
#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_hash.h"

#include "utils.h"

#include "private/svn_wc_private.h"
#include "private/svn_sqlite.h"

#include "../svn_test.h"


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

static const char *const my_statements[] = {
  "SELECT op_depth, presence, local_relpath, revision, repos_path "
    "FROM nodes "
    "WHERE local_relpath = ?1 OR local_relpath LIKE ?2",
  NULL };

#define STMT_SELECT_NODES_INFO 0

static svn_error_t *
open_wc_db(svn_sqlite__db_t **sdb,
           const char *wc_root_abspath,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
{
  const char *dbpath = svn_dirent_join_many(scratch_pool,
                                            wc_root_abspath, ".svn", "wc.db",
                                            NULL);
  SVN_ERR(svn_sqlite__open(sdb, dbpath, svn_sqlite__mode_readonly,
                           my_statements, 0, NULL,
                           result_pool, scratch_pool));
  return SVN_NO_ERROR;
}


/* ---------------------------------------------------------------------- */
/* Functions for easy manipulation of a WC. Paths given to these functions
 * can be relative to the WC root as stored in the WC baton. */

/* A baton for easy manipulation of a WC. */
typedef struct wc_baton_t {
  apr_pool_t *pool;
  svn_wc_context_t *wc_ctx;
  const char *repos_url;
  const char *wc_abspath;
} wc_baton_t;

/* Return the abspath of PATH which is absolute or relative to the WC in B. */
#define wc_path(b, path) (svn_dirent_join((b)->wc_abspath, (path), (b)->pool))

/* Create a file on disk at PATH, with TEXT as its content. */
static void
file_write(wc_baton_t *b, const char *path, const char *text)
{
  FILE *f = fopen(wc_path(b, path), "w");
  fputs(text, f);
  fclose(f);
}

/* Schedule for addition the single node that exists on disk at PATH,
 * non-recursively. */
static svn_error_t *
wc_add(wc_baton_t *b, const char *path)
{
  const char *parent_abspath;
  path = wc_path(b, path);
  parent_abspath = svn_dirent_dirname(path, b->pool);
  SVN_ERR(svn_wc__acquire_write_lock(NULL, b->wc_ctx, parent_abspath, FALSE,
                                     b->pool, b->pool));
  SVN_ERR(svn_wc_add_from_disk(b->wc_ctx, path, NULL, NULL, b->pool));
  SVN_ERR(svn_wc__release_write_lock(b->wc_ctx, parent_abspath, b->pool));
  return SVN_NO_ERROR;
}

/* Create a single directory on disk. */
static svn_error_t *
disk_mkdir(wc_baton_t *b, const char *path)
{
  path = wc_path(b, path);
  SVN_ERR(svn_io_dir_make(path, APR_FPROT_OS_DEFAULT, b->pool));
  return SVN_NO_ERROR;
}

/* Create a single directory on disk and schedule it for addition. */
static svn_error_t *
wc_mkdir(wc_baton_t *b, const char *path)
{
  SVN_ERR(disk_mkdir(b, path));
  SVN_ERR(wc_add(b, path));
  return SVN_NO_ERROR;
}

#if 0 /* not used */
/* Copy the file or directory tree FROM_PATH to TO_PATH which must not exist
 * beforehand. */
static svn_error_t *
disk_copy(wc_baton_t *b, const char *from_path, const char *to_path)
{
  const char *to_dir, *to_name;
  from_path = wc_path(b, from_path);
  to_path = wc_path(b, to_path);
  svn_dirent_split(&to_dir, &to_name, to_path, b->pool);
  return svn_io_copy_dir_recursively(from_path, to_dir, to_name,
                                     FALSE, NULL, NULL, b->pool);
}
#endif

/* Copy the WC file or directory tree FROM_PATH to TO_PATH which must not
 * exist beforehand. */
static svn_error_t *
wc_copy(wc_baton_t *b, const char *from_path, const char *to_path)
{
  from_path = wc_path(b, from_path);
  to_path = wc_path(b, to_path);
  return svn_wc_copy3(b->wc_ctx, from_path, to_path, FALSE,
                      NULL, NULL, NULL, NULL, b->pool);
}

/* Revert a WC file or directory tree at PATH */
static svn_error_t *
wc_revert(wc_baton_t *b, const char *path, svn_depth_t depth)
{
  const char *abspath = wc_path(b, path);

  return svn_wc_revert4(b->wc_ctx, abspath, depth, FALSE, NULL,
                        NULL, NULL, /* cancel baton + func */
                        NULL, NULL, /* notify baton + func */
                        b->pool);
}

static svn_error_t *
wc_delete(wc_baton_t *b, const char *path)
{
  const char *abspath = wc_path(b, path);

  return svn_wc_delete4(b->wc_ctx, abspath, FALSE, TRUE, NULL, NULL, NULL, NULL,
                        b->pool);
}

/* Create the Greek tree on disk in the WC, and commit it. */
static svn_error_t *
add_and_commit_greek_tree(wc_baton_t *b)
{
  const char *greek_tree_dirs[8] =
  {
    "A",
    "A/B",
    "A/B/E",
    "A/B/F",
    "A/C",
    "A/D",
    "A/D/G",
    "A/D/H"
  };
  const char *greek_tree_files[12][2] =
  {
    { "iota",         "This is the file 'iota'.\n" },
    { "A/mu",         "This is the file 'mu'.\n" },
    { "A/B/lambda",   "This is the file 'lambda'.\n" },
    { "A/B/E/alpha",  "This is the file 'alpha'.\n" },
    { "A/B/E/beta",   "This is the file 'beta'.\n" },
    { "A/D/gamma",    "This is the file 'gamma'.\n" },
    { "A/D/G/pi",     "This is the file 'pi'.\n" },
    { "A/D/G/rho",    "This is the file 'rho'.\n" },
    { "A/D/G/tau",    "This is the file 'tau'.\n" },
    { "A/D/H/chi",    "This is the file 'chi'.\n" },
    { "A/D/H/psi",    "This is the file 'psi'.\n" },
    { "A/D/H/omega",  "This is the file 'omega'.\n" }
  };
  int i;

  for (i = 0; i < 8; i++)
    SVN_ERR(wc_mkdir(b, greek_tree_dirs[i]));

  for (i = 0; i < 12; i++)
    {
      file_write(b, greek_tree_files[i][0], greek_tree_files[i][1]);
      SVN_ERR(wc_add(b, greek_tree_files[i][0]));
    }

  {
    svn_client_ctx_t *ctx;
    apr_array_header_t *targets = apr_array_make(b->pool, 1,
                                                 sizeof(const char *));

    APR_ARRAY_PUSH(targets, const char *) = wc_path(b, "A");
    SVN_ERR(svn_client_create_context(&ctx, b->pool));
    SVN_ERR(svn_client_commit5(targets, svn_depth_infinity,
                               FALSE, FALSE, /* keep locks/cl's */
                               NULL, NULL, NULL, NULL, ctx, b->pool));
  }

  return SVN_NO_ERROR;
}


/* ---------------------------------------------------------------------- */
/* Functions for comparing expected and actual WC DB data. */

/* Some of the fields from a NODES table row. */
typedef struct nodes_row_t {
    int op_depth;
    const char *local_relpath;
    const char *presence;
    svn_revnum_t repo_revnum;
    const char *repo_relpath;
} nodes_row_t;

/* Return a human-readable string representing ROW. */
static const char *
print_row(const nodes_row_t *row,
          apr_pool_t *result_pool)
{
  if (row == NULL)
    return "(null)";
  if (row->repo_revnum == SVN_INVALID_REVNUM)
    return apr_psprintf(result_pool, "%d, %s, %s",
                        row->op_depth, row->local_relpath, row->presence);
  else
    return apr_psprintf(result_pool, "%d, %s, %s, from ^/%s@%d",
                        row->op_depth, row->local_relpath, row->presence,
                        row->repo_relpath, (int)row->repo_revnum);
}

/* A baton to pass through svn_hash_diff() to compare_nodes_rows(). */
typedef struct {
    apr_hash_t *expected_hash;  /* Maps "OP_DEPTH PATH" to nodes_row_t. */
    apr_hash_t *actual_hash;    /* Maps "OP_DEPTH PATH" to nodes_row_t. */
    apr_pool_t *scratch_pool;
    svn_error_t *errors;        /* Chain of errors found in comparison. */
} comparison_baton_t;

/* Compare two hash entries indexed by KEY, in the two hashes in BATON.
 * Append an error message to BATON->errors if they differ or are not both
 * present.
 *
 * If the ACTUAL row has field values that should have been elided
 * (because they match the parent row), then do so now.  We want to ignore
 * any such lack of elision, for the purposes of these tests, because the
 * method of copying in use (at the time this tweak is introduced) does
 * calculate these values itself, it simply copies from the source rows.
 *
 * Implements svn_hash_diff_func_t. */
static svn_error_t *
compare_nodes_rows(const void *key, apr_ssize_t klen,
                   enum svn_hash_diff_key_status status,
                   void *baton)
{
  comparison_baton_t *b = baton;
  nodes_row_t *expected = apr_hash_get(b->expected_hash, key, klen);
  nodes_row_t *actual = apr_hash_get(b->actual_hash, key, klen);

  /* If the ACTUAL row has field values that should have been elided
   * (because they match the parent row), then do so now. */
  if (actual && actual->op_depth > 0 && actual->repo_relpath)
    {
      const char *parent_relpath, *name, *parent_key;
      nodes_row_t *parent_actual;

      svn_relpath_split(&parent_relpath, &name, actual->local_relpath,
                        b->scratch_pool);
      parent_key = apr_psprintf(b->scratch_pool, "%d %s",
                                actual->op_depth, parent_relpath);
      parent_actual = apr_hash_get(b->actual_hash, parent_key,
                                   APR_HASH_KEY_STRING);
      if (parent_actual && parent_actual > 0 && parent_actual->repo_relpath
          && strcmp(actual->repo_relpath,
                    svn_relpath_join(parent_actual->repo_relpath, name,
                                     b->scratch_pool)) == 0
          && actual->repo_revnum == parent_actual->repo_revnum)
        {
          actual->repo_relpath = NULL;
          actual->repo_revnum = SVN_INVALID_REVNUM;
        }
    }

  if (! expected)
    {
      b->errors = svn_error_createf(
                    SVN_ERR_TEST_FAILED, b->errors,
                    "actual   {%s}",
                    print_row(actual, b->scratch_pool));
    }
  else if (! actual)
    {
      b->errors = svn_error_createf(
                    SVN_ERR_TEST_FAILED, b->errors,
                    "expected {%s}",
                    print_row(expected, b->scratch_pool));
    }
  else if (expected->repo_revnum != actual->repo_revnum
           || (strcmp_null(expected->repo_relpath, actual->repo_relpath) != 0)
           || (strcmp_null(expected->presence, actual->presence) != 0))
    {
      b->errors = svn_error_createf(
                    SVN_ERR_TEST_FAILED, b->errors,
                    "expected {%s}; actual {%s}",
                    print_row(expected, b->scratch_pool),
                    print_row(actual, b->scratch_pool));
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
check_db_rows(wc_baton_t *b,
              const char *root_path,
              const nodes_row_t *expected_rows)
{
  const char *base_relpath = root_path;
  svn_sqlite__db_t *sdb;
  int i;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_hash_t *actual_hash = apr_hash_make(b->pool);
  apr_hash_t *expected_hash = apr_hash_make(b->pool);
  comparison_baton_t comparison_baton
    = { expected_hash, actual_hash, b->pool, NULL };

  /* Fill ACTUAL_HASH with data from the WC DB. */
  SVN_ERR(open_wc_db(&sdb, b->wc_abspath, b->pool, b->pool));
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_NODES_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "ss", base_relpath,
                            apr_psprintf(b->pool, "%s/%%", base_relpath)));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *key;
      nodes_row_t *row = apr_palloc(b->pool, sizeof(*row));

      row->op_depth = svn_sqlite__column_int(stmt, 0);
      row->presence = svn_sqlite__column_text(stmt, 1, b->pool);
      row->local_relpath = svn_sqlite__column_text(stmt, 2, b->pool);
      row->repo_revnum = svn_sqlite__column_revnum(stmt, 3);
      row->repo_relpath = svn_sqlite__column_text(stmt, 4, b->pool);

      key = apr_psprintf(b->pool, "%d %s", row->op_depth, row->local_relpath);
      apr_hash_set(actual_hash, key, APR_HASH_KEY_STRING, row);

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
  SVN_ERR(svn_hash_diff(expected_hash, actual_hash,
                        compare_nodes_rows, &comparison_baton, b->pool));
  return comparison_baton.errors;
}


/* ---------------------------------------------------------------------- */
/* The test functions */

/* Check that all kinds of WC-to-WC copies give correct op_depth results:
 * create a Greek tree, make copies in it, and check the resulting DB rows. */
static svn_error_t *
wc_wc_copies(wc_baton_t *b)
{
  const char source_everything[]  = "A/B";

  const char source_base_file[]   = "A/B/lambda";
  const char source_base_dir[]    = "A/B/E";

  const char source_added_file[]  = "A/B/file-added";
  const char source_added_dir[]   = "A/B/D-added";
  const char source_added_dir2[]  = "A/B/D-added/D2";

  const char source_copied_file[] = "A/B/lambda-copied";
  const char source_copied_dir[]  = "A/B/E-copied";

  SVN_ERR(add_and_commit_greek_tree(b));

  /* Create the various kinds of source node which will be copied */

  file_write(b, source_added_file, "New file");
  SVN_ERR(wc_add(b, source_added_file));
  SVN_ERR(wc_mkdir(b, source_added_dir));
  SVN_ERR(wc_mkdir(b, source_added_dir2));

  SVN_ERR(wc_copy(b, source_base_file, source_copied_file));
  SVN_ERR(wc_copy(b, source_base_dir, source_copied_dir));

  /* Test copying various things */

  {
#define NO_COPY_FROM SVN_INVALID_REVNUM, NULL
    struct subtest_t
      {
        const char *from_path;
        const char *to_path;
        nodes_row_t expected[20];
      }
    subtests[] =
      {
        /* base file */
        { source_base_file, "A/C/copy1", {
            { 3, "",                "normal",   1, source_base_file }
          } },

        /* base dir */
        { source_base_dir, "A/C/copy2", {
            { 3, "",                "normal",   1, source_base_dir },
            { 3, "alpha",           "normal",   NO_COPY_FROM },
            { 3, "beta",            "normal",   NO_COPY_FROM }
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
            { 3, "alpha",           "normal",   NO_COPY_FROM },
            { 3, "beta",            "normal",   NO_COPY_FROM }
          } },

        /* copied tree with everything in it */
        { source_everything, "A/C/copy7", {
            { 3, "",                "normal",   1, source_everything },
            { 3, "lambda",          "normal",   NO_COPY_FROM },
            { 3, "E",               "normal",   NO_COPY_FROM },
            { 3, "E/alpha",         "normal",   NO_COPY_FROM },
            { 3, "E/beta",          "normal",   NO_COPY_FROM },
            { 3, "F",               "normal",   NO_COPY_FROM },
            /* Each add is an op_root */
            { 4, "file-added",      "normal",   NO_COPY_FROM },
            { 4, "D-added",         "normal",   NO_COPY_FROM },
            { 5, "D-added/D2",      "normal",   NO_COPY_FROM },
            /* Each copied-copy subtree is an op_root */
            { 4, "lambda-copied",   "normal",   1, source_base_file },
            { 4, "E-copied",        "normal",   1, source_base_dir },
            { 4, "E-copied/alpha",  "normal",   NO_COPY_FROM },
            { 4, "E-copied/beta",   "normal",   NO_COPY_FROM }
          } },

        { 0 }
      };
    struct subtest_t *subtest;

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
        SVN_ERR(svn_wc_copy3(b->wc_ctx,
                             wc_path(b, subtest->from_path),
                             wc_path(b, subtest->to_path),
                             FALSE /* metadata_only */,
                             NULL, NULL, NULL, NULL, b->pool));
        SVN_ERR(check_db_rows(b, subtest->to_path, subtest->expected));
      }
  }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_wc_wc_copies(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  wc_baton_t b;

  b.pool = pool;
  SVN_ERR(svn_test__create_repos_and_wc(&b.repos_url, &b.wc_abspath,
                                        "wc_wc_copies", opts, pool));
  SVN_ERR(svn_wc_context_create(&b.wc_ctx, NULL, pool, pool));

  return wc_wc_copies(&b);
}

static svn_error_t *
test_reverts(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  wc_baton_t b;
  nodes_row_t no_node_rows_expected[] = { { 0 } };

  b.pool = pool;
  SVN_ERR(svn_test__create_repos_and_wc(&b.repos_url, &b.wc_abspath,
                                        "reverts", opts, pool));
  SVN_ERR(svn_wc_context_create(&b.wc_ctx, NULL, pool, pool));

  SVN_ERR(wc_wc_copies(&b));


    /* Implement revert tests below, now that we have a wc with lots of
     copy-changes */

  SVN_ERR(wc_revert(&b, "A/B/D-added", svn_depth_infinity));
  SVN_ERR(check_db_rows(&b, "A/B/D-added", no_node_rows_expected));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_deletes(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  wc_baton_t b;

  b.pool = pool;
  SVN_ERR(svn_test__create_repos_and_wc(&b.repos_url, &b.wc_abspath,
                                        "deletes", opts, pool));
  SVN_ERR(svn_wc_context_create(&b.wc_ctx, NULL, pool, pool));
  SVN_ERR(add_and_commit_greek_tree(&b));

  file_write(&b,     "A/B/E/new-file", "New file");
  SVN_ERR(wc_add(&b, "A/B/E/new-file"));
  {
    nodes_row_t rows[] = {
      { 4, "A/B/E/new-file", "normal", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B/E/new-file", rows));
  }

  SVN_ERR(svn_wc_delete4(b.wc_ctx, wc_path(&b, "A/B/E/alpha"),
                         FALSE, TRUE, NULL, NULL, NULL, NULL, pool));
  {
    nodes_row_t rows[] = {
      { 0, "A/B/E/alpha", "normal",       1, "A/B/E/alpha" },
      { 4, "A/B/E/alpha", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B/E/alpha", rows));
  }

  SVN_ERR(svn_wc_delete4(b.wc_ctx, wc_path(&b, "A/B/F"),
                         FALSE, TRUE, NULL, NULL, NULL, NULL, pool));
  {
    nodes_row_t rows[] = {
      { 0, "A/B/F", "normal",       1, "A/B/F" },
      { 3, "A/B/F", "base-deleted", NO_COPY_FROM },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B/F", rows));
  }

  SVN_ERR(svn_wc_delete4(b.wc_ctx, wc_path(&b, "A/B"),
                         FALSE, TRUE, NULL, NULL, NULL, NULL, pool));
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
test_delete_of_copies(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  wc_baton_t b;

  b.pool = pool;
  SVN_ERR(svn_test__create_repos_and_wc(&b.repos_url, &b.wc_abspath,
                                        "deletes", opts, pool));
  SVN_ERR(svn_wc_context_create(&b.wc_ctx, NULL, pool, pool));
  SVN_ERR(add_and_commit_greek_tree(&b));
  SVN_ERR(wc_copy(&b, "A/B", "A/B-copied"));

  SVN_ERR(svn_wc_delete4(b.wc_ctx, wc_path(&b, "A/B-copied/E"),
                         FALSE, TRUE, NULL, NULL, NULL, NULL, pool));
  {
    nodes_row_t rows[] = {
      { 2, "A/B-copied/E",       "not-present",       1, "A/B/E" },
      { 2, "A/B-copied/E/alpha", "not-present",       NO_COPY_FROM},
      { 2, "A/B-copied/E/beta",  "not-present",       NO_COPY_FROM},
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B-copied/E", rows));
  }

  SVN_ERR(wc_copy(&b, "A/D/G", "A/B-copied/E"));
  {
    nodes_row_t rows[] = {
      { 2, "A/B-copied/E",       "not-present",       1, "A/B/E" },
      { 2, "A/B-copied/E/alpha", "not-present",       NO_COPY_FROM},
      { 2, "A/B-copied/E/beta",  "not-present",       NO_COPY_FROM},
      { 3, "A/B-copied/E",       "normal",            1, "A/D/G" },
      { 3, "A/B-copied/E/pi",    "normal",            NO_COPY_FROM},
      { 3, "A/B-copied/E/rho",   "normal",            NO_COPY_FROM},
      { 3, "A/B-copied/E/tau",   "normal",            NO_COPY_FROM},
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B-copied/E", rows));
  }

  SVN_ERR(svn_wc_delete4(b.wc_ctx, wc_path(&b, "A/B-copied/E/rho"),
                         FALSE, TRUE, NULL, NULL, NULL, NULL, pool));
  {
    nodes_row_t rows[] = {
      { 2, "A/B-copied/E",       "not-present",       1, "A/B/E" },
      { 2, "A/B-copied/E/alpha", "not-present",       NO_COPY_FROM},
      { 2, "A/B-copied/E/beta",  "not-present",       NO_COPY_FROM},
      { 3, "A/B-copied/E",       "normal",            1, "A/D/G" },
      { 3, "A/B-copied/E/pi",    "normal",            NO_COPY_FROM},
      { 3, "A/B-copied/E/rho",   "not-present",       NO_COPY_FROM},
      { 3, "A/B-copied/E/tau",   "normal",            NO_COPY_FROM},
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B-copied/E", rows));
  }

  SVN_ERR(svn_wc_delete4(b.wc_ctx, wc_path(&b, "A/B-copied/E"),
                         FALSE, TRUE, NULL, NULL, NULL, NULL, pool));
  {
    nodes_row_t rows[] = {
      { 2, "A/B-copied/E",       "not-present",       1, "A/B/E" },
      { 2, "A/B-copied/E/alpha", "not-present",       NO_COPY_FROM},
      { 2, "A/B-copied/E/beta",  "not-present",       NO_COPY_FROM},
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B-copied/E", rows));
  }

  SVN_ERR(wc_copy(&b, "A/B", "A/B-copied/E"));

  SVN_ERR(svn_wc_delete4(b.wc_ctx, wc_path(&b, "A/B-copied/E/F"),
                         FALSE, TRUE, NULL, NULL, NULL, NULL, pool));
  {
    nodes_row_t rows[] = {
      { 3, "A/B-copied/E/F", "not-present",       1, "A/B/F" },
      { 0 }
    };
    SVN_ERR(check_db_rows(&b, "A/B-copied/E/F", rows));
  }

  SVN_ERR(svn_wc_delete4(b.wc_ctx, wc_path(&b, "A/B-copied"),
                         FALSE, TRUE, NULL, NULL, NULL, NULL, pool));
  {
    nodes_row_t rows[] = { { 0 } };
    SVN_ERR(check_db_rows(&b, "A/B-copied", rows));
  }

  return SVN_NO_ERROR;
}

/* ---------------------------------------------------------------------- */
/* The list of test functions */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_WIMP(test_wc_wc_copies,
                       "wc_wc_copies",
                       "needs op_depth"),
    SVN_TEST_OPTS_WIMP(test_reverts,
                       "test_reverts",
                       "needs op_depth"),
    SVN_TEST_OPTS_WIMP(test_deletes,
                       "test_deletes",
                       "needs op_depth"),
    SVN_TEST_OPTS_WIMP(test_delete_of_copies,
                       "test_delete_of_copies",
                       "needs op_depth"),
    SVN_TEST_NULL
  };
