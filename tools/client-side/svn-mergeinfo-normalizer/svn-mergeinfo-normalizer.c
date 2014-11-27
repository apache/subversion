/*
 * svn-mergeinfo-normalizer.c:  MI normalization tool main file.
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

/* ==================================================================== */



/*** Includes. ***/

#include <string.h>
#include <assert.h>

#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_general.h>
#include <apr_signal.h>

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_config.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_diff.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_opt.h"
#include "svn_utf.h"
#include "svn_auth.h"
#include "svn_hash.h"
#include "svn_version.h"
#include "mergeinfo-normalizer.h"

#include "private/svn_opt_private.h"
#include "private/svn_cmdline_private.h"
#include "private/svn_subr_private.h"

#include "svn_private_config.h"


/*** Option Processing ***/

/* Add an identifier here for long options that don't have a short
   option. Options that have both long and short options should just
   use the short option letter as identifier.  */
typedef enum svn_min__longopt_t {
  opt_auth_password = SVN_OPT_FIRST_LONGOPT_ID,
  opt_auth_username,
  opt_config_dir,
  opt_config_options,
  opt_dry_run,
  opt_no_auth_cache,
  opt_targets,
  opt_depth,
  opt_version,
  opt_non_interactive,
  opt_force_interactive,
  opt_trust_server_cert,
  opt_trust_server_cert_unknown_ca,
  opt_trust_server_cert_cn_mismatch,
  opt_trust_server_cert_expired,
  opt_trust_server_cert_not_yet_valid,
  opt_trust_server_cert_other_failure,
  opt_allow_mixed_revisions,
} svn_cl__longopt_t;


/* Option codes and descriptions for the command line client.
 *
 * The entire list must be terminated with an entry of nulls.
 */
const apr_getopt_option_t svn_min__options[] =
{
  {"help",          'h', 0, N_("show help on a subcommand")},
  {NULL,            '?', 0, N_("show help on a subcommand")},
  {"quiet",         'q', 0, N_("print nothing, or only summary information")},
  {"version",       opt_version, 0, N_("show program version information")},
  {"verbose",       'v', 0, N_("print extra information")},
  {"username",      opt_auth_username, 1, N_("specify a username ARG")},
  {"password",      opt_auth_password, 1,
                    N_("specify a password ARG (caution: on many operating\n"
                       "                             "
                       "systems, other users will be able to see this)")},
  {"targets",       opt_targets, 1,
                    N_("pass contents of file ARG as additional args")},
  {"depth",         opt_depth, 1,
                    N_("limit operation by depth ARG ('empty', 'files',\n"
                       "                             "
                       "'immediates', or 'infinity')")},
  {"no-auth-cache", opt_no_auth_cache, 0,
                    N_("do not cache authentication tokens")},
  {"trust-server-cert", opt_trust_server_cert, 0,
                    N_("deprecated; same as --trust-unknown-ca")},
  {"trust-unknown-ca", opt_trust_server_cert_unknown_ca, 0,
                    N_("with --non-interactive, accept SSL server\n"
                       "                             "
                       "certificates from unknown certificate authorities")},
  {"trust-cn-mismatch", opt_trust_server_cert_cn_mismatch, 0,
                    N_("with --non-interactive, accept SSL server\n"
                       "                             "
                       "certificates even if the server hostname does not\n"
                       "                             "
                       "match the certificate's common name attribute")},
  {"trust-expired", opt_trust_server_cert_expired, 0,
                    N_("with --non-interactive, accept expired SSL server\n"
                       "                             "
                       "certificates")},
  {"trust-not-yet-valid", opt_trust_server_cert_not_yet_valid, 0,
                    N_("with --non-interactive, accept SSL server\n"
                       "                             "
                       "certificates from the future")},
  {"trust-other-failure", opt_trust_server_cert_other_failure, 0,
                    N_("with --non-interactive, accept SSL server\n"
                       "                             "
                       "certificates with failures other than the above")},
  {"non-interactive", opt_non_interactive, 0,
                    N_("do no interactive prompting (default is to prompt\n"
                       "                             "
                       "only if standard input is a terminal device)")},
  {"force-interactive", opt_force_interactive, 0,
                    N_("do interactive prompting even if standard input\n"
                       "                             "
                       "is not a terminal device")},
  {"dry-run",       opt_dry_run, 0,
                    N_("try operation but make no changes")},
  {"config-dir",    opt_config_dir, 1,
                    N_("read user configuration files from directory ARG")},
  {"config-option", opt_config_options, 1,
                    N_("set user configuration option in the format:\n"
                       "                             "
                       "    FILE:SECTION:OPTION=[VALUE]\n"
                       "                             "
                       "For example:\n"
                       "                             "
                       "    servers:global:http-library=serf")},
  {"use-merge-history", 'g', 0,
                    N_("use/display additional information from merge\n"
                       "                             "
                       "history")},
  {"allow-mixed-revisions", opt_allow_mixed_revisions, 0,
                       N_("Allow operation on mixed-revision working copy.\n"
                       "                             "
                       "Use of this option is not recommended!\n"
                       "                             "
                       "Please run 'svn update' instead.")},

  {0,               0, 0, 0},
};



