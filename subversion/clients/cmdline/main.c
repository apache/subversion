/*
 * main.c:  Subversion command line client.
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

#include <string.h>
#include <assert.h>
#include <locale.h>

#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_general.h>

#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_time.h"
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
  { "add",        FALSE, svn_cl__add,
    "Add new files and directories to version control.\n"
    "usage: add [TARGETS]\n" },
  { "ad",         TRUE, NULL, NULL },
  { "new",        TRUE, NULL, NULL },

  { "checkout",   FALSE, svn_cl__checkout,
    "Check out a working directory from a repository.\n"
    "usage: checkout REPOSPATH1 [REPOSPATH2 REPOSPATH3...]\n" },
  { "co",         TRUE, NULL, NULL },

  { "commit",     FALSE, svn_cl__commit,
    "Commit changes from your working copy to the repository.\n"
    "usage: commit [TARGETS]\n" },
  { "ci",         TRUE, NULL, NULL },

  { "delete",     FALSE, svn_cl__delete,
    "Remove files and directories from version control.\n"
    "usage: delete [TARGETS]\n" },
  { "del",        TRUE, NULL, NULL },
  { "remove",     TRUE, NULL, NULL },
  { "rm",         TRUE, NULL, NULL },

  { "help",       FALSE, svn_cl__help,
    "Display this usage message.\n"
    "usage: help [SUBCOMMAND1 [SUBCOMMAND2] ...]\n" },
  { "?",          TRUE, NULL, NULL },
  { "h",          TRUE, NULL, NULL },
  /* We need to support "--help", "-?", and all that good stuff, of
     course.  But those options, since unknown, will result in the
     help message being printed out anyway, so there's no need to
     support them explicitly. */

  { "import",     FALSE, svn_cl__import,
    "Import a file or tree into the repository.\n"
    "usage: import REPOS_URL [PATH] [NEW_ENTRY_IN_REPOS] \n" },

  { "proplist",   FALSE, svn_cl__proplist,
    "List all properties for given files and directories.\n"
    "usage: proplist [TARGETS]\n" },
  { "plist",      TRUE, NULL, NULL },
  { "pl",         TRUE, NULL, NULL },

  { "propget",    FALSE, svn_cl__propget,
    "Get the value of property PROPNAME on files and directories.\n"
    "usage: propget PROPNAME [TARGETS]\n" },
  { "pget",       TRUE, NULL, NULL },
  { "pg",         TRUE, NULL, NULL },

  { "propset",    FALSE, svn_cl__propset, 
    "Set property PROPNAME to PROPVAL on files and directories.\n"
    "usage: propset PROPNAME [PROPVAL | --valfile VALFILE] "
    "[TARGETS]\n"},
  { "pset",       TRUE, NULL, NULL },
  { "ps",         TRUE, NULL, NULL },

  { "propdel",    FALSE, svn_cl__propdel, 
    "Remove property PROPNAME on files and directories.\n"
    "usage: propdel PROPNAME [TARGETS]\n"},
  { "pdel",       TRUE, NULL, NULL },

  { "status",     FALSE, svn_cl__status,
    "Print the status of working copy files and directories.\n"
    "usage: status [TARGETS]\n" },
  { "stat",       TRUE, NULL, NULL },
  { "st",         TRUE, NULL, NULL },
 
  { "diff",       FALSE, svn_cl__diff,
    "Display local file changes as contextual diffs.\n"
    "usage: diff [TARGETS]\n" },
  { "di",         TRUE, NULL, NULL },

  { "update",     FALSE, svn_cl__update,
    "Bring changes from the repository into the working copy.\n"
    "usage: update [TARGETS]\n" },
  { "up",         TRUE, NULL, NULL },

  { "cleanup",    FALSE, svn_cl__cleanup,
    "Recursively clean up the working copy, removing locks, resuming\n"
    "unfinished operations, etc.\n"
    "usage: cleanup [TARGETS]\n" },

  { "revert",     FALSE, svn_cl__revert,
    "Restore pristine working copy file (undo all local edits)\n"
    "usage: revert [TARGETS]\n" },

  { NULL,         FALSE, NULL, NULL }
};



/*** Main. ***/

