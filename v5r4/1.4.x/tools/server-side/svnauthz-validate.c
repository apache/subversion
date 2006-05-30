/*
 * svnauthz-validate.c : Load and validate an authz file.
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
 *
 *
 * svnauthz-validate.c : load and validate an authz file, returns 
 *       value == 0 if syntax of authz file is correct
 *       value == 1 if syntax of authz file is invalid or file not found
 *       value == 2 in case of general error
 *
 */

#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_cmdline.h"

int
main(int argc, const char **argv)
{
  apr_pool_t *pool;
  svn_error_t *err;
  svn_authz_t *authz;
  const char *authz_file;

  if (argc <= 1)
    {
      printf("Usage:  %s PATH \n\n", argv[0]);  
      printf("Loads the authz file at PATH and validates its syntax. \n"
             "Returns:\n"
             "    0   when syntax is OK.\n"
             "    1   when syntax is invalid.\n"
             "    2   operational error\n");              
      return 2;
    }

  authz_file = argv[1];

  /* Initialize the app.  Send all error messages to 'stderr'.  */
  if (svn_cmdline_init(argv[0], stderr) != EXIT_SUCCESS)
    return 2;

  pool = svn_pool_create(NULL);

  /* Read the access file and validate it. */
  err = svn_repos_authz_read(&authz, authz_file, TRUE, pool);

  svn_pool_destroy(pool); 

  if (err)
    {
      svn_handle_error2(err, stderr, FALSE, "svnauthz-validate: ");
      return 1;
    }
  else
    {
      return 0;
    }
}
