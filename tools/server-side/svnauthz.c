/*
 * svnauthz.c : Tool for working with authz files.
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
 */

#include "svn_cmdline.h"
#include "svn_dirent_uri.h"
#include "svn_opt.h"
#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_utf.h"
#include "svn_path.h"


/*** Option Processing. ***/

enum svnauthz__cmdline_options_t
{
  svnauthz__version = SVN_OPT_FIRST_LONGOPT_ID,
  svnauthz__username,
  svnauthz__path,
  svnauthz__repos
};

/* Option codes and descriptions.
 *
 * The entire list must be terminated with an entry of nulls.
 */
static const apr_getopt_option_t options_table[] =
{
  {"help", 'h', 0, ("show help on a subcommand")},
  {NULL, '?', 0, ("show help on a subcommand")},
  {"version", svnauthz__version, 0, ("show program version information")},
  {"username", svnauthz__username, 1, ("username to check access of")},
  {"path", svnauthz__path, 1, ("path within repository to check access of")},
  {"repository", svnauthz__repos, 1, ("repository authz name")},
  {"transaction", 't', 1, ("transaction id")},
  {0, 0, 0, 0}
};

struct svnauthz_opt_state
{
  svn_boolean_t help;
  svn_boolean_t version;
  const char *authz_file;
  const char *username;
  const char *fspath;
  const char *repos_name;
  const char *txn;
  const char *repos_path;
};

#define SVNAUTHZ_COMPAT_NAME "svnauthz-validate"



/*** Subcommands. */

static svn_opt_subcommand_t
  subcommand_help,
  subcommand_validate,
  subcommand_accessof;

/* Array of available subcommands.
 * The entire list must be terminated with an entry of nulls.
 */
static const svn_opt_subcommand_desc2_t cmd_table[] =
{
  {"help", subcommand_help, {"?", "h"},
   ("usage: svnauthz help [SUBCOMMAND...]\n\n"
    "Describe the usage of this program or its subcommands.\n"),
   {0} },
  {"validate", subcommand_validate, {0} /* no aliases */,
   ("Checks the syntax of an authz file.\n"
    "usage: 1. svnauthz validate TARGET\n"
    "       2. svnauthz validate --transaction TXN REPOS_PATH FILE_PATH\n\n"
    "  1. Loads and validates the syntax of the authz file at TARGET.\n"
    "     TARGET can be a path to a file or an absolute file:// URL to an authz\n"
    "     file in a repository, but cannot be a repository relative URL (^/).\n\n"
    "  2. Loads and validates the syntax of the authz file at FILE_PATH in the\n"
    "     transaction TXN in the repository at REPOS_PATH.\n\n"
    "Returns:\n"
    "    0   when syntax is OK.\n"
    "    1   when syntax is invalid.\n"
    "    2   operational error\n"
    ),
   {'t'} },
  {"accessof", subcommand_accessof, {0} /* no aliases */,
   ("Output the permissions set by an authz file for a specific circumstance.\n"
    "usage: 1. svnauthz accessof [--username USER] TARGET\n"
    "       2. svnauthz accessof [--username USER] -t TXN REPOS_PATH FILE_PATH\n\n" 
    "  1. Prints the access of USER based on TARGET.\n"
    "     TARGET can be a path to a file or an absolute file:// URL to an authz\n"
    "     file in a repository, but cannot be a repository relative URL (^/).\n\n"
    "  2. Prints the access of USER based on authz file at FILE_PATH in the\n"
    "     transaction TXN in the repository at REPOS_PATH.\n\n"
    "  If the --username argument is ommitted then access of an anonymous user\n"
    "  will be printed.  If --path argument is ommitted prints if any access\n"
    "  to the repo is allowed.\n\n"
    "Outputs one of the following:\n"
    "     rw    write access (which also implies read)\n"
    "      r    read access\n"
    "     no    no access\n\n"
    "Returns:\n"
    "    0   when syntax is OK.\n"
    "    1   when syntax is invalid.\n"
    "    2   operational error\n"
    ),
   {'t', svnauthz__username, svnauthz__path, svnauthz__repos} },
};

