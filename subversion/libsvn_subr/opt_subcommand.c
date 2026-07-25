/*
 * opt.c :  subcommand stuff &  help
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



#define APR_WANT_STRFUNC
#define APR_WANT_STDIO
#include <apr_want.h>

#include <assert.h>
#include <apr_general.h>

#include "svn_hash.h"
#include "svn_cmdline.h"
#include "svn_version.h"
#include "svn_types.h"
#include "svn_opt.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_utf.h"

#include "opt.h"
#include "svn_private_config.h"


/*** Code. ***/

const svn_opt_subcommand_desc3_t *
svn_opt_get_canonical_subcommand3(const svn_opt_subcommand_desc3_t *table,
                                  const char *cmd_name)
{
  int i = 0;

  if (cmd_name == NULL)
    return NULL;

  while (table[i].name) {
    int j;
    if (strcmp(cmd_name, table[i].name) == 0)
      return table + i;
    for (j = 0; (j < SVN_OPT_MAX_ALIASES) && table[i].aliases[j]; j++)
      if (strcmp(cmd_name, table[i].aliases[j]) == 0)
        return table + i;

    i++;
  }

  /* If we get here, there was no matching subcommand name or alias. */
  return NULL;
}

const apr_getopt_option_t *
svn_opt_get_option_from_code3(int code,
                              const apr_getopt_option_t *option_table,
                              const svn_opt_subcommand_desc3_t *command,
                              apr_pool_t *pool)
{
  apr_size_t i;

  for (i = 0; option_table[i].optch; i++)
    if (option_table[i].optch == code)
      {
        if (command)
          {
            int j;

            for (j = 0; ((j < SVN_OPT_MAX_OPTIONS) &&
                         command->desc_overrides[j].optch); j++)
              if (command->desc_overrides[j].optch == code)
                {
                  apr_getopt_option_t *tmpopt =
                      apr_palloc(pool, sizeof(*tmpopt));
                  *tmpopt = option_table[i];
                  tmpopt->description = command->desc_overrides[j].desc;
                  return tmpopt;
                }
          }
        return &(option_table[i]);
      }

  return NULL;
}

/* Like svn_opt_get_option_from_code3(), but also, if CODE appears a second
 * time in OPTION_TABLE with a different name, then set *LONG_ALIAS to that
 * second name, else set it to NULL. */
static const apr_getopt_option_t *
get_option_from_code3(const char **long_alias,
                      int code,
                      const apr_getopt_option_t *option_table,
                      const svn_opt_subcommand_desc3_t *command,
                      apr_pool_t *pool)
{
  const apr_getopt_option_t *i;
  const apr_getopt_option_t *opt
    = svn_opt_get_option_from_code3(code, option_table, command, pool);

  /* Find a long alias in the table, if there is one. */
  *long_alias = NULL;
  for (i = option_table; i->optch; i++)
    {
      if (i->optch == code && i->name != opt->name)
        {
          *long_alias = i->name;
          break;
        }
    }

  return opt;
}


/* Print an option OPT nicely into a STRING allocated in POOL.
 * If OPT has a single-character short form, then print OPT->name (if not
 * NULL) as an alias, else print LONG_ALIAS (if not NULL) as an alias.
 * If DOC is set, include the generic documentation string of OPT,
 * localized to the current locale if a translation is available.
 */
static void
format_option(const char **string,
              const apr_getopt_option_t *opt,
              const char *long_alias,
              svn_boolean_t doc,
              apr_pool_t *pool)
{
  char *opts;

  if (opt == NULL)
    {
      *string = "?";
      return;
    }

  /* We have a valid option which may or may not have a "short
     name" (a single-character alias for the long option). */
  if (opt->optch <= 255)
    opts = apr_psprintf(pool, "-%c [--%s]", opt->optch, opt->name);
  else if (long_alias)
    opts = apr_psprintf(pool, "--%s [--%s]", opt->name, long_alias);
  else
    opts = apr_psprintf(pool, "--%s", opt->name);

  if (opt->has_arg)
    opts = apr_pstrcat(pool, opts, _(" ARG"), SVN_VA_NULL);

  if (doc)
    opts = apr_psprintf(pool, "%-24s : %s", opts, _(opt->description));

  *string = opts;
}

void
svn_opt_format_option(const char **string,
                      const apr_getopt_option_t *opt,
                      svn_boolean_t doc,
                      apr_pool_t *pool)
{
  format_option(string, opt, NULL, doc, pool);
}


