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
#include "fs.h"
#include "rev-table.h"
#include "trail.c"

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

static int
create_berkeley_filesystem (const char **msg)
{
  svn_fs_t *fs;

  *msg = "create Berkeley DB filesystem";

  fs = svn_fs_new (pool);
  if (fs == NULL)
    return fail();

  if (SVN_NO_ERROR != svn_fs_create_berkeley (fs, repository))
    return fail();

  if (SVN_NO_ERROR != svn_fs_close_fs (fs))
    return fail();

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


static int
open_berkeley_filesystem (const char **msg)
{
  svn_fs_t *fs;

  *msg = "open Berkeley DB filesystem";

  fs = svn_fs_new (pool);
  if (fs == NULL)
    return fail();

  if (SVN_NO_ERROR != svn_fs_open_berkeley (fs, repository))
    return fail();

  if (SVN_NO_ERROR != svn_fs__retry_txn (fs, check_filesystem_root_id,
                                         fs, fs->pool))
    return fail();

  if (SVN_NO_ERROR != svn_fs_close_fs (fs))
    return fail();

  return 0;
}


static int
trivial_transaction (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_error_t *err;

  *msg = "begin a txn, check its name, then immediately close it";

  /* Open the FS. */
  fs = svn_fs_new (pool);
  if (fs == NULL)
    return fail();

  if (SVN_NO_ERROR != svn_fs_open_berkeley (fs, repository))
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

  return 0;
}


static int
reopen_trivial_transaction (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;

  *msg = "reopen and check the transaction name";

  /* Open the FS. */
  fs = svn_fs_new (pool);
  if (fs == NULL)
    return fail();

  if (SVN_NO_ERROR != svn_fs_open_berkeley (fs, repository))
    return fail();

  /* Open the transaceion, just to make sure it's in the database. */
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
list_live_transactions (const char **msg)
{
  svn_fs_t *fs;
  char **txn_list;

  *msg = "list active transactions";

  /* Open the FS. */
  fs = svn_fs_new (pool);
  if (fs == NULL)
    return fail();

  if (SVN_NO_ERROR != svn_fs_open_berkeley (fs, repository))
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
  list_live_transactions,
  0
};



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
