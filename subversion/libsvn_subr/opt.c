/*
 * opt.c :  option and argument parsing for Subversion command lines
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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



#include <stdio.h>
#include <string.h>
#include <apr_pools.h>
#include <apr_general.h>
#include <apr_lib.h>

#include "svn_types.h"
#include "svn_opt.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_utf.h"
#include "svn_time.h"


/*** Code. ***/

const svn_opt_subcommand_desc_t *
svn_opt_get_canonical_subcommand (const svn_opt_subcommand_desc_t *table,
                                  const char *cmd_name)
{
  int i = 0;

  if (cmd_name == NULL)
    return NULL;

  while (table[i].name) {
    int j;
    if (strcmp (cmd_name, table[i].name) == 0)
      return table + i;
    for (j = 0; (j < SVN_OPT_MAX_ALIASES) && table[i].aliases[j]; j++)
      if (strcmp (cmd_name, table[i].aliases[j]) == 0)
        return table + i;

    i++;
  }

  /* If we get here, there was no matching subcommand name or alias. */
  return NULL;
}


const apr_getopt_option_t *
svn_opt_get_option_from_code (int code,
                              const apr_getopt_option_t *option_table)
{
  apr_size_t i;

  for (i = 0; option_table[i].optch; i++)
    if (option_table[i].optch == code)
      return &(option_table[i]);
  
  return NULL;
}


