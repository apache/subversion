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


/*** Code. ***/


/* For each item in STATUSHASH (a hash of svn_wc_status_t * items),
   append a short descriptive status output regarding that item to
   *OUTSTR.  Use POOL for all allocations. */
static void
append_statuses (svn_stringbuf_t **outstr,
                 apr_hash_t *statushash, 
                 apr_pool_t *pool)
{
  int i;
  apr_array_header_t *statusarray;
  svn_wc_status_t *status = NULL;

  if (! outstr)
    return;

  if (! *outstr)
    *outstr = svn_stringbuf_create ("", pool);

  /* Convert the unordered hash to an ordered, sorted array */
  statusarray = apr_hash_sorted_keys (statushash,
                                      svn_sort_compare_items_as_paths,
                                      pool);

  /* Loop over array, printing each name/status-structure */
  for (i = 0; i < statusarray->nelts; i++)
    {
      const svn_item_t *item;
      char str_status[5];
      char array[128];
      const char *path;

      /* Get the item from the sorted array. */
      item = &APR_ARRAY_IDX (statusarray, i, const svn_item_t);
      status = item->value;
      path = item->key;

      /* Ignore unversioned things. */
      if (status->text_status == svn_wc_status_unversioned)
        continue;

      /* Create local-mod status code block. */
      svn_cl__generate_status_codes (str_status,
                                     status->text_status,
                                     status->prop_status,
                                     status->locked,
                                     status->copied);
  
      /* Write status codes out to FILE. */
      apr_snprintf (array, 128, "SVN:   %s   %s\n", str_status, path);
      svn_stringbuf_appendcstr (*outstr, array);
    }
}


/* Create the default log message contents, and return it in *OUTSTR.
   Part of this involves gathering status output about TARGETS.  Use
   POOL for all necessary allocations. */
static svn_error_t *
init_log_contents (svn_stringbuf_t **outstr,
                   svn_client_auth_baton_t *auth_baton,
                   svn_cl__opt_state_t *opt_state,
                   apr_array_header_t *targets,
                   apr_pool_t *pool)
{
#define DEFAULT_MSG \
"\n"\
"SVN: ---------------------------------------------------------------------\n"\
"SVN: Enter Log.  Lines beginning with 'SVN:' are removed automatically\n"\
"SVN: \n"\
"SVN: Current status of the target files and directories:\n"\
"SVN: \n"

  apr_hash_t *statushash;
  svn_revnum_t youngest = SVN_INVALID_REVNUM;
  int i;
  const char *default_msg = DEFAULT_MSG;

  if (! outstr)
    return SVN_NO_ERROR;

  if (! *outstr)
    *outstr = svn_stringbuf_create ("", pool);
 
  /* Add the default message to the output. */
  svn_stringbuf_appendcstr (*outstr, default_msg);

  for (i = 0; i < targets->nelts; i++)
    {
      svn_stringbuf_t *target = ((svn_stringbuf_t **) (targets->elts))[i];

      /* Retrieve a hash of status structures with the information
         requested by the user.  svn_client_status directly
         understands three commandline switches (-n, -u, -[vV]) : */
      SVN_ERR (svn_client_status (&statushash, &youngest, target, auth_baton,
                                  opt_state->nonrecursive ? 0 : 1,
                                  opt_state->verbose,
                                  FALSE, /* no update */
                                  pool));

      /* Now append the structures to the output string.  */
      append_statuses (outstr, statushash, pool);
    }

  return SVN_NO_ERROR;
}



/* Strip out lines beginning with SVN: from LOG_MSG. */
static void
strip_ignored_lines (svn_stringbuf_t *log_msg)
{
  char *ptr = log_msg->data;
  char *prefix;

  do
    {
      if ((prefix = strstr (ptr, "SVN:")))
        {
          /* We found an instance of "SVN:" */

          /* Is this instance the first bit of data in the file?  Or
             is it preceded by a newline character? */
          if ((prefix == log_msg->data) ||
              ((prefix[-1] == '\n') || (prefix[-1] == '\r')))
            {
              /* This instance of "SVN:" is the start of a line. */

              /* Find the end of this line.  ### todo: This needs a
                 little better intelligence since a LF is not the
                 end-of-line on every imaginable system .*/
              char *eol = strchr (prefix, '\n');
              size_t linelen;

              if (! eol)
                {
                  /* This is the last line, so just hack it off. */
                  linelen = strlen (prefix);
                  log_msg->len -= linelen;
                  log_msg->data[log_msg->len] = 0;
                  break;
                }

              /* Increment EOL past the ... EOL. */
              eol++; 

              /* The line about to get removed measures from 'prefix'
                 to 'eol'. */
              linelen = eol - prefix;

              /* Shift the remaining data "left" by the length of the
                 line we're removing. */
              memmove (prefix, eol,
                       log_msg->len - (prefix - log_msg->data) - linelen);
              log_msg->len -= linelen;
              log_msg->data[log_msg->len] = 0;

              /* Continue searching from here. */
              ptr = prefix;
            }
          else
            {
              /* Substring found, but not on the first column of a
                 line.  Just continue from here.  */
              ptr = prefix + 1;
            }
        }
    }
  while (prefix);
}



