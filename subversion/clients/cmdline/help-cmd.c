/*
 * help-cmd.c -- Provide help
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include "svn_private_config.h"


/*** Code. ***/

/* Print help or version information. If PRINT_VERSION, print version
   info rather than help. Be less verbose if QUIET. OS is the
   command-line options context, do any allocations from POOL. */
static svn_error_t *
print_help (apr_getopt_t *os,
            svn_boolean_t print_version,
            svn_boolean_t quiet,
            apr_pool_t *pool)
{
  void *ra_baton;

  /* xgettext: the %s is for SVN_VER_NUMBER. */
  char help_header_template[] =
  N_("usage: svn <subcommand> [options] [args]\n"
     "Subversion command-line client, version %s.\n"
     "Type 'svn help <subcommand>' for help on a specific subcommand.\n"
     "\n"
     "Most subcommands take file and/or directory arguments, recursing\n"
     "on the directories.  If no arguments are supplied to such a\n"
     "command, it recurses on the current directory (inclusive) by default.\n"
     "\n"
     "Available subcommands:\n");

  char help_footer[] =
  N_("Subversion is a tool for version control.\n"
     "For additional information, see http://subversion.tigris.org/\n");

  char *help_header =
    apr_psprintf (pool, gettext (help_header_template), SVN_VER_NUMBER);

  const char *ra_desc_start
    = _("The following repository access (RA) modules are available:\n\n");
  svn_stringbuf_t *ra_desc_body, *ra_desc_all;

  ra_desc_all = svn_stringbuf_create (ra_desc_start, pool);
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_print_ra_libraries (&ra_desc_body, ra_baton, pool));
  svn_stringbuf_appendstr (ra_desc_all, ra_desc_body);

  return svn_opt_print_help (os,
                             "svn",   /* ### erm, derive somehow? */
                             print_version,
                             quiet,
                             ra_desc_all->data,
                             help_header,   /* already gettext()'d */
                             svn_cl__cmd_table,
                             svn_cl__options,
                             gettext (help_footer),
                             pool);
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__help (apr_getopt_t *os,
              void *baton,
              apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state;

  if (baton)
    opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  else
    opt_state = NULL;

  return print_help (os,
                     opt_state ? opt_state->version : FALSE,
                     opt_state ? opt_state->quiet : FALSE,
                     pool);
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__version (apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_boolean_t quiet = FALSE;

  if (baton)
    {
      const svn_cl__opt_state_t *const opt_state =
        ((svn_cl__cmd_baton_t *) baton)->opt_state;
      quiet = opt_state->quiet;
    }

  return print_help (os, TRUE, quiet, pool);
}
