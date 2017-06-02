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

#include "private/svn_fspath.h"
#include "private/svn_cmdline_private.h"


/*** Option Processing. ***/

enum svnauthz__cmdline_options_t
{
  svnauthz__version = SVN_OPT_FIRST_LONGOPT_ID,
  svnauthz__username,
  svnauthz__path,
  svnauthz__repos,
  svnauthz__is,
  svnauthz__groups_file
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
  {"is", svnauthz__is, 1,
    ("instead of outputting, test if the access is\n"
     "                             "
     "exactly ARG\n"
     "                             "
     "ARG can be one of the following values:\n"
     "                             "
     "   rw    write access (which also implies read)\n"
     "                             "
     "    r    read-only access\n"
     "                             "
     "   no    no access")
  },
  {"groups-file", svnauthz__groups_file, 1,
   ("use the groups from file ARG")},
  {"recursive", 'R', 0,
   ("determine recursive access to PATH")},
  {0, 0, 0, 0}
};

struct svnauthz_opt_state
{
  svn_boolean_t help;
  svn_boolean_t version;
  svn_boolean_t recursive;
  const char *authz_file;
  const char *groups_file;
  const char *username;
  const char *fspath;
  const char *repos_name;
  const char *txn;
  const char *repos_path;
  const char *is;
};

/* The name of this binary in 1.7 and earlier. */
#define SVNAUTHZ_COMPAT_NAME "svnauthz-validate"

/* Libtool command prefix */
#define SVNAUTHZ_LT_PREFIX "lt-"


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
   ("Print or test the permissions set by an authz file.\n"
    "usage: 1. svnauthz accessof TARGET\n"
    "       2. svnauthz accessof -t TXN REPOS_PATH FILE_PATH\n"
    "\n"
    "  1. Prints the access of USER to PATH based on authorization file at TARGET.\n"
    "     TARGET can be a path to a file or an absolute file:// URL to an authz\n"
    "     file in a repository, but cannot be a repository relative URL (^/).\n"
    "\n"
    "  2. Prints the access of USER to PATH based on authz file at FILE_PATH in the\n"
    "     transaction TXN in the repository at REPOS_PATH.\n"
    "\n"
    "  USER is the argument to the --username option; if that option is not\n"
    "  provided, then access of an anonymous user will be printed or tested.\n"
    "\n"
    "  PATH is the argument to the --path option; if that option is not provided,\n"
    "  the maximal access to any path in the repository will be considered.\n"
    "\n"
    "Outputs one of the following:\n"
    "     rw    write access (which also implies read)\n"
    "      r    read access\n"
    "     no    no access\n"
    "\n"
    "Returns:\n"
    "    0   when syntax is OK and '--is' argument (if any) matches.\n"
    "    1   when syntax is invalid.\n"
    "    2   operational error\n"
    "    3   when '--is' argument doesn't match\n"
    ),
   {'t', svnauthz__username, svnauthz__path, svnauthz__repos, svnauthz__is,
    svnauthz__groups_file, 'R'} },
  { NULL, NULL, {0}, NULL, {0} }
};

