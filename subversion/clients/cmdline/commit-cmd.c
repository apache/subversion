/*
 * commit-cmd.c -- Check changes into the repository.
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

#include <apr_general.h>
#include <apr_file_info.h>
#include <apr_lib.h>

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_sorts.h"
#include "cl.h"

#include "client_errors.h"



/*** Code. ***/

/*
 * Prints a single status line to a given file about the given entry.
 * Return failure, that means TRUE if something went wrong.
 */
static svn_boolean_t
print_single_file_status (apr_file_t *file,
                          const char *path,
                          const char *editor_prefix,
                          svn_wc_status_t *status)
{
  char str_status[5];
  apr_size_t size;
  apr_size_t written;
  svn_boolean_t failure = FALSE;
  apr_status_t rc;
  char array[80];
  const char newline[]="\n"; /* ### FIXME: This should somehow be different
                                for different platforms... */

  if (! status)
    /* this shouldn't happen */
    return TRUE;

  /* Create local-mod status code block. */
  if (status->text_status !=  svn_wc_status_unversioned)
    {
      /* skip locally present files that aren't versioned, they won't
         serve any purpose in this output */

      svn_cl__generate_status_codes (str_status,
                                     status->text_status,
                                     status->prop_status,
                                     status->locked,
                                     status->copied);

      /* write the prefix and status output first */
      apr_snprintf (array, sizeof (array),
                    "%s   %s   ", editor_prefix, str_status);
      
      size = strlen (array);

      rc = apr_file_write_full (file, array, size, &written);
      if ((size != written) || (! APR_STATUS_IS_SUCCESS (rc)))
        {
          /* we didn't write the complete line, return an error code */
          return TRUE;
        }

      /* now write the full path without any length restrictions */
      size = strlen (path);
      rc = apr_file_write_full (file, path, size, &written);
      if ((size != written) || (! APR_STATUS_IS_SUCCESS (rc)))
        {
          /* we didn't write the complete path, return an error code */
          return TRUE;
        }

      /* and finally append a newline to the output */
      size = strlen(newline);
      rc = apr_file_write_full (file, newline, size, &written);
      if ((size != written) || (! APR_STATUS_IS_SUCCESS (rc)))
        {
          /* we didn't write the complete path, return an error code */
          return TRUE;
        }

    }

  return failure;
}

/*
 * Walks throught the 'nelts' of the given hash and calls the status-
 * print function for each.
 */
static svn_boolean_t
print_hash_status (apr_file_t *file,
                   const char *editor_prefix,
                   apr_hash_t *statushash, 
                   apr_pool_t *pool)
{
  int i;
  apr_array_header_t *statusarray;
  svn_wc_status_t *status = NULL;
  svn_boolean_t failure=FALSE;

  /* Convert the unordered hash to an ordered, sorted array */
  statusarray = apr_hash_sorted_keys (statushash,
                                      svn_sort_compare_items_as_paths,
                                      pool);

  /* Loop over array, printing each name/status-structure */
  for (i = 0; i < statusarray->nelts; i++)
    {
      const svn_item_t *item;

      item = &APR_ARRAY_IDX (statusarray, i, const svn_item_t);
      status = item->value;

      if (print_single_file_status (file, item->key,
                                    editor_prefix, status))
        {
          failure = TRUE;
          break;
        }
    }

  return failure;
}

/*
 * This function gather a status output to be used within a commit message,
 * possibly edited in your favourite $EDITOR.
 */