svn_boolean_t
svn_opt_subcommand_takes_option (const svn_opt_subcommand_desc_t *command,
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
static void
print_command_info (const svn_opt_subcommand_desc_t *cmd,
                    const apr_getopt_option_t *options_table,
                    svn_boolean_t help, 
                    apr_pool_t *pool,
                    FILE *stream)
{
  svn_boolean_t first_time;
  apr_size_t i;

  /* Print the canonical command name. */
  fputs (cmd->name, stream);

  /* Print the list of aliases. */
  first_time = TRUE;
  for (i = 0; i < SVN_OPT_MAX_ALIASES; i++) 
    {
      if (cmd->aliases[i] == NULL)
        break;

      if (first_time) {
        fprintf (stream, " (");
        first_time = FALSE;
      }
      else
        fprintf (stream, ", ");
      
      fprintf (stream, "%s", cmd->aliases[i]);
    }

  if (! first_time)
    fprintf (stream, ")");
  
  if (help)
    {
      const apr_getopt_option_t *option;
      svn_boolean_t have_options = FALSE;

      fprintf (stream, ": %s", cmd->help);

      /* Loop over all valid option codes attached to the subcommand */
      for (i = 0; i < SVN_OPT_MAX_OPTIONS; i++)
        {
          if (cmd->valid_options[i])
            {
              if (have_options == FALSE)
                {
                  fprintf (stream, "\nValid options:\n");
                  have_options = TRUE;
                }

              /* convert each option code into an option */
              option = 
                svn_opt_get_option_from_code (cmd->valid_options[i],
                                              options_table);

              /* print the option's docstring */
              if (option)
                {
                  const char *optstr;
                  svn_opt_format_option (&optstr, option, TRUE, pool);
                  fprintf (stream, "  %s\n", optstr);
                }
            }
        }

      if (have_options)
        fprintf (stream, "\n");
    }
}


void
svn_opt_print_generic_help (const char *header,
                            const svn_opt_subcommand_desc_t *cmd_table,
                            const apr_getopt_option_t *opt_table,
                            const char *footer,
                            apr_pool_t *pool, FILE *stream)
{
  int i = 0;

  fprintf (stream, "%s", header);
  while (cmd_table[i].name) 
    {
      fprintf (stream, "   ");
      print_command_info (cmd_table + i, opt_table, FALSE, pool, stream);
      fprintf (stream, "\n");
      i++;
    }

  fprintf (stream, "\n");
  fprintf (stream, "%s\n", footer);

}


void
svn_opt_format_option (const char **string,
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
    opts = apr_psprintf (pool, "-%c [--%s]", opt->optch, opt->name);
  else
    opts = apr_psprintf (pool, "--%s", opt->name);

  if (opt->has_arg)
    opts = apr_pstrcat (pool, opts, " arg", NULL);

  if (doc)
    opts = apr_psprintf (pool, "%-24s : %s", opts, opt->description);

  *string = opts;
}


void
svn_opt_subcommand_help (const char *subcommand,
                         const svn_opt_subcommand_desc_t *table,
                         const apr_getopt_option_t *options_table,
                         apr_pool_t *pool)
{
  const svn_opt_subcommand_desc_t *cmd =
    svn_opt_get_canonical_subcommand (table, subcommand);
    
  if (cmd)
    print_command_info (cmd, options_table, TRUE, pool, stdout);
  else
    fprintf (stderr, "\"%s\": unknown command.\n\n", subcommand);
}



/*** Parsing revisions and dates. ***/


/** Parsing "X:Y"-style arguments. **/

/* If WORD matches one of the special revision descriptors,
 * case-insensitively, set *REVISION accordingly:
 *
 *   - For "head", set REVISION->kind to svn_opt_revision_head.
 *
 *   - For "first", set REVISION->kind to svn_opt_revision_number
 *     and REVISION->value.number to 0.  ### iffy, but might be useful
 *     when mixed with dates ###
 *
 *   - For "prev", set REVISION->kind to svn_opt_revision_previous.
 *
 *   - For "base", set REVISION->kind to svn_opt_revision_base.
 *
 *   - For "committed" or "changed", set REVISION->kind to
 *     svn_opt_revision_committed.
 *
 * If match, return 1, else return 0 and don't touch REVISION.
 *
 * ### should we enforce a requirement that users write out these
 * words in full?  Actually, we probably will need to start enforcing
 * it as date parsing gets more sophisticated and the chances of a
 * first-letter overlap between a valid date and a valid word go up.
 */
static int
revision_from_word (svn_opt_revision_t *revision, const char *word)
{
  if (strcasecmp (word, "head") == 0)
    {
      revision->kind = svn_opt_revision_head;
    }
  else if (strcasecmp (word, "first") == 0)
    {
      revision->kind = svn_opt_revision_number;
      revision->value.number = 0;
    }
  else if (strcasecmp (word, "prev") == 0)
    {
      revision->kind = svn_opt_revision_previous;
    }
  else if (strcasecmp (word, "base") == 0)
    {
      revision->kind = svn_opt_revision_base;
    }
  else if ((strcasecmp (word, "committed") == 0)
           || (strcasecmp (word, "changed") == 0))
    {
      revision->kind = svn_opt_revision_committed;
    }
  else
    return 0;

  return 1;
}


/* Return non-zero if REV is all digits, else return 0. */
static int
valid_revision_number (const char *rev)
{
  while (*rev)
    {
      if (! apr_isdigit (*rev))
        return 0;

      /* Note: Keep this increment out of the apr_isdigit call, which
         is probably a macro, although you can supposedly #undef to
         get the function definition... But wait, I've said too much
         already.  Let us speak of this no more tonight, for there are
         strange doings in the Shire of late. */
      rev++;
    }

  return 1;
}


svn_boolean_t
svn_opt_parse_revision (svn_opt_revision_t *start_revision,
                        svn_opt_revision_t *end_revision,
                        const char *arg,
                        apr_pool_t *pool)
{
  char *left_rev, *right_rev;
  char *sep;

  /* Operate on a copy of the argument. */
  left_rev = apr_pstrdup (pool, arg);
  
  if ((sep = strchr (arg, ':')))
    {
      /* There can only be one colon. */
      if (strchr (sep + 1, ':'))
        return TRUE;

      *(left_rev + (sep - arg)) = '\0';
      right_rev = (left_rev + (sep - arg)) + 1;

      /* If there was a separator, both revisions must be present. */
      if ((! *left_rev) || (! *right_rev))
        return TRUE;
    }
  else  /* no separator */
    right_rev = NULL;

  /* Now left_rev holds N and right_rev holds M or null. */

  if (! revision_from_word (start_revision, left_rev))
    {
      if (! valid_revision_number (left_rev))
        return TRUE;

      start_revision->kind = svn_opt_revision_number;
      start_revision->value.number = SVN_STR_TO_REV (left_rev);
    }

  if (right_rev)
    {
      if (! revision_from_word (end_revision, right_rev))
        {
          if (! valid_revision_number (right_rev))
            return TRUE;

          end_revision->kind = svn_opt_revision_number;
          end_revision->value.number = SVN_STR_TO_REV (right_rev);
        }
    }

  return FALSE;
}


svn_boolean_t
svn_opt_parse_date (svn_opt_revision_t *start_revision,
                    svn_opt_revision_t *end_revision,
                    const char *arg, apr_pool_t *pool)
{
  char *left_date, *right_date;
  char *sep;

  /* Operate on a copy of the argument. */
  left_date = apr_pstrdup (pool, arg);

  if ((sep = strchr (arg, ':')))
    {
      /* ### todo: some standard date formats contain colons.
         Eventually, we should probably allow those, and use some
         other syntax for expressing ranges.  But for now, I'm just
         going to bail if see a non-separator colon, to get this up
         and running.  -kff */
      if (strchr (sep + 1, ':'))
        return TRUE;

      /* First, turn one string into two. */
      *(left_date + (sep - arg)) = '\0';
      right_date = (left_date + (sep - arg)) + 1;

      /* If there was a separator, both dates must be present. */
      if ((! *left_date) || (! *right_date))
        return TRUE;
    }
  else  /* no separator */
    right_date = NULL;
    
  /* Now left_date holds X and right_date holds Y or null. */

  if (! revision_from_word (start_revision, left_date))
    {
      start_revision->kind = svn_opt_revision_date;
      apr_time_ansi_put (&(start_revision->value.date),
                         svn_parse_date (left_date, NULL));
      /* ### todo: check if apr_time_t is valid? */
    }

  if (right_date)
    {
      if (! revision_from_word (end_revision, right_date))
        {
          end_revision->kind = svn_opt_revision_date;
          apr_time_ansi_put (&(end_revision->value.date),
                             svn_parse_date (right_date, NULL));
          /* ### todo: check if apr_time_t is valid? */
        }
    }

  return FALSE;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
