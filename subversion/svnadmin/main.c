/*
 * main.c: Subversion server administration tool.
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

#include <apr_general.h>
#include <apr_pools.h>

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_fs.h"




/*** Tree printing. ***/

/* Print the tree at ROOT:PATH, indenting by INDENTATION spaces.
   Use POOL for any allocation.  */
static svn_error_t *
print_tree (svn_fs_root_t *root,
            const char *path,
            int indentation,
            apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  
  SVN_ERR (svn_fs_dir_entries (&entries, root, path, pool));

  for (hi = apr_hash_first (entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t keylen;
      void *val;
      svn_fs_dirent_t *this_entry;
      const char *this_full_path;
      int is_dir;
      int i;
 
      apr_hash_this (hi, &key, &keylen, &val);
      this_entry = val;

      this_full_path = apr_psprintf (pool, "%s/%s", path, this_entry->name);

      /* Indent. */
      for (i = 0; i < indentation; i++)
        printf (" ");

      printf ("%s", this_entry->name);
      
      SVN_ERR (svn_fs_is_dir (&is_dir, root, this_full_path, pool));
      if (is_dir)
        {
          printf ("/\n");  /* trailing slash for dirs */
          print_tree (root, this_full_path, indentation + 1, pool);
        }
      else   /* assume it's a file */
        {
          apr_off_t len;
          SVN_ERR (svn_fs_file_length (&len, root, this_full_path, pool));
          printf ("[%ld]\n", (long int) len);
        }
    }

  return SVN_NO_ERROR;
}




/*** Argument parsing and usage. ***/
void
usage (const char *progname, int exit_code)
{
  fprintf
    (exit_code ? stderr : stdout,
     "usage: %s COMMAND REPOS_PATH [LOWER_REV [UPPER_REV]]\n"
     "\n"
     "Commands are: \n"
     "  - create   REPOS_PATH\n"
     "  - youngest REPOS_PATH\n"
     "  - lstxn    REPOS_PATH\n"
     "  - lsrevs   REPOS_PATH [LOWER_REV [UPPER_REV]]\n"
     "      If no revision is given, all revision trees are printed.\n"
     "      If just LOWER_REV is given, that revision trees is printed.\n"
     "      If two revisions are given, that range is printed, inclusive.\n"
     "      (Printing a revision tree shows its structure and file sizes.)\n"
     "\n",
     progname);

  exit (exit_code);
}



/*** Main. ***/

int
main (int argc, const char * const *argv)
{
  apr_pool_t *pool;
  svn_fs_t *fs;
  svn_error_t *err;
  int               /* commands */
    is_create = 0,
    is_youngest = 0,
    is_lstxn = 0,
    is_lsrevs = 0;
  const char *path = NULL;

  /* ### this whole thing needs to be cleaned up once client/main.c
     ### is refactored. for now, let's just get the tool up and
     ### running. */

  if (argc < 3)
    {
      usage (argv[0], 1);
      return EXIT_FAILURE;
    }

  path = argv[2];

  if (! ((is_create = strcmp(argv[1], "create") == 0)
         || (is_youngest = strcmp(argv[1], "youngest") == 0)
         || (is_lstxn = strcmp(argv[1], "lstxn") == 0)
         || (is_lsrevs = strcmp(argv[1], "lsrevs") == 0)))
    {
      usage (argv[0], 1);
      return EXIT_FAILURE;
    }

  apr_initialize ();
  pool = svn_pool_create (NULL);

  fs = svn_fs_new(pool);

  if (is_create)
    {
      err = svn_fs_create_berkeley(fs, path);
      if (err) goto error;
    }
  else if (is_lstxn)
    {
      char **txns;

      err = svn_fs_open_berkeley(fs, path);
      if (err) goto error;

      err = svn_fs_list_transactions(&txns, fs, pool);
      if (err) goto error;

      while (*txns != NULL)
        printf("%s\n", *txns++);
    }
  else if (is_youngest)
    {
      svn_revnum_t youngest_rev;

      err = svn_fs_open_berkeley(fs, path);
      if (err) goto error;

      svn_fs_youngest_rev (&youngest_rev, fs, pool);
      printf ("%ld\n", (long int) youngest_rev);
    }
  else if (is_lsrevs)
    {
      svn_revnum_t
        lower = SVN_INVALID_REVNUM,
        upper = SVN_INVALID_REVNUM,
        this;

      err = svn_fs_open_berkeley(fs, path);
      if (err) goto error;

      /* Do the args tell us what revisions to inspect? */
      if (argv[3])
        {
          lower = (svn_revnum_t) atoi (argv[3]);
          if (argv[4])
            upper = (svn_revnum_t) atoi (argv[4]);
        }

      /* Fill in for implied args. */
      if (lower == SVN_INVALID_REVNUM)
        {
          lower = 0;
          svn_fs_youngest_rev (&upper, fs, pool);
        }
      else if (upper == SVN_INVALID_REVNUM)
        upper = lower;

      /* Loop, printing revisions. */
      for (this = lower; this <= upper; this++)
        {
          svn_fs_root_t *this_root;
          apr_pool_t *this_pool = svn_pool_create (pool);

          err = svn_fs_revision_root (&this_root, fs, this, this_pool);
          if (err) goto error;

          printf ("Revision %ld:\n", (long int) this);
          printf ("===============\n");
          print_tree (this_root, "", 1, this_pool);
          printf ("\n");

          apr_pool_destroy (this_pool);
        }
    }

  err = svn_fs_close_fs(fs);
  if (err) goto error;

  apr_pool_destroy (pool);
  apr_terminate();

  return EXIT_SUCCESS;

 error:
  svn_handle_error(err, stderr, FALSE);
  return EXIT_FAILURE;
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
