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
  opt_auth_password_from_stdin,
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
  opt_remove_obsoletes,
  opt_remove_redundant,
  opt_combine_ranges,
  opt_remove_redundant_misaligned
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
  {"file",          'F', 1, N_("read list of branches to remove from file ARG.\n"
                       "                             "
                       "Each branch given on a separate line with no\n"
                       "                             "
                       "extra whitespace.")},
  {"verbose",       'v', 0, N_("print extra information")},
  {"username",      opt_auth_username, 1, N_("specify a username ARG")},
  {"password",      opt_auth_password, 1,
                    N_("specify a password ARG (caution: on many operating\n"
                       "                             "
                       "systems, other users will be able to see this)")},
  {"password-from-stdin",
                    opt_auth_password_from_stdin, 0,
                    N_("read password from stdin")},
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
  {"allow-mixed-revisions", opt_allow_mixed_revisions, 0,
                       N_("Allow operation on mixed-revision working copy.\n"
                       "                             "
                       "Use of this option is not recommended!\n"
                       "                             "
                       "Please run 'svn update' instead.")},

  {"remove-obsoletes", opt_remove_obsoletes, 0,
                       N_("Remove mergeinfo for deleted branches.")},
  {"remove-redundant", opt_remove_redundant, 0,
                       N_("Remove mergeinfo on sub-nodes if it is\n"
                       "                             "
                       "redundant with the parent mergeinfo.")},
  {"remove-redundant-misaligned", opt_remove_redundant_misaligned, 0,
                       N_("Remove mergeinfo of a misaligned branch if it\n"
                       "                             "
                       "is already covered by a correctly aligned one.\n")},
  {"combine-ranges",   opt_combine_ranges, 0,
                       N_("Try to combine adjacent revision ranges\n"
                       "                             "
                       "to reduce the size of the mergeinfo.")},

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
{ opt_auth_username, opt_auth_password, opt_auth_password_from_stdin,
  opt_no_auth_cache, opt_non_interactive, opt_force_interactive,
  opt_trust_server_cert, opt_trust_server_cert_unknown_ca,
  opt_trust_server_cert_cn_mismatch, opt_trust_server_cert_expired,
  opt_trust_server_cert_not_yet_valid, opt_trust_server_cert_other_failure,
  opt_config_dir, opt_config_options, 0
};

