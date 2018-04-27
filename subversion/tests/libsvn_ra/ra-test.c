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

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"
#include "svn_time.h"
#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"

#include "../svn_test.h"
#include "../svn_test_fs.h"
#include "../../libsvn_ra_local/ra_local.h"

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
  SVN_ERR(svn_ra_get_repos_root2(session, &repos_root_url, pool));

  SVN_ERR(editor->open_root(edit_baton, SVN_INVALID_REVNUM,
                            pool, &root_baton));
  /* copy root-dir@0 to A@1 */
  SVN_ERR(editor->add_directory("A", root_baton, repos_root_url, 0,
                               pool, &dir_baton));
  SVN_ERR(editor->close_directory(dir_baton, pool));
  SVN_ERR(editor->close_directory(root_baton, pool));
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
  SVN_ERR(svn_ra_get_repos_root2(session, &repos_root_url, pool));

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
  SVN_ERR(editor->close_directory(root_baton, pool));
  SVN_ERR(editor->close_edit(edit_baton, pool));
  return SVN_NO_ERROR;
}

/* Baton for opening tunnels */
typedef struct tunnel_baton_t
{
  int magic; /* TUNNEL_MAGIC */
  int open_count;
  svn_boolean_t last_check;
} tunnel_baton_t;

#define TUNNEL_MAGIC 0xF00DF00F

/* Baton for closing a specific tunnel */
typedef struct close_baton_t
{
  int magic;
  tunnel_baton_t *tb;
  apr_proc_t *proc;
} close_baton_t;

#define CLOSE_MAGIC 0x1BADBAD1

static svn_boolean_t
check_tunnel(void *tunnel_baton, const char *tunnel_name)
{
  tunnel_baton_t *b = tunnel_baton;

  if (b->magic != TUNNEL_MAGIC)
    abort();

  b->last_check = (0 == strcmp(tunnel_name, "test"));
  return b->last_check;
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
  tunnel_baton_t *b = tunnel_baton;
  close_baton_t *cb;

  SVN_TEST_ASSERT(b->magic == TUNNEL_MAGIC);

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
  apr_pool_note_subprocess(pool, proc, APR_KILL_NEVER);

  /* APR pipe objects inherit by default.  But we don't want the
   * tunnel agent's pipes held open by future child processes
   * (such as other ra_svn sessions), so turn that off. */
  apr_file_inherit_unset(proc->in);
  apr_file_inherit_unset(proc->out);

  cb = apr_pcalloc(pool, sizeof(*cb));
  cb->magic = CLOSE_MAGIC;
  cb->tb = b;
  cb->proc = proc;

  *request = svn_stream_from_aprfile2(proc->in, FALSE, pool);
  *response = svn_stream_from_aprfile2(proc->out, FALSE, pool);
  *close_func = close_tunnel;
  *close_baton = cb;
  ++b->open_count;
  return SVN_NO_ERROR;
}

static void
close_tunnel(void *tunnel_context, void *tunnel_baton)
{
  close_baton_t *b = tunnel_context;

  if (b->magic != CLOSE_MAGIC)
    abort();
  if (--b->tb->open_count == 0)
    {
      apr_status_t child_exit_status;
      int child_exit_code;
      apr_exit_why_e child_exit_why;

      SVN_TEST_ASSERT_NO_RETURN(0 == apr_file_close(b->proc->in));
      SVN_TEST_ASSERT_NO_RETURN(0 == apr_file_close(b->proc->out));

      child_exit_status =
        apr_proc_wait(b->proc, &child_exit_code, &child_exit_why, APR_WAIT);

      SVN_TEST_ASSERT_NO_RETURN(child_exit_status == APR_CHILD_DONE);
      SVN_TEST_ASSERT_NO_RETURN(child_exit_code == 0);
      SVN_TEST_ASSERT_NO_RETURN(child_exit_why == APR_PROC_EXIT);
    }
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
  tunnel_baton_t *b = apr_pcalloc(pool, sizeof(*b));
  svn_ra_callbacks2_t *cbtable;
  svn_ra_session_t *session;

  b->magic = TUNNEL_MAGIC;

  SVN_ERR(svn_ra_create_callbacks(&cbtable, pool));
  cbtable->check_tunnel_func = check_tunnel;
  cbtable->open_tunnel_func = open_tunnel;
  cbtable->tunnel_baton = b;
  SVN_ERR(svn_cmdline_create_auth_baton2(&cbtable->auth_baton,
                                         TRUE  /* non_interactive */,
                                         "jrandom", "rayjandom",
                                         NULL,
                                         TRUE  /* no_auth_cache */,
                                         FALSE /* trust_server_cert */,
                                         FALSE, FALSE, FALSE, FALSE,
                                         NULL, NULL, NULL, pool));

  b->last_check = TRUE;
  SVN_TEST_ASSERT_ERROR(svn_ra_open4(&session, NULL,
                                     "svn+foo://localhost/no-repo",
                                     NULL, cbtable, NULL, NULL, pool),
                        SVN_ERR_RA_CANNOT_CREATE_SESSION);
  SVN_TEST_ASSERT(!b->last_check);
  return SVN_NO_ERROR;
}

