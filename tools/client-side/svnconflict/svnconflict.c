/*
 * svnconflict.c:  Non-interactive conflict resolution tool for Subversion.
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

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_config.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_opt.h"
#include "svn_utf.h"
#include "svn_auth.h"
#include "svn_hash.h"
#include "svn_version.h"

#include "private/svn_opt_private.h"
#include "private/svn_cmdline_private.h"
#include "private/svn_subr_private.h"

#include "svn_private_config.h"

typedef struct svnconflict_opt_state_t {
  svn_boolean_t version;         /* print version information */
  svn_boolean_t help;            /* print usage message */
  const char *auth_username;     /* auth username */
  const char *auth_password;     /* auth password */
  const char *config_dir;        /* over-riding configuration directory */
  apr_array_header_t *config_options; /* over-riding configuration options */
} svnconflict_opt_state_t;

typedef struct svnconflict_cmd_baton_t
{
  svnconflict_opt_state_t *opt_state;
  svn_client_ctx_t *ctx;
} svnconflict_cmd_baton_t;


/*** Option Processing ***/

/* Add an identifier here for long options that don't have a short
   option. Options that have both long and short options should just
   use the short option letter as identifier.  */
typedef enum svnconflict_longopt_t {
  opt_auth_password = SVN_OPT_FIRST_LONGOPT_ID,
  opt_auth_password_from_stdin,
  opt_auth_username,
  opt_config_dir,
  opt_config_options,
  opt_version,
} svnconflict_longopt_t;

/* Option codes and descriptions.
 * The entire list must be terminated with an entry of nulls. */
static const apr_getopt_option_t svnconflict_options[] =
{
  {"help",          'h', 0, N_("show help on a subcommand")},
  {NULL,            '?', 0, N_("show help on a subcommand")},
  {"version",       opt_version, 0, N_("show program version information")},
  {"username",      opt_auth_username, 1, N_("specify a username ARG")},
  {"password",      opt_auth_password, 1,
                    N_("specify a password ARG (caution: on many operating\n"
                       "                             "
                       "systems, other users will be able to see this)")},
  {"password-from-stdin",
                    opt_auth_password_from_stdin, 0,
                    N_("read password from stdin")},
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
  {0,               0, 0, 0},
};



/*** Command dispatch. ***/

/* Forward declarations. */
static svn_error_t * svnconflict_help(apr_getopt_t *, void *, apr_pool_t *);
static svn_error_t * svnconflict_list(apr_getopt_t *, void *, apr_pool_t *);
static svn_error_t * svnconflict_options_text(apr_getopt_t *, void *,
                                              apr_pool_t *);
static svn_error_t * svnconflict_options_prop(apr_getopt_t *, void *,
                                              apr_pool_t *);
static svn_error_t * svnconflict_options_tree(apr_getopt_t *, void *,
                                              apr_pool_t *);
static svn_error_t * svnconflict_resolve_text(apr_getopt_t *, void *,
                                              apr_pool_t *);
static svn_error_t * svnconflict_resolve_prop(apr_getopt_t *, void *,
                                              apr_pool_t *);
static svn_error_t * svnconflict_resolve_tree(apr_getopt_t *, void *,
                                              apr_pool_t *);

/* Our array of available subcommands.
 *
 * The entire list must be terminated with an entry of nulls.
 *
 * In most of the help text "PATH" is used where a working copy path is
 * required, "URL" where a repository URL is required and "TARGET" when
 * either a path or a url can be used.  Hmm, should this be part of the
 * help text?
 */

/* Options that apply to all commands. */
static const int svnconflict_global_options[] =
{ opt_auth_username, opt_auth_password, opt_auth_password_from_stdin,
  opt_config_dir, opt_config_options, 0 };

