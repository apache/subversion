/*
 * help-cmd.c -- Provide help
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "cl.h"

#include "svn_private_config.h"  /* for SVN_VERSION */


/*** Code. ***/

/* Print the canonical command name for CMD, all its aliases,
   and if HELP is set, print the help string for the command too. */
static void
print_command_info (const svn_cl__cmd_desc_t *cmd_desc,
                    svn_boolean_t help, 
                    apr_pool_t *pool,
                    FILE *stream)
{
  const svn_cl__cmd_desc_t *this_cmd
    = svn_cl__get_canonical_command (cmd_desc->name);
  const svn_cl__cmd_desc_t *canonical_cmd = this_cmd;
  svn_boolean_t first_time;

  /* Print the canonical command name. */
  fputs (canonical_cmd->name, stream);

  /* Print the list of aliases. */
  first_time = TRUE;
  for (this_cmd++; (this_cmd->name && this_cmd->is_alias); this_cmd++) 
    {
      if (first_time) {
        fprintf (stream, " (");
        first_time = FALSE;
      }
      else
        fprintf (stream, ", ");
      
      fprintf (stream, "%s", this_cmd->name);
    }

  if (! first_time)
    fprintf (stream, ")");
  
  if (help)
    fprintf (stream, ": %s\n", canonical_cmd->help);
}


/* Print a generic (non-command-specific) usage message. */
static void
print_generic_help (apr_pool_t *pool, FILE *stream)
{
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

  static const char info[] =
    "Subversion is a tool for revision control.\n"
    "For additional information, see http://subversion.tigris.org\n";

  int i = 0;

  fprintf (stream, "%s", usage);
  while (svn_cl__cmd_table[i].name) 
    {
      /*  for (i = 0; i < max; i++) */
      if (! svn_cl__cmd_table[i].is_alias)
        {
          fprintf (stream, "   ");
          print_command_info (svn_cl__cmd_table + i, FALSE, pool, stream);
          fprintf (stream, "\n");
        }
      i++;
    }

  fprintf (stream, "\n");
  fprintf (stream, "%s\n", info);

}


static svn_error_t *
print_version_info (apr_pool_t *pool)
{
  void *ra_baton;
  svn_stringbuf_t *descriptions;
  static const char info[] =
    "Copyright (C) 2000-2001 CollabNet.\n"
    "Subversion is open source software, see http://subversion.tigris.org/\n";

  printf ("Subversion, version %s  (client)\n", SVN_VERSION);
  printf ("   compiled %s, %s\n\n", __DATE__, __TIME__);
  printf ("%s\n", info);

  printf ("The following repository access (RA) modules are available:\n\n");

  /* Get a hash full of all available RA libraries.  */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));

  /* Get a descriptive list of them. */
  SVN_ERR (svn_ra_print_ra_libraries (&descriptions, ra_baton, pool));

  printf ("%s\n", descriptions->data);

  return SVN_NO_ERROR;
}



/* Print either generic help, or command-specific help for each
 * command in os->args.  OPT_STATE is unused and may be null.
 * If OS is null then generic help will always be printed.
 * 
 * Unlike all the other command routines, ``help'' has its own
 * option processing.
 */
svn_error_t *
svn_cl__help (apr_getopt_t *os,
              svn_cl__opt_state_t *opt_state,
              apr_pool_t *pool)
{
  apr_array_header_t *targets;
  int i;

  if (os)
    targets = svn_cl__args_to_target_array (os, pool);

  if (os && targets->nelts)  /* help on subcommand(s) requested */
    for (i = 0; i < targets->nelts; i++)
      {
        svn_stringbuf_t *this = (((svn_stringbuf_t **) (targets)->elts))[i];
	svn_cl__subcommand_help (this->data, pool);
      }
  else if (opt_state && opt_state->version)  /* just -v or --version */
    SVN_ERR (print_version_info (pool));        
  else if (os && !targets->nelts)            /* `-h', `--help', or `help' */
    print_generic_help (pool, stdout);  
  else                                       /* unknown option or cmd */
    print_generic_help (pool, stderr);

  return SVN_NO_ERROR;
}

/* Helper function that will print the usage test of a subcommand
 * given the subcommand name as a char*. This function is also
 * used by subcommands that need to print a usage message */

void
svn_cl__subcommand_help (const char* subcommand,
                         apr_pool_t *pool)
{
  const svn_cl__cmd_desc_t *cmd =
    svn_cl__get_canonical_command (subcommand);
    
  if (cmd)
    print_command_info (cmd, TRUE, pool, stdout);
  else
    fprintf (stderr, "\"%s\": unknown command.\n\n", subcommand);
}


/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end: 
 */
