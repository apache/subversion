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
#include "apr.h"
#include "fs.h"

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



/* Create/Open a filesystem.  */

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

  if (SVN_NO_ERROR != svn_fs_close_fs (fs))
    return fail();

  return 0;
}



/* The test table.  */

int (*test_funcs[]) (const char **msg) = {
  0,
  create_berkeley_filesystem,
  open_berkeley_filesystem,
  0
};



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
