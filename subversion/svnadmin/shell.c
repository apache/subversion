/*
 * shell.c:  interactive fs shell for 'svnadmin'
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


#include "svnadmin.h"


/*** Code ***/

/* Helper */
static svn_error_t *
path_stat (svn_boolean_t *exists,
           svn_stringbuf_t *path,
           shcxt_t *shcxt,
           apr_pool_t *pool)
{
  apr_hash_t *dirents;
  svn_stringbuf_t *parent, *basename;
  svn_error_t *err;

  /* Sanity check: does path actually exist??  ### oddly, there is no
     'svn_fs_stat', so they only way we can check is by opening its
     -parent- and doing a hash lookup! */

  svn_path_split (path, &parent, &basename, pool);
    
  err = svn_fs_dir_entries (&dirents, shcxt->root, parent->data, pool);
  if (err)
    {
      *exists = FALSE;
      return SVN_NO_ERROR;
    }

  /* else... */

  if ((apr_hash_get (dirents, basename->data, basename->len))
      || (! strcmp (path->data, "/")))
    *exists = TRUE;
  else
    *exists = FALSE;

  return SVN_NO_ERROR;
}



/* Helper: Given a CURRENT_PATH in a SHCXT, and some GIVEN_PATH that
   is either relative to CURRENT_PATH (or absolute), return a
   *NEW_PATH allocated in POOL that is possibly a combination of the
   two.

   Do a sanity check:  if the *NEW_PATH dosen't actually exist in the
   current filesytem revision, then set it to NULL! */
static svn_error_t *
compute_new_path (svn_stringbuf_t **new_path,
                  svn_stringbuf_t *current_path,
                  char *given_path,
                  shcxt_t *shcxt,
                  apr_pool_t *pool)
{
  svn_boolean_t exists;
  svn_stringbuf_t *final_path = svn_stringbuf_dup (current_path, pool);

  if (given_path[0] == '/')
    {
      /* if absolute path, just set final_path to it. */
      svn_stringbuf_setempty (final_path);
      svn_stringbuf_appendcstr (final_path, given_path);
    }
  else if (! strcmp (given_path, ".."))
    {
      /* go up a level */
      svn_path_remove_component (final_path);

      /* our path library is so broken, sigh. */
      if (svn_stringbuf_isempty (final_path))
        svn_stringbuf_appendcstr (final_path, "/");
    }
  else 
    {
      /* just append path to cwd */
      svn_path_add_component_nts (final_path, given_path);
    }

  SVN_ERR (path_stat (&exists, final_path, shcxt, pool));
  if (exists)
    *new_path = final_path;
  else
    *new_path = NULL;

  return SVN_NO_ERROR;
}




/* ----------------------------------------------------------------*/
/** Subcommands **/


static void
help (void)
{
  /* make this not suck.  maybe define shell commands in a list
     somewhere, the way it's done with the svn client app. */
  printf ("\nAvailable commands are:\n");

  printf ("   cd:   change directory\n");
  printf ("   cr:   change revision\n");
  printf ("   ls:   list directory entries\n");
  printf (" exit:   leave this shell\n");
}


/* Change Directory */
static svn_error_t *
cd (char *path,
    shcxt_t *shcxt,
    apr_pool_t *pool)
{
  if (strlen(path) == 0)
    {
      /* no argument?  change to root dir. */
      svn_stringbuf_setempty (shcxt->cwd);
      svn_stringbuf_appendcstr (shcxt->cwd, "/");
    }
  else
    {
      svn_stringbuf_t *new_path;

      SVN_ERR (compute_new_path (&new_path, shcxt->cwd, path,
                                 shcxt, shcxt->pool));

      if (new_path == NULL)
        printf ("No such object: %s\n", path);
      else
        shcxt->cwd = new_path;
    }

  return SVN_NO_ERROR;
}



