/*
 * wc-test.c :  test WC APIs
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
#include <apr_md5.h>

#define SVN_DEPRECATED

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
#include "private/svn_dep_compat.h"
#include "../../libsvn_wc/wc.h"
#include "../../libsvn_wc/wc_db.h"
#define SVN_WC__I_AM_WC_DB
#include "../../libsvn_wc/wc_db_private.h"

#include "../svn_test.h"

#ifdef _MSC_VER
#pragma warning(disable: 4221) /* nonstandard extension used */
#endif


/* ---------------------------------------------------------------------- */
/* The test functions */

/* Structure for testing node_get_base and node_get_origin. */
struct base_origin_t
{
  /* Path to create and test, WC-relative */
  const char *path;
  /* Expected base rev.  "-1" means no base.  (Expected base path
   *   == base_rev valid ? path : NULL) */
  svn_revnum_t base_rev;
  /* Path to copy from, WC-relative */
  const char *src_path;
  /* Expected "origin" */
  struct {
      const char *path;
      svn_revnum_t rev;
  } origin;
};

/* Data for testing node_get_base and node_get_origin. */
static struct base_origin_t base_origin_subtests[] =
  {
    /* file copied onto nothing */
    { "A/C/copy1",  -1, "iota",   {"iota", 1} },

    /* dir copied onto nothing */
    { "A/C/copy2",  -1, "A/B/E",  {"A/B/E", 1} },

    /* replacement: file copied over a schedule-delete file */
    { "A/B/lambda", 1,  "iota",   {"iota", 1} },

    /* replacement: dir copied over a schedule-delete dir */
    { "A/D/G",      1,  "A/B/E",  {"A/B/E", 1} },

    /* replacement: dir copied over a schedule-delete file */
    { "A/D/gamma",  1,  "A/B/E",  {"A/B/E", 1} },

    /* replacement: file copied over a schedule-delete dir */
    { "A/D/H",      1,  "iota",   {"iota", 1} },

    { 0 }
  };

/* Create a WC containing lots of different node states, in the sandbox B. */
static svn_error_t *
create_wc_for_base_and_origin_tests(svn_test__sandbox_t *b)
{
  struct base_origin_t *copy;

  SVN_ERR(sbox_add_and_commit_greek_tree(b));

  /* Copy various things */
  for (copy = base_origin_subtests; copy->src_path; copy++)
    {
      if (SVN_IS_VALID_REVNUM(copy->base_rev))
        SVN_ERR(sbox_wc_delete(b, copy->path));
      SVN_ERR(sbox_wc_copy(b, copy->src_path, copy->path));
    }

  return SVN_NO_ERROR;
}

/* Test svn_wc__node_get_base(). */
static svn_error_t *
test_node_get_base(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(svn_test__sandbox_create(b, "node_get_base", opts, pool));

  SVN_ERR(create_wc_for_base_and_origin_tests(b));

  {
    struct base_origin_t *subtest;

    for (subtest = base_origin_subtests; subtest->path; subtest++)
      {
        const char *local_abspath
          = svn_dirent_join(b->wc_abspath, subtest->path, b->pool);
        svn_revnum_t revision;
        const char *repos_relpath, *repos_root_url, *repos_uuid;

        SVN_ERR(svn_wc__node_get_base(NULL, &revision, &repos_relpath,
                                      &repos_root_url, &repos_uuid,
                                      NULL,
                                      b->wc_ctx, local_abspath,
                                      TRUE /* ignore_enoent */,
                                      b->pool, b->pool));
        SVN_TEST_ASSERT(revision == subtest->base_rev);
        if (SVN_IS_VALID_REVNUM(subtest->base_rev))
          {
            SVN_TEST_STRING_ASSERT(repos_relpath, subtest->path);
            SVN_TEST_STRING_ASSERT(repos_root_url, b->repos_url);
            SVN_TEST_ASSERT(repos_uuid != NULL);
          }
        else
          {
            SVN_TEST_STRING_ASSERT(repos_relpath, NULL);
            SVN_TEST_STRING_ASSERT(repos_root_url, NULL);
            SVN_TEST_STRING_ASSERT(repos_uuid, NULL);
          }
      }
  }

  return SVN_NO_ERROR;
}