/*** Command dispatch. ***/

/* Our array of available subcommands.
 *
 * The entire list must be terminated with an entry of nulls.
 *
 * In most of the help text "PATH" is used where a working copy path is
 * required, "URL" where a repository URL is required and "TARGET" when
 * either a path or a url can be used.  Hmm, should this be part of the
 * help text?
 */

/* Options that apply to all commands.  (While not every command may
   currently require authentication or be interactive, allowing every
   command to take these arguments allows scripts to just pass them
   willy-nilly to every invocation of 'svn') . */
const int svn_min__global_options[] =
{ opt_auth_username, opt_auth_password, opt_no_auth_cache, opt_non_interactive,
  opt_force_interactive, opt_trust_server_cert,
  opt_trust_server_cert_unknown_ca, opt_trust_server_cert_cn_mismatch,
  opt_trust_server_cert_expired, opt_trust_server_cert_not_yet_valid,
  opt_trust_server_cert_other_failure,
  opt_config_dir, opt_config_options, 0
};

/* Options for giving a log message.  (Some of these also have other uses.)
 */
#define SVN_CL__LOG_MSG_OPTIONS 'm', 'F', \
                                opt_force_log, \
                                opt_editor_cmd, \
                                opt_encoding, \
                                opt_with_revprop

const svn_opt_subcommand_desc2_t svn_min__cmd_table[] =
{
  { "help", svn_min__help, {"?", "h"}, N_
    ("Describe the usage of this program or its subcommands.\n"
     "usage: help [SUBCOMMAND...]\n"),
    {0} },

  /* This command is also invoked if we see option "--help", "-h" or "-?". */

  { "normalize", svn_min__normalize, { 0 }, N_
    ("Normalize the mergeinfo throughout the working copy sub-tree.\n"
     "usage: normalize [WCPATH...]\n"),
    {opt_targets, opt_depth, opt_dry_run, 'q'} },

  { "clear-obsoletes", svn_min__clear_obsolete, { 0 }, N_
    ("Remove mergeinfo that refers to branches that no longer exist.\n"
     "usage: clear-obsoletes [WCPATH...]\n"),
    {opt_targets, opt_depth, opt_dry_run, 'q'} },

  { "combine-ranges", svn_min__combine_ranges, { 0 }, N_
    ("Combine revision ranges if all revisions in between are inoperative.\n"
     "usage: remove-ranges [WCPATH...]\n"),
    {opt_targets, opt_depth, opt_dry_run, 'q'} },

  { "analyze", svn_min__analyze, { "analyse" }, N_
    ("Generate a report of which part of the sub-tree mergeinfo\n"
     "can be removed and which part can't.\n"
     "usage: remove-ranges [WCPATH...]\n"),
    {opt_targets, opt_depth, opt_dry_run} },

  { NULL, NULL, {0}, NULL, {0} }
};


