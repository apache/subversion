/* strings-reps-test.c --- test `strings' and `representations' interfaces
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "svn_error.h"
#include "apr.h"
#include "../fs-helpers.h"
#include "../../libsvn_fs/skel.h"
#include "../../libsvn_fs/strings-table.h"
#include "../../libsvn_fs/reps-table.h"


/*-----------------------------------------------------------------*/

/* Helper functions and batons for reps-table testing. */
struct rep_args
{
  char *key;
  svn_fs_t *fs;
  skel_t *skel;
};


static svn_error_t *
txn_body_write_new_rep (void *baton, trail_t *trail)
{
  struct rep_args *wb = (struct rep_args *) baton;
  return svn_fs__write_new_rep (&(wb->key), wb->fs, wb->skel, trail);
}


static svn_error_t *
txn_body_write_rep (void *baton, trail_t *trail)
{
  struct rep_args *wb = (struct rep_args *) baton;
  return svn_fs__write_rep (wb->fs, wb->key, wb->skel, trail);
}


static svn_error_t *
txn_body_read_rep (void *baton, trail_t *trail)
{
  struct rep_args *wb = (struct rep_args *) baton;
  return svn_fs__read_rep (&(wb->skel), wb->fs, wb->key, trail);
}


static svn_error_t *
txn_body_delete_rep (void *baton, trail_t *trail)
{
  struct rep_args *wb = (struct rep_args *) baton;
  return svn_fs__delete_rep (wb->fs, wb->key, trail);
}



/* Representation Table Test functions. */

static svn_error_t *
write_new_rep (const char **msg, apr_pool_t *pool)
{
  struct rep_args args;
  const char *rep = "(fulltext a83t2Z0q)";
  svn_fs_t *fs;

  *msg = "Write a new rep, get a new key back.";

  /* Create a new fs and repos */
  SVN_ERR (svn_test__create_fs_and_repos
           (&fs, "test-repo-write-new-rep", pool));

  /* Set up transaction baton */
  args.fs = fs;
  args.skel = svn_fs__parse_skel ((char *)rep, strlen (rep), pool);
  args.key = NULL;

  /* Write new rep to reps table. */
  SVN_ERR (svn_fs__retry_txn (args.fs, txn_body_write_new_rep, &args, pool));

  /* Close the filesystem. */
  SVN_ERR (svn_fs_close_fs (fs));

  if (args.key == NULL)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "error writing new representation");

  return SVN_NO_ERROR;
}


static svn_error_t *
write_rep (const char **msg, apr_pool_t *pool)
{
  struct rep_args new_args;
  struct rep_args args;
  const char *new_rep = "(fulltext a83t2Z0q)";
  const char *rep = "(fulltext kfogel31337)";
  svn_fs_t *fs;

  *msg = "Write a new rep, then overwrite it.";

  /* Create a new fs and repos */
  SVN_ERR (svn_test__create_fs_and_repos
           (&fs, "test-repo-write-rep", pool));

  /* Set up transaction baton */
  new_args.fs = fs;
  new_args.skel = svn_fs__parse_skel ((char *)new_rep, strlen (new_rep), pool);
  new_args.key = NULL;

  /* Write new rep to reps table. */
  SVN_ERR (svn_fs__retry_txn (new_args.fs, 
                              txn_body_write_new_rep, &new_args, pool));

  /* Make sure we got a valid key. */
  if (new_args.key == NULL)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "error writing new representation");

  /* Set up transaction baton for re-writing reps. */
  args.fs = new_args.fs;
  args.skel = svn_fs__parse_skel ((char *)rep, strlen (rep), pool);
  args.key = new_args.key;

  /* Overwrite first rep in reps table. */
  SVN_ERR (svn_fs__retry_txn (new_args.fs, 
                              txn_body_write_rep, &args, pool));

  /* Close the filesystem. */
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}