static const svn_opt_subcommand_desc2_t svnconflict_cmd_table[] =
{
  /* This command is also invoked if we see option "--help", "-h" or "-?". */
  { "help", svnconflict_help, {"?", "h"}, N_
    ("Describe the usage of this program or its subcommands.\n"
     "usage: help [SUBCOMMAND...]\n"),
    {0} },

  { "list", svnconflict_list, {"ls"}, N_
    ("List conflicts at a conflicted path.\n"
     "usage: list PATH\n"
     "\n"
     "  List conflicts at PATH, one per line. Possible conflicts are:\n"
     "  \n"
     "  text-conflict\n"
     "    One or more text merge conflicts are present in a file.\n"
     "    This conflict can be resolved with the resolve-text subcommand.\n"
     "  \n"
     "  prop-conflict: PROPNAME\n"
     "    The property PROPNAME contains a text merge conflic conflict.\n"
     "    This conflict can be resolved with the resolve-prop subcommand.\n"
     "  \n"
     "  tree-conflict: DESCRIPTION\n"
     "    The PATH is a victim of a tree conflict described by DESCRIPTION.\n"
     "    This conflict can be resolved with the resolve-tree subcommand.\n"
     "    If a tree conflict exists, no text or property conflicts exist.\n"
     "  \n"
     "  If PATH is not in conflict, the exit code will be 1, and 0 otherwise.\n"
     ""),
    {0}, },

  { "options-text", svnconflict_options_text, {0}, N_
    ("List options for resolving a text conflict at path.\n"
     "usage: options-text PATH\n"
     "\n"
     "  List text conflict resolution options at PATH, one per line.\n"
     "  Each line contains a numeric option ID, a colon, and a description.\n"
     "  If PATH is not in conflict, the exit code will be 1, and 0 otherwise.\n"
     ""),
    {0}, },

  { "options-prop", svnconflict_options_prop, {0}, N_
    ("List options for resolving a property conflict at path.\n"
     "usage: options-prop PATH\n"
     "\n"
     "  List property conflict resolution options at PATH, one per line.\n"
     "  Each line contains a numeric option ID, a colon, and a description.\n"
     "  If PATH is not in conflict, the exit code will be 1, and 0 otherwise.\n"
     ""),
    {0}, },

  { "options-tree", svnconflict_options_tree, {0}, N_
    ("List options for resolving a tree conflict at path.\n"
     "usage: options-tree PATH\n"
     "\n"
     "  List tree conflict resolution options at PATH, one per line.\n"
     "  Each line contains a numeric option ID, a colon, and a description.\n"
     "  If PATH is not in conflict, the exit code will be 1, and 0 otherwise.\n"
     ""),
    {0}, },

  { "resolve-text", svnconflict_resolve_text, {0}, N_
    ("Resolve the text conflict at path.\n"
     "usage: resolve-text OPTION_ID PATH\n"
     "\n"
     "  Resolve the text conflict at PATH with a given resolution option.\n"
     "  If PATH is not in conflict, the exit code will be 1, and 0 otherwise.\n"
     ""),
    {0}, },

  { "resolve-prop", svnconflict_resolve_prop, {0}, N_
    ("Resolve the property conflict at path.\n"
     "usage: resolve-prop PROPNAME OPTION_ID PATH\n"
     "\n"
     "  Resolve conflicted property PROPNAME at PATH with a given resolution option.\n"
     "  If PATH is not in conflict, the exit code will be 1, and 0 otherwise.\n"
     ""),
    {0}, },

  { "resolve-tree", svnconflict_resolve_tree, {0}, N_
    ("Resolve the tree conflict at path.\n"
     "usage: resolve-tree OPTION_ID PATH\n"
     "\n"
     "  Resolve the tree conflict at PATH with a given resolution option.\n"
     "  If PATH is not in conflict, the exit code will be 1, and 0 otherwise.\n"
     ""),
    {0}, },

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
      { NULL, NULL }
    };
  SVN_VERSION_DEFINE(my_version);

  return svn_ver_check_list2(&my_version, checklist, svn_ver_equal);
}


/*** Subcommands ***/

