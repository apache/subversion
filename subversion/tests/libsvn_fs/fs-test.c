/* fs-test.c --- tests for the filesystem
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <stdlib.h>
#include <apr_pools.h>
#include "svn_error.h"
#include "svn_fs.h"
#include "../../libsvn_fs/fs.h"
#include "../../libsvn_fs/rev-table.h"
#include "../../libsvn_fs/trail.c"

/* Some utility functions.  */


/* A global pool, initialized by `main' for tests to use.  */
apr_pool_t *pool;


/* A place to set a breakpoint.  */
static int
fail (void)
{
  return 1;
}


/* The name of the test repository. */
const char repository[] = "test-repo";



/* Create a filesystem.  */

/* Safe to call this multiple times -- only creates a filesystem once. */
static int
create_berkeley_filesystem (const char **msg)
{
  svn_fs_t *fs;
  static int fs_already_created = 0;

  *msg = "create Berkeley DB filesystem";

  if (! fs_already_created)
    {
      fs = svn_fs_new (pool);
      if (fs == NULL)
        return fail();
      
      if (SVN_NO_ERROR != svn_fs_create_berkeley (fs, repository))
        return fail();
      
      if (SVN_NO_ERROR != svn_fs_close_fs (fs))
        return fail();
      
      fs_already_created = 1;
    }

  return 0;
}



/* Open a filesystem.  */


/* Get and check the initial root id; must be 0.0. */
static svn_error_t *
check_filesystem_root_id (void *fs_baton, trail_t *trail)
{
  svn_fs_t *fs = fs_baton;
  svn_fs_id_t *root_id;

  SVN_ERR (svn_fs__rev_get_root (&root_id, fs, 0, trail));
  if (root_id[0] != 0
      && root_id[1] != 0
      && root_id[2] != -1)
    return svn_error_create(SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
                            "node id of revision `0' is not `0.0'");
  return SVN_NO_ERROR;
}



/* The Prologue: stuff to do at the beginning of most txn-based tests.
   See in-line comments at each step to find out what this does. */
static int
common_test_prologue (svn_fs_t **fs)
{
  const char *ignored;

  /* Make sure the filesystem exists. */
  if (create_berkeley_filesystem (&ignored) != 0)
    return fail();

  /* Init a new FS structure */
  *fs = svn_fs_new (pool);
  if (*fs == NULL)
    return fail();

  /* Open our db tables, and hook them up to our FS structure */
  if (SVN_NO_ERROR != svn_fs_open_berkeley (*fs, repository))
    return fail();

  return 0;
}



static int
open_berkeley_filesystem (const char **msg)
{
  svn_fs_t *fs;

  *msg = "open Berkeley DB filesystem";

  /* Do our common `startup stuff' */
  if (common_test_prologue (&fs))
    return fail();

  if (SVN_NO_ERROR != svn_fs__retry_txn (fs, check_filesystem_root_id,
                                         fs, fs->pool))
    return fail();

  {
    svn_revnum_t rev;

    if (SVN_NO_ERROR != svn_fs_youngest_rev (&rev, fs, pool))
      return fail();

    if (rev != 0)
      return fail();
  }

  if (SVN_NO_ERROR != svn_fs_close_fs (fs))
    return fail();

  return 0;
}




/* Safe to call this multiple times -- only creates first txn once. */
static int
trivial_transaction (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_error_t *err;
  static int made_first_txn_already = 0;

  *msg = "begin a txn, check its name, then immediately close it";

  if (! made_first_txn_already)
    {
      /* Do our common `startup stuff' */
      if (common_test_prologue (&fs))
        return fail();

      /* Begin a transaction. */
      if (SVN_NO_ERROR != svn_fs_begin_txn (&txn, fs, 0, pool))
        return fail();
      
      /* Test that it got id "0", since it's the first txn. */
      {
        char *txn_name;
        
        err = svn_fs_txn_name (&txn_name, txn, pool);
        if (err)
          return fail();
        
        if (strcmp (txn_name, "0") != 0)
          return fail();
      }
      
      /* Close it. */
      if (SVN_NO_ERROR != svn_fs_close_txn (txn))
        return fail();
      
      /* Close the FS. */
      if (SVN_NO_ERROR != svn_fs_close_fs (fs))
        return fail();

      made_first_txn_already = 1;
    }

  return 0;
}


static int
reopen_trivial_transaction (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  const char *ignored;

  *msg = "reopen and check the transaction name";

  /* Do our common `startup stuff' */
  if (common_test_prologue (&fs))
    return fail();

  /* Make sure the transaction exists. */
  if (trivial_transaction (&ignored) != 0)
    return fail();

  /* Open the transaction, just to make sure it's in the database. */
  if (SVN_NO_ERROR != svn_fs_open_txn (&txn, fs, "0", pool))
    return fail();

  /* Close it. */
  if (SVN_NO_ERROR != svn_fs_close_txn (txn))
    return fail();

  /* Close the FS. */
  if (SVN_NO_ERROR != svn_fs_close_fs (fs))
    return fail();

  return 0;
}


static int
create_file_transaction (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_error_t *err;
  static int made_txn_already = 0;

  *msg = "begin a txn, get the txn root, and add a file!";

  if (! made_txn_already)
    {
      /* Do our common `startup stuff' */
      if (common_test_prologue (&fs))
        return fail();
      
      /* Begin a transaction. */
      if (SVN_NO_ERROR != svn_fs_begin_txn (&txn, fs, 0, pool))
        return fail();
      
      /* Test that it got id "0", since it's the second txn. */
      {
        char *txn_name;
        
        err = svn_fs_txn_name (&txn_name, txn, pool);
        if (err)
          return fail();
        
        if (strcmp (txn_name, "0") != 0)
          return fail();
      }
      
      {
        svn_fs_root_t *txn_root;

        /* Get the txn root */
        if (SVN_NO_ERROR != svn_fs_txn_root (&txn_root, txn, pool))
          return fail();

        /* Create a file named "my_file.txt" in the root directory. */
        if (SVN_NO_ERROR != svn_fs_make_file (txn_root,
                                              "beer.txt",
                                              pool))
          return fail();
      }

      /* Close it. */
      if (SVN_NO_ERROR != svn_fs_close_txn (txn))
        return fail();
      
     /* Close the FS. */
      if (SVN_NO_ERROR != svn_fs_close_fs (fs))
        return fail();

      made_txn_already = 1;
    }

  return 0;
}


static int
list_live_transactions (const char **msg)
{
  svn_fs_t *fs;
  char **txn_list;
  const char *ignored;

  *msg = "list active transactions";

  /* Do our common `startup stuff' */
  if (common_test_prologue (&fs))
    return fail();

  /* Make sure the transaction exists. */
  if (trivial_transaction (&ignored) != 0)
    return fail();

  /* Get the list of transactions. */
  if (SVN_NO_ERROR != svn_fs_list_transactions (&txn_list, fs, pool))
    return fail();

  /* Check the list. It should have exactly one entry, "0". */
  if (txn_list[0] == NULL
      || txn_list[1] != NULL
      || strcmp (txn_list[0], "0") != 0)
    return fail();

  /* Close the FS. */
  if (SVN_NO_ERROR != svn_fs_close_fs (fs))
    return fail();

  return 0;
}



/* The test table.  */

int (*test_funcs[]) (const char **msg) = {
  0,
  create_berkeley_filesystem,
  open_berkeley_filesystem,
  trivial_transaction,
  reopen_trivial_transaction,
  create_file_transaction,
  list_live_transactions,
  0
};



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