int
main (int argc, const char * const *argv)
{
  apr_status_t apr_err;
  svn_error_t *err;
  apr_pool_t *pool;
  int opt_id;
  const char *opt_arg;
  apr_getopt_t *os;
  svn_cl__opt_state_t opt_state;
  const svn_cl__cmd_desc_t *subcommand = NULL;

  static const apr_getopt_option_t options[] =
  {
    {"destination",   'd', 1}, 
    {"force",         svn_cl__force_opt, 0},
    {"help",          'h', 0},
    {"message",       'm', 1},
    {"quiet",         'q', 0},
    {"recursive",     svn_cl__recursive_opt, 0},
    {"nonrecursive",  'n', 0},
    {"revision",      'r', 1},
    {"date",          'D', 1},
    {"filedata",      'F', 1},
    {"xml-file",      svn_cl__xml_file_opt, 1},
    {"locale",        svn_cl__locale_opt, 1},
    {"version",       svn_cl__version_opt, 0},
    {"verbose",       'v', 0},
    {"show-updates",  'u', 0},
    /* Here begin authentication args, add more as needed: */
    {"username",      svn_cl__auth_username_opt, 1},
    {"password",      svn_cl__auth_password_opt, 1},    
    {"extensions",    'x', 1},
    {0,               0, 0}
  };

  /* FIXME: This is a first step towards support for localization in
     `svn'.  In real life, this call would be

         setlocale (LC_ALL, "");

     so that initial help or error messages are displayed in the
     language defined by the environment.  Right now, though, we don't
     even care if the call fails.

     (Actually, this is a no-op; according to the C standard, "C" is
     the default locale at program startup.) */
  setlocale (LC_ALL, "C");


  apr_initialize ();
  pool = svn_pool_create (NULL);
  svn_cl__init_feedback_vtable (pool);
  memset (&opt_state, 0, sizeof (opt_state));
  opt_state.revision = SVN_INVALID_REVNUM;

  /* No args?  Show usage. */
  if (argc <= 1)
    {
      svn_cl__help (NULL, NULL, pool);
      svn_pool_destroy (pool);
      return EXIT_FAILURE;
    }

  /* Else, parse options. */
  apr_getopt_init (&os, pool, argc, argv);
  os->interleave = 1;
  while (1)
    {
      /* Parse the next option. */
      apr_err = apr_getopt_long (os, options, &opt_id, &opt_arg);
      if (APR_STATUS_IS_EOF (apr_err))
        break;
      else if (! APR_STATUS_IS_SUCCESS (apr_err))
        {
          svn_cl__help (NULL, NULL, pool);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }

      switch (opt_id) {
      case 'm':
        opt_state.message = svn_stringbuf_create (opt_arg, pool);
        break;
      case 'r':
        opt_state.revision = (svn_revnum_t) atoi (opt_arg);
        break;
      case 'D':
        /* svn_parse_date() originates in getdate.y; while I'd love to
           change it to const char *, that turns out to be a little
           more complex than just adding the qualifier.  So for now,
           I'm casting to get rid of the compilation warning, and have
           filed issue #408 so we don't forget about this.  -kff  */
        apr_ansi_time_to_apr_time (&opt_state.date,
                                   svn_parse_date ((char *) opt_arg, NULL));
        break;
      case 'v':
        opt_state.verbose = TRUE;
        break;
      case 'u':
        opt_state.update = TRUE;
        break;
      case 'h':
      case '?':
        opt_state.help = TRUE;
        break;
      case 'q':
        opt_state.quiet = TRUE;
        break;
      case svn_cl__xml_file_opt:
        opt_state.xml_file = svn_stringbuf_create (opt_arg, pool);
        break;
      case 'd':
        opt_state.target = svn_stringbuf_create (opt_arg, pool);
        break;
      case 'F':
        err = svn_string_from_file (&(opt_state.filedata), opt_arg, pool);
        if (err)
          svn_handle_error (err, stdout, TRUE);
        break;
      case 'M':
        opt_state.modified = TRUE;
        break;
      case svn_cl__force_opt:
        opt_state.force = TRUE;
        break;
      case svn_cl__recursive_opt:
        opt_state.recursive = TRUE;
        break;
      case 'n':
        opt_state.nonrecursive = TRUE;
        break;
      case svn_cl__version_opt:
        opt_state.version = TRUE;
        opt_state.help = TRUE;
        break;
      case svn_cl__auth_username_opt:
        opt_state.auth_username = svn_stringbuf_create (opt_arg, pool);
        break;
      case svn_cl__auth_password_opt:
        opt_state.auth_password = svn_stringbuf_create (opt_arg, pool);
        break;
      case svn_cl__locale_opt:
        /* The only locale name that ISO C defines is the "C" locale;
           using any other argument is not portable. But that's O.K.,
           because the main purpose of this option is:

              a) support for wrapper programs which parse `svn's
                 output, and should call `svn --locale=C' to get
                 predictable results; and

              b) for testing various translations without having to
                 twiddle with the environment.
        */
        if (NULL == setlocale (LC_ALL, opt_arg))
          {
            err = svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR,
                                     0, NULL, pool,
                                     "The locale `%s' can not be set",
                                     opt_arg);
            svn_handle_error (err, stderr, FALSE);
          }
        break;
      case 'x':
        opt_state.extensions = svn_stringbuf_create(opt_arg, pool);
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
          svn_cl__help (NULL, NULL, pool);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }
      else
        {
          const char *first_arg = os->argv[os->ind++];
          subcommand = svn_cl__get_canonical_command (first_arg);
          if (subcommand == NULL)
            {
              /* FIXME: should we print "unknown foo" ?? seems ok */
              fprintf (stderr, "unknown command: %s\n", first_arg);
              svn_cl__help (NULL, NULL, pool);
              svn_pool_destroy (pool);
              return EXIT_FAILURE;
            }
        }
    }

  /* If we made it this far, then we definitely have the subcommand,
     so call it. */
  err = (*subcommand->cmd_func) (os, &opt_state, pool);
  if (err)
    {
      if (err->apr_err != SVN_ERR_CL_ARG_PARSING_ERROR)
        svn_handle_error (err, stderr, 0);
      svn_pool_destroy (pool);
      return EXIT_FAILURE;
    }
  else
    {
      svn_pool_destroy (pool);
      return EXIT_SUCCESS;
    }
}



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end: 
 */