/* This implements the `svn_opt_subcommand_t' interface. */
static svn_error_t *
svnconflict_help(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svnconflict_cmd_baton_t *b = baton;
  svnconflict_opt_state_t *opt_state = b ? b->opt_state : NULL;
  char help_header[] =
  N_("usage: svnconflict <subcommand> [args]\n"
     "Type 'svnconflict --version' to see the program version and RA modules,\n"
     "\n"
     "svnconflict provides a non-interactive conflict resolution interface.\n"
     "It is intended for use by non-interactive scripts which cannot make\n"
     "use of interactive conflict resolution provided by 'svn resolve'.\n"
     "\n"
     "svnconflict operates on a single working copy path only. It is assumed that\n"
     "scripts are able to discover conflicted paths in the working copy via other\n"
     "means, such as 'svn status'.\n"
     "Some advanced operations offered by 'svn resolve' are not supported.\n"
     "\n"
     "svnconflict may contact the repository to obtain information about a conflict.\n"
     "It will never modify the repository, but only read information from it.\n"
     "svnconflict will not prompt for credentials. If read-access to the repository\n"
     "requires credentials but no suitable credentials are stored in Subversion's\n"
     "authentication cache or provided on the command line, the operation may fail.\n"
     "\nAvailable subcommands:\n");
  char help_footer[] =
  N_("Subversion is a tool for version control.\n"
     "For additional information, see http://subversion.apache.org/\n");
  const char *ra_desc_start
    = _("The following repository access (RA) modules are available:\n\n");
  svn_stringbuf_t *version_footer = svn_stringbuf_create_empty(pool);

  if (opt_state && opt_state->version)
    {
      svn_stringbuf_appendcstr(version_footer, ra_desc_start);
      SVN_ERR(svn_ra_print_modules(version_footer, pool));
    }

  SVN_ERR(svn_opt_print_help4(os,
                              "svnconflict",   /* ### erm, derive somehow? */
                              opt_state ? opt_state->version : FALSE,
                              FALSE, /* quiet */
                              FALSE, /* verbose */
                              version_footer->data,
                              _(help_header),
                              svnconflict_cmd_table,
                              svnconflict_options,
                              svnconflict_global_options,
                              _(help_footer),
                              pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
get_conflicts(svn_boolean_t *text_conflicted,
              apr_array_header_t **props_conflicted,
              svn_boolean_t *tree_conflicted,
              svn_client_conflict_t **conflict,
              const char *local_abspath,
              svn_client_ctx_t *ctx,
              apr_pool_t *pool)
{
  svn_boolean_t text;
  apr_array_header_t *props;
  svn_boolean_t tree;

  SVN_ERR(svn_client_conflict_get(conflict, local_abspath, ctx, pool, pool));
  SVN_ERR(svn_client_conflict_get_conflicted(&text, &props, &tree,
                                             *conflict, pool, pool));

  if (!text && props->nelts == 0 && !tree)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("The path '%s' is not in conflict"),
                             local_abspath);

  if (text_conflicted)
    *text_conflicted = text;
  if (props_conflicted)
    *props_conflicted = props;
  if (tree_conflicted)
    *tree_conflicted = tree;

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
static svn_error_t *
svnconflict_list(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svnconflict_cmd_baton_t *b = baton;
  svn_client_ctx_t *ctx = b->ctx;
  apr_array_header_t *args;
  const char *path;
  const char *local_abspath;
  svn_client_conflict_t *conflict;
  svn_boolean_t text_conflicted;
  apr_array_header_t *props_conflicted;
  svn_boolean_t tree_conflicted;
  int i;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, pool));
  path = APR_ARRAY_IDX(args, 0, const char *);
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(get_conflicts(&text_conflicted, &props_conflicted, &tree_conflicted,
                        &conflict, local_abspath, ctx, pool));

  if (text_conflicted)
    svn_cmdline_printf(pool, "text-conflict\n");

  for (i = 0; i < props_conflicted->nelts; i++)
    {
      const char *propname = APR_ARRAY_IDX(props_conflicted, i, const char *); 
      svn_cmdline_printf(pool, "prop-conflict: %s\n", propname);
    }

  if (tree_conflicted)
    {
      const char *incoming_change;
      const char *local_change;

      SVN_ERR(svn_client_conflict_tree_get_description(&incoming_change,
                                                       &local_change,
                                                       conflict, ctx,
                                                       pool, pool));
      svn_cmdline_printf(pool, "tree-conflict: %s %s\n",
                         incoming_change, local_change);
    }

  return SVN_NO_ERROR;
}

static void
print_conflict_options(apr_array_header_t *options, apr_pool_t *pool)
{
  int i;

  for (i = 0; i < options->nelts; i++)
    {
      svn_client_conflict_option_t *option;
      svn_client_conflict_option_id_t id;
      const char *label;

      option = APR_ARRAY_IDX(options, i, svn_client_conflict_option_t *);
      id = svn_client_conflict_option_get_id(option);
      label = svn_client_conflict_option_get_label(option, pool);
      svn_cmdline_printf(pool, "%d: %s\n", id, label);
    }
}

/* This implements the `svn_opt_subcommand_t' interface. */
static svn_error_t *
svnconflict_options_text(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svnconflict_cmd_baton_t *b = baton;
  svn_client_ctx_t *ctx = b->ctx;
  apr_array_header_t *args;
  const char *path;
  const char *local_abspath;
  svn_client_conflict_t *conflict;
  svn_boolean_t text_conflicted;
  apr_array_header_t *options;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, pool));
  path = APR_ARRAY_IDX(args, 0, const char *);
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(get_conflicts(&text_conflicted, NULL, NULL,
                        &conflict, local_abspath, ctx, pool));

  if (!text_conflicted)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("The path '%s' has no text conflict"),
                             local_abspath);

  SVN_ERR(svn_client_conflict_text_get_resolution_options(&options,
                                                          conflict, ctx,
                                                          pool, pool));
  print_conflict_options(options, pool);

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
static svn_error_t *
svnconflict_options_prop(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svnconflict_cmd_baton_t *b = baton;
  svn_client_ctx_t *ctx = b->ctx;
  apr_array_header_t *args;
  const char *path;
  const char *local_abspath;
  svn_client_conflict_t *conflict;
  apr_array_header_t *props_conflicted;
  apr_array_header_t *options;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, pool));
  path = APR_ARRAY_IDX(args, 0, const char *);
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(get_conflicts(NULL, &props_conflicted, NULL,
                        &conflict, local_abspath, ctx, pool));

  if (props_conflicted->nelts == 0)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("The path '%s' has no property conflict"),
                             local_abspath);

  SVN_ERR(svn_client_conflict_prop_get_resolution_options(&options,
                                                          conflict, ctx,
                                                          pool, pool));
  print_conflict_options(options, pool);

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
static svn_error_t *
svnconflict_options_tree(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svnconflict_cmd_baton_t *b = baton;
  svn_client_ctx_t *ctx = b->ctx;
  apr_array_header_t *args;
  const char *path;
  const char *local_abspath;
  svn_client_conflict_t *conflict;
  svn_boolean_t tree_conflicted;
  apr_array_header_t *options;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, pool));
  path = APR_ARRAY_IDX(args, 0, const char *);
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(get_conflicts(NULL, NULL, &tree_conflicted,
                        &conflict, local_abspath, ctx, pool));

  if (!tree_conflicted)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("The path '%s' is not a tree conflict victim"),
                             local_abspath);

  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, pool));
  SVN_ERR(svn_client_conflict_tree_get_resolution_options(&options,
                                                          conflict, ctx,
                                                          pool, pool));
  print_conflict_options(options, pool);

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
static svn_error_t *
svnconflict_resolve_text(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svnconflict_cmd_baton_t *b = baton;
  svn_client_ctx_t *ctx = b->ctx;
  apr_array_header_t *args;
  const char *option_id_str;
  int optid;
  svn_client_conflict_option_id_t option_id;
  const char *path;
  const char *local_abspath;
  svn_client_conflict_t *conflict;
  svn_boolean_t text_conflicted;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 2, pool));
  option_id_str = APR_ARRAY_IDX(args, 0, const char *);
  path = APR_ARRAY_IDX(args, 1, const char *);
  SVN_ERR(svn_cstring_atoi(&optid, option_id_str));
  option_id = (svn_client_conflict_option_id_t)optid;
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(get_conflicts(&text_conflicted, NULL, NULL,
                        &conflict, local_abspath, ctx, pool));

  if (!text_conflicted)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("The path '%s' has no text conflict"),
                             local_abspath);

  SVN_ERR(svn_client_conflict_text_resolve_by_id(conflict, option_id, ctx,
                                                 pool));

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
static svn_error_t *
svnconflict_resolve_prop(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svnconflict_cmd_baton_t *b = baton;
  svn_client_ctx_t *ctx = b->ctx;
  apr_array_header_t *args;
  const char *option_id_str;
  int optid;
  svn_client_conflict_option_id_t option_id;
  const char *path;
  const char *propname;
  const char *local_abspath;
  svn_client_conflict_t *conflict;
  apr_array_header_t *props_conflicted;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 3, pool));
  propname = APR_ARRAY_IDX(args, 0, const char *);
  option_id_str = APR_ARRAY_IDX(args, 1, const char *);
  path = APR_ARRAY_IDX(args, 2, const char *);
  SVN_ERR(svn_cstring_atoi(&optid, option_id_str));
  option_id = (svn_client_conflict_option_id_t)optid;
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(get_conflicts(NULL, &props_conflicted, NULL,
                        &conflict, local_abspath, ctx, pool));

  if (props_conflicted->nelts == 0)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("The path '%s' has no property conflict"),
                             local_abspath);

  SVN_ERR(svn_client_conflict_prop_resolve_by_id(conflict, propname,
                                                 option_id, ctx, pool));

  return SVN_NO_ERROR;
}

