/* lock-test.c --- tests for the filesystem locking functions
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

#include <string.h>
#include <apr_pools.h>
#include <apr_time.h>

#include "../svn_test.h"

#include "svn_error.h"
#include "svn_fs.h"
#include "svn_hash.h"

#include "../svn_test_fs.h"


/*-----------------------------------------------------------------*/

/** Helper functions **/

/* Implementations of the svn_fs_get_locks_callback_t interface and
   baton, for verifying expected output from svn_fs_get_locks(). */

struct get_locks_baton_t
{
  apr_hash_t *locks;
};

static svn_error_t *
get_locks_callback(void *baton,
                   svn_lock_t *lock,
                   apr_pool_t *pool)
{
  struct get_locks_baton_t *b = baton;
  apr_pool_t *hash_pool = apr_hash_pool_get(b->locks);
  svn_string_t *lock_path = svn_string_create(lock->path, hash_pool);

  if (!apr_hash_get(b->locks, lock_path->data, lock_path->len))
    {
      apr_hash_set(b->locks, lock_path->data, lock_path->len,
                   svn_lock_dup(lock, hash_pool));
      return SVN_NO_ERROR;
    }
  else
    {
      return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                               "Lock for path '%s' is being reported twice.",
                               lock->path);
    }
}

/* A factory function. */

static struct get_locks_baton_t *
make_get_locks_baton(apr_pool_t *pool)
{
  struct get_locks_baton_t *baton = apr_pcalloc(pool, sizeof(*baton));
  baton->locks = apr_hash_make(pool);
  return baton;
}


/* And verification function(s). */

static svn_error_t *
verify_matching_lock_paths(struct get_locks_baton_t *baton,
                           const char *expected_paths[],
                           apr_size_t num_expected_paths,
                           apr_pool_t *pool)
{
  apr_size_t i;
  if (num_expected_paths != apr_hash_count(baton->locks))
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Unexpected number of locks.");
  for (i = 0; i < num_expected_paths; i++)
    {
      const char *path = expected_paths[i];
      if (! apr_hash_get(baton->locks, path, APR_HASH_KEY_STRING))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "Missing lock for path '%s'", path);
    }
  return SVN_NO_ERROR;
}


/* Create a filesystem in a directory called NAME, and populate it with
 * the standard Greek tree.  Set *FS_P to the new filesystem object and
 * *NEWREV_P to the head revision number.  Unwanted outputs may be NULL. */
