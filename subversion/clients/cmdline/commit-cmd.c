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



/* Return a pointer to the character after the first newline in LINE,
   or NULL if there is no newline.  ### FIXME: this is not fully
   portable for other kinds of newlines! */
static char *
get_next_line (char *line)
{
  char *newline = strchr (line, '\n');
  return (newline ? newline + 1 : NULL);
}


/* Remove all lines from BUFFER that begin with PREFIX. */
static svn_stringbuf_t *
strip_prefix_from_buffer (svn_stringbuf_t *buffer,
                          const char *strip_prefix,
                          apr_pool_t *pool)
{
  /* Start with a pointer to the first letter in the buffer, this is
     also on the beginning of a line */
  char *ptr = buffer->data;
  size_t strip_prefix_len = strlen (strip_prefix);

  while (ptr && (ptr < (buffer->data + buffer->len)))
    {
      char *first_prefix = ptr;

      /* First scan through all consecutive lines WITH prefix. */
      while (ptr && (! strncmp (ptr, strip_prefix, strip_prefix_len)))
        ptr = get_next_line (ptr);

      if (first_prefix != ptr)
        {
          /* One or more prefixed lines were found, cut them off. */
          if (! ptr)
            {
              /* This is the last line, no memmove() is necessary. */
              buffer->len -= (buffer->data + buffer->len - first_prefix);
              break;
            }

          /* Shift over the rest of the buffer to cover up the
             stripped prefix'ed line. */
          memmove (first_prefix, ptr, buffer->data + buffer->len - ptr);
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


struct log_msg_baton
{
  svn_stringbuf_t *msg;
  svn_stringbuf_t *base_dir;
};


/* This function is of type svn_client_get_commit_log_t. */
static svn_error_t *
get_log_msg (svn_stringbuf_t **log_msg,
             apr_array_header_t *commit_items,
             void *baton,
             apr_pool_t *pool)
{
#define EDITOR_PREFIX_TXT  "SVN:"
  const char *default_msg = "\n"
    EDITOR_PREFIX_TXT 
    " ---------------------------------------------------------------------\n" 
    EDITOR_PREFIX_TXT " Enter Log.  Lines beginning with '" 
                             EDITOR_PREFIX_TXT "' are removed automatically\n"
    EDITOR_PREFIX_TXT "\n"
    EDITOR_PREFIX_TXT " Current status of the target files and directories:\n"
    EDITOR_PREFIX_TXT "\n";
  struct log_msg_baton *lmb = baton;
  svn_stringbuf_t *message = NULL;

  if (lmb->msg)
    {
      *log_msg = svn_stringbuf_dup (lmb->msg, pool);
      return SVN_NO_ERROR;
    }

  if (! (commit_items || commit_items->nelts))
    {
      *log_msg = svn_stringbuf_create ("", pool);
      return SVN_NO_ERROR;
    }

  while (! message)
    {
      /* We still don't have a valid commit message.  Use $EDITOR to
         get one. */
      int i;
      svn_stringbuf_t *tmp_message = svn_stringbuf_create (default_msg, pool);
      svn_string_t tmp_str;

      for (i = 0; i < commit_items->nelts; i++)
        {
          svn_client_commit_item_t *item
            = ((svn_client_commit_item_t **) commit_items->elts)[i];
          svn_stringbuf_t *path = item->path;
          char text_mod = 'M', prop_mod = ' ';

          if (! path)
            path = item->url;

          if ((item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
              && (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD))
            text_mod = 'R';
          else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
            text_mod = 'A';
          else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
            text_mod = 'D';

          if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
            prop_mod = '_';

          svn_stringbuf_appendcstr (tmp_message, EDITOR_PREFIX_TXT);
          svn_stringbuf_appendcstr (tmp_message, "   ");
          svn_stringbuf_appendbytes (tmp_message, &text_mod, 1); 
          svn_stringbuf_appendbytes (tmp_message, &prop_mod, 1); 
          svn_stringbuf_appendcstr (tmp_message, "   ");
          svn_stringbuf_appendcstr (tmp_message, path->data);
          svn_stringbuf_appendcstr (tmp_message, "\n");
        }

      tmp_str.data = tmp_message->data;
      tmp_str.len = tmp_message->len;
      SVN_ERR (svn_cl__edit_externally (&message, lmb->base_dir, 
                                        &tmp_str, pool));

      /* Strip the prefix from the buffer. */
      if (message)
        message = strip_prefix_from_buffer (message, EDITOR_PREFIX_TXT, pool);

      if (message)
        {
          /* We did get message, now check if it is anything more than just
             white space as we will consider white space only as empty */
          int len;

          for (len = message->len; len >= 0; len--)
            {
              if (! apr_isspace (message->data[len]))
                break;
            }
          if (len < 0)
            message = NULL;
        }

      if (! message)
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
                  printf ("*** Commit aborted!\n");
                  return SVN_NO_ERROR;
                }
              else if ('c' == letter) 
                break;

              /* anything else will cause a loop and have the editor
                 restarted! */
            }
        }
    }

  if (message)
    *log_msg = svn_stringbuf_dup (message, pool);
  else
    *log_msg = NULL;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__commit (apr_getopt_t *os,
                svn_cl__opt_state_t *opt_state,
                apr_pool_t *pool)
{
  apr_array_header_t *targets;
  apr_array_header_t *condensed_targets;
  svn_stringbuf_t *base_dir;
  const svn_delta_editor_t *trace_editor;
  void *trace_edit_baton;
  svn_client_auth_baton_t *auth_baton;
  svn_client_commit_info_t *commit_info = NULL;
  svn_revnum_t revnum;
  struct log_msg_baton *lmb = apr_pcalloc (pool, sizeof (*lmb));
    
  targets = svn_cl__args_to_target_array (os, opt_state, pool);

  /* Build an authentication object to give to libsvn_client. */
  auth_baton = svn_cl__make_auth_baton (opt_state, pool);

  /* Add "." if user passed 0 arguments. */
  svn_cl__push_implicit_dot_target (targets, pool);

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

  /* Get a trace commit editor. */
  SVN_ERR (svn_cl__get_trace_commit_editor (&trace_editor,
                                            &trace_edit_baton,
                                            NULL,
                                            pool));

  /* Get revnum set to something meaningful, to cover the xml case. */
  if (opt_state->start_revision.kind == svn_client_revision_number)
    revnum = opt_state->start_revision.value.number;
  else
    revnum = SVN_INVALID_REVNUM; /* no matter, this is fine */

  /* Initialize our log message baton. */
  if (opt_state->filedata) 
    lmb->msg = opt_state->filedata;
  else
    lmb->msg = opt_state->message;
  lmb->base_dir = base_dir;

  /* Commit. */
  SVN_ERR (svn_client_commit (&commit_info,
                              NULL, NULL,
                              opt_state->quiet ? NULL : trace_editor, 
                              opt_state->quiet ? NULL : trace_edit_baton,
                              auth_baton,
                              targets,
                              &get_log_msg,
                              lmb,
                              opt_state->xml_file,
                              revnum,
                              pool));
  if (commit_info)
    svn_cl__print_commit_info (commit_info);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
