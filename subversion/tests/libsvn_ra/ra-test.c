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
#include "svn_ra_svn.h"
#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_dirent_uri.h"

#include "../svn_test.h"
#include "../svn_test_fs.h"
#include "../../libsvn_ra_local/ra_local.h"

static const char tunnel_repos_name[] = "test-repo-tunnel";

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

  SVN_ERR(svn_ra_open4(session, NULL, url, NULL, cbtable, NULL, NULL, pool));

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

static int tunnel_open_count;

static svn_error_t *
open_tunnel(svn_ra_svn_conn_t **conn, void **tunnel_baton,
            void *callbacks_baton,
            const char *tunnel_name, const char *user,
            const char *hostname, int port,
            apr_pool_t *pool)
{
  svn_node_kind_t kind;
  apr_proc_t *proc;
  apr_procattr_t *attr;
  apr_status_t status;
  const char *args[] = { "svnserve", "-t", "-r", ".", NULL };
  const char *svnserve;

  SVN_ERR(svn_dirent_get_absolute(&svnserve, "../../svnserve/svnserve", pool));
#ifdef WIN32
  svnserve = apr_pstrcat(pool, svnserve, ".exe", NULL);
#endif
  SVN_ERR(svn_io_check_path(svnserve, &kind, pool));
  if (kind != svn_node_file)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Could not find svnserve at %s",
                             svn_dirent_local_style(svnserve, pool));

  status = apr_procattr_create(&attr, pool);
  if (status == APR_SUCCESS)
    status = apr_procattr_io_set(attr, 1, 1, 0);
  if (status == APR_SUCCESS)
    status = apr_procattr_cmdtype_set(attr, APR_PROGRAM);
  proc = apr_palloc(pool, sizeof(*proc));
  if (status == APR_SUCCESS)
    status = apr_proc_create(proc,
                             svn_dirent_local_style(svnserve, pool),
                             args, NULL, attr, pool);
  if (status != APR_SUCCESS)
    return svn_error_wrap_apr(status, "Could not run svnserve");
#ifdef WIN32
  apr_pool_note_subprocess(pool, proc, APR_KILL_NEVER);
#else
  apr_pool_note_subprocess(pool, proc, APR_KILL_ONLY_ONCE);
#endif

  /* APR pipe objects inherit by default.  But we don't want the
   * tunnel agent's pipes held open by future child processes
   * (such as other ra_svn sessions), so turn that off. */
  apr_file_inherit_unset(proc->in);
  apr_file_inherit_unset(proc->out);

  *conn = svn_ra_svn_create_conn3(NULL, proc->out, proc->in,
                                  SVN_DELTA_COMPRESSION_LEVEL_DEFAULT,
                                  0, 0, pool);

  *tunnel_baton = NULL;
  ++tunnel_open_count;
  return SVN_NO_ERROR;
}

static svn_error_t *
close_tunnel(void *tunnel_baton, void *callbacks_baton,
             const char *tunnel_name, const char *user,
             const char *hostname, int port)
{
  --tunnel_open_count;
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


/* Test ra_svn tunnel callbacks. */
static svn_error_t *
tunel_callback_test(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  apr_pool_t *connection_pool;
  svn_repos_t *repos;
  const char *url;
  svn_ra_callbacks2_t *cbtable;
  svn_ra_session_t *session;
  svn_error_t *err;

  SVN_ERR(svn_test__create_repos(&repos, tunnel_repos_name, opts, pool));

  url = apr_pstrcat(pool, "svn+test://localhost/", tunnel_repos_name, NULL);
  SVN_ERR(svn_ra_create_callbacks(&cbtable, pool));
  cbtable->open_tunnel = open_tunnel;
  cbtable->close_tunnel = close_tunnel;
  SVN_ERR(svn_cmdline_create_auth_baton(&cbtable->auth_baton,
                                        TRUE  /* non_interactive */,
                                        "jrandom", "rayjandom",
                                        NULL,
                                        TRUE  /* no_auth_cache */,
                                        FALSE /* trust_server_cert */,
                                        NULL, NULL, NULL, pool));

  tunnel_open_count = 0;
  connection_pool = svn_pool_create(pool);
  err = svn_ra_open4(&session, NULL, url, NULL, cbtable, NULL, NULL,
                     connection_pool);
  if (err && err->apr_err == SVN_ERR_TEST_FAILED)
    {
      svn_handle_error2(err, stderr, FALSE, "svn_tests: ");
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  SVN_ERR(err);
  SVN_TEST_ASSERT(tunnel_open_count > 0);
  svn_pool_destroy(connection_pool);
  SVN_TEST_ASSERT(tunnel_open_count == 0);
  return SVN_NO_ERROR;
}



/* The test table.  */
struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(location_segments_test,
                       "test svn_ra_get_location_segments"),
    SVN_TEST_OPTS_PASS(tunel_callback_test,
                       "test ra_svn tunnel creation callbacks"),
    SVN_TEST_NULL
  };
