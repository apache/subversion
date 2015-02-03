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
#include <apr_file_io.h>
#include <assert.h>
#define SVN_DEPRECATED

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"
#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"

#include "../svn_test.h"
#include "../svn_test_fs.h"
#include "../../libsvn_ra_local/ra_local.h"

static const char tunnel_repos_name[] = "test-repo-tunnel";

/*-------------------------------------------------------------------*/

/** Helper routines. **/


static svn_error_t *
make_and_open_repos(svn_ra_session_t **session,
                    const char *repos_name,
                    const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  const char *url;
  svn_ra_callbacks2_t *cbtable;

  SVN_ERR(svn_ra_create_callbacks(&cbtable, pool));
  SVN_ERR(svn_test__init_auth_baton(&cbtable->auth_baton, pool));

  SVN_ERR(svn_test__create_repos2(NULL, &url, NULL, repos_name, opts,
                                  pool, pool));
  SVN_ERR(svn_ra_initialize(pool));

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

static svn_error_t *
commit_tree(svn_ra_session_t *session,
            apr_pool_t *pool)
{
  apr_hash_t *revprop_table = apr_hash_make(pool);
  const svn_delta_editor_t *editor;
  void *edit_baton;
  const char *repos_root_url;
  void *root_baton, *A_baton, *B_baton, *file_baton;

  SVN_ERR(svn_ra_get_commit_editor3(session, &editor, &edit_baton,
                                    revprop_table,
                                    NULL, NULL, NULL, TRUE, pool));
  SVN_ERR(svn_ra_get_repos_root(session, &repos_root_url, pool));

  SVN_ERR(editor->open_root(edit_baton, SVN_INVALID_REVNUM,
                            pool, &root_baton));
  SVN_ERR(editor->add_directory("A", root_baton, NULL, SVN_INVALID_REVNUM,
                                pool, &A_baton));
  SVN_ERR(editor->add_directory("A/B", A_baton, NULL, SVN_INVALID_REVNUM,
                                pool, &B_baton));
  SVN_ERR(editor->add_file("A/B/f", B_baton, NULL, SVN_INVALID_REVNUM,
                           pool, &file_baton));
  SVN_ERR(editor->close_file(file_baton, NULL, pool));
  SVN_ERR(editor->add_file("A/B/g", B_baton, NULL, SVN_INVALID_REVNUM,
                           pool, &file_baton));
  SVN_ERR(editor->close_file(file_baton, NULL, pool));
  SVN_ERR(editor->close_directory(B_baton, pool));
  SVN_ERR(editor->add_directory("A/BB", A_baton, NULL, SVN_INVALID_REVNUM,
                                pool, &B_baton));
  SVN_ERR(editor->add_file("A/BB/f", B_baton, NULL, SVN_INVALID_REVNUM,
                           pool, &file_baton));
  SVN_ERR(editor->close_file(file_baton, NULL, pool));
  SVN_ERR(editor->add_file("A/BB/g", B_baton, NULL, SVN_INVALID_REVNUM,
                           pool, &file_baton));
  SVN_ERR(editor->close_file(file_baton, NULL, pool));
  SVN_ERR(editor->close_directory(B_baton, pool));
  SVN_ERR(editor->close_directory(A_baton, pool));
  SVN_ERR(editor->close_edit(edit_baton, pool));
  return SVN_NO_ERROR;
}

static svn_boolean_t last_tunnel_check;
static int tunnel_open_count;
static void *check_tunnel_baton;
static void *open_tunnel_context;

static svn_boolean_t
check_tunnel(void *tunnel_baton, const char *tunnel_name)
{
  if (tunnel_baton != check_tunnel_baton)
    abort();
  last_tunnel_check = (0 == strcmp(tunnel_name, "test"));
  return last_tunnel_check;
}

static void
close_tunnel(void *tunnel_context, void *tunnel_baton);

static svn_error_t *
open_tunnel(svn_stream_t **request, svn_stream_t **response,
            svn_ra_close_tunnel_func_t *close_func, void **close_baton,
            void *tunnel_baton,
            const char *tunnel_name, const char *user,
            const char *hostname, int port,
            svn_cancel_func_t cancel_func, void *cancel_baton,
            apr_pool_t *pool)
{
  svn_node_kind_t kind;
  apr_proc_t *proc;
  apr_procattr_t *attr;
  apr_status_t status;
  const char *args[] = { "svnserve", "-t", "-r", ".", NULL };
  const char *svnserve;

  SVN_TEST_ASSERT(tunnel_baton == check_tunnel_baton);

  SVN_ERR(svn_dirent_get_absolute(&svnserve, "../../svnserve/svnserve", pool));
#ifdef WIN32
  svnserve = apr_pstrcat(pool, svnserve, ".exe", SVN_VA_NULL);
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

  *request = svn_stream_from_aprfile2(proc->in, FALSE, pool);
  *response = svn_stream_from_aprfile2(proc->out, FALSE, pool);
  *close_func = close_tunnel;
  open_tunnel_context = *close_baton = &last_tunnel_check;
  ++tunnel_open_count;
  return SVN_NO_ERROR;
}

static void
close_tunnel(void *tunnel_context, void *tunnel_baton)
{
  assert(tunnel_context == open_tunnel_context);
  assert(tunnel_baton == check_tunnel_baton);
  --tunnel_open_count;
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

  SVN_ERR(make_and_open_repos(&session,
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
check_tunnel_callback_test(const svn_test_opts_t *opts,
                           apr_pool_t *pool)
{
  svn_ra_callbacks2_t *cbtable;
  svn_ra_session_t *session;
  svn_error_t *err;

  SVN_ERR(svn_ra_create_callbacks(&cbtable, pool));
  cbtable->check_tunnel_func = check_tunnel;
  cbtable->open_tunnel_func = open_tunnel;
  cbtable->tunnel_baton = check_tunnel_baton = &cbtable;
  SVN_ERR(svn_cmdline_create_auth_baton(&cbtable->auth_baton,
                                        TRUE  /* non_interactive */,
                                        "jrandom", "rayjandom",
                                        NULL,
                                        TRUE  /* no_auth_cache */,
                                        FALSE /* trust_server_cert */,
                                        NULL, NULL, NULL, pool));

  last_tunnel_check = TRUE;
  open_tunnel_context = NULL;
  err = svn_ra_open4(&session, NULL, "svn+foo://localhost/no-repo",
                     NULL, cbtable, NULL, NULL, pool);
  svn_error_clear(err);
  SVN_TEST_ASSERT(err);
  SVN_TEST_ASSERT(!last_tunnel_check);
  return SVN_NO_ERROR;
}

static svn_error_t *
tunnel_callback_test(const svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  apr_pool_t *connection_pool;
  svn_repos_t *repos;
  const char *url;
  svn_ra_callbacks2_t *cbtable;
  svn_ra_session_t *session;
  svn_error_t *err;

  SVN_ERR(svn_test__create_repos(&repos, tunnel_repos_name, opts, pool));

  url = apr_pstrcat(pool, "svn+test://localhost/", tunnel_repos_name,
                    SVN_VA_NULL);
  SVN_ERR(svn_ra_create_callbacks(&cbtable, pool));
  cbtable->check_tunnel_func = check_tunnel;
  cbtable->open_tunnel_func = open_tunnel;
  cbtable->tunnel_baton = check_tunnel_baton = &cbtable;
  SVN_ERR(svn_cmdline_create_auth_baton(&cbtable->auth_baton,
                                        TRUE  /* non_interactive */,
                                        "jrandom", "rayjandom",
                                        NULL,
                                        TRUE  /* no_auth_cache */,
                                        FALSE /* trust_server_cert */,
                                        NULL, NULL, NULL, pool));

  last_tunnel_check = FALSE;
  open_tunnel_context = NULL;
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
  SVN_TEST_ASSERT(last_tunnel_check);
  SVN_TEST_ASSERT(tunnel_open_count > 0);
  svn_pool_destroy(connection_pool);
  SVN_TEST_ASSERT(tunnel_open_count == 0);
  return SVN_NO_ERROR;
}

struct lock_result_t {
  svn_lock_t *lock;
  svn_error_t *err;
};

struct lock_baton_t {
  apr_hash_t *results;
  apr_pool_t *pool;
};

/* Implements svn_ra_lock_callback_t. */
static svn_error_t *
lock_cb(void *baton,
        const char *path,
        svn_boolean_t do_lock,
        const svn_lock_t *lock,
        svn_error_t *ra_err,
        apr_pool_t *pool)
{
  struct lock_baton_t *b = baton;
  struct lock_result_t *result = apr_palloc(b->pool,
                                            sizeof(struct lock_result_t));

  if (lock)
    {
      result->lock = apr_palloc(b->pool, sizeof(svn_lock_t));
      *result->lock = *lock;
      result->lock->path = apr_pstrdup(b->pool, lock->path);
      result->lock->token = apr_pstrdup(b->pool, lock->token);
      result->lock->owner = apr_pstrdup(b->pool, lock->owner);
      result->lock->comment = apr_pstrdup(b->pool, lock->comment);
    }
  else
    result->lock = NULL;
  result->err = ra_err;

  svn_hash_sets(b->results, apr_pstrdup(b->pool, path), result);
  
  return SVN_NO_ERROR;
}

static svn_error_t *
expect_lock(const char *path,
            apr_hash_t *results,
            svn_ra_session_t *session,
            apr_pool_t *scratch_pool)
{
  svn_lock_t *lock;
  struct lock_result_t *result = svn_hash_gets(results, path);

  SVN_TEST_ASSERT(result && result->lock && !result->err);
  SVN_ERR(svn_ra_get_lock(session, &lock, path, scratch_pool));
  SVN_TEST_ASSERT(lock);
  return SVN_NO_ERROR;
}

static svn_error_t *
expect_error(const char *path,
             apr_hash_t *results,
             svn_ra_session_t *session,
             apr_pool_t *scratch_pool)
{
  svn_lock_t *lock;
  struct lock_result_t *result = svn_hash_gets(results, path);

  SVN_TEST_ASSERT(result && result->err);
  SVN_TEST_ASSERT(!result->lock);
  /* RA layers shouldn't report SVN_ERR_FS_NOT_FOUND */
  SVN_ERR(svn_ra_get_lock(session, &lock, path, scratch_pool));

  SVN_TEST_ASSERT(!lock);
  return SVN_NO_ERROR;
}

static svn_error_t *
expect_unlock(const char *path,
              apr_hash_t *results,
              svn_ra_session_t *session,
              apr_pool_t *scratch_pool)
{
  svn_lock_t *lock;
  struct lock_result_t *result = svn_hash_gets(results, path);

  SVN_TEST_ASSERT(result && !result->err);
  SVN_ERR(svn_ra_get_lock(session, &lock, path, scratch_pool));
  SVN_TEST_ASSERT(!lock);
  return SVN_NO_ERROR;
}

static svn_error_t *
expect_unlock_error(const char *path,
                    apr_hash_t *results,
                    svn_ra_session_t *session,
                    apr_pool_t *scratch_pool)
{
  svn_lock_t *lock;
  struct lock_result_t *result = svn_hash_gets(results, path);

  SVN_TEST_ASSERT(result && result->err);
  SVN_ERR(svn_ra_get_lock(session, &lock, path, scratch_pool));
  SVN_TEST_ASSERT(lock);
  return SVN_NO_ERROR;
}

/* Test svn_ra_lock(). */
static svn_error_t *
lock_test(const svn_test_opts_t *opts,
          apr_pool_t *pool)
{
  svn_ra_session_t *session;
  apr_hash_t *lock_targets = apr_hash_make(pool);
  apr_hash_t *unlock_targets = apr_hash_make(pool);
  svn_revnum_t rev = 1;
  struct lock_result_t *result;
  struct lock_baton_t baton;
  apr_hash_index_t *hi;

  SVN_ERR(make_and_open_repos(&session, "test-repo-lock", opts, pool));
  SVN_ERR(commit_tree(session, pool));

  baton.results = apr_hash_make(pool);
  baton.pool = pool;

  svn_hash_sets(lock_targets, "A/B/f", &rev);
  svn_hash_sets(lock_targets, "A/B/g", &rev);
  svn_hash_sets(lock_targets, "A/B/z", &rev);
  svn_hash_sets(lock_targets, "A/BB/f", &rev);
  svn_hash_sets(lock_targets, "X/z", &rev);

  /* Lock some paths. */
  SVN_ERR(svn_ra_lock(session, lock_targets, "foo", FALSE, lock_cb, &baton,
                      pool));

  SVN_ERR(expect_lock("A/B/f", baton.results, session, pool));
  SVN_ERR(expect_lock("A/B/g", baton.results, session, pool));
  SVN_ERR(expect_error("A/B/z", baton.results, session, pool));
  SVN_ERR(expect_lock("A/BB/f", baton.results, session, pool));
  SVN_ERR(expect_error("X/z", baton.results, session, pool));

  /* Unlock without force and wrong lock tokens */
  for (hi = apr_hash_first(pool, lock_targets); hi; hi = apr_hash_next(hi))
    svn_hash_sets(unlock_targets, apr_hash_this_key(hi), "wrong-token");
  apr_hash_clear(baton.results);
  SVN_ERR(svn_ra_unlock(session, unlock_targets, FALSE, lock_cb, &baton, pool));

  SVN_ERR(expect_unlock_error("A/B/f", baton.results, session, pool));
  SVN_ERR(expect_unlock_error("A/B/g", baton.results, session, pool));
  SVN_ERR(expect_error("A/B/z", baton.results, session, pool));
  SVN_ERR(expect_unlock_error("A/BB/f", baton.results, session, pool));
  SVN_ERR(expect_error("X/z", baton.results, session, pool));

  /* Force unlock */
  for (hi = apr_hash_first(pool, lock_targets); hi; hi = apr_hash_next(hi))
    svn_hash_sets(unlock_targets, apr_hash_this_key(hi), "");
  apr_hash_clear(baton.results);
  SVN_ERR(svn_ra_unlock(session, unlock_targets, TRUE, lock_cb, &baton, pool));

  SVN_ERR(expect_unlock("A/B/f", baton.results, session, pool));
  SVN_ERR(expect_unlock("A/B/g", baton.results, session, pool));
  SVN_ERR(expect_error("A/B/z", baton.results, session, pool));
  SVN_ERR(expect_unlock("A/BB/f", baton.results, session, pool));
  SVN_ERR(expect_error("X/z", baton.results, session, pool));

  /* Lock again. */
  apr_hash_clear(baton.results);
  SVN_ERR(svn_ra_lock(session, lock_targets, "foo", FALSE, lock_cb, &baton,
                      pool));

  SVN_ERR(expect_lock("A/B/f", baton.results, session, pool));
  SVN_ERR(expect_lock("A/B/g", baton.results, session, pool));
  SVN_ERR(expect_error("A/B/z", baton.results, session, pool));
  SVN_ERR(expect_lock("A/BB/f", baton.results, session, pool));
  SVN_ERR(expect_error("X/z", baton.results, session, pool));

  for (hi = apr_hash_first(pool, baton.results); hi; hi = apr_hash_next(hi))
    {
      result = apr_hash_this_val(hi);
      svn_hash_sets(unlock_targets, apr_hash_this_key(hi),
                    result->lock ? result->lock->token : "non-existent-token");
    }
  apr_hash_clear(baton.results);
  SVN_ERR(svn_ra_unlock(session, unlock_targets, FALSE, lock_cb, &baton, pool));

  SVN_ERR(expect_unlock("A/B/f", baton.results, session, pool));
  SVN_ERR(expect_unlock("A/B/g", baton.results, session, pool));
  SVN_ERR(expect_error("A/B/z", baton.results, session, pool));
  SVN_ERR(expect_unlock("A/BB/f", baton.results, session, pool));
  SVN_ERR(expect_error("X/z", baton.results, session, pool));

  return SVN_NO_ERROR;
}

/* Test svn_ra_get_dir2(). */
static svn_error_t *
get_dir_test(const svn_test_opts_t *opts,
             apr_pool_t *pool)
{
  svn_ra_session_t *session;
  apr_hash_t *dirents;

  SVN_ERR(make_and_open_repos(&session, "test-get-dir", opts, pool));
  SVN_ERR(commit_tree(session, pool));

  /* This call used to block on ra-svn for 1.8.0...r1656713 */
  SVN_TEST_ASSERT_ERROR(svn_ra_get_dir2(session, &dirents, NULL, NULL,
                                        "non/existing/relpath", 1,
                                        SVN_DIRENT_KIND, pool),
                        SVN_ERR_FS_NOT_FOUND);

  return SVN_NO_ERROR;
}



/* The test table.  */

static int max_threads = 2;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(location_segments_test,
                       "test svn_ra_get_location_segments"),
    SVN_TEST_OPTS_PASS(check_tunnel_callback_test,
                       "test ra_svn tunnel callback check"),
    SVN_TEST_OPTS_PASS(tunnel_callback_test,
                       "test ra_svn tunnel creation callbacks"),
    SVN_TEST_OPTS_PASS(lock_test,
                       "lock multiple paths"),
    SVN_TEST_OPTS_PASS(get_dir_test,
                       "test ra_get_dir2"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
