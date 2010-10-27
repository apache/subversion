/*
 * opt.c :  option and argument parsing for Subversion command lines
 *
 * ====================================================================
 * Copyright (c) 2000-2009 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */



#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <stdio.h>
#include <assert.h>
#include <apr_pools.h>
#include <apr_general.h>
#include <apr_lib.h>
#include <apr_file_info.h>

#include "svn_cmdline.h"
#include "svn_version.h"
#include "svn_types.h"
#include "svn_opt.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_time.h"
#include "svn_props.h"

#include "private/svn_opt_private.h"

#include "opt.h"
#include "svn_private_config.h"


/*** Code. ***/

const svn_opt_subcommand_desc2_t *
svn_opt_get_canonical_subcommand2(const svn_opt_subcommand_desc2_t *table,
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
svn_opt_get_option_from_code2(int code,
                              const apr_getopt_option_t *option_table,
                              const svn_opt_subcommand_desc2_t *command,
                              apr_pool_t *pool)
{
  apr_size_t i;

  for (i = 0; option_table[i].optch; i++)
    if (option_table[i].optch == code)
      {
        int j;
        if (command)
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
        return &(option_table[i]);
      }

  return NULL;
}


const apr_getopt_option_t *
svn_opt_get_option_from_code(int code,
                             const apr_getopt_option_t *option_table)
{
  apr_size_t i;

  for (i = 0; option_table[i].optch; i++)
    if (option_table[i].optch == code)
      return &(option_table[i]);

  return NULL;
}


svn_boolean_t
svn_opt_subcommand_takes_option3(const svn_opt_subcommand_desc2_t *command,
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

svn_boolean_t
svn_opt_subcommand_takes_option2(const svn_opt_subcommand_desc2_t *command,
                                 int option_code)
{
  return svn_opt_subcommand_takes_option3(command,
                                          option_code,
                                          NULL);
}


svn_boolean_t
svn_opt_subcommand_takes_option(const svn_opt_subcommand_desc_t *command,
                                int option_code)
{
  apr_size_t i;

  for (i = 0; i < SVN_OPT_MAX_OPTIONS; i++)
    if (command->valid_options[i] == option_code)
      return TRUE;

  return FALSE;
}


/* Print the canonical command name for CMD, and all its aliases, to
   STREAM.  If HELP is set, print CMD's help string too, in which case
   obtain option usage from OPTIONS_TABLE. */
static svn_error_t *
print_command_info2(const svn_opt_subcommand_desc2_t *cmd,
                    const apr_getopt_option_t *options_table,
                    const int *global_options,
                    svn_boolean_t help,
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
      svn_boolean_t have_options = FALSE;

      SVN_ERR(svn_cmdline_fprintf(stream, pool, ": %s", _(cmd->help)));

      /* Loop over all valid option codes attached to the subcommand */
      for (i = 0; i < SVN_OPT_MAX_OPTIONS; i++)
        {
          if (cmd->valid_options[i])
            {
              if (have_options == FALSE)
                {
                  SVN_ERR(svn_cmdline_fputs(_("\nValid options:\n"),
                                            stream, pool));
                  have_options = TRUE;
                }

              /* convert each option code into an option */
              option =
                svn_opt_get_option_from_code2(cmd->valid_options[i],
                                              options_table,
                                              cmd, pool);

              /* print the option's docstring */
              if (option && option->description)
                {
                  const char *optstr;
                  svn_opt_format_option(&optstr, option, TRUE, pool);
                  SVN_ERR(svn_cmdline_fprintf(stream, pool, "  %s\n",
                                              optstr));
                }
            }
        }
      /* And global options too */
      if (global_options && *global_options)
        {
          SVN_ERR(svn_cmdline_fputs(_("\nGlobal options:\n"),
                                    stream, pool));
          have_options = TRUE;

          for (i = 0; global_options[i]; i++)
            {

              /* convert each option code into an option */
              option =
                svn_opt_get_option_from_code2(global_options[i],
                                              options_table,
                                              cmd, pool);

              /* print the option's docstring */
              if (option && option->description)
                {
                  const char *optstr;
                  svn_opt_format_option(&optstr, option, TRUE, pool);
                  SVN_ERR(svn_cmdline_fprintf(stream, pool, "  %s\n",
                                              optstr));
                }
            }
        }

      if (have_options)
        SVN_ERR(svn_cmdline_fprintf(stream, pool, "\n"));
    }

  return SVN_NO_ERROR;
}

void
svn_opt_print_generic_help2(const char *header,
                            const svn_opt_subcommand_desc2_t *cmd_table,
                            const apr_getopt_option_t *opt_table,
                            const char *footer,
                            apr_pool_t *pool, FILE *stream)
{
  int i = 0;
  svn_error_t *err;

  if (header)
    if ((err = svn_cmdline_fputs(header, stream, pool)))
      goto print_error;

  while (cmd_table[i].name)
    {
      if ((err = svn_cmdline_fputs("   ", stream, pool))
          || (err = print_command_info2(cmd_table + i, opt_table,
                                        NULL, FALSE,
                                        pool, stream))
          || (err = svn_cmdline_fputs("\n", stream, pool)))
        goto print_error;
      i++;
    }

  if ((err = svn_cmdline_fputs("\n", stream, pool)))
    goto print_error;

  if (footer)
    if ((err = svn_cmdline_fputs(footer, stream, pool)))
      goto print_error;

  return;

 print_error:
  svn_handle_error2(err, stderr, FALSE, "svn: ");
  svn_error_clear(err);
}

void
svn_opt_format_option(const char **string,
                      const apr_getopt_option_t *opt,
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
  else
    opts = apr_psprintf(pool, "--%s", opt->name);

  if (opt->has_arg)
    opts = apr_pstrcat(pool, opts, _(" ARG"), NULL);

  if (doc)
    opts = apr_psprintf(pool, "%-24s : %s", opts, _(opt->description));

  *string = opts;
}


void
svn_opt_subcommand_help3(const char *subcommand,
                         const svn_opt_subcommand_desc2_t *table,
                         const apr_getopt_option_t *options_table,
                         const int *global_options,
                         apr_pool_t *pool)
{
  const svn_opt_subcommand_desc2_t *cmd =
    svn_opt_get_canonical_subcommand2(table, subcommand);
  svn_error_t *err;

  if (cmd)
    err = print_command_info2(cmd, options_table, global_options,
                              TRUE, pool, stdout);
  else
    err = svn_cmdline_fprintf(stderr, pool,
                              _("\"%s\": unknown command.\n\n"), subcommand);

  if (err) {
    svn_handle_error2(err, stderr, FALSE, "svn: ");
    svn_error_clear(err);
  }
}



/*** Parsing revision and date options. ***/


/** Parsing "X:Y"-style arguments. **/

/* If WORD matches one of the special revision descriptors,
 * case-insensitively, set *REVISION accordingly:
 *
 *   - For "head", set REVISION->kind to svn_opt_revision_head.
 *
 *   - For "prev", set REVISION->kind to svn_opt_revision_previous.
 *
 *   - For "base", set REVISION->kind to svn_opt_revision_base.
 *
 *   - For "committed", set REVISION->kind to svn_opt_revision_committed.
 *
 * If match, return 0, else return -1 and don't touch REVISION.
 */
static int
revision_from_word(svn_opt_revision_t *revision, const char *word)
{
  if (svn_cstring_casecmp(word, "head") == 0)
    {
      revision->kind = svn_opt_revision_head;
    }
  else if (svn_cstring_casecmp(word, "prev") == 0)
    {
      revision->kind = svn_opt_revision_previous;
    }
  else if (svn_cstring_casecmp(word, "base") == 0)
    {
      revision->kind = svn_opt_revision_base;
    }
  else if (svn_cstring_casecmp(word, "committed") == 0)
    {
      revision->kind = svn_opt_revision_committed;
    }
  else
    return -1;

  return 0;
}


/* Parse one revision specification.  Return pointer to character
   after revision, or NULL if the revision is invalid.  Modifies
   str, so make sure to pass a copy of anything precious.  Uses
   POOL for temporary allocation. */
static char *parse_one_rev(svn_opt_revision_t *revision, char *str,
                           apr_pool_t *pool)
{
  char *end, save;

  /* Allow any number of 'r's to prefix a revision number, because
     that way if a script pastes svn output into another svn command
     (like "svn log -r${REV_COPIED_FROM_OUTPUT}"), it'll Just Work,
     even when compounded.

     As it happens, none of our special revision words begins with
     "r".  If any ever do, then this code will have to get smarter.

     Incidentally, this allows "r{DATE}".  We could avoid that with
     some trivial code rearrangement, but it's not clear what would
     be gained by doing so. */
  while (*str == 'r')
    str++;

  if (*str == '{')
    {
      svn_boolean_t matched;
      apr_time_t tm;
      svn_error_t *err;

      /* Brackets denote a date. */
      str++;
      end = strchr(str, '}');
      if (!end)
        return NULL;
      *end = '\0';
      err = svn_parse_date(&matched, &tm, str, apr_time_now(), pool);
      if (err)
        {
          svn_error_clear(err);
          return NULL;
        }
      if (!matched)
        return NULL;
      revision->kind = svn_opt_revision_date;
      revision->value.date = tm;
      return end + 1;
    }
  else if (apr_isdigit(*str))
    {
      /* It's a number. */
      end = str + 1;
      while (apr_isdigit(*end))
        end++;
      save = *end;
      *end = '\0';
      revision->kind = svn_opt_revision_number;
      revision->value.number = SVN_STR_TO_REV(str);
      *end = save;
      return end;
    }
  else if (apr_isalpha(*str))
    {
      end = str + 1;
      while (apr_isalpha(*end))
        end++;
      save = *end;
      *end = '\0';
      if (revision_from_word(revision, str) != 0)
        return NULL;
      *end = save;
      return end;
    }
  else
    return NULL;
}


int
svn_opt_parse_revision(svn_opt_revision_t *start_revision,
                       svn_opt_revision_t *end_revision,
                       const char *arg,
                       apr_pool_t *pool)
{
  char *left_rev, *right_rev, *end;

  /* Operate on a copy of the argument. */
  left_rev = apr_pstrdup(pool, arg);

  right_rev = parse_one_rev(start_revision, left_rev, pool);
  if (right_rev && *right_rev == ':')
    {
      right_rev++;
      end = parse_one_rev(end_revision, right_rev, pool);
      if (!end || *end != '\0')
        return -1;
    }
  else if (!right_rev || *right_rev != '\0')
    return -1;

  return 0;
}


int
svn_opt_parse_revision_to_range(apr_array_header_t *opt_ranges,
                                const char *arg,
                                apr_pool_t *pool)
{
  svn_opt_revision_range_t *range = apr_palloc(pool, sizeof(*range));

  range->start.kind = svn_opt_revision_unspecified;
  range->end.kind = svn_opt_revision_unspecified;

  if (svn_opt_parse_revision(&(range->start), &(range->end),
                             arg, pool) == -1)
    return -1;

  APR_ARRAY_PUSH(opt_ranges, svn_opt_revision_range_t *) = range;
  return 0;
}

svn_error_t *
svn_opt_resolve_revisions(svn_opt_revision_t *peg_rev,
                          svn_opt_revision_t *op_rev,
                          svn_boolean_t is_url,
                          svn_boolean_t notice_local_mods,
                          apr_pool_t *pool)
{
  if (peg_rev->kind == svn_opt_revision_unspecified)
    {
      if (is_url)
        {
          peg_rev->kind = svn_opt_revision_head;
        }
      else
        {
          if (notice_local_mods)
            peg_rev->kind = svn_opt_revision_working;
          else
            peg_rev->kind = svn_opt_revision_base;
        }
    }

  if (op_rev->kind == svn_opt_revision_unspecified)
    *op_rev = *peg_rev;

  return SVN_NO_ERROR;
}


/*** Parsing arguments. ***/
#define DEFAULT_ARRAY_SIZE 5


/* Copy STR into POOL and push the copy onto ARRAY. */
static void
array_push_str(apr_array_header_t *array,
               const char *str,
               apr_pool_t *pool)
{
  /* ### Not sure if this function is still necessary.  It used to
     convert str to svn_stringbuf_t * and push it, but now it just
     dups str in pool and pushes the copy.  So its only effect is
     transfer str's lifetime to pool.  Is that something callers are
     depending on? */

  APR_ARRAY_PUSH(array, const char *) = apr_pstrdup(pool, str);
}


void
svn_opt_push_implicit_dot_target(apr_array_header_t *targets,
                                 apr_pool_t *pool)
{
  if (targets->nelts == 0)
    array_push_str(targets, "", pool); /* Ha! "", not ".", is the canonical */
  assert(targets->nelts);
}


svn_error_t *
svn_opt_parse_num_args(apr_array_header_t **args_p,
                       apr_getopt_t *os,
                       int num_args,
                       apr_pool_t *pool)
{
  int i;
  apr_array_header_t *args
    = apr_array_make(pool, DEFAULT_ARRAY_SIZE, sizeof(const char *));

  /* loop for num_args and add each arg to the args array */
  for (i = 0; i < num_args; i++)
    {
      if (os->ind >= os->argc)
        {
          return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL);
        }
      array_push_str(args, os->argv[os->ind++], pool);
    }

  *args_p = args;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_opt_parse_all_args(apr_array_header_t **args_p,
                       apr_getopt_t *os,
                       apr_pool_t *pool)
{
  apr_array_header_t *args
    = apr_array_make(pool, DEFAULT_ARRAY_SIZE, sizeof(const char *));

  if (os->ind > os->argc)
    {
      return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL);
    }
  while (os->ind < os->argc)
    {
      array_push_str(args, os->argv[os->ind++], pool);
    }

  *args_p = args;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_opt_parse_path(svn_opt_revision_t *rev,
                   const char **truepath,
                   const char *path /* UTF-8! */,
                   apr_pool_t *pool)
{
  const char *peg_rev;

  SVN_ERR(svn_opt__split_arg_at_peg_revision(truepath, &peg_rev, path, pool));

  /* Parse the peg revision, if one was found */
  if (strlen(peg_rev))
    {
      int ret;
      svn_opt_revision_t start_revision, end_revision;

      end_revision.kind = svn_opt_revision_unspecified;

      if (peg_rev[1] == '\0')  /* looking at empty peg revision */
        {
          ret = 0;
          start_revision.kind = svn_opt_revision_unspecified;
        }
      else  /* looking at non-empty peg revision */
        {
          const char *rev_str = &peg_rev[1];

          /* URLs get treated differently from wc paths. */
          if (svn_path_is_url(path))
            {
              /* URLs are URI-encoded, so we look for dates with
                 URI-encoded delimeters.  */
              int rev_len = strlen(rev_str);
              if (rev_len > 6
                  && rev_str[0] == '%'
                  && rev_str[1] == '7'
                  && (rev_str[2] == 'B'
                      || rev_str[2] == 'b')
                  && rev_str[rev_len-3] == '%'
                  && rev_str[rev_len-2] == '7'
                  && (rev_str[rev_len-1] == 'D'
                      || rev_str[rev_len-1] == 'd'))
                {
                  rev_str = svn_path_uri_decode(rev_str, pool);
                }
            }
          ret = svn_opt_parse_revision(&start_revision,
                                       &end_revision,
                                       rev_str, pool);
        }

      if (ret || end_revision.kind != svn_opt_revision_unspecified)
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("Syntax error parsing revision '%s'"),
                                 &peg_rev[1]);

      rev->kind = start_revision.kind;
      rev->value = start_revision.value;
    }
  else
    {
      /* Didn't find a peg revision. */
      rev->kind = svn_opt_revision_unspecified;
    }

  return SVN_NO_ERROR;
}