/* Change Revision;  INVALID revnum means "change to head revision". */
static svn_error_t *
cr (svn_revnum_t rev,
    shcxt_t *shcxt,
    apr_pool_t *pool)
{
  svn_revnum_t youngest;
  svn_boolean_t exists = FALSE;

  SVN_ERR (svn_fs_youngest_rev (&youngest, shcxt->fs, pool));

  if (! SVN_IS_VALID_REVNUM(rev))
    rev = youngest;

  /* sanity check */
  if ((rev < 0) || (rev > youngest))
    {
      /* non-fatal error */
      printf ("There is no revision %" SVN_REVNUM_T_FMT ".\n", rev);
      return SVN_NO_ERROR;
    }

  /* else... */

  /* close the old root */
  if (shcxt->root != NULL)    
    svn_fs_close_root (shcxt->root);

  /* and open the new root */
  shcxt->current_rev = rev;
  SVN_ERR (svn_fs_revision_root (&(shcxt->root),
                                 shcxt->fs,
                                 shcxt->current_rev,
                                 shcxt->pool));

  /* final sanity check:  after switching to the new revision, make
     sure the CWD still exists!  If not, keep cd'ing upwards until you
     find a parent that exists, even if it means going back to the
     root directory. */
  SVN_ERR (path_stat (&exists, shcxt->cwd, shcxt, pool));
  while (exists == FALSE)
    {
      svn_path_remove_component (shcxt->cwd);
      /* again, broken path library */
      if (svn_stringbuf_isempty (shcxt->cwd))
        svn_stringbuf_appendcstr (shcxt->cwd, "/");

      SVN_ERR (path_stat (&exists, shcxt->cwd, shcxt, pool));        
    }

  /* ### if we bumped the user upwards, should we mention it? */

  return SVN_NO_ERROR;
}


/* Helper:  print a single dirent nicely. */
static svn_error_t *
print_dirent (svn_stringbuf_t *abs_path,
              svn_fs_dirent_t *entry,
              shcxt_t *shcxt,
              apr_pool_t *pool)
{
  int is_dir;
  apr_off_t size;
  svn_revnum_t created_rev;
  svn_stringbuf_t *id_str;
  svn_boolean_t has_props;

  apr_hash_t *props;

  /* directory or file? */
  SVN_ERR (svn_fs_is_dir (&is_dir, shcxt->root, abs_path->data, pool));

  /* calculate size of dirent */
  if (is_dir)
    size = 0;
  else
    SVN_ERR (svn_fs_file_length (&size, shcxt->root, abs_path->data, pool));
  
  /* revision in which this file was created. */
  SVN_ERR (svn_fs_node_created_rev (&created_rev,
                                    shcxt->root, abs_path->data, pool));

  /* convert id to a stringbuf */
  id_str = svn_fs_unparse_id (entry->id, pool);

  /* does this object have properties? 
     funny, there's no way to know but to see if we fetch a non-empty
     prophash. */
  SVN_ERR (svn_fs_node_proplist (&props, shcxt->root, abs_path->data, pool));
  if (apr_hash_count (props) != 0)
    has_props = TRUE;
  else
    has_props = FALSE;


  /* Now PRINT all this information. */
  printf ("  <%8s>  [%6ld]  %1d  %10ld",
          id_str->data, created_rev, has_props, (long int) size);
  printf ("    %s", entry->name);
  if (is_dir)
    printf ("/");
  printf ("\n");
  
  return SVN_NO_ERROR;
}


/* List files in CWD, or possibly at relative/absolute PATH.  */
static svn_error_t *
ls (shcxt_t *shcxt,
    char *path,
    apr_pool_t *pool)
{
  svn_stringbuf_t *tmp_path;
  apr_hash_t *dirents;
  apr_hash_index_t *hi;

  /* default */
  svn_stringbuf_t *path_to_list = shcxt->cwd;

  if (strlen(path) != 0)
    {
      /* we want to list some dir -other- than CWD */
      svn_stringbuf_t *new_path;
      
      SVN_ERR (compute_new_path (&new_path, shcxt->cwd, path,
                                 shcxt, pool));
      
      if (new_path == NULL)
        {
          /* non-fatal error */
          printf ("No such object: %s\n", path);
          return SVN_NO_ERROR;
        }
      else
        path_to_list = new_path;
    }
  
  SVN_ERR (svn_fs_dir_entries (&dirents, shcxt->root,
                               path_to_list->data, pool));
  tmp_path = svn_stringbuf_dup (path_to_list, pool);

  for (hi = apr_hash_first (pool, dirents); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      const char *entryname;
      svn_fs_dirent_t *entry;

      apr_hash_this (hi, &key, NULL, &val);
      entryname = (const char *) key;
      entry = (svn_fs_dirent_t *) val;
      svn_path_add_component_nts (tmp_path, entry->name);
      
      SVN_ERR (print_dirent (tmp_path, entry, shcxt, pool));

      svn_path_remove_component (tmp_path);
    }

  fflush (stdout);

  return SVN_NO_ERROR;
}