static svn_error_t *
write_status_to_file (apr_pool_t *pool,
                      apr_file_t *file,
                      const char *editor_prefix,
                      svn_client_auth_baton_t *auth_baton,
                      svn_cl__opt_state_t *opt_state,
                      apr_array_header_t *targets)
{
  apr_hash_t *statushash;
  svn_revnum_t youngest = SVN_INVALID_REVNUM;

  int i;
  for (i = 0; i < targets->nelts; i++)
    {
      svn_stringbuf_t *target = ((svn_stringbuf_t **) (targets->elts))[i];

      /* Retrieve a hash of status structures with the information
         requested by the user.

         svn_client_status directly understands the three commandline
         switches (-n, -u, -[vV]) : */

      SVN_ERR (svn_client_status (&statushash, &youngest, target, auth_baton,
                                  opt_state->nonrecursive ? FALSE : TRUE,
                                  opt_state->verbose,
                                  FALSE, /* no update */
                                  pool));

      /* Now print the structures to a file. */
      if (print_hash_status (file, editor_prefix, statushash, pool))
        {
          return svn_error_create
            (SVN_ERR_CMDLINE__TMPFILE_WRITE, 0, NULL, 
             pool,
             "Failed to write status information to temporary file: %s");
        }
    }

  return SVN_NO_ERROR;
}

/* Return a pointer to the char after the newline or NULL if there is no
   newline */
static char *
get_next_line( char *line )
{
  /* ### FIXME: this is not fully portable for other kinds of newlines! */
  char *newline = strchr(line, '\n');
  if(newline)
    newline++;

  return newline;
}

/* cut off all the lines with the given prefix from the buffer */
static svn_stringbuf_t *
strip_prefix_from_buffer (svn_stringbuf_t *buffer,
                          const char *strip_prefix,
                          apr_pool_t *pool)
{
  /* start with a pointer to the first letter in the buffer, this is
     also on the beginning of a line */
  char *ptr = buffer->data;
  size_t strip_prefix_len = strlen (strip_prefix);

  while ( ptr && ptr < &buffer->data[buffer->len] )
    {
      char *first_prefix= ptr;

      /* First scan through all consecutive lines WITH prefix */
      while (ptr && !strncmp (ptr, strip_prefix, strip_prefix_len) )
        ptr = get_next_line (ptr);

      if (first_prefix != ptr)
        {
          /* one or more prefixed lines were found, cut them off */

          if (NULL == ptr)
            {
              /* last line, no memmove() necessary */
              buffer->len -= (&buffer->data[buffer->len] - first_prefix);
              break;
            }

          memmove (first_prefix, ptr,
                   &buffer->data [buffer->len] - ptr);

          buffer->len -= (ptr - first_prefix);

          ptr = first_prefix;
        }

      /* Now skip all consecutive lines WITHOUT prefix */
      while (ptr && strncmp (ptr, strip_prefix, strip_prefix_len) )
        ptr = get_next_line (ptr);

    }

  buffer->data[buffer->len] = 0;

  return buffer;
}

/*
 * Invoke $EDITOR to get a commit message.
 */
