/*
 *  svnrdump.c: Produce a dumpfile of a local or remote repository
 *              without touching the filesystem, but for temporary files.
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

#include <apr_signal.h>

#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_client.h"
#include "svn_hash.h"
#include "svn_ra.h"
#include "svn_repos.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_private_config.h"
#include "svn_string.h"
#include "svn_props.h"

#include "dump_editor.h"
#include "load_editor.h"

#include "private/svn_cmdline_private.h"



/*** Cancellation ***/

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
static svn_error_t *
check_cancel(void *baton)
{
  if (cancelled)
    return svn_error_create(SVN_ERR_CANCELLED, NULL, _("Caught signal"));
  else
    return SVN_NO_ERROR;
}




static svn_opt_subcommand_t dump_cmd, load_cmd;

enum svn_svnrdump__longopt_t
  {
    opt_config_dir = SVN_OPT_FIRST_LONGOPT_ID,
    opt_config_option,
    opt_auth_username,
    opt_auth_password,
    opt_auth_nocache,
    opt_non_interactive,
    opt_version,
  };

#define SVN_SVNRDUMP__BASE_OPTIONS opt_config_dir, \
                                   opt_config_option, \
                                   opt_auth_username, \
                                   opt_auth_password, \
                                   opt_auth_nocache, \
                                   opt_non_interactive

static const svn_opt_subcommand_desc2_t svnrdump__cmd_table[] =
{
  { "dump", dump_cmd, { 0 },
    N_("usage: svnrdump dump URL [-r LOWER[:UPPER]]\n\n"
       "Dump revisions LOWER to UPPER of repository at remote URL to stdout\n"
       "in a 'dumpfile' portable format.  If only LOWER is given, dump that\n"
       "one revision.\n"),
    { 'r', 'q', SVN_SVNRDUMP__BASE_OPTIONS } },
  { "load", load_cmd, { 0 },
    N_("usage: svnrdump load URL\n\n"
       "Load a 'dumpfile' given on stdin to a repository at remote URL.\n"),
    { 'q', SVN_SVNRDUMP__BASE_OPTIONS } },
  { "help", 0, { "?", "h" },
    N_("usage: svnrdump help [SUBCOMMAND...]\n\n"
       "Describe the usage of this program or its subcommands.\n"),
    { 0 } },
  { NULL, NULL, { 0 }, NULL, { 0 } }
};

static const apr_getopt_option_t svnrdump__options[] =
  {
    {"revision",     'r', 1, 
                      N_("specify revision number ARG (or X:Y range)")},
    {"quiet",         'q', 0,
                      N_("no progress (only errors) to stderr")},
    {"config-dir",    opt_config_dir, 1,
                      N_("read user configuration files from directory ARG")},
    {"username",      opt_auth_username, 1,
                      N_("specify a username ARG")},
    {"password",      opt_auth_password, 1,
                      N_("specify a password ARG")},
    {"non-interactive", opt_non_interactive, 0,
                      N_("do no interactive prompting")},
    {"no-auth-cache", opt_auth_nocache, 0,
                      N_("do not cache authentication tokens")},
    {"help",          'h', 0,
                      N_("display this help")},
    {"version",       opt_version, 0,
                      N_("show program version information")},
    {"config-option", opt_config_option, 1,
                      N_("set user configuration option in the format:\n"
                         "                             "
                         "    FILE:SECTION:OPTION=[VALUE]\n"
                         "                             "
                         "For example:\n"
                         "                             "
                         "    servers:global:http-library=serf")},
    {0, 0, 0, 0}
  };

/* Baton for the RA replay session. */
struct replay_baton {
  /* The editor producing diffs. */
  const svn_delta_editor_t *editor;

  /* Baton for the editor. */
  void *edit_baton;

  /* Whether to be quiet. */
  svn_boolean_t quiet;
};

/* Option set */
typedef struct opt_baton_t {
  svn_ra_session_t *session;
  const char *url;
  svn_opt_revision_t start_revision;
  svn_opt_revision_t end_revision;
  svn_boolean_t quiet;
} opt_baton_t;

/* Print dumpstream-formatted information about REVISION.
 * Implements the `svn_ra_replay_revstart_callback_t' interface.
 */