/* ----------------------------------------------------------------*/
/** Main routines **/

/* Print the SHCXT info in a prompt. */
static void
display_prompt (shcxt_t *shcxt)
{
  /* this could be more sophisticated, or configurable, I suppose. */

  printf ("<%" SVN_REVNUM_T_FMT ": %s>$ ",
          shcxt->current_rev, shcxt->cwd->data);
  fflush (stdout);
}



/* Read stdin into a new stringbuf_t allocated in POOL; input is
   terminated when user types a newline.  Assign stringbuf to
   *INPUT. */
static svn_error_t *
get_input (svn_stringbuf_t **input,
           apr_pool_t *pool)
{
  apr_status_t status;
  apr_file_t *fp;
  char c;
  svn_stringbuf_t *strbuf = svn_stringbuf_create ("", pool);
  
  status = apr_file_open_stdin (&fp, pool);
  if (status)
    return
      svn_error_create (status, 0, NULL, pool,
                        "get_input():  couldn't open STDIN.");

  /* ### Rewrite, to make backspaces work??? */

  while (1)
    {
      status = apr_file_getc (&c, fp);
      if (status && ! APR_STATUS_IS_EOF(status))
        return svn_error_create (status, 0, NULL, pool,
                                 "get_input(): error reading STDIN.");
      if ((c == '\n') || (c == '\r'))
        break;
      
      svn_stringbuf_appendbytes (strbuf, &c, 1);
    }

  *input = strbuf;
  return SVN_NO_ERROR;
}



/* Parse the user's INPUT string, and call some other routine
   appropriately.  This routine will use POOL and SHCXT to do work.

   If FINISHED is set to non-zero, then the user wants to exit the shell.
 */
static svn_error_t *
parse_input (int *finished,
             char *input,
             shcxt_t *shcxt,
             apr_pool_t *pool)
{
  char *subcommand;
  char *state;

  /* Get the subcommand by grabbing the first "token" of input. */
  subcommand = apr_strtok (input, " ", &state);

  if (subcommand == NULL)
    return SVN_NO_ERROR;

  else if (! strcmp(subcommand, "cd"))
    return cd (state, shcxt, pool);
  
  else if (! strcmp(subcommand, "cr"))
    return cr (SVN_STR_TO_REV(state), shcxt, pool);

  else if (! strcmp(subcommand, "ls"))
    return ls (shcxt, state, pool);

  else if (! strcmp(subcommand, "help"))
    {
      help();
      return SVN_NO_ERROR;
    }

  else if ((! strcmp(subcommand, "quit"))
           || (! strcmp(subcommand, "exit")))
    *finished = 1;


  return SVN_NO_ERROR;
}



/* Main Func, called by main()'s subcommand. */
svn_error_t *
svnadmin_run_shell (svn_fs_t *fs, apr_pool_t *pool)
{
  int finished = 0;

  /* create a shell-context object in the top pool.
     start at the HEAD revision, root directory. */
  shcxt_t *shcxt = apr_pcalloc (pool, sizeof(*shcxt));
  shcxt->fs = fs;
  shcxt->pool = pool;
  shcxt->cwd = svn_stringbuf_create ("/", pool);
  SVN_ERR (cr (SVN_INVALID_REVNUM, shcxt, pool));

  printf ("\n");

  while (! finished)
    {
      /* make a subpool for parsing the next user command. */
      apr_pool_t *subpool = svn_pool_create (pool);
      svn_stringbuf_t *input;

      /* display a prompt. */
      display_prompt (shcxt);

      /* get input from user. */
      SVN_ERR (get_input (&input, subpool));

      /* parse input */
      SVN_ERR (parse_input (&finished, input->data, shcxt, subpool));

      /* lather, rinse, repeat. */
      svn_pool_destroy (subpool);
    }
  
  printf ("\n");
  return SVN_NO_ERROR;
}





/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