static svn_error_t *
read_rep (const char **msg, apr_pool_t *pool)
{
  struct rep_args new_args;
  struct rep_args args;
  struct rep_args read_args;
  const char *new_rep = "(fulltext a83t2Z0q)";
  const char *rep = "(fulltext kfogel31337)";
  svn_stringbuf_t *skel_data;
  svn_fs_t *fs;

  *msg = "Write and overwrite a new rep; confirm with reads.";

  /* Create a new fs and repos */
  SVN_ERR (svn_test__create_fs_and_repos
           (&fs, "test-repo-read-rep", pool));

  /* Set up transaction baton */
  new_args.fs = fs;
  new_args.skel = svn_fs__parse_skel ((char *)new_rep, strlen (new_rep), pool);
  new_args.key = NULL;

  /* Write new rep to reps table. */
  SVN_ERR (svn_fs__retry_txn (new_args.fs, 
                              txn_body_write_new_rep, &new_args, pool));

  /* Make sure we got a valid key. */
  if (new_args.key == NULL)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "error writing new representation");

  /* Read the new rep back from the reps table. */
  read_args.fs = new_args.fs;
  read_args.skel = NULL;
  read_args.key = new_args.key;
  SVN_ERR (svn_fs__retry_txn (new_args.fs, 
                              txn_body_read_rep, &read_args, pool));

  /* Make sure the skel matches. */
  if (! read_args.skel)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "error reading new representation");
  
  skel_data = svn_fs__unparse_skel (read_args.skel, pool);
  if (strcmp (skel_data->data, new_rep))
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "representation corrupted");
  
  /* Set up transaction baton for re-writing reps. */
  args.fs = new_args.fs;
  args.skel = svn_fs__parse_skel ((char *)rep, strlen (rep), pool);
  args.key = new_args.key;

  /* Overwrite first rep in reps table. */
  SVN_ERR (svn_fs__retry_txn (new_args.fs, 
                              txn_body_write_rep, &args, pool));

  /* Read the new rep back from the reps table (using the same FS and
     key as the first read...let's make sure this thing didn't get
     written to the wrong place). */
  read_args.skel = NULL;
  SVN_ERR (svn_fs__retry_txn (new_args.fs, 
                              txn_body_read_rep, &read_args, pool));

  /* Make sure the skel matches. */
  if (! read_args.skel)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "error reading new representation");
  
  skel_data = svn_fs__unparse_skel (read_args.skel, pool);
  if (strcmp (skel_data->data, rep))
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "representation corrupted");
  
  /* Close the filesystem. */
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}


static svn_error_t *
delete_rep (const char **msg, apr_pool_t *pool)
{
  struct rep_args new_args;
  struct rep_args delete_args;
  struct rep_args read_args;
  const char *new_rep = "(fulltext a83t2Z0q)";
  svn_fs_t *fs;
  svn_error_t *err;

  *msg = "Write, then delete, a new rep; confirm deletion.";

  /* Create a new fs and repos */
  SVN_ERR (svn_test__create_fs_and_repos
           (&fs, "test-repo-delete-rep", pool));

  /* Set up transaction baton */
  new_args.fs = fs;
  new_args.skel = svn_fs__parse_skel ((char *)new_rep, strlen (new_rep), pool);
  new_args.key = NULL;

  /* Write new rep to reps table. */
  SVN_ERR (svn_fs__retry_txn (new_args.fs, 
                              txn_body_write_new_rep, &new_args, pool));

  /* Make sure we got a valid key. */
  if (new_args.key == NULL)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "error writing new representation");

  /* Delete the rep we just wrote. */
  delete_args.fs = new_args.fs;
  delete_args.key = new_args.key;
  SVN_ERR (svn_fs__retry_txn (new_args.fs, 
                              txn_body_delete_rep, &delete_args, pool));

  /* Try to read the new rep back from the reps table. */
  read_args.fs = new_args.fs;
  read_args.skel = NULL;
  read_args.key = new_args.key;
  err = svn_fs__retry_txn (new_args.fs, 
                           txn_body_read_rep, &read_args, pool);

  /* We better have an error... */
  if ((! err) && (read_args.skel))
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "error deleting representation");
  
  /* Close the filesystem. */
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}




/* The test table.  */

svn_error_t * (*test_funcs[]) (const char **msg,
                               apr_pool_t *pool) = {
  0,
  write_new_rep,
  write_rep,
  read_rep,
  delete_rep,
  0
};



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
