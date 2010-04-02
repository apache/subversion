/*
 * Regression tests for logic in the libsvn_client library.
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



#include <limits.h>
#include "svn_mergeinfo.h"
#include "../../libsvn_client/mergeinfo.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_repos.h"
#include "svn_subst.h"

#include "../svn_test.h"
#include "../svn_test_fs.h"

typedef struct {
  const char *path;
  const char *unparsed_mergeinfo;
  svn_boolean_t remains;
} mergeinfo_catalog_item;

#define MAX_ITEMS 10

static mergeinfo_catalog_item elide_testcases[][MAX_ITEMS] = {
  { {"/foo", "/bar: 1-4", TRUE},
    {"/foo/beep/baz", "/bar/beep/baz: 1-4", FALSE},
    { NULL }},
  { {"/foo", "/bar: 1-4", TRUE},
    {"/foo/beep/baz", "/blaa/beep/baz: 1-4", TRUE},
    { NULL }},
  { {"/", "/gah: 1-4", TRUE},
    {"/foo/beep/baz", "/gah/foo/beep/baz: 1-4", FALSE},
    { NULL }}
};

static svn_error_t *
test_elide_mergeinfo_catalog(apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool;

  iterpool = svn_pool_create(pool);

  for (i = 0;
       i < sizeof(elide_testcases) / sizeof(elide_testcases[0]);
       i++)
    {
      apr_hash_t *catalog;
      mergeinfo_catalog_item *item;

      svn_pool_clear(iterpool);

      catalog = apr_hash_make(iterpool);
      for (item = elide_testcases[i]; item->path; item++)
        {
          apr_hash_t *mergeinfo;

          SVN_ERR(svn_mergeinfo_parse(&mergeinfo, item->unparsed_mergeinfo,
                                      iterpool));
          apr_hash_set(catalog, item->path, APR_HASH_KEY_STRING, mergeinfo);
        }

      SVN_ERR(svn_client__elide_mergeinfo_catalog(catalog, iterpool));

      for (item = elide_testcases[i]; item->path; item++)
        {
          apr_hash_t *mergeinfo = apr_hash_get(catalog, item->path,
                                               APR_HASH_KEY_STRING);
          if (item->remains && !mergeinfo)
            return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "Elision for test case #%d incorrectly "
                                     "elided '%s'", i, item->path);
          if (!item->remains && mergeinfo)
            return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "Elision for test case #%d failed to "
                                     "elide '%s'", i, item->path);
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
test_args_to_target_array(apr_pool_t *pool)
{
  apr_size_t i;
  apr_pool_t *iterpool;
  svn_client_ctx_t *ctx;
  static struct {
    const char *input;
    const char *output; /* NULL means an error is expected. */
  } const tests[] = {
    { ".",                      "" },
    { ".@BASE",                 "@BASE" },
    { "foo///bar",              "foo/bar" },
    { "foo///bar@13",           "foo/bar@13" },
    { "foo///bar@HEAD",         "foo/bar@HEAD" },
    { "foo///bar@{1999-12-31}", "foo/bar@{1999-12-31}" },
    { "http://a//b////",        "http://a/b" },
    { "http://a///b@27",        "http://a/b@27" },
    { "http://a/b//@COMMITTED", "http://a/b@COMMITTED" },
    { "foo///bar@1:2",          "foo/bar@1:2" },
    { "foo///bar@baz",          "foo/bar@baz" },
    { "foo///bar@",             "foo/bar@" },
    { "foo///bar///@13",        "foo/bar@13" },
    { "foo///bar@@13",          "foo/bar@@13" },
    { "foo///@bar@HEAD",        "foo/@bar@HEAD" },
    { "foo@///bar",             "foo@/bar" },
    { "foo@HEAD///bar",         "foo@HEAD/bar" },
  };

  SVN_ERR(svn_client_create_context(&ctx, pool));

  iterpool = svn_pool_create(pool);

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      const char *input = tests[i].input;
      const char *expected_output = tests[i].output;
      apr_array_header_t *targets;
      apr_getopt_t *os;
      const int argc = 2;
      const char *argv[3] = { 0 };
      apr_status_t apr_err;
      svn_error_t *err;

      argv[0] = "opt-test";
      argv[1] = input;
      argv[2] = NULL;

      apr_err = apr_getopt_init(&os, iterpool, argc, argv);
      if (apr_err)
        return svn_error_wrap_apr(apr_err,
                                  "Error initializing command line arguments");

      err = svn_client_args_to_target_array(&targets, os, NULL, ctx, iterpool);

      if (expected_output)
        {
          const char *actual_output;

          if (err)
            return err;
          if (argc - 1 != targets->nelts)
            return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "Passed %d target(s) to "
                                     "svn_client_args_to_target_array() but "
                                     "got %d back.",
                                     argc - 1,
                                     targets->nelts);

          actual_output = APR_ARRAY_IDX(targets, 0, const char *);

          if (! svn_path_is_canonical(actual_output, iterpool))
            return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "Input '%s' to "
                                     "svn_client_args_to_target_array() should "
                                     "have returned a canonical path but "
                                     "'%s' is not.",
                                     input,
                                     actual_output);

          if (strcmp(expected_output, actual_output) != 0)
            return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "Input '%s' to "
                                     "svn_client_args_to_target_array() should "
                                     "have returned '%s' but returned '%s'.",
                                     input,
                                     expected_output,
                                     actual_output);
        }
      else
        {
          if (! err)
            return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "Unexpected success in passing '%s' "
                                     "to svn_client_args_to_target_array().",
                                     input);
        }
    }

  return SVN_NO_ERROR;
}