/* Write out the contents of MESSAGE to a temporary file (based on
   PATH), and use stdout to tell the user where you wrote the data.
   Use POOL for all allocations. */
static svn_error_t *
store_message (svn_stringbuf_t *message,
               svn_stringbuf_t *path,
               apr_pool_t *pool)
{
  apr_file_t *tempfile;
  apr_size_t size;
  apr_status_t apr_err;
  const char *fullfile;

  /* Create a temporary file, and ask APR for its name.  */
  SVN_ERR (svn_wc_create_tmp_file (&tempfile, path, FALSE, pool));
  apr_file_name_get (&fullfile, tempfile);

  size = message->len;
  apr_err = apr_file_write (tempfile, message->data, &size);
  apr_file_close (tempfile);

  if (! apr_err)
    {
      printf ("The commit message has been stored in this location:\n%s\n",
              fullfile);
    }
  else 
    {
      /* ### todo: Return a proper error message here. */
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
query_for_log_message (svn_stringbuf_t **message,
                       svn_boolean_t *abort_commit,
                       svn_stringbuf_t *base_dir,
                       svn_client_auth_baton_t *auth_baton,
                       svn_cl__opt_state_t *opt_state,
                       apr_array_header_t *targets,
                       apr_pool_t *pool)
{
  svn_stringbuf_t *default_message = NULL;
  svn_string_t contents;

  *message = NULL;
  *abort_commit = FALSE;
  
  /* Get the default message. */
  SVN_ERR (init_log_contents (&default_message, auth_baton, opt_state, 
                              targets, pool));

  /* If we couldn't generate a default message, but no errors occured,
     just ... um ... go with an empty message, yeah! */
  if (! default_message)
    default_message = svn_stringbuf_create ("", pool);

  /* Convert to a regular string. */
  contents.data = default_message->data;
  contents.len = default_message->len;

  while (! *message)
    {
      svn_stringbuf_t *editor_message = NULL;
      
      /* There was no commit message given anywhere in the command line,
         fire up our favourite editor to get one instead! */

      /* Now, edit the message. */
      SVN_ERR (svn_cl__edit_externally (&editor_message,
                                        base_dir,
                                        &contents,
                                        pool));

      if (editor_message)
        {
          /* We did get message, now check if it is anything more than
             just white space as we will consider white space only as
             empty.  */
          int len;

          /* Strip off the lines we don't care about. */
          strip_ignored_lines (editor_message);

          for (len = editor_message->len; len >= 0; len--)
            {
              if (!apr_isspace (editor_message->data[len]))
                break;
            }

          if (len >= 0)
            *message = editor_message;
        }

      /* message can still be NULL here if none was entered or if an
         error occurred */
      if (! *message)
        {
          char *reply;
          svn_cl__prompt_user (&reply,
                               "\nLog message unchanged or not specified\n"
                               "a)bort, c)ontinue, e)dit\n",
                               FALSE, /* don't hide the reply */
                               NULL, pool);
          if (reply)
            {
              char letter = apr_tolower (reply[0]);

              if ('a' == letter)
                {
                  *abort_commit = TRUE;
                  return SVN_NO_ERROR;
                }
              else if ('c' == letter) 
                break;

              /* Anything else will cause a loop and have the editor
                 restarted! */
            }
        }
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
  const svn_delta_edit_fns_t *trace_editor;
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

  if (! message)
    {
      svn_boolean_t abort_commit = FALSE;
      
      used_editor_for_message = TRUE;
      SVN_ERR (query_for_log_message (&message, &abort_commit,
                                      base_dir, auth_baton, opt_state,
                                      targets, pool));
      if (abort_commit)
        {
          printf ("*** Commit aborted.\n");
          return SVN_NO_ERROR;
        }
    }

  SVN_ERR (svn_cl__get_trace_commit_editor 
           (&trace_editor,
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