static svn_error_t *
replay_revstart(svn_revnum_t revision,
                void *replay_baton,
                const svn_delta_editor_t **editor,
                void **edit_baton,
                apr_hash_t *rev_props,
                apr_pool_t *pool)
{
  struct replay_baton *rb = replay_baton;
  svn_stringbuf_t *propstring;
  svn_stream_t *stdout_stream;
  svn_stream_t *revprop_stream;

  svn_stream_for_stdout(&stdout_stream, pool);

  /* Revision-number: 19 */
  SVN_ERR(svn_stream_printf(stdout_stream, pool,
                            SVN_REPOS_DUMPFILE_REVISION_NUMBER
                            ": %ld\n", revision));
  SVN_ERR(normalize_props(rev_props, pool));
  propstring = svn_stringbuf_create_ensure(0, pool);
  revprop_stream = svn_stream_from_stringbuf(propstring, pool);
  SVN_ERR(svn_hash_write2(rev_props, revprop_stream, "PROPS-END", pool));
  SVN_ERR(svn_stream_close(revprop_stream));

  /* Prop-content-length: 13 */
  SVN_ERR(svn_stream_printf(stdout_stream, pool,
                            SVN_REPOS_DUMPFILE_PROP_CONTENT_LENGTH
                            ": %" APR_SIZE_T_FMT "\n", propstring->len));

  /* Content-length: 29 */
  SVN_ERR(svn_stream_printf(stdout_stream, pool,
                            SVN_REPOS_DUMPFILE_CONTENT_LENGTH
                            ": %" APR_SIZE_T_FMT "\n\n", propstring->len));

  /* Property data. */
  SVN_ERR(svn_stream_write(stdout_stream, propstring->data,
                           &(propstring->len)));

  SVN_ERR(svn_stream_printf(stdout_stream, pool, "\n"));
  SVN_ERR(svn_stream_close(stdout_stream));

  /* Extract editor and editor_baton from the replay_baton and
     set them so that the editor callbacks can use them. */
  *editor = rb->editor;
  *edit_baton = rb->edit_baton;

  return SVN_NO_ERROR;
}

/* Print progress information about the dump of REVISION.
   Implements the `svn_ra_replay_revfinish_callback_t' interface. */
static svn_error_t *
replay_revend(svn_revnum_t revision,
              void *replay_baton,
              const svn_delta_editor_t *editor,
              void *edit_baton,
              apr_hash_t *rev_props,
              apr_pool_t *pool)
{
  /* No resources left to free. */
  struct replay_baton *rb = replay_baton;
  if (! rb->quiet)
    svn_cmdline_fprintf(stderr, pool, "* Dumped revision %lu.\n", revision);
  return SVN_NO_ERROR;
}

/* Set *SESSION to a new RA session opened to URL.  Allocate *SESSION
 * and related data structures in POOL.  Use CONFIG_DIR and pass
 * USERNAME, PASSWORD, CONFIG_DIR and NO_AUTH_CACHE to initialize the
 * authorization baton.  CONFIG_OPTIONS (if not NULL) is a list of
 * configuration overrides.
 */
static svn_error_t *
open_connection(svn_ra_session_t **session,
                const char *url,
                svn_boolean_t non_interactive,
                const char *username,
                const char *password,
                const char *config_dir,
                svn_boolean_t no_auth_cache,
                apr_array_header_t *config_options,
                apr_pool_t *pool)
{
  svn_client_ctx_t *ctx = NULL;
  svn_config_t *cfg_config;

  SVN_ERR(svn_ra_initialize(pool));

  SVN_ERR(svn_config_ensure(config_dir, pool));
  SVN_ERR(svn_client_create_context(&ctx, pool));

  SVN_ERR(svn_config_get_config(&(ctx->config), config_dir, pool));

  if (config_options)
    SVN_ERR(svn_cmdline__apply_config_options(ctx->config, config_options,
                                              "svnrdump: ", "--config-option"));

  cfg_config = apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_CONFIG,
                            APR_HASH_KEY_STRING);

  /* Set up our cancellation support. */
  ctx->cancel_func = check_cancel;

  /* Default authentication providers for non-interactive use */
  SVN_ERR(svn_cmdline_create_auth_baton(&(ctx->auth_baton), non_interactive,
                                        username, password, config_dir,
                                        no_auth_cache, FALSE, cfg_config,
                                        ctx->cancel_func, ctx->cancel_baton,
                                        pool));
  SVN_ERR(svn_client_open_ra_session(session, url, ctx, pool));
  return SVN_NO_ERROR;
}

/* Replay revisions START_REVISION thru END_REVISION (inclusive) of
 * the repository located at URL, using callbacks which generate
 * Subversion repository dumpstreams describing the changes made in
 * those revisions.  If QUIET is set, don't generate progress
 * messages.
 */