static svn_error_t *
create_greek_fs(svn_fs_t **fs_p,
                svn_revnum_t *newrev_p,
                const char *name,
                const svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t newrev;

  /* Prepare a filesystem and a new txn. */
  SVN_ERR(svn_test__create_fs(&fs, name, opts, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree and commit it. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(newrev));

  if (fs_p)
    *fs_p = fs;
  if (newrev_p)
    *newrev_p = newrev;
  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------*/

/** The actual lock-tests called by `make check` **/



/* Test that we can create a lock--nothing more.  */
static svn_error_t *
lock_only(const svn_test_opts_t *opts,
          apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_access_t *access;
  svn_lock_t *mylock;

  SVN_ERR(create_greek_fs(&fs, NULL, "test-repo-lock-only",
                          opts, pool));

  /* We are now 'bubba'. */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  /* Lock /A/D/G/rho. */
  SVN_ERR(svn_fs_lock(&mylock, fs, "/A/D/G/rho", NULL, "", 0, 0,
                      SVN_INVALID_REVNUM, FALSE, pool));

  return SVN_NO_ERROR;
}





/* Test that we can create, fetch, and destroy a lock.  It exercises
   each of the five public fs locking functions.  */
static svn_error_t *
lookup_lock_by_path(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_access_t *access;
  svn_lock_t *mylock, *somelock;

  SVN_ERR(create_greek_fs(&fs, NULL, "test-repo-lookup-lock-by-path",
                          opts, pool));

  /* We are now 'bubba'. */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  /* Lock /A/D/G/rho. */
  SVN_ERR(svn_fs_lock(&mylock, fs, "/A/D/G/rho", NULL, "", 0, 0,
                      SVN_INVALID_REVNUM, FALSE, pool));

  /* Can we look up the lock by path? */
  SVN_ERR(svn_fs_get_lock(&somelock, fs, "/A/D/G/rho", pool));
  if ((! somelock) || (strcmp(somelock->token, mylock->token) != 0))
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Couldn't look up a lock by pathname.");

  return SVN_NO_ERROR;
}

/* Test that we can create a lock outside of the fs and attach it to a
   path.  */
static svn_error_t *
attach_lock(const svn_test_opts_t *opts,
            apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_access_t *access;
  svn_lock_t *somelock;
  svn_lock_t *mylock;
  const char *token;

  SVN_ERR(create_greek_fs(&fs, NULL, "test-repo-attach-lock",
                          opts, pool));

  /* We are now 'bubba'. */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  SVN_ERR(svn_fs_generate_lock_token(&token, fs, pool));
  SVN_ERR(svn_fs_lock(&mylock, fs, "/A/D/G/rho", token,
                      "This is a comment.  Yay comment!", 0,
                      apr_time_now() + apr_time_from_sec(3),
                      SVN_INVALID_REVNUM, FALSE, pool));

  /* Can we look up the lock by path? */
  SVN_ERR(svn_fs_get_lock(&somelock, fs, "/A/D/G/rho", pool));
  if ((! somelock) || (strcmp(somelock->token, mylock->token) != 0))
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Couldn't look up a lock by pathname.");

  /* Unlock /A/D/G/rho, and verify that it's gone. */
  SVN_ERR(svn_fs_unlock(fs, mylock->path, mylock->token, 0, pool));
  SVN_ERR(svn_fs_get_lock(&somelock, fs, "/A/D/G/rho", pool));
  if (somelock)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Removed a lock, but it's still there.");

  return SVN_NO_ERROR;
}


/* Test that we can get all locks under a directory. */
static svn_error_t *
get_locks(const svn_test_opts_t *opts,
          apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_access_t *access;
  svn_lock_t *mylock;
  struct get_locks_baton_t *get_locks_baton;
  apr_size_t i, num_expected_paths;

  SVN_ERR(create_greek_fs(&fs, NULL, "test-repo-get-locks",
                          opts, pool));

  /* We are now 'bubba'. */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  /* Lock our paths; verify from "/". */
  {
    static const char *expected_paths[] = {
      "/A/D/G/pi",
      "/A/D/G/rho",
      "/A/D/G/tau",
      "/A/D/H/psi",
      "/A/D/H/chi",
      "/A/D/H/omega",
      "/A/B/E/alpha",
      "/A/B/E/beta",
    };
    num_expected_paths = sizeof(expected_paths) / sizeof(const char *);
    for (i = 0; i < num_expected_paths; i++)
      {
        SVN_ERR(svn_fs_lock(&mylock, fs, expected_paths[i], NULL, "", 0, 0,
                            SVN_INVALID_REVNUM, FALSE, pool));
      }
    get_locks_baton = make_get_locks_baton(pool);
    SVN_ERR(svn_fs_get_locks(fs, "", get_locks_callback,
                             get_locks_baton, pool));
    SVN_ERR(verify_matching_lock_paths(get_locks_baton, expected_paths,
                                       num_expected_paths, pool));
  }

  /* Verify from "/A/B". */
  {
    static const char *expected_paths[] = {
      "/A/B/E/alpha",
      "/A/B/E/beta",
    };
    num_expected_paths = sizeof(expected_paths) / sizeof(const char *);
    get_locks_baton = make_get_locks_baton(pool);
    SVN_ERR(svn_fs_get_locks(fs, "A/B", get_locks_callback,
                             get_locks_baton, pool));
    SVN_ERR(verify_matching_lock_paths(get_locks_baton, expected_paths,
                                       num_expected_paths, pool));
  }

  /* Verify from "/A/D". */
  {
    static const char *expected_paths[] = {
      "/A/D/G/pi",
      "/A/D/G/rho",
      "/A/D/G/tau",
      "/A/D/H/psi",
      "/A/D/H/chi",
      "/A/D/H/omega",
    };
    num_expected_paths = sizeof(expected_paths) / sizeof(const char *);
    get_locks_baton = make_get_locks_baton(pool);
    SVN_ERR(svn_fs_get_locks(fs, "A/D", get_locks_callback,
                             get_locks_baton, pool));
    SVN_ERR(verify_matching_lock_paths(get_locks_baton, expected_paths,
                                       num_expected_paths, pool));
  }

  /* Verify from "/A/D/G". */
  {
    static const char *expected_paths[] = {
      "/A/D/G/pi",
      "/A/D/G/rho",
      "/A/D/G/tau",
    };
    num_expected_paths = sizeof(expected_paths) / sizeof(const char *);
    get_locks_baton = make_get_locks_baton(pool);
    SVN_ERR(svn_fs_get_locks(fs, "A/D/G", get_locks_callback,
                             get_locks_baton, pool));
    SVN_ERR(verify_matching_lock_paths(get_locks_baton, expected_paths,
                                       num_expected_paths, pool));
  }

  /* Verify from "/A/D/H/omega". */
  {
    static const char *expected_paths[] = {
      "/A/D/H/omega",
    };
    num_expected_paths = sizeof(expected_paths) / sizeof(const char *);
    get_locks_baton = make_get_locks_baton(pool);
    SVN_ERR(svn_fs_get_locks(fs, "A/D/H/omega", get_locks_callback,
                             get_locks_baton, pool));
    SVN_ERR(verify_matching_lock_paths(get_locks_baton, expected_paths,
                                       num_expected_paths, pool));
  }

  /* Verify from "/iota" (which wasn't locked... tricky...). */
  {
    static const char *expected_paths[] = { 0 };
    num_expected_paths = 0;
    get_locks_baton = make_get_locks_baton(pool);
    SVN_ERR(svn_fs_get_locks(fs, "iota", get_locks_callback,
                             get_locks_baton, pool));
    SVN_ERR(verify_matching_lock_paths(get_locks_baton, expected_paths,
                                       num_expected_paths, pool));
  }

  /* A path that is longer and alphabetically earlier than some locked
     paths, this exercises the r1205848 BDB lock code. */
  {
    static const char *expected_paths[] = { 0 };
    num_expected_paths = 0;
    get_locks_baton = make_get_locks_baton(pool);
    SVN_ERR(svn_fs_get_locks(fs, "A/D/H/ABCDEFGHIJKLMNOPQR", get_locks_callback,
                             get_locks_baton, pool));
    SVN_ERR(verify_matching_lock_paths(get_locks_baton, expected_paths,
                                       num_expected_paths, pool));
  }

  return SVN_NO_ERROR;
}


/* Test that we can create, fetch, and destroy a lock.  It exercises
   each of the five public fs locking functions.  */
static svn_error_t *
basic_lock(const svn_test_opts_t *opts,
           apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_access_t *access;
  svn_lock_t *mylock, *somelock;

  SVN_ERR(create_greek_fs(&fs, NULL, "test-repo-basic-lock",
                          opts, pool));

  /* We are now 'bubba'. */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  /* Lock /A/D/G/rho. */
  SVN_ERR(svn_fs_lock(&mylock, fs, "/A/D/G/rho", NULL, "", 0, 0,
                      SVN_INVALID_REVNUM, FALSE, pool));

  /* Can we look up the lock by path? */
  SVN_ERR(svn_fs_get_lock(&somelock, fs, "/A/D/G/rho", pool));
  if ((! somelock) || (strcmp(somelock->token, mylock->token) != 0))
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Couldn't look up a lock by pathname.");

  /* Unlock /A/D/G/rho, and verify that it's gone. */
  SVN_ERR(svn_fs_unlock(fs, mylock->path, mylock->token, 0, pool));
  SVN_ERR(svn_fs_get_lock(&somelock, fs, "/A/D/G/rho", pool));
  if (somelock)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Removed a lock, but it's still there.");

  return SVN_NO_ERROR;
}



/* Test that locks are enforced -- specifically that both a username
   and token are required to make use of the lock.  */
static svn_error_t *
lock_credentials(const svn_test_opts_t *opts,
                 apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_lock_t *mylock;
  svn_error_t *err;

  SVN_ERR(create_greek_fs(&fs, &newrev, "test-repo-lock-credentials",
                          opts, pool));

  /* We are now 'bubba'. */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  /* Lock /A/D/G/rho. */
  SVN_ERR(svn_fs_lock(&mylock, fs, "/A/D/G/rho", NULL, "", 0, 0,
                      SVN_INVALID_REVNUM, FALSE, pool));

  /* Push the proper lock-token into the fs access context. */
  SVN_ERR(svn_fs_access_add_lock_token(access, mylock->token));

  /* Make a new transaction and change rho. */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, newrev, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "/A/D/G/rho",
                                      "new contents", pool));

  /* We are no longer 'bubba'.  We're nobody. */
  SVN_ERR(svn_fs_set_access(fs, NULL));

  /* Try to commit the file change.  Should fail, because we're nobody. */
  err = svn_fs_commit_txn(&conflict, &newrev, txn, pool);
  SVN_TEST_ASSERT(! SVN_IS_VALID_REVNUM(newrev));
  if (! err)
    return svn_error_create
      (SVN_ERR_TEST_FAILED, NULL,
       "Uhoh, able to commit locked file without any fs username.");
  svn_error_clear(err);

  /* We are now 'hortense'. */
  SVN_ERR(svn_fs_create_access(&access, "hortense", pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  /* Try to commit the file change.  Should fail, because we're 'hortense'. */
  err = svn_fs_commit_txn(&conflict, &newrev, txn, pool);
  SVN_TEST_ASSERT(! SVN_IS_VALID_REVNUM(newrev));
  if (! err)
    return svn_error_create
      (SVN_ERR_TEST_FAILED, NULL,
       "Uhoh, able to commit locked file as non-owner.");
  svn_error_clear(err);

  /* Be 'bubba' again. */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  /* Try to commit the file change.  Should fail, because there's no token. */
  err = svn_fs_commit_txn(&conflict, &newrev, txn, pool);
  SVN_TEST_ASSERT(! SVN_IS_VALID_REVNUM(newrev));
  if (! err)
    return svn_error_create
      (SVN_ERR_TEST_FAILED, NULL,
       "Uhoh, able to commit locked file with no lock token.");
  svn_error_clear(err);

  /* Push the proper lock-token into the fs access context. */
  SVN_ERR(svn_fs_access_add_lock_token(access, mylock->token));

  /* Commit should now succeed. */
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(newrev));

  return SVN_NO_ERROR;
}



/* Test that locks are enforced at commit time.  Somebody might lock
   something behind your back, right before you run
   svn_fs_commit_txn().  Also, this test verifies that recursive
   lock-checks on directories is working properly. */
static svn_error_t *
final_lock_check(const svn_test_opts_t *opts,
                 apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_lock_t *mylock;
  svn_error_t *err;

  SVN_ERR(create_greek_fs(&fs, &newrev, "test-repo-final-lock-check",
                          opts, pool));

  /* Make a new transaction and delete "/A" */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, newrev, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_delete(txn_root, "/A", pool));

  /* Become 'bubba' and lock "/A/D/G/rho". */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));
  SVN_ERR(svn_fs_lock(&mylock, fs, "/A/D/G/rho", NULL, "", 0, 0,
                      SVN_INVALID_REVNUM, FALSE, pool));

  /* We are no longer 'bubba'.  We're nobody. */
  SVN_ERR(svn_fs_set_access(fs, NULL));

  /* Try to commit the transaction.  Should fail, because a child of
     the deleted directory is locked by someone else. */
  err = svn_fs_commit_txn(&conflict, &newrev, txn, pool);
  SVN_TEST_ASSERT(! SVN_IS_VALID_REVNUM(newrev));
  if (! err)
    return svn_error_create
      (SVN_ERR_TEST_FAILED, NULL,
       "Uhoh, able to commit dir deletion when a child is locked.");
  svn_error_clear(err);

  /* Supply correct username and token;  commit should work. */
  SVN_ERR(svn_fs_set_access(fs, access));
  SVN_ERR(svn_fs_access_add_lock_token(access, mylock->token));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(newrev));

  return SVN_NO_ERROR;
}