static svn_error_t *
message_from_editor (apr_pool_t *pool,
                     apr_array_header_t *targets,
                     svn_client_auth_baton_t *auth_baton,
                     svn_stringbuf_t *path,
                     svn_cl__opt_state_t *opt_state,
                     svn_stringbuf_t **messagep,
                     const char *editor_prefix,
                     const char *default_msg, /* text to include in editor */
                     svn_boolean_t include_status_output)
{
  char const *editor;
  apr_file_t *tempfile;
  apr_status_t rc;
  const char *fullfile;
  apr_finfo_t finfo_before;
  apr_finfo_t finfo_after;
  apr_size_t size;
  apr_size_t written;
  svn_error_t *error=NULL;
  int exitcode;
  apr_exit_why_e exitwhy;

  int i;
  svn_stringbuf_t *command;
  apr_array_header_t *array;
  const char **cmdargs;
  svn_stringbuf_t **cmdstrings;

  /* default is no returned message */
  *messagep = NULL;

  /* ### FIXME: editor-choice:
     1. the command line
     2. config file.
     3. environment variable
     4. configure-default */     

  /* try to get an editor to use */
  editor = getenv ("SVN_EDITOR");
  if(NULL == editor)
    editor = getenv ("EDITOR");
  if(NULL == editor)
    editor = getenv ("VISUAL");

  if(NULL == editor)
    {
      /* no custom editor, use built-in defaults */
      /* ### FIXME: the path should be discovered in configure */
#ifdef SVN_WIN32
      editor = "notepad.exe";
#else
      editor = "vi";
#endif
    }

  /* this sets up a new temporary file for us */
  SVN_ERR (svn_wc_create_tmp_file (&tempfile,
                                   path,
                                   FALSE, /* do *not* delete on close */
                                   pool));

  /* we need to know the name of the temporary file */
  rc = apr_file_name_get (&fullfile, tempfile);
  if (APR_SUCCESS != rc)
    {
      /* close temp file first, but we can't remove it since we can't get
         its name! */
      apr_file_close (tempfile);

      /* could not get name we can't continue */
      return svn_error_create
        (SVN_ERR_CMDLINE__TMPFILE_WRITE, 0, NULL, 
         pool,
         "Failed to write status information to temporary file.");
    }

  size = strlen (default_msg);
  rc = apr_file_write_full (tempfile, default_msg, size, &written);
  
  if ((APR_SUCCESS != rc) || written != size)
    {
      error = svn_error_create
        (SVN_ERR_CMDLINE__TMPFILE_WRITE, 0, NULL, 
         pool,
         "Failed to write stauts information to temporary file.");
    }
  else if (include_status_output)
    {
      error = write_status_to_file (pool, tempfile, editor_prefix,
                                    auth_baton, opt_state, targets);
    }

  apr_file_close (tempfile); /* we don't check return code here, since
                                we have no way to deal with errors on
                                file close */
  
  if (error)
    {
      /* we didn't manage to write the complete file, we can't fulfill
         what we're set out to do, get out */
      
      return error;
    }

  /* Get information about the temporary file before the user has
     been allowed to edit any message */
  rc = apr_stat (&finfo_before, fullfile,
                 APR_FINFO_MTIME|APR_FINFO_SIZE, pool);

  if (APR_SUCCESS != rc)
    {
      /* remove file */
      apr_file_remove (fullfile, pool);

      return svn_error_create
        (SVN_ERR_CMDLINE__TMPFILE_STAT, 0, NULL, 
         pool,
         "Failed getting info about temporary file.");
    }

  /* split the EDITOR varible into an array, we need to do this since the
     variable may contain spaces and options etc */
  command = svn_stringbuf_create (editor, pool);
  array = svn_cl__stringlist_to_array (command, pool);

  /* now we must convert the stringbuf** array to a plain char ** array,
     allocate two extra entries for the file and the trailing NULL */
  cmdargs = (const char **)apr_palloc (pool, 
                                       sizeof (char *) * (array->nelts + 2) );

  cmdstrings=(svn_stringbuf_t **)array->elts;
  for (i=0 ; i < array->nelts; i++)
    cmdargs[i] = cmdstrings[i]->data;

  /* it is important to add the file name here and not just as a part of the
     'editor' string above, as we must preserve spaces etc that this file name
     might contain */
  cmdargs[i++] = fullfile;
  cmdargs[i] = NULL;

  /* run the editor and come back when done */
  error = svn_io_run_cmd (".",
                          cmdargs[0], cmdargs,
                          &exitcode, &exitwhy,
                          TRUE,
                          NULL, NULL, NULL, /* no in, out or err files */
                          pool);

  if (error)
    {
      /* remove file */
      apr_file_remove (fullfile, pool);

      return error;
    }

  /* Get information about the message file after the assumed editing. */
  rc = apr_stat (&finfo_after, fullfile,
                 APR_FINFO_MTIME|APR_FINFO_SIZE, pool);

  if (APR_SUCCESS != rc)
    {
      /* remove file */
      apr_file_remove (fullfile, pool);

      return svn_error_create
        (SVN_ERR_CMDLINE__TMPFILE_STAT, 0, NULL, 
         pool,
         "Failed getting info about temporary file.");
    }
  
  /* Check if there seems to be any changes in the file */
  if ((finfo_before.mtime != finfo_after.mtime) ||
      (finfo_before.size != finfo_after.size))
    {
      /* The file seem to have been modified, load it and strip it */
      apr_file_t *read_file;

      /* we have a commit message in a temporary file, get it */
      rc = apr_file_open (&read_file, fullfile,
                          APR_READ, APR_UREAD, pool);
          
      if (APR_SUCCESS != rc) /* open failed */
        {
          /* This is an annoying situation, as the file seems to have
             been edited but we can't read it! */

          /* remove file */
          apr_file_remove (fullfile, pool);

          return svn_error_create
            (SVN_ERR_CMDLINE__TMPFILE_OPEN, 0, NULL, 
             pool,
             "Failed opening temporary file.");
        }
      else
        {
          svn_stringbuf_t *entirefile;

          /* read the entire file into one chunk of memory */
          SVN_ERR (svn_string_from_aprfile (&entirefile, read_file, pool));

          /* close the file */
          apr_file_close (read_file);

          /* strip prefix lines lines */
          entirefile = strip_prefix_from_buffer(entirefile,
                                                editor_prefix,
                                                pool);          
          
          /* set the return-message to the entire-file buffer */
          *messagep = entirefile;
        }
        
    }

  /* remove the temporary file */
  apr_file_remove (fullfile, pool);

  return SVN_NO_ERROR;
}