static svn_error_t *
replay_revisions(svn_ra_session_t *session,
                 const char *url,
                 svn_revnum_t start_revision,
                 svn_revnum_t end_revision,
                 svn_boolean_t quiet,
                 apr_pool_t *pool)
{
  const svn_delta_editor_t *dump_editor;
  struct replay_baton *replay_baton;
  void *dump_baton;
  const char *uuid;
  svn_stream_t *stdout_stream;

  SVN_ERR(svn_stream_for_stdout(&stdout_stream, pool));

  SVN_ERR(get_dump_editor(&dump_editor, &dump_baton, stdout_stream, 
                          check_cancel, NULL, pool));

  replay_baton = apr_pcalloc(pool, sizeof(*replay_baton));
  replay_baton->editor = dump_editor;
  replay_baton->edit_baton = dump_baton;
  replay_baton->quiet = quiet;

  /* Write the magic header and UUID */
  SVN_ERR(svn_stream_printf(stdout_stream, pool,
                            SVN_REPOS_DUMPFILE_MAGIC_HEADER ": %d\n\n",
                            SVN_REPOS_DUMPFILE_FORMAT_VERSION));
  SVN_ERR(svn_ra_get_uuid2(session, &uuid, pool));
  SVN_ERR(svn_stream_printf(stdout_stream, pool,
                            SVN_REPOS_DUMPFILE_UUID ": %s\n\n", uuid));

  /* Fake revision 0 if necessary */
  if (start_revision == 0)
    {
      apr_hash_t *prophash;
      svn_stringbuf_t *propstring;
      svn_stream_t *propstream;
      SVN_ERR(svn_stream_printf(stdout_stream, pool,
                                SVN_REPOS_DUMPFILE_REVISION_NUMBER
                                ": %ld\n", start_revision));

      prophash = apr_hash_make(pool);
      propstring = svn_stringbuf_create("", pool);

      SVN_ERR(svn_ra_rev_proplist(session, start_revision,
                                  &prophash, pool));

      propstream = svn_stream_from_stringbuf(propstring, pool);
      SVN_ERR(svn_hash_write2(prophash, propstream, "PROPS-END", pool));
      SVN_ERR(svn_stream_close(propstream));

      /* Property-content-length: 14; Content-length: 14 */
      SVN_ERR(svn_stream_printf(stdout_stream, pool,
                                SVN_REPOS_DUMPFILE_PROP_CONTENT_LENGTH
                                ": %" APR_SIZE_T_FMT "\n",
                                propstring->len));
      SVN_ERR(svn_stream_printf(stdout_stream, pool,
                                SVN_REPOS_DUMPFILE_CONTENT_LENGTH
                                ": %" APR_SIZE_T_FMT "\n\n",
                                propstring->len));
      /* The properties */
      SVN_ERR(svn_stream_write(stdout_stream, propstring->data,
                               &(propstring->len)));
      SVN_ERR(svn_stream_printf(stdout_stream, pool, "\n"));
      if (! quiet)
        svn_cmdline_fprintf(stderr, pool, "* Dumped revision %lu.\n",
                            start_revision);

      start_revision++;
    }

  SVN_ERR(svn_ra_replay_range(session, start_revision, end_revision,
                              0, TRUE, replay_revstart, replay_revend,
                              replay_baton, pool));
  SVN_ERR(svn_stream_close(stdout_stream));
  return SVN_NO_ERROR;
}

/* Read a dumpstream from stdin, and use it to feed a loader capable
 * of transmitting that information to the repository located at URL
 * (to which SESSION has been opened).
 */
static svn_error_t *
load_revisions(svn_ra_session_t *session,
               const char *url,
               svn_boolean_t quiet,
               apr_pool_t *pool)
{
  apr_file_t *stdin_file;
  svn_stream_t *stdin_stream;
  const svn_repos_parse_fns2_t *parser;
  void *parse_baton;

  apr_file_open_stdin(&stdin_file, pool);
  stdin_stream = svn_stream_from_aprfile2(stdin_file, FALSE, pool);

  SVN_ERR(get_dumpstream_loader(&parser, &parse_baton, session, pool));
  SVN_ERR(drive_dumpstream_loader(stdin_stream, parser, parse_baton,
                                  session, check_cancel, NULL, pool));

  svn_stream_close(stdin_stream);

  return SVN_NO_ERROR;
}

/* Return a program name for this program, the basename of the path
 * represented by PROGNAME if not NULL; use "svnrdump" otherwise.
 */