/* This implements the `svn_opt_subcommand_t' interface. */
static svn_error_t *
svnconflict_resolve_tree(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svnconflict_cmd_baton_t *b = baton;
  svn_client_ctx_t *ctx = b->ctx;
  apr_array_header_t *args;
  const char *option_id_str;
  int optid;
  svn_client_conflict_option_id_t option_id;
  const char *path;
  const char *local_abspath;
  svn_client_conflict_t *conflict;
  svn_boolean_t tree_conflicted;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 2, pool));
  option_id_str = APR_ARRAY_IDX(args, 0, const char *);
  path = APR_ARRAY_IDX(args, 1, const char *);
  SVN_ERR(svn_cstring_atoi(&optid, option_id_str));
  option_id = (svn_client_conflict_option_id_t)optid;
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(get_conflicts(NULL, NULL, &tree_conflicted,
                        &conflict, local_abspath, ctx, pool));

  if (!tree_conflicted)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("The path '%s' is not a tree conflict victim"),
                             local_abspath);

  SVN_ERR(svn_client_conflict_tree_get_details(conflict, ctx, pool));
  SVN_ERR(svn_client_conflict_tree_resolve_by_id(conflict, option_id, ctx,
                                                 pool));

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
  svnconflict_opt_state_t opt_state = { 0 };
  svn_client_ctx_t *ctx;
  apr_array_header_t *received_opts;
  svnconflict_cmd_baton_t command_baton;
  int i;
  const svn_opt_subcommand_desc2_t *subcommand = NULL;
  svn_auth_baton_t *ab;
  svn_config_t *cfg_config;
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

  /* No args?  Show usage. */
  if (argc <= 1)
    {
      SVN_ERR(svnconflict_help(NULL, NULL, pool));
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
      apr_status_t apr_err = apr_getopt_long(os, svnconflict_options, &opt_id,
                                             &opt_arg);
      if (APR_STATUS_IS_EOF(apr_err))
        break;
      else if (apr_err)
        {
          SVN_ERR(svnconflict_help(NULL, NULL, pool));
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
      case opt_version:
        opt_state.version = TRUE;
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
                                                 utf8_opt_arg, "svnconflict: ",
                                                 pool));
        break;
      default:
        break;
      }
    }

  /* ### This really belongs in libsvn_client. */
  SVN_ERR(svn_config_ensure(opt_state.config_dir, pool));

  /* If the user asked for help, then the rest of the arguments are
     the names of subcommands to get help on (if any), or else they're
     just typos/mistakes.  Whatever the case, the subcommand to
     actually run is svnconflict_help(). */
  if (opt_state.help)
    subcommand = svn_opt_get_canonical_subcommand2(svnconflict_cmd_table,
                                                   "help");

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
                { "--version", svnconflict_help, {0}, "",
                  {opt_version,    /* must accept its own option */
                   opt_config_dir  /* all commands accept this */
                  } };

              subcommand = &pseudo_cmd;
            }
          else
            {
              svn_error_clear
                (svn_cmdline_fprintf(stderr, pool,
                                     _("Subcommand argument required\n")));
              svn_error_clear(svnconflict_help(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
            }
        }
      else
        {
          const char *first_arg;

          SVN_ERR(svn_utf_cstring_to_utf8(&first_arg, os->argv[os->ind++],
                                          pool));
          subcommand = svn_opt_get_canonical_subcommand2(svnconflict_cmd_table,
                                                         first_arg);
          if (subcommand == NULL)
            {
              svn_error_clear
                (svn_cmdline_fprintf(stderr, pool,
                                     _("Unknown subcommand: '%s'\n"),
                                     first_arg));
              svn_error_clear(svnconflict_help(NULL, NULL, pool));
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
                                             svnconflict_global_options))
        {
          const char *optstr;
          const apr_getopt_option_t *badopt =
            svn_opt_get_option_from_code2(opt_id, svnconflict_options,
                                          subcommand, pool);
          svn_opt_format_option(&optstr, badopt, FALSE, pool);
          if (subcommand->name[0] == '-')
            svn_error_clear(svnconflict_help(NULL, NULL, pool));
          else
            svn_error_clear
              (svn_cmdline_fprintf
               (stderr, pool, _("Subcommand '%s' doesn't accept option '%s'\n"
                                "Type 'svnconflict help %s' for usage.\n"),
                subcommand->name, optstr, subcommand->name));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }
    }

  err = svn_config_get_config(&cfg_hash, opt_state.config_dir, pool);
  if (err)
    {
      /* Fallback to default config if the config directory isn't readable
         or is not a directory. */
      if (APR_STATUS_IS_EACCES(err->apr_err)
          || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err))
        {
          svn_handle_warning2(stderr, err, "svnconflict: ");
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
                                            "svnconflict: ",
                                            "--config-option"));
    }

  cfg_config = svn_hash_gets(cfg_hash, SVN_CONFIG_CATEGORY_CONFIG);

  /* Get password from stdin if necessary */
  if (read_pass_from_stdin)
    {
      SVN_ERR(svn_cmdline__stdin_readline(&opt_state.auth_password, pool, pool));
    }


  /* Create a client context object. */
  command_baton.opt_state = &opt_state;
  SVN_ERR(svn_client_create_context2(&ctx, cfg_hash, pool));
  command_baton.ctx = ctx;

  /* Set up Authentication stuff. */
  SVN_ERR(svn_cmdline_create_auth_baton2(
            &ab,
            TRUE, /* non-interactive */
            opt_state.auth_username,
            opt_state.auth_password,
            opt_state.config_dir,
            TRUE, /* no auth cache */
            FALSE, FALSE, FALSE, FALSE, FALSE, /* reject invalid SSL certs */
            cfg_config,
            NULL, NULL,
            pool));

  ctx->auth_baton = ab;

  /* We don't use legacy libsvn_wc conflict handlers. */
  {
    ctx->conflict_func = NULL;
    ctx->conflict_baton = NULL;
    ctx->conflict_func2 = NULL;
    ctx->conflict_baton2 = NULL;
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
          err = svn_error_quick_wrapf(
                  err, _("Try 'svnconflict help %s' for more information"),
                  subcommand->name);
        }
      if (err->apr_err == SVN_ERR_WC_UPGRADE_REQUIRED)
        {
          err = svn_error_quick_wrap(err,
                                     _("Please see the 'svn upgrade' command"));
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
      svn_cmdline_handle_exit_error(err, NULL, "svnconflict: ");
    }

  svn_pool_destroy(pool);

  svn_cmdline__cancellation_exit();

  return exit_code;
}
