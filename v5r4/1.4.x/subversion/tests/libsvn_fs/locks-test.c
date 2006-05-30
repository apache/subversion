/* lock-test.c --- tests for the filesystem locking functions
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#include <string.h>
#include <apr_pools.h>
#include <apr_time.h>

#include "svn_error.h"
#include "svn_fs.h"

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
  apr_hash_set(b->locks, lock_path->data, lock_path->len, 
               svn_lock_dup(lock, hash_pool));
  return SVN_NO_ERROR;
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


/*-----------------------------------------------------------------*/

/** The actual lock-tests called by `make check` **/



/* Test that we can create a lock--nothing more.  */
static svn_error_t *
lock_only(const char **msg,
          svn_boolean_t msg_only,
          svn_test_opts_t *opts,
          apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_lock_t *mylock;
  
  *msg = "lock only";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Prepare a filesystem and a new txn. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-lock-only", 
                              opts->fs_type, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree and commit it. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

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
lookup_lock_by_path(const char **msg,
                    svn_boolean_t msg_only,
                    svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_lock_t *mylock, *somelock;
  
  *msg = "lookup lock by path";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Prepare a filesystem and a new txn. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-lookup-lock-by-path", 
                              opts->fs_type, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree and commit it. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

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
attach_lock(const char **msg,
            svn_boolean_t msg_only,
            svn_test_opts_t *opts,
            apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_lock_t *somelock;
  svn_lock_t *mylock;
  const char *token;

  *msg = "attach lock";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Prepare a filesystem and a new txn. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-attach-lock", 
                              opts->fs_type, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree and commit it. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

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
get_locks(const char **msg,
          svn_boolean_t msg_only,
          svn_test_opts_t *opts,
          apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_lock_t *mylock;
  struct get_locks_baton_t *get_locks_baton;
  apr_size_t i, num_expected_paths;

  *msg = "get locks";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Prepare a filesystem and a new txn. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-get-locks", 
                              opts->fs_type, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree and commit it. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

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

  return SVN_NO_ERROR;
}


/* Test that we can create, fetch, and destroy a lock.  It exercises
   each of the five public fs locking functions.  */
static svn_error_t *
basic_lock(const char **msg,
           svn_boolean_t msg_only,
           svn_test_opts_t *opts,
           apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_lock_t *mylock, *somelock;
  
  *msg = "basic locking";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Prepare a filesystem and a new txn. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-basic-lock", 
                              opts->fs_type, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree and commit it. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

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
lock_credentials(const char **msg,
                 svn_boolean_t msg_only,
                 svn_test_opts_t *opts,
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

  *msg = "test that locking requires proper credentials";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Prepare a filesystem and a new txn. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-lock-credentials", 
                              opts->fs_type, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree and commit it. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

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
  if (! err)
    return svn_error_create 
      (SVN_ERR_TEST_FAILED, NULL,
       "Uhoh, able to commit locked file with no lock token.");
  svn_error_clear(err);
  
  /* Push the proper lock-token into the fs access context. */
  SVN_ERR(svn_fs_access_add_lock_token(access, mylock->token));

  /* Commit should now succeed. */
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

  return SVN_NO_ERROR;
}



/* Test that locks are enforced at commit time.  Somebody might lock
   something behind your back, right before you run
   svn_fs_commit_txn().  Also, this test verifies that recursive
   lock-checks on directories is working properly. */
static svn_error_t *
final_lock_check(const char **msg,
                 svn_boolean_t msg_only,
                 svn_test_opts_t *opts,
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

  *msg = "test that locking is enforced in final commit step";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Prepare a filesystem and a new txn. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-final-lock-check", 
                              opts->fs_type, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree and commit it. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

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
  if (! err)
    return svn_error_create 
      (SVN_ERR_TEST_FAILED, NULL,
       "Uhoh, able to commit dir deletion when a child is locked.");
  svn_error_clear(err);

  /* Supply correct username and token;  commit should work. */
  SVN_ERR(svn_fs_set_access(fs, access));
  SVN_ERR(svn_fs_access_add_lock_token(access, mylock->token));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

  return SVN_NO_ERROR;
}



/* If a directory's child is locked by someone else, we should still
   be able to commit a propchange on the directory. */
static svn_error_t *
lock_dir_propchange(const char **msg,
                    svn_boolean_t msg_only,
                    svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_lock_t *mylock;

  *msg = "dir propchange can be committed with locked child";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Prepare a filesystem and a new txn. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-lock-dir-propchange", 
                              opts->fs_type, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree and commit it. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

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

  return SVN_NO_ERROR;
}



/* DAV clients sometimes LOCK non-existent paths, as a way of
   reserving names.  Check that this technique works. */
static svn_error_t *
lock_name_reservation(const char **msg,
                      svn_boolean_t msg_only,
                      svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root, *rev_root;
  const char *conflict;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_lock_t *mylock;
  svn_error_t *err;

  *msg = "able to reserve a name (lock non-existent path)";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Prepare a filesystem and a new txn. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-lock-name-reservation", 
                              opts->fs_type, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree and commit it. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

  /* Become 'bubba' and lock imaginary path  "/A/D/G2/blooga". */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));
  SVN_ERR(svn_fs_lock(&mylock, fs, "/A/D/G2/blooga", NULL, "", 0, 0,
                      SVN_INVALID_REVNUM, FALSE, pool));

  /* We are no longer 'bubba'.  We're nobody. */
  SVN_ERR(svn_fs_set_access(fs, NULL));

  /* Make a new transaction. */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, newrev, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* This copy should fail, because an imaginary path in the target of
     the copy is reserved by someone else. */
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, 1, pool));
  err = svn_fs_copy(rev_root, "/A/D/G", txn_root, "/A/D/G2", pool);
  if (! err)
    return svn_error_create 
      (SVN_ERR_TEST_FAILED, NULL,
       "Uhoh, copy succeeded when path within target was locked.");
  svn_error_clear(err);
   
  return SVN_NO_ERROR;
}


