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
#include "svn_pools.h"
#include "svn_utf.h"
#include "svn_subst.h"
#include "svn_config.h"
#include "cl.h"




void
svn_cl__print_commit_info (svn_client_commit_info_t *commit_info)
{
  if ((commit_info) 
      && (SVN_IS_VALID_REVNUM (commit_info->revision)))
    printf ("\nCommitted revision %" SVN_REVNUM_T_FMT ".\n",
            commit_info->revision);

  return;
}


svn_error_t *
svn_cl__edit_externally (const char **edited_contents /* UTF-8! */,
                         const char **tmpfile_left /* UTF-8! */,
                         const char *base_dir /* UTF-8! */,
                         const char *contents /* UTF-8! */,
                         const char *prefix,
                         apr_pool_t *pool)
{
  const char *editor = NULL;
  const char *cmd;
  apr_file_t *tmp_file;
  const char *tmpfile_name;
  const char *contents_native, *tmpfile_native, *base_dir_native;
  apr_status_t apr_err, apr_err2;
  apr_size_t written;
  apr_finfo_t finfo_before, finfo_after;
  svn_error_t *err = SVN_NO_ERROR, *err2;
  char *old_cwd;
  int sys_err;
  svn_boolean_t remove_file = TRUE;
  struct svn_config_t *cfg;

  /* ### TODO: This function should, if it fails to create a temporary
     file in the requested directory, fall back to the use of a
     temporary directory (like /tmp, or C:\TEMP, or ...) as described
     by a not-yet-existant APR function.  See issue #929. */

     
  /* Try to find an editor in the environment. */
  editor = getenv ("SVN_EDITOR");
  if (! editor)
    editor = getenv ("VISUAL");
  if (! editor)
    editor = getenv ("EDITOR");
  
  /* Now, override this editor choice with a selection from our config
     file (using what we have found thus far as the default in case no
     config option exists). */
  SVN_ERR (svn_config_read_config (&cfg, pool));
  svn_config_get (cfg, &editor, "helpers", "editor-cmd", editor);

  /* Abort if there is no editor specified */
  if (! editor)
    return svn_error_create 
      (SVN_ERR_CL_NO_EXTERNAL_EDITOR, 0, NULL,
       "None of the environment variables "
       "SVN_EDITOR, VISUAL or EDITOR is set.");

  /* Convert file contents from UTF-8 */
  SVN_ERR (svn_utf_cstring_from_utf8 (&contents_native, contents, pool));

  /* Move to BASE_DIR to avoid getting characters that need quoting
     into tmpfile_name */
  apr_err = apr_filepath_get (&old_cwd, APR_FILEPATH_NATIVE, pool);
  if (apr_err)
    {
      return svn_error_create
        (apr_err, 0, NULL, "failed to get current working directory");
    }
  SVN_ERR (svn_utf_cstring_from_utf8 (&base_dir_native, base_dir, pool));

  /* APR doesn't like "" directories */
  if (base_dir_native[0] == '\0')
    base_dir_native = ".";
  apr_err = apr_filepath_set (base_dir_native, pool);
  if (apr_err)
    {
      return svn_error_createf
        (apr_err, 0, NULL,
         "failed to change working directory to '%s'", base_dir);
    }

  /*** From here on, any problems that occur require us to cd back!! ***/

  /* Ask the working copy for a temporary file that starts with
     PREFIX. */
  err = svn_io_open_unique_file (&tmp_file, &tmpfile_name,
                                 prefix, ".tmp", FALSE, pool);
  if (err)
    goto cleanup2;

  /*** From here on, any problems that occur require us to cleanup
       the file we just created!! ***/

  /* Dump initial CONTENTS to TMP_FILE. */
  apr_err = apr_file_write_full (tmp_file, contents_native, 
                                 strlen (contents_native), &written);

  apr_err2 = apr_file_close (tmp_file);
  if (! apr_err)
    apr_err = apr_err2;
  
  /* Make sure the whole CONTENTS were written, else return an error. */
  if (apr_err || (written != strlen (contents_native)))
    {
      err = svn_error_createf
        (apr_err ? apr_err : SVN_ERR_INCOMPLETE_DATA, 0, NULL,
         "failed writing '%s'", tmpfile_name);
      goto cleanup;
    }

  err = svn_utf_cstring_from_utf8 (&tmpfile_native, tmpfile_name, pool);
  if (err)
    goto cleanup;

  /* Get information about the temporary file before the user has
     been allowed to edit its contents. */
  apr_err = apr_stat (&finfo_before, tmpfile_native, 
                      APR_FINFO_MTIME | APR_FINFO_SIZE, pool);
  if (apr_err)
    {
      err =  svn_error_createf (apr_err, 0, NULL,
                                "failed to stat '%s'", tmpfile_name);
      goto cleanup;
    }

  /* Now, run the editor command line.  */
  cmd = apr_psprintf (pool, "%s %s", editor, tmpfile_native);
  sys_err = system (cmd);
  if (sys_err != 0)
    {
      /* Extracting any meaning from sys_err is platform specific, so just
         use the raw value. */
      err =  svn_error_createf (SVN_ERR_EXTERNAL_PROGRAM, 0, NULL,
                                "system('%s') returned %d", cmd, sys_err);
      goto cleanup;
    }
  
  /* Get information about the temporary file after the assumed editing. */
  apr_err = apr_stat (&finfo_after, tmpfile_native, 
                      APR_FINFO_MTIME | APR_FINFO_SIZE, pool);
  if (apr_err)
    {
      err = svn_error_createf (apr_err, 0, NULL,
                               "failed to stat '%s'", tmpfile_name);
      goto cleanup;
    }
  
  /* If the file looks changed... */
  if ((finfo_before.mtime != finfo_after.mtime) ||
      (finfo_before.size != finfo_after.size))
    {
      svn_stringbuf_t *edited_contents_s;
      err = svn_stringbuf_from_file (&edited_contents_s, tmpfile_name, pool);
      if (err)
        goto cleanup;

      *edited_contents = edited_contents_s->data;
    }
  else
    {
      /* No edits seem to have been made */
      *edited_contents = NULL;
    }

  /* If the caller wants us to leave the file around, return the path
     of the file we used, and make a note not to destroy it.  */
  if (tmpfile_left)
    {
      *tmpfile_left = svn_path_join (base_dir, tmpfile_name, pool);
      remove_file = FALSE;
    }
  
 cleanup:
  if (remove_file)
    {
      /* Remove the file from disk.  */
      err2 = svn_io_remove_file (tmpfile_name, pool);

      /* Only report remove error if there was no previous error. */
      if (! err && err2)
        err = err2;
    }

 cleanup2:
  /* If we against all probability can't cd back, all further relative
     file references would be screwed up, so we have to abort. */
  apr_err = apr_filepath_set (old_cwd, pool);
  if (apr_err)
    {
      svn_handle_error (svn_error_create
                        (apr_err, 0, NULL,
                         "failed to restore current working directory"),
                        stderr, TRUE /* fatal */);
    }

  return err;
}


