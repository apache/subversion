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
#include <assert.h>

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
#include "svn_io.h"
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
const svn_cl__cmd_desc_t svn_cl__cmd_table[] = 
{
  { "add",        FALSE, svn_cl__add, 0,
    "Add new files and directories to version control.\n"
    "usage: add [TARGETS]\n" },
  { "ad",         TRUE, NULL, 0, NULL },
  { "new",        TRUE, NULL, 0, NULL },

  { "checkout",   FALSE, svn_cl__checkout, 0,
    "Check out a working directory from a repository.\n"
    "usage: checkout REPOSPATH\n" },
  { "co",         TRUE, NULL, 0, NULL },

  { "commit",     FALSE, svn_cl__commit, 0,
    "Commit changes from your working copy to the repository.\n"
    "usage: commit [TARGETS]\n" },
  { "ci",         TRUE, NULL, 0, NULL },

  { "delete",     FALSE, svn_cl__delete, 0,
    "Remove files and directories from version control.\n"
    "usage: delete [TARGETS]\n" },
  { "del",        TRUE, NULL, 0, NULL },
  { "remove",     TRUE, NULL, 0, NULL },
  { "rm",         TRUE, NULL, 0, NULL },

  { "help",       FALSE, svn_cl__help, 0,
    "Display this usage message.\n"
    "usage: help [SUBCOMMAND1 [SUBCOMMAND2] ...]\n" },
  { "?",          TRUE, NULL, 0, NULL },
  { "h",          TRUE, NULL, 0, NULL },
  /* We need to support "--help", "-?", and all that good stuff, of
     course.  But those options, since unknown, will result in the
     help message being printed out anyway, so there's no need to
     support them explicitly. */

  { "proplist",   FALSE, svn_cl__proplist, 0,
    "List all properties for given files and directories.\n"
    "usage: proplist [TARGETS]\n" },
  { "plist",      TRUE, NULL, 0, NULL },
  { "pl",         TRUE, NULL, 0, NULL },

  { "propget",    FALSE, svn_cl__propget, 1,
    "Get the value of property PROPNAME on files and directories.\n"
    "usage: propget PROPNAME [TARGETS]\n" },
  { "pget",       TRUE, NULL, 1, NULL },
  { "pg",         TRUE, NULL, 1, NULL },

  { "propset",    FALSE, svn_cl__propset, 2, 
    "Set property PROPNAME to PROPVAL on the named files and directories.\n"
    "usage: propset PROPNAME [PROPVAL | --valfile VALFILE] "
    "[TARGET1 [TARGET2] ...]\n"},
  { "pset",       TRUE, NULL, 2, NULL },
  { "ps",         TRUE, NULL, 2, NULL },

  { "status",     FALSE, svn_cl__status, 0,
    "Print the status of working copy files and directories.\n"
    "usage: status [TARGETS]\n" },
  { "stat",       TRUE, NULL, 0, NULL },
  { "st",         TRUE, NULL, 0, NULL },

  { "diff",     FALSE, svn_cl__diff, 0,
    "Display local file changes as contextual diffs.\n"
    "usage: diff [TARGETS]\n" },
  { "df",         TRUE, NULL, 0, NULL },

  { "update",     FALSE, svn_cl__update, 0,
    "Bring changes from the repository into the working copy.\n"
    "usage: update [TARGETS]\n" },
  { "up",         TRUE, NULL, 0, NULL },
  { NULL,         FALSE, NULL, 0, NULL }
};


/* Some commands take an implicit "." string argument when invoked
 * with no arguments. Those commands make use of this function to
 * add "." to the target array if the user passes no args */
void
svn_cl__push_implicit_dot_target(apr_array_header_t *targets, apr_pool_t *pool)
{
  if (targets->nelts == 0) {
    (*((svn_string_t **) apr_array_push (targets)))
      = svn_string_create (".", pool);
  }
  assert(targets->nelts);
}

/* Return the entry in svn_cl__cmd_table whose name matches CMD_NAME,
 * or null if none.  CMD_NAME may be an alias, in which case the alias
 * entry will be returned (so caller may need to canonicalize result).  */
static const svn_cl__cmd_desc_t *
get_cmd_table_entry (const char *cmd_name)
{
  int max = (sizeof (svn_cl__cmd_table) / sizeof (svn_cl__cmd_table[0])) - 1;
  int i;

  if (cmd_name == NULL)
    return NULL;

  for (i = 0; i < max; i++)
    if (strcmp (cmd_name, svn_cl__cmd_table[i].name) == 0)
      return svn_cl__cmd_table + i;

  /* Else command not found. */
  return NULL;
}


/* Return the canonical command table entry for CMD (which may be the
 * entry for CMD itself, or some other entry if CMD is an alias).
 * If CMD is not found, return null.
 */
