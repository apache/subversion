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
#include "svn_path.h"

enum {
  OPT_USERNAME = SVN_OPT_FIRST_LONGOPT_ID,
  OPT_PATH,
  OPT_REPOS
};

static int
usage(const char *argv0)
{
  printf("Usage: 1. %s [OPTION]... TARGET\n", argv0);
  printf("       2. %s [OPTION]... --transaction TXN REPOS_PATH FILE_PATH\n\n", argv0); 
  printf(" 1. Loads and validates the syntax of the authz file at TARGET.\n"
         "    TARGET can be a path to a file or an absolute file:// URL to an authz\n"
         "    file in a repository, but cannot be a repository relative URL (^/).\n\n"
         " 2. Loads and validates the syntax of the authz file at FILE_PATH in the\n"
         "    transaction TXN in the repository at REPOS_PATH.\n\n"
         " Options:\n\n"
         "   --username USER         : prints access available for a user\n"
         "   --path FSPATH           : makes --username print access available for FSPATH\n"
         "   --repository REPOS_NAME : use REPOS_NAME as repository authz name when\n"
         "                               determining access available with --username\n"
         "   -t, --transaction TXN   : enables mode 2 which looks for the file in an\n"
         "                               uncommitted transaction TXN\n\n"
         "Returns:\n"
         "    0   when syntax is OK.\n"
         "    1   when syntax is invalid.\n"
         "    2   operational error\n");
  return 2;
}

/* Loads the authz config into *AUTHZ from the file at AUTHZ_FILE
   in repository at REPOS_PATH from the transaction TXN_NAME.  Using
   POOL for allocations. */
static svn_error_t *
get_authz_from_txn(svn_authz_t **authz, const char *repos_path,
                   const char *authz_file, const char *txn_name,
                   apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *root;
  svn_node_kind_t node_kind;
  svn_stream_t *contents;
  svn_error_t *err;

  /* Open up the repository and find the transaction root */
  SVN_ERR(svn_repos_open2(&repos, repos_path, NULL, pool));
  fs = svn_repos_fs(repos);
  SVN_ERR(svn_fs_open_txn(&txn, fs, txn_name, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));

  /* Make sure the path is a file */
  SVN_ERR(svn_fs_check_path(&node_kind, root, authz_file, pool));
  if (node_kind != svn_node_file)
    return svn_error_createf(SVN_ERR_FS_NOT_FILE, NULL,
                             "Path '%s' is not a file", authz_file);

  SVN_ERR(svn_fs_file_contents(&contents, root, authz_file, pool));
  err = svn_repos_authz_parse(authz, contents, pool);

  /* Add the filename to the error stack since the parser doesn't have it. */
  if (err != SVN_NO_ERROR)
    return svn_error_createf(err->apr_err, err,
                             "Error parsing authz file: '%s':", authz_file);

  return SVN_NO_ERROR;
}

static int
sub_main(int argc, const char *argv[], apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  svn_authz_t *authz;
  apr_getopt_t *os;
  const apr_getopt_option_t options[] =
    {
      {"username", OPT_USERNAME, 1, ("the authenticated username")},
      {"path", OPT_PATH, 1, ("path within the repository")},
      {"repository", OPT_REPOS, 1, ("repository authz name")},
      {"transaction", 't', 1, ("transaction id")},
      {0,             0,  0,  0}
    };
  struct {
    const char *authz_file;
    const char *username;
    const char *fspath;
    const char *repos_name;
    const char *txn;
    const char *repos_path;
  } opts;
  opts.username = opts.fspath = opts.repos_name = opts.txn = NULL;
  opts.repos_path = NULL;

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
          err = svn_utf_cstring_to_utf8(&opts.username, arg, pool);
          if (err)
            {
              svn_handle_warning2(stderr, err, "svnauthz-validate: ");
              svn_error_clear(err);
              return 2;
            }
          break;
        case OPT_PATH:
          err = svn_utf_cstring_to_utf8(&opts.fspath, arg, pool);
          if (err)
            {
              svn_handle_warning2(stderr, err, "svnauthz-validate: ");
              svn_error_clear(err);
              return 2;
            }
          break;
        case OPT_REPOS:
          err = svn_utf_cstring_to_utf8(&opts.repos_name, arg, pool);
          if (err)
            {
              svn_handle_warning2(stderr, err, "svnauthz-validate: ");
              svn_error_clear(err);
              return 2;
            }
          break;
        case 't':
          err = svn_utf_cstring_to_utf8(&opts.txn, arg, pool);
          if (err)
            {
              svn_handle_warning2(stderr, err, "svnauthz-validate: ");
              svn_error_clear(err);
              return 2;
            }
          break;
        default:
          return usage(argv[0]);
        }
    }

  /* Consume a non-option argument (repos_path) if --transaction */
  if (opts.txn)
    {
      if (os->ind +2 != argc)
        return usage(argv[0]);

      err = svn_utf_cstring_to_utf8(&opts.repos_path, os->argv[os->ind], pool);
      if (err)
        {
          svn_handle_warning2(stderr, err, "svnauthz-validate: ");
          svn_error_clear(err);
          return 2;
        }
      os->ind++;

      opts.repos_path = svn_dirent_internal_style(opts.repos_path, pool);
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

  /* Can't accept repos relative urls since we don't have the path to the
   * repository and URLs don't need to be converted to internal style. */
  if (svn_path_is_repos_relative_url(opts.authz_file))
    return usage(argv[0]);
  else if (!svn_path_is_url(opts.authz_file))
    opts.authz_file = svn_dirent_internal_style(opts.authz_file, pool);
  else if (opts.txn) /* don't allow urls with transaction argument */
    return usage(argv[0]);

  /* Read the access file and validate it. */
  if (opts.txn)
    {
      err = get_authz_from_txn(&authz, opts.repos_path,
                               opts.authz_file, opts.txn, pool);
    }
  else
    {
      err = svn_repos_authz_read2(&authz, opts.authz_file, TRUE, NULL, pool);
    }

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

int
main(int argc, const char *argv[])
{
  apr_pool_t *pool;
  int exit_code;

  /* Initialize the app.  Send all error messages to 'stderr'.  */
  if (svn_cmdline_init(argv[0], stderr) != EXIT_SUCCESS)
    return 2;

  pool = svn_pool_create(NULL);

  exit_code = sub_main(argc, argv, pool);

  svn_pool_destroy(pool);
  return exit_code;
}
