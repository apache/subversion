/*
 * help-cmd.c -- Provide help
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_version.h"
#include "svn_utf.h"
#include "cl.h"


/*** Code. ***/

const char svn_cl__help_header[] =
    "usage: svn <subcommand> [options] [args]\n"
    "Type \"svn help <subcommand>\" for help on a specific subcommand.\n"
    "\n"
    "Most subcommands take file and/or directory arguments, recursing\n"
    "on the directories.  If no arguments are supplied to such a\n"
    "command, it will recurse on the current directory (inclusive) by\n" 
    "default.\n"
    "\n"
    "Available subcommands:\n";

const char svn_cl__help_footer[] =
    "Subversion is a tool for revision control.\n"
    "For additional information, see http://subversion.tigris.org\n";


static svn_error_t *
print_version_info (svn_boolean_t quiet, apr_pool_t *pool)
{
  void *ra_baton;
  svn_stringbuf_t *descriptions;
  const char *descriptions_native;
  static const char info[] =
    "Copyright (C) 2000-2002 CollabNet.\n"
    "Subversion is open source software, see http://subversion.tigris.org/\n";

  if (quiet)
    {
      printf ("%s\n", SVN_VER_NUMBER);
      return SVN_NO_ERROR;
    }

  printf ("Subversion Client, version %s\n", SVN_VERSION);
  printf ("   compiled %s, %s\n\n", __DATE__, __TIME__);
  printf ("%s\n", info);

  printf ("The following repository access (RA) modules are available:\n\n");

  /* Get a hash full of all available RA libraries.  */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));

  /* Get a descriptive list of them. */
  SVN_ERR (svn_ra_print_ra_libraries (&descriptions, ra_baton, pool));

  SVN_ERR (svn_utf_cstring_from_utf8_stringbuf (&descriptions_native,
                                                descriptions, pool));

  printf ("%s\n", descriptions_native);

  return SVN_NO_ERROR;
}



/* Print either generic help, or command-specific help for each
 * command in os->args.  OPT_STATE is only examined for the
 * '--version' switch.  If OS is null then generic help will always be
 * printed.
 * 
 * Unlike all the other command routines, ``help'' has its own
 * option processing.
 *
 * This implements the `svn_opt_subcommand_t' interface.
 *
 * ### todo: this should really be abstracted into svn_opt_help or
 * something, since all the command line programs want to offer help
 * in the same format... But one step at a time.
 */
svn_error_t *
svn_cl__help (apr_getopt_t *os,
              void *baton,
              apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = baton;
  apr_array_header_t *targets = NULL;
  int i;

  if (os)
    SVN_ERR (svn_cl__parse_all_args (&targets, os, pool));

  if (targets && targets->nelts)  /* help on subcommand(s) requested */
    for (i = 0; i < targets->nelts; i++)
      {
        svn_opt_subcommand_help (((const char **) (targets->elts))[i],
                                 svn_cl__cmd_table,
                                 svn_cl__options,
                                 pool);
      }
  else if (opt_state && opt_state->version)  /* just --version */
    SVN_ERR (print_version_info (opt_state->quiet, pool));
  else if (os && !targets->nelts)            /* `-h', `--help', or `help' */
    svn_opt_print_generic_help (svn_cl__help_header,
                                svn_cl__cmd_table,
                                svn_cl__options,
                                svn_cl__help_footer,
                                pool,
                                stdout);
  else                                       /* unknown option or cmd */
    fprintf (stderr, "Type `svn help' for help on Subversion usage.\n");

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
