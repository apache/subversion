/*
 * util.c: Subversion command line client utility functions. Any
 * functions that need to be shared across subcommands should be put
 * in here.
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

#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_general.h>
#include <apr_lib.h>

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_io.h"
#include "cl.h"


#define DEFAULT_ARRAY_SIZE 5

/* Hmm. This should probably find its way into libsvn_subr -Fitz */
/* Create a SVN string from the char* and add it to the array */
static void 
array_push_svn_stringbuf (apr_array_header_t *array,
                          const char *str,
                          apr_pool_t *pool)
{
  (*((svn_stringbuf_t **) apr_array_push (array)))
    = svn_stringbuf_create (str, pool);
}


/* Some commands take an implicit "." string argument when invoked
 * with no arguments. Those commands make use of this function to
 * add "." to the target array if the user passes no args */
void
svn_cl__push_implicit_dot_target (apr_array_header_t *targets, 
                                  apr_pool_t *pool)
{
  if (targets->nelts == 0)
    array_push_svn_stringbuf (targets, ".", pool);
  assert (targets->nelts);
}

/* Parse a given number of non-target arguments from the
 * command line args passed in by the user. Put them
 * into the opt_state args array */
svn_error_t *
svn_cl__parse_num_args (apr_getopt_t *os,
                        svn_cl__opt_state_t *opt_state,
                        const char *subcommand,
                        int num_args,
                        apr_pool_t *pool)
{
  int i;
  
  opt_state->args = apr_array_make (pool, DEFAULT_ARRAY_SIZE, 
                                    sizeof (svn_stringbuf_t *));

  /* loop for num_args and add each arg to the args array */
  for (i = 0; i < num_args; i++)
    {
      if (os->ind >= os->argc)
        {
          svn_cl__subcommand_help (subcommand, pool);
          return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 
                                   0, 0, pool, "");
        }
      array_push_svn_stringbuf (opt_state->args, os->argv[os->ind++], pool);
    }

  return SVN_NO_ERROR;
}

/* Parse all of the arguments from the command line args
 * passed in by the user. Put them into the opt_state
 * args array */
svn_error_t *
svn_cl__parse_all_args (apr_getopt_t *os,
                        svn_cl__opt_state_t *opt_state,
                        const char *subcommand,
                        apr_pool_t *pool)
{
  opt_state->args = apr_array_make (pool, DEFAULT_ARRAY_SIZE, 
                                    sizeof (svn_stringbuf_t *));

  if (os->ind >= os->argc)
    {
      svn_cl__subcommand_help (subcommand, pool);
      return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, 0, 0, pool, "");
    }

  while (os->ind < os->argc)
    {
      array_push_svn_stringbuf (opt_state->args, os->argv[os->ind++], pool);
    }

  return SVN_NO_ERROR;
}

/* Create a targets array and add all the remaining arguments
 * to it. We also process arguments passed in the --target file, if
 * specified, just as if they were passed on the command line.  */
apr_array_header_t*
svn_cl__args_to_target_array (apr_getopt_t *os,
			      svn_cl__opt_state_t *opt_state,
                              apr_pool_t *pool)
{
  apr_array_header_t *targets =
    apr_array_make (pool, DEFAULT_ARRAY_SIZE, sizeof (svn_stringbuf_t *));
 
  /* Command line args take precendence.  */
  for (; os->ind < os->argc; os->ind++)
    {
      svn_stringbuf_t *target = svn_stringbuf_create (os->argv[os->ind], pool);
      svn_string_t tstr;

      /* If this path looks like it would work as a URL in one of the
         currently available RA libraries, we add it unconditionally
         to the target array. */
      tstr.data = target->data;
      tstr.len  = target->len;
      if (! svn_path_is_url (&tstr))
        {
          const char *basename = svn_path_basename (target->data, pool);

          /* If this target is a Subversion administrative directory,
             skip it.  TODO: Perhaps this check should not call the
             target a SVN admin dir unless svn_wc_check_wc passes on
             the target, too? */
          if (! strcmp (basename, SVN_WC_ADM_DIR_NAME))
            continue;
        }
      else
        {
          svn_path_canonicalize (target);
        }
      (*((svn_stringbuf_t **) apr_array_push (targets))) = target;
    }

  /* Now args from --targets, if any */
  if (NULL != opt_state->targets)
    apr_array_cat(targets, opt_state->targets);

  /* kff todo: need to remove redundancies from targets before
     passing it to the cmd_func. */
     
  return targets;
}

/* Convert a whitespace separated list of items into an apr_array_header_t */
apr_array_header_t*
svn_cl__stringlist_to_array(svn_stringbuf_t *buffer, apr_pool_t *pool)
{
  apr_array_header_t *array = apr_array_make(pool, DEFAULT_ARRAY_SIZE,
                                             sizeof(svn_stringbuf_t *));
  if (buffer != NULL)
    {
      apr_size_t start = 0, end = 0;
      svn_stringbuf_t *item;
      while (end < buffer->len)
        {
          while (apr_isspace(buffer->data[start]))
            start++; 

          end = start;

          while (end < buffer->len && !apr_isspace(buffer->data[end]))
            end++;

          item  = svn_stringbuf_ncreate(&buffer->data[start],
                                          end - start, pool);
          *((svn_stringbuf_t**)apr_array_push(array)) = item;

          start = end;
        }
    }
  return array;
}