static svn_error_t *
subcommand_help(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnauthz_opt_state *opt_state = baton;
  const char *header =
    ("general usage: svnauthz SUBCOMMAND TARGET [ARGS & OPTIONS ...]\n"
     "               " SVNAUTHZ_COMPAT_NAME " TARGET\n\n"
     "If the command name starts with '" SVNAUTHZ_COMPAT_NAME "', runs in\n"
     "pre-1.8 compatibility mode: run the 'validate' subcommand on TARGET.\n\n"
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

/* Loads the fs FILENAME contents into *CONTENTS ensuring that the
   corresponding node is a file. Using POOL for allocations. */
static svn_error_t *
read_file_contents(svn_stream_t **contents, const char *filename,
                   svn_fs_root_t *root, apr_pool_t *pool)
{
  svn_node_kind_t node_kind;

  /* Make sure the path is a file */
  SVN_ERR(svn_fs_check_path(&node_kind, root, filename, pool));
  if (node_kind != svn_node_file)
    return svn_error_createf(SVN_ERR_FS_NOT_FILE, NULL,
                             "Path '%s' is not a file", filename);

  SVN_ERR(svn_fs_file_contents(contents, root, filename, pool));

  return SVN_NO_ERROR;
}

/* Loads the authz config into *AUTHZ from the file at AUTHZ_FILE
   in repository at REPOS_PATH from the transaction TXN_NAME.  If GROUPS_FILE
   is set, the resulting *AUTHZ will be constructed from AUTHZ_FILE with
   global groups taken from GROUPS_FILE.  Using POOL for allocations. */
static svn_error_t *
get_authz_from_txn(svn_authz_t **authz, const char *repos_path,
                   const char *authz_file, const char *groups_file,
                   const char *txn_name, apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *root;
  svn_stream_t *authz_contents;
  svn_stream_t *groups_contents;
  svn_error_t *err;

  /* Open up the repository and find the transaction root */
  SVN_ERR(svn_repos_open3(&repos, repos_path, NULL, pool, pool));
  fs = svn_repos_fs(repos);
  SVN_ERR(svn_fs_open_txn(&txn, fs, txn_name, pool));
  SVN_ERR(svn_fs_txn_root(&root, txn, pool));

  /* Get the authz file contents. */
  SVN_ERR(read_file_contents(&authz_contents, authz_file, root, pool));

  /* Get the groups file contents if needed. */
  if (groups_file)
    SVN_ERR(read_file_contents(&groups_contents, groups_file, root, pool));
  else
    groups_contents = NULL;

  err = svn_repos_authz_parse(authz, authz_contents, groups_contents, pool);

  /* Add the filename to the error stack since the parser doesn't have it. */
  if (err != SVN_NO_ERROR)
    return svn_error_createf(err->apr_err, err,
                             "Error parsing authz file: '%s':", authz_file);

  return SVN_NO_ERROR;
}

/* Loads the authz config into *AUTHZ from OPT_STATE->AUTHZ_FILE.  If
   OPT_STATE->GROUPS_FILE is set, loads the global groups from it.
   If OPT_STATE->TXN is set then OPT_STATE->AUTHZ_FILE and
   OPT_STATE->GROUPS_FILE are treated as fspaths in repository at
   OPT_STATE->REPOS_PATH. */
static svn_error_t *
get_authz(svn_authz_t **authz, struct svnauthz_opt_state *opt_state,
          apr_pool_t *pool)
{
  /* Read the access file and validate it. */
  if (opt_state->txn)
    return get_authz_from_txn(authz, opt_state->repos_path,
                              opt_state->authz_file,
                              opt_state->groups_file,
                              opt_state->txn, pool);

  /* Else */
  return svn_repos_authz_read3(authz, opt_state->authz_file,
                               opt_state->groups_file,
                               TRUE, NULL, pool, pool);
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
  svn_boolean_t read_access = FALSE, write_access = FALSE;
  svn_boolean_t check_r = FALSE, check_rw = FALSE, check_no = FALSE;
  svn_error_t *err;
  struct svnauthz_opt_state *opt_state = baton;
  const char *user = opt_state->username;
  const char *path = opt_state->fspath;
  const char *repos = opt_state->repos_name;
  const char *is = opt_state->is;
  svn_repos_authz_access_t request;

  if (opt_state->recursive && !path)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            ("--recursive not valid without --path"));

  /* Handle is argument parsing/allowed values */
  if (is) {
      if (0 == strcmp(is, "rw"))
        check_rw = TRUE;
      else if (0 == strcmp(is, "r"))
        check_r = TRUE;
      else if (0 == strcmp(is, "no"))
        check_no = TRUE;
      else
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 ("'%s' is not a valid argument for --is"), is);
  }

  SVN_ERR(get_authz(&authz, opt_state, pool));


  request = svn_authz_write;
  if (opt_state->recursive)
    request |= svn_authz_recursive;
  err = svn_repos_authz_check_access(authz, repos, path, user,
                                     request, &write_access,
                                     pool);

  if (!write_access && !err)
    {
      request = svn_authz_read;
      if (opt_state->recursive)
        request |= svn_authz_recursive;
      err = svn_repos_authz_check_access(authz, repos, path, user,
                                         request, &read_access,
                                         pool);
    }

  if (!err)
    {
      const char *access_str = write_access ? "rw" : read_access ? "r" : "no";

      if (is)
        {
          /* Check that --is argument matches.
           * The errors returned here are not strictly correct, but
           * none of the other code paths will generate them and they
           * roughly mean what we're saying here. */
          if (check_rw && !write_access)
            err = svn_error_createf(SVN_ERR_AUTHZ_UNWRITABLE, NULL,
                                    ("%s is '%s', not writable"),
                                    path ? path : ("Repository"), access_str);
          else if (check_r && !read_access)
            err = svn_error_createf(SVN_ERR_AUTHZ_UNREADABLE, NULL,
                                    ("%s is '%s', not read only"),
                                    path ? path : ("Repository"), access_str);
          else if (check_no && (read_access || write_access))
            err = svn_error_createf(SVN_ERR_AUTHZ_PARTIALLY_READABLE,
                                    NULL, ("%s is '%s', not no access"),
                                    path ? path : ("Repository"), access_str);
        }
      else
        {
          err = svn_cmdline_printf(pool, "%s\n", access_str);
        }
    }

  return err;
}