svn_boolean_t
svn_opt_subcommand_takes_option4(const svn_opt_subcommand_desc3_t *command,
                                 int option_code,
                                 const int *global_options)
{
  apr_size_t i;

  for (i = 0; i < SVN_OPT_MAX_OPTIONS; i++)
    if (command->valid_options[i] == option_code)
      return TRUE;

  if (global_options)
    for (i = 0; global_options[i]; i++)
      if (global_options[i] == option_code)
        return TRUE;

  return FALSE;
}


/* Print the canonical command name for CMD, and all its aliases, to
   STREAM.  If HELP is set, print CMD's help string too, in which case
   obtain option usage from OPTIONS_TABLE.

   Include global and experimental options iff VERBOSE is true.
 */
static svn_error_t *
print_command_info3(const svn_opt_subcommand_desc3_t *cmd,
                    const apr_getopt_option_t *options_table,
                    const int *global_options,
                    svn_boolean_t help,
                    svn_boolean_t verbose,
                    apr_pool_t *pool,
                    FILE *stream)
{
  svn_boolean_t first_time;
  apr_size_t i;

  /* Print the canonical command name. */
  SVN_ERR(svn_cmdline_fputs(cmd->name, stream, pool));

  /* Print the list of aliases. */
  first_time = TRUE;
  for (i = 0; i < SVN_OPT_MAX_ALIASES; i++)
    {
      if (cmd->aliases[i] == NULL)
        break;

      if (first_time) {
        SVN_ERR(svn_cmdline_fputs(" (", stream, pool));
        first_time = FALSE;
      }
      else
        SVN_ERR(svn_cmdline_fputs(", ", stream, pool));

      SVN_ERR(svn_cmdline_fputs(cmd->aliases[i], stream, pool));
    }

  if (! first_time)
    SVN_ERR(svn_cmdline_fputs(")", stream, pool));

  if (help)
    {
      const apr_getopt_option_t *option;
      const char *long_alias;
      svn_boolean_t have_options = FALSE;
      svn_boolean_t have_experimental = FALSE;

      SVN_ERR(svn_cmdline_fprintf(stream, pool, ": "));

      for (i = 0; i < SVN_OPT_MAX_PARAGRAPHS && cmd->help[i]; i++)
        {
          SVN_ERR(svn_cmdline_fprintf(stream, pool, "%s", _(cmd->help[i])));
        }

      /* Loop over all valid option codes attached to the subcommand */
      for (i = 0; i < SVN_OPT_MAX_OPTIONS; i++)
        {
          if (cmd->valid_options[i])
            {
              if (!have_options)
                {
                  SVN_ERR(svn_cmdline_fputs(_("\nValid options:\n"),
                                            stream, pool));
                  have_options = TRUE;
                }

              /* convert each option code into an option */
              option = get_option_from_code3(&long_alias, cmd->valid_options[i],
                                             options_table, cmd, pool);

              /* print the option's docstring */
              if (option && option->description)
                {
                  const char *optstr;

                  if (option->name && strncmp(option->name, "x-", 2) == 0)
                    {
                      if (verbose && !have_experimental)
                        SVN_ERR(svn_cmdline_fputs(_("\nExperimental options:\n"),
                                                  stream, pool));
                      have_experimental = TRUE;
                      if (!verbose)
                        continue;
                    }

                  format_option(&optstr, option, long_alias, TRUE, pool);
                  SVN_ERR(svn_cmdline_fprintf(stream, pool, "  %s\n",
                                              optstr));
                }
            }
        }
      /* And global options too */
      if (verbose && global_options && *global_options)
        {
          SVN_ERR(svn_cmdline_fputs(_("\nGlobal options:\n"),
                                    stream, pool));
          have_options = TRUE;

          for (i = 0; global_options[i]; i++)
            {

              /* convert each option code into an option */
              option = get_option_from_code3(&long_alias, global_options[i],
                                             options_table, cmd, pool);

              /* print the option's docstring */
              if (option && option->description)
                {
                  const char *optstr;
                  format_option(&optstr, option, long_alias, TRUE, pool);
                  SVN_ERR(svn_cmdline_fprintf(stream, pool, "  %s\n",
                                              optstr));
                }
            }
        }

      if (!verbose && global_options && *global_options)
        SVN_ERR(svn_cmdline_fputs(_("\n(Use '-v' to show global and experimental options.)\n"),
                                  stream, pool));
      if (have_options)
        SVN_ERR(svn_cmdline_fprintf(stream, pool, "\n"));
    }

  return SVN_NO_ERROR;
}

/* The body for svn_opt_print_generic_help3() function with standard error
 * handling semantic. Handling of errors implemented at caller side. */
