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
struct base_origin_t base_origin_subtests[] =
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

        SVN_ERR(svn_wc__node_get_base(&revision, &repos_relpath,
                                      &repos_root_url, &repos_uuid,
                                      b->wc_ctx, local_abspath,
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


/* ---------------------------------------------------------------------- */
/* The list of test functions */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(test_node_get_base,
                       "test_node_get_base"),
    SVN_TEST_OPTS_PASS(test_node_get_origin,
                       "test_node_get_origin"),
    SVN_TEST_NULL
  };