/* Version compatibility check */
static svn_error_t *
check_lib_versions(void)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",   svn_subr_version },
      { "svn_client", svn_client_version },
      { "svn_wc",     svn_wc_version },
      { "svn_ra",     svn_ra_version },
      { "svn_delta",  svn_delta_version },
      { "svn_diff",   svn_diff_version },
      { NULL, NULL }
    };
  SVN_VERSION_DEFINE(my_version);

  return svn_ver_check_list2(&my_version, checklist, svn_ver_equal);
}


/* A flag to see if we've been cancelled by the client or not. */
static volatile sig_atomic_t cancelled = FALSE;

/* A signal handler to support cancellation. */
static void
signal_handler(int signum)
{
  apr_signal(signum, SIG_IGN);
  cancelled = TRUE;
}

/* Our cancellation callback. */
svn_error_t *
svn_min__check_cancel(void *baton)
{
  /* Cancel baton should be always NULL in command line client. */
  SVN_ERR_ASSERT(baton == NULL);
  if (cancelled)
    return svn_error_create(SVN_ERR_CANCELLED, NULL, _("Caught signal"));
  else
    return SVN_NO_ERROR;
}


/*** Main. ***/

/*
 * On success, leave *EXIT_CODE untouched and return SVN_NO_ERROR. On error,
 * either return an error to be displayed, or set *EXIT_CODE to non-zero and
 * return SVN_NO_ERROR.
 */
