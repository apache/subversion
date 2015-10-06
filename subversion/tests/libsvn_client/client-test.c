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



#define SVN_DEPRECATED

#include <limits.h>
#include "svn_mergeinfo.h"
#include "../../libsvn_client/mergeinfo.h"
#include "../../libsvn_client/client.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "private/svn_client_mtcc.h"
#include "svn_repos.h"
#include "svn_subst.h"
#include "private/svn_sorts_private.h"
#include "private/svn_wc_private.h"
#include "svn_props.h"
#include "svn_hash.h"

#include "../svn_test.h"
#include "../svn_test_fs.h"


/* Create a repository with a filesystem based on OPTS in a subdir NAME,
 * commit the standard Greek tree as revision 1, and set *REPOS_URL to
 * the URL we will use to access it.
 *
 * ### This always returns a file: URL. We should upgrade this to use the
 *     test suite's specified URL scheme instead. */
static svn_error_t *
create_greek_repos(const char **repos_url,
                   const char *name,
                   const svn_test_opts_t *opts,
                   apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_revnum_t committed_rev;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  /* Create a filesytem and repository. */
  SVN_ERR(svn_test__create_repos(
              &repos, svn_test_data_path(name, pool), opts, pool));

  /* Prepare and commit a txn containing the Greek tree. */
  SVN_ERR(svn_fs_begin_txn2(&txn, svn_repos_fs(repos), 0 /* rev */,
                            0 /* flags */, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &committed_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(committed_rev));

  SVN_ERR(svn_uri_get_file_url_from_dirent(
              repos_url, svn_test_data_path(name, pool), pool));
  return SVN_NO_ERROR;
}


typedef struct mergeinfo_catalog_item {
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
      svn_mergeinfo_catalog_t mergeinfo_catalog;
      mergeinfo_catalog_item *item;

      svn_pool_clear(iterpool);

      mergeinfo_catalog = apr_hash_make(iterpool);
      for (item = elide_testcases[i]; item->path; item++)
        {
          svn_mergeinfo_t mergeinfo;

          SVN_ERR(svn_mergeinfo_parse(&mergeinfo, item->unparsed_mergeinfo,
                                      iterpool));
          apr_hash_set(mergeinfo_catalog, item->path, APR_HASH_KEY_STRING,
                       mergeinfo);
        }

      SVN_ERR(svn_client__elide_mergeinfo_catalog(mergeinfo_catalog,
                                                  iterpool));

      for (item = elide_testcases[i]; item->path; item++)
        {
          apr_hash_t *mergeinfo = apr_hash_get(mergeinfo_catalog, item->path,
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

      err = svn_client_args_to_target_array2(&targets, os, NULL, ctx, FALSE,
                                             iterpool);

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
 * It compares a patched or reject file against expected content using the
 *  specified EOL. It also deletes the file if the check was successful. */
static svn_error_t *
check_patch_result(const char *path, const char **expected_lines, const char *eol,
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

      SVN_ERR(svn_stream_readline(stream, &line, eol, &eof, pool));
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

  SVN_TEST_ASSERT(i == num_expected_lines);
  SVN_ERR(svn_stream_close(stream));
  SVN_ERR(svn_io_remove_file2(path, FALSE, pool));

  return SVN_NO_ERROR;
}

/* A baton for the patch collection function. */
struct patch_collection_baton
{
  apr_hash_t *patched_tempfiles;
  apr_hash_t *reject_tempfiles;
  apr_pool_t *state_pool;
};

/* Collect all the patch information we're interested in. */
static svn_error_t *
patch_collection_func(void *baton,
                      svn_boolean_t *filtered,
                      const char *canon_path_from_patchfile,
                      const char *patch_abspath,
                      const char *reject_abspath,
                      apr_pool_t *scratch_pool)
{
  struct patch_collection_baton *pcb = baton;

  if (patch_abspath)
    apr_hash_set(pcb->patched_tempfiles,
                 apr_pstrdup(pcb->state_pool, canon_path_from_patchfile),
                 APR_HASH_KEY_STRING,
                 apr_pstrdup(pcb->state_pool, patch_abspath));

  if (reject_abspath)
    apr_hash_set(pcb->reject_tempfiles,
                 apr_pstrdup(pcb->state_pool, canon_path_from_patchfile),
                 APR_HASH_KEY_STRING,
                 apr_pstrdup(pcb->state_pool, reject_abspath));

  if (filtered)
    *filtered = FALSE;

  return SVN_NO_ERROR;
}

static svn_error_t *
test_patch(const svn_test_opts_t *opts,
           apr_pool_t *pool)
{
  const char *repos_url;
  const char *wc_path;
  svn_opt_revision_t rev;
  svn_opt_revision_t peg_rev;
  svn_client_ctx_t *ctx;
  apr_file_t *patch_file;
  struct patch_collection_baton pcb;
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

  /* Create a filesytem and repository containing the Greek tree. */
  SVN_ERR(create_greek_repos(&repos_url, "test-patch-repos", opts, pool));

  /* Check out the HEAD revision */

  /* Put wc inside an unversioned directory.  Checking out a 1.7 wc
     directly inside a 1.6 wc doesn't work reliably, an intervening
     unversioned directory prevents the problems. */
  wc_path = svn_test_data_path("test-patch", pool);
  SVN_ERR(svn_io_make_dir_recursively(wc_path, pool));
  svn_test_add_dir_cleanup(wc_path);

  wc_path = svn_dirent_join(wc_path, "test-patch-wc", pool);
  SVN_ERR(svn_io_remove_dir2(wc_path, TRUE, NULL, NULL, pool));
  rev.kind = svn_opt_revision_head;
  peg_rev.kind = svn_opt_revision_unspecified;
  SVN_ERR(svn_client_create_context(&ctx, pool));
  SVN_ERR(svn_client_checkout3(NULL, repos_url, wc_path,
                               &peg_rev, &rev, svn_depth_infinity,
                               TRUE, FALSE, ctx, pool));

  /* Create the patch file. */
  patch_file_path = svn_dirent_join_many(
      pool, svn_test_data_path("test-patch", pool),
      "test-patch.diff", SVN_VA_NULL);
  SVN_ERR(svn_io_file_open(&patch_file, patch_file_path,
                           (APR_READ | APR_WRITE | APR_CREATE | APR_TRUNCATE),
                           APR_OS_DEFAULT, pool));
  for (i = 0; i < UNIDIFF_LINES; i++)
    {
      apr_size_t len = strlen(unidiff_patch[i]);
      SVN_ERR(svn_io_file_write(patch_file, unidiff_patch[i], &len, pool));
      SVN_TEST_ASSERT(len == strlen(unidiff_patch[i]));
    }
  SVN_ERR(svn_io_file_flush(patch_file, pool));

  /* Apply the patch. */
  pcb.patched_tempfiles = apr_hash_make(pool);
  pcb.reject_tempfiles = apr_hash_make(pool);
  pcb.state_pool = pool;
  SVN_ERR(svn_client_patch(patch_file_path, wc_path, FALSE, 0, FALSE,
                           FALSE, FALSE, patch_collection_func, &pcb,
                           ctx, pool));
  SVN_ERR(svn_io_file_close(patch_file, pool));

  SVN_TEST_ASSERT(apr_hash_count(pcb.patched_tempfiles) == 1);
  key = "A/D/gamma";
  patched_tempfile_path = apr_hash_get(pcb.patched_tempfiles, key,
                                       APR_HASH_KEY_STRING);
  SVN_ERR(check_patch_result(patched_tempfile_path, expected_gamma, "\n",
                             EXPECTED_GAMMA_LINES, pool));
  SVN_TEST_ASSERT(apr_hash_count(pcb.reject_tempfiles) == 1);
  key = "A/D/gamma";
  reject_tempfile_path = apr_hash_get(pcb.reject_tempfiles, key,
                                     APR_HASH_KEY_STRING);
  SVN_ERR(check_patch_result(reject_tempfile_path, expected_gamma_reject,
                             APR_EOL_STR, EXPECTED_GAMMA_REJECT_LINES, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_wc_add_scenarios(const svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  const char *repos_url;
  const char *wc_path;
  svn_revnum_t committed_rev;
  svn_client_ctx_t *ctx;
  svn_opt_revision_t rev, peg_rev;
  const char *new_dir_path;
  const char *ex_file_path;
  const char *ex_dir_path;
  const char *ex2_dir_path;

  /* Create a filesytem and repository containing the Greek tree. */
  SVN_ERR(create_greek_repos(&repos_url, "test-wc-add-repos", opts, pool));
  committed_rev = 1;

  wc_path = svn_test_data_path("test-wc-add", pool);

  /* Remove old test data from the previous run */
  SVN_ERR(svn_io_remove_dir2(wc_path, TRUE, NULL, NULL, pool));

  SVN_ERR(svn_io_make_dir_recursively(wc_path, pool));
  svn_test_add_dir_cleanup(wc_path);

  rev.kind = svn_opt_revision_head;
  peg_rev.kind = svn_opt_revision_unspecified;
  SVN_ERR(svn_client_create_context(&ctx, pool));
  /* Checkout greek tree as wc_path */
  SVN_ERR(svn_client_checkout3(NULL, repos_url, wc_path, &peg_rev, &rev,
                               svn_depth_infinity, FALSE, FALSE, ctx, pool));

  /* Now checkout again as wc_path/NEW */
  new_dir_path = svn_dirent_join(wc_path, "NEW", pool);
  SVN_ERR(svn_client_checkout3(NULL, repos_url, new_dir_path, &peg_rev, &rev,
                               svn_depth_infinity, FALSE, FALSE,
                               ctx, pool));

  ex_dir_path = svn_dirent_join(wc_path, "NEW_add", pool);
  ex2_dir_path = svn_dirent_join(wc_path, "NEW_add2", pool);
  SVN_ERR(svn_io_dir_make(ex_dir_path, APR_OS_DEFAULT, pool));
  SVN_ERR(svn_io_dir_make(ex2_dir_path, APR_OS_DEFAULT, pool));

  SVN_ERR(svn_io_open_uniquely_named(NULL, &ex_file_path, wc_path, "new_file",
                                     NULL, svn_io_file_del_none, pool, pool));

  /* Now use an access baton to do some add operations like an old client
     might do */
  {
    svn_wc_adm_access_t *adm_access, *adm2;
    svn_boolean_t locked;

    SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, wc_path, TRUE, -1, NULL, NULL,
                             pool));

    /* ### The above svn_wc_adm_open3 creates a new svn_wc__db_t
       ### instance.  The svn_wc_add3 below doesn't work while the
       ### original svn_wc__db_t created by svn_client_create_context
       ### remains open.  Closing the wc-context gets around the
       ### problem but is obviously a hack. */
    SVN_ERR(svn_wc_context_destroy(ctx->wc_ctx));
    SVN_ERR(svn_wc_context_create(&ctx->wc_ctx, NULL, pool, pool));

    /* Fix up copy as add with history */
    SVN_ERR(svn_wc_add3(new_dir_path, adm_access, svn_depth_infinity,
                        repos_url, committed_rev, NULL, NULL, NULL, NULL,
                        pool));

    /* Verify if the paths are locked now */
    SVN_ERR(svn_wc_locked(&locked, wc_path, pool));
    SVN_TEST_ASSERT(locked && "wc_path locked");
    SVN_ERR(svn_wc_locked(&locked, new_dir_path, pool));
    SVN_TEST_ASSERT(locked && "new_path locked");

    SVN_ERR(svn_wc_adm_retrieve(&adm2, adm_access, new_dir_path, pool));
    SVN_TEST_ASSERT(adm2 != NULL && "available in set");

    /* Add local (new) file */
    SVN_ERR(svn_wc_add3(ex_file_path, adm_access, svn_depth_unknown, NULL,
                        SVN_INVALID_REVNUM, NULL, NULL, NULL, NULL, pool));

    /* Add local (new) directory */
    SVN_ERR(svn_wc_add3(ex_dir_path, adm_access, svn_depth_infinity, NULL,
                        SVN_INVALID_REVNUM, NULL, NULL, NULL, NULL, pool));

    SVN_ERR(svn_wc_adm_retrieve(&adm2, adm_access, ex_dir_path, pool));
    SVN_TEST_ASSERT(adm2 != NULL && "available in set");

    /* Add empty directory with copy trail */
    SVN_ERR(svn_wc_add3(ex2_dir_path, adm_access, svn_depth_infinity,
                        repos_url, committed_rev, NULL, NULL, NULL, NULL,
                        pool));

    SVN_ERR(svn_wc_adm_retrieve(&adm2, adm_access, ex2_dir_path, pool));
    SVN_TEST_ASSERT(adm2 != NULL && "available in set");

    SVN_ERR(svn_wc_adm_close2(adm_access, pool));
  }

  /* Some simple status calls to verify that the paths are added */
  {
    svn_wc_status3_t *status;

    SVN_ERR(svn_wc_status3(&status, ctx->wc_ctx, new_dir_path, pool, pool));

    SVN_TEST_ASSERT(status->node_status == svn_wc_status_added
                    && status->copied
                    && !strcmp(status->repos_relpath, "NEW"));

    SVN_ERR(svn_wc_status3(&status, ctx->wc_ctx, ex_file_path, pool, pool));

    SVN_TEST_ASSERT(status->node_status == svn_wc_status_added
                    && !status->copied);

    SVN_ERR(svn_wc_status3(&status, ctx->wc_ctx, ex_dir_path, pool, pool));

    SVN_TEST_ASSERT(status->node_status == svn_wc_status_added
                    && !status->copied);

    SVN_ERR(svn_wc_status3(&status, ctx->wc_ctx, ex2_dir_path, pool, pool));

    SVN_TEST_ASSERT(status->node_status == svn_wc_status_added
                    && status->copied);
  }

  /* ### Add a commit? */

  return SVN_NO_ERROR;
}

/* This is for issue #3234. */
static svn_error_t *
test_copy_crash(const svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  apr_array_header_t *sources;
  svn_opt_revision_t rev;
  svn_client_copy_source_t source;
  svn_client_ctx_t *ctx;
  const char *dest;
  const char *repos_url;

  /* Create a filesytem and repository containing the Greek tree. */
  SVN_ERR(create_greek_repos(&repos_url, "test-copy-crash", opts, pool));

  SVN_ERR(svn_client_create_context(&ctx, pool));

  rev.kind = svn_opt_revision_head;
  dest = svn_path_url_add_component2(repos_url, "A/E", pool);
  source.path = svn_path_url_add_component2(repos_url, "A/B", pool);
  source.revision = &rev;
  source.peg_revision = &rev;
  sources = apr_array_make(pool, 1, sizeof(svn_client_copy_source_t *));
  APR_ARRAY_PUSH(sources, svn_client_copy_source_t *) = &source;

  /* This shouldn't crash. */
  SVN_ERR(svn_client_copy6(sources, dest, FALSE, TRUE, FALSE, NULL, NULL, NULL,
                           ctx, pool));

  return SVN_NO_ERROR;
}

#ifdef TEST16K_ADD
static svn_error_t *
test_16k_add(const svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  svn_opt_revision_t rev;
  svn_client_ctx_t *ctx;
  const char *repos_url;
  const char *wc_path;
  svn_opt_revision_t peg_rev;
  apr_array_header_t *targets;
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i;

  /* Create a filesytem and repository containing the Greek tree. */
  SVN_ERR(create_greek_repos(&repos_url, "test-16k-repos", opts, pool));

  /* Check out the HEAD revision */

  /* Put wc inside an unversioned directory.  Checking out a 1.7 wc
     directly inside a 1.6 wc doesn't work reliably, an intervening
     unversioned directory prevents the problems. */
  wc_path = svn_test_data_path("test-16k", pool);
  SVN_ERR(svn_io_make_dir_recursively(wc_path, pool));
  svn_test_add_dir_cleanup(wc_path);

  wc_path = svn_dirent_join(wc_path, "trunk", pool);
  SVN_ERR(svn_io_remove_dir2(wc_path, TRUE, NULL, NULL, pool));
  rev.kind = svn_opt_revision_head;
  peg_rev.kind = svn_opt_revision_unspecified;
  SVN_ERR(svn_client_create_context(&ctx, pool));
  SVN_ERR(svn_client_checkout3(NULL, repos_url, wc_path,
                               &peg_rev, &rev, svn_depth_infinity,
                               TRUE, FALSE, ctx, pool));

  for (i = 0; i < 16384; i++)
    {
      const char *path;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_io_open_unique_file3(NULL, &path, wc_path,
                                       svn_io_file_del_none,
                                       iterpool, iterpool));

      SVN_ERR(svn_client_add5(path, svn_depth_unknown, FALSE, FALSE, FALSE,
                              FALSE, ctx, iterpool));
    }

  targets = apr_array_make(pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(targets, const char *) = wc_path;
  svn_pool_clear(iterpool);

  SVN_ERR(svn_client_commit5(targets, svn_depth_infinity, FALSE, FALSE, TRUE,
                             NULL, NULL, NULL, NULL, ctx, iterpool));


  return SVN_NO_ERROR;
}
#endif

static svn_error_t *
test_youngest_common_ancestor(const svn_test_opts_t *opts,
                              apr_pool_t *pool)
{
  const char *repos_url;
  const char *repos_uuid = "fake-uuid";  /* the functions we call don't care */
  svn_client_ctx_t *ctx;
  svn_opt_revision_t head_rev = { svn_opt_revision_head, { 0 } };
  svn_opt_revision_t zero_rev = { svn_opt_revision_number, { 0 } };
  svn_client_copy_source_t source;
  apr_array_header_t *sources;
  const char *dest;
  svn_client__pathrev_t *yc_ancestor;

  /* Create a filesytem and repository containing the Greek tree. */
  SVN_ERR(create_greek_repos(&repos_url, "test-youngest-common-ancestor", opts, pool));

  SVN_ERR(svn_client_create_context(&ctx, pool));

  /* Copy a file into dir 'A', keeping its own basename. */
  sources = apr_array_make(pool, 1, sizeof(svn_client_copy_source_t *));
  source.path = svn_path_url_add_component2(repos_url, "iota", pool);
  source.peg_revision = &head_rev;
  source.revision = &head_rev;
  APR_ARRAY_PUSH(sources, svn_client_copy_source_t *) = &source;
  dest = svn_path_url_add_component2(repos_url, "A", pool);
  SVN_ERR(svn_client_copy6(sources, dest, TRUE /* copy_as_child */,
                           FALSE /* make_parents */,
                           FALSE /* ignore_externals */,
                           NULL, NULL, NULL, ctx, pool));

  /* Test: YCA(iota@2, A/iota@2) is iota@1. */
  SVN_ERR(svn_client__get_youngest_common_ancestor(
            &yc_ancestor,
            svn_client__pathrev_create_with_relpath(
              repos_url, repos_uuid, 2, "iota", pool),
            svn_client__pathrev_create_with_relpath(
              repos_url, repos_uuid, 2, "A/iota", pool),
            NULL, ctx, pool, pool));
  SVN_TEST_STRING_ASSERT(svn_client__pathrev_relpath(yc_ancestor, pool),
                         "iota");
  SVN_TEST_ASSERT(yc_ancestor->rev == 1);

  /* Copy the root directory (at revision 0) into A as 'ROOT'. */
  sources = apr_array_make(pool, 1, sizeof(svn_client_copy_source_t *));
  source.path = repos_url;
  source.peg_revision = &zero_rev;
  source.revision = &zero_rev;
  APR_ARRAY_PUSH(sources, svn_client_copy_source_t *) = &source;
  dest = svn_path_url_add_component2(repos_url, "A/ROOT", pool);
  SVN_ERR(svn_client_copy6(sources, dest, FALSE /* copy_as_child */,
                           FALSE /* make_parents */,
                           FALSE /* ignore_externals */,
                           NULL, NULL, NULL, ctx, pool));

  /* Test: YCA(''@0, A/ROOT@3) is ''@0 (handled as a special case). */
  SVN_ERR(svn_client__get_youngest_common_ancestor(
            &yc_ancestor,
            svn_client__pathrev_create_with_relpath(
              repos_url, repos_uuid, 0, "", pool),
            svn_client__pathrev_create_with_relpath(
              repos_url, repos_uuid, 3, "A/ROOT", pool),
            NULL, ctx, pool, pool));
  SVN_TEST_STRING_ASSERT(svn_client__pathrev_relpath(yc_ancestor, pool), "");
  SVN_TEST_ASSERT(yc_ancestor->rev == 0);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_foreign_repos_copy(const svn_test_opts_t *opts,
                        apr_pool_t *pool)
{
  svn_opt_revision_t rev;
  svn_opt_revision_t peg_rev;
  const char *repos_url;
  const char *repos2_url;
  const char *wc_path;
  svn_client_ctx_t *ctx;
/* Create a filesytem and repository containing the Greek tree. */
  SVN_ERR(create_greek_repos(&repos_url, "foreign-copy1", opts, pool));
  SVN_ERR(create_greek_repos(&repos2_url, "foreign-copy2", opts, pool));

  wc_path = svn_test_data_path("test-foreign-repos-copy", pool);

  /* Remove old test data from the previous run */
  SVN_ERR(svn_io_remove_dir2(wc_path, TRUE, NULL, NULL, pool));

  SVN_ERR(svn_io_make_dir_recursively(wc_path, pool));
  svn_test_add_dir_cleanup(wc_path);

  wc_path = svn_dirent_join(wc_path, "foreign-wc", pool);

  rev.kind = svn_opt_revision_head;
  peg_rev.kind = svn_opt_revision_unspecified;
  SVN_ERR(svn_client_create_context(&ctx, pool));
  /* Checkout greek tree as wc_path */
  SVN_ERR(svn_client_checkout3(NULL, repos_url, wc_path, &peg_rev, &rev,
                               svn_depth_infinity, FALSE, FALSE, ctx, pool));

  SVN_ERR(svn_client__copy_foreign(svn_path_url_add_component2(repos2_url, "A",
                                                               pool),
                                   svn_dirent_join(wc_path, "A-copied", pool),
                                   &peg_rev, &rev, svn_depth_infinity, FALSE, FALSE,
                                   ctx, pool));


  SVN_ERR(svn_client__copy_foreign(svn_path_url_add_component2(repos2_url,
                                                               "iota",
                                                               pool),
                                   svn_dirent_join(wc_path, "iota-copied", pool),
                                   &peg_rev, &rev, svn_depth_infinity, FALSE, FALSE,
                                   ctx, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_suggest_mergesources(const svn_test_opts_t *opts,
                          apr_pool_t *pool)
{
  const char *repos_url;
  svn_client_ctx_t *ctx;
  svn_client__mtcc_t *mtcc;
  apr_array_header_t *results;
  svn_opt_revision_t peg_rev;
  svn_opt_revision_t head_rev;
  const char *wc_path;

  peg_rev.kind = svn_opt_revision_unspecified;

  /* Create a filesytem and repository containing the Greek tree. */
  SVN_ERR(create_greek_repos(&repos_url, "mergesources", opts, pool));

  SVN_ERR(svn_client_create_context(&ctx, pool));

  SVN_ERR(svn_client__mtcc_create(&mtcc, repos_url, -1, ctx, pool, pool));
  SVN_ERR(svn_client__mtcc_add_copy("A", 1, "AA", mtcc, pool));
  SVN_ERR(svn_client__mtcc_commit(NULL, NULL, NULL, mtcc, pool));

  SVN_ERR(svn_client_suggest_merge_sources(
                    &results,
                    svn_path_url_add_component2(repos_url, "AA", pool),
                    &peg_rev, ctx, pool));
  SVN_TEST_ASSERT(results != NULL);
  SVN_TEST_ASSERT(results->nelts >= 1);
  SVN_TEST_STRING_ASSERT(APR_ARRAY_IDX(results, 0, const char *),
                          svn_path_url_add_component2(repos_url, "A", pool));

  /* And now test the same thing with a minimal working copy */
  wc_path = svn_test_data_path("mergesources-wc", pool);
  svn_test_add_dir_cleanup(wc_path);
  SVN_ERR(svn_io_remove_dir2(wc_path, TRUE, NULL, NULL, pool));

  head_rev.kind = svn_opt_revision_head;
  SVN_ERR(svn_client_checkout3(NULL,
                               svn_path_url_add_component2(repos_url, "AA", pool),
                               wc_path,
                               &head_rev, &head_rev, svn_depth_empty,
                               FALSE, FALSE, ctx, pool));


  SVN_ERR(svn_client_suggest_merge_sources(&results,
                                           wc_path,
                                           &peg_rev, ctx, pool));
  SVN_TEST_ASSERT(results != NULL);
  SVN_TEST_ASSERT(results->nelts >= 1);
  SVN_TEST_STRING_ASSERT(APR_ARRAY_IDX(results, 0, const char *),
                          svn_path_url_add_component2(repos_url, "A", pool));

  return SVN_NO_ERROR;
}


static char
status_to_char(enum svn_wc_status_kind status)
{

  switch (status)
    {
    case svn_wc_status_none:        return '.';
    case svn_wc_status_unversioned: return '?';
    case svn_wc_status_normal:      return '-';
    case svn_wc_status_added:       return 'A';
    case svn_wc_status_missing:     return '!';
    case svn_wc_status_incomplete:  return ':';
    case svn_wc_status_deleted:     return 'D';
    case svn_wc_status_replaced:    return 'R';
    case svn_wc_status_modified:    return 'M';
    case svn_wc_status_merged:      return 'G';
    case svn_wc_status_conflicted:  return 'C';
    case svn_wc_status_obstructed:  return '~';
    case svn_wc_status_ignored:     return 'I';
    case svn_wc_status_external:    return 'X';
    default:                        return '*';
    }
}

static int
compare_status_paths(const void *a, const void *b)
{
  const svn_client_status_t *const *const sta = a;
  const svn_client_status_t *const *const stb = b;
  return svn_path_compare_paths((*sta)->local_abspath, (*stb)->local_abspath);
}

static svn_error_t *
remote_only_status_receiver(void *baton, const char *path,
                            const svn_client_status_t *status,
                            apr_pool_t *scratch_pool)
{
  apr_array_header_t *results = baton;
  APR_ARRAY_PUSH(results, const svn_client_status_t *) =
    svn_client_status_dup(status, results->pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
test_remote_only_status(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  static const struct remote_only_status_result
  {
    const char *relpath;
    svn_revnum_t revision;
    enum svn_wc_status_kind node_status;
    enum svn_wc_status_kind text_status;
    enum svn_wc_status_kind prop_status;
    svn_revnum_t ood_changed_rev;
    enum svn_wc_status_kind repos_node_status;
    enum svn_wc_status_kind repos_text_status;
    enum svn_wc_status_kind repos_prop_status;
  } expected[] = {
    { ".",
      +1, svn_wc_status_normal,   svn_wc_status_normal,   svn_wc_status_none,
      +2, svn_wc_status_modified, svn_wc_status_modified, svn_wc_status_none },
    { "B",
      +1, svn_wc_status_normal,   svn_wc_status_normal,   svn_wc_status_none,
      +2, svn_wc_status_none,     svn_wc_status_none,     svn_wc_status_none },
    { "C",
      +1, svn_wc_status_normal,   svn_wc_status_normal,   svn_wc_status_none,
      +2, svn_wc_status_deleted,  svn_wc_status_none,     svn_wc_status_none },
    { "D",
      +1, svn_wc_status_normal,   svn_wc_status_normal,   svn_wc_status_none,
      +2, svn_wc_status_none,     svn_wc_status_none,     svn_wc_status_none },
    { "epsilon",
      -1, svn_wc_status_none,     svn_wc_status_none,     svn_wc_status_none,
      +2, svn_wc_status_added,    svn_wc_status_modified, svn_wc_status_none },
    { "mu",
      +1, svn_wc_status_normal,   svn_wc_status_normal,   svn_wc_status_none,
      +2, svn_wc_status_modified, svn_wc_status_normal,   svn_wc_status_none },

    { NULL }
  };

  const char *repos_url;
  const char *wc_path;
  const char *local_path;
  apr_file_t *local_file;
  svn_client_ctx_t *ctx;
  svn_client__mtcc_t *mtcc;
  svn_opt_revision_t rev;
  svn_revnum_t result_rev;
  svn_string_t *contents = svn_string_create("modified\n", pool);
  svn_stream_t *contentstream = svn_stream_from_string(contents, pool);
  const struct remote_only_status_result *ex;
  svn_stream_mark_t *start;
  apr_array_header_t *targets;
  apr_array_header_t *results;
  int i;

  SVN_ERR(svn_stream_mark(contentstream, &start, pool));

  /* Create a filesytem and repository containing the Greek tree. */
  SVN_ERR(create_greek_repos(&repos_url, "test-remote-only-status", opts, pool));

  SVN_ERR(svn_client_create_context(&ctx, pool));

  /* Make some modifications in the repository, creating revision 2. */
  SVN_ERR(svn_client__mtcc_create(&mtcc, repos_url, -1, ctx, pool, pool));
  SVN_ERR(svn_stream_seek(contentstream, start));
  SVN_ERR(svn_client__mtcc_add_add_file("A/epsilon", contentstream, NULL,
                                        mtcc, pool));
  SVN_ERR(svn_stream_seek(contentstream, start));
  SVN_ERR(svn_client__mtcc_add_update_file("A/mu",
                                           contentstream, NULL, NULL, NULL,
                                           mtcc, pool));
  SVN_ERR(svn_stream_seek(contentstream, start));
  SVN_ERR(svn_client__mtcc_add_add_file("A/D/epsilon", contentstream, NULL,
                                        mtcc, pool));
  SVN_ERR(svn_stream_seek(contentstream, start));
  SVN_ERR(svn_client__mtcc_add_update_file("A/B/lambda",
                                           contentstream, NULL, NULL, NULL,
                                           mtcc, pool));
  SVN_ERR(svn_client__mtcc_add_delete("A/C", mtcc, pool));
  SVN_ERR(svn_client__mtcc_commit(NULL, NULL, NULL, mtcc, pool));

  /* Check out a sparse root @r1 of the repository */
  wc_path = svn_test_data_path("test-remote-only-status-wc", pool);
  svn_test_add_dir_cleanup(wc_path);
  SVN_ERR(svn_io_remove_dir2(wc_path, TRUE, NULL, NULL, pool));

  rev.kind = svn_opt_revision_number;
  rev.value.number = 1;
  SVN_ERR(svn_client_checkout3(NULL,
                               apr_pstrcat(pool, repos_url, "/A", SVN_VA_NULL),
                               wc_path, &rev, &rev, svn_depth_immediates,
                               FALSE, FALSE, ctx, pool));

  /* Add a local file; this is a double-check to make sure that
     remote-only status ignores local changes. */
  local_path = svn_dirent_join(wc_path, "zeta", pool);
  SVN_ERR(svn_io_file_create_empty(local_path, pool));
  SVN_ERR(svn_client_add5(local_path, svn_depth_unknown,
                          FALSE, FALSE, FALSE, FALSE,
                          ctx, pool));

  /* Replace a local dir */
  local_path = svn_dirent_join(wc_path, "B", pool);
  targets = apr_array_make(pool, 1, sizeof(const char*));
  APR_ARRAY_PUSH(targets, const char*) = local_path;
  SVN_ERR(svn_client_delete4(targets, FALSE, FALSE, NULL, NULL, NULL,
                             ctx, pool));
  SVN_ERR(svn_client_mkdir4(targets, FALSE, NULL, NULL, NULL,
                            ctx, pool));

  /* Modify a local dir's props */
  local_path = svn_dirent_join(wc_path, "D", pool);
  targets = apr_array_make(pool, 1, sizeof(const char*));
  APR_ARRAY_PUSH(targets, const char*) = local_path;
  SVN_ERR(svn_client_propset_local("prop", contents, targets,
                                   svn_depth_empty, FALSE, NULL,
                                   ctx, pool));

  /* Modify a local file's contents */
  local_path = svn_dirent_join(wc_path, "mu", pool);
  SVN_ERR(svn_io_file_open(&local_file, local_path,
                           APR_FOPEN_WRITE | APR_FOPEN_TRUNCATE,
                           0, pool));
  SVN_ERR(svn_io_file_write_full(local_file,
                                 contents->data, contents->len,
                                 NULL, pool));
  SVN_ERR(svn_io_file_close(local_file, pool));

  /* Run the remote-only status. */
  results = apr_array_make(pool, 3, sizeof(const svn_client_status_t *));
  rev.kind = svn_opt_revision_head;
  SVN_ERR(svn_client_status6(
              &result_rev, ctx, wc_path, &rev, svn_depth_unknown,
              TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, NULL,
              remote_only_status_receiver, results, pool));

  SVN_TEST_ASSERT(result_rev == 2);

  /* Compare the number of results with the expected results */
  for (i = 0, ex = expected; ex->relpath; ++ex, ++i)
    ;
  SVN_TEST_ASSERT(results->nelts == i);

  if (opts->verbose)
    svn_sort__array(results, compare_status_paths);

  for (i = 0; i < results->nelts; ++i)
    {
      const svn_client_status_t *st =
        APR_ARRAY_IDX(results, i, const svn_client_status_t *);

      const char *relpath =
        svn_dirent_skip_ancestor(wc_path, st->local_abspath);
      if (!relpath)
        relpath = st->local_abspath;
      if (!*relpath)
        relpath = ".";

      for (ex = expected; ex->relpath; ++ex)
        {
          if (0 == strcmp(relpath, ex->relpath))
            break;
        }
      SVN_TEST_ASSERT(ex->relpath != NULL);

      if (opts->verbose)
        printf("%c%c%c %2ld  %c%c%c %2ld  %s\n",
               status_to_char(st->node_status),
               status_to_char(st->text_status),
               status_to_char(st->prop_status),
               (long)st->revision,
               status_to_char(st->repos_node_status),
               status_to_char(st->repos_text_status),
               status_to_char(st->repos_prop_status),
               (long)st->ood_changed_rev,
               relpath);

      SVN_TEST_ASSERT(st->revision == ex->revision);
      SVN_TEST_ASSERT(st->ood_changed_rev == ex->ood_changed_rev);
      SVN_TEST_ASSERT(st->node_status == ex->node_status);
      SVN_TEST_ASSERT(st->repos_node_status == ex->repos_node_status);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_copy_pin_externals(const svn_test_opts_t *opts,
                        apr_pool_t *pool)
{
  svn_opt_revision_t rev;
  svn_opt_revision_t peg_rev;
  const char *repos_url;
  const char *A_url;
  const char *A_copy_url;
  const char *wc_path;
  svn_client_ctx_t *ctx;
  const svn_string_t *propval;
  apr_hash_t *externals_to_pin;
  apr_array_header_t *external_items;
  apr_array_header_t *copy_sources;
  svn_wc_external_item2_t items[6];
  svn_client_copy_source_t copy_source;
  apr_hash_t *props;
  apr_array_header_t *pinned_externals_descs;
  apr_array_header_t *pinned_externals;
  int i;
  int num_tested_externals;
  svn_stringbuf_t *externals_test_prop;
  struct pin_externals_test_data {
    const char *src_external_desc;
    const char *expected_dst_external_desc;
  } pin_externals_test_data[] = {
    { "^/A/D/gamma B/gamma",    "^/A/D/gamma@2 B/gamma" },
    { "-r1 ^/A/D/G C/exdir_G",  "-r1 ^/A/D/G C/exdir_G" },
    { "^/A/D/H@1 C/exdir_H",    "^/A/D/H@1 C/exdir_H"  },
    { "^/A/D/H C/exdir_H2",     "^/A/D/H@2 C/exdir_H2" },
    { "-r1 ^/A/B D/z/y/z/blah", "-r1 ^/A/B@2 D/z/y/z/blah" } ,
    { "-r1 ^/A/D@2 exdir_D", "-r1 ^/A/D@2 exdir_D" },
    /* Dated revision should retain their date string exactly. */
    { "-r{1970-01-01T00:00} ^/A/C 70s", "-r{1970-01-01T00:00} ^/A/C@2 70s"},
    { "-r{2004-02-23} ^/svn 1.0", "-r{2004-02-23} ^/svn 1.0"},
    { NULL },
  };

  /* Create a filesytem and repository containing the Greek tree. */
  SVN_ERR(create_greek_repos(&repos_url, "pin-externals", opts, pool));

  wc_path = svn_test_data_path("pin-externals-working-copy", pool);

  /* Remove old test data from the previous run */
  SVN_ERR(svn_io_remove_dir2(wc_path, TRUE, NULL, NULL, pool));

  SVN_ERR(svn_io_make_dir_recursively(wc_path, pool));
  svn_test_add_dir_cleanup(wc_path);

  rev.kind = svn_opt_revision_head;
  peg_rev.kind = svn_opt_revision_unspecified;
  SVN_ERR(svn_client_create_context(&ctx, pool));

  /* Configure some externals on ^/A */
  i = 0;
  externals_test_prop = svn_stringbuf_create_empty(pool);
  while (pin_externals_test_data[i].src_external_desc)
    {
      svn_stringbuf_appendcstr(externals_test_prop,
                               pin_externals_test_data[i].src_external_desc);
      svn_stringbuf_appendbyte(externals_test_prop, '\n');
      i++;
    }
  propval = svn_string_create_from_buf(externals_test_prop, pool);
  A_url = apr_pstrcat(pool, repos_url, "/A", SVN_VA_NULL);
  SVN_ERR(svn_client_propset_remote(SVN_PROP_EXTERNALS, propval,
                                    A_url, TRUE, 1, NULL,
                                    NULL, NULL, ctx, pool));

  /* Set up parameters for pinning some externals. */
  externals_to_pin = apr_hash_make(pool);

  items[0].url = "^/A/D/gamma";
  items[0].target_dir = "B/gamma";
  items[1].url = "^/A/B";
  items[1].target_dir = "D/z/y/z/blah";
  items[2].url = "^/A/D/H";
  items[2].target_dir = "C/exdir_H2";
  items[3].url= "^/A/D";
  items[3].target_dir= "exdir_D";
  items[4].url = "^/A/C";
  items[4].target_dir = "70s";
  /* Also add an entry which doesn't match any actual definition. */
  items[5].url = "^/this/does/not/exist";
  items[5].target_dir = "in/test/data";

  external_items = apr_array_make(pool, 2, sizeof(svn_wc_external_item2_t *));
  for (i = 0; i < sizeof(items) / sizeof(items[0]); i++)
    APR_ARRAY_PUSH(external_items, svn_wc_external_item2_t *) = &items[i];
  svn_hash_sets(externals_to_pin, A_url, external_items);

  /* Copy ^/A to ^/A_copy, pinning two non-pinned externals. */
  copy_source.path = A_url;
  copy_source.revision = &rev;
  copy_source.peg_revision = &peg_rev;
  copy_sources = apr_array_make(pool, 1, sizeof(svn_client_copy_source_t *));
  APR_ARRAY_PUSH(copy_sources, svn_client_copy_source_t *) = &copy_source;
  A_copy_url = apr_pstrcat(pool, repos_url, "/A_copy", SVN_VA_NULL);
  SVN_ERR(svn_client_copy7(copy_sources, A_copy_url, FALSE, FALSE,
                           FALSE, FALSE, TRUE, externals_to_pin,
                           NULL, NULL, NULL, ctx, pool));

  /* Verify that externals were pinned as expected. */
  SVN_ERR(svn_client_propget5(&props, NULL, SVN_PROP_EXTERNALS,
                              A_copy_url, &peg_rev, &rev, NULL,
                              svn_depth_empty, NULL, ctx, pool, pool));
  propval = svn_hash_gets(props, A_copy_url);
  SVN_TEST_ASSERT(propval);

  /* Test the unparsed representation of copied externals descriptions. */
  pinned_externals_descs = svn_cstring_split(propval->data, "\n", FALSE, pool);
  for (i = 0; i < pinned_externals_descs->nelts; i++)
    {
      const char *externals_desc;
      const char *expected_desc;

      externals_desc = APR_ARRAY_IDX(pinned_externals_descs, i, const char *);
      expected_desc = pin_externals_test_data[i].expected_dst_external_desc;
      SVN_TEST_STRING_ASSERT(externals_desc, expected_desc);
    }
  /* Ensure all test cases were tested. */
  SVN_TEST_ASSERT(i == (sizeof(pin_externals_test_data) /
                        sizeof(pin_externals_test_data[0]) - 1));

  SVN_ERR(svn_wc_parse_externals_description3(&pinned_externals, A_copy_url,
                                              propval->data, TRUE, pool));

  /* For completeness, test the parsed representation, too */
  num_tested_externals = 0;
  for (i = 0; i < pinned_externals->nelts; i++)
    {
      svn_wc_external_item2_t *item;

      item = APR_ARRAY_IDX(pinned_externals, i, svn_wc_external_item2_t *);
      if (strcmp(item->url, "^/A/D/gamma") == 0)
        {
          SVN_TEST_STRING_ASSERT(item->target_dir, "B/gamma");
          /* Pinned to r2. */
          SVN_TEST_ASSERT(item->revision.kind == svn_opt_revision_number);
          SVN_TEST_ASSERT(item->revision.value.number == 2);
          SVN_TEST_ASSERT(item->peg_revision.kind == svn_opt_revision_number);
          SVN_TEST_ASSERT(item->peg_revision.value.number == 2);
          num_tested_externals++;
        }
      else if (strcmp(item->url, "^/A/D/G") == 0)
        {
          SVN_TEST_STRING_ASSERT(item->target_dir, "C/exdir_G");
          /* Not pinned. */
          SVN_TEST_ASSERT(item->revision.kind == svn_opt_revision_number);
          SVN_TEST_ASSERT(item->revision.value.number == 1);
          SVN_TEST_ASSERT(item->peg_revision.kind == svn_opt_revision_head);
          num_tested_externals++;
        }
      else if (strcmp(item->url, "^/A/D/H") == 0)
        {
          if (strcmp(item->target_dir, "C/exdir_H") == 0)
            {
              /* Was already pinned to r1. */
              SVN_TEST_ASSERT(item->revision.kind == svn_opt_revision_number);
              SVN_TEST_ASSERT(item->revision.value.number == 1);
              SVN_TEST_ASSERT(item->peg_revision.kind ==
                              svn_opt_revision_number);
              SVN_TEST_ASSERT(item->peg_revision.value.number == 1);
              num_tested_externals++;
            }
          else if (strcmp(item->target_dir, "C/exdir_H2") == 0)
            {
              /* Pinned to r2. */
              SVN_TEST_ASSERT(item->revision.kind == svn_opt_revision_number);
              SVN_TEST_ASSERT(item->revision.value.number == 2);
              SVN_TEST_ASSERT(item->peg_revision.kind ==
                              svn_opt_revision_number);
              SVN_TEST_ASSERT(item->peg_revision.value.number == 2);
              num_tested_externals++;
            }
          else
            SVN_TEST_ASSERT(FALSE); /* unknown external */
        }
      else if (strcmp(item->url, "^/A/B") == 0)
        {
          SVN_TEST_STRING_ASSERT(item->target_dir, "D/z/y/z/blah");
          /* Pinned to r2. */
          SVN_TEST_ASSERT(item->revision.kind == svn_opt_revision_number);
          SVN_TEST_ASSERT(item->revision.value.number == 1);
          SVN_TEST_ASSERT(item->peg_revision.kind == svn_opt_revision_number);
          SVN_TEST_ASSERT(item->peg_revision.value.number == 2);
          num_tested_externals++;
        }
      else if (strcmp(item->url, "^/A/D") == 0)
        {
          SVN_TEST_STRING_ASSERT(item->target_dir, "exdir_D");
          /* Pinned to r2. */
          SVN_TEST_ASSERT(item->revision.kind == svn_opt_revision_number);
          SVN_TEST_ASSERT(item->revision.value.number == 1);
          SVN_TEST_ASSERT(item->peg_revision.kind == svn_opt_revision_number);
          SVN_TEST_ASSERT(item->peg_revision.value.number == 2);
          num_tested_externals++;
        }
      else if (strcmp(item->url, "^/A/C") == 0)
        {
          SVN_TEST_STRING_ASSERT(item->target_dir, "70s");
          /* Pinned to r2. */
          SVN_TEST_ASSERT(item->revision.kind == svn_opt_revision_date);
          /* Don't bother testing the exact date value here. */
          SVN_TEST_ASSERT(item->peg_revision.kind == svn_opt_revision_number);
          SVN_TEST_ASSERT(item->peg_revision.value.number == 2);
          num_tested_externals++;
        }
      else if (strcmp(item->url, "^/svn") == 0)
        {
          SVN_TEST_STRING_ASSERT(item->target_dir, "1.0");
          /* Was and not in externals_to_pin, operative revision was a date. */
          SVN_TEST_ASSERT(item->revision.kind == svn_opt_revision_date);
          /* Don't bother testing the exact date value here. */
          SVN_TEST_ASSERT(item->peg_revision.kind == svn_opt_revision_head);
          num_tested_externals++;
        }
      else
        SVN_TEST_ASSERT(FALSE); /* unknown URL */
    }

  /* Ensure all test cases were tested. */
  SVN_TEST_ASSERT(num_tested_externals == (sizeof(pin_externals_test_data) /
                                           sizeof(pin_externals_test_data[0])
                                          - 1));

  return SVN_NO_ERROR;
}

/* issue #4560 */
static svn_error_t *
test_copy_pin_externals_select_subtree(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_opt_revision_t rev;
  svn_opt_revision_t peg_rev;
  const char *repos_url;
  const char *A_copy_url;
  const char *B_url;
  const char *wc_path;
  svn_client_ctx_t *ctx;
  apr_hash_t *externals_to_pin;
  apr_array_header_t *external_items;
  apr_array_header_t *copy_sources;
  svn_wc_external_item2_t item;
  svn_client_copy_source_t copy_source;
  apr_hash_t *props;
  int i;
  struct test_data {
    const char *subtree_relpath;
    const char *src_external_desc;
    const char *expected_dst_external_desc;
  } test_data[] = {
    /* Note: these externals definitions contain extra whitespace on
       purpose, to test that the pinning logic doesn't make
       whitespace-only changes to values that aren't pinned. */

    /* External on A/B will be pinned. */
    { "B", "^/A/D/gamma  gamma-ext", "^/A/D/gamma@3 gamma-ext" },

    /* External on A/D won't be pinned. */
    { "D", "^/A/B/F  F-ext", "^/A/B/F  F-ext" } ,

    { NULL },
  };

  /* Create a filesytem and repository containing the Greek tree. */
  SVN_ERR(create_greek_repos(&repos_url, "pin-externals-select-subtree",
                             opts, pool));

  wc_path = svn_test_data_path("pin-externals-select-subtree-wc", pool);

  /* Remove old test data from the previous run */
  SVN_ERR(svn_io_remove_dir2(wc_path, TRUE, NULL, NULL, pool));

  SVN_ERR(svn_io_make_dir_recursively(wc_path, pool));
  svn_test_add_dir_cleanup(wc_path);

  rev.kind = svn_opt_revision_head;
  peg_rev.kind = svn_opt_revision_unspecified;
  SVN_ERR(svn_client_create_context(&ctx, pool));

  /* Configure externals. */
  i = 0;
  while (test_data[i].subtree_relpath)
    {
      const char *subtree_relpath;
      const char *url;
      const svn_string_t *propval;

      subtree_relpath = test_data[i].subtree_relpath;
      propval = svn_string_create(test_data[i].src_external_desc, pool);

      url = apr_pstrcat(pool, repos_url, "/A/", subtree_relpath, SVN_VA_NULL);
      SVN_ERR(svn_client_propset_remote(SVN_PROP_EXTERNALS, propval,
                                        url, TRUE, 1, NULL,
                                        NULL, NULL, ctx, pool));
      i++;
    }

  /* Set up parameters for pinning externals on A/B. */
  externals_to_pin = apr_hash_make(pool);

  item.url = "^/A/D/gamma";
  item.target_dir = "gamma-ext";

  external_items = apr_array_make(pool, 2, sizeof(svn_wc_external_item2_t *));
  APR_ARRAY_PUSH(external_items, svn_wc_external_item2_t *) = &item;
  B_url = apr_pstrcat(pool, repos_url, "/A/B", SVN_VA_NULL);
  svn_hash_sets(externals_to_pin, B_url, external_items);

  /* Copy ^/A to ^/A_copy, pinning externals on ^/A/B. */
  copy_source.path = apr_pstrcat(pool, repos_url, "/A", SVN_VA_NULL);
  copy_source.revision = &rev;
  copy_source.peg_revision = &peg_rev;
  copy_sources = apr_array_make(pool, 1, sizeof(svn_client_copy_source_t *));
  APR_ARRAY_PUSH(copy_sources, svn_client_copy_source_t *) = &copy_source;
  A_copy_url = apr_pstrcat(pool, repos_url, "/A_copy", SVN_VA_NULL);
  SVN_ERR(svn_client_copy7(copy_sources, A_copy_url, FALSE, FALSE,
                           FALSE, FALSE, TRUE, externals_to_pin,
                           NULL, NULL, NULL, ctx, pool));

  /* Verify that externals were pinned as expected. */
  i = 0;
  while (test_data[i].subtree_relpath)
    {
      const char *subtree_relpath;
      const char *url;
      const svn_string_t *propval;
      svn_stringbuf_t *externals_desc;
      const char *expected_desc;

      subtree_relpath = test_data[i].subtree_relpath;
      url = apr_pstrcat(pool, A_copy_url, "/", subtree_relpath, SVN_VA_NULL);

      SVN_ERR(svn_client_propget5(&props, NULL, SVN_PROP_EXTERNALS,
                                  url, &peg_rev, &rev, NULL,
                                  svn_depth_empty, NULL, ctx, pool, pool));
      propval = svn_hash_gets(props, url);
      SVN_TEST_ASSERT(propval);
      externals_desc = svn_stringbuf_create(propval->data, pool);
      svn_stringbuf_strip_whitespace(externals_desc);
      expected_desc = test_data[i].expected_dst_external_desc;
      SVN_TEST_STRING_ASSERT(externals_desc->data, expected_desc);

      i++;
    }

  return SVN_NO_ERROR;
}

/* ========================================================================== */


static int max_threads = 3;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_elide_mergeinfo_catalog,
                   "test svn_client__elide_mergeinfo_catalog"),
    SVN_TEST_PASS2(test_args_to_target_array,
                   "test svn_client_args_to_target_array"),
    SVN_TEST_OPTS_PASS(test_wc_add_scenarios, "test svn_wc_add3 scenarios"),
    SVN_TEST_OPTS_PASS(test_foreign_repos_copy, "test foreign repository copy"),
    SVN_TEST_OPTS_PASS(test_patch, "test svn_client_patch"),
    SVN_TEST_OPTS_PASS(test_copy_crash, "test a crash in svn_client_copy5"),
#ifdef TEST16K_ADD
    SVN_TEST_OPTS_PASS(test_16k_add, "test adding 16k files"),
#endif
    SVN_TEST_OPTS_PASS(test_youngest_common_ancestor, "test youngest_common_ancestor"),
    SVN_TEST_OPTS_PASS(test_suggest_mergesources,
                       "test svn_client_suggest_merge_sources"),
    SVN_TEST_OPTS_PASS(test_remote_only_status,
                       "test svn_client_status6 with ignore_local_mods"),
    SVN_TEST_OPTS_PASS(test_copy_pin_externals,
                       "test svn_client_copy7 with externals_to_pin"),
    SVN_TEST_OPTS_PASS(test_copy_pin_externals_select_subtree,
                       "pin externals on selected subtrees only"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