/* Test that we can set and get locks in and under a directory.  We'll
   use non-existent FS paths for this test, though, as the FS API
   currently disallows directory locking.  */
static svn_error_t *
directory_locks_kinda(const char **msg,
                      svn_boolean_t msg_only,
                      svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_fs_access_t *access;
  svn_lock_t *mylock;
  apr_size_t num_expected_paths, i;
  struct get_locks_baton_t *get_locks_baton;

  *msg = "directory locks (kinda)";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Prepare a filesystem and a new txn. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-directory-locks-kinda", 
                              opts->fs_type, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* We are now 'bubba'. */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  /*** Lock some various, non-existent, yet dir-name-spacily
       overlapping paths; verify. ***/
  {
    static const char *expected_paths[] = { 
      "/Program Files/Tigris.org/Subversion", 
      "/Program Files/Tigris.org",
      "/Stuff/Junk/Fluff",
      "/Program Files",
    };
    num_expected_paths = sizeof(expected_paths) / sizeof(const char *);

    /* Lock all paths under /A/D/G. */
    for (i = 0; i < num_expected_paths; i++)
      {
        SVN_ERR(svn_fs_lock(&mylock, fs, expected_paths[i], NULL, "", 0, 0, 
                            SVN_INVALID_REVNUM, FALSE, pool));
      }
    get_locks_baton = make_get_locks_baton(pool);
    SVN_ERR(svn_fs_get_locks(fs, "/", get_locks_callback,
                             get_locks_baton, pool));
    SVN_ERR(verify_matching_lock_paths(get_locks_baton, expected_paths,
                                       num_expected_paths, pool));
  }

  /*** Now unlock a "middle directory" ***/
  {
    static const char *expected_paths[] = { 
      "/Program Files/Tigris.org/Subversion", 
      "/Stuff/Junk/Fluff",
      "/Program Files",
    };
    num_expected_paths = sizeof(expected_paths) / sizeof(const char *);

    SVN_ERR(svn_fs_unlock(fs, "/Program Files/Tigris.org", NULL, 
                          TRUE, pool));
    get_locks_baton = make_get_locks_baton(pool);
    SVN_ERR(svn_fs_get_locks(fs, "/", get_locks_callback,
                             get_locks_baton, pool));
    SVN_ERR(verify_matching_lock_paths(get_locks_baton, expected_paths,
                                       num_expected_paths, pool));
  }

  return SVN_NO_ERROR;
}