static const char *
ensure_appname(const char *progname,
               apr_pool_t *pool)
{
  if (!progname)
    return "svnrdump";

  return svn_dirent_basename(svn_dirent_internal_style(progname, pool), NULL);
}

/* Print a simple usage string. */
static svn_error_t *
usage(const char *progname,
      apr_pool_t *pool)
{
  return svn_cmdline_fprintf(stderr, pool,
                             _("Type '%s help' for usage.\n"),
                             ensure_appname(progname, pool));
}

/* Print information about the version of this program and dependent
 * modules.
 */
static svn_error_t *
version(const char *progname,
        apr_pool_t *pool)
{
  svn_stringbuf_t *version_footer = 
    svn_stringbuf_create(_("The following repository access (RA) modules "
                           "are available:\n\n"),
                         pool);

  SVN_ERR(svn_ra_print_modules(version_footer, pool));
  return svn_opt_print_help3(NULL, ensure_appname(progname, pool),
                             TRUE, FALSE, version_footer->data,
                             NULL, NULL, NULL, NULL, NULL, pool);
}


/* A statement macro, similar to @c SVN_ERR, but returns an integer.
 * Evaluate @a expr. If it yields an error, handle that error and
 * return @c EXIT_FAILURE.
 */
#define SVNRDUMP_ERR(expr)                                               \
  do                                                                     \
    {                                                                    \
      svn_error_t *svn_err__temp = (expr);                               \
      if (svn_err__temp)                                                 \
        {                                                                \
          svn_handle_error2(svn_err__temp, stderr, FALSE, "svnrdump: "); \
          svn_error_clear(svn_err__temp);                                \
          return EXIT_FAILURE;                                           \
        }                                                                \
    }                                                                    \
  while (0)

/* Handle the "dump" subcommand.  Implements `svn_opt_subcommand_t'.  */
static svn_error_t *
dump_cmd(apr_getopt_t *os,
         void *baton,
         apr_pool_t *pool)
{
  opt_baton_t *opt_baton = baton;
  return replay_revisions(opt_baton->session, opt_baton->url,
                          opt_baton->start_revision.value.number,
                          opt_baton->end_revision.value.number,
                          opt_baton->quiet, pool);
}

/* Handle the "load" subcommand.  Implements `svn_opt_subcommand_t'.  */
static svn_error_t *
load_cmd(apr_getopt_t *os,
         void *baton,
         apr_pool_t *pool)
{
  opt_baton_t *opt_baton = baton;
  return load_revisions(opt_baton->session, opt_baton->url,
                        opt_baton->quiet, pool);
}

/* Handle the "help" subcommand.  Implements `svn_opt_subcommand_t'.  */
static svn_error_t *
help_cmd(apr_getopt_t *os,
         void *baton,
         apr_pool_t *pool)
{
  const char *header =
    _("general usage: svnrdump SUBCOMMAND URL [-r LOWER[:UPPER]]\n"
      "Type 'svnrdump help <subcommand>' for help on a specific subcommand.\n"
      "\n"
      "Available subcommands:\n");

  return svn_opt_print_help3(os, "svnrdump", FALSE, FALSE, NULL, header,
                             svnrdump__cmd_table, svnrdump__options, NULL,
                             NULL, pool);
}

/* Examine the OPT_BATON's 'start_revision' and 'end_revision'
 * members, making sure that they make sense (in general, and as
 * applied to a repository whose current youngest revision is
 * LATEST_REVISION).
 */