/* If a directory's child is locked by someone else, we should still
   be able to commit a propchange on the directory. */
static svn_error_t *
lock_dir_propchange(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_lock_t *mylock;

  SVN_ERR(create_greek_fs(&fs, &newrev, "test-repo-lock-dir-propchange",
                          opts, pool));

  /* Become 'bubba' and lock "/A/D/G/rho". */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));
  SVN_ERR(svn_fs_lock(&mylock, fs, "/A/D/G/rho", NULL, "", 0, 0,
                      SVN_INVALID_REVNUM, FALSE, pool));

  /* We are no longer 'bubba'.  We're nobody. */
  SVN_ERR(svn_fs_set_access(fs, NULL));

  /* Make a new transaction and make a propchange on "/A" */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, newrev, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_change_node_prop(txn_root, "/A",
                                  "foo", svn_string_create("bar", pool),
                                  pool));

  /* Commit should succeed;  this means we're doing a non-recursive
     lock-check on directory, rather than a recursive one.  */
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(newrev));

  return SVN_NO_ERROR;
}

/* Test that locks auto-expire correctly. */
static svn_error_t *
lock_expiration(const svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_lock_t *mylock;
  svn_error_t *err;
  struct get_locks_baton_t *get_locks_baton;

  SVN_ERR(create_greek_fs(&fs, &newrev, "test-repo-lock-expiration",
                          opts, pool));

  /* Make a new transaction and change rho. */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, newrev, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "/A/D/G/rho",
                                      "new contents", pool));

  /* We are now 'bubba'. */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  /* Lock /A/D/G/rho, with an expiration 2 seconds from now. */
  SVN_ERR(svn_fs_lock(&mylock, fs, "/A/D/G/rho", NULL, "", 0,
                      apr_time_now() + apr_time_from_sec(2),
                      SVN_INVALID_REVNUM, FALSE, pool));

  /* Become nobody. */
  SVN_ERR(svn_fs_set_access(fs, NULL));

  /* Try to commit.  Should fail because we're 'nobody', and the lock
     hasn't expired yet. */
  err = svn_fs_commit_txn(&conflict, &newrev, txn, pool);
  SVN_TEST_ASSERT(! SVN_IS_VALID_REVNUM(newrev));
  if (! err)
    return svn_error_create
      (SVN_ERR_TEST_FAILED, NULL,
       "Uhoh, able to commit a file that has a non-expired lock.");
  svn_error_clear(err);

  /* Check that the lock is there, by getting it via the paths parent. */
  {
    static const char *expected_paths [] = {
      "/A/D/G/rho"
    };
    apr_size_t num_expected_paths = (sizeof(expected_paths)
                                     / sizeof(expected_paths[0]));
    get_locks_baton = make_get_locks_baton(pool);
    SVN_ERR(svn_fs_get_locks(fs, "/A/D/G", get_locks_callback,
                             get_locks_baton, pool));
    SVN_ERR(verify_matching_lock_paths(get_locks_baton, expected_paths,
                                       num_expected_paths, pool));
  }

  /* Sleep 2 seconds, so the lock auto-expires.  Anonymous commit
     should then succeed. */
  apr_sleep(apr_time_from_sec(3));

  /* Verify that the lock auto-expired even in the recursive case. */
  {
    static const char *expected_paths [] = { 0 };
    apr_size_t num_expected_paths = 0;
    get_locks_baton = make_get_locks_baton(pool);
    SVN_ERR(svn_fs_get_locks(fs, "/A/D/G", get_locks_callback,
                             get_locks_baton, pool));
    SVN_ERR(verify_matching_lock_paths(get_locks_baton, expected_paths,
                                       num_expected_paths, pool));
  }

  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(newrev));

  return SVN_NO_ERROR;
}

