/*
 * util.c: Subversion command line client utility functions. Any
 * functions that need to be shared across subcommands should be put
 * in here.
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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

#include <apr_errno.h>
#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_general.h>
#include <apr_lib.h>

#include "svn_client.h"
#include "svn_cmdline.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_utf.h"
#include "svn_subst.h"
#include "svn_config.h"
#include "svn_xml.h"
#include "svn_private_config.h"
#include "cl.h"




svn_error_t *
svn_cl__print_commit_info(svn_commit_info_t *commit_info,
                          apr_pool_t *pool)
{
  if (commit_info) 
    {
      if (SVN_IS_VALID_REVNUM(commit_info->revision))
        SVN_ERR(svn_cmdline_printf(pool, _("\nCommitted revision %ld.\n"),
                                   commit_info->revision));
     
      /* Writing to stdout, as there maybe systems that consider the
       * presence of stderr as an indication of commit failure.
       * OTOH, this is only of informational nature to the user as
       * the commit has succeeded. */
      if (commit_info->post_commit_err)
        SVN_ERR(svn_cmdline_printf(pool, _("\nWarning: %s\n"),
                                   commit_info->post_commit_err));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__edit_externally(svn_string_t **edited_contents /* UTF-8! */,
                        const char **tmpfile_left /* UTF-8! */,
                        const char *editor_cmd,
                        const char *base_dir /* UTF-8! */,
                        const svn_string_t *contents /* UTF-8! */,
                        const char *prefix,
                        apr_hash_t *config,
                        svn_boolean_t as_text,
                        const char *encoding,
                        apr_pool_t *pool)
{
  const char *editor;
  const char *cmd;
  apr_file_t *tmp_file;
  const char *tmpfile_name;
  const char *tmpfile_native;
  const char *tmpfile_apr, *base_dir_apr;
  svn_string_t *translated_contents;
  apr_status_t apr_err, apr_err2;
  apr_size_t written;
  apr_finfo_t finfo_before, finfo_after;
  svn_error_t *err = SVN_NO_ERROR, *err2;
  char *old_cwd;
  int sys_err;
  svn_boolean_t remove_file = TRUE;
  struct svn_config_t *cfg;

  /* Use the editor specified on the command line via --editor-cmd, if any. */
  editor = editor_cmd;

  /* Otherwise look for the Subversion-specific environment variable. */
  if (! editor)
    editor = getenv("SVN_EDITOR");

  /* If not found then fall back on the config file. */
  if (! editor)
    {
      cfg = config ? apr_hash_get(config, SVN_CONFIG_CATEGORY_CONFIG, 
                                  APR_HASH_KEY_STRING) : NULL;
      svn_config_get(cfg, &editor, SVN_CONFIG_SECTION_HELPERS, 
                     SVN_CONFIG_OPTION_EDITOR_CMD, NULL);
    }

  /* If not found yet then try general purpose environment variables. */
  if (! editor)
    editor = getenv("VISUAL");
  if (! editor)
    editor = getenv("EDITOR");

#ifdef SVN_CLIENT_EDITOR
  /* If still not found then fall back on the hard-coded default. */
  if (! editor)
    editor = SVN_CLIENT_EDITOR;
#endif

  /* Abort if there is no editor specified */
  if (! editor)
    return svn_error_create 
      (SVN_ERR_CL_NO_EXTERNAL_EDITOR, NULL,
       _("None of the environment variables SVN_EDITOR, VISUAL or EDITOR is "
         "set, and no 'editor-cmd' run-time configuration option was found"));

  /* Convert file contents from UTF-8/LF if desired. */
  if (as_text)
    {
      const char *translated;
      SVN_ERR(svn_subst_translate_cstring2(contents->data, &translated,
                                           APR_EOL_STR, FALSE,
                                           NULL, FALSE, pool));
      translated_contents = svn_string_create("", pool);
      if (encoding)
        SVN_ERR(svn_utf_cstring_from_utf8_ex2(&translated_contents->data,
                                              translated, encoding, pool));
      else
        SVN_ERR(svn_utf_cstring_from_utf8(&translated_contents->data,
                                          translated, pool));
      translated_contents->len = strlen(translated_contents->data);
    }
  else
    translated_contents = svn_string_dup(contents, pool);

  /* Move to BASE_DIR to avoid getting characters that need quoting
     into tmpfile_name */
  apr_err = apr_filepath_get(&old_cwd, APR_FILEPATH_NATIVE, pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't get working directory"));

  /* APR doesn't like "" directories */
  if (base_dir[0] == '\0')
    base_dir_apr = ".";
  else
    SVN_ERR(svn_path_cstring_from_utf8(&base_dir_apr, base_dir, pool));
  apr_err = apr_filepath_set(base_dir_apr, pool);
  if (apr_err)
    {
      return svn_error_wrap_apr
        (apr_err, _("Can't change working directory to '%s'"), base_dir);
    }

  /*** From here on, any problems that occur require us to cd back!! ***/

  /* Ask the working copy for a temporary file that starts with
     PREFIX. */
  err = svn_io_open_unique_file2(&tmp_file, &tmpfile_name,
                                 prefix, ".tmp", svn_io_file_del_none, pool);

  if (err && APR_STATUS_IS_EACCES(err->apr_err))
    {
      const char *temp_dir_apr;

      svn_error_clear(err);

      SVN_ERR(svn_io_temp_dir(&base_dir, pool));

      SVN_ERR(svn_path_cstring_from_utf8(&temp_dir_apr, base_dir, pool));
      apr_err = apr_filepath_set(temp_dir_apr, pool);
      if (apr_err)
        {
          return svn_error_wrap_apr
            (apr_err, _("Can't change working directory to '%s'"), base_dir);
        }

      err = svn_io_open_unique_file2(&tmp_file, &tmpfile_name,
                                     prefix, ".tmp",
                                     svn_io_file_del_none, pool);
    }

  if (err)
    goto cleanup2;

  /*** From here on, any problems that occur require us to cleanup
       the file we just created!! ***/

  /* Dump initial CONTENTS to TMP_FILE. */
  apr_err = apr_file_write_full(tmp_file, translated_contents->data,
                                translated_contents->len, &written);

  apr_err2 = apr_file_close(tmp_file);
  if (! apr_err)
    apr_err = apr_err2;
  
  /* Make sure the whole CONTENTS were written, else return an error. */
  if (apr_err)
    {
      err = svn_error_wrap_apr(apr_err, _("Can't write to '%s'"), 
                               tmpfile_name);
      goto cleanup;
    }

  err = svn_path_cstring_from_utf8(&tmpfile_apr, tmpfile_name, pool);
  if (err)
    goto cleanup;

  /* Get information about the temporary file before the user has
     been allowed to edit its contents. */
  apr_err = apr_stat(&finfo_before, tmpfile_apr,
                     APR_FINFO_MTIME | APR_FINFO_SIZE, pool);
  if (apr_err)
    {
      err = svn_error_wrap_apr(apr_err, _("Can't stat '%s'"), tmpfile_name);
      goto cleanup;
    }

  /* Now, run the editor command line.  */
  err = svn_utf_cstring_from_utf8(&tmpfile_native, tmpfile_name, pool);
  if (err)
    goto cleanup;
  cmd = apr_psprintf(pool, "%s %s", editor, tmpfile_native);
  sys_err = system(cmd);
  if (sys_err != 0)
    {
      /* Extracting any meaning from sys_err is platform specific, so just
         use the raw value. */
      err =  svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                               _("system('%s') returned %d"), cmd, sys_err);
      goto cleanup;
    }
  
  /* Get information about the temporary file after the assumed editing. */
  apr_err = apr_stat(&finfo_after, tmpfile_apr,
                     APR_FINFO_MTIME | APR_FINFO_SIZE, pool);
  if (apr_err)
    {
      err = svn_error_wrap_apr(apr_err, _("Can't stat '%s'"), tmpfile_name);
      goto cleanup;
    }

  /* If the caller wants us to leave the file around, return the path
     of the file we used, and make a note not to destroy it.  */
  if (tmpfile_left)
    {
      *tmpfile_left = svn_path_join(base_dir, tmpfile_name, pool);
      remove_file = FALSE;
    }

  /* If the file looks changed... */
  if ((finfo_before.mtime != finfo_after.mtime) ||
      (finfo_before.size != finfo_after.size))
    {
      svn_stringbuf_t *edited_contents_s;
      err = svn_stringbuf_from_file(&edited_contents_s, tmpfile_name, pool);
      if (err)
        goto cleanup;

      *edited_contents = svn_string_create_from_buf(edited_contents_s, pool);

      /* Translate back to UTF8/LF if desired. */
      if (as_text)
        {
          err = svn_subst_translate_string(edited_contents, *edited_contents,
                                           encoding, pool);
          if (err)
            goto cleanup;
        }
    }
  else
    {
      /* No edits seem to have been made */
      *edited_contents = NULL;
    }

 cleanup:
  if (remove_file)
    {
      /* Remove the file from disk.  */
      err2 = svn_io_remove_file(tmpfile_name, pool);

      /* Only report remove error if there was no previous error. */
      if (! err && err2)
        err = err2;
      else
        svn_error_clear(err2);
    }

 cleanup2:
  /* If we against all probability can't cd back, all further relative
     file references would be screwed up, so we have to abort. */
  apr_err = apr_filepath_set(old_cwd, pool);
  if (apr_err)
    {
      svn_handle_error2(svn_error_wrap_apr
                        (apr_err, _("Can't restore working directory")),
                        stderr, TRUE /* fatal */, "svn: ");
    }

  return err;
}