static svn_error_t *
tunnel_callback_test(const svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  tunnel_baton_t *b = apr_pcalloc(pool, sizeof(*b));
  apr_pool_t *scratch_pool = svn_pool_create(pool);
  const char *url;
  svn_ra_callbacks2_t *cbtable;
  svn_ra_session_t *session;
  const char tunnel_repos_name[] = "test-repo-tunnel";

  b->magic = TUNNEL_MAGIC;

  SVN_ERR(svn_test__create_repos(NULL, tunnel_repos_name, opts, scratch_pool));

  /* Immediately close the repository to avoid race condition with svnserve
     (and then the cleanup code) with BDB when our pool is cleared. */
  svn_pool_clear(scratch_pool);

  url = apr_pstrcat(pool, "svn+test://localhost/", tunnel_repos_name,
                    SVN_VA_NULL);
  SVN_ERR(svn_ra_create_callbacks(&cbtable, pool));
  cbtable->check_tunnel_func = check_tunnel;
  cbtable->open_tunnel_func = open_tunnel;
  cbtable->tunnel_baton = b;
  SVN_ERR(svn_cmdline_create_auth_baton2(&cbtable->auth_baton,
                                         TRUE  /* non_interactive */,
                                         "jrandom", "rayjandom",
                                         NULL,
                                         TRUE  /* no_auth_cache */,
                                         FALSE /* trust_server_cert */,
                                         FALSE, FALSE, FALSE, FALSE,
                                         NULL, NULL, NULL, pool));

  b->last_check = FALSE;
  SVN_ERR(svn_ra_open4(&session, NULL, url, NULL, cbtable, NULL, NULL,
                        scratch_pool));
  SVN_TEST_ASSERT(b->last_check);
  SVN_TEST_ASSERT(b->open_count > 0);
  svn_pool_destroy(scratch_pool);
  SVN_TEST_ASSERT(b->open_count == 0);
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

  result->lock = svn_lock_dup(lock, b->pool);
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
  svn_dirent_t *ent;

  SVN_ERR(make_and_open_repos(&session, "test-get-dir", opts, pool));
  SVN_ERR(commit_tree(session, pool));

  /* This call used to block on ra-svn for 1.8.0...r1656713 */
  SVN_TEST_ASSERT_ERROR(svn_ra_get_dir2(session, &dirents, NULL, NULL,
                                        "non/existing/relpath", 1,
                                        SVN_DIRENT_KIND, pool),
                        SVN_ERR_FS_NOT_FOUND);

  /* Test fetching SVN_DIRENT_SIZE without SVN_DIRENT_KIND. */
  SVN_ERR(svn_ra_get_dir2(session, &dirents, NULL, NULL, "", 1,
                          SVN_DIRENT_SIZE, pool));
  SVN_TEST_INT_ASSERT(apr_hash_count(dirents), 1);
  ent = svn_hash_gets(dirents, "A");
  SVN_TEST_ASSERT(ent);

#if 0
  /* ra_serf has returns SVN_INVALID_SIZE instead of documented zero for
   * for directories. */
  SVN_TEST_INT_ASSERT(ent->size, 0);
#endif

  return SVN_NO_ERROR;
}

/* Implements svn_commit_callback2_t for commit_callback_failure() */
static svn_error_t *
commit_callback_with_failure(const svn_commit_info_t *info,
                             void *baton,
                             apr_pool_t *scratch_pool)
{
  apr_time_t timetemp;

  SVN_TEST_ASSERT(info != NULL);
  SVN_TEST_STRING_ASSERT(info->author, "jrandom");
  SVN_TEST_STRING_ASSERT(info->post_commit_err, NULL);

  SVN_ERR(svn_time_from_cstring(&timetemp, info->date, scratch_pool));
  SVN_TEST_ASSERT(timetemp != 0);
  SVN_TEST_ASSERT(info->repos_root != NULL);
  SVN_TEST_ASSERT(info->revision == 1);

  return svn_error_create(SVN_ERR_CANCELLED, NULL, NULL);
}

static svn_error_t *
commit_callback_failure(const svn_test_opts_t *opts,
                        apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  void *root_baton;
  SVN_ERR(make_and_open_repos(&ra_session, "commit_cb_failure", opts, pool));

  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    apr_hash_make(pool), commit_callback_with_failure,
                                    NULL, NULL, FALSE, pool));

  SVN_ERR(editor->open_root(edit_baton, 0, pool, &root_baton));
  SVN_ERR(editor->change_dir_prop(root_baton, "A",
                                  svn_string_create("B", pool), pool));
  SVN_ERR(editor->close_directory(root_baton, pool));
  SVN_TEST_ASSERT_ERROR(editor->close_edit(edit_baton, pool),
                        SVN_ERR_CANCELLED);

  /* This is what users should do if close_edit fails... Except that in this case
     the commit actually succeeded*/
  SVN_ERR(editor->abort_edit(edit_baton, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
base_revision_above_youngest(const svn_test_opts_t *opts,
                              apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  void *root_baton;
  svn_error_t *err;
  SVN_ERR(make_and_open_repos(&ra_session, "base_revision_above_youngest",
                              opts, pool));

  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    apr_hash_make(pool), NULL,
                                    NULL, NULL, FALSE, pool));

  /* r1 doesn't exist, but we say we want to apply changes against this
     revision to see how the ra layers behave.

     Some will see an error directly on open_root, others in a later
     state. */

  /* ra-local and http pre-v2 will see the error here */
  err = editor->open_root(edit_baton, 1, pool, &root_baton);

  if (!err)
    err = editor->change_dir_prop(root_baton, "A",
                                  svn_string_create("B", pool), pool);

  /* http v2 will notice it here (PROPPATCH) */
  if (!err)
    err = editor->close_directory(root_baton, pool);

  /* ra svn only notes it at some later point. Typically here */
  if (!err)
    err = editor->close_edit(edit_baton, pool);

  SVN_TEST_ASSERT_ERROR(err,
                        SVN_ERR_FS_NO_SUCH_REVISION);

  SVN_ERR(editor->abort_edit(edit_baton, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_revision_above_youngest(const svn_test_opts_t *opts,
                               apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  const svn_delta_editor_t *editor;
  svn_error_t *err;
  void *edit_baton;

  SVN_ERR(make_and_open_repos(&ra_session, "delete_revision_above_youngest",
                              opts, pool));

  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    apr_hash_make(pool), NULL,
                                    NULL, NULL, FALSE, pool));

  {
    void *root_baton;
    void *dir_baton;

    SVN_ERR(editor->open_root(edit_baton, 0, pool, &root_baton));
    SVN_ERR(editor->add_directory("A", root_baton, NULL, SVN_INVALID_REVNUM,
                                  pool, &dir_baton));
    SVN_ERR(editor->close_directory(dir_baton, pool));
    SVN_ERR(editor->close_directory(root_baton, pool));
    SVN_ERR(editor->close_edit(edit_baton, pool));
  }

  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    apr_hash_make(pool), NULL,
                                    NULL, NULL, FALSE, pool));

  {
    void *root_baton;
    SVN_ERR(editor->open_root(edit_baton, 1, pool, &root_baton));

    /* Now we supply r2, while HEAD is r1 */
    err = editor->delete_entry("A", 2, root_baton, pool);

    if (!err)
      err = editor->close_edit(edit_baton, pool);

    SVN_TEST_ASSERT_ERROR(err,
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_ERR(editor->abort_edit(edit_baton, pool));
  }
  return SVN_NO_ERROR;
}

