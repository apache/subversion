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

static svn_error_t *
print_version_info (apr_pool_t *pool)
{
  void *ra_baton;
  svn_stringbuf_t *descriptions;
  const char *descriptions_native;
  static const char info[] =
    "Copyright (C) 2000-2002 CollabNet.\n"
    "Subversion is open source software, see http://subversion.tigris.org/\n";

  printf ("Subversion Client, version %s\n", SVN_VERSION);
  printf ("   compiled %s, %s\n\n", __DATE__, __TIME__);
  printf ("%s\n", info);

  printf ("The following repository access (RA) modules are available:\n\n");

  /* Get a hash full of all available RA libraries.  */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));

  /* Get a descriptive list of them. */
  SVN_ERR (svn_ra_print_ra_libraries (&descriptions, ra_baton, pool));

  SVN_ERR (svn_utf_cstring_from_utf8_stringbuf (descriptions,
                                                &descriptions_native, pool));

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
 */
svn_error_t *
svn_cl__help (apr_getopt_t *os,
              svn_cl__opt_state_t *opt_state,
              apr_pool_t *pool)
{
  apr_array_header_t *targets = NULL;
  int i;

  if (os)
    SVN_ERR (svn_cl__parse_all_args (&targets, os, pool));

  if (targets && targets->nelts)  /* help on subcommand(s) requested */
    for (i = 0; i < targets->nelts; i++)
      {
        svn_cl__subcommand_help (((const char **) (targets->elts))[i], pool);
      }
  else if (opt_state && opt_state->version)  /* just -v or --version */
    SVN_ERR (print_version_info (pool));        
  else if (os && !targets->nelts)            /* `-h', `--help', or `help' */
    svn_cl__print_generic_help (pool, stdout);  
  else                                       /* unknown option or cmd */
    svn_cl__print_generic_help (pool, stderr);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
