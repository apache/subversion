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


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__help (apr_getopt_t *os,
              void *baton,
              apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = baton;

  void *ra_baton;
  const char *ra_desc_start
    = "The following repository access (RA) modules are available:\n\n";
  svn_stringbuf_t *ra_desc_body, *ra_desc_all;

  ra_desc_all = svn_stringbuf_create (ra_desc_start, pool);
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_print_ra_libraries (&ra_desc_body, ra_baton, pool));
  svn_stringbuf_appendstr (ra_desc_all, ra_desc_body);

  SVN_ERR (svn_opt_print_help (os,
                               "svn",   /* ### erm, derive somehow? */
                               opt_state ? opt_state->version : FALSE,
                               opt_state ? opt_state->quiet : FALSE,
                               ra_desc_all->data,
                               svn_cl__help_header,
                               svn_cl__cmd_table,
                               svn_cl__options,
                               svn_cl__help_footer,
                               pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
