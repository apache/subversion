/*
 * commit-cmd.c -- Check changes into the repository.
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


/*** Code. ***/

/*
 * Prints a single status line to a given file about the given entry.
 */
static void 
print_short_format (apr_file_t *file,
                    const char *path,
                    svn_wc_status_t *status)
{
  char str_status[5];
  char array[128];
  apr_size_t size;

  if (! status)
    return;

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

      apr_snprintf(array, 128, "SVN:   %s   %s\n", str_status, path);

      size = strlen(array);

      apr_file_write(file, array, &size);
    }
}

/*
 * Walks throught the 'nelts' of the given hash and calls the status-
 * print function for each.
 */
void static
print_status (apr_file_t *file,
              apr_hash_t *statushash, 
              apr_pool_t *pool)
{
  int i;
  apr_array_header_t *statusarray;
  svn_wc_status_t *status = NULL;

  /* Convert the unordered hash to an ordered, sorted array */
  statusarray = apr_hash_sorted_keys (statushash,
                                      svn_sort_compare_items_as_paths,
                                      pool);

  /* Loop over array, printing each name/status-structure */
  for (i = 0; i < statusarray->nelts; i++)
    {
      const svn_item_t *item;

      item = &APR_ARRAY_IDX(statusarray, i, const svn_item_t);
      status = item->value;

      print_short_format (file, item->key, status);
    }
}

/*
 * This function gather a status output to be used within a commit message,
 * possibly edited in your favourite $EDITOR.
 */
static svn_error_t *
write_status_to_file(apr_pool_t *pool,
                     apr_file_t *file,
                     svn_cl__opt_state_t *opt_state,
                     apr_array_header_t *targets)
{
  apr_hash_t *statushash;
  svn_client_auth_baton_t *auth_baton;
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
                                  opt_state->nonrecursive ? 0 : 1,
                                  opt_state->verbose,
                                  FALSE, /* no update */
                                  pool));

      /* Now print the structures to the screen.
         The flag we pass indicates whether to use the 'detailed'
         output format or not. */
      print_status (file,
                    statushash, 
                    pool);
    }

  return SVN_NO_ERROR;
}

/*
 * Invoke $EDITOR to get a commit message.
 */