/*** Main. ***/

/* A redefinition of EXIT_FAILURE since our contract demands that we
   exit with 2 for internal failures. */
#undef EXIT_FAILURE
#define EXIT_FAILURE 2

/* Return TRUE if the UI of 'svnauthz-validate' (svn 1.7 and earlier)
   should be emulated, given argv[0]. */
static svn_boolean_t
use_compat_mode(const char *cmd, apr_pool_t *pool)
{
  cmd = svn_dirent_internal_style(cmd, pool);
  cmd = svn_dirent_basename(cmd, NULL);

  /* Skip over the Libtool command prefix if it exists on the command. */
  if (0 == strncmp(SVNAUTHZ_LT_PREFIX, cmd, sizeof(SVNAUTHZ_LT_PREFIX)-1))
    cmd += sizeof(SVNAUTHZ_LT_PREFIX) - 1;

  /* Deliberately look only for the start of the name to deal with
     the executable extension on some platforms. */
  return 0 == strncmp(SVNAUTHZ_COMPAT_NAME, cmd,
                      sizeof(SVNAUTHZ_COMPAT_NAME)-1);
}

/* Canonicalize ACCESS_FILE into *CANONICALIZED_ACCESS_FILE based on the type
   of argument.  Error out on unsupported path types.  If WITHIN_TXN is set,
   ACCESS_FILE has to be a fspath in the repo.  Use POOL for allocations. */
static svn_error_t *
canonicalize_access_file(const char **canonicalized_access_file,
                         const char *access_file,
                         svn_boolean_t within_txn,
                         apr_pool_t *pool)
{
  if (svn_path_is_repos_relative_url(access_file))
    {
      /* Can't accept repos relative urls since we don't have the path to
       * the repository. */
      return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                               ("'%s' is a repository relative URL when it "
                               "should be a local path or file:// URL"),
                               access_file);
    }
  else if (svn_path_is_url(access_file))
    {
      if (within_txn)
        {
          /* Don't allow urls with transaction argument. */
          return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                   ("'%s' is a URL when it should be a "
                                   "repository-relative path"),
                                   access_file);
        }

      *canonicalized_access_file = svn_uri_canonicalize(access_file, pool);
    }
  else if (within_txn)
    {
      /* Transaction flag means this has to be a fspath to the access file
       * in the repo. */
      *canonicalized_access_file =
          svn_fspath__canonicalize(access_file, pool);
    }
  else
    {
      /* If it isn't a URL and there's no transaction flag then it's a
       * dirent to the access file on local disk. */
      *canonicalized_access_file =
          svn_dirent_internal_style(access_file, pool);
    }

  return SVN_NO_ERROR;
}

/*
 * On success, leave *EXIT_CODE untouched and return SVN_NO_ERROR. On error,
 * either return an error to be displayed, or set *EXIT_CODE to non-zero and
 * return SVN_NO_ERROR.
 */