/* Test that a lock can be broken, stolen, or refreshed */
static svn_error_t *
lock_break_steal_refresh(const svn_test_opts_t *opts,
                         apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_access_t *access;
  svn_lock_t *mylock, *somelock;

  SVN_ERR(create_greek_fs(&fs, NULL, "test-repo-steal-refresh",
                          opts, pool));

  /* Become 'bubba' and lock "/A/D/G/rho". */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));
  SVN_ERR(svn_fs_lock(&mylock, fs, "/A/D/G/rho", NULL, "", 0, 0,
                      SVN_INVALID_REVNUM, FALSE, pool));

  /* Become 'hortense' and break bubba's lock, then verify it's gone. */
  SVN_ERR(svn_fs_create_access(&access, "hortense", pool));
  SVN_ERR(svn_fs_set_access(fs, access));
  SVN_ERR(svn_fs_unlock(fs, mylock->path, mylock->token,
                        1 /* FORCE BREAK */, pool));
  SVN_ERR(svn_fs_get_lock(&somelock, fs, "/A/D/G/rho", pool));
  if (somelock)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Tried to break a lock, but it's still there.");

  /* As hortense, create a new lock, and verify that we own it. */
  SVN_ERR(svn_fs_lock(&mylock, fs, "/A/D/G/rho", NULL, "", 0, 0,
                      SVN_INVALID_REVNUM, FALSE, pool));
  SVN_ERR(svn_fs_get_lock(&somelock, fs, "/A/D/G/rho", pool));
  if (strcmp(somelock->owner, mylock->owner) != 0)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Made a lock, but we don't seem to own it.");

  /* As bubba, steal hortense's lock, creating a new one that expires. */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));
  SVN_ERR(svn_fs_lock(&mylock, fs, "/A/D/G/rho", NULL, "", 0,
                      apr_time_now() + apr_time_from_sec(300), /* 5 min. */
                      SVN_INVALID_REVNUM,
                      TRUE /* FORCE STEAL */,
                      pool));
  SVN_ERR(svn_fs_get_lock(&somelock, fs, "/A/D/G/rho", pool));
  if (strcmp(somelock->owner, mylock->owner) != 0)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Made a lock, but we don't seem to own it.");
  if (! somelock->expiration_date)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Made expiring lock, but seems not to expire.");

  /* Refresh the lock, so that it never expires. */
  SVN_ERR(svn_fs_lock(&somelock, fs, somelock->path, somelock->token,
                      somelock->comment, 0, 0,
                      SVN_INVALID_REVNUM,
                      TRUE /* FORCE STEAL */,
                      pool));
  SVN_ERR(svn_fs_get_lock(&somelock, fs, "/A/D/G/rho", pool));
  if (somelock->expiration_date)
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "Made non-expirirng lock, but it expires.");

  return SVN_NO_ERROR;
}