/* Test that locks auto-expire correctly. */
static svn_error_t *
lock_expiration(const char **msg,
                svn_boolean_t msg_only,
                svn_test_opts_t *opts,
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

  *msg = "test that locks can expire";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Prepare a filesystem and a new txn. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-lock-expiration", 
                              opts->fs_type, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree and commit it. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

  /* Make a new transaction and change rho. */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, newrev, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "/A/D/G/rho",
                                      "new contents", pool));

  /* We are now 'bubba'. */
  SVN_ERR(svn_fs_create_access(&access, "bubba", pool));
  SVN_ERR(svn_fs_set_access(fs, access));

  /* Lock /A/D/G/rho, with an expiration 3 seconds from now. */
  SVN_ERR(svn_fs_lock(&mylock, fs, "/A/D/G/rho", NULL, "", 0,
                      apr_time_now() + apr_time_from_sec(3),
                      SVN_INVALID_REVNUM, FALSE, pool));

  /* Become nobody. */
  SVN_ERR(svn_fs_set_access(fs, NULL));
  
  /* Try to commit.  Should fail because we're 'nobody', and the lock
     hasn't expired yet. */
  err = svn_fs_commit_txn(&conflict, &newrev, txn, pool);
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

  /* Sleep 5 seconds, so the lock auto-expires.  Anonymous commit
     should then succeed. */
  apr_sleep(apr_time_from_sec(5));

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

  return SVN_NO_ERROR;
}

/* Test that a lock can be broken, stolen, or refreshed */
static svn_error_t *
lock_break_steal_refresh(const char **msg,
                         svn_boolean_t msg_only,
                         svn_test_opts_t *opts,
                         apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  const char *conflict;
  svn_revnum_t newrev;
  svn_fs_access_t *access;
  svn_lock_t *mylock, *somelock;

  *msg = "breaking, stealing, refreshing a lock";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Prepare a filesystem and a new txn. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-steal-refresh", 
                              opts->fs_type, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree and commit it. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

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
lock_out_of_date(const char **msg,
                 svn_boolean_t msg_only,
                 svn_test_opts_t *opts,
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
  
  *msg = "check out-of-dateness before locking";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Prepare a filesystem and a new txn. */
  SVN_ERR(svn_test__create_fs(&fs, "test-repo-lock-out-of-date", 
                              opts->fs_type, pool));
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, 0, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create the greek tree and commit it. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));

  /* Commit a small change to /A/D/G/rho, creating revision 2. */
  SVN_ERR(svn_fs_begin_txn2(&txn, fs, newrev, SVN_FS_TXN_CHECK_LOCKS, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));
  SVN_ERR(svn_test__set_file_contents(txn_root, "/A/D/G/rho",
                                      "new contents", pool));
  SVN_ERR(svn_fs_commit_txn(&conflict, &newrev, txn, pool));
  
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



/* ------------------------------------------------------------------------ */

/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(lock_only),
    SVN_TEST_PASS(lookup_lock_by_path),
    SVN_TEST_PASS(attach_lock),
    SVN_TEST_PASS(get_locks),
    SVN_TEST_PASS(basic_lock),
    SVN_TEST_PASS(lock_credentials),
    SVN_TEST_PASS(final_lock_check),
    SVN_TEST_PASS(lock_dir_propchange),
    SVN_TEST_XFAIL(lock_name_reservation),
    SVN_TEST_XFAIL(directory_locks_kinda),
    SVN_TEST_PASS(lock_expiration),
    SVN_TEST_PASS(lock_break_steal_refresh),
    SVN_TEST_PASS(lock_out_of_date),
    SVN_TEST_NULL
  };
