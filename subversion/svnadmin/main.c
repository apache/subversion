/*
 * main.c: Subversion server administration tool.
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

  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t keylen;
      void *val;
      svn_fs_dirent_t *this_entry;
      const char *this_full_path;
      int is_dir;
      int i;
      svn_fs_id_t *id;
      svn_stringbuf_t *id_str;

      apr_hash_this (hi, &key, &keylen, &val);
      this_entry = val;

      this_full_path = apr_psprintf (pool, "%s/%s", path, this_entry->name);

      /* Indent. */
      for (i = 0; i < indentation; i++)
        printf (" ");

      printf ("%s", this_entry->name);
      
      SVN_ERR (svn_fs_node_id (&id, root, this_full_path, pool));
      id_str = svn_fs_unparse_id (id, pool);

      SVN_ERR (svn_fs_is_dir (&is_dir, root, this_full_path, pool));
      if (is_dir)
        {

          printf ("/ <%s>\n", id_str->data);  /* trailing slash for dirs */
          print_tree (root, this_full_path, indentation + 1, pool);
        }
      else   /* assume it's a file */
        {
          apr_off_t len;
          SVN_ERR (svn_fs_file_length (&len, root, this_full_path, pool));
          printf (" <%s> [%ld]\n", id_str->data, (long int) len);
        }
    }

  return SVN_NO_ERROR;
}




/*** Argument parsing and usage. ***/
static void
usage (const char *progname, int exit_code)
{
  fprintf
    (exit_code ? stderr : stdout,
     "usage: %s COMMAND REPOS_PATH [LOWER_REV [UPPER_REV]]\n"
     "\n"
     "Commands are: \n"
     "\n"
     "  create   REPOS_PATH\n"
     "\n"
     "  youngest REPOS_PATH\n"
     "\n"
     "  lstxns   REPOS_PATH\n"
     "      Print all txns and their trees.\n"
     "\n"
     "  lsrevs   REPOS_PATH [LOWER_REV [UPPER_REV]]\n"
     "      If no revision is given, all revision trees are printed.\n"
     "      If just LOWER_REV is given, that revision tree is printed.\n"
     "      If two revisions are given, that range is printed, inclusive.\n"
     "\n"
     "Printing a tree shows its structure, node ids, and file sizes.\n"
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
         || (is_lstxn = strcmp(argv[1], "lstxns") == 0)
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
  else if (is_youngest)
    {
      svn_revnum_t youngest_rev;

      err = svn_fs_open_berkeley(fs, path);
      if (err) goto error;

      svn_fs_youngest_rev (&youngest_rev, fs, pool);
      printf ("%ld\n", (long int) youngest_rev);
    }
  else if (is_lstxn)
    {
      char **txns;
      char *txn_name;

      err = svn_fs_open_berkeley(fs, path);
      if (err) goto error;

      err = svn_fs_list_transactions(&txns, fs, pool);
      if (err) goto error;

      /* Loop, printing revisions. */
      while ((txn_name = *txns++))
        {
          svn_fs_txn_t *txn;
          svn_fs_root_t *this_root;
          svn_stringbuf_t *datestamp;
          svn_stringbuf_t *author;
          svn_string_t date_prop = {SVN_PROP_REVISION_DATE,
                                    strlen(SVN_PROP_REVISION_DATE)};
          svn_string_t auth_prop = {SVN_PROP_REVISION_AUTHOR,
                                    strlen(SVN_PROP_REVISION_AUTHOR)};
          apr_pool_t *this_pool = svn_pool_create (pool);

          err = svn_fs_open_txn (&txn, fs, txn_name, this_pool);
          if (err) goto error;

          err = svn_fs_txn_root (&this_root, txn, this_pool);
          if (err) goto error;

          err = svn_fs_txn_prop (&datestamp, txn, &date_prop, this_pool);
          if (err) goto error;
          err = svn_fs_txn_prop (&author, txn, &auth_prop, this_pool);
          if (err) goto error;
          if (! author)
            author = svn_stringbuf_create ("<none>", this_pool);
          
          printf ("Txn %s:\n", txn_name);
          printf ("Author: %s\n", author->data);
          printf ("Created: %s\n", datestamp->data);
          printf ("==========================================\n");
          print_tree (this_root, "", 1, this_pool);
          printf ("\n");

          svn_pool_destroy (this_pool);
        }
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
          svn_stringbuf_t *datestamp;
          svn_stringbuf_t *author;
          apr_pool_t *this_pool = svn_pool_create (pool);
          svn_string_t date_prop = {SVN_PROP_REVISION_DATE,
                                    strlen(SVN_PROP_REVISION_DATE)};
          svn_string_t auth_prop = {SVN_PROP_REVISION_AUTHOR,
                                    strlen(SVN_PROP_REVISION_AUTHOR)};
           
          err = svn_fs_revision_root (&this_root, fs, this, this_pool);
          if (err) goto error;

          err = svn_fs_revision_prop (&datestamp, fs, this,
                                      &date_prop, this_pool);
          if (err) goto error;
          err = svn_fs_revision_prop (&author, fs, this,
                                      &auth_prop, this_pool);
          if (err) goto error;
          if (! author)
            author = svn_stringbuf_create ("<none>", this_pool);

          printf ("Revision %ld\n", (long int) this);
          printf ("Author: %s\n", author->data);
          printf ("Created: %s\n", datestamp->data);
          printf ("==========================================\n");
          print_tree (this_root, "", 1, this_pool);
          printf ("\n");

          svn_pool_destroy (this_pool);
        }
    }

  err = svn_fs_close_fs(fs);
  if (err) goto error;

  svn_pool_destroy (pool);
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