/* Test that svn_fs_lock() and svn_fs_attach_lock() can do
   out-of-dateness checks..  */
static svn_error_t *
lock_out_of_date(const svn_test_opts_t *opts,
                 apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_lock_t *mylock;
  svn_error_t *err;

  SVN_ERR(create_greek_fs(&fs, &newrev, "test-repo-lock-out-of-date",
                          opts, pool));

  /* Commit a small change to /A/D/G/rho, creating revision 2. */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, newrev, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "/A/D/G/rho",
                                      "new contents", pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(newrev));

  /* We are now 'bubba'. */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  /* Try to lock /A/D/G/rho, but claim that we still have r1 of the file. */
  err = svn_fs_lock(&mylock, fs, "/A/D/G/rho", NULL, "", 0, 0, 1, FALSE, pool);
  if (! err)
    return svn_error_create
      (SVN_ERR_TEST_FAILED, NULL,
       "Uhoh, able to lock an out-of-date file.");
  svn_error_clear(err);

  /* Attempt lock again, this time claiming to have r2. */
  SVN_ERR(svn_fs_lock(&mylock, fs, "/A/D/G/rho", NULL, "", 0,
                      0, 2, FALSE, pool));

  /* 'Refresh' the lock, claiming to have r1... should fail. */
  err = svn_fs_lock(&mylock, fs, mylock->path,
                    mylock->token, mylock->comment, 0,
                    apr_time_now() + apr_time_from_sec(50),
                    1,
                    TRUE /* FORCE STEAL */,
                    pool);
  if (! err)
    return svn_error_create
      (SVN_ERR_TEST_FAILED, NULL,
       "Uhoh, able to refresh a lock on an out-of-date file.");
  svn_error_clear(err);

  return SVN_NO_ERROR;
}

struct lock_result_t {
  const svn_lock_t *lock;
  svn_error_t *fs_err;
};

static svn_error_t *
expect_lock(const char *path,
            apr_hash_t *results,
            svn_fs_t *fs,
            apr_pool_t *scratch_pool)
{
  svn_lock_t *lock;
  struct lock_result_t *result = svn_hash_gets(results, path);

  SVN_TEST_ASSERT(result && result->lock && !result->fs_err);
  SVN_ERR(svn_fs_get_lock(&lock, fs, path, scratch_pool));
  SVN_TEST_ASSERT(lock);
  return SVN_NO_ERROR;
}

static svn_error_t *
expect_error(const char *path,
             apr_hash_t *results,
             svn_fs_t *fs,
             apr_pool_t *scratch_pool)
{
  svn_lock_t *lock;
  struct lock_result_t *result = svn_hash_gets(results, path);

  SVN_TEST_ASSERT(result && !result->lock && result->fs_err);
  svn_error_clear(result->fs_err);
  SVN_ERR(svn_fs_get_lock(&lock, fs, path, scratch_pool));
  SVN_TEST_ASSERT(!lock);
  return SVN_NO_ERROR;
}

static svn_error_t *
expect_unlock(const char *path,
              apr_hash_t *results,
              svn_fs_t *fs,
              apr_pool_t *scratch_pool)
{
  svn_lock_t *lock;
  struct lock_result_t *result = svn_hash_gets(results, path);

  SVN_TEST_ASSERT(result && !result->fs_err);
  SVN_ERR(svn_fs_get_lock(&lock, fs, path, scratch_pool));
  SVN_TEST_ASSERT(!lock);
  return SVN_NO_ERROR;
}

static svn_error_t *
expect_unlock_error(const char *path,
                    apr_hash_t *results,
                    svn_fs_t *fs,
                    apr_pool_t *scratch_pool)
{
  svn_lock_t *lock;
  struct lock_result_t *result = svn_hash_gets(results, path);

  SVN_TEST_ASSERT(result && result->fs_err);
  svn_error_clear(result->fs_err);
  SVN_ERR(svn_fs_get_lock(&lock, fs, path, scratch_pool));
  SVN_TEST_ASSERT(lock);
  return SVN_NO_ERROR;
}

struct lock_many_baton_t {
  apr_hash_t *results;
  apr_pool_t *pool;
  int count;
};

/* Implements svn_fs_lock_callback_t. */
static svn_error_t *
lock_many_cb(void *lock_baton,
             const char *path,
             const svn_lock_t *lock,
             svn_error_t *fs_err,
             apr_pool_t *pool)
{
  struct lock_many_baton_t *b = lock_baton;
  struct lock_result_t *result = apr_palloc(b->pool,
                                            sizeof(struct lock_result_t));

  result->lock = lock;
  result->fs_err = svn_error_dup(fs_err);
  svn_hash_sets(b->results, apr_pstrdup(b->pool, path), result);

  if (b->count)
    if (!--(b->count))
      return svn_error_create(SVN_ERR_FS_GENERAL, NULL, "lock_many_cb");

  return SVN_NO_ERROR;
}