const svn_opt_subcommand_desc2_t svn_min__cmd_table[] =
{
  { "help", svn_min__help, {"?", "h"}, N_
    ("Describe the usage of this program or its subcommands.\n"
     "usage: help [SUBCOMMAND...]\n"),
    {0} },

  /* This command is also invoked if we see option "--help", "-h" or "-?". */

  { "analyze", svn_min__analyze, { "analyse" }, N_
    ("Generate a report of which part of the sub-tree mergeinfo can be\n"
     "removed and which part can't.\n"
     "usage: analyze [WCPATH...]\n"
     "\n"
     "  If neither --remove-obsoletes, --remove-redundant nor --combine-ranges\n"
     "  option is given, all three will be used implicitly.\n"
     "\n"
     "  In verbose mode, the command will behave just like 'normalize --dry-run'\n"
     "  but will show an additional summary of all deleted branches that were\n"
     "  encountered plus the revision of their latest deletion (if available).\n"
     "\n"
     "  In non-verbose mode, the per-node output does not give the parent path,\n"
     "  no successful elisions and branch removals nor the list of remaining\n"
     "  branches.\n"
    ),
    {opt_targets, opt_depth, 'v',
     opt_remove_obsoletes, opt_remove_redundant,
     opt_remove_redundant_misaligned, opt_combine_ranges} },

  { "normalize", svn_min__normalize, { 0 }, N_
    ("Normalize / reduce the mergeinfo throughout the working copy sub-tree.\n"
     "usage: normalize [WCPATH...]\n"
     "\n"
     "  If neither --remove-obsoletes, --remove-redundant, --combine-ranges\n"
     "  nor --remove-redundant-misaligned option is given, --remove-redundant\n"
     "  will be used implicitly.\n"
     "\n"
     "  In non-verbose mode, only general progress as well as a summary before\n"
     "  and after the normalization process will be shown.  Note that sub-node\n"
     "  mergeinfo which could be removed entirely does not contribute to the\n"
     "  number of removed branch lines.  Similarly, the number of revision\n"
     "  ranges combined only refers to the mergeinfo lines still present after\n"
     "  the normalization process.  To get total numbers, compare the initial\n"
     "  with the final mergeinfo statistics.\n"
     "\n"
     "  The detailed operation log in verbose mode replaces the progress display.\n"
     "  For each node with mergeinfo, the nearest parent node with mergeinfo is\n"
     "  given - if there is one and the result of trying to remove the mergeinfo\n"
     "  is shown for each branch.  The various outputs are:\n"
     "\n"
     "    elide redundant branch - Revision ranges are the same as in the parent.\n"
     "                             Mergeinfo for this branch can be elided.\n"
     "    elide branch           - Not an exact match with the parent but the\n"
     "                             differences could be eliminated by ...\n"
     "      revisions implied in parent\n"
     "                             ... ignoring these revisions because they are\n"
     "                             part of the parent's copy history.\n"
     "      revisions moved to parent\n"
     "                             ... adding these revisions to the parent node\n"
     "                             because they only affect the current sub-tree.\n"
     "      revisions implied in sub-tree\n"
     "                             ... ignoring these revisions because they are\n"
     "                             part of the sub-tree's copy history.\n"
     "      revisions inoperative in sub-node\n"
     "                             ... removing these revisions from the sub-tree\n"
     "                             mergeinfo because they did not change it.\n"
     "    remove deleted branch  - The branch no longer exists in the repository.\n"
     "                             We will remove its mergeinfo line.\n"
     "    elide misaligned branch- All revisions merged from that misaligned\n"
     "                             branch have also been merged from the likely\n"
     "                             correctly aligned branch.\n"
     "    CANNOT elide branch    - Mergeinfo differs from parent's significantly\n"
     "                             and can't be elided because ...\n"
     "      revisions not movable to parent\n"
     "                             ... these revisions affect the parent tree\n"
     "                             outside the current sub-tree but are only\n"
     "                             listed as merged in the current sub-tree.\n"
     "      revisions missing in sub-node\n"
     "                             ... these revisions affect current sub-tree\n"
     "                             but are only listed as merged for the parent.\n"
     "    keep POTENTIAL branch  - The path does not exist @HEAD but may appear\n"
     "                             in the future as the result of catch-up merges\n"
     "                             from other branches.\n"
     "    has SURVIVING COPIES:  - The path does not exist @HEAD but copies of it\n"
     "                             or its sub-nodes do.  This mergeinfo may be\n"
     "                             relevant to them and will be kept.\n"
     "    NON-RECURSIVE RANGE(S) found\n"
     "                           - Those revisions had been merged into a sparse\n"
     "                             working copy resulting in incomplete merges.\n"
     "                             The sub-tree mergeinfo cannot be elided.\n"
     "    MISSING in parent      - The branch for the parent node exists in the\n"
     "                             repository but is not in its mergeinfo.\n"
     "                             The sub-tree mergeinfo will not be elided.\n"
     "    CANNOT elide MISALIGNED branch\n"
     "                             The misaligned branch cannot be elide because\n"
     "                             the revisions listed ...\n"
     "      revisions not merged from likely correctly aligned branch\n"
     "                             ... here have not also been merged from the\n"
     "                             likely correctly aligned branch.\n"
     "    MISALIGNED branch      - There is no such branch for the parent node.\n"
     "                             The sub-tree mergeinfo cannot be elided.\n"
     "    REVERSE RANGE(S) found - The mergeinfo contains illegal reverse ranges.\n"
     "                             The sub-tree mergeinfo cannot be elided.\n"
     "\n"
     "  If all branches have been removed from a nodes' mergeinfo, the whole\n"
     "  svn:mergeinfo property will be removed.  Otherwise, only obsolete\n"
     "  branches will be removed.  In verbose mode, a list of branches that\n"
     "  could not be removed will be shown per node.\n"),
    {opt_targets, opt_depth, opt_dry_run, 'q', 'v',
     opt_remove_obsoletes, opt_remove_redundant,
     opt_remove_redundant_misaligned, opt_combine_ranges} },

  { "remove-branches", svn_min__remove_branches, { 0 }, N_
    ("Read a list of branch names from the given file and remove all\n"
     "mergeinfo referring to these branches from the given targets.\n"
     "usage: remove-branches [WCPATH...] --file FILE\n"
     "\n"
     "  The command will behave just like 'normalize --remove-obsoletes' but\n"
     "  will never actually contact the repository.  Instead, it assumes any\n"
     "  path given in FILE is a deleted branch.\n"
     "\n"
     "  Compared to a simple 'normalize --remove-obsoletes' run, this command\n"
     "  allows for selective removal of obsolete branches.  It may therefore be\n"
     "  better suited for large deployments with complex branch structures.\n"
     "  You may also use this to remove mergeinfo that refers to still existing\n"
     "  branches.\n"),
    {opt_targets, opt_depth, opt_dry_run, 'q', 'v', 'F'} },

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
  svn_min__cmd_baton_t command_baton = { 0 };
  svn_auth_baton_t *ab;
  svn_config_t *cfg_config;
  svn_boolean_t interactive_conflicts = FALSE;
  svn_boolean_t force_interactive = FALSE;
  apr_hash_t *cfg_hash;
  svn_boolean_t read_pass_from_stdin = FALSE;

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
      case 'v':
        opt_state.verbose = TRUE;
        break;
      case 'F':
        /* We read the raw file content here. */
        SVN_ERR(svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool));
        SVN_ERR(svn_stringbuf_from_file2(&(opt_state.filedata),
                                         utf8_opt_arg, pool));
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
      case opt_auth_password_from_stdin:
        read_pass_from_stdin = TRUE;
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
                                                 utf8_opt_arg,
                                                 "svn-mi-normalizer: ",
                                                 pool));
        break;
      case opt_allow_mixed_revisions:
        opt_state.allow_mixed_rev = TRUE;
        break;

      case opt_remove_obsoletes:
        opt_state.remove_obsoletes = TRUE;
        break;
      case opt_remove_redundant:
        opt_state.remove_redundants = TRUE;
        break;
      case opt_combine_ranges:
        opt_state.combine_ranges = TRUE;
        break;
      case opt_remove_redundant_misaligned:
        opt_state.remove_redundant_misaligned = TRUE;
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

  /* --password-from-stdin can only be used with --non-interactive */
  if (read_pass_from_stdin && !opt_state.non_interactive)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("--password-from-stdin requires "
                                "--non-interactive"));
    }

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
          const char *first_arg;

          SVN_ERR(svn_utf_cstring_to_utf8(&first_arg, os->argv[os->ind++],
                                          pool));
          subcommand = svn_opt_get_canonical_subcommand2(svn_min__cmd_table,
                                                         first_arg);
          if (subcommand == NULL)
            {
              svn_error_clear
                (svn_cmdline_fprintf(stderr, pool,
                                     _("Unknown subcommand: '%s'\n"),
                                     first_arg));
              svn_error_clear(svn_min__help(NULL, NULL, pool));

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

  /* Get password from stdin if necessary */
  if (read_pass_from_stdin)
    {
      SVN_ERR(svn_cmdline__stdin_readline(&opt_state.auth_password, pool, pool));
    }

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

  /* Get targets from command line - unless we are running "help".
   * The help sub-command will do its own parsing. */
  if (strcmp(subcommand->name, "help"))
    {
      SVN_ERR(svn_client_args_to_target_array2(&opt_state.targets,
                                              os, opt_state.targets,
                                              ctx, FALSE, pool));

      /* Add "." if user passed 0 arguments. */
      svn_opt_push_implicit_dot_target(opt_state.targets, pool);
    }

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