struct log_msg_baton
{
  const char *message;  /* the message. */
  const char *message_encoding; /* the locale/encoding of the message. */
  const char *base_dir; /* the base directory for an external edit. UTF-8! */
  const char *tmpfile_left; /* the tmpfile left by an external edit. UTF-8! */
  apr_pool_t *pool; /* a pool. */
};


void *
svn_cl__make_log_msg_baton (svn_cl__opt_state_t *opt_state,
                            const char *base_dir /* UTF-8! */,
                            apr_pool_t *pool)
{
  struct log_msg_baton *baton = apr_palloc (pool, sizeof (*baton));

  if (opt_state->filedata) 
    baton->message = opt_state->filedata->data;
  else
    baton->message = opt_state->message;
  baton->message_encoding = opt_state->encoding;
  baton->base_dir = base_dir ? base_dir : "";
  baton->tmpfile_left = NULL;
  baton->pool = pool;
  return baton;
}


svn_error_t *
svn_cl__cleanup_log_msg (void *log_msg_baton,
                         svn_error_t *commit_err)
{
  struct log_msg_baton *lmb = log_msg_baton;
  const char *native;
  svn_error_t *conv_err;

  /* If there was no tmpfile left, or there is no log message baton,
     return COMMIT_ERR. */
  if ((! lmb) || (! lmb->tmpfile_left))
    return commit_err;

  /* If there was no commit error, cleanup the tmpfile and return. */
  if (! commit_err)
    return svn_io_remove_file (lmb->tmpfile_left, lmb->pool);

  /* There was a commit error; there is a tmpfile.  Leave the tmpfile
     around, and add message about its presence to the commit error
     chain.  Then return COMMIT_ERR.  If the conversion from UTF-8 to
     native encoding fails, we have to compose that error with the
     commit error chain, too. */
  conv_err = svn_utf_cstring_from_utf8 (&native, lmb->tmpfile_left, lmb->pool);
  svn_error_compose (commit_err, svn_error_createf 
                     (commit_err->apr_err, 0, conv_err,
                      "Your commit message was left in a temporary file:\n"
                      "   %s", 
                      conv_err ? "(see the following error)" : native));
  return commit_err;
}


