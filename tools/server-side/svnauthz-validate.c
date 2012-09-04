/*
 * svnauthz-validate.c : Load and validate an authz file.
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
 *
 *
 * svnauthz-validate.c : load and validate an authz file, returns
 *       value == 0 if syntax of authz file is correct
 *       value == 1 if syntax of authz file is invalid or file not found
 *       value == 2 in case of general error
 *
 */

#include "svn_cmdline.h"
#include "svn_dirent_uri.h"
#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_utf.h"

static int
usage(const char *argv0)
{
  printf("Usage:  %s FILE [USER PATH [REPOS_NAME]]\n\n", argv0);
  printf("Loads the authz file at FILE and validates its syntax.\n"
         "Optionally prints the access available to USER for PATH in\n"
         "repository REPOS_NAME.\n"
         "Returns:\n"
         "    0   when syntax is OK.\n"
         "    1   when syntax is invalid.\n"
         "    2   operational error\n");
  return 2;
}

int
main(int argc, const char **argv)
{
  apr_pool_t *pool;
  svn_error_t *err;
  svn_authz_t *authz;
  const char *authz_file;

  if (argc != 2 && argc != 4 && argc != 5)
    return usage(argv[0]);

  /* Initialize the app.  Send all error messages to 'stderr'.  */
  if (svn_cmdline_init(argv[0], stderr) != EXIT_SUCCESS)
    return 2;

  pool = svn_pool_create(NULL);

  /* Grab AUTHZ_FILE from argv. */
  SVN_INT_ERR(svn_utf_cstring_to_utf8(&authz_file, argv[1], pool));
  authz_file = svn_dirent_internal_style(authz_file, pool);

  /* Read the access file and validate it. */
  err = svn_repos_authz_read(&authz, authz_file, TRUE, pool);

  if (!err && (argc == 4 || argc == 5))
    {
      const char *user = argv[2];
      const char *path = argv[3];
      const char *repos = (argc == 5 ? argv[4] : "");
      svn_boolean_t read_access, write_access;

      if (path[0] != '/')
        path = apr_pstrcat(pool, "/", path, NULL);

      err = svn_repos_authz_check_access(authz, repos, path, user,
                                         svn_authz_write, &write_access,
                                         pool);
      if (!write_access && !err)
        err = svn_repos_authz_check_access(authz, repos, path, user,
                                           svn_authz_read, &read_access,
                                           pool);
      if (!err)
        printf("%s\n",
               write_access ? "rw" : read_access ? "r" : "no"
               );
    }

  svn_pool_destroy(pool);

  if (err)
    {
      svn_handle_error2(err, stderr, FALSE, "svnauthz-validate: ");
      svn_error_clear(err);
      return 1;
    }
  else
    {
      return 0;
    }
}