static svn_error_t *
validate_and_resolve_revisions(opt_baton_t *opt_baton,
                               svn_revnum_t latest_revision,
                               apr_pool_t *pool)
{
  svn_revnum_t provided_start_rev = SVN_INVALID_REVNUM;

  /* Ensure that the start revision is something we can handle.  We
     want a number >= 0.  If unspecified, make it a number (r0) --
     anything else is bogus.  */
  if (opt_baton->start_revision.kind == svn_opt_revision_number)
    {
      provided_start_rev = opt_baton->start_revision.value.number;
    }
  else if (opt_baton->start_revision.kind == svn_opt_revision_unspecified)
    {
      opt_baton->start_revision.kind = svn_opt_revision_number;
      opt_baton->start_revision.value.number = 0;
    }

  if (opt_baton->start_revision.kind != svn_opt_revision_number)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("Unsupported revision specifier used; use "
                                "only integer values or 'HEAD'"));
    }

  if ((opt_baton->start_revision.value.number < 0) ||
      (opt_baton->start_revision.value.number > latest_revision))
    {
      return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                               _("Revision '%ld' does not exist"),
                               opt_baton->start_revision.value.number);
    }

  /* Ensure that the end revision is something we can handle.  We want
     a number <= the youngest, and > the start revision.  If
     unspecified, make it a number (start_revision + 1 if that was
     specified, the youngest revision in the repository otherwise) --
     anything else is bogus.  */
  if (opt_baton->end_revision.kind == svn_opt_revision_unspecified)
    {
      opt_baton->end_revision.kind = svn_opt_revision_number;
      if (SVN_IS_VALID_REVNUM(provided_start_rev))
        opt_baton->end_revision.value.number = provided_start_rev;
      else
        opt_baton->end_revision.value.number = latest_revision;
    }

  if (opt_baton->end_revision.kind != svn_opt_revision_number)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("Unsupported revision specifier used; use "
                                "only integer values or 'HEAD'"));
    }

  if ((opt_baton->end_revision.value.number < 0) ||
      (opt_baton->end_revision.value.number > latest_revision))
    {
      return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                               _("Revision '%ld' does not exist"),
                               opt_baton->end_revision.value.number);
    }

  /* Finally, make sure that the end revision is younger than the
     start revision.  We don't do "backwards" 'round here.  */
  if (opt_baton->end_revision.value.number < 
      opt_baton->start_revision.value.number)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("LOWER revision cannot be greater than "
                                "UPPER revision; consider reversing your "
                                "revision range"));
    }
  return SVN_NO_ERROR;
}

