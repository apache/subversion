/*
 * main.c: Subversion server administration tool.
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

#include <apr_general.h>
#include <apr_pools.h>

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_fs.h"

int
main (int argc, const char * const *argv)
{
  apr_pool_t *pool;
  svn_fs_t *fs;
  svn_error_t *err;
  int is_create;
  int is_lstxn;
  const char *path = ".";

  /* ### this whole thing needs to be cleaned up once client/main.c
     ### is refactored. for now, let's just get the tool up and
     ### running. */
  if (argc < 2 || argc > 3 ||
      !((is_create = strcmp(argv[1], "create") == 0)
        || (is_lstxn = strcmp(argv[1], "lstxn") == 0)))
    {
      fprintf(stderr,
              "USAGE: %s COMMAND [ REPOS-PATH ]\n"
              "  COMMAND is one of: create, lstxn\n",
              argv[0]);
      return EXIT_FAILURE;
    }

  if (argc == 3)
    path = argv[2];

  apr_initialize ();
  pool = svn_pool_create (NULL);

  fs = svn_fs_new(pool);
  if (is_create)
    {
      err = svn_fs_create_berkeley(fs, path);
      if (err) goto error;
    }
  else if (is_lstxn)
    {
      char **txns;

      err = svn_fs_open_berkeley(fs, path);
      if (err) goto error;

      err = svn_fs_list_transactions(&txns, fs, pool);
      if (err) goto error;

      while (*txns != NULL)
        printf("%s\n", *txns++);
    }

  err = svn_fs_close_fs(fs);
  if (err) goto error;

  apr_pool_destroy (pool);
  apr_terminate();

  return EXIT_SUCCESS;

 error:
  svn_handle_error(err, stderr, FALSE);
  return EXIT_FAILURE;
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