/*
 * Store the given message in a "random" file name in the given path.
 * This function also outputs this fact to stdout for users to see.
 */
static svn_error_t *
store_message (svn_stringbuf_t *message,
               svn_stringbuf_t *path,
               apr_pool_t *pool)
{
  /* Store the message in a temporary file name and display the
     file name to the user */
  apr_file_t *tempfile;
  apr_size_t written;
  apr_status_t rc;
  const char *fullfile;

  SVN_ERR (svn_wc_create_tmp_file (&tempfile,
                                   path,
                                   FALSE, /* do *not* delete on close */
                                   pool));

  /* we need to know the name of the temporary file */
  rc = apr_file_name_get (&fullfile, tempfile);

  if (APR_SUCCESS == rc)
    {
      rc = apr_file_write_full (tempfile, message->data,
                                message->len, &written);
    }

  apr_file_close (tempfile);

  if ((APR_SUCCESS == rc) && (message->len == written))
    {
      printf ("The commit message has been stored in this location:\n%s\n",
              fullfile);
    }
  else 
    {
      return svn_error_createf
        (SVN_ERR_CMDLINE__TMPFILE_WRITE, 0, NULL, 
         pool,
         "Failed writing to temporary file %s.", fullfile);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_cl__commit (apr_getopt_t *os,
                svn_cl__opt_state_t *opt_state,
                apr_pool_t *pool)
{
  apr_array_header_t *targets;
  apr_array_header_t *condensed_targets;
  svn_stringbuf_t *message;
  svn_stringbuf_t *base_dir;
  svn_stringbuf_t *cur_dir;
  svn_stringbuf_t *remainder;
  svn_stringbuf_t *trace_dir;
  const svn_delta_editor_t *trace_editor;
  void *trace_edit_baton;
  svn_client_auth_baton_t *auth_baton;
  svn_client_commit_info_t *commit_info = NULL;
  svn_error_t *err;
  svn_revnum_t revnum;
  svn_boolean_t used_editor_for_message = FALSE;

  /* Take our message from ARGV or a FILE */
  if (opt_state->filedata) 
    message = opt_state->filedata;
  else
    message = opt_state->message;

  targets = svn_cl__args_to_target_array (os, pool);

  /* Build an authentication object to give to libsvn_client. */
  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  /* Add "." if user passed 0 arguments */
  svn_cl__push_implicit_dot_target (targets, pool);

  /* Get the current working directory as an absolute path. */
  SVN_ERR (svn_path_get_absolute (&cur_dir,
                                  svn_stringbuf_create (".", pool),
                                  pool));

  /* Condense the targets (like commit does)... */
  SVN_ERR (svn_path_condense_targets (&base_dir,
                                      &condensed_targets,
                                      targets,
                                      pool));

  if ((! condensed_targets) || (! condensed_targets->nelts))
    {
      svn_stringbuf_t *parent_dir, *basename;

      SVN_ERR (svn_wc_get_actual_target (base_dir, &parent_dir, 
                                         &basename, pool));
      if (basename)
        svn_stringbuf_set (base_dir, parent_dir->data);
    }

  /* ...so we can have a common parent path to pass to the trace
     editor.  Note that what we are actually passing here is the
     difference between the absolute path of the current working
     directory and the absolute path of the common parent directory
     used in the commit (if there is a concise difference). */
  remainder = svn_path_is_child (cur_dir, base_dir, pool);
  if (remainder)
    trace_dir = remainder;
  else
    trace_dir = base_dir;

  while (NULL == message)
    {
      /* There was no commit message given anywhere in the command line,
         fire up our favourite editor to get one instead! */
#define EDITOR_PREFIX_TXT  "SVN:"
      const char editor_prefix[] = EDITOR_PREFIX_TXT;

      /* this default message might need to be configurable somehow */
      const char *default_msg= 
        "\n" EDITOR_PREFIX_TXT " ----------------------------------------------------------------------\n" 
        EDITOR_PREFIX_TXT " Enter Log.  Lines beginning with '" EDITOR_PREFIX_TXT "' are removed automatically\n" \
        EDITOR_PREFIX_TXT "\n"
        EDITOR_PREFIX_TXT " Current status of the target files and directories:\n"
        EDITOR_PREFIX_TXT "\n";

      /* no message given, try getting one from $EDITOR */
      message_from_editor (pool, targets, auth_baton,
                           base_dir, opt_state, &message,
                           editor_prefix, default_msg, TRUE);

      if (message)
        {
          /* We did get message, now check if it is anything more than just
             white space as we will consider white space only as empty */
          int len;

          for (len=message->len; len>=0; len--)
            if (!apr_isspace (message->data[len]))
              {
                break;
              }
          if (len < 0)
            message = NULL;
          else
            used_editor_for_message = TRUE;
        }

      /* message can still be NULL here if none was entered or if an
         error occurred */
      if (NULL == message)
        {
          char *reply;
          svn_cl__prompt_user (&reply,
                               "\nLog message unchanged or not specified\n"
                               "a)bort, c)ontinue, e)dit\n",
                               FALSE, /* don't hide the reply */
                               NULL, pool);
          if (reply)
            {
              char letter = apr_tolower(reply[0]);

              if('a' == letter)
                {
                  printf("*** Commit aborted!\n");
                  return SVN_NO_ERROR;
                }
              else if('c' == letter) 
                break;

              /* anything else will cause a loop and have the editor
                 restarted! */
            }
        }
    }

  SVN_ERR (svn_cl__get_trace_commit_editor (&trace_editor,
                                            &trace_edit_baton,
                                            trace_dir,
                                            pool));

  /* Get revnum set to something meaningful, to cover the xml case. */
  if (opt_state->start_revision.kind == svn_client_revision_number)
    revnum = opt_state->start_revision.value.number;
  else
    revnum = SVN_INVALID_REVNUM; /* no matter, this is fine */

  /* Commit. */
  err = svn_client_commit (&commit_info,
                           NULL, NULL,
                           opt_state->quiet ? NULL : trace_editor, 
                           opt_state->quiet ? NULL : trace_edit_baton,
                           auth_baton,
                           targets,
                           message,
                           opt_state->xml_file,
                           revnum,
                           pool);

  if (err)
    {
      /* If an editor was used and the commit failed, we store the
         edited message for the user's convenience.  This function in
         itself can of course also fail which makes it troublesome!  */
      if ((used_editor_for_message) && (message))
        store_message (message, base_dir, pool);
      return err;
    }

  if (commit_info)
    svn_cl__print_commit_info (commit_info);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