/* Test svn_wc__node_get_origin(). */
static svn_error_t *
test_node_get_origin(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t *b = apr_palloc(pool, sizeof(*b));

  SVN_ERR(svn_test__sandbox_create(b, "node_get_origin", opts, pool));

  SVN_ERR(create_wc_for_base_and_origin_tests(b));

  {
    struct base_origin_t *subtest;

    for (subtest = base_origin_subtests; subtest->path; subtest++)
      {
        const char *local_abspath
          = svn_dirent_join(b->wc_abspath, subtest->path, b->pool);
        svn_revnum_t revision;
        const char *repos_relpath, *repos_root_url, *repos_uuid;

        SVN_ERR(svn_wc__node_get_origin(NULL, &revision, &repos_relpath,
                                        &repos_root_url, &repos_uuid, NULL,
                                        NULL,
                                        b->wc_ctx, local_abspath, FALSE,
                                        b->pool, b->pool));
        SVN_TEST_ASSERT(revision == subtest->origin.rev);
        if (SVN_IS_VALID_REVNUM(subtest->origin.rev))
          {
            SVN_TEST_STRING_ASSERT(repos_relpath, subtest->origin.path);
            SVN_TEST_STRING_ASSERT(repos_root_url, b->repos_url);
            SVN_TEST_ASSERT(repos_uuid != NULL);
          }
        else
          {
            SVN_TEST_STRING_ASSERT(repos_relpath, NULL);
            SVN_TEST_STRING_ASSERT(repos_root_url, NULL);
            SVN_TEST_STRING_ASSERT(repos_uuid, NULL);
          }
      }
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_externals_parse(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  int i;
  struct external_info
    {
      const char *line;
      const char *url;
      const char *local_path;
      svn_revnum_t peg_rev;
      svn_revnum_t rev;

    } items[] = {
        {
            "dir http://server/svn/a",
            "http://server/svn/a",
            "dir"
        },
        {
            "/svn/home dir",
            "u://svr/svn/home",
            "dir"
        },
        {
            "//server/home dir",
            "u://server/home",
            "dir"
        },
        {
            "../../../../home dir",
            "u://svr/svn/home",
            "dir",
        },
        {
            "^/../repB/tools/scripts scripts",
            "u://svr/svn/cur/repB/tools/scripts",
            "scripts"
        },
        {
            "^/../repB/tools/README.txt scripts/README.txt",
            "u://svr/svn/cur/repB/tools/README.txt",
            "scripts/README.txt"
        },
    };

  for (i = 0; i < sizeof(items) / sizeof(items[0]); i++)
    {
      apr_array_header_t *results;
      svn_wc_external_item2_t *external_item;
      const char *resolved_url;
      SVN_ERR(svn_wc_parse_externals_description3(&results, "/my/current/dir",
                                                  items[i].line, FALSE, pool));

      SVN_TEST_ASSERT(results && results->nelts == 1);

      external_item = APR_ARRAY_IDX(results, 0, svn_wc_external_item2_t *);

      SVN_ERR(svn_wc__resolve_relative_external_url(&resolved_url,
                                                    external_item,
                                                    "u://svr/svn/cur/dir",
                                                    "u://svr/svn/cur/dir/sd/fl",
                                                    pool, pool));

      SVN_TEST_STRING_ASSERT(resolved_url, items[i].url);
      SVN_TEST_STRING_ASSERT(external_item->target_dir, items[i].local_path);

      if (items[i].peg_rev != 0)
        SVN_TEST_ASSERT(external_item->peg_revision.value.number
                                == items[i].peg_rev);
      if (items[i].rev != 0)
        SVN_TEST_ASSERT(external_item->revision.value.number == items[i].rev);
      SVN_TEST_ASSERT(svn_uri_is_canonical(resolved_url, pool));
    }


  return SVN_NO_ERROR;

}

static svn_error_t *
test_externals_parse_erratic(apr_pool_t *pool)
{
  svn_error_t *err;
  apr_array_header_t *list = NULL;

  err = svn_wc_parse_externals_description3(
          &list, "parent_dir",
          "^/valid/but/should/not/be/on/record wc_target\n"
           "because_this_is_an_error",
          FALSE, pool);

  /* DESC above has an error, so expect one. */
  SVN_TEST_ASSERT(err != NULL);
  svn_error_clear(err);

  /* svn_wc_parse_externals_description3() should not
     touch LIST when DESC had an error.*/
  SVN_TEST_ASSERT(list == NULL);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_legacy_commit1(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  svn_wc_adm_access_t *adm_access;
  const char *lambda;

  SVN_ERR(svn_test__sandbox_create(&b, "legacy_commit1", opts, pool));
  SVN_ERR(sbox_add_and_commit_greek_tree(&b));

  SVN_ERR(sbox_wc_copy(&b, "A", "A_copied"));

  lambda = sbox_wc_path(&b, "A_copied/B/lambda");


  SVN_ERR(svn_io_remove_file2(lambda, FALSE, pool));
  SVN_ERR(svn_io_copy_file(sbox_wc_path(&b, "iota"), lambda, FALSE, pool));
  SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, b.wc_abspath, TRUE, -1,
                           NULL, NULL, pool));

  {
    svn_wc_status2_t *status;

    SVN_ERR(svn_wc_status2(&status, lambda, adm_access, pool));

    SVN_TEST_ASSERT(status != NULL);
    SVN_TEST_ASSERT(status->text_status == svn_wc_status_modified);
    SVN_TEST_ASSERT(status->copied == TRUE);
  }

  /* Simulate a very old style svn ci . -m "QQQ" on the WC root */
  SVN_ERR(svn_wc_process_committed4(sbox_wc_path(&b, "A_copied"), adm_access,
                                    TRUE, 12, "2014-10-01T19:00:50.966679Z",
                                    "me", NULL, TRUE, TRUE,
                                    NULL, pool));

  {
    unsigned char digest[APR_MD5_DIGESTSIZE];

    /* Use the fact that iota has the same checksum to ease committing */

    SVN_ERR(svn_io_file_checksum (digest, lambda, pool));

    SVN_ERR(svn_wc_process_committed4(lambda, adm_access,
                                      TRUE, 12, "2014-10-01T19:00:50.966679Z",
                                      "me", NULL, TRUE, TRUE,
                                      digest, pool));
  }

  {
    svn_wc_status2_t *status;

    SVN_ERR(svn_wc_status2(&status, lambda, adm_access, pool));

    /* Node is still modified, as we didn't change the text base! */
    SVN_TEST_ASSERT(status != NULL);
    SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
    SVN_TEST_ASSERT(status->copied == FALSE);
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_legacy_commit2(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  svn_wc_adm_access_t *adm_access;
  const char *lambda;
  svn_wc_committed_queue_t *queue;

  SVN_ERR(svn_test__sandbox_create(&b, "legacy_commit2", opts, pool));
  SVN_ERR(sbox_add_and_commit_greek_tree(&b));

  SVN_ERR(sbox_wc_copy(&b, "A", "A_copied"));

  lambda = sbox_wc_path(&b, "A_copied/B/lambda");

  SVN_ERR(svn_io_remove_file2(lambda, FALSE, pool));
  SVN_ERR(svn_io_copy_file(sbox_wc_path(&b, "iota"), lambda, FALSE, pool));

  SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, b.wc_abspath, TRUE, -1,
                           NULL, NULL, pool));

  {
    svn_wc_status2_t *status;

    SVN_ERR(svn_wc_status2(&status, lambda, adm_access, pool));

    SVN_TEST_ASSERT(status != NULL);
    SVN_TEST_ASSERT(status->text_status == svn_wc_status_modified);
    SVN_TEST_ASSERT(status->copied == TRUE);
  }

  /* Simulate an old style svn ci . -m "QQQ" on the WC root */
  queue = svn_wc_committed_queue_create(pool);
  SVN_ERR(svn_wc_queue_committed(&queue, sbox_wc_path(&b, "A_copied"), adm_access,
                                 TRUE, NULL, FALSE, FALSE, NULL, pool));
  {
    unsigned char digest[APR_MD5_DIGESTSIZE];

    /* Use the fact that iota has the same checksum to ease committing */

    SVN_ERR(svn_io_file_checksum(digest, lambda, pool));

    SVN_ERR(svn_wc_queue_committed(&queue, lambda, adm_access, FALSE, NULL,
                                   FALSE, FALSE, digest, pool));
  }

  SVN_ERR(svn_wc_process_committed_queue(queue, adm_access,
                                         12, "2014-10-01T19:00:50.966679Z",
                                        "me", pool));

  {
    svn_wc_status2_t *status;

    SVN_ERR(svn_wc_status2(&status, lambda, adm_access, pool));

    /* Node is still modified, as we didn't change the text base! */
    SVN_TEST_ASSERT(status != NULL);
    SVN_TEST_ASSERT(status->text_status == svn_wc_status_normal);
    SVN_TEST_ASSERT(status->copied == FALSE);
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_internal_file_modified(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  svn_test__sandbox_t b;
  svn_boolean_t modified;
  const char *iota_path;
  apr_time_t time;

  SVN_ERR(svn_test__sandbox_create(&b, "internal_file_modified_p",
                                   opts, pool));
  SVN_ERR(sbox_add_and_commit_greek_tree(&b));

  iota_path = sbox_wc_path(&b, "iota");

  /* No modification, timestamps match.*/
  SVN_ERR(svn_wc__internal_file_modified_p(&modified, b.wc_ctx->db,
                                           iota_path, FALSE, pool));
  SVN_TEST_ASSERT(!modified);

  SVN_ERR(svn_wc__internal_file_modified_p(&modified, b.wc_ctx->db,
                                           iota_path, TRUE, pool));
  SVN_TEST_ASSERT(!modified);

  /* Change timestamp on 'iota' and check. */
  SVN_ERR(svn_io_file_affected_time(&time, iota_path, pool));
  SVN_ERR(svn_io_set_file_affected_time(time + apr_time_from_sec(1),
                                        iota_path, pool));
  SVN_ERR(svn_wc__internal_file_modified_p(&modified, b.wc_ctx->db,
                                           iota_path, FALSE, pool));
  SVN_TEST_ASSERT(!modified);

  SVN_ERR(svn_wc__internal_file_modified_p(&modified, b.wc_ctx->db,
                                           iota_path, TRUE, pool));
  SVN_TEST_ASSERT(!modified);

  /* Modify 'iota' to be different size. */
  SVN_ERR(sbox_file_write(&b, iota_path, "new iota"));
  SVN_ERR(svn_wc__internal_file_modified_p(&modified, b.wc_ctx->db,
                                           iota_path, FALSE, pool));
  SVN_TEST_ASSERT(modified);

  SVN_ERR(svn_wc__internal_file_modified_p(&modified, b.wc_ctx->db,
                                           iota_path, TRUE, pool));
  SVN_TEST_ASSERT(modified);

  /* Working copy is smart and able to detect changes in files of different
   * size even if timestamp didn't change. */
  SVN_ERR(svn_io_set_file_affected_time(time, iota_path, pool));
  SVN_ERR(svn_wc__internal_file_modified_p(&modified, b.wc_ctx->db,
                                           iota_path, FALSE, pool));
  SVN_TEST_ASSERT(modified);

  SVN_ERR(svn_wc__internal_file_modified_p(&modified, b.wc_ctx->db,
                                           iota_path, TRUE, pool));
  SVN_TEST_ASSERT(modified);

  return SVN_NO_ERROR;
}

/* ---------------------------------------------------------------------- */
/* The list of test functions */

static int max_threads = 2;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(test_node_get_base,
                       "test_node_get_base"),
    SVN_TEST_OPTS_PASS(test_node_get_origin,
                       "test_node_get_origin"),
    SVN_TEST_OPTS_PASS(test_externals_parse,
                       "test svn_wc_parse_externals_description3"),
    SVN_TEST_PASS2(test_externals_parse_erratic,
                   "parse erratic externals definition"),
    SVN_TEST_OPTS_PASS(test_legacy_commit1,
                       "test legacy commit1"),
    SVN_TEST_OPTS_PASS(test_legacy_commit2,
                       "test legacy commit2"),
    SVN_TEST_OPTS_PASS(test_internal_file_modified,
                       "test internal_file_modified"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