/* Remove line-starting PREFIX and everything after it from BUFFER.
   If NEW_LEN is non-NULL, return the new length of BUFFER in
   *NEW_LEN.  */
static void
truncate_buffer_at_prefix (apr_size_t *new_len,
                           char *buffer,
                           const char *prefix)
{
  char *substring = buffer;

  assert (buffer && prefix);

  /* Initialize *NEW_LEN. */
  if (new_len)
    *new_len = strlen (buffer);

  while (1)
    {
      /* Find PREFIX in BUFFER. */
      substring = strstr (substring, prefix);
      if (! substring)
        return;

      /* We found PREFIX.  Is it really a PREFIX?  Well, if it's the first
         thing in the file, or if the character before it is a
         line-terminator character, it sure is. */
      if ((substring == buffer)
          || (*(substring - 1) == '\r')
          || (*(substring - 1) == '\n'))
        {
          *substring = '\0';
          if (new_len)
            *new_len = substring - buffer;
        }
      else if (substring)
        {
          /* Well, it wasn't really a prefix, so just advance by 1
             character and continue. */
          substring++;
        }
    }

  return;
}


#define EDITOR_EOF_PREFIX  "--This line, and those below, will be ignored--"

/* This function is of type svn_client_get_commit_log_t. */
svn_error_t *
svn_cl__get_log_message (const char **log_msg,
                         const char **tmp_file,
                         apr_array_header_t *commit_items,
                         void *baton,
                         apr_pool_t *pool)
{
  const char *default_msg = "\n" EDITOR_EOF_PREFIX "\n\n";
  struct log_msg_baton *lmb = baton;
  svn_stringbuf_t *message = NULL;
  
  *tmp_file = NULL;
  if (lmb->message)
    {
      svn_string_t *log_msg_string = svn_string_create ("", pool);

      log_msg_string->data = lmb->message;
      log_msg_string->len = strlen (lmb->message);

      SVN_ERR (svn_subst_translate_string (&log_msg_string, log_msg_string,
                                           lmb->message_encoding, pool));

      *log_msg = log_msg_string->data;

      /* Trim incoming messages the EOF marker text and the junk that
         follows it.  */
      truncate_buffer_at_prefix (NULL, (char*)*log_msg, EDITOR_EOF_PREFIX);

      return SVN_NO_ERROR;
    }

  if (! (commit_items || commit_items->nelts))
    {
      *log_msg = "";
      return SVN_NO_ERROR;
    }

  while (! message)
    {
      /* We still don't have a valid commit message.  Use $EDITOR to
         get one.  Note that svn_cl__edit_externally will still return
         a UTF-8'ized log message. */
      int i;
      svn_stringbuf_t *tmp_message = svn_stringbuf_create (default_msg, pool);
      svn_error_t *err = NULL;
      const char *msg2 = NULL;  /* ### shim for svn_cl__edit_externally */

      for (i = 0; i < commit_items->nelts; i++)
        {
          svn_client_commit_item_t *item
            = ((svn_client_commit_item_t **) commit_items->elts)[i];
          const char *path = item->path;
          char text_mod = '_', prop_mod = ' ';

          if (! path)
            path = item->url;
          else if (! *path)
            path = ".";

          if ((item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
              && (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD))
            text_mod = 'R';
          else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
            text_mod = 'A';
          else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
            text_mod = 'D';
          else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS)
            text_mod = 'M';

          if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
            prop_mod = 'M';

          svn_stringbuf_appendbytes (tmp_message, &text_mod, 1); 
          svn_stringbuf_appendbytes (tmp_message, &prop_mod, 1); 
          svn_stringbuf_appendcstr (tmp_message, "   ");
          svn_stringbuf_appendcstr (tmp_message, path);
          svn_stringbuf_appendcstr (tmp_message, "\n");
        }

      /* Use the external edit to get a log message. */
      err = svn_cl__edit_externally (&msg2, &lmb->tmpfile_left,
                                     lmb->base_dir, tmp_message->data, 
                                     "svn-commit", pool);

      /* Clean up the log message into UTF8/LF before giving it to
         libsvn_client. */
      if (msg2)
        {
          svn_string_t *new_logval = svn_string_create ("", pool);
          new_logval->data = msg2;
          new_logval->len = strlen (msg2);
          SVN_ERR (svn_subst_translate_string (&new_logval, new_logval,
                                               NULL, pool));
          msg2 = new_logval->data;
        }        

      /* Dup the tmpfile path into its baton's pool. */
      *tmp_file = lmb->tmpfile_left = apr_pstrdup (lmb->pool, 
                                                   lmb->tmpfile_left);

      /* If the edit returned an error, handle it. */
      if (err)
        {
          if (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_EDITOR)
            err = svn_error_quick_wrap 
              (err, "Could not use external editor to fetch log message; "
               "consider setting the $SVN_EDITOR environment variable "
               "or using the --message (-m) or --file (-F) options.");
          return err;
        }

      if (msg2)
        message = svn_stringbuf_create (msg2, pool);

      /* Strip the prefix from the buffer. */
      if (message)
        truncate_buffer_at_prefix (&message->len, message->data, 
                                   EDITOR_EOF_PREFIX);

      if (message)
        {
          /* We did get message, now check if it is anything more than just
             white space as we will consider white space only as empty */
          int len;

          for (len = message->len; len >= 0; len--)
            {
              /* FIXME: should really use an UTF-8 whitespace test
                 rather than apr_isspace, which is locale dependant */
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

              /* If the user chooses to abort, we cleanup the
                 temporary file and exit the loop with a NULL
                 message. */
              if ('a' == letter)
                {
                  SVN_ERR (svn_io_remove_file (lmb->tmpfile_left, pool));
                  *tmp_file = lmb->tmpfile_left = NULL;
                  break;
                }

              /* If the user chooses to continue, we make an empty
                 message, which will cause us to exit the loop.  We
                 also cleanup the temporary file. */
              if ('c' == letter) 
                {
                  SVN_ERR (svn_io_remove_file (lmb->tmpfile_left, pool));
                  *tmp_file = lmb->tmpfile_left = NULL;
                  message = svn_stringbuf_create ("", pool);
                }

              /* If the user chooses anything else, the loop will
                 continue on the NULL message. */
            }
        }
    }
  
  *log_msg = message ? message->data : NULL;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__get_url_from_target (const char **URL,
                             const char *target,
                             apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;          
  const svn_wc_entry_t *entry;  
  svn_boolean_t is_url = svn_path_is_url (target);
  
  if (is_url)
    *URL = target;

  else
    {
      SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, target,
                                      FALSE, FALSE, pool));
      SVN_ERR (svn_wc_entry (&entry, target, adm_access, FALSE, pool));
      SVN_ERR (svn_wc_adm_close (adm_access));
      
      *URL = entry ? entry->url : NULL;
    }

  return SVN_NO_ERROR;
}


