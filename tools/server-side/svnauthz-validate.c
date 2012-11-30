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
#include "svn_opt.h"
#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_utf.h"

enum {
  OPT_USERNAME = SVN_OPT_FIRST_LONGOPT_ID,
  OPT_PATH,
  OPT_REPOS
};

static int
usage(const char *argv0)
{
  printf("Usage:  %s FILE [--username USER [--path FSPATH] [--repository REPOS_NAME]] FILE\n\n", argv0);
  printf("Loads the authz file at FILE and validates its syntax.\n"
         "Optionally prints the access available to USER for FSPATH in\n"
         "repository with authz name REPOS_NAME.  If FSPATH is omitted, reports\n"
         "whether USER has any access at all.\n"
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
  apr_status_t apr_err;
  svn_authz_t *authz;
  apr_getopt_t *os;
  const apr_getopt_option_t options[] =
    {
      {"username", OPT_USERNAME, 1, ("the authenticated username")},
      {"path", OPT_PATH, 1, ("path within the repository")},
      {"repository", OPT_REPOS, 1, ("repository authz name")},
      {0,             0,  0,  0}
    };
  struct {
    const char *authz_file;
    const char *username;
    const char *fspath;
    const char *repos_name;
  } opts;
  opts.username = opts.fspath = opts.repos_name = NULL;

  /* Initialize the app.  Send all error messages to 'stderr'.  */
  if (svn_cmdline_init(argv[0], stderr) != EXIT_SUCCESS)
    return 2;

  pool = svn_pool_create(NULL);

  /* Repeat svn_cmdline__getopt_init() inline. */
  apr_err = apr_getopt_init(&os, pool, argc, argv);
  if (apr_err)
    {
       err = svn_error_wrap_apr(apr_err,
                                ("Error initializing command line arguments"));
       svn_handle_warning2(stderr, err, "svnauthz-validate: ");
       return 2;
    }

  os->interleave = 1;
  while (1)
    {
      int opt;
      const char *arg;
      apr_status_t status = apr_getopt_long(os, options, &opt, &arg);
      if (APR_STATUS_IS_EOF(status))
        break;
      if (status != APR_SUCCESS)
        {
          return usage(argv[0]);
        }
      switch (opt)
        {
        case OPT_USERNAME:
          /* ### TODO: UTF-8? */
          opts.username = arg;
          break;
        case OPT_PATH:
          /* ### TODO: UTF-8? */
          opts.fspath = arg;
          break;
        case OPT_REPOS:
          opts.repos_name = arg;
          break;
        default:
          return usage(argv[0]);
        }
    }

  /* Exactly 1 non-option argument, and no --repository/--path
     unless --username.  */
  if (os->ind + 1 != argc || (!opts.username && (opts.fspath || opts.repos_name)))
    {
      return usage(argv[0]);
    }

  /* Grab AUTHZ_FILE from argv. */
  err = svn_utf_cstring_to_utf8(&opts.authz_file, os->argv[os->ind], pool);
  if (err)
    {
      svn_handle_warning2(stderr, err, "svnauthz-validate: ");
      svn_error_clear(err);
      return 2;
    }

  opts.authz_file = svn_dirent_internal_style(opts.authz_file, pool);

  /* Read the access file and validate it. */
  err = svn_repos_authz_read(&authz, opts.authz_file, TRUE, pool);

  /* Optionally, print the access a USER has to a given PATH in REPOS.
     PATH and REPOS may be NULL. */
  if (!err && opts.username)
    {
      const char *user = opts.username;
      const char *path = opts.fspath;
      const char *repos = opts.repos_name;
      svn_boolean_t read_access, write_access;

      if (path && path[0] != '/')
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
