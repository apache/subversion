/*
 * main.c:  Subversion command line client.
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#include <string.h>

#include <apr_strings.h>
#include <apr_getopt.h>
#include <apr_tables.h>
#include <apr_general.h>

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "cl.h"


/*** Command dispatch. ***/

/* Map names to command routine, etc. 
 *
 * Canonical name entries must come immediately before their aliases.
 * For example, "add" must be the first of the add commands listed,
 * followed immediately by its aliases "ad" and "new".
 *
 * Alias entries should have null or 0 for every field except `name'
 * and `is_alias'.  The canonical entry will be used for everything
 * else.
 *
 * The entire list must be terminated with a entry of nulls.
 */
static const svn_cl__cmd_desc_t cmd_table[] = 
{
  { "add",        FALSE,  svn_cl__add_command,      svn_cl__add,
    "Add new files and directories to version control.\n"
    "usage: add [TARGETS]\n" },
  { "ad",         TRUE, 0, NULL, NULL },
  { "new",        TRUE, 0, NULL, NULL },

  { "checkout",   FALSE,  svn_cl__checkout_command, svn_cl__checkout,
    "Check out a working directory from a repository.\n"
    "usage: checkout REPOSPATH\n" },
  { "co",         TRUE, 0, NULL, NULL },

  { "commit",     FALSE,  svn_cl__commit_command,   svn_cl__commit,
    "Commit changes from your working copy to the repository.\n"
    "usage: commit [TARGETS]\n" },
  { "ci",         TRUE, 0, NULL, NULL },

  { "delete",     FALSE,  svn_cl__delete_command,   svn_cl__delete,
    "Remove files and directories from version control.\n"
    "usage: delete [TARGETS]\n" },
  { "del",        TRUE, 0, NULL, NULL },
  { "remove",     TRUE, 0, NULL, NULL },
  { "rm",         TRUE, 0, NULL, NULL },

  { "help",       FALSE,  svn_cl__help_command,   svn_cl__help,
    "Display this usage message.\n"
    "usage: help [SUBCOMMAND1 [SUBCOMMAND2] ...]\n" },
  { "?",          TRUE, 0, NULL, NULL },
  { "h",          TRUE, 0, NULL, NULL },
  /* We need to support "--help", "-?", and all that good stuff, of
     course.  But those options, since unknown, will result in the
     help message being printed out anyway, so there's no need to
     support them explicitly. */

  { "proplist",   FALSE,  svn_cl__proplist_command, svn_cl__proplist,
    "List all properties for given files and directories.\n"
    "usage: proplist [TARGETS]\n" },
  { "plist",      TRUE, 0, NULL, NULL },
  { "pl",         TRUE, 0, NULL, NULL },

  { "propget",    FALSE,  svn_cl__propget_command,  svn_cl__propget,
    "Get the value of property PROPNAME on files and directories.\n"
    "usage: propget PROPNAME [TARGETS]\n" },
  { "pget",       TRUE,   svn_cl__propget_command,  svn_cl__propget, NULL },
  { "pg",         TRUE,   svn_cl__propget_command,  svn_cl__propget, NULL },

  { "propset",    FALSE,  svn_cl__propset_command,  svn_cl__propset,
    "Set property PROPNAME to PROPVAL on the named files and directories.\n"
    "usage: propset PROPNAME [PROPVAL | --valfile VALFILE] "
    "[TARGET1 [TARGET2] ...]\n"},
  { "pset",       TRUE, 0, NULL, NULL },
  { "ps",         TRUE, 0, NULL, NULL },

  { "status",     FALSE,  svn_cl__status_command,   svn_cl__status,
    "Print the status of working copy files and directories.\n"
    "usage: status [TARGETS]\n" },
  { "stat",       TRUE, 0, NULL, NULL },
  { "st",         TRUE, 0, NULL, NULL },

  { "diff",     FALSE,  svn_cl__diff_command,   svn_cl__diff,
    "Display local file changes as contextual diffs.\n"
    "usage: diff [TARGETS]\n" },
  { "df",         TRUE, 0, NULL, NULL },

  { "update",     FALSE,  svn_cl__update_command,   svn_cl__update,
    "Bring changes from the repository into the working copy.\n"
    "usage: update [TARGETS]\n" },
  { "up",         TRUE, 0, NULL, NULL },
  { NULL,         FALSE,  0, NULL, NULL }
};