static svn_error_t *
print_generic_help_body3(const char *header,
                         const svn_opt_subcommand_desc3_t *cmd_table,
                         const apr_getopt_option_t *opt_table,
                         const char *footer,
                         svn_boolean_t with_experimental,
                         apr_pool_t *pool, FILE *stream)
{
  svn_boolean_t have_experimental = FALSE;
  int i;

  if (header)
    SVN_ERR(svn_cmdline_fputs(header, stream, pool));

  for (i = 0; cmd_table[i].name; i++)
    {
      if (strncmp(cmd_table[i].name, "x-", 2) == 0)
        {
          if (with_experimental && !have_experimental)
            SVN_ERR(svn_cmdline_fputs(_("\nExperimental subcommands:\n"),
                                      stream, pool));
          have_experimental = TRUE;
          if (!with_experimental)
            continue;
        }
      SVN_ERR(svn_cmdline_fputs("   ", stream, pool));
      SVN_ERR(print_command_info3(cmd_table + i, opt_table,
                                  NULL, FALSE, FALSE,
                                  pool, stream));
      SVN_ERR(svn_cmdline_fputs("\n", stream, pool));
    }

  if (have_experimental && !with_experimental)
    SVN_ERR(svn_cmdline_fputs(_("\n(Use '-v' to show experimental subcommands.)\n"),
                              stream, pool));

  SVN_ERR(svn_cmdline_fputs("\n", stream, pool));

  if (footer)
    SVN_ERR(svn_cmdline_fputs(footer, stream, pool));

  return SVN_NO_ERROR;
}

static void
print_generic_help(const char *header,
                   const svn_opt_subcommand_desc3_t *cmd_table,
                   const apr_getopt_option_t *opt_table,
                   const char *footer,
                   svn_boolean_t with_experimental,
                   apr_pool_t *pool, FILE *stream)
{
  svn_error_t *err;

  err = print_generic_help_body3(header, cmd_table, opt_table, footer,
                                 with_experimental,
                                 pool, stream);

  /* Issue #3014:
   * Don't print anything on broken pipes. The pipe was likely
   * closed by the process at the other end. We expect that
   * process to perform error reporting as necessary.
   *
   * ### This assumes that there is only one error in a chain for
   * ### SVN_ERR_IO_PIPE_WRITE_ERROR. See svn_cmdline_fputs(). */
  if (err && err->apr_err != SVN_ERR_IO_PIPE_WRITE_ERROR)
    svn_handle_error2(err, stderr, FALSE, "svn: ");
  svn_error_clear(err);
}

void
svn_opt_print_generic_help3(const char *header,
                            const svn_opt_subcommand_desc3_t *cmd_table,
                            const apr_getopt_option_t *opt_table,
                            const char *footer,
                            apr_pool_t *pool, FILE *stream)
{
  print_generic_help(header, cmd_table, opt_table, footer,
                     TRUE, pool, stream);
}


/* The body of svn_opt_subcommand_help4(), which see.
 *
 * VERBOSE means show also the subcommand's global and experimental options.
 */
static void
subcommand_help(const char *subcommand,
                const svn_opt_subcommand_desc3_t *table,
                const apr_getopt_option_t *options_table,
                const int *global_options,
                svn_boolean_t verbose,
                apr_pool_t *pool)
{
  const svn_opt_subcommand_desc3_t *cmd =
    svn_opt_get_canonical_subcommand3(table, subcommand);
  svn_error_t *err;

  if (cmd)
    err = print_command_info3(cmd, options_table, global_options,
                              TRUE, verbose, pool, stdout);
  else
    err = svn_cmdline_fprintf(stderr, pool,
                              _("\"%s\": unknown command.\n\n"), subcommand);

  if (err) {
    /* Issue #3014: Don't print anything on broken pipes. */
    if (err->apr_err != SVN_ERR_IO_PIPE_WRITE_ERROR)
      svn_handle_error2(err, stderr, FALSE, "svn: ");
    svn_error_clear(err);
  }
}

void
svn_opt_subcommand_help4(const char *subcommand,
                         const svn_opt_subcommand_desc3_t *table,
                         const apr_getopt_option_t *options_table,
                         const int *global_options,
                         apr_pool_t *pool)
{
  subcommand_help(subcommand, table, options_table, global_options,
                  TRUE, pool);
}