/* A helper function for test_patch().
 * It compares a patched or reject file against expected content.
 * It also deletes the file if the check was successful. */
static svn_error_t *
check_patch_result(const char *path, const char **expected_lines,
                   int num_expected_lines, apr_pool_t *pool)
{
  svn_stream_t *stream;
  apr_pool_t *iterpool;
  int i;

  SVN_ERR(svn_stream_open_readonly(&stream, path, pool, pool));
  i = 0;
  iterpool = svn_pool_create(pool);
  while (TRUE)
    {
      svn_boolean_t eof;
      svn_stringbuf_t *line;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_stream_readline(stream, &line, APR_EOL_STR, &eof, pool));
      if (i < num_expected_lines)
        if (strcmp(expected_lines[i++], line->data) != 0)
          return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                   "%s line %d didn't match the expected line "
                                   "(strlen=%d vs strlen=%d)", path, i,
                                   (int)strlen(expected_lines[i-1]),
                                   (int)strlen(line->data));

      if (eof)
        break;
    }
  svn_pool_destroy(iterpool);

  SVN_ERR_ASSERT(i == num_expected_lines);
  SVN_ERR(svn_io_remove_file2(path, FALSE, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_patch(const svn_test_opts_t *opts,
           apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  apr_hash_t *patched_tempfiles;
  apr_hash_t *reject_tempfiles;
  const char *repos_url;
  const char *wc_path;
  const char *cwd;
  svn_revnum_t committed_rev;
  svn_opt_revision_t rev;
  svn_opt_revision_t peg_rev;
  svn_client_ctx_t *ctx;
  apr_file_t *patch_file;
  const char *patch_file_path;
  const char *patched_tempfile_path;
  const char *reject_tempfile_path;
  const char *key;
  int i;
#define NL APR_EOL_STR
#define UNIDIFF_LINES 7
  const char *unidiff_patch[UNIDIFF_LINES] =  {
    "Index: A/D/gamma" NL,
    "===================================================================\n",
    "--- A/D/gamma\t(revision 1)" NL,
    "+++ A/D/gamma\t(working copy)" NL,
    "@@ -1 +1 @@" NL,
    "-This is really the file 'gamma'." NL,
    "+It is really the file 'gamma'." NL
  };
#define EXPECTED_GAMMA_LINES 1
  const char *expected_gamma[EXPECTED_GAMMA_LINES] = {
    "This is the file 'gamma'."
  };
#define EXPECTED_GAMMA_REJECT_LINES 5
  const char *expected_gamma_reject[EXPECTED_GAMMA_REJECT_LINES] = {
    "--- A/D/gamma",
    "+++ A/D/gamma",
    "@@ -1,1 +1,1 @@",
    "-This is really the file 'gamma'.",
    "+It is really the file 'gamma'."
  };

  /* Create a filesytem and repository. */
  SVN_ERR(svn_test__create_repos(&repos, "test-patch-repos",
                                 opts, pool));
  fs = svn_repos_fs(repos);

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &committed_rev, txn, pool));

  /* Check out the HEAD revision */
  SVN_ERR(svn_dirent_get_absolute(&cwd, "", pool));

  if (cwd[0] == '/')
    repos_url = apr_pstrcat(pool, "file://", cwd,
                            "/test-patch-repos", NULL);
  else
    /* On Windows CWD is always in "X:/..." style */
    repos_url = apr_pstrcat(pool, "file:///", cwd,
                            "/test-patch-repos", NULL);

  repos_url = svn_uri_canonicalize(repos_url, pool);

  /* Put wc inside an unversioned directory.  Checking out a 1.7 wc
     directly inside a 1.6 wc doesn't work reliably, an intervening
     unversioned directory prevents the problems. */
  wc_path = svn_dirent_join(cwd, "test-patch", pool);
  SVN_ERR(svn_io_make_dir_recursively(wc_path, pool));
  svn_test_add_dir_cleanup(wc_path);

  wc_path = svn_dirent_join(wc_path, "test-patch-wc", pool);
  SVN_ERR(svn_io_remove_dir2(wc_path, TRUE, NULL, NULL, pool));

  /* Create an extra directory level to avoid upgrade require message */
  SVN_ERR(svn_io_dir_make(wc_path, APR_OS_DEFAULT, pool));
  wc_path = svn_dirent_join(wc_path, "wc", pool);

  rev.kind = svn_opt_revision_head;
  peg_rev.kind = svn_opt_revision_unspecified;
  SVN_ERR(svn_client_create_context(&ctx, pool));
  SVN_ERR(svn_client_checkout3(NULL, repos_url, wc_path,
                               &peg_rev, &rev, svn_depth_infinity,
                               TRUE, FALSE, ctx, pool));

  /* Create the patch file. */
  patch_file_path = svn_dirent_join_many(pool, cwd,
                                         "test-patch", "test-patch.diff", NULL);
  SVN_ERR(svn_io_file_open(&patch_file, patch_file_path,
                           (APR_READ | APR_WRITE | APR_CREATE | APR_TRUNCATE),
                           APR_OS_DEFAULT, pool));
  for (i = 0; i < UNIDIFF_LINES; i++)
    {
      apr_size_t len = strlen(unidiff_patch[i]);
      SVN_ERR(svn_io_file_write(patch_file, unidiff_patch[i], &len, pool));
      SVN_ERR_ASSERT(len == strlen(unidiff_patch[i]));
    }
  SVN_ERR(svn_io_file_flush_to_disk(patch_file, pool));

  /* Apply the patch. */
  SVN_ERR(svn_client_patch(patch_file_path, wc_path, FALSE, 0, FALSE,
                           NULL, NULL, &patched_tempfiles, &reject_tempfiles,
                           ctx, pool, pool));
  SVN_ERR(svn_io_file_close(patch_file, pool));

  SVN_ERR_ASSERT(apr_hash_count(patched_tempfiles) == 1);
  key = "A/D/gamma";
  patched_tempfile_path = apr_hash_get(patched_tempfiles, key,
                                       APR_HASH_KEY_STRING);
  SVN_ERR(check_patch_result(patched_tempfile_path, expected_gamma,
                             EXPECTED_GAMMA_LINES, pool));
  SVN_ERR_ASSERT(apr_hash_count(reject_tempfiles) == 1);
  key = "A/D/gamma";
  reject_tempfile_path = apr_hash_get(reject_tempfiles, key,
                                     APR_HASH_KEY_STRING);
  SVN_ERR(check_patch_result(reject_tempfile_path, expected_gamma_reject,
                             EXPECTED_GAMMA_REJECT_LINES, pool));

  return SVN_NO_ERROR;
}

/* ========================================================================== */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_elide_mergeinfo_catalog,
                   "test svn_client__elide_mergeinfo_catalog"),
    SVN_TEST_PASS2(test_args_to_target_array,
                   "test svn_client_args_to_target_array"),
    SVN_TEST_OPTS_PASS(test_patch, "test svn_client_patch"),
    SVN_TEST_NULL
  };