static svn_error_t *
sub_main(int *exit_code, int argc, const char *argv[], apr_pool_t *pool)
{
  svn_error_t *err;
  int opt_id;
  apr_getopt_t *os;
  svn_min__opt_state_t opt_state = { 0 };
  svn_client_ctx_t *ctx;
  apr_array_header_t *received_opts;
  int i;
  const svn_opt_subcommand_desc2_t *subcommand = NULL;
  svn_min__cmd_baton_t command_baton;
  svn_auth_baton_t *ab;
  svn_config_t *cfg_config;
  svn_boolean_t interactive_conflicts = FALSE;
  svn_boolean_t force_interactive = FALSE;
  apr_hash_t *cfg_hash;

  received_opts = apr_array_make(pool, SVN_OPT_MAX_OPTIONS, sizeof(int));

  /* Check library versions */
  SVN_ERR(check_lib_versions());

#if defined(WIN32) || defined(__CYGWIN__)
  /* Set the working copy administrative directory name. */
  if (getenv("SVN_ASP_DOT_NET_HACK"))
    {
      SVN_ERR(svn_wc_set_adm_dir("_svn", pool));
    }
#endif

  /* Initialize the RA library. */
  SVN_ERR(svn_ra_initialize(pool));

  /* Begin processing arguments. */
  opt_state.depth = svn_depth_unknown;

  /* No args?  Show usage. */
  if (argc <= 1)
    {
      SVN_ERR(svn_min__help(NULL, NULL, pool));
      *exit_code = EXIT_FAILURE;
      return SVN_NO_ERROR;
    }

  /* Else, parse options. */
  SVN_ERR(svn_cmdline__getopt_init(&os, argc, argv, pool));

  os->interleave = 1;
  while (1)
    {
      const char *opt_arg;
      const char *utf8_opt_arg;

      /* Parse the next option. */
      apr_status_t apr_err = apr_getopt_long(os, svn_min__options, &opt_id,
                                             &opt_arg);
      if (APR_STATUS_IS_EOF(apr_err))
        break;
      else if (apr_err)
        {
          SVN_ERR(svn_min__help(NULL, NULL, pool));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }

      /* Stash the option code in an array before parsing it. */
      APR_ARRAY_PUSH(received_opts, int) = opt_id;

      switch (opt_id) {
      case 'h':
      case '?':
        opt_state.help = TRUE;
        break;
      case 'q':
        opt_state.quiet = TRUE;
        break;
      case opt_targets:
        {
          svn_stringbuf_t *buffer, *buffer_utf8;

          SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
          SVN_ERR(svn_stringbuf_from_file2(&buffer, utf8_opt_arg, pool));
          SVN_ERR(svn_utf_stringbuf_to_utf8(&buffer_utf8, buffer, pool));
          opt_state.targets = svn_cstring_split(buffer_utf8->data, "\n\r",
                                                TRUE, pool);
        }
        break;
      case opt_depth:
        err = svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool);
        if (err)
          return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, err,
                                   _("Error converting depth "
                                     "from locale to UTF-8"));
        opt_state.depth = svn_depth_from_word(utf8_opt_arg);
        if (opt_state.depth == svn_depth_unknown
            || opt_state.depth == svn_depth_exclude)
          {
            return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                     _("'%s' is not a valid depth; try "
                                       "'empty', 'files', 'immediates', "
                                       "or 'infinity'"),
                                     utf8_opt_arg);
          }
        break;
      case opt_version:
        opt_state.version = TRUE;
        break;
      case opt_dry_run:
        opt_state.dry_run = TRUE;
        break;
      case opt_auth_username:
        SVN_ERR(svn_utf_cstring_to_utf8(&opt_state.auth_username,
                                        opt_arg, pool));
        break;
      case opt_auth_password:
        SVN_ERR(svn_utf_cstring_to_utf8(&opt_state.auth_password,
                                        opt_arg, pool));
        break;
      case opt_no_auth_cache:
        opt_state.no_auth_cache = TRUE;
        break;
      case opt_non_interactive:
        opt_state.non_interactive = TRUE;
        break;
      case opt_force_interactive:
        force_interactive = TRUE;
        break;
      case opt_trust_server_cert: /* backwards compat to 1.8 */
      case opt_trust_server_cert_unknown_ca:
        opt_state.trust_server_cert_unknown_ca = TRUE;
        break;
      case opt_trust_server_cert_cn_mismatch:
        opt_state.trust_server_cert_cn_mismatch = TRUE;
        break;
      case opt_trust_server_cert_expired:
        opt_state.trust_server_cert_expired = TRUE;
        break;
      case opt_trust_server_cert_not_yet_valid:
        opt_state.trust_server_cert_not_yet_valid = TRUE;
        break;
      case opt_trust_server_cert_other_failure:
        opt_state.trust_server_cert_other_failure = TRUE;
        break;
      case opt_config_dir:
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        opt_state.config_dir = svn_dirent_internal_style(utf8_opt_arg, pool);
        break;
      case opt_config_options:
        if (!opt_state.config_options)
          opt_state.config_options =
                   apr_array_make(pool, 1,
                                  sizeof(svn_cmdline__config_argument_t*));

        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        SVN_ERR(svn_cmdline__parse_config_option(opt_state.config_options,
                                                 utf8_opt_arg, pool));
        break;
      case opt_allow_mixed_revisions:
        opt_state.allow_mixed_rev = TRUE;
        break;
      default:
        /* Hmmm. Perhaps this would be a good place to squirrel away
           opts that commands like svn diff might need. Hmmm indeed. */
        break;
      }
    }

  /* The --non-interactive and --force-interactive options are mutually
   * exclusive. */
  if (opt_state.non_interactive && force_interactive)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--non-interactive and --force-interactive "
                                "are mutually exclusive"));
    }
  else
    opt_state.non_interactive = !svn_cmdline__be_interactive(
                                  opt_state.non_interactive,
                                  force_interactive);

  /* ### This really belongs in libsvn_client.  The trouble is,
     there's no one place there to run it from, no
     svn_client_init().  We'd have to add it to all the public
     functions that a client might call.  It's unmaintainable to do
     initialization from within libsvn_client itself, but it seems
     burdensome to demand that all clients call svn_client_init()
     before calling any other libsvn_client function... On the other
     hand, the alternative is effectively to demand that they call
     svn_config_ensure() instead, so maybe we should have a generic
     init function anyway.  Thoughts?  */
  SVN_ERR(svn_config_ensure(opt_state.config_dir, pool));

  /* If the user asked for help, then the rest of the arguments are
     the names of subcommands to get help on (if any), or else they're
     just typos/mistakes.  Whatever the case, the subcommand to
     actually run is svn_cl__help(). */
  if (opt_state.help)
    subcommand = svn_opt_get_canonical_subcommand2(svn_min__cmd_table, "help");

  /* If we're not running the `help' subcommand, then look for a
     subcommand in the first argument. */
  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          if (opt_state.version)
            {
              /* Use the "help" subcommand to handle the "--version" option. */
              static const svn_opt_subcommand_desc2_t pseudo_cmd =
                { "--version", svn_min__help, {0}, "",
                  {opt_version,    /* must accept its own option */
                   'q',            /* brief output */
                   'v',            /* verbose output */
                   opt_config_dir  /* all commands accept this */
                  } };

              subcommand = &pseudo_cmd;
            }
          else
            {
              svn_error_clear
                (svn_cmdline_fprintf(stderr, pool,
                                     _("Subcommand argument required\n")));
              svn_error_clear(svn_min__help(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
            }
        }
      else
        {
          const char *first_arg = os->argv[os->ind++];
          subcommand = svn_opt_get_canonical_subcommand2(svn_min__cmd_table,
                                                         first_arg);
          if (subcommand == NULL)
            {
              const char *first_arg_utf8;
              SVN_ERR(svn_utf_cstring_to_utf8(&first_arg_utf8,
                                              first_arg, pool));
              svn_error_clear
                (svn_cmdline_fprintf(stderr, pool,
                                     _("Unknown subcommand: '%s'\n"),
                                     first_arg_utf8));
              svn_error_clear(svn_min__help(NULL, NULL, pool));

              /* Be kind to people who try 'svn undo'. */
              if (strcmp(first_arg_utf8, "undo") == 0)
                {
                  svn_error_clear
                    (svn_cmdline_fprintf(stderr, pool,
                                         _("Undo is done using either the "
                                           "'svn revert' or the 'svn merge' "
                                           "command.\n")));
                }

              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
            }
        }
    }

  /* Check that the subcommand wasn't passed any inappropriate options. */
  for (i = 0; i < received_opts->nelts; i++)
    {
      opt_id = APR_ARRAY_IDX(received_opts, i, int);

      /* All commands implicitly accept --help, so just skip over this
         when we see it. Note that we don't want to include this option
         in their "accepted options" list because it would be awfully
         redundant to display it in every commands' help text. */
      if (opt_id == 'h' || opt_id == '?')
        continue;

      if (! svn_opt_subcommand_takes_option3(subcommand, opt_id,
                                             svn_min__global_options))
        {
          const char *optstr;
          const apr_getopt_option_t *badopt =
            svn_opt_get_option_from_code2(opt_id, svn_min__options,
                                          subcommand, pool);
          svn_opt_format_option(&optstr, badopt, FALSE, pool);
          if (subcommand->name[0] == '-')
            svn_error_clear(svn_min__help(NULL, NULL, pool));
          else
            svn_error_clear
              (svn_cmdline_fprintf
               (stderr, pool, _("Subcommand '%s' doesn't accept option '%s'\n"
                                "Type 'svn-mergeinfo-normalizer help %s' for usage.\n"),
                subcommand->name, optstr, subcommand->name));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }
    }

  /* --trust-* options can only be used with --non-interactive */
  if (!opt_state.non_interactive)
    {
      if (opt_state.trust_server_cert_unknown_ca)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--trust-unknown-ca requires "
                                  "--non-interactive"));
      if (opt_state.trust_server_cert_cn_mismatch)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--trust-cn-mismatch requires "
                                  "--non-interactive"));
      if (opt_state.trust_server_cert_expired)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--trust-expired requires "
                                  "--non-interactive"));
      if (opt_state.trust_server_cert_not_yet_valid)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--trust-not-yet-valid requires "
                                  "--non-interactive"));
      if (opt_state.trust_server_cert_other_failure)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("--trust-other-failure requires "
                                  "--non-interactive"));
    }

  err = svn_config_get_config(&cfg_hash, opt_state.config_dir, pool);
  if (err)
    {
      /* Fallback to default config if the config directory isn't readable
         or is not a directory. */
      if (APR_STATUS_IS_EACCES(err->apr_err)
          || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err))
        {
          svn_handle_warning2(stderr, err, "svn: ");
          svn_error_clear(err);

          SVN_ERR(svn_config__get_default_config(&cfg_hash, pool));
        }
      else
        return err;
    }

  /* Update the options in the config */
  if (opt_state.config_options)
    {
      svn_error_clear(
          svn_cmdline__apply_config_options(cfg_hash,
                                            opt_state.config_options,
                                            "svn: ", "--config-option"));
    }

  cfg_config = svn_hash_gets(cfg_hash, SVN_CONFIG_CATEGORY_CONFIG);