/* Stub svn_log_entry_receiver_t */
static svn_error_t *
stub_log_receiver(void *baton,
                  svn_log_entry_t *entry,
                  apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* Stub svn_location_segment_receiver_t */
static svn_error_t *
stub_segment_receiver(svn_location_segment_t *segment,
                      void *baton,
                      apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}
/* Stub svn_file_rev_handler_t */
static svn_error_t *
stub_file_rev_handler(void *baton,
                      const char *path,
                      svn_revnum_t rev,
                      apr_hash_t *rev_props,
                      svn_boolean_t result_of_merge,
                      svn_txdelta_window_handler_t *delta_handler,
                      void **delta_baton,
                      apr_array_header_t *prop_diffs,
                      apr_pool_t *pool)
{
  if (delta_handler)
    *delta_handler = svn_delta_noop_window_handler;

  return SVN_NO_ERROR;
}

struct lock_stub_baton_t
{
  apr_status_t result_code;
};

static svn_error_t *
store_lock_result(void *baton,
                  const char *path,
                  svn_boolean_t do_lock,
                  const svn_lock_t *lock,
                  svn_error_t *ra_err,
                  apr_pool_t *pool)
{
  struct lock_stub_baton_t *b = baton;

  b->result_code = ra_err ? ra_err->apr_err : APR_SUCCESS;
  return SVN_NO_ERROR;
}

static svn_error_t *
replay_range_rev_start(svn_revnum_t revision,
                       void *replay_baton,
                       const svn_delta_editor_t **editor,
                       void **edit_baton,
                       apr_hash_t *rev_props,
                       apr_pool_t *pool)
{
  *editor = svn_delta_default_editor(pool);
  *edit_baton = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
replay_range_rev_end(svn_revnum_t revision,
                     void *replay_baton,
                     const svn_delta_editor_t *editor,
                     void *edit_baton,
                     apr_hash_t *rev_props,
                     apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_revision_errors(const svn_test_opts_t *opts,
                   apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  const svn_delta_editor_t *editor;
  svn_error_t *err;
  void *edit_baton;

  /* This function DOESN'T use a scratch/iter pool between requests...

     That has a reason: some ra layers (e.g. Serf) are sensitive to
     reusing the same pool. In that case they may produce bad results
     that they wouldn't do (as often) when the pool wasn't reused.

     It the amount of memory used gets too big we should probably split
     this test... as the reuse already discovered a few issues that
     are now resolved in ra_serf.
   */
  SVN_ERR(make_and_open_repos(&ra_session, "ra_revision_errors",
                              opts, pool));

  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    apr_hash_make(pool), NULL,
                                    NULL, NULL, FALSE, pool));

  {
    void *root_baton;
    void *dir_baton;
    void *file_baton;

    SVN_ERR(editor->open_root(edit_baton, 0, pool, &root_baton));
    SVN_ERR(editor->add_directory("A", root_baton, NULL, SVN_INVALID_REVNUM,
                                  pool, &dir_baton));
    SVN_ERR(editor->add_file("A/iota", dir_baton, NULL, SVN_INVALID_REVNUM,
                             pool, &file_baton));
    SVN_ERR(editor->close_file(file_baton, NULL, pool));
    SVN_ERR(editor->close_directory(dir_baton, pool));
    SVN_ERR(editor->add_directory("B", root_baton, NULL, SVN_INVALID_REVNUM,
                                  pool, &dir_baton));
    SVN_ERR(editor->close_directory(dir_baton, pool));
    SVN_ERR(editor->add_directory("C", root_baton, NULL, SVN_INVALID_REVNUM,
                                  pool, &dir_baton));
    SVN_ERR(editor->close_directory(dir_baton, pool));
    SVN_ERR(editor->add_directory("D", root_baton, NULL, SVN_INVALID_REVNUM,
                                  pool, &dir_baton));
    SVN_ERR(editor->close_directory(dir_baton, pool));
    SVN_ERR(editor->close_directory(root_baton, pool));
    SVN_ERR(editor->close_edit(edit_baton, pool));
  }

  {
    const svn_ra_reporter3_t *reporter;
    void *report_baton;

    err = svn_ra_do_update3(ra_session, &reporter, &report_baton,
                            2, "", svn_depth_infinity, FALSE, FALSE,
                            svn_delta_default_editor(pool), NULL,
                            pool, pool);

    if (!err)
      err = reporter->set_path(report_baton, "", 0, svn_depth_infinity, FALSE,
                               NULL, pool);

    if (!err)
      err = reporter->finish_report(report_baton, pool);

    SVN_TEST_ASSERT_ERROR(err, SVN_ERR_FS_NO_SUCH_REVISION);
  }

  {
    const svn_ra_reporter3_t *reporter;
    void *report_baton;

    err = svn_ra_do_update3(ra_session, &reporter, &report_baton,
                            1, "", svn_depth_infinity, FALSE, FALSE,
                            svn_delta_default_editor(pool), NULL,
                            pool, pool);

    if (!err)
      err = reporter->set_path(report_baton, "", 2, svn_depth_infinity, FALSE,
                               NULL, pool);

    if (!err)
      err = reporter->finish_report(report_baton, pool);

    SVN_TEST_ASSERT_ERROR(err, SVN_ERR_FS_NO_SUCH_REVISION);
  }

  {
    const svn_ra_reporter3_t *reporter;
    void *report_baton;

    err = svn_ra_do_update3(ra_session, &reporter, &report_baton,
                            1, "", svn_depth_infinity, FALSE, FALSE,
                            svn_delta_default_editor(pool), NULL,
                            pool, pool);

    if (!err)
      err = reporter->set_path(report_baton, "", 0, svn_depth_infinity, FALSE,
                               NULL, pool);

    if (!err)
      err = reporter->finish_report(report_baton, pool);

    SVN_ERR(err);
  }

  {
    svn_revnum_t revision;

    SVN_ERR(svn_ra_get_dated_revision(ra_session, &revision,
                                      apr_time_now() - apr_time_from_sec(3600),
                                      pool));

    SVN_TEST_ASSERT(revision == 0);

    SVN_ERR(svn_ra_get_dated_revision(ra_session, &revision,
                                      apr_time_now() + apr_time_from_sec(3600),
                                      pool));

    SVN_TEST_ASSERT(revision == 1);
  }

  {
    /* SVN_INVALID_REVNUM is protected by assert in ra loader */

    SVN_TEST_ASSERT_ERROR(svn_ra_change_rev_prop2(ra_session,
                                                  2,
                                                  "bad", NULL,
                                                  svn_string_create("value",
                                                                    pool),
                                                  pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);
  }

  {
    apr_hash_t *props;
    svn_string_t *value;

    /* SVN_INVALID_REVNUM is protected by assert in ra loader */

    SVN_TEST_ASSERT_ERROR(svn_ra_rev_proplist(ra_session, 2, &props, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_TEST_ASSERT_ERROR(svn_ra_rev_prop(ra_session, 2, "bad", &value, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);
  }

  {
    apr_hash_t *props;
    svn_string_t *value;

    /* SVN_INVALID_REVNUM is protected by assert in ra loader */

    SVN_TEST_ASSERT_ERROR(svn_ra_rev_proplist(ra_session, 2, &props, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_TEST_ASSERT_ERROR(svn_ra_rev_prop(ra_session, 2, "bad", &value, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);
  }

  {
    svn_revnum_t fetched;
    apr_hash_t *props;

    SVN_TEST_ASSERT_ERROR(svn_ra_get_file(ra_session, "A", 1,
                                          svn_stream_empty(pool), &fetched,
                                          &props, pool),
                          SVN_ERR_FS_NOT_FILE);

    SVN_TEST_ASSERT_ERROR(svn_ra_get_file(ra_session, "A/iota", 2,
                                          svn_stream_empty(pool), &fetched,
                                          &props, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_TEST_ASSERT_ERROR(svn_ra_get_file(ra_session, "Z", 1,
                                          svn_stream_empty(pool), &fetched,
                                          &props, pool),
                          SVN_ERR_FS_NOT_FOUND);

    SVN_ERR(svn_ra_get_file(ra_session, "A/iota", SVN_INVALID_REVNUM,
                            svn_stream_empty(pool), &fetched,
                            &props, pool));
    SVN_TEST_ASSERT(fetched == 1);
  }

  {
    svn_revnum_t fetched;
    apr_hash_t *dirents;
    apr_hash_t *props;

    SVN_TEST_ASSERT_ERROR(svn_ra_get_dir2(ra_session, &dirents, &fetched,
                                          &props, "A/iota", 1,
                                          SVN_DIRENT_ALL, pool),
                          SVN_ERR_FS_NOT_DIRECTORY);

    SVN_TEST_ASSERT_ERROR(svn_ra_get_dir2(ra_session, &dirents, &fetched,
                                          &props, "A", 2,
                                          SVN_DIRENT_ALL, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_TEST_ASSERT_ERROR(svn_ra_get_dir2(ra_session, &dirents, &fetched,
                                          &props, "Z", 1,
                                          SVN_DIRENT_ALL, pool),
                          SVN_ERR_FS_NOT_FOUND);

    SVN_ERR(svn_ra_get_dir2(ra_session, &dirents, &fetched,
                            &props, "A", SVN_INVALID_REVNUM,
                            SVN_DIRENT_ALL, pool));
    SVN_TEST_ASSERT(fetched == 1);
    SVN_TEST_ASSERT(apr_hash_count(dirents) == 1);
  }

  {
    svn_mergeinfo_catalog_t catalog;
    apr_array_header_t *paths = apr_array_make(pool, 1, sizeof(const char*));
    APR_ARRAY_PUSH(paths, const char *) = "A";

    SVN_TEST_ASSERT_ERROR(svn_ra_get_mergeinfo(ra_session, &catalog, paths,
                                               2, svn_mergeinfo_inherited,
                                               FALSE, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_TEST_ASSERT_ERROR(svn_ra_get_mergeinfo(ra_session, &catalog, paths,
                                               0, svn_mergeinfo_inherited,
                                               FALSE, pool),
                          SVN_ERR_FS_NOT_FOUND);

    SVN_ERR(svn_ra_get_mergeinfo(ra_session, &catalog, paths,
                                 SVN_INVALID_REVNUM, svn_mergeinfo_inherited,
                                 FALSE, pool));
  }

  {
    apr_array_header_t *paths = apr_array_make(pool, 1, sizeof(const char*));
    APR_ARRAY_PUSH(paths, const char *) = "A";

    SVN_TEST_ASSERT_ERROR(svn_ra_get_log2(ra_session, paths, 0, 2, -1,
                                          FALSE, FALSE, FALSE, NULL,
                                          stub_log_receiver, NULL, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_TEST_ASSERT_ERROR(svn_ra_get_log2(ra_session, paths, 2, 0, -1,
                                          FALSE, FALSE, FALSE, NULL,
                                          stub_log_receiver, NULL, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_TEST_ASSERT_ERROR(svn_ra_get_log2(ra_session, paths,
                                          SVN_INVALID_REVNUM, 2, -1,
                                          FALSE, FALSE, FALSE, NULL,
                                          stub_log_receiver, NULL, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_TEST_ASSERT_ERROR(svn_ra_get_log2(ra_session, paths,
                                          2, SVN_INVALID_REVNUM, -1,
                                          FALSE, FALSE, FALSE, NULL,
                                          stub_log_receiver, NULL, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);
  }

  {
    svn_node_kind_t kind;
    SVN_TEST_ASSERT_ERROR(svn_ra_check_path(ra_session, "A", 2, &kind, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_ERR(svn_ra_check_path(ra_session, "A", SVN_INVALID_REVNUM, &kind,
                              pool));

    SVN_TEST_ASSERT(kind == svn_node_dir);
  }

  {
    svn_dirent_t *dirent;
    apr_array_header_t *paths = apr_array_make(pool, 1, sizeof(const char*));
    APR_ARRAY_PUSH(paths, const char *) = "A";

    SVN_TEST_ASSERT_ERROR(svn_ra_stat(ra_session, "A", 2, &dirent, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_ERR(svn_ra_stat(ra_session, "A", SVN_INVALID_REVNUM, &dirent,
                              pool));

    SVN_TEST_ASSERT(dirent->kind == svn_node_dir);
  }

  {
    apr_hash_t *locations;
    apr_array_header_t *revisions = apr_array_make(pool, 2, sizeof(svn_revnum_t));
    APR_ARRAY_PUSH(revisions, svn_revnum_t) = 1;

    /* SVN_INVALID_REVNUM as passed revision doesn't work */

    SVN_TEST_ASSERT_ERROR(svn_ra_get_locations(ra_session, &locations, "A", 2,
                                               revisions, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    APR_ARRAY_PUSH(revisions, svn_revnum_t) = 7;
    SVN_TEST_ASSERT_ERROR(svn_ra_get_locations(ra_session, &locations, "A", 1,
                                               revisions, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    /* Putting SVN_INVALID_REVNUM in the array doesn't marshal properly in svn://
     */
  }

  {
    /* peg_rev   -> SVN_INVALID_REVNUM -> youngest
       start_rev -> SVN_INVALID_REVNUM -> peg_rev
       end_rev   -> SVN_INVALID_REVNUM -> 0 */
    SVN_TEST_ASSERT_ERROR(svn_ra_get_location_segments(ra_session, "A",
                                                       2, 1, 0,
                                                       stub_segment_receiver,
                                                       NULL, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_TEST_ASSERT_ERROR(svn_ra_get_location_segments(ra_session, "A",
                                                       SVN_INVALID_REVNUM,
                                                       2, 0,
                                                       stub_segment_receiver,
                                                       NULL, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);


    SVN_TEST_ASSERT_ERROR(svn_ra_get_location_segments(ra_session, "A",
                                                       SVN_INVALID_REVNUM,
                                                       SVN_INVALID_REVNUM,
                                                       2,
                                                       stub_segment_receiver,
                                                       NULL, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_ERR(svn_ra_get_location_segments(ra_session, "A",
                                         SVN_INVALID_REVNUM,
                                         SVN_INVALID_REVNUM,
                                         SVN_INVALID_REVNUM,
                                         stub_segment_receiver,
                                         NULL, pool));
  }

  {
    SVN_TEST_ASSERT_ERROR(svn_ra_get_file_revs2(ra_session, "A/iota", 2, 0,
                                                FALSE, stub_file_rev_handler,
                                                NULL, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_TEST_ASSERT_ERROR(svn_ra_get_file_revs2(ra_session, "A/iota", 0, 2,
                                                FALSE, stub_file_rev_handler,
                                                NULL, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_TEST_ASSERT_ERROR(svn_ra_get_file_revs2(ra_session, "A", 1, 1,
                                                FALSE, stub_file_rev_handler,
                                                NULL, pool),
                          SVN_ERR_FS_NOT_FILE);
  }

  {
    apr_hash_t *locks = apr_hash_make(pool);
    svn_revnum_t rev = 2;
    struct lock_stub_baton_t lr = {0};

    svn_hash_sets(locks, "A/iota", &rev);

    SVN_ERR(svn_ra_lock(ra_session, locks, "comment", FALSE,
                         store_lock_result, &lr, pool));
    SVN_TEST_ASSERT(lr.result_code == SVN_ERR_FS_NO_SUCH_REVISION);

    rev = 0;
    SVN_ERR(svn_ra_lock(ra_session, locks, "comment", FALSE,
                         store_lock_result, &lr, pool));
    SVN_TEST_ASSERT(lr.result_code == SVN_ERR_FS_OUT_OF_DATE);

    svn_hash_sets(locks, "A/iota", NULL);
    svn_hash_sets(locks, "A", &rev);
    rev = SVN_INVALID_REVNUM;
    SVN_ERR(svn_ra_lock(ra_session, locks, "comment", FALSE,
                        store_lock_result, &lr, pool));
    SVN_TEST_ASSERT(lr.result_code == SVN_ERR_FS_NOT_FILE);
  }

  {
    apr_hash_t *locks = apr_hash_make(pool);
    struct lock_stub_baton_t lr = {0};

    svn_hash_sets(locks, "A/iota", "no-token");

    SVN_ERR(svn_ra_unlock(ra_session, locks, FALSE,
                          store_lock_result, &lr, pool));
    SVN_TEST_ASSERT(lr.result_code == SVN_ERR_FS_NO_SUCH_LOCK);


    svn_hash_sets(locks, "A/iota", NULL);
    svn_hash_sets(locks, "A", "no-token");
    SVN_ERR(svn_ra_unlock(ra_session, locks, FALSE,
                          store_lock_result, &lr, pool));
    SVN_TEST_ASSERT(lr.result_code == SVN_ERR_FS_NO_SUCH_LOCK);
  }

  {
    svn_lock_t *lock;
    SVN_ERR(svn_ra_get_lock(ra_session, &lock, "A", pool));
    SVN_TEST_ASSERT(lock == NULL);
  }

  {
    SVN_TEST_ASSERT_ERROR(svn_ra_replay(ra_session, 2, 0, TRUE,
                                        svn_delta_default_editor(pool), NULL,
                                        pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    /* Simply assumes everything is there*/
    SVN_ERR(svn_ra_replay(ra_session, 1, 2, TRUE,
                          svn_delta_default_editor(pool), NULL,
                          pool));
  }

  {
    SVN_TEST_ASSERT_ERROR(svn_ra_replay_range(ra_session, 1, 2, 0,
                                              TRUE,
                                              replay_range_rev_start,
                                              replay_range_rev_end, NULL,
                                              pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    /* Simply assumes everything is there*/
    SVN_TEST_ASSERT_ERROR(svn_ra_replay_range(ra_session, 2, 2, 0,
                                              TRUE,
                                              replay_range_rev_start,
                                              replay_range_rev_end, NULL,
                                              pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);
  }

  {
    svn_revnum_t del_rev;

    /* ### Explicitly documented to not return an FS or RA error???? */

    SVN_TEST_ASSERT_ERROR(svn_ra_get_deleted_rev(ra_session, "Z", 2, 1,
                                                 &del_rev, pool),
                          SVN_ERR_CLIENT_BAD_REVISION);

    SVN_TEST_ASSERT_ERROR(svn_ra_get_deleted_rev(ra_session, "Z",
                                                 SVN_INVALID_REVNUM, 2,
                                                 &del_rev, pool),
                          SVN_ERR_CLIENT_BAD_REVISION);

  }

  {
    apr_array_header_t *iprops;

    SVN_TEST_ASSERT_ERROR(svn_ra_get_inherited_props(ra_session, &iprops,
                                                     "A", 2, pool, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);
    SVN_TEST_ASSERT_ERROR(svn_ra_get_inherited_props(ra_session, &iprops,
                                                     "A", SVN_INVALID_REVNUM,
                                                     pool, pool),
                          SVN_ERR_FS_NO_SUCH_REVISION);

    SVN_TEST_ASSERT_ERROR(svn_ra_get_inherited_props(ra_session, &iprops,
                                                     "Z", 1,
                                                     pool, pool),
                          SVN_ERR_FS_NOT_FOUND);
  }

  return SVN_NO_ERROR;
}
/* svn_log_entry_receiver_t returning cease invocation */
static svn_error_t *
error_log_receiver(void *baton,
                  svn_log_entry_t *entry,
                  apr_pool_t *scratch_pool)
{
  return svn_error_create(SVN_ERR_CEASE_INVOCATION, NULL, NULL);
}

/* Stub svn_location_segment_receiver_t */
static svn_error_t *
error_segment_receiver(svn_location_segment_t *segment,
                      void *baton,
                      apr_pool_t *scratch_pool)
{
  return svn_error_create(SVN_ERR_CEASE_INVOCATION, NULL, NULL);
}


static svn_error_t *
errors_from_callbacks(const svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  const svn_delta_editor_t *editor;
  void *edit_baton;

  /* This function DOESN'T use a scratch/iter pool between requests...

     That has a reason: some ra layers (e.g. Serf) are sensitive to
     reusing the same pool. In that case they may produce bad results
     that they wouldn't do (as often) when the pool wasn't reused.

     It the amount of memory used gets too big we should probably split
     this test... as the reuse already discovered a few issues that
     are now resolved in ra_serf.
   */
  SVN_ERR(make_and_open_repos(&ra_session, "errors_from_callbacks",
                              opts, pool));

  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    apr_hash_make(pool), NULL,
                                    NULL, NULL, FALSE, pool));

  {
    void *root_baton;
    void *dir_baton;
    void *file_baton;

    SVN_ERR(editor->open_root(edit_baton, 0, pool, &root_baton));
    SVN_ERR(editor->add_directory("A", root_baton, NULL, SVN_INVALID_REVNUM,
                                  pool, &dir_baton));
    SVN_ERR(editor->add_file("A/iota", dir_baton, NULL, SVN_INVALID_REVNUM,
                             pool, &file_baton));
    SVN_ERR(editor->close_file(file_baton, NULL, pool));
    SVN_ERR(editor->close_directory(dir_baton, pool));
    SVN_ERR(editor->add_directory("B", root_baton, NULL, SVN_INVALID_REVNUM,
                                  pool, &dir_baton));
    SVN_ERR(editor->close_directory(dir_baton, pool));
    SVN_ERR(editor->add_directory("C", root_baton, NULL, SVN_INVALID_REVNUM,
                                  pool, &dir_baton));
    SVN_ERR(editor->close_directory(dir_baton, pool));
    SVN_ERR(editor->add_directory("D", root_baton, NULL, SVN_INVALID_REVNUM,
                                  pool, &dir_baton));
    SVN_ERR(editor->close_directory(dir_baton, pool));
    SVN_ERR(editor->close_directory(root_baton, pool));
    SVN_ERR(editor->close_edit(edit_baton, pool));
  }

  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    apr_hash_make(pool), NULL,
                                    NULL, NULL, FALSE, pool));

  {
    void *root_baton;
    void *dir_baton;
    void *file_baton;

    SVN_ERR(editor->open_root(edit_baton, 1, pool, &root_baton));
    SVN_ERR(editor->open_directory("A", root_baton, 1, pool, &dir_baton));
    SVN_ERR(editor->open_file("A/iota", dir_baton, 1, pool, &file_baton));

    SVN_ERR(editor->change_file_prop(file_baton, "A", svn_string_create("B",
                                                                        pool),
                                     pool));

    SVN_ERR(editor->close_file(file_baton, NULL, pool));

    SVN_ERR(editor->change_dir_prop(dir_baton, "A", svn_string_create("B",
                                                                        pool),
                                     pool));
    SVN_ERR(editor->close_directory(dir_baton, pool));
    SVN_ERR(editor->close_directory(root_baton, pool));
    SVN_ERR(editor->close_edit(edit_baton, pool));
  }

  {
    apr_array_header_t *paths = apr_array_make(pool, 1, sizeof(const char*));
    APR_ARRAY_PUSH(paths, const char *) = "A/iota";

    /* Note that ra_svn performs OK for SVN_ERR_CEASE_INVOCATION, but any
       other error will make it break the ra session for further operations */

    SVN_TEST_ASSERT_ERROR(svn_ra_get_log2(ra_session, paths, 2, 0, -1,
                                          FALSE, FALSE, FALSE, NULL,
                                          error_log_receiver, NULL, pool),
                          SVN_ERR_CEASE_INVOCATION);
  }

  {
    /* Note that ra_svn performs OK for SVN_ERR_CEASE_INVOCATION, but any
       other error will make it break the ra session for further operations */

    SVN_TEST_ASSERT_ERROR(svn_ra_get_location_segments(ra_session, "A/iota",
                                                       2, 2, 0,
                                                       error_segment_receiver,
                                                       NULL, pool),
                          SVN_ERR_CEASE_INVOCATION);
  }

  /* And a final check to see if the ra session is still ok */
  {
    svn_node_kind_t kind;

    SVN_ERR(svn_ra_check_path(ra_session, "A", 2, &kind, pool));

    SVN_TEST_ASSERT(kind == svn_node_dir);
  }
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_list_has_props(const svn_test_opts_t *opts,
                  apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  const svn_delta_editor_t *editor;
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i;
  void *edit_baton;
  const char *trunk_url;

  SVN_ERR(make_and_open_repos(&ra_session, "ra_list_has_props",
                              opts, pool));

  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    apr_hash_make(pool), NULL,
                                    NULL, NULL, FALSE, iterpool));

  /* Create initial layout*/
  {
    void *root_baton;
    void *dir_baton;

    SVN_ERR(editor->open_root(edit_baton, 0, pool, &root_baton));
    SVN_ERR(editor->add_directory("trunk", root_baton, NULL, SVN_INVALID_REVNUM,
                                  iterpool, &dir_baton));
    SVN_ERR(editor->close_directory(dir_baton, iterpool));
    SVN_ERR(editor->add_directory("tags", root_baton, NULL, SVN_INVALID_REVNUM,
                                  iterpool, &dir_baton));
    SVN_ERR(editor->close_directory(dir_baton, iterpool));
    SVN_ERR(editor->close_directory(root_baton, iterpool));
    SVN_ERR(editor->close_edit(edit_baton, iterpool));
  }

  SVN_ERR(svn_ra_get_repos_root2(ra_session, &trunk_url, pool));
  trunk_url = svn_path_url_add_component2(trunk_url, "trunk", pool);

  /* Create a few tags. Using a value like 8000 will take too long for a normal
     testrun, but produces more realistic problems */
  for (i = 0; i < 50; i++)
    {
      void *root_baton;
      void *tags_baton;
      void *dir_baton;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                        apr_hash_make(pool), NULL,
                                        NULL, NULL, FALSE, iterpool));

      SVN_ERR(editor->open_root(edit_baton, i+1, pool, &root_baton));
      SVN_ERR(editor->open_directory("tags", root_baton, i+1, iterpool,
                                     &tags_baton));
      SVN_ERR(editor->add_directory(apr_psprintf(iterpool, "tags/T%05d", i+1),
                                    tags_baton, trunk_url, 1, iterpool,
                                    &dir_baton));

      SVN_ERR(editor->close_directory(dir_baton, iterpool));
      SVN_ERR(editor->close_directory(tags_baton, iterpool));
      SVN_ERR(editor->close_directory(root_baton, iterpool));
      SVN_ERR(editor->close_edit(edit_baton, iterpool));
    }

  {
    apr_hash_t *dirents;
    svn_revnum_t fetched_rev;
    apr_hash_t *props;

    SVN_ERR(svn_ra_get_dir2(ra_session, &dirents, &fetched_rev, &props,
                            "tags", SVN_INVALID_REVNUM,
                            SVN_DIRENT_ALL, pool));
  }

  return SVN_NO_ERROR;
}

/* Test ra_svn tunnel editor handling, including polling. */

static svn_error_t *
tunnel_run_checkout(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  tunnel_baton_t *b = apr_pcalloc(pool, sizeof(*b));
  apr_pool_t *scratch_pool = svn_pool_create(pool);
  const char *url;
  svn_ra_callbacks2_t *cbtable;
  svn_ra_session_t *session;
  const char tunnel_repos_name[] = "test-run_checkout";
  const svn_ra_reporter3_t *reporter;
  void *report_baton;

  b->magic = TUNNEL_MAGIC;

  SVN_ERR(svn_test__create_repos(NULL, tunnel_repos_name, opts, scratch_pool));

  /* Immediately close the repository to avoid race condition with svnserve
  (and then the cleanup code) with BDB when our pool is cleared. */
  svn_pool_clear(scratch_pool);

  url = apr_pstrcat(pool, "svn+test://localhost/", tunnel_repos_name,
    SVN_VA_NULL);
  SVN_ERR(svn_ra_create_callbacks(&cbtable, pool));
  cbtable->check_tunnel_func = check_tunnel;
  cbtable->open_tunnel_func = open_tunnel;
  cbtable->tunnel_baton = b;
  SVN_ERR(svn_cmdline_create_auth_baton2(&cbtable->auth_baton,
    TRUE  /* non_interactive */,
    "jrandom", "rayjandom",
    NULL,
    TRUE  /* no_auth_cache */,
    FALSE /* trust_server_cert */,
    FALSE, FALSE, FALSE, FALSE,
    NULL, NULL, NULL, pool));

  b->last_check = FALSE;

  SVN_ERR(svn_ra_open4(&session, NULL, url, NULL, cbtable, NULL, NULL,
                       scratch_pool));

  SVN_ERR(commit_changes(session, pool));

  SVN_ERR(svn_ra_do_update3(session,
                            &reporter, &report_baton,
                            1, "",
                            svn_depth_infinity, FALSE, FALSE,
                            svn_delta_default_editor(pool), NULL,
                            pool, pool));

  SVN_ERR(reporter->set_path(report_baton, "", 0, svn_depth_infinity, FALSE,
                             NULL, pool));

  SVN_ERR(reporter->finish_report(report_baton, pool));

  return SVN_NO_ERROR;
}

/* Implements svn_log_entry_receiver_t for commit_empty_last_change */
static svn_error_t *
AA_receiver(void *baton,
            svn_log_entry_t *log_entry,
            apr_pool_t *pool)
{
  svn_log_changed_path2_t *p;
  apr_hash_index_t *hi;

  SVN_TEST_ASSERT(log_entry->changed_paths2 != NULL);
  SVN_TEST_ASSERT(apr_hash_count(log_entry->changed_paths2) == 1);

  hi = apr_hash_first(pool, log_entry->changed_paths2);

  SVN_TEST_STRING_ASSERT(apr_hash_this_key(hi), "/AA");
  p = apr_hash_this_val(hi);
  SVN_TEST_STRING_ASSERT(p->copyfrom_path, "/A");
  SVN_TEST_INT_ASSERT(p->copyfrom_rev, 3);

  return SVN_NO_ERROR;
}

static svn_error_t *
commit_empty_last_change(const svn_test_opts_t *opts,
                         apr_pool_t *pool)
{
  svn_ra_session_t *session;
  apr_hash_t *revprop_table = apr_hash_make(pool);
  const svn_delta_editor_t *editor;
  void *edit_baton;
  const char *repos_root_url;
  void *root_baton, *aa_baton;
  apr_pool_t *tmp_pool = svn_pool_create(pool);
  svn_dirent_t *dirent;
  int i;

  SVN_ERR(make_and_open_repos(&session,
                              "commit_empty_last_change", opts,
                              pool));

  SVN_ERR(commit_changes(session, tmp_pool));

  SVN_ERR(svn_ra_get_repos_root2(session, &repos_root_url, pool));
  for (i = 0; i < 2; i++)
    {
      svn_pool_clear(tmp_pool);

      SVN_ERR(svn_ra_get_commit_editor3(session, &editor, &edit_baton,
                                        revprop_table,
                                        NULL, NULL, NULL, TRUE, tmp_pool));
      
      SVN_ERR(editor->open_root(edit_baton, 1, tmp_pool, &root_baton));
      SVN_ERR(editor->close_directory(root_baton, tmp_pool));
      SVN_ERR(editor->close_edit(edit_baton, tmp_pool));
      
      SVN_ERR(svn_ra_stat(session, "", 2+i, &dirent, tmp_pool));
      
      SVN_TEST_ASSERT(dirent != NULL);
      SVN_TEST_STRING_ASSERT(dirent->last_author, "jrandom");
      
      /* BDB used to only updates last_changed on the repos_root when there
         was an actual change. Now all filesystems behave in the same way */
      SVN_TEST_INT_ASSERT(dirent->created_rev, 2+i);
    }

  svn_pool_clear(tmp_pool);

  SVN_ERR(svn_ra_get_commit_editor3(session, &editor, &edit_baton,
                                    revprop_table,
                                    NULL, NULL, NULL, TRUE, tmp_pool));

  SVN_ERR(editor->open_root(edit_baton, 1, tmp_pool, &root_baton));
  SVN_ERR(editor->add_directory("AA", root_baton,
                                svn_path_url_add_component2(repos_root_url,
                                                            "A", tmp_pool),
                                3, tmp_pool,
                                &aa_baton));
  SVN_ERR(editor->close_directory(aa_baton, tmp_pool));
  SVN_ERR(editor->close_directory(root_baton, tmp_pool));
  SVN_ERR(editor->close_edit(edit_baton, tmp_pool));

  svn_pool_clear(tmp_pool);

  {
    apr_array_header_t *paths = apr_array_make(tmp_pool, 1, sizeof(const char*));
    APR_ARRAY_PUSH(paths, const char *) = "AA";

    SVN_ERR(svn_ra_get_log2(session, paths, 4, 4, 1, TRUE, FALSE, FALSE, NULL,
                            AA_receiver, NULL, tmp_pool));
  }

  svn_pool_destroy(tmp_pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
commit_locked_file(const svn_test_opts_t *opts, apr_pool_t *pool)
{
  const char *url;
  svn_ra_callbacks2_t *cbtable;
  svn_ra_session_t *session;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  void *root_baton;
  void *file_baton;
  struct lock_result_t *lock_result;
  apr_hash_t *lock_tokens;
  svn_txdelta_window_handler_t handler;
  void *handler_baton;
  svn_revnum_t fetched_rev;
  apr_hash_t *fetched_props;
  const svn_string_t *propval;

  SVN_ERR(svn_test__create_repos2(NULL, &url, NULL,
                                  "test-repo-commit-locked-file-test",
                                  opts, pool, pool));

  SVN_ERR(svn_ra_initialize(pool));
  SVN_ERR(svn_ra_create_callbacks(&cbtable, pool));
  SVN_ERR(svn_test__init_auth_baton(&cbtable->auth_baton, pool));

  SVN_ERR(svn_ra_open4(&session, NULL, url, NULL, cbtable,
                       NULL, NULL, pool));
  SVN_ERR(svn_ra_get_commit_editor3(session, &editor, &edit_baton,
                                    apr_hash_make(pool),
                                    NULL, NULL, NULL, TRUE, pool));
  /* Add a file. */
  SVN_ERR(editor->open_root(edit_baton, SVN_INVALID_REVNUM,
                            pool, &root_baton));
  SVN_ERR(editor->add_file("file", root_baton, NULL, SVN_INVALID_REVNUM,
                           pool, &file_baton));
  SVN_ERR(editor->close_file(file_baton, NULL, pool));
  SVN_ERR(editor->close_directory(root_baton, pool));
  SVN_ERR(editor->close_edit(edit_baton, pool));

  /* Acquire a lock on this file. */
  {
    struct lock_baton_t baton = {0};
    svn_revnum_t rev = 1;
    apr_hash_t *lock_targets;

    baton.results = apr_hash_make(pool);
    baton.pool = pool;

    lock_targets = apr_hash_make(pool);
    svn_hash_sets(lock_targets, "file", &rev);
    SVN_ERR(svn_ra_lock(session, lock_targets, "comment", FALSE,
                        lock_cb, &baton, pool));

    SVN_ERR(expect_lock("file", baton.results, session, pool));
    lock_result = svn_hash_gets(baton.results, "file");
  }

  /* Open a new session using the file parent's URL. */
  SVN_ERR(svn_ra_open4(&session, NULL, url, NULL, cbtable,
                       NULL, NULL, pool));

  /* Create a new commit editor supplying our lock token. */
  lock_tokens = apr_hash_make(pool);
  svn_hash_sets(lock_tokens, "file", lock_result->lock->token);
  SVN_ERR(svn_ra_get_commit_editor3(session, &editor, &edit_baton,
                                    apr_hash_make(pool), NULL, NULL,
                                    lock_tokens, TRUE, pool));
  /* Edit the locked file. */
  SVN_ERR(editor->open_root(edit_baton, SVN_INVALID_REVNUM,
                            pool, &root_baton));
  SVN_ERR(editor->open_file("file", root_baton, SVN_INVALID_REVNUM, pool,
                            &file_baton));
  SVN_ERR(editor->apply_textdelta(file_baton, NULL, pool, &handler,
                                  &handler_baton));
  SVN_ERR(svn_txdelta_send_string(svn_string_create("A", pool),
                                  handler, handler_baton, pool));
  SVN_ERR(editor->close_file(file_baton, NULL, pool));
  SVN_ERR(editor->close_directory(root_baton, pool));
  SVN_ERR(editor->close_edit(edit_baton, pool));

  /* Check the result. */
  SVN_ERR(svn_ra_get_file(session, "file", SVN_INVALID_REVNUM, NULL,
                          &fetched_rev, NULL, pool));
  SVN_TEST_INT_ASSERT((int) fetched_rev, 2);

  /* Change property of the locked file. */
  SVN_ERR(svn_ra_get_commit_editor3(session, &editor, &edit_baton,
                                    apr_hash_make(pool), NULL, NULL,
                                    lock_tokens, TRUE, pool));
  SVN_ERR(editor->open_root(edit_baton, SVN_INVALID_REVNUM,
                            pool, &root_baton));
  SVN_ERR(editor->open_file("file", root_baton, SVN_INVALID_REVNUM, pool,
                            &file_baton));
  SVN_ERR(editor->change_file_prop(file_baton, "propname",
                                   svn_string_create("propval", pool),
                                   pool));
  SVN_ERR(editor->close_file(file_baton, NULL, pool));
  SVN_ERR(editor->close_directory(root_baton, pool));
  SVN_ERR(editor->close_edit(edit_baton, pool));

  /* Check the result. */
  SVN_ERR(svn_ra_get_file(session, "file", SVN_INVALID_REVNUM, NULL,
                          &fetched_rev, &fetched_props, pool));
  SVN_TEST_INT_ASSERT((int) fetched_rev, 3);
  propval = svn_hash_gets(fetched_props, "propname");
  SVN_TEST_ASSERT(propval);
  SVN_TEST_STRING_ASSERT(propval->data, "propval");

  return SVN_NO_ERROR;
}


/* The test table.  */

static int max_threads = 4;

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
    SVN_TEST_OPTS_PASS(commit_callback_failure,
                       "commit callback failure"),
    SVN_TEST_OPTS_PASS(base_revision_above_youngest,
                       "base revision newer than youngest"),
    SVN_TEST_OPTS_PASS(delete_revision_above_youngest,
                       "delete revision newer than youngest"),
    SVN_TEST_OPTS_PASS(ra_revision_errors,
                       "check how ra functions handle bad revisions"),
    SVN_TEST_OPTS_PASS(errors_from_callbacks,
                       "check how ra layers handle errors from callbacks"),
    SVN_TEST_OPTS_PASS(ra_list_has_props,
                       "check list has_props performance"),
    SVN_TEST_OPTS_PASS(tunnel_run_checkout,
                       "verify checkout over a tunnel"),
    SVN_TEST_OPTS_PASS(commit_empty_last_change,
                       "check how last change applies to empty commit"),
    SVN_TEST_OPTS_PASS(commit_locked_file,
                       "check commit editor for a locked file"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
