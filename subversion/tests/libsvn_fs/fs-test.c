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

/* A global pool, initialized by `main' for tests to use.  */
apr_pool_t *pool;


/*-------------------------------------------------------------------*/

/** Helper routines. **/

/* Create a berkeley db repository in a subdir NAME, and return a new
   FS object which points to it.  */
static svn_error_t *
create_fs_and_repos (svn_fs_t **fs, const char *name)
{
  *fs = svn_fs_new (pool);
  if (! fs)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "Couldn't alloc a new fs object.");
  
  SVN_ERR (svn_fs_create_berkeley (*fs, name));
  
  return SVN_NO_ERROR;
}



/*-----------------------------------------------------------------*/

/** The actual fs-tests called by `make check` **/

/* Create a filesystem.  */
static svn_error_t *
create_berkeley_filesystem (const char **msg)
{
  svn_fs_t *fs;

  *msg = "svn_fs_create_berkeley";

  /* Create and close a repository. */
  SVN_ERR (create_fs_and_repos (&fs, "test-repo-1")); /* helper */
  SVN_ERR (svn_fs_close_fs (fs));
  
  return SVN_NO_ERROR;
}


/* Open an existing filesystem.  */
static svn_error_t *
open_berkeley_filesystem (const char **msg)
{
  svn_fs_t *fs, *fs2;

  *msg = "open an existing Berkeley DB filesystem";

  /* Create and close a repository (using fs). */
  SVN_ERR (create_fs_and_repos (&fs, "test-repo-2")); /* helper */
  SVN_ERR (svn_fs_close_fs (fs));

  /* Create a different fs object, and use it to re-open the
     repository again.  */
  fs2 = svn_fs_new (pool);
  if (! fs2)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "Couldn't alloc a new fs object.");

  SVN_ERR (svn_fs_open_berkeley (fs2, "test-repo-2"));
  SVN_ERR (svn_fs_close_fs (fs2));

  return SVN_NO_ERROR;
}


/* Fetch the youngest revision from a repos. */
static svn_error_t *
fetch_youngest_rev (const char **msg)
{
  svn_fs_t *fs;
  svn_revnum_t rev;

  *msg = "fetch the youngest revision from a filesystem";

  SVN_ERR (create_fs_and_repos (&fs, "test-repo-3")); /* helper */

  SVN_ERR (svn_fs_youngest_rev (&rev, fs, pool));
  
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}



/* Begin a txn, check its name, then close it */
static svn_error_t *
trivial_transaction (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  char *txn_name;

  *msg = "begin a txn, check its name, then close it";

  SVN_ERR (create_fs_and_repos (&fs, "test-repo-4")); /* helper */

  /* Begin a new transaction that is based on revision 0.  */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, pool));
      
  /* Test that the txn name is non-null. */
  SVN_ERR (svn_fs_txn_name (&txn_name, txn, pool));
  
  if (! txn_name)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "Got a NULL txn name.");

  /* Close the transaction and fs. */
  SVN_ERR (svn_fs_close_txn (txn));
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}



/* Open an existing transaction by name. */
static svn_error_t *
reopen_trivial_transaction (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  char *txn_name;

  *msg = "open an existing transaction by name";

  SVN_ERR (create_fs_and_repos (&fs, "test-repo-5")); /* helper */

  /* Begin a new transaction that is based on revision 0.  */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, pool));
  SVN_ERR (svn_fs_txn_name (&txn_name, txn, pool));

  /* Close the transaction. */
  SVN_ERR (svn_fs_close_txn (txn));

  /* Reopen the transaction by name */
  SVN_ERR (svn_fs_open_txn (&txn, fs, txn_name, pool));

  /* Close the transaction and fs. */
  SVN_ERR (svn_fs_close_txn (txn));
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}



/* Create a file! */
static svn_error_t *
create_file_transaction (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  *msg = "begin a txn, get the txn root, and add a file!";

  SVN_ERR (create_fs_and_repos (&fs, "test-repo-6")); /* helper */

  /* Begin a new transaction that is based on revision 0.  */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, pool));

  /* Get the txn root */
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, pool));
  
  /* Create a new file in the root directory. */
  SVN_ERR (svn_fs_make_file (txn_root, "beer.txt", pool));

  /* Close the transaction and fs. */
  SVN_ERR (svn_fs_close_txn (txn));
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}


/* Make sure we get txn lists correctly. */
static svn_error_t *
verify_txn_list (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn1, *txn2;
  char *name1, *name2;
  char **txn_list;

  *msg = "create 2 txns, list them, and verify the list.";

  SVN_ERR (create_fs_and_repos (&fs, "test-repo-7")); /* helper */

  /* Begin a new transaction, get its name, close it.  */
  SVN_ERR (svn_fs_begin_txn (&txn1, fs, 0, pool));
  SVN_ERR (svn_fs_txn_name (&name1, txn1, pool));
  SVN_ERR (svn_fs_close_txn (txn1));

  /* Begin *another* transaction, get its name, close it.  */
  SVN_ERR (svn_fs_begin_txn (&txn2, fs, 0, pool));
  SVN_ERR (svn_fs_txn_name (&name2, txn2, pool));
  SVN_ERR (svn_fs_close_txn (txn2));

  /* Get the list of active transactions from the fs. */
  SVN_ERR (svn_fs_list_transactions (&txn_list, fs, pool));

  /* Check the list. It should have *exactly* two entries. */
  if ((txn_list[0] == NULL)
      || (txn_list[1] == NULL)
      || (txn_list[2] != NULL))
    goto all_bad;
  
  /* We should be able to find our 2 txn names in the list, in some
     order. */
  if ((! strcmp (txn_list[0], name1))
      && (! strcmp (txn_list[1], name2)))
    goto all_good;
  
  else if ((! strcmp (txn_list[1], name1))
           && (! strcmp (txn_list[0], name2)))
    goto all_good;
  
 all_bad:

  return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                           "Got a bogus txn list.");
 all_good:
  
  /* Close the fs. */
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}



/* The test table.  */

svn_error_t * (*test_funcs[]) (const char **msg) = {
  0,
  create_berkeley_filesystem,
  open_berkeley_filesystem,
  fetch_youngest_rev,
  trivial_transaction,
  reopen_trivial_transaction,
  create_file_transaction,
  verify_txn_list,
  0
};



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