#if !defined(SVN_CL_NO_EXCLUSIVE_LOCK)
  {
    const char *exclusive_clients_option;
    apr_array_header_t *exclusive_clients;

    svn_config_get(cfg_config, &exclusive_clients_option,
                   SVN_CONFIG_SECTION_WORKING_COPY,
                   SVN_CONFIG_OPTION_SQLITE_EXCLUSIVE_CLIENTS,
                   NULL);
    exclusive_clients = svn_cstring_split(exclusive_clients_option,
                                          " ,", TRUE, pool);
    for (i = 0; i < exclusive_clients->nelts; ++i)
      {
        const char *exclusive_client = APR_ARRAY_IDX(exclusive_clients, i,
                                                     const char *);

        /* This blocks other clients from accessing the wc.db so it must
           be explicitly enabled.*/
        if (!strcmp(exclusive_client, "svn"))
          svn_config_set(cfg_config,
                         SVN_CONFIG_SECTION_WORKING_COPY,
                         SVN_CONFIG_OPTION_SQLITE_EXCLUSIVE,
                         "true");
      }
  }
#endif

  /* Create a client context object. */
  command_baton.opt_state = &opt_state;
  SVN_ERR(svn_client_create_context2(&ctx, cfg_hash, pool));
  command_baton.ctx = ctx;

  /* Set up our cancellation support. */
  ctx->cancel_func = svn_min__check_cancel;
  apr_signal(SIGINT, signal_handler);