/* Return the entry in cmd_table whose name matches CMD_NAME, or null
 * if none.  CMD_NAME may be an alias, in which case the alias entry
 * will be returned (so caller may need to canonicalize result).
 */
static const svn_cl__cmd_desc_t *
get_cmd_table_entry (const char *cmd_name)
{
  int max = (sizeof (cmd_table) / sizeof (cmd_table[0])) - 1;
  int i;

  if (cmd_name == NULL)
    return NULL;

  for (i = 0; i < max; i++)
    if (strcmp (cmd_name, cmd_table[i].name) == 0)
      return cmd_table + i;

  /* Else command not found. */
  return NULL;
}


/* Return the canonical command table entry for CMD (which may be the
 * entry for CMD itself, or some other entry if CMD is an alias).
 * If CMD is not found, return null.
 */
static const svn_cl__cmd_desc_t *
get_canonical_command (const char *cmd)
{
  const svn_cl__cmd_desc_t *cmd_desc = get_cmd_table_entry (cmd);

  if (cmd_desc == NULL)
    return NULL;

  while (cmd_desc->is_alias)
    cmd_desc--;

  return cmd_desc;
}



/*** Help. ***/

/* Print the canonical command name for CMD, all its aliases,
   and if HELP is set, print the help string for the command too. */
static void
print_command_info (const svn_cl__cmd_desc_t *cmd_desc,
                    svn_boolean_t help, 
                    apr_pool_t *pool)
{
  const svn_cl__cmd_desc_t *this_cmd = get_canonical_command (cmd_desc->name);
  const svn_cl__cmd_desc_t *canonical_cmd = this_cmd;
  svn_boolean_t first_time;

  /* Print the canonical command name. */
  fputs (canonical_cmd->name, stdout);

  /* Print the list of aliases. */
  first_time = TRUE;
  for (this_cmd++; (this_cmd->name && this_cmd->is_alias); this_cmd++)
    {
      if (first_time) {
        printf (" (");
        first_time = FALSE;
      }
      else
        printf (", ");

      printf ("%s", this_cmd->name);
    }
  if (! first_time)
    printf (")");

  if (help)
    printf (": %s\n", canonical_cmd->help);
}


/* Print a generic (non-command-specific) usage message. */
static void
print_generic_help (apr_pool_t *pool)
{
  size_t max = (sizeof (cmd_table) / sizeof (cmd_table[0])) - 1;
  static const char usage[] =
    "usage: svn <subcommand> [options] [args]\n"
    "Type \"svn help <subcommand>\" for help on a specific subcommand.\n"
    "\n"
    "Most subcommands take file and/or directory arguments, recursing\n"
    "on the directories.  If no arguments are supplied to such a\n"
    "command, it will recurse on the current directory (inclusive) by\n" 
    "default.\n"
    "\n"
    "Available subcommands:\n";
  int i;

  printf ("%s", usage);
  for (i = 0; i < max; i++)
    if (! cmd_table[i].is_alias)
      {
        printf ("   ");
        print_command_info (cmd_table + i, FALSE, pool);
        printf ("\n");
      }
}


/* Print either generic help, or command-specific help for each
 * command in ARGV.  OPT_STATE is unused and may be null.
 * 
 * Unlike all the other command routines, ``help'' has its own
 * option processing.  Of course, it does not accept any options :-),
 * just command line args.
 */
svn_error_t *
svn_cl__help (svn_cl__opt_state_t *opt_state,
              apr_array_header_t *targets,
              apr_pool_t *pool)
{
  int i;

  if (targets->nelts)
    for (i = 0; i < targets->nelts; i++)
      {
        svn_string_t *this = (((svn_string_t **) (targets)->elts))[i];
        const svn_cl__cmd_desc_t *cmd = get_canonical_command (this->data);
        if (cmd)
          print_command_info (cmd, TRUE, pool);
        else
          fprintf (stderr, "\"%s\": unknown command.\n\n", this->data);
      }
  else
    print_generic_help (pool);

  return SVN_NO_ERROR;
}