static svn_error_t *
subcommand_help(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnauthz_opt_state *opt_state = baton;
  const char *header =
    ("general usage: svnauthz SUBCOMMAND TARGET [ARGS & OPTIONS ...]\n"
     "               " SVNAUTHZ_COMPAT_NAME " TARGET\n\n"
     "If the filename for the command starts with '" SVNAUTHZ_COMPAT_NAME "', runs in\n"
     "pre 1.8 compatability mode; which runs the validate subcommand on TARGET.\n\n"
     "Type 'svnauthz help <subcommand>' for help on a specific subcommand.\n"
     "Type 'svnauthz --version' to see the program version.\n\n"
     "Available subcommands:\n");

  const char *fs_desc_start
    = ("The following repository back-end (FS) modules are available:\n\n");

  svn_stringbuf_t *version_footer;

  version_footer = svn_stringbuf_create(fs_desc_start, pool);
  SVN_ERR(svn_fs_print_modules(version_footer, pool));

  SVN_ERR(svn_opt_print_help4(os, "svnauthz",
                              opt_state ? opt_state->version : FALSE,
                              FALSE, /* quiet */
                              FALSE, /* verbose */
                              version_footer->data,
                              header, cmd_table, options_table, NULL, NULL,
                              pool));

  return SVN_NO_ERROR;
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

/* Loads the authz config into *AUTHZ from OPT_STATE->AUTHZ_FILE.  If
   OPT_STATE->TXN is set then OPT_STATE->AUTHZ_FILE is treated as a fspath
   in repository at OPT_STATE->REPOS_PATH. */
static svn_error_t *
get_authz(svn_authz_t **authz, struct svnauthz_opt_state *opt_state,
          apr_pool_t *pool)
{
  /* Read the access file and validate it. */
  if (opt_state->txn)
    return get_authz_from_txn(authz, opt_state->repos_path,
                              opt_state->authz_file, opt_state->txn, pool);

  /* Else */
  return svn_repos_authz_read2(authz, opt_state->authz_file, TRUE, NULL, pool);
}

static svn_error_t *
subcommand_validate(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnauthz_opt_state *opt_state = baton;
  svn_authz_t *authz;

  /* Not much to do here since just loading the authz file also validates. */
  return get_authz(&authz, opt_state, pool);
}

static svn_error_t *
subcommand_accessof(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svn_authz_t *authz;
  svn_boolean_t read_access, write_access;
  svn_error_t *err;
  struct svnauthz_opt_state *opt_state = baton;
  const char *user = opt_state->username;
  const char *path = opt_state->fspath;
  const char *repos = opt_state->repos_name;
 
  SVN_ERR(get_authz(&authz, opt_state, pool));

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

  return err;
}



/*** Main. ***/

/* A redefinition of EXIT_FAILURE since our contract demands that we
   exit with 2 for internal failures. */
#undef EXIT_FAILURE
#define EXIT_FAILURE 2

/* Similar to svn_cmdline_handle_exit_error but with an exit_code argument
   so we can comply with our contract an exit with 2 for internal failures.
   Also is missing the pool argument since we don't need it given
   main/sub_main. */
static int
handle_exit_error(svn_error_t *err, const char *prefix, int exit_code)
{
  /* Issue #3014:
   * Don't print anything on broken pipes. The pipe was likely
   * closed by the process at the other end. We expect that
   * process to perform error reporting as necessary.
   *
   * ### This assumes that there is only one error in a chain for
   * ### SVN_ERR_IO_PIPE_WRITE_ERROR. See svn_cmdline_fputs(). */
  if (err->apr_err != SVN_ERR_IO_PIPE_WRITE_ERROR)
    svn_handle_error2(err, stderr, FALSE, prefix);
  svn_error_clear(err);
  return exit_code;
}

/* Report and clear the error ERR, and return EXIT_FAILURE. */
#define EXIT_ERROR(err, exit_code)                               \
  handle_exit_error(err, "svnauthz: ", exit_code)

/* A redefinition of the public SVN_INT_ERR macro, that suppresses the
 * error message if it is SVN_ERR_IO_PIPE_WRITE_ERROR, amd with the
 * program name 'svnauthz' instead of 'svn'. */
#undef SVN_INT_ERR
#define SVN_INT_ERR(expr)                                        \
  do {                                                           \
    svn_error_t *svn_err__temp = (expr);                         \
    if (svn_err__temp)                                           \
      return EXIT_ERROR(svn_err__temp, EXIT_FAILURE);            \
  } while (0)


static svn_boolean_t
use_compat_mode(const char *cmd, apr_pool_t *pool)
{
  cmd = svn_dirent_internal_style(cmd, pool);
  cmd = svn_dirent_basename(cmd, NULL);

  /* Deliberately look only for the start of the name to deal with
     the executable extension on some platforms. */
  return 0 == strncmp(SVNAUTHZ_COMPAT_NAME, cmd,
                      strlen(SVNAUTHZ_COMPAT_NAME));
}

static int
sub_main(int argc, const char *argv[], apr_pool_t *pool)
{
  svn_error_t *err;

  const svn_opt_subcommand_desc2_t *subcommand = NULL;
  struct svnauthz_opt_state opt_state = { 0 };
  apr_getopt_t *os;
  apr_array_header_t *received_opts;
  int i;

  /* Initialize the FS library. */
  SVN_INT_ERR(svn_fs_initialize(pool));

  received_opts = apr_array_make(pool, SVN_OPT_MAX_OPTIONS, sizeof(int));

  /* Initialize opt_state */
  opt_state.username = opt_state.fspath = opt_state.repos_name = NULL;
  opt_state.txn = opt_state.repos_path = NULL;

  /* Parse options. */
  SVN_INT_ERR(svn_cmdline__getopt_init(&os, argc, argv, pool));
  os->interleave = 1;

  if (!use_compat_mode(argv[0], pool))
    {
      while (1)
        {
          int opt;
          const char *arg;
          apr_status_t status = apr_getopt_long(os, options_table, &opt, &arg);

          if (APR_STATUS_IS_EOF(status))
            break;
          if (status != APR_SUCCESS)
            {
              SVN_INT_ERR(subcommand_help(NULL, NULL, pool));
              return EXIT_FAILURE;
            }

          /* Stash the option code in an array before parsing it. */
          APR_ARRAY_PUSH(received_opts, int) = opt;

          switch (opt)
            {
            case 'h':
            case '?':
              opt_state.help = TRUE;
              break;
            case 't':
              SVN_INT_ERR(svn_utf_cstring_to_utf8(&opt_state.txn, arg, pool));
              break;
            case svnauthz__version:
              opt_state.version = TRUE;
              break;
            case svnauthz__username:
              SVN_INT_ERR(svn_utf_cstring_to_utf8(&opt_state.username, arg, pool));
              break;
            case svnauthz__path:
              SVN_INT_ERR(svn_utf_cstring_to_utf8(&opt_state.fspath, arg, pool));
              break;
            case svnauthz__repos:
              SVN_INT_ERR(svn_utf_cstring_to_utf8(&opt_state.repos_name, arg, pool));
              break;
            default:
                {
                  SVN_INT_ERR(subcommand_help(NULL, NULL, pool));
                  return EXIT_FAILURE;
                }
            }
        }
    }
  else
    {
      /* Pre 1.8 compatability mode. */
      if (argc == 1) /* No path argument */
        subcommand = svn_opt_get_canonical_subcommand2(cmd_table, "help");
      else
        subcommand = svn_opt_get_canonical_subcommand2(cmd_table, "validate");
    }

  /* If the user asked for help, then the rest of the arguments are
     the names of subcommands to get help on (if any), or else they're
     just typos/mistakes.  Whatever the case, the subcommand to
     actually run is subcommand_help(). */
  if (opt_state.help)
    subcommand = svn_opt_get_canonical_subcommand2(cmd_table, "help");

  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          if (opt_state.version)
            {
              /* Use the "help" subcommand to handle the "--version" option. */
              static const svn_opt_subcommand_desc2_t pseudo_cmd = 
                { "--version", subcommand_help, {0}, "",
                  {svnauthz__version /* must accept its own option */ } };

              subcommand = &pseudo_cmd;
            }
          else
            {
              svn_error_clear(svn_cmdline_fprintf(stderr, pool,
                                        ("subcommand argument required\n")));
              SVN_INT_ERR(subcommand_help(NULL, NULL, pool));
              return EXIT_FAILURE;
            }
        }
      else
        {
          const char *first_arg = os->argv[os->ind++];
          subcommand = svn_opt_get_canonical_subcommand2(cmd_table, first_arg);
          if (subcommand == NULL)
            {
              const char *first_arg_utf8;

              os->ind++;
              SVN_INT_ERR(svn_utf_cstring_to_utf8(&first_arg_utf8,
                                                  first_arg, pool));
              svn_error_clear(svn_cmdline_fprintf(stderr, pool,
                                                  ("Unknown command: '%s'\n"),
                                                  first_arg_utf8));
              SVN_INT_ERR(subcommand_help(NULL, NULL, pool));
              return EXIT_FAILURE;
            }
        }
    }

  /* Every subcommand except `help' requires one or two non-option arguments.
     Parse them and store them in opt_state.*/
  if (subcommand->cmd_func != subcommand_help)
    {
      /* Consume a non-option argument (repos_path) if --transaction */
      if (opt_state.txn)
        {
          if (os->ind +2 != argc)
            {
              err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                     ("Repository and authz file arguments "
                                      "required"));
              return EXIT_ERROR(err, EXIT_FAILURE); 
            }

          SVN_INT_ERR(svn_utf_cstring_to_utf8(&opt_state.repos_path, os->argv[os->ind],
                                              pool));
          os->ind++;

          opt_state.repos_path = svn_dirent_internal_style(opt_state.repos_path, pool);
        }

      /* Exactly 1 non-option argument */
      if (os->ind + 1 != argc)
        {
          err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 ("Authz file argument required"));
          return EXIT_ERROR(err, EXIT_FAILURE); 
        }

      /* Grab AUTHZ_FILE from argv. */
      SVN_INT_ERR(svn_utf_cstring_to_utf8(&opt_state.authz_file, os->argv[os->ind],
                                          pool));

      /* Can't accept repos relative urls since we don't have the path to the
       * repository and URLs don't need to be converted to internal style. */
      if (svn_path_is_repos_relative_url(opt_state.authz_file))
        {
          err = svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                  ("'%s' is a repository relative URL when it "
                                   "should be a local path or file:// URL"),
                                  opt_state.authz_file);
          return EXIT_ERROR(err, EXIT_FAILURE);
        }
      else if (!svn_path_is_url(opt_state.authz_file))
        opt_state.authz_file = svn_dirent_internal_style(opt_state.authz_file, pool);
      else if (opt_state.txn) /* don't allow urls with transaction argument */
        {
          err = svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                  ("'%s' is a URL when it should be a "
                                   "local path"), opt_state.authz_file);
          return EXIT_ERROR(err, EXIT_FAILURE);
        }
    }

  /* Check that the subcommand wasn't passed any inappropriate options. */
  for (i = 0; i < received_opts->nelts; i++)
    {
      int opt_id = APR_ARRAY_IDX(received_opts, i, int);

      /* All commands implicitly accept --help, so just skip over this
         when we see it. Note that we don't want to include this option
         in their "accepted options" list because it would be awfully
         redundant to display it in every commands' help text. */
      if (opt_id == 'h' || opt_id == '?')
        continue;

      if (! svn_opt_subcommand_takes_option3(subcommand, opt_id, NULL))
        {
          const char *optstr;
          const apr_getopt_option_t *badopt =
            svn_opt_get_option_from_code2(opt_id, options_table, subcommand,
                                          pool);
          svn_opt_format_option(&optstr, badopt, FALSE, pool);
          if (subcommand->name[0] == '-')
            SVN_INT_ERR(subcommand_help(NULL, NULL, pool));
          else
            svn_error_clear(svn_cmdline_fprintf(stderr, pool,
                            ("Subcommand '%s' doesn't accept option '%s'\n"
                             "Type 'svnauthz help %s' for usage.\n"),
                            subcommand->name, optstr, subcommand->name));
          return EXIT_FAILURE;
        }
    }

  /* Run the subcommand. */
  err = (*subcommand->cmd_func)(os, &opt_state, pool);

  if (err)
    {
      if (err->apr_err == SVN_ERR_CL_INSUFFICIENT_ARGS
          || err->apr_err == SVN_ERR_CL_ARG_PARSING_ERROR)
        {
          /* For argument-related problems, suggest using the 'help'
             subcommand. */
          err = svn_error_quick_wrap(err,
                                     ("Try 'svnauthz help' for more info"));
        }
      else if (err->apr_err == SVN_ERR_AUTHZ_INVALID_CONFIG
               || err->apr_err == SVN_ERR_MALFORMED_FILE)
        {
          /* Follow our contract that says we exit with 1 if the file does not
             validate. */
          return EXIT_ERROR(err, 1);
        }

      return EXIT_ERROR(err, EXIT_FAILURE);
    }
  else
    {
      /* Ensure that everything is written to stdout, so the user will
         see any print errors. */
      err = svn_cmdline_fflush(stdout);
      if (err)
        {
          return EXIT_ERROR(err, EXIT_FAILURE);
        }
      return EXIT_SUCCESS;
    }

}

int
main(int argc, const char *argv[])
{
  apr_pool_t *pool;
  int exit_code;

  /* Initialize the app.  Send all error messages to 'stderr'.  */
  if (svn_cmdline_init(argv[0], stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  pool = svn_pool_create(NULL);

  exit_code = sub_main(argc, argv, pool);

  svn_pool_destroy(pool);
  return exit_code;
}