int
main(int argc, const char **argv)
{
  svn_error_t *err = SVN_NO_ERROR;
  const svn_opt_subcommand_desc2_t *subcommand = NULL;
  opt_baton_t *opt_baton;
  svn_revnum_t latest_revision = SVN_INVALID_REVNUM;
  apr_pool_t *pool = NULL;
  const char *config_dir = NULL;
  const char *username = NULL;
  const char *password = NULL;
  svn_boolean_t no_auth_cache = FALSE;
  svn_boolean_t non_interactive = FALSE;
  apr_array_header_t *config_options = NULL;
  apr_getopt_t *os;
  const char *first_arg;
  apr_array_header_t *received_opts;
  int i;

  if (svn_cmdline_init ("svnrdump", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  pool = svn_pool_create(NULL);
  opt_baton = apr_pcalloc(pool, sizeof(*opt_baton));
  opt_baton->start_revision.kind = svn_opt_revision_unspecified;
  opt_baton->end_revision.kind = svn_opt_revision_unspecified;
  opt_baton->url = NULL;

  SVNRDUMP_ERR(svn_cmdline__getopt_init(&os, argc, argv, pool));

  os->interleave = TRUE; /* Options and arguments can be interleaved */

  /* Set up our cancellation support. */
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

  received_opts = apr_array_make(pool, SVN_OPT_MAX_OPTIONS, sizeof(int));

  while (1)
    {
      int opt;
      const char *opt_arg;
      apr_status_t status = apr_getopt_long(os, svnrdump__options, &opt,
                                            &opt_arg);

      if (APR_STATUS_IS_EOF(status))
        break;
      if (status != APR_SUCCESS)
        {
          SVNRDUMP_ERR(usage(argv[0], pool));
          exit(EXIT_FAILURE);
        }

      /* Stash the option code in an array before parsing it. */
      APR_ARRAY_PUSH(received_opts, int) = opt;

      switch(opt)
        {
        case 'r':
          {
            /* Make sure we've not seen -r already. */
            if (opt_baton->start_revision.kind != svn_opt_revision_unspecified)
              {
                err = svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                       _("Multiple revision arguments "
                                         "encountered; try '-r N:M' instead "
                                         "of '-r N -r M'"));
                return svn_cmdline_handle_exit_error(err, pool, "svnrdump: ");
              }
            /* Parse the -r argument. */
            if (svn_opt_parse_revision(&(opt_baton->start_revision),
                                       &(opt_baton->end_revision),
                                       opt_arg, pool) != 0)
              {
                const char *utf8_opt_arg;
                err = svn_utf_cstring_to_utf8(&utf8_opt_arg, opt_arg, pool);
                if (! err)
                  err = svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                          _("Syntax error in revision "
                                            "argument '%s'"), utf8_opt_arg);
                return svn_cmdline_handle_exit_error(err, pool, "svnrdump: ");
              }
          }
          break;
        case 'q':
          opt_baton->quiet = TRUE;
          break;
        case opt_config_dir:
          config_dir = opt_arg;
          break;
        case opt_version:
          SVNRDUMP_ERR(version(argv[0], pool));
          exit(EXIT_SUCCESS);
          break;
        case 'h':
          SVNRDUMP_ERR(help_cmd(os, opt_baton, pool));
          exit(EXIT_SUCCESS);
          break;
        case opt_auth_username:
          SVNRDUMP_ERR(svn_utf_cstring_to_utf8(&username, opt_arg, pool));
          break;
        case opt_auth_password:
          SVNRDUMP_ERR(svn_utf_cstring_to_utf8(&password, opt_arg, pool));
          break;
        case opt_auth_nocache:
          no_auth_cache = TRUE;
          break;
        case opt_non_interactive:
          non_interactive = TRUE;
          break;
        case opt_config_option:
          if (!config_options)
              config_options =
                    apr_array_make(pool, 1,
                                   sizeof(svn_cmdline__config_argument_t*));

            SVNRDUMP_ERR(svn_utf_cstring_to_utf8(&opt_arg, opt_arg, pool));
            SVNRDUMP_ERR(svn_cmdline__parse_config_option(config_options,
                                                          opt_arg, pool));
        }
    }

  if (os->ind >= os->argc)
    {
      svn_error_clear(svn_cmdline_fprintf(stderr, pool,
                                          _("Subcommand argument required\n")));
      SVNRDUMP_ERR(help_cmd(NULL, NULL, pool));
      svn_pool_destroy(pool);
      exit(EXIT_FAILURE);
    }

  first_arg = os->argv[os->ind++];

  subcommand = svn_opt_get_canonical_subcommand2(svnrdump__cmd_table,
                                                 first_arg);

  if (subcommand == NULL)
    {
      const char *first_arg_utf8;
      err = svn_utf_cstring_to_utf8(&first_arg_utf8, first_arg, pool);
      if (err)
        return svn_cmdline_handle_exit_error(err, pool, "svnrdump: ");
      svn_error_clear(svn_cmdline_fprintf(stderr, pool,
                                          _("Unknown command: '%s'\n"),
                                          first_arg_utf8));
      SVNRDUMP_ERR(help_cmd(NULL, NULL, pool));
      svn_pool_destroy(pool);
      exit(EXIT_FAILURE);
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
            svn_opt_get_option_from_code2(opt_id, svnrdump__options,
                                          subcommand, pool);
          svn_opt_format_option(&optstr, badopt, FALSE, pool);
          if (subcommand->name[0] == '-')
            help_cmd(NULL, NULL, pool);
          else
            svn_error_clear
              (svn_cmdline_fprintf
               (stderr, pool,
                _("Subcommand '%s' doesn't accept option '%s'\n"
                  "Type 'svnrdump help %s' for usage.\n"),
                subcommand->name, optstr, subcommand->name));
          svn_pool_destroy(pool);
          return EXIT_FAILURE;
        }
    }

  if (subcommand && strcmp(subcommand->name, "help") == 0)
    {
      SVNRDUMP_ERR(help_cmd(os, opt_baton, pool));
      exit(EXIT_SUCCESS);
    }

  /* Only continue if the only not option argument is a url */
  if ((os->ind != os->argc-1)
      || !svn_path_is_url(os->argv[os->ind]))
    {
      SVNRDUMP_ERR(usage(argv[0], pool));
      exit(EXIT_FAILURE);
    }

  SVNRDUMP_ERR(svn_utf_cstring_to_utf8(&(opt_baton->url),
                                       os->argv[os->ind], pool));

  opt_baton->url = svn_uri_canonicalize(os->argv[os->ind], pool);

  SVNRDUMP_ERR(open_connection(&(opt_baton->session),
                               opt_baton->url,
                               non_interactive,
                               username,
                               password,
                               config_dir,
                               no_auth_cache,
                               config_options,
                               pool));

  /* Have sane opt_baton->start_revision and end_revision defaults if
     unspecified.  */
  SVNRDUMP_ERR(svn_ra_get_latest_revnum(opt_baton->session,
                                        &latest_revision, pool));

  /* Make sure any provided revisions make sense. */
  SVNRDUMP_ERR(validate_and_resolve_revisions(opt_baton, 
                                              latest_revision, pool));

  /* Dispatch the subcommand */
  SVNRDUMP_ERR((*subcommand->cmd_func)(os, opt_baton, pool));

  svn_pool_destroy(pool);

  return EXIT_SUCCESS;
}