/* Read the contents of FILENAME into *RESULT. */
static svn_error_t *
read_from_file (svn_string_t **result, const char *filename, apr_pool_t *pool)
{
  /* Right now, there are two places in Subversion where we'd need to
     read a value from a file: when setting a property, and for log
     messages (like "cvs commit -f<filename>").  Since properties are
     implemented using prop-sized memory chunks anyway, it's no loss
     to just read the file into a string.  Someday, we may streamify
     properties, at which point it would be desirable to let the
     callee read streamily from the file. */

  svn_string_t *res;
  apr_status_t apr_err;
  char buf[BUFSIZ];
  apr_size_t len;
  apr_file_t *f = NULL;

  res = svn_string_create ("", pool);

  apr_err = apr_open (&f, filename, APR_READ, APR_OS_DEFAULT, pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "read_from_file: failed to open '%s'",
                              filename);
      
  do {
    apr_err = apr_full_read (f, buf, sizeof(buf), &len);
    if (apr_err && !APR_STATUS_IS_EOF (apr_err))
      return svn_error_createf (apr_err, 0, NULL, pool,
                                "read_from_file: failed to read '%s'",
                                filename);
    
    svn_string_appendbytes (res, buf, len);
  } while (len != 0);

  apr_err = apr_close (f);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "read_from_file: failed to close '%s'",
                              filename);
  
  *result = res;
  return SVN_NO_ERROR;
}



/*** Main. ***/