/* Convert a newline seperated list of items into an apr_array_header_t */
#define IS_NEWLINE(c) (c == '\n' || c == '\r')
apr_array_header_t*
svn_cl__newlinelist_to_array(svn_stringbuf_t *buffer, apr_pool_t *pool)
{
  apr_array_header_t *array = apr_array_make(pool, DEFAULT_ARRAY_SIZE,
					     sizeof(svn_stringbuf_t *));

  if (buffer != NULL)
    {
      apr_size_t start = 0, end = 0;
      svn_stringbuf_t *item;
      while (end < buffer->len)
        {
          /* Skip blank lines, lines with nothing but spaces, and spaces at
           * the start of a line.  */
	  while (IS_NEWLINE(buffer->data[start]) ||
                 apr_isspace(buffer->data[start]))
	    start++;

	  end = start;

          /* If end of the string, we're done */
	  if (start >= buffer->len)
	    break;

          /* Find The end of this line.  */
	  while (end < buffer->len && !IS_NEWLINE(buffer->data[end]))
	    end++;

	  item = svn_stringbuf_ncreate(&buffer->data[start],
				       end - start, pool);
	  *((svn_stringbuf_t**)apr_array_push(array)) = item;

	  start = end;
	}
    }
  return array;
}
#undef IS_NEWLINE

void
svn_cl__print_commit_info (svn_client_commit_info_t *commit_info)
{
  if ((commit_info) 
      && (SVN_IS_VALID_REVNUM (commit_info->revision)))
    printf ("Committed revision %ld.\n", commit_info->revision);

  return;
}


svn_error_t *
svn_cl__edit_externally (svn_stringbuf_t **edited_contents,
                         svn_stringbuf_t *base_dir,
                         const svn_string_t *contents,
                         apr_pool_t *pool)
{
  const char *editor = NULL;
  const char *command = NULL;
  apr_file_t *tmp_file;
  const char *tmpfile_name;
  apr_status_t apr_err;
  apr_size_t written;
  apr_finfo_t finfo_before, finfo_after;
  svn_error_t *err = SVN_NO_ERROR;

  /* Try to find an editor in the environment. */
  editor = getenv ("SVN_EDITOR");
  if (! editor)
    editor = getenv ("EDITOR");
  if (! editor)
    editor = getenv ("VISUAL");

  /* Worst-case scenario: pick the default editor. */
  if (! editor)
#ifdef SVN_WIN32
    editor = "notepad.exe";
#else
    editor = "vi";
#endif

  /* By now, we had better have an EDITOR to work with. */
  assert (editor);

  /* Ask the working copy for a temporary file based on BASE_DIR, and ask
     APR for that new file's name. */
  SVN_ERR (svn_wc_create_tmp_file (&tmp_file, base_dir, FALSE, pool));
  apr_file_name_get (&tmpfile_name, tmp_file);

  /*** From here one, any problems that occur require us to cleanup
       the file we just created!! ***/

  /* Dump initial CONTENTS to TMP_FILE. */
  apr_err = apr_file_write_full (tmp_file, contents->data, 
                                 contents->len, &written);

  /* Close the file. */
  apr_file_close (tmp_file);
  
  /* Make sure the whole CONTENTS were written, else return an error. */
  if (apr_err || (written != contents->len))
    {
      err = svn_error_create 
        (apr_err ? apr_err : SVN_ERR_INCOMPLETE_DATA, 0, NULL, pool,
         "Unable to write initial contents to temporary file.");
      goto cleanup;
    }

  /* Create the editor command line. */
  command = apr_psprintf (pool, "%s %s", editor, tmpfile_name);

  /* Get information about the temporary file before the user has
     been allowed to edit its contents. */
  apr_stat (&finfo_before, tmpfile_name, 
            APR_FINFO_MTIME | APR_FINFO_SIZE, pool);

  /* Now, run the editor command line.  

     ### todo: The following might be better done using APR's
     process code (or svn_io_run_cmd, perhaps).  */
  system (command);

  /* Get information about the temporary file after the assumed editing. */
  apr_stat (&finfo_after, tmpfile_name, 
            APR_FINFO_MTIME | APR_FINFO_SIZE, pool);
  
  /* If the file looks changed... */
  if ((finfo_before.mtime != finfo_after.mtime) ||
      (finfo_before.size != finfo_after.size))
    {
      svn_stringbuf_t *new_contents = svn_stringbuf_create ("", pool);

      /* We have new contents in a temporary file, so read them. */
      apr_err = apr_file_open (&tmp_file, tmpfile_name, APR_READ,
                               APR_UREAD, pool);
      
      if (apr_err)
        {
          /* This is an annoying situation, as the file seems to have
             been edited but we can't read it! */
          
          /* ### todo: Handle error here. */
          goto cleanup;
        }
      else
        {
          /* Read the entire file into memory. */
          svn_stringbuf_ensure (new_contents, finfo_after.size + 1);
          apr_err = apr_file_read_full (tmp_file, 
                                        new_contents->data, 
                                        finfo_after.size,
                                        &(new_contents->len));
          new_contents->data[new_contents->len] = 0;
          
          /* Close the file */
          apr_file_close (tmp_file);
          
          /* Make sure we read the whole file, or return an error if we
             didn't. */
          if (apr_err || (new_contents->len != finfo_after.size))
            {
              /* ### todo: Handle error here. */
              goto cleanup;
            }
        }

      /* Return the new contents. */
      *edited_contents = new_contents;
    }
  else
    {
      /* No edits seem to have been made.  Just dup the original
         contents. */
      *edited_contents = NULL;
    }

 cleanup:

  /* Destroy the temp file if we created one. */
  apr_file_remove (tmpfile_name, pool); /* ignore status */

  /* ...and return! */
  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