/* Note: This is substantially copied into svn_client_args_to_target_array() in
 * order to move to libsvn_client while maintaining backward compatibility. */
svn_error_t *
svn_opt__args_to_target_array(apr_array_header_t **targets_p,
                              apr_getopt_t *os,
                              apr_array_header_t *known_targets,
                              apr_pool_t *pool)
{
  int i;
  svn_error_t *err = SVN_NO_ERROR;
  apr_array_header_t *input_targets =
    apr_array_make(pool, DEFAULT_ARRAY_SIZE, sizeof(const char *));
  apr_array_header_t *output_targets =
    apr_array_make(pool, DEFAULT_ARRAY_SIZE, sizeof(const char *));

  /* Step 1:  create a master array of targets that are in UTF-8
     encoding, and come from concatenating the targets left by apr_getopt,
     plus any extra targets (e.g., from the --targets switch.) */

  for (; os->ind < os->argc; os->ind++)
    {
      /* The apr_getopt targets are still in native encoding. */
      const char *raw_target = os->argv[os->ind];
      SVN_ERR(svn_utf_cstring_to_utf8
              ((const char **) apr_array_push(input_targets),
               raw_target, pool));
    }

  if (known_targets)
    {
      for (i = 0; i < known_targets->nelts; i++)
        {
          /* The --targets array have already been converted to UTF-8,
             because we needed to split up the list with svn_cstring_split. */
          const char *utf8_target = APR_ARRAY_IDX(known_targets,
                                                  i, const char *);
          APR_ARRAY_PUSH(input_targets, const char *) = utf8_target;
        }
    }

  /* Step 2:  process each target.  */

  for (i = 0; i < input_targets->nelts; i++)
    {
      const char *utf8_target = APR_ARRAY_IDX(input_targets, i, const char *);
      const char *true_target;
      const char *target;      /* after all processing is finished */
      const char *peg_rev;

      /*
       * This is needed so that the target can be properly canonicalized,
       * otherwise the canonicalization does not treat a ".@BASE" as a "."
       * with a BASE peg revision, and it is not canonicalized to "@BASE".
       * If any peg revision exists, it is appended to the final
       * canonicalized path or URL.  Do not use svn_opt_parse_path()
       * because the resulting peg revision is a structure that would have
       * to be converted back into a string.  Converting from a string date
       * to the apr_time_t field in the svn_opt_revision_value_t and back to
       * a string would not necessarily preserve the exact bytes of the
       * input date, so its easier just to keep it in string form.
       */
      SVN_ERR(svn_opt__split_arg_at_peg_revision(&true_target, &peg_rev,
                                                 utf8_target, pool));

      /* URLs and wc-paths get treated differently. */
      if (svn_path_is_url(true_target))
        {
          SVN_ERR(svn_opt__arg_canonicalize_url(&true_target, true_target,
                                                 pool));
        }
      else  /* not a url, so treat as a path */
        {
          const char *base_name;

          SVN_ERR(svn_opt__arg_canonicalize_path(&true_target, true_target,
                                                 pool));

          /* If the target has the same name as a Subversion
             working copy administrative dir, skip it. */
          base_name = svn_path_basename(true_target, pool);

          /* FIXME:
             The canonical list of administrative directory names is
             maintained in libsvn_wc/adm_files.c:svn_wc_set_adm_dir().
             That list can't be used here, because that use would
             create a circular dependency between libsvn_wc and
             libsvn_subr.  Make sure changes to the lists are always
             synchronized! */
          if (0 == strcmp(base_name, ".svn")
              || 0 == strcmp(base_name, "_svn"))
            {
              err = svn_error_createf(SVN_ERR_RESERVED_FILENAME_SPECIFIED,
                                      err, _("'%s' ends in a reserved name"),
                                      utf8_target);
              continue;
            }
        }

      target = apr_pstrcat(pool, true_target, peg_rev, NULL);

      APR_ARRAY_PUSH(output_targets, const char *) = target;
    }


  /* kff todo: need to remove redundancies from targets before
     passing it to the cmd_func. */

  *targets_p = output_targets;

  return err;
}