svn_error_t *
svn_opt__print_version_info(const char *pgm_name,
                            const char *footer,
                            const svn_version_extended_t *info,
                            svn_boolean_t quiet,
                            svn_boolean_t verbose,
                            apr_pool_t *pool)
{
  if (quiet)
    return svn_cmdline_printf(pool, "%s\n", SVN_VER_NUMBER);

  SVN_ERR(svn_cmdline_printf(pool, _("%s, version %s\n"
                                     "   compiled %s, %s on %s\n\n"),
                             pgm_name, SVN_VERSION,
                             svn_version_ext_build_date(info),
                             svn_version_ext_build_time(info),
                             svn_version_ext_build_host(info)));
  SVN_ERR(svn_cmdline_printf(pool, "%s\n", svn_version_ext_copyright(info)));

  if (footer)
    {
      SVN_ERR(svn_cmdline_printf(pool, "%s\n", footer));
    }

  if (verbose)
    {
      const apr_array_header_t *libs;

      SVN_ERR(svn_cmdline_fputs(_("System information:\n\n"), stdout, pool));
      SVN_ERR(svn_cmdline_printf(pool, _("* running on %s\n"),
                                 svn_version_ext_runtime_host(info)));
      if (svn_version_ext_runtime_osname(info))
        {
          SVN_ERR(svn_cmdline_printf(pool, _("  - %s\n"),
                                     svn_version_ext_runtime_osname(info)));
        }

      SVN_ERR(svn_cmdline_printf(pool, _("  - character encoding: %s\n"),
                                 svn_version_ext_character_encoding(info)));

      libs = svn_version_ext_linked_libs(info);
      if (libs && libs->nelts)
        {
          const svn_version_ext_linked_lib_t *lib;
          int i;

          SVN_ERR(svn_cmdline_fputs(_("* linked dependencies:\n"),
                                    stdout, pool));
          for (i = 0; i < libs->nelts; ++i)
            {
              lib = &APR_ARRAY_IDX(libs, i, svn_version_ext_linked_lib_t);
              if (lib->runtime_version)
                SVN_ERR(svn_cmdline_printf(pool,
                                           "  - %s %s (compiled with %s)\n",
                                           lib->name,
                                           lib->runtime_version,
                                           lib->compiled_version));
              else
                SVN_ERR(svn_cmdline_printf(pool,
                                           "  - %s %s (static)\n",
                                           lib->name,
                                           lib->compiled_version));
            }
        }

      libs = svn_version_ext_loaded_libs(info);
      if (libs && libs->nelts)
        {
          const svn_version_ext_loaded_lib_t *lib;
          int i;

          SVN_ERR(svn_cmdline_fputs(_("* loaded shared libraries:\n"),
                                    stdout, pool));
          for (i = 0; i < libs->nelts; ++i)
            {
              lib = &APR_ARRAY_IDX(libs, i, svn_version_ext_loaded_lib_t);
              if (lib->version)
                SVN_ERR(svn_cmdline_printf(pool,
                                           "  - %s   (%s)\n",
                                           lib->name, lib->version));
              else
                SVN_ERR(svn_cmdline_printf(pool, "  - %s\n", lib->name));
            }
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_opt_print_help5(apr_getopt_t *os,
                    const char *pgm_name,
                    svn_boolean_t print_version,
                    svn_boolean_t quiet,
                    svn_boolean_t verbose,
                    const char *version_footer,
                    const char *header,
                    const svn_opt_subcommand_desc3_t *cmd_table,
                    const apr_getopt_option_t *option_table,
                    const int *global_options,
                    const char *footer,
                    apr_pool_t *pool)
{
  apr_array_header_t *targets = NULL;

  if (os)
    SVN_ERR(svn_opt_parse_all_args(&targets, os, pool));

  if (os && targets->nelts)  /* help on subcommand(s) requested */
    {
      int i;

      for (i = 0; i < targets->nelts; i++)
        {
          subcommand_help(APR_ARRAY_IDX(targets, i, const char *),
                          cmd_table, option_table, global_options,
                          verbose, pool);
        }
    }
  else if (print_version)   /* just --version */
    {
      SVN_ERR(svn_opt__print_version_info(pgm_name, version_footer,
                                          svn_version_extended(verbose, pool),
                                          quiet, verbose, pool));
    }
  else if (os && !targets->nelts)            /* `-h', `--help', or `help' */
    print_generic_help(header, cmd_table, option_table, footer,
                       verbose,
                       pool, stdout);
  else                                       /* unknown option or cmd */
    SVN_ERR(svn_cmdline_fprintf(stderr, pool,
                                _("Type '%s help' for usage.\n"), pgm_name));

  return SVN_NO_ERROR;
}
