/*
 * main.c: Subversion server inspection tool.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <apr_general.h>
#include <apr_pools.h>

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"



#define INT_ERR(expr)                              \
  do {                                             \
    svn_error_t *svn_err__temp = (expr);           \
    if (svn_err__temp)                             \
      svn_handle_error (svn_err__temp, stdout, 1); \
  } while (0)


typedef enum svnlook_cmd_t
{
  svnlook_cmd_default = 0,
  svnlook_cmd_log,
  svnlook_cmd_author,
  svnlook_cmd_date,
  svnlook_cmd_dirschanged,
  svnlook_cmd_changed,
  svnlook_cmd_diff
  
} svnlook_cmd_t;



/*** Argument parsing and usage. ***/
static void
usage (const char *progname, int exit_code)
{
  fprintf
    (exit_code ? stderr : stdout,
     "usage: %s REPOS_PATH rev REV [COMMAND] - inspect revision REV\n"
     "       %s REPOS_PATH txn TXN [COMMAND] - inspect transaction TXN\n"
     "       %s REPOS_PATH [COMMAND] - inspect the youngest revision\n"
     "\n"
     "If no command is given, general information about the revision\n"
     "or transaction will be given.\n"
     "\n"
     "COMMAND can be one of: \n"
     "\n"
     "   log:           print log message to stdout.\n"
     "   author:        print author to stdout\n"
     "   date:          date to stdout (only for revs, not txns)\n"
     "   dirs-changed:  directories in which things were changed\n"
     "   changed:       full change summary: all dirs & files changed\n"
     "   diff:          GNU diffs of changed files, prop diffs too\n"
     "\n",
     progname,
     progname,
     progname);

  exit (exit_code);
}



/*** Main. ***/

int
main (int argc, const char * const *argv)
{
  apr_pool_t *pool;
  svn_fs_t *fs;
  svnlook_cmd_t command;
  const char *repos_path = NULL, *txn_name = NULL;
  svn_boolean_t is_revision = FALSE;
  svn_revnum_t rev_id = SVN_INVALID_REVNUM;
  svn_fs_root_t *root;
  int cmd_offset = 4;

  /* We require at least 3 arguments:  REPOS_PATH, REV/TXN, ID  */
  if (argc < 4)
    {
      usage (argv[0], 1);
      return EXIT_FAILURE;
    }

  /* Argument 1 is the repository path. */
  repos_path = argv[1];

  /* Argument 2 could be "rev" or "txn".  If "rev", Argument 3 is a
     numerical revision number.  If "txn", Argument 3 is a transaction
     name string.  If neither, this is an inspection of the youngest
     revision.  */
  if (! strcmp (argv[2], "txn")) /* transaction */
    {
      is_revision = FALSE;
      txn_name = argv[3];
    }
  else if (! strcmp (argv[2], "rev")) /* revision */
    {
      is_revision = TRUE;
      rev_id = atoi (argv[3]);
    }
  else /* youngest revision */
    {
      is_revision = TRUE;
      cmd_offset = 2;
    }

  /* If there is a subcommand, parse it. */
  if (argc >= 5)
    {
      if (! strcmp (argv[cmd_offset], "log"))
        command = svnlook_cmd_log;
      else if (! strcmp (argv[cmd_offset], "author"))
        command = svnlook_cmd_author;
      else if (! strcmp (argv[cmd_offset], "date"))
        command = svnlook_cmd_date;
      else if (! strcmp (argv[cmd_offset], "dirs-changed"))
        command = svnlook_cmd_dirschanged;
      else if (! strcmp (argv[cmd_offset], "changed"))
        command = svnlook_cmd_changed;
      else if (! strcmp (argv[cmd_offset], "diff"))
        command = svnlook_cmd_diff;
      else
        {
          usage (argv[0], 2);
          return EXIT_FAILURE;
        }
    }
  else
    {
      command = svnlook_cmd_default;
    }

  /* Now, let's begin processing.  */

  /* Initialize APR and our top-level pool. */
  apr_initialize ();
  pool = svn_pool_create (NULL);

  /* Allocate a new filesystem object. */
  fs = svn_fs_new (pool);

  /* Open the repository with the given path. */
  INT_ERR (svn_fs_create_berkeley (fs, repos_path));

  /* Open up the appropriate root (revision or transaction). */
  if (is_revision)
    {
      /* If we didn't get a valid revision number, we'll look at the
         youngest revision. */
      if (! SVN_IS_VALID_REVNUM (rev_id))
        INT_ERR (svn_fs_youngest_rev (&rev_id, fs, pool));

      INT_ERR (svn_fs_revision_root (&root, fs, rev_id, pool));
    }
  else
    {
      svn_fs_txn_t *txn;
      INT_ERR (svn_fs_open_txn (&txn, fs, txn_name, pool));
      INT_ERR (svn_fs_txn_root (&root, txn, pool));
    }

  /* Now, we have an FS, a ROOT, and an COMMAND.  Let's go to work. */
  switch (command)
    {
    case svnlook_cmd_log:
    case svnlook_cmd_author:
    case svnlook_cmd_date:
    case svnlook_cmd_dirschanged:
    case svnlook_cmd_changed:
    case svnlook_cmd_diff:
    case svnlook_cmd_default:
    default:
    }

  /* Cleanup after ourselves. */
  svn_pool_destroy (pool);
  apr_terminate ();

  return EXIT_SUCCESS;
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