svn_error_t *
svn_opt_parse_revprop(apr_hash_t **revprop_table_p, const char *revprop_spec,
                      apr_pool_t *pool)
{
  const char *sep, *propname;
  svn_string_t *propval;

  if (! *revprop_spec)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Revision property pair is empty"));

  if (! *revprop_table_p)
    *revprop_table_p = apr_hash_make(pool);

  sep = strchr(revprop_spec, '=');
  if (sep)
    {
      propname = apr_pstrndup(pool, revprop_spec, sep - revprop_spec);
      SVN_ERR(svn_utf_cstring_to_utf8(&propname, propname, pool));
      propval = svn_string_create(sep + 1, pool);
    }
  else
    {
      SVN_ERR(svn_utf_cstring_to_utf8(&propname, revprop_spec, pool));
      propval = svn_string_create("", pool);
    }

  if (!svn_prop_name_is_valid(propname))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("'%s' is not a valid Subversion property name"),
                             propname);

  apr_hash_set(*revprop_table_p, propname, APR_HASH_KEY_STRING, propval);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_opt__split_arg_at_peg_revision(const char **true_target,
                                   const char **peg_revision,
                                   const char *utf8_target,
                                   apr_pool_t *pool)
{
  const char *peg_start = NULL; /* pointer to the peg revision, if any */
  int j;

  for (j = (strlen(utf8_target) - 1); j >= 0; --j)
    {
      /* If we hit a path separator, stop looking.  This is OK
          only because our revision specifiers can't contain '/'. */
      if (utf8_target[j] == '/')
        break;

      if (utf8_target[j] == '@')
        {
          peg_start = &utf8_target[j];
          break;
        }
    }

  if (peg_start)
    {
      /* Error out if target is the empty string. */
      if (j == 0)
        return svn_error_createf(SVN_ERR_BAD_FILENAME, NULL,
                                 _("'%s' is just a peg revision. "
                                   "Maybe try '%s@' instead?"),
                                 utf8_target, utf8_target);

      *true_target = apr_pstrmemdup(pool, utf8_target, j);
      if (peg_revision)
        *peg_revision = apr_pstrdup(pool, peg_start);
    }
  else
    {
      *true_target = utf8_target;
      if (peg_revision)
        *peg_revision = "";
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_opt__arg_canonicalize_url(const char **url_out, const char *url_in,
                              apr_pool_t *pool)
{
  const char *target;

  /* Convert to URI. */
  target = svn_path_uri_from_iri(url_in, pool);
  /* Auto-escape some ASCII characters. */
  target = svn_path_uri_autoescape(target, pool);

  /* Verify that no backpaths are present in the URL. */
  if (svn_path_is_backpath_present(target))
    return svn_error_createf(SVN_ERR_BAD_URL, 0,
                             _("URL '%s' contains a '..' element"),
                             target);

  /* strip any trailing '/' and collapse other redundant elements */
  target = svn_path_canonicalize(target, pool);

  *url_out = target;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_opt__arg_canonicalize_path(const char **path_out, const char *path_in,
                               apr_pool_t *pool)
{
  const char *apr_target;
  char *truenamed_target; /* APR-encoded */
  apr_status_t apr_err;

  /* canonicalize case, and change all separators to '/'. */
  SVN_ERR(svn_path_cstring_from_utf8(&apr_target, path_in, pool));
  apr_err = apr_filepath_merge(&truenamed_target, "", apr_target,
                               APR_FILEPATH_TRUENAME, pool);

  if (!apr_err)
    /* We have a canonicalized APR-encoded target now. */
    apr_target = truenamed_target;
  else if (APR_STATUS_IS_ENOENT(apr_err))
    /* It's okay for the file to not exist, that just means we
       have to accept the case given to the client. We'll use
       the original APR-encoded target. */
    ;
  else
    return svn_error_createf(apr_err, NULL,
                             _("Error resolving case of '%s'"),
                             svn_path_local_style(path_in, pool));

  /* convert back to UTF-8. */
  SVN_ERR(svn_path_cstring_to_utf8(path_out, apr_target, pool));
  *path_out = svn_path_canonicalize(*path_out, pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_opt__print_version_info(const char *pgm_name,
                            const char *footer,
                            svn_boolean_t quiet,
                            apr_pool_t *pool)
{
  if (quiet)
    return svn_cmdline_printf(pool, "%s\n", SVN_VER_NUMBER);

  SVN_ERR(svn_cmdline_printf(pool, _("%s, version %s\n"
                                     "   compiled %s, %s\n\n"), pgm_name,
                             SVN_VERSION, __DATE__, __TIME__));
  SVN_ERR(svn_cmdline_fputs(_("Copyright (C) 2000-2009 CollabNet.\n"
                              "Subversion is open source software, see"
                              " http://subversion.apache.org/\n"
                              "This product includes software developed by "
                              "CollabNet (http://www.Collab.Net/).\n\n"),
                            stdout, pool));

  if (footer)
    {
      SVN_ERR(svn_cmdline_printf(pool, "%s\n", footer));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_opt_print_help3(apr_getopt_t *os,
                    const char *pgm_name,
                    svn_boolean_t print_version,
                    svn_boolean_t quiet,
                    const char *version_footer,
                    const char *header,
                    const svn_opt_subcommand_desc2_t *cmd_table,
                    const apr_getopt_option_t *option_table,
                    const int *global_options,
                    const char *footer,
                    apr_pool_t *pool)
{
  apr_array_header_t *targets = NULL;
  int i;

  if (os)
    SVN_ERR(svn_opt_parse_all_args(&targets, os, pool));

  if (os && targets->nelts)  /* help on subcommand(s) requested */
    for (i = 0; i < targets->nelts; i++)
      {
        svn_opt_subcommand_help3(APR_ARRAY_IDX(targets, i, const char *),
                                 cmd_table, option_table,
                                 global_options, pool);
      }
  else if (print_version)   /* just --version */
    SVN_ERR(svn_opt__print_version_info(pgm_name, version_footer, quiet,
                                        pool));
  else if (os && !targets->nelts)            /* `-h', `--help', or `help' */
    svn_opt_print_generic_help2(header,
                                cmd_table,
                                option_table,
                                footer,
                                pool,
                                stdout);
  else                                       /* unknown option or cmd */
    SVN_ERR(svn_cmdline_fprintf(stderr, pool,
                                _("Type '%s help' for usage.\n"), pgm_name));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_opt__eat_peg_revisions(apr_array_header_t **true_targets_p,
                           apr_array_header_t *targets,
                           apr_pool_t *pool)
{
  unsigned int i;
  apr_array_header_t *true_targets;

  true_targets = apr_array_make(pool, DEFAULT_ARRAY_SIZE, sizeof(const char *));

  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      const char *true_target;

      SVN_ERR(svn_opt__split_arg_at_peg_revision(&true_target, NULL,
                                                 target, pool));
      APR_ARRAY_PUSH(true_targets, const char *) = true_target;
    }

  SVN_ERR_ASSERT(true_targets_p);
  *true_targets_p = true_targets;

  return SVN_NO_ERROR;
}
