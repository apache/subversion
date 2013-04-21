/*
 * ra-local-test.c :  basic tests for the RA LOCAL library
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



#include <apr_general.h>
#include <apr_pools.h>

#define SVN_DEPRECATED

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"

#include "../svn_test.h"
#include "../svn_test_fs.h"
#include "../../libsvn_ra_local/ra_local.h"

/*-------------------------------------------------------------------*/

/** Helper routines. **/


static svn_error_t *
make_and_open_local_repos(svn_ra_session_t **session,
                          const char *repos_name,
                          const svn_test_opts_t *opts,
                          apr_pool_t *pool)
{
  svn_repos_t *repos;
  const char *url;
  svn_ra_callbacks2_t *cbtable;

  SVN_ERR(svn_ra_create_callbacks(&cbtable, pool));

  SVN_ERR(svn_test__create_repos(&repos, repos_name, opts, pool));
  SVN_ERR(svn_ra_initialize(pool));

  SVN_ERR(svn_uri_get_file_url_from_dirent(&url, repos_name, pool));

  SVN_ERR(svn_ra_open3(session, url, NULL, cbtable, NULL, NULL, pool));

  return SVN_NO_ERROR;
}

/* Commit some simple changes */
static svn_error_t *
commit_changes(svn_ra_session_t *session,
               apr_pool_t *pool)
{
  apr_hash_t *revprop_table = apr_hash_make(pool);
  const svn_delta_editor_t *editor;
  void *edit_baton;
  const char *repos_root_url;
  void *root_baton, *dir_baton;

  SVN_ERR(svn_ra_get_commit_editor3(session, &editor, &edit_baton,
                                    revprop_table,
                                    NULL, NULL, NULL, TRUE, pool));
  SVN_ERR(svn_ra_get_repos_root(session, &repos_root_url, pool));

  SVN_ERR(editor->open_root(edit_baton, SVN_INVALID_REVNUM,
                            pool, &root_baton));
  /* copy root-dir@0 to A@1 */
  SVN_ERR(editor->add_directory("A", root_baton, repos_root_url, 0,
                               pool, &dir_baton));
  SVN_ERR(editor->close_edit(edit_baton, pool));
  return SVN_NO_ERROR;
}

/*-------------------------------------------------------------------*/

/** The tests **/

/* Baton for gls_receiver(). */
struct gls_receiver_baton_t
{
  apr_array_header_t *segments;
  apr_pool_t *pool;
};

/* Receive a location segment and append it to BATON.segments. */
static svn_error_t *
gls_receiver(svn_location_segment_t *segment,
             void *baton,
             apr_pool_t *pool)
{
  struct gls_receiver_baton_t *b = baton;

  APR_ARRAY_PUSH(b->segments, svn_location_segment_t *)
    = svn_location_segment_dup(segment, b->pool);
  return SVN_NO_ERROR;
}

/* Test svn_ra_get_location_segments(). */
static svn_error_t *
location_segments_test(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  svn_ra_session_t *session;
  apr_array_header_t *segments
    = apr_array_make(pool, 1, sizeof(svn_location_segment_t *));
  struct gls_receiver_baton_t b;
  const char *path = "A";
  svn_revnum_t peg_revision = 1;
  svn_location_segment_t *seg;

  b.segments = segments;
  b.pool = pool;

  SVN_ERR(make_and_open_local_repos(&session,
                                    "test-repo-locsegs", opts,
                                    pool));

  /* ### This currently tests only a small subset of what's possible. */
  SVN_ERR(commit_changes(session, pool));
  SVN_ERR(svn_ra_get_location_segments(session, path, peg_revision,
                                       SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
                                       gls_receiver, &b, pool));
  SVN_TEST_ASSERT(segments->nelts == 2);
  seg = APR_ARRAY_IDX(segments, 0, svn_location_segment_t *);
  SVN_TEST_STRING_ASSERT(seg->path, "A");
  SVN_TEST_ASSERT(seg->range_start == 1);
  SVN_TEST_ASSERT(seg->range_end == 1);
  seg = APR_ARRAY_IDX(segments, 1, svn_location_segment_t *);
  SVN_TEST_STRING_ASSERT(seg->path, "");
  SVN_TEST_ASSERT(seg->range_start == 0);
  SVN_TEST_ASSERT(seg->range_end == 0);

  return SVN_NO_ERROR;
}



/* The test table.  */
struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(location_segments_test,
                       "test svn_ra_get_location_segments"),
    SVN_TEST_NULL
  };