static svn_error_t *
message_from_editor(apr_pool_t *pool,
                    apr_array_header_t *targets,
                    svn_stringbuf_t *path,
                    svn_cl__opt_state_t *opt_state,
                    svn_stringbuf_t **messagep )
{
  char const *editor;
  char *command;
  size_t editorlen;
  apr_file_t *tempfile;
  apr_status_t rc;
  const char *fullfile;

  /* default is no returned message */
  *messagep = NULL;

  /* try to get an editor to use */
  editor = getenv ("SVN_EDITOR");
  if(NULL == editor)
    editor = getenv ("EDITOR");
  if(NULL == editor)
    editor = getenv ("VISUAL");

  if(NULL == editor)
    {
      /* no custom editor, use built-in defaults */
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
  apr_file_name_get (&fullfile, tempfile);

  editorlen = strlen (editor);

  command = (char *)malloc (editorlen + strlen(fullfile) + 2);

#define DEFAULT_MSG \
"\nSVN: ----------------------------------------------------------------------\n" \
"SVN: Enter Log.  Lines beginning with 'SVN:' are removed automatically\n" \
"SVN: \n" \
"SVN: Current status of the target files and directories:\n"\
"SVN: \n"

  if (command)
    {
      apr_finfo_t finfo_before;
      apr_finfo_t finfo_after;
      apr_size_t size, written;

      size = strlen (DEFAULT_MSG);

      rc = apr_file_write_full (tempfile, DEFAULT_MSG, size, &written);

      write_status_to_file (pool, tempfile, opt_state, targets);

      apr_file_close (tempfile);

      /* we didn't manage to write the complete file, we can't fulfill
         what we're set out to do, get out */
      /* ### FIXME: The documentation for apr_file_full_write()
         doesn't explicitly promise that if size != written, then
         there *must* be an error returned, so below we handle the two
         cases separately.  But a glance at apr_file_full_write's 
         implementation, on Unix at least, shows that it could
         document that promise.  Maybe we should fix the doc in APR,
         and just check rc below?  */
      if (! APR_STATUS_IS_SUCCESS (rc))
        {
          return svn_error_createf
            (rc, 0, NULL, pool, "Trouble writing `%s'", fullfile);
        }
      else if (written != size)
        {
          /* ### FIXME: this error code may change when there is a
             general need to revamp the client's error code system. */
          return svn_error_createf
            (SVN_ERR_INCOMPLETE_DATA,
             0, NULL, pool, "Failed to completely write `%s'", fullfile);
        }

      /* Get information about the temporary file before the user has
         been allowed to edit any message */
      apr_stat (&finfo_before, fullfile,
                APR_FINFO_MTIME|APR_FINFO_SIZE, pool);
  
      /* create the command line */
      apr_snprintf (command, editorlen + strlen(fullfile) + 2,
                   "%s %s", editor, fullfile);
      /* run the editor command line */
      system (command);

      /* Get information about the message file after the assumed editing. */
      apr_stat (&finfo_after, fullfile,
                APR_FINFO_MTIME|APR_FINFO_SIZE, pool);
      
      /* Check if there seems to be any changes in the file */
      if((finfo_before.mtime == finfo_after.mtime) &&
         (finfo_before.size == finfo_after.size))
        {
          /* The file doesn't seem to have been modified, no
             need to load it and strip it and such */
        }
      else {
        apr_file_t *read_file;

        /* we have a commit message in a temporary file, get it */
        rc = apr_file_open (&read_file, fullfile,
                            APR_READ, APR_UREAD, pool);

        if(APR_SUCCESS != rc) /* open failed */
          {
            /* This is an annoying situation, as the file seems to have
               been edited but we can't read it! */
          }
        else
          {
            /* read the entire file into one chunk of memory */
            char readbuffer[1024];
            svn_stringbuf_t *entirefile;
            char *ptr;
            char *prefix;
            
            /* create a buffer */
            entirefile = svn_stringbuf_ncreate ("", 0, pool);

            do
              {
                size = sizeof (readbuffer);
                apr_file_read (read_file, readbuffer, &size);

                /* append chunk to the entirefile string */
                svn_stringbuf_appendbytes (entirefile, readbuffer, size);

                if( size != sizeof (readbuffer))
                  {
                    /* partly filled buffer, this is the end */
                    break; /* out of loop */
                  }
              }
            while(1);

            /* close the file */
            apr_file_close (read_file);

            /* a full chunk was read, now strip all the SVN: lines, but
               nothing else */
            ptr = entirefile->data;

            do
              {
                prefix=strstr (ptr, "SVN:");

                if(prefix)
                  {
                    /* substring found */

                    if( (prefix == entirefile->data) ||
                        ((prefix[-1] == '\n') || (prefix[-1] == '\r')))
                      {
                        /* it is on the start of a line */

                        /* Figure out the end of the line. This needs a little
                           better intelligence since a LF is not the
                           end-of-line on every imaginable system .*/
                        char *eol= strchr(prefix, '\n');
                        size_t linelen;

                        if(NULL == eol)
                          {
                            /* last line, we just make the buffer shorter,
                               no need to move around any data */
                            linelen = strlen(prefix);

                            entirefile->len -= linelen;

                            /* set a new zero terminator */
                            entirefile->data [ entirefile->len] = 0;

                            break;
                          }

                        eol++; /* eol now points to the first character
                                  beyond */

                        /* this line that is about to get cut off measures
                           from 'prefix' to 'eol' */
                        linelen = eol-prefix;

                        /* move the rest of the chunk over this line that
                           shouldn't be a part of the final message */
                        memmove (prefix, eol,
                                 entirefile->len - (prefix-entirefile->data) -
                                 linelen);

                        /* decrease total message size */
                        entirefile->len -= linelen;

                        /* set a new zero terminator */
                        entirefile->data [ entirefile->len] = 0;

                        /* continue searching from here */
                        ptr = prefix;
                      }
                    else
                      {
                        /* substring found but not on the first column of
                           a line, just continue from here */
                        ptr = prefix+1;
                      }
                  }
              }
            while(prefix);

            /* set the return-message to the entire-file buffer */
            *messagep = entirefile;
          }
        
      }

      /* free the memory allocated for the command line here */
      free (command);
    }
  else
    {
      /* major memory problem */
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
store_message(svn_stringbuf_t *message,
              svn_stringbuf_t *path,
              apr_pool_t *pool)
{
  /* Store the message in a temporary file name and display the
     file name to the user */
  apr_file_t *tempfile;
  apr_size_t size;
  apr_status_t rc;
  const char *fullfile;

  SVN_ERR (svn_wc_create_tmp_file (&tempfile,
                                   path,
                                   FALSE, /* do *not* delete on close */
                                   pool));

  /* we need to know the name of the temporary file */
  apr_file_name_get (&fullfile, tempfile);

  size = message->len;
  rc = apr_file_write (tempfile, message->data, &size);

  apr_file_close(tempfile);

  if (APR_SUCCESS == rc)
    {
      printf("The commit message has been stored in this location:\n%s\n",
             fullfile);
    }
  else 
    {
      /* FIX! return a proper error message here */
    }

  return NULL;
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
  const svn_delta_edit_fns_t *trace_editor;
  void *trace_edit_baton;
  svn_client_auth_baton_t *auth_baton;
  svn_client_commit_info_t *commit_info = NULL;
  svn_stringbuf_t *messagep=NULL;
  svn_error_t *error;

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

      /* no message given, try getting one from $EDITOR */
      message_from_editor (pool, targets, base_dir, opt_state, &messagep);

      if(messagep)
        {
          /* We did get message, now check if it is anything more than just
             white space as we will consider white space only as empty */
          int len;

          for (len=messagep->len; len>=0; len--)
              if(!apr_isspace (messagep->data[len]))
                break;
          if (len >= 0)
            /* there was something else besides space */
            message = messagep;
        }

      /* message can still be NULL here if none was entered or if an
         error occurred */
      if(NULL == message)
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

  SVN_ERR (svn_cl__get_trace_commit_editor 
           (&trace_editor,
            &trace_edit_baton,
            trace_dir,
            pool));

  /* Commit. */
  error = svn_client_commit (&commit_info,
                             NULL, NULL,
                             opt_state->quiet ? NULL : trace_editor, 
                             opt_state->quiet ? NULL : trace_edit_baton,
                             auth_baton,
                             targets,
                             message,
                             opt_state->xml_file,
                             opt_state->start_revision,
                             pool);

  if (error)
    {
      if (messagep)
        /* An editor was used and the commit failed, we store the 
           edited message for the user's convenience. This function in
           itself can of course also fail which makes it troublesome! */
        store_message(message, base_dir, pool);

      return error;
    }


  if (commit_info)
    svn_cl__print_commit_info (commit_info);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end: 
 */