static svn_error_t *
lock_multiple_paths(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *root, *txn_root;
  const char *conflict;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_fs_lock_target_t *target;
  struct lock_many_baton_t baton;
  apr_hash_t *lock_paths, *unlock_paths;
  apr_hash_index_t *hi;

  SVN_ERR(create_greek_fs(&fs, &newrev, "test-lock-multiple-paths",
                          opts, pool));
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));
  SVN_ERR(svn_fs_revision_root(&root, fs, newrev, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, newrev, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_fs_make_dir(txn_root, "/A/BB", pool));
  SVN_ERR(svn_fs_make_dir(txn_root, "/A/BBB", pool));
  SVN_ERR(svn_fs_copy(root, "/A/mu", txn_root, "/A/BB/mu", pool));
  SVN_ERR(svn_fs_copy(root, "/A/mu", txn_root, "/A/BBB/mu", pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

  baton.results = apr_hash_make(pool);
  baton.pool = pool;
  baton.count = 0;
  lock_paths = apr_hash_make(pool);
  unlock_paths = apr_hash_make(pool);
  target = svn_fs_lock_target_create(NULL, newrev, pool);

  svn_hash_sets(lock_paths, "/A/B/E/alpha", target);
  svn_hash_sets(lock_paths, "/A/B/E/beta", target);
  svn_hash_sets(lock_paths, "/A/B/E/zulu", target);
  svn_hash_sets(lock_paths, "/A/BB/mu", target);
  svn_hash_sets(lock_paths, "/A/BBB/mu", target);
  svn_hash_sets(lock_paths, "/A/D/G/pi", target);
  svn_hash_sets(lock_paths, "/A/D/G/rho", target);
  svn_hash_sets(lock_paths, "/A/mu", target);
  svn_hash_sets(lock_paths, "/X/zulu", target);

  /* Lock some paths. */
  apr_hash_clear(baton.results);
  SVN_ERR(svn_fs_lock_many(fs, lock_paths, "comment", 0, 0, 0,
                           lock_many_cb, &baton,
                           pool, pool));

  SVN_ERR(expect_lock("/A/B/E/alpha", baton.results, fs, pool));
  SVN_ERR(expect_lock("/A/B/E/beta", baton.results, fs, pool));
  SVN_ERR(expect_error("/A/B/E/zulu", baton.results, fs, pool));
  SVN_ERR(expect_lock("/A/BB/mu", baton.results, fs, pool));
  SVN_ERR(expect_lock("/A/BBB/mu", baton.results, fs, pool));
  SVN_ERR(expect_lock("/A/D/G/pi", baton.results, fs, pool));
  SVN_ERR(expect_lock("/A/D/G/rho", baton.results, fs, pool));
  SVN_ERR(expect_lock("/A/mu", baton.results, fs, pool));
  SVN_ERR(expect_error("/X/zulu", baton.results, fs, pool));

  /* Unlock without force and wrong tokens. */
  for (hi = apr_hash_first(pool, lock_paths); hi; hi = apr_hash_next(hi))
    svn_hash_sets(unlock_paths, apr_hash_this_key(hi), "wrong-token");
  apr_hash_clear(baton.results);
  SVN_ERR(svn_fs_unlock_many(fs, unlock_paths, FALSE, lock_many_cb, &baton,
                             pool, pool));

  SVN_ERR(expect_unlock_error("/A/B/E/alpha", baton.results, fs, pool));
  SVN_ERR(expect_unlock_error("/A/B/E/beta", baton.results, fs, pool));
  SVN_ERR(expect_error("/A/B/E/zulu", baton.results, fs, pool));
  SVN_ERR(expect_unlock_error("/A/BB/mu", baton.results, fs, pool));
  SVN_ERR(expect_unlock_error("/A/BBB/mu", baton.results, fs, pool));
  SVN_ERR(expect_unlock_error("/A/D/G/pi", baton.results, fs, pool));
  SVN_ERR(expect_unlock_error("/A/D/G/rho", baton.results, fs, pool));
  SVN_ERR(expect_unlock_error("/A/mu", baton.results, fs, pool));
  SVN_ERR(expect_error("/X/zulu", baton.results, fs, pool));

  /* Force unlock. */
  for (hi = apr_hash_first(pool, lock_paths); hi; hi = apr_hash_next(hi))
    svn_hash_sets(unlock_paths, apr_hash_this_key(hi), "");
  apr_hash_clear(baton.results);
  SVN_ERR(svn_fs_unlock_many(fs, unlock_paths, TRUE, lock_many_cb, &baton,
                             pool, pool));

  SVN_ERR(expect_unlock("/A/B/E/alpha", baton.results, fs, pool));
  SVN_ERR(expect_unlock("/A/B/E/beta", baton.results, fs, pool));
  SVN_ERR(expect_error("/A/B/E/zulu", baton.results, fs, pool));
  SVN_ERR(expect_unlock("/A/BB/mu", baton.results, fs, pool));
  SVN_ERR(expect_unlock("/A/BBB/mu", baton.results, fs, pool));
  SVN_ERR(expect_unlock("/A/D/G/pi", baton.results, fs, pool));
  SVN_ERR(expect_unlock("/A/D/G/rho", baton.results, fs, pool));
  SVN_ERR(expect_unlock("/A/mu", baton.results, fs, pool));
  SVN_ERR(expect_error("/X/zulu", baton.results, fs, pool));

  /* Lock again. */
  apr_hash_clear(baton.results);
  SVN_ERR(svn_fs_lock_many(fs, lock_paths, "comment", 0, 0, 0,
                           lock_many_cb, &baton,
                           pool, pool));

  SVN_ERR(expect_lock("/A/B/E/alpha", baton.results, fs, pool));
  SVN_ERR(expect_lock("/A/B/E/beta", baton.results, fs, pool));
  SVN_ERR(expect_error("/A/B/E/zulu", baton.results, fs, pool));
  SVN_ERR(expect_lock("/A/BB/mu", baton.results, fs, pool));
  SVN_ERR(expect_lock("/A/BBB/mu", baton.results, fs, pool));
  SVN_ERR(expect_lock("/A/D/G/pi", baton.results, fs, pool));
  SVN_ERR(expect_lock("/A/D/G/rho", baton.results, fs, pool));
  SVN_ERR(expect_lock("/A/mu", baton.results, fs, pool));
  SVN_ERR(expect_error("/X/zulu", baton.results, fs, pool));

  /* Unlock without force. */
  for (hi = apr_hash_first(pool, baton.results); hi; hi = apr_hash_next(hi))
    {
      struct lock_result_t *result = apr_hash_this_val(hi);
      svn_hash_sets(unlock_paths, apr_hash_this_key(hi),
                    result->lock ? result->lock->token : "non-existent-token");
    }
  apr_hash_clear(baton.results);
  SVN_ERR(svn_fs_unlock_many(fs, unlock_paths, FALSE, lock_many_cb, &baton,
                             pool, pool));

  SVN_ERR(expect_unlock("/A/B/E/alpha", baton.results, fs, pool));
  SVN_ERR(expect_unlock("/A/B/E/beta", baton.results, fs, pool));
  SVN_ERR(expect_error("/A/B/E/zulu", baton.results, fs, pool));
  SVN_ERR(expect_unlock("/A/BB/mu", baton.results, fs, pool));
  SVN_ERR(expect_unlock("/A/BBB/mu", baton.results, fs, pool));
  SVN_ERR(expect_unlock("/A/D/G/pi", baton.results, fs, pool));
  SVN_ERR(expect_unlock("/A/D/G/rho", baton.results, fs, pool));
  SVN_ERR(expect_unlock("/A/mu", baton.results, fs, pool));
  SVN_ERR(expect_error("/X/zulu", baton.results, fs, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
lock_cb_error(const svn_test_opts_t *opts,
              apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_fs_lock_target_t *target;
  struct lock_many_baton_t baton;
  apr_hash_t *lock_paths, *unlock_paths;
  svn_lock_t *lock;

  SVN_ERR(create_greek_fs(&fs, &newrev, "test-lock-cb-error", opts, pool));
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  baton.results = apr_hash_make(pool);
  baton.pool = pool;
  baton.count = 1;
  lock_paths = apr_hash_make(pool);
  unlock_paths = apr_hash_make(pool);
  target = svn_fs_lock_target_create(NULL, newrev, pool);

  svn_hash_sets(lock_paths, "/A/B/E/alpha", target);
  svn_hash_sets(lock_paths, "/A/B/E/beta", target);

  apr_hash_clear(baton.results);
  SVN_TEST_ASSERT_ERROR(svn_fs_lock_many(fs, lock_paths, "comment", 0, 0, 0,
                                         lock_many_cb, &baton,
                                         pool, pool),
                        SVN_ERR_FS_GENERAL);

  SVN_TEST_ASSERT(apr_hash_count(baton.results) == 1);
  SVN_TEST_ASSERT(svn_hash_gets(baton.results, "/A/B/E/alpha")
                  || svn_hash_gets(baton.results, "/A/B/E/beta"));
  SVN_ERR(svn_fs_get_lock(&lock, fs, "/A/B/E/alpha", pool));
  SVN_TEST_ASSERT(lock);
  svn_hash_sets(unlock_paths, "/A/B/E/alpha", lock->token);
  SVN_ERR(svn_fs_get_lock(&lock, fs, "/A/B/E/beta", pool));
  SVN_TEST_ASSERT(lock);
  svn_hash_sets(unlock_paths, "/A/B/E/beta", lock->token);

  baton.count = 1;
  apr_hash_clear(baton.results);
  SVN_TEST_ASSERT_ERROR(svn_fs_unlock_many(fs, unlock_paths, FALSE,
                                           lock_many_cb, &baton,
                                           pool, pool),
                        SVN_ERR_FS_GENERAL);

  SVN_TEST_ASSERT(apr_hash_count(baton.results) == 1);
  SVN_TEST_ASSERT(svn_hash_gets(baton.results, "/A/B/E/alpha")
                  || svn_hash_gets(baton.results, "/A/B/E/beta"));

  SVN_ERR(svn_fs_get_lock(&lock, fs, "/A/B/E/alpha", pool));
  SVN_TEST_ASSERT(!lock);
  SVN_ERR(svn_fs_get_lock(&lock, fs, "/A/B/E/beta", pool));
  SVN_TEST_ASSERT(!lock);

  return SVN_NO_ERROR;
}

static svn_error_t *
obtain_write_lock_failure(const svn_test_opts_t *opts,
                          apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_fs_lock_target_t *target;
  struct lock_many_baton_t baton;
  apr_hash_t *lock_paths, *unlock_paths;

  /* The test makes sense only for FSFS. */
  if (strcmp(opts->fs_type, SVN_FS_TYPE_FSFS) != 0
      && strcmp(opts->fs_type, SVN_FS_TYPE_FSX) != 0)
    return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                            "this will test FSFS/FSX repositories only");

  SVN_ERR(create_greek_fs(&fs, &newrev, "test-obtain-write-lock-failure",
                          opts, pool));
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  /* Make a read only 'write-lock' file.  This prevents any write operations
     from being executed. */
  SVN_ERR(svn_io_set_file_read_only("test-obtain-write-lock-failure/write-lock",
                                    FALSE, pool));

  baton.results = apr_hash_make(pool);
  baton.pool = pool;
  baton.count = 0;

  /* Trying to lock some paths.  We don't really care about error; the test
     shouldn't crash. */
  target = svn_fs_lock_target_create(NULL, newrev, pool);
  lock_paths = apr_hash_make(pool);
  svn_hash_sets(lock_paths, "/iota", target);
  svn_hash_sets(lock_paths, "/A/mu", target);

  apr_hash_clear(baton.results);
  SVN_TEST_ASSERT_ANY_ERROR(svn_fs_lock_many(fs, lock_paths, "comment", 0, 0, 0,
                                             lock_many_cb, &baton, pool, pool));

  /* Trying to unlock some paths.  We don't really care about error; the test
     shouldn't crash. */
  unlock_paths = apr_hash_make(pool);
  svn_hash_sets(unlock_paths, "/iota", "");
  svn_hash_sets(unlock_paths, "/A/mu", "");

  apr_hash_clear(baton.results);
  SVN_TEST_ASSERT_ANY_ERROR(svn_fs_unlock_many(fs, unlock_paths, TRUE,
                                               lock_many_cb, &baton, pool,
                                               pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
parent_and_child_lock(const svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_access_t *access;
  svn_fs_txn_t *txn;
  svn_fs_root_t *root;
  const char *conflict;
  svn_revnum_t newrev;
  svn_lock_t *lock;
  struct get_locks_baton_t *get_locks_baton;
  apr_size_t num_expected_paths;

  SVN_ERR(svn_test__create_fs(&fs, "test-parent-and-child-lock", opts, pool));
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  /* Make a file '/A'. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_make_file(root, "/A", pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

  /* Obtain a lock on '/A'. */
  SVN_ERR(svn_fs_lock(&lock, fs, "/A", NULL, NULL, FALSE, 0, newrev, FALSE,
                      pool));

  /* Add a lock token to FS access context. */
  SVN_ERR(svn_fs_access_add_lock_token(access, lock->token));

  /* Make some weird change: replace file '/A' by a directory with a
     child.  Issue 2507 means that the result is that the directory /A
     remains locked. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, newrev, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));
  SVN_ERR(svn_fs_delete(root, "/A", pool));
  SVN_ERR(svn_fs_make_dir(root, "/A", pool));
  SVN_ERR(svn_fs_make_file(root, "/A/b", pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

  /* Obtain a lock on '/A/b'.  Issue 2507 means that the lock index
     for / refers to both /A and /A/b, and that the lock index for /A
     refers to /A/b. */
  SVN_ERR(svn_fs_lock(&lock, fs, "/A/b", NULL, NULL, FALSE, 0, newrev, FALSE,
                      pool));

  /* Verify the locked paths. The lock for /A/b should not be reported
     twice even though issue 2507 means we access the index for / and
     the index for /A both of which refer to /A/b. */
  {
    static const char *expected_paths[] = {
      "/A",
      "/A/b",
    };
    num_expected_paths = sizeof(expected_paths) / sizeof(const char *);
    get_locks_baton = make_get_locks_baton(pool);
    SVN_ERR(svn_fs_get_locks(fs, "/", get_locks_callback,
                             get_locks_baton, pool));
    SVN_ERR(verify_matching_lock_paths(get_locks_baton, expected_paths,
                                       num_expected_paths, pool));
  }

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* The test table.  */

static int max_threads = 2;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(lock_expiration,
                       "test that locks can expire"),
    SVN_TEST_OPTS_PASS(lock_only,
                       "lock only"),
    SVN_TEST_OPTS_PASS(lookup_lock_by_path,
                       "lookup lock by path"),
    SVN_TEST_OPTS_PASS(attach_lock,
                       "attach lock"),
    SVN_TEST_OPTS_PASS(get_locks,
                       "get locks"),
    SVN_TEST_OPTS_PASS(basic_lock,
                       "basic locking"),
    SVN_TEST_OPTS_PASS(lock_credentials,
                       "test that locking requires proper credentials"),
    SVN_TEST_OPTS_PASS(final_lock_check,
                       "test that locking is enforced in final commit step"),
    SVN_TEST_OPTS_PASS(lock_dir_propchange,
                       "dir propchange can be committed with locked child"),
    SVN_TEST_OPTS_PASS(lock_break_steal_refresh,
                       "breaking, stealing, refreshing a lock"),
    SVN_TEST_OPTS_PASS(lock_out_of_date,
                       "check out-of-dateness before locking"),
    SVN_TEST_OPTS_PASS(lock_multiple_paths,
                       "lock multiple paths"),
    SVN_TEST_OPTS_PASS(lock_cb_error,
                       "lock callback error"),
    SVN_TEST_OPTS_PASS(obtain_write_lock_failure,
                       "lock/unlock when 'write-lock' couldn't be obtained"),
    SVN_TEST_OPTS_PASS(parent_and_child_lock,
                       "lock parent and it's child"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
