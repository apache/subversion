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



struct write_new_rep_args
{
  char *key;
  svn_fs_t *fs;
  skel_t *skel;
};


static svn_error_t *
txn_body_write_new_rep (void *baton, trail_t *trail)
{
  struct write_new_rep_args *wb = (struct write_new_rep_args *) baton;
  SVN_ERR (svn_fs__write_new_rep (&(wb->key), wb->fs, wb->skel, trail));

  return SVN_NO_ERROR;
}


static svn_error_t *
write_new_rep (const char **msg, apr_pool_t *pool)
{
  struct write_new_rep_args args;
  const char *rep = "(\"fulltext\" \"a83t2Z0q\")";

  *msg = "Write a new rep, get a new key back.";

  SVN_ERR (svn_test__create_fs_and_repos
           (&(args.fs), "test-repo-write-new-rep", pool));

  args.skel = svn_fs__parse_skel ((char *) rep, strlen (rep), pool);
  args.key = NULL;

  SVN_ERR (svn_fs__retry_txn (args.fs, txn_body_write_new_rep, &args, pool));

  SVN_ERR (svn_fs_close_fs (args.fs));

  if (args.key == NULL)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "error writing new representation");

  printf ("GOT KEY: %s\n", args.key);
  
  return SVN_NO_ERROR;
}



/* The test table.  */

svn_error_t * (*test_funcs[]) (const char **msg,
                               apr_pool_t *pool) = {
  0,
  write_new_rep,
#if 0  /* coming soon */
  write_named_rep,
  read_rep,
#endif
  0
};



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