static svn_error_t *
sub_main(int *exit_code, int argc, const char *argv[], apr_pool_t *pool)
{
  svn_error_t *err;

  const svn_opt_subcommand_desc2_t *subcommand = NULL;
  struct svnauthz_opt_state opt_state = { 0 };
  apr_getopt_t *os;
  apr_array_header_t *received_opts;
  int i;

  /* Initialize the FS library. */
  SVN_ERR(svn_fs_initialize(pool));

  received_opts = apr_array_make(pool, SVN_OPT_MAX_OPTIONS, sizeof(int));

  /* Initialize opt_state */
  opt_state.username = opt_state.fspath = opt_state.repos_name = NULL;
  opt_state.txn = opt_state.repos_path = opt_state.groups_file = NULL;

  /* Parse options. */
  SVN_ERR(svn_cmdline__getopt_init(&os, argc, argv, pool));
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
              SVN_ERR(subcommand_help(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
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
              SVN_ERR(svn_utf_cstring_to_utf8(&opt_state.txn, arg, pool));
              break;
            case 'R':
              opt_state.recursive = TRUE;
              break;
            case svnauthz__version:
              opt_state.version = TRUE;
              break;
            case svnauthz__username:
              SVN_ERR(svn_utf_cstring_to_utf8(&opt_state.username, arg, pool));
              break;
            case svnauthz__path:
              SVN_ERR(svn_utf_cstring_to_utf8(&opt_state.fspath, arg, pool));
              opt_state.fspath = svn_fspath__canonicalize(opt_state.fspath,
                                                          pool);
              break;
            case svnauthz__repos:
              SVN_ERR(svn_utf_cstring_to_utf8(&opt_state.repos_name, arg, pool));
              break;
            case svnauthz__is:
              SVN_ERR(svn_utf_cstring_to_utf8(&opt_state.is, arg, pool));
              break;
            case svnauthz__groups_file:
              SVN_ERR(
                  svn_utf_cstring_to_utf8(&opt_state.groups_file,
                                          arg, pool));
              break;
            default:
                {
                  SVN_ERR(subcommand_help(NULL, NULL, pool));
                  *exit_code = EXIT_FAILURE;
                  return SVN_NO_ERROR;
                }
            }
        }
    }
  else
    {
      /* Pre 1.8 compatibility mode. */
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
              SVN_ERR(subcommand_help(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
            }
        }
      else
        {
          const char *first_arg;

          SVN_ERR(svn_utf_cstring_to_utf8(&first_arg, os->argv[os->ind++],
                                          pool));
          subcommand = svn_opt_get_canonical_subcommand2(cmd_table, first_arg);
          if (subcommand == NULL)
            {
              os->ind++;
              svn_error_clear(
                svn_cmdline_fprintf(stderr, pool,
                                    ("Unknown subcommand: '%s'\n"),
                                    first_arg));
              SVN_ERR(subcommand_help(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
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
              return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                      ("Repository and authz file arguments "
                                       "required"));
            }

          SVN_ERR(svn_utf_cstring_to_utf8(&opt_state.repos_path, os->argv[os->ind],
                                              pool));
          os->ind++;

          opt_state.repos_path = svn_dirent_internal_style(opt_state.repos_path, pool);
        }

      /* Exactly 1 non-option argument */
      if (os->ind + 1 != argc)
        {
          return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                  ("Authz file argument required"));
        }

      /* Grab AUTHZ_FILE from argv. */
      SVN_ERR(svn_utf_cstring_to_utf8(&opt_state.authz_file, os->argv[os->ind],
                                          pool));

      /* Canonicalize opt_state.authz_file appropriately. */
      SVN_ERR(canonicalize_access_file(&opt_state.authz_file,
                                           opt_state.authz_file,
                                           opt_state.txn != NULL, pool));

      /* Same for opt_state.groups_file if it is present. */
      if (opt_state.groups_file)
        {
          SVN_ERR(canonicalize_access_file(&opt_state.groups_file,
                                               opt_state.groups_file,
                                               opt_state.txn != NULL, pool));
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
            SVN_ERR(subcommand_help(NULL, NULL, pool));
          else
            svn_error_clear(svn_cmdline_fprintf(stderr, pool,
                            ("Subcommand '%s' doesn't accept option '%s'\n"
                             "Type 'svnauthz help %s' for usage.\n"),
                            subcommand->name, optstr, subcommand->name));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
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
          *exit_code = 1;
          return err;
        }
      else if (err->apr_err == SVN_ERR_AUTHZ_UNREADABLE
               || err->apr_err == SVN_ERR_AUTHZ_UNWRITABLE
               || err->apr_err == SVN_ERR_AUTHZ_PARTIALLY_READABLE)
        {
          /* Follow our contract that says we exit with 3 if --is does not
           * match. */
          *exit_code = 3;
          return err;
        }

      return err;
    }

  return SVN_NO_ERROR;
}

int
main(int argc, const char *argv[])
{
  apr_pool_t *pool;
  int exit_code = EXIT_SUCCESS;
  svn_error_t *err;

  /* Initialize the app.  Send all error messages to 'stderr'.  */
  if (svn_cmdline_init(argv[0], stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  pool = svn_pool_create(NULL);

  err = sub_main(&exit_code, argc, argv, pool);

  /* Flush stdout and report if it fails. It would be flushed on exit anyway
     but this makes sure that output is not silently lost if it fails. */
  err = svn_error_compose_create(err, svn_cmdline_fflush(stdout));

  if (err)
    {
      if (exit_code == 0)
        exit_code = EXIT_FAILURE;
      svn_cmdline_handle_exit_error(err, NULL, "svnauthz: ");
    }

  svn_pool_destroy(pool);
  return exit_code;
}