#ifdef SIGBREAK
  /* SIGBREAK is a Win32 specific signal generated by ctrl-break. */
  apr_signal(SIGBREAK, signal_handler);
#endif
#ifdef SIGHUP
  apr_signal(SIGHUP, signal_handler);
#endif
#ifdef SIGTERM
  apr_signal(SIGTERM, signal_handler);
#endif

#ifdef SIGPIPE
  /* Disable SIGPIPE generation for the platforms that have it. */
  apr_signal(SIGPIPE, SIG_IGN);
#endif

#ifdef SIGXFSZ
  /* Disable SIGXFSZ generation for the platforms that have it, otherwise
   * working with large files when compiled against an APR that doesn't have
   * large file support will crash the program, which is uncool. */
  apr_signal(SIGXFSZ, SIG_IGN);
#endif

  /* Set up Authentication stuff. */
  SVN_ERR(svn_cmdline_create_auth_baton2(
            &ab,
            opt_state.non_interactive,
            opt_state.auth_username,
            opt_state.auth_password,
            opt_state.config_dir,
            opt_state.no_auth_cache,
            opt_state.trust_server_cert_unknown_ca,
            opt_state.trust_server_cert_cn_mismatch,
            opt_state.trust_server_cert_expired,
            opt_state.trust_server_cert_not_yet_valid,
            opt_state.trust_server_cert_other_failure,
            cfg_config,
            ctx->cancel_func,
            ctx->cancel_baton,
            pool));

  ctx->auth_baton = ab;

  /* Check whether interactive conflict resolution is disabled by
   * the configuration file. If no --accept option was specified
   * we postpone all conflicts in this case. */
  SVN_ERR(svn_config_get_bool(cfg_config, &interactive_conflicts,
                              SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_INTERACTIVE_CONFLICTS,
                              TRUE));

  SVN_ERR(svn_client_args_to_target_array2(&opt_state.targets,
                                           os, opt_state.targets,
                                           ctx, FALSE, pool));

  /* Add "." if user passed 0 arguments. */
  svn_opt_push_implicit_dot_target(opt_state.targets, pool);

  /* And now we finally run the subcommand. */
  err = (*subcommand->cmd_func)(os, &command_baton, pool);
  if (err)
    {
      /* For argument-related problems, suggest using the 'help'
         subcommand. */
      if (err->apr_err == SVN_ERR_CL_INSUFFICIENT_ARGS
          || err->apr_err == SVN_ERR_CL_ARG_PARSING_ERROR)
        {
          err = svn_error_quick_wrap(
                  err, apr_psprintf(pool,
                                    _("Try 'svn help %s' for more information"),
                                    subcommand->name));
        }

        if (err->apr_err == SVN_ERR_AUTHN_FAILED && opt_state.non_interactive)
        {
          err = svn_error_quick_wrap(err,
                                     _("Authentication failed and interactive"
                                       " prompting is disabled; see the"
                                       " --force-interactive option"));
        }

      /* Tell the user about 'svn cleanup' if any error on the stack
         was about locked working copies. */
      if (svn_error_find_cause(err, SVN_ERR_WC_LOCKED))
        {
          err = svn_error_quick_wrap(
                  err, _("Run 'svn cleanup' to remove locks "
                         "(type 'svn help cleanup' for details)"));
        }

      if (err->apr_err == SVN_ERR_SQLITE_BUSY)
        {
          err = svn_error_quick_wrap(err,
                                     _("Another process is blocking the "
                                       "working copy database, or the "
                                       "underlying filesystem does not "
                                       "support file locking; if the working "
                                       "copy is on a network filesystem, make "
                                       "sure file locking has been enabled "
                                       "on the file server"));
        }

      if (svn_error_find_cause(err, SVN_ERR_RA_CANNOT_CREATE_TUNNEL) &&
          (opt_state.auth_username || opt_state.auth_password))
        {
          err = svn_error_quick_wrap(
                  err, _("When using svn+ssh:// URLs, keep in mind that the "
                         "--username and --password options are ignored "
                         "because authentication is performed by SSH, not "
                         "Subversion"));
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

  /* Initialize the app. */
  if (svn_cmdline_init("svn", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool.  Use a separate mutexless allocator,
   * given this application is single threaded.
   */
  pool = apr_allocator_owner_get(svn_pool_create_allocator(FALSE));

  err = sub_main(&exit_code, argc, argv, pool);

  /* Flush stdout and report if it fails. It would be flushed on exit anyway
     but this makes sure that output is not silently lost if it fails. */
  err = svn_error_compose_create(err, svn_cmdline_fflush(stdout));

  if (err)
    {
      exit_code = EXIT_FAILURE;
      svn_cmdline_handle_exit_error(err, NULL, "svn: ");
    }

  svn_pool_destroy(pool);
  return exit_code;
}