const svn_cl__cmd_desc_t *
svn_cl__get_canonical_command (const char *cmd)
{
  const svn_cl__cmd_desc_t *cmd_desc = get_cmd_table_entry (cmd);

  if (cmd_desc == NULL)
    return NULL;

  while (cmd_desc->is_alias)
    cmd_desc--;

  return cmd_desc;
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
  const svn_cl__cmd_desc_t *subcommand = NULL;
  apr_array_header_t *targets;  /* file/dir args from the command line */

  static const apr_getopt_option_t options[] =
  {
    {"ancestor-path", svn_cl__ancestor_path_opt, 1}, /* !doc'ed in README */
    {"force",         svn_cl__force_opt, 0},
    {"help",          'h', 0},
    {"message",       'm', 1},
    {"revision",      'r', 1},
    {"target-dir",    svn_cl__target_dir_opt, 1}, /* README: --destination */
    {"valfile",       svn_cl__valfile_opt, 1},       /* !doc'ed in README */
    {"xml-file",      svn_cl__xml_file_opt, 1},
    {0,               0, 0}
  };

  apr_initialize ();
  pool = svn_pool_create (NULL);
  memset (&opt_state, 0, sizeof (opt_state));
  opt_state.revision = SVN_INVALID_REVNUM;

  targets = apr_array_make (pool, 0, sizeof (svn_string_t *));

  /* No args?  Show usage. */
  if (argc <= 1)
    {
      svn_cl__help (NULL, targets, pool);
      apr_pool_destroy (pool);
      return EXIT_FAILURE;
    }

  /* Else, parse options. */
  apr_getopt_init (&os, pool, argc, argv);
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
          apr_pool_destroy (pool);
          return EXIT_FAILURE;
        }

      switch (opt_id) {
      case 'm':
        opt_state.message = svn_string_create (optarg, pool);
        break;
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
        /* TODO This needs a little thought before being implemented
           properly. I'm putting the non-file args into an array now,
           so how to best get this to the commands without special
           casing myself into a corner? For example, propset takes 2
           non-file args: name and value. name is (effectively, since
           I'm using an apr_array) args[0] and value is
           args[1]. Suggestions? Greg[SH]? I'll get to it this week
           -Fitz 14-Mar-2001*/
#if 0
        err = svn_string_from_file (&(opt_state.value), optarg, pool);
        if (err)
          svn_handle_error (err, stdout, TRUE);
#endif
        fprintf (stderr, "TODO fixme\n");
        break;
      case svn_cl__force_opt:
        opt_state.force = TRUE;
        break;
      default:
        /* Hmmm. Perhaps this would be a good place to squirrel away
           opts that commands like svn diff might need. Hmmm indeed. */
        break;  
      }
    }

  /* If the user asked for help, then the rest of the arguments are
     the names of subcommands to get help on (if any), or else they're
     just typos/mistakes.  Whatever the case, the subcommand to
     actually run is svn_cl__help(). */
  if (opt_state.help)
    subcommand = svn_cl__get_canonical_command ("help");

  /* If we're not running the `help' subcommand, then look for a
     subcommand in the first argument. */
  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          fprintf (stderr, "subcommand argument required\n");
          svn_cl__help (NULL, targets, pool);
          apr_pool_destroy (pool);
          return EXIT_FAILURE;
        }
      else
        {
          const char *first_arg = os->argv[os->ind++];
          subcommand = svn_cl__get_canonical_command (first_arg);
          if (subcommand == NULL)
            {
              fprintf (stderr, "unknown command: %s\n", first_arg);
              svn_cl__help (NULL, targets, pool);
              apr_pool_destroy (pool);
              return EXIT_FAILURE;
            }
        }
    }
  
  /* If we made it this far, then we definitely have the subcommand. */

  /* Below, we parse out some of the regular arguments, because
   * certain subcommands consume them.  For example, both propget and
   * propset need NAME, and propset needs VALUE in addition. */
  opt_state.args = apr_array_make (pool, 0, sizeof (svn_string_t *));

  if (subcommand->num_args > 0) {
    const char *this_arg;
    int i;
    /* loop for num_args and add each arg to the args array */
    for (i = 0; i < subcommand->num_args; i++) {
      if (os->ind >= os->argc) {
        const char *plural = "s"; 
        fprintf (stderr, "ERROR: The %s command requires %i argument%s\n",
                 subcommand->name, subcommand->num_args, 
                 (subcommand->num_args == 1) ? "" : plural);
        fprintf (stderr, "Help for %s:\n%s", subcommand->name, subcommand->help);
        apr_pool_destroy (pool);
        return EXIT_FAILURE;
      }
      this_arg = os->argv[os->ind++];
      (*((svn_string_t **) apr_array_push (opt_state.args)))
        = svn_string_create (this_arg, pool);

    }
  }
  /* greedily suck up all args if num_args is a negative number. */
  else if (subcommand->num_args == -1) {
    while (os->ind < os->argc) {
      const char *this_arg = os->argv[os->ind++];
      (*((svn_string_t **) apr_array_push (opt_state.args)))
        = svn_string_create (this_arg, pool);
    }
  }

  /* Do the regular arguments (target files and target dirs). */
  for (; os->ind < os->argc; os->ind++)
    {
      const char *this_arg = os->argv[os->ind];
      (*((svn_string_t **) apr_array_push (targets)))
        = svn_string_create (this_arg, pool);
    }
    
  /* kff todo: need to remove redundancies from targets before
     passing it to the cmd_func. */

  /* Run the subcommand. */
  err = (*subcommand->cmd_func) (&opt_state, targets, pool);
  if (err)
    svn_handle_error (err, stdout, 0);
  
  apr_pool_destroy (pool);
  return EXIT_SUCCESS;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