int
main (int argc, const char * const *argv)
{
  apr_status_t apr_err;
  svn_error_t *err;
  apr_pool_t *pool;
  int opt_id;
  const char *optarg;
  apr_getopt_t *os;
  svn_cl__opt_state_t opt_state;
  const svn_cl__cmd_desc_t *subcommand;
  apr_array_header_t *targets;  /* file/dir args from the command line */

  static const apr_getopt_option_t options[] =
  {
    {"xml-file",      svn_cl__xml_file_opt, 1},
    {"target-dir",    svn_cl__target_dir_opt, 1}, /* README: --destination */
    {"ancestor-path", svn_cl__ancestor_path_opt, 1}, /* !doc'ed in README */
    {"revision",      'r', 1},
    {"valfile",       svn_cl__valfile_opt, 1},       /* !doc'ed in README */
    {"force",         svn_cl__force_opt, 0},
    {"help",          'h', 0},
    {0,               0, 0}
  };

  apr_initialize ();
  pool = svn_pool_create (NULL);
  memset (&opt_state, 0, sizeof (opt_state));
  opt_state.revision = SVN_INVALID_REVNUM;

  targets = apr_make_array (pool, 0, sizeof (svn_string_t *));

  /* No args?  Show usage. */
  if (argc <= 1)
    {
      svn_cl__help (NULL, targets, pool);
      apr_destroy_pool (pool);
      return EXIT_FAILURE;
    }

  /* Else, parse options. */
  apr_initopt (&os, pool, argc, argv);
  os->interleave = 1;
  while (1)
    {
      /* Parse the next option. */
      apr_err = apr_getopt_long (os, options, &opt_id, &optarg);
      if (APR_STATUS_IS_EOF (apr_err))
        break;
      else if (! APR_STATUS_IS_SUCCESS (apr_err))
        {
          svn_cl__help (NULL, targets, pool);
          apr_destroy_pool (pool);
          return EXIT_FAILURE;
        }

      switch (opt_id) {
      case 'r':
        opt_state.revision = (svn_revnum_t) atoi (optarg);
        break;
      case 'h':
      case '?':
        opt_state.help = TRUE;
        break;
      case svn_cl__xml_file_opt:
        opt_state.xml_file = svn_string_create (optarg, pool);
        break;
      case svn_cl__target_dir_opt:
        opt_state.target = svn_string_create (optarg, pool);
        break;
      case svn_cl__ancestor_path_opt:
        opt_state.ancestor_path = svn_string_create (optarg, pool);
        break;
      case svn_cl__valfile_opt:
        err = read_from_file (&(opt_state.value), optarg, pool);
        if (err)
          svn_handle_error (err, stdout, TRUE);
        break;
      case svn_cl__force_opt:
        opt_state.force = TRUE;
        break;
      default:
        break;  /* kff todo: ? */
      }
    }

  /* If the user asked for help, then the rest of the arguments are
     the names of subcommands to get help on (if any), or else they're
     just typos/mistakes.  Whatever the case, the subcommand to
     actually run is svn_cl__help(). */
  if (opt_state.help)
    subcommand = get_canonical_command ("help");
  else
    subcommand = NULL;

  /* If we're not running the `help' subcommand, then look for a
     subcommand in the first argument. */
  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          fprintf (stderr, "subcommand argument required\n");
          svn_cl__help (NULL, targets, pool);
          apr_destroy_pool (pool);
          return EXIT_FAILURE;
        }
      else
        {
          const char *first_arg = os->argv[os->ind++];
          subcommand = get_canonical_command (first_arg);
          if (subcommand == NULL)
            {
              fprintf (stderr, "unknown command: %s\n", first_arg);
              svn_cl__help (NULL, targets, pool);
              apr_destroy_pool (pool);
              return EXIT_FAILURE;
            }
        }
    }
  
  /* If made it this far, then we definitely have the subcommand. */

  /* Below, we greedily parse out some of the regular arguments,
   * because certain subcommands consume them.  For example, both
   * propget and propset need NAME, and propset needs VALUE in
   * addition.
   *
   * This code will probably move into subcommand-specific domains,
   * where it belongs.  As Greg Hudson points out:
   *
   * > A further, independent suggestion: instead of forcing main() to know
   * > about the syntax of particular commands (which will only get uglier as
   * > time goes on), make the command desc include a number of non-filename
   * > arguments--2 for propset, 1 for propget, 0 for most other commands.
   * > Bundle the non-option arguments into an array of strings and then
   * > iterate over the filename arguments like you do now.  When we need it,
   * > a -1 in that field can mean "just give me all of the arguments in a
   * > string array and I'll decide what to do with them."  Get rid of
   * > svn_cl__command_id so that you won't get back into the trap of main()
   * > having to know about specific commands.
   * 
   * kff todo: do above. :-)
   */
  if ((subcommand->cmd_code == svn_cl__propset_command)
      || (subcommand->cmd_code == svn_cl__propget_command))
    {
      if (os->ind >= os->argc)
        {
          fprintf (stderr, "property name argument required\n");
          svn_cl__help (NULL, targets, pool);
          apr_destroy_pool (pool);
          return EXIT_FAILURE;
        }
      else
        {
          const char *this_arg = os->argv[os->ind++];
          opt_state.name = svn_string_create (this_arg, pool);
        }
    }

  /* Grab value for propset, unless got it already via --valfile. */
  if ((subcommand->cmd_code == svn_cl__propset_command)
      && (opt_state.value == NULL))
    {
      if (os->ind >= os->argc)
        {
          fprintf (stderr, "property value argument required\n");
          svn_cl__help (NULL, targets, pool);
          apr_destroy_pool (pool);
          return EXIT_FAILURE;
        }
      else
        {
          const char *this_arg = os->argv[os->ind++];
          opt_state.value = svn_string_create (this_arg, pool);
        }
    }

  /* Do the regular arguments (target files and target dirs). */
  for (; os->ind < os->argc; os->ind++)
    {
      const char *this_arg = os->argv[os->ind];
      (*((svn_string_t **) apr_push_array (targets)))
        = svn_string_create (this_arg, pool);
    }

  /* Certain commands have an implied `.' as argument, if nothing else
     is specified. */
  if ((targets->nelts == 0) 
      && (   (subcommand->cmd_code == svn_cl__commit_command)
          || (subcommand->cmd_code == svn_cl__proplist_command)
          || (subcommand->cmd_code == svn_cl__propget_command)
          || (subcommand->cmd_code == svn_cl__status_command)
          || (subcommand->cmd_code == svn_cl__update_command)))
    {
      (*((svn_string_t **) apr_push_array (targets)))
        = svn_string_create (".", pool);
    }
  else
    {
      /* kff todo: need to remove redundancies from targets before
         passing it to the cmd_func. */
    }

  /* Run the subcommand. */
  err = (*subcommand->cmd_func) (&opt_state, targets, pool);
  if (err)
    svn_handle_error (err, stdout, 0);
  
  apr_destroy_pool (pool);
  return EXIT_SUCCESS;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