struct log_msg_baton
{
  const char *editor_cmd;  /* editor specified via --editor-cmd, else NULL */
  const char *message;  /* the message. */
  const char *message_encoding; /* the locale/encoding of the message. */
  const char *base_dir; /* the base directory for an external edit. UTF-8! */
  const char *tmpfile_left; /* the tmpfile left by an external edit. UTF-8! */
  svn_boolean_t non_interactive; /* if true, don't pop up an editor */
  apr_hash_t *config; /* client configuration hash */
  svn_boolean_t keep_locks; /* Keep repository locks? */
  apr_pool_t *pool; /* a pool. */
};


svn_error_t *
svn_cl__make_log_msg_baton(void **baton,
                           svn_cl__opt_state_t *opt_state,
                           const char *base_dir /* UTF-8! */,
                           apr_hash_t *config,
                           apr_pool_t *pool)
{
  struct log_msg_baton *lmb = apr_palloc(pool, sizeof(*lmb));

  if (opt_state->filedata) 
    {
      if (strlen(opt_state->filedata->data) < opt_state->filedata->len)
        {
          /* The data contains a zero byte, and therefore can't be
             represented as a C string.  Punt now; it's probably not
             a deliberate encoding, and even if it is, we still
             can't handle it. */
          return svn_error_create(SVN_ERR_CL_BAD_LOG_MESSAGE, NULL,
                                  _("Log message contains a zero byte"));
        }
      lmb->message = opt_state->filedata->data;
    }      
  else
    {
      lmb->message = opt_state->message;
    }

  lmb->editor_cmd = opt_state->editor_cmd;
  if (opt_state->encoding)
    {
      lmb->message_encoding = opt_state->encoding;
    }
  else if (config)
    {
      svn_config_t *cfg = apr_hash_get(config, SVN_CONFIG_CATEGORY_CONFIG, 
                                       APR_HASH_KEY_STRING);
      svn_config_get(cfg, &(lmb->message_encoding),
                     SVN_CONFIG_SECTION_MISCELLANY,
                     SVN_CONFIG_OPTION_LOG_ENCODING,
                     NULL);
    }

  lmb->base_dir = base_dir ? base_dir : "";
  lmb->tmpfile_left = NULL;
  lmb->config = config;
  lmb->keep_locks = opt_state->no_unlock;
  lmb->non_interactive = opt_state->non_interactive;
  lmb->pool = pool;
  *baton = lmb;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__cleanup_log_msg(void *log_msg_baton,
                        svn_error_t *commit_err)
{
  struct log_msg_baton *lmb = log_msg_baton;

  /* If there was no tmpfile left, or there is no log message baton,
     return COMMIT_ERR. */
  if ((! lmb) || (! lmb->tmpfile_left))
    return commit_err;

  /* If there was no commit error, cleanup the tmpfile and return. */
  if (! commit_err)
    return svn_io_remove_file(lmb->tmpfile_left, lmb->pool);

  /* There was a commit error; there is a tmpfile.  Leave the tmpfile
     around, and add message about its presence to the commit error
     chain.  Then return COMMIT_ERR.  If the conversion from UTF-8 to
     native encoding fails, we have to compose that error with the
     commit error chain, too. */
  svn_error_compose
    (commit_err,
     svn_error_create(commit_err->apr_err,
                      svn_error_createf(commit_err->apr_err, NULL,
                                        "   '%s'", lmb->tmpfile_left),
                      _("Your commit message was left in "
                        "a temporary file:")));
  return commit_err;
}


/* Remove line-starting PREFIX and everything after it from BUFFER.
   If NEW_LEN is non-NULL, return the new length of BUFFER in
   *NEW_LEN.  */
static void
truncate_buffer_at_prefix(apr_size_t *new_len,
                          char *buffer,
                          const char *prefix)
{
  char *substring = buffer;

  assert(buffer && prefix);

  /* Initialize *NEW_LEN. */
  if (new_len)
    *new_len = strlen(buffer);

  while (1)
    {
      /* Find PREFIX in BUFFER. */
      substring = strstr(substring, prefix);
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

  /* NOTREACHED */
}


#define EDITOR_EOF_PREFIX  _("--This line, and those below, will be ignored--")

/* This function is of type svn_client_get_commit_log2_t. */
svn_error_t *
svn_cl__get_log_message(const char **log_msg,
                        const char **tmp_file,
                        const apr_array_header_t *commit_items,
                        void *baton,
                        apr_pool_t *pool)
{
  svn_stringbuf_t *default_msg = NULL;
  struct log_msg_baton *lmb = baton;
  svn_stringbuf_t *message = NULL;

  /* Set default message.  */
  default_msg = svn_stringbuf_create(APR_EOL_STR, pool);
  svn_stringbuf_appendcstr(default_msg, EDITOR_EOF_PREFIX);
  svn_stringbuf_appendcstr(default_msg, APR_EOL_STR APR_EOL_STR);

  *tmp_file = NULL;
  if (lmb->message)
    {
      svn_string_t *log_msg_string = svn_string_create(lmb->message, pool);

      SVN_ERR(svn_subst_translate_string(&log_msg_string, log_msg_string,
                                         lmb->message_encoding, pool));

      *log_msg = log_msg_string->data;

      /* Trim incoming messages the EOF marker text and the junk that
         follows it.  */
      truncate_buffer_at_prefix(NULL, (char*)*log_msg, EDITOR_EOF_PREFIX);

      return SVN_NO_ERROR;
    }
#ifdef AS400
  /* OS400 supports only -F and -m for specifying log messages. */
  else
    return svn_error_create
      (SVN_ERR_CL_NO_EXTERNAL_EDITOR, NULL,
       _("Use of an external editor to fetch log message is not supported "
         "on OS400; consider using the --message (-m) or --file (-F) "
         "options"));
#endif

  if (! commit_items->nelts)
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
      svn_stringbuf_t *tmp_message = svn_stringbuf_dup(default_msg, pool);
      svn_error_t *err = SVN_NO_ERROR;
      svn_string_t *msg_string = svn_string_create("", pool);

      for (i = 0; i < commit_items->nelts; i++)
        {
          svn_client_commit_item2_t *item
            = APR_ARRAY_IDX(commit_items, i, svn_client_commit_item2_t *);
          const char *path = item->path;
          char text_mod = '_', prop_mod = ' ', unlock = ' ';

          if (! path)
            path = item->url;
          else if (! *path)
            path = ".";

          if (path && lmb->base_dir)
            path = svn_path_is_child(lmb->base_dir, path, pool);

          /* If still no path, then just use current directory. */
          if (! path)
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

          if (! lmb->keep_locks
              && item->state_flags & SVN_CLIENT_COMMIT_ITEM_LOCK_TOKEN)
            unlock = 'U';

          svn_stringbuf_appendbytes(tmp_message, &text_mod, 1); 
          svn_stringbuf_appendbytes(tmp_message, &prop_mod, 1); 
          svn_stringbuf_appendbytes(tmp_message, &unlock, 1); 
          svn_stringbuf_appendcstr(tmp_message, "  ");
          svn_stringbuf_appendcstr(tmp_message, path);
          svn_stringbuf_appendcstr(tmp_message, APR_EOL_STR);
        }

      msg_string->data = tmp_message->data;
      msg_string->len = tmp_message->len;

      /* Use the external edit to get a log message. */
      if (! lmb->non_interactive)
        {
          err = svn_cl__edit_externally(&msg_string, &lmb->tmpfile_left,
                                        lmb->editor_cmd, lmb->base_dir,
                                        msg_string, "svn-commit",
                                        lmb->config, TRUE,
                                        lmb->message_encoding,
                                        pool);
        }
      else /* non_interactive flag says we can't pop up an editor, so error */
        {
          return svn_error_create
            (SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
             _("Cannot invoke editor to get log message "
               "when non-interactive"));
        }

      /* Dup the tmpfile path into its baton's pool. */
      *tmp_file = lmb->tmpfile_left = apr_pstrdup(lmb->pool, 
                                                  lmb->tmpfile_left);

      /* If the edit returned an error, handle it. */
      if (err)
        {
          if (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_EDITOR)
            err = svn_error_quick_wrap 
              (err, _("Could not use external editor to fetch log message; "
                      "consider setting the $SVN_EDITOR environment variable "
                      "or using the --message (-m) or --file (-F) options"));
          return err;
        }

      if (msg_string)
        message = svn_stringbuf_create_from_string(msg_string, pool);

      /* Strip the prefix from the buffer. */
      if (message)
        truncate_buffer_at_prefix(&message->len, message->data, 
                                  EDITOR_EOF_PREFIX);

      if (message)
        {
          /* We did get message, now check if it is anything more than just
             white space as we will consider white space only as empty */
          int len;

          for (len = message->len - 1; len >= 0; len--)
            {
              /* FIXME: should really use an UTF-8 whitespace test
                 rather than apr_isspace, which is locale dependant */
              if (! apr_isspace(message->data[len]))
                break;
            }
          if (len < 0)
            message = NULL;
        }

      if (! message)
        {
          const char *reply;
          SVN_ERR(svn_cmdline_prompt_user
                  (&reply,
                   _("\nLog message unchanged or not specified\n"
                     "a)bort, c)ontinue, e)dit\n"), pool));
          if (reply)
            {
              char letter = apr_tolower(reply[0]);

              /* If the user chooses to abort, we cleanup the
                 temporary file and exit the loop with a NULL
                 message. */
              if ('a' == letter)
                {
                  SVN_ERR(svn_io_remove_file(lmb->tmpfile_left, pool));
                  *tmp_file = lmb->tmpfile_left = NULL;
                  break;
                }

              /* If the user chooses to continue, we make an empty
                 message, which will cause us to exit the loop.  We
                 also cleanup the temporary file. */
              if ('c' == letter) 
                {
                  SVN_ERR(svn_io_remove_file(lmb->tmpfile_left, pool));
                  *tmp_file = lmb->tmpfile_left = NULL;
                  message = svn_stringbuf_create("", pool);
                }

              /* If the user chooses anything else, the loop will
                 continue on the NULL message. */
            }
        }
    }
  
  *log_msg = message ? message->data : NULL;
  return SVN_NO_ERROR;
}


/* ### The way our error wrapping currently works, the error returned
 * from here will look as though it originates in this source file,
 * instead of in the caller's source file.  This can be a bit
 * misleading, until one starts debugging.  Ideally, there'd be a way
 * to wrap an error while preserving its FILE/LINE info.
 */
svn_error_t *
svn_cl__may_need_force(svn_error_t *err)
{
  if (err
      && (err->apr_err == SVN_ERR_UNVERSIONED_RESOURCE ||
          err->apr_err == SVN_ERR_CLIENT_MODIFIED))
    {
      /* Should this svn_error_compose a new error number? Probably not,
         the error hasn't changed. */
      err = svn_error_quick_wrap
        (err, _("Use --force to override this restriction") );
    }

  return err;
}


svn_error_t *
svn_cl__error_checked_fputs(const char *string, FILE* stream)
{
  /* This function is equal to svn_cmdline_fputs() minus
     the utf8->local encoding translation */

  /* On POSIX systems, errno will be set on an error in fputs, but this might
     not be the case on other platforms.  We reset errno and only
     use it if it was set by the below fputs call.  Else, we just return
     a generic error. */
  errno = 0;

  if (fputs(string, stream) == EOF)
    {
      if (errno)
        return svn_error_wrap_apr(errno, _("Write error"));
      else
        return svn_error_create(SVN_ERR_IO_WRITE_ERROR, NULL, NULL);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__try(svn_error_t *err,
            svn_boolean_t *success,
            svn_boolean_t quiet,
            ...)
{
  if (err)
    {
      apr_status_t apr_err;
      va_list ap;

      if (success)
        *success = FALSE;

      va_start(ap, quiet);
      while ((apr_err = va_arg(ap, apr_status_t)) != SVN_NO_ERROR)
        {
          if (err->apr_err == apr_err)
            {
              if (! quiet)
                svn_handle_warning(stderr, err);
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }
        }
      va_end(ap);
    }
  else if (success)
    {
      *success = TRUE;
    }

  return err;
}


void
svn_cl__xml_tagged_cdata(svn_stringbuf_t **sb,
                         apr_pool_t *pool,
                         const char *tagname,
                         const char *string)
{
  if (string)
    {
      svn_xml_make_open_tag(sb, pool, svn_xml_protect_pcdata,
                            tagname, NULL);
      svn_xml_escape_cdata_cstring(sb, string, pool);
      svn_xml_make_close_tag(sb, pool, tagname);
    }
}


const char *
svn_cl__node_kind_str(svn_node_kind_t kind)
{
  switch (kind)
    {
    case svn_node_dir:
      return "dir";
    case svn_node_file:
      return "file";
    default:
      return "";
    }
}
