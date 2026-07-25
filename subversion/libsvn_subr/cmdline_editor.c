/*
 * cmdline_editor.c :  svn editor implementation.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include <apr_version.h>
#if APR_VERSION_AT_LEAST(1,5,0)
#include <apr_escape.h>
#else
#include "private/svn_dep_compat.h"
#endif
#include <apr_env.h>            /* for apr_env_get */
#include <apr_pools.h>

#include "svn_cmdline.h"
#include "svn_ctype.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_config.h"
#include "svn_subst.h"

#include "private/svn_cmdline_private.h"
#include "private/svn_utf_private.h"

#include "svn_private_config.h"

/* Helper for the edit_externally functions.  Set *EDITOR to some path to an
   editor binary, in native C string on Unix/Linux platforms and in UTF-8
   string on Windows platform.  Sources to search include: the EDITOR_CMD
   argument (if not NULL), $SVN_EDITOR, the runtime CONFIG variable (if CONFIG
   is not NULL), $VISUAL, $EDITOR.  Return
   SVN_ERR_CL_NO_EXTERNAL_EDITOR if no binary can be found. */
static svn_error_t *
find_editor_binary(const char **editor,
                   const char *editor_cmd,
                   apr_hash_t *config,
                   apr_pool_t *pool)
{
  const char *e;
  const char *e_cfg;
  struct svn_config_t *cfg;
  apr_status_t status;

  /* Use the editor specified on the command line via --editor-cmd, if any. */
#ifdef WIN32
  /* On Windows, editor_cmd is transcoded to the system active code page
     because we use main() as a entry point without APR's (or our own) wrapper
     in command line tools. */
  if (editor_cmd)
    {
      SVN_ERR(svn_utf_cstring_to_utf8(&e, editor_cmd, pool));
    }
  else
    {
      e = NULL;
    }
#else
  e = editor_cmd;
#endif

  /* Otherwise look for the Subversion-specific environment variable. */
  if (! e)
    {
      status = apr_env_get((char **)&e, "SVN_EDITOR", pool);
      if (status || ! *e)
        {
           e = NULL;
        }
    }

  /* If not found then fall back on the config file. */
  if (! e)
    {
      cfg = config ? svn_hash_gets(config, SVN_CONFIG_CATEGORY_CONFIG) : NULL;
      svn_config_get(cfg, &e_cfg, SVN_CONFIG_SECTION_HELPERS,
                     SVN_CONFIG_OPTION_EDITOR_CMD, NULL);
#ifdef WIN32
      if (e_cfg)
        {
          /* On Windows, we assume that config values are set in system active
             code page, so we need transcode it here. */
          SVN_ERR(svn_utf_cstring_to_utf8(&e, e_cfg, pool));
        }
#else
      e = e_cfg;
#endif
    }

  /* If not found yet then try general purpose environment variables. */
  if (! e)
    {
      status = apr_env_get((char**)&e, "VISUAL", pool);
      if (status || ! *e)
        {
           e = NULL;
        }
    }
  if (! e)
    {
      status = apr_env_get((char**)&e, "EDITOR", pool);
      if (status || ! *e)
        {
           e = NULL;
        }
    }

#ifdef SVN_CLIENT_EDITOR
  /* If still not found then fall back on the hard-coded default. */
  if (! e)
    e = SVN_CLIENT_EDITOR;
#endif

  /* Error if there is no editor specified */
  if (e)
    {
      const char *c;

      for (c = e; *c; c++)
        if (!svn_ctype_isspace(*c))
          break;

      if (! *c)
        return svn_error_create
          (SVN_ERR_CL_NO_EXTERNAL_EDITOR, NULL,
           _("The EDITOR, SVN_EDITOR or VISUAL environment variable or "
             "'editor-cmd' run-time configuration option is empty or "
             "consists solely of whitespace. Expected a shell command."));
    }
  else
    return svn_error_create
      (SVN_ERR_CL_NO_EXTERNAL_EDITOR, NULL,
       _("None of the environment variables SVN_EDITOR, VISUAL or EDITOR are "
         "set, and no 'editor-cmd' run-time configuration option was found"));

  *editor = e;
  return SVN_NO_ERROR;
}

/* Wrapper around apr_pescape_shell() which also escapes whitespace. */
static const char *
escape_path(apr_pool_t *pool, const char *orig_path)
{
  apr_size_t len, esc_len;
  apr_status_t status;

  len = strlen(orig_path);
  esc_len = 0;

  status = apr_escape_shell(NULL, orig_path, len, &esc_len);

  if (status == APR_NOTFOUND)
    {
      /* No special characters found by APR, so just surround it in double
         quotes in case there is whitespace, which APR (as of 1.6.5) doesn't
         consider special. */
      return apr_psprintf(pool, "\"%s\"", orig_path);
    }
  else
    {
#ifdef WIN32
      const char *p;
      /* Following the advice from
         https://docs.microsoft.com/en-us/archive/blogs/twistylittlepassagesallalike/everyone-quotes-command-line-arguments-the-wrong-way
         1. Surround argument with double-quotes
         2. Escape backslashes, if they're followed by a double-quote, and double-quotes
         3. Escape any metacharacter, including double-quotes, with ^ */

      /* Use APR's buffer size as an approximation for how large the escaped
         string should be, plus 4 bytes for the leading/trailing ^" */
      svn_stringbuf_t *buf = svn_stringbuf_create_ensure(esc_len + 4, pool);
      svn_stringbuf_appendcstr(buf, "^\"");
      for (p = orig_path; *p; p++)
        {
          int nr_backslash = 0;
          while (*p && *p == '\\')
            {
              nr_backslash++;
              p++;
            }

          if (!*p)
            /* We've reached the end of the argument, so we need 2n backslash
               characters.  That will be interpreted as n backslashes and the
               final double-quote character will be interpreted as the final
               string delimiter. */
            svn_stringbuf_appendfill(buf, '\\', nr_backslash * 2);
          else if (*p == '"')
            {
              /* Double-quote as part of the argument means we need to double
                 any preceding backslashes and then add one to escape the
                 double-quote. */
              svn_stringbuf_appendfill(buf, '\\', nr_backslash * 2 + 1);
              svn_stringbuf_appendbyte(buf, '^');
              svn_stringbuf_appendbyte(buf, *p);
            }
          else
            {
              /* Since there's no double-quote, we just insert any backslashes
                 literally.  No escaping needed. */
              svn_stringbuf_appendfill(buf, '\\', nr_backslash);
              if (strchr("()%!^<>&|", *p))
                svn_stringbuf_appendbyte(buf, '^');
              svn_stringbuf_appendbyte(buf, *p);
            }
        }
      svn_stringbuf_appendcstr(buf, "^\"");
      return buf->data;
#else
      char *path, *p, *esc_path;

      /* Account for whitespace, since APR doesn't */
      for (p = (char *)orig_path; *p; p++)
        if (strchr(" \t\n\r", *p))
          esc_len++;

      path = apr_pcalloc(pool, esc_len);
      apr_escape_shell(path, orig_path, len, NULL);

      p = esc_path = apr_pcalloc(pool, len + esc_len + 1);
      while (*path)
        {
          if (strchr(" \t\n\r", *path))
            *p++ = '\\';
          *p++ = *path++;
        }

      return esc_path;
#endif
    }
}

svn_error_t *
svn_cmdline__edit_file_externally(const char *path,
                                  const char *editor_cmd,
                                  apr_hash_t *config,
                                  apr_pool_t *pool)
{
  const char *editor, *cmd, *base_dir, *file_name, *base_dir_apr;
  const char *file_name_local;
#ifdef WIN32
  const WCHAR *wcmd;
#endif
  char *old_cwd;
  int sys_err;
  apr_status_t apr_err;

  svn_dirent_split(&base_dir, &file_name, path, pool);

  SVN_ERR(find_editor_binary(&editor, editor_cmd, config, pool));

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
    return svn_error_wrap_apr
      (apr_err, _("Can't change working directory to '%s'"), base_dir);

  SVN_ERR(svn_path_cstring_from_utf8(&file_name_local,
                                     escape_path(pool, file_name), pool));
  /* editor is explicitly documented as being interpreted by the user's shell,
     and as such should already be quoted/escaped as needed. */
#ifndef WIN32
  cmd = apr_psprintf(pool, "%s %s", editor, file_name_local);
  sys_err = system(cmd);
#else
  cmd = apr_psprintf(pool, "\"%s %s\"", editor, file_name_local);
  SVN_ERR(svn_utf__win32_utf8_to_utf16(&wcmd, cmd, NULL, pool));
  sys_err = _wsystem(wcmd);
#endif

  apr_err = apr_filepath_set(old_cwd, pool);
  if (apr_err)
    svn_handle_error2(svn_error_wrap_apr
                      (apr_err, _("Can't restore working directory")),
                      stderr, TRUE /* fatal */, "svn: ");

  if (sys_err)
    {
      const char *cmd_utf8;

      /* Extracting any meaning from sys_err is platform specific, so just
         use the raw value. */
      SVN_ERR(svn_path_cstring_to_utf8(&cmd_utf8, cmd, pool));
      return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                               _("system('%s') returned %d"),
                               cmd_utf8, sys_err);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_cmdline__edit_string_externally(svn_string_t **edited_contents /* UTF-8! */,
                                    const char **tmpfile_left /* UTF-8! */,
                                    const char *editor_cmd,
                                    const char *base_dir /* UTF-8! */,
                                    const svn_string_t *contents /* UTF-8! */,
                                    const char *filename,
                                    apr_hash_t *config,
                                    svn_boolean_t as_text,
                                    const char *encoding,
                                    apr_pool_t *pool)
{
  const char *editor;
  const char *cmd;
#ifdef WIN32
  const WCHAR *wcmd;
#endif
  apr_file_t *tmp_file;
  const char *tmpfile_name;
  const char *tmpfile_native;
  const char *base_dir_apr;
  svn_string_t *translated_contents;
  apr_status_t apr_err;
  apr_size_t written;
  apr_finfo_t finfo_before, finfo_after;
  svn_error_t *err = SVN_NO_ERROR;
  char *old_cwd;
  int sys_err;
  svn_boolean_t remove_file = TRUE;

  SVN_ERR(find_editor_binary(&editor, editor_cmd, config, pool));

  /* Convert file contents from UTF-8/LF if desired. */
  if (as_text)
    {
      const char *translated;
      SVN_ERR(svn_subst_translate_cstring2(contents->data, &translated,
                                           APR_EOL_STR, FALSE,
                                           NULL, FALSE, pool));
      translated_contents = svn_string_create_empty(pool);
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

  /* Ask the working copy for a temporary file named FILENAME-something. */
  err = svn_io_open_uniquely_named(&tmp_file, &tmpfile_name,
                                   "" /* dirpath */,
                                   filename,
                                   ".tmp",
                                   svn_io_file_del_none, pool, pool);

  if (err && (APR_STATUS_IS_EACCES(err->apr_err) || err->apr_err == EROFS))
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

      err = svn_io_open_uniquely_named(&tmp_file, &tmpfile_name,
                                       "" /* dirpath */,
                                       filename,
                                       ".tmp",
                                       svn_io_file_del_none, pool, pool);
    }

  if (err)
    goto cleanup2;

  /*** From here on, any problems that occur require us to cleanup
       the file we just created!! ***/

  /* Dump initial CONTENTS to TMP_FILE. */
  err = svn_io_file_write_full(tmp_file, translated_contents->data,
                               translated_contents->len, &written,
                               pool);

  err = svn_error_compose_create(err, svn_io_file_close(tmp_file, pool));

  /* Make sure the whole CONTENTS were written, else return an error. */
  if (err)
    goto cleanup;

  /* Get information about the temporary file before the user has
     been allowed to edit its contents. */
  err = svn_io_stat(&finfo_before, tmpfile_name, APR_FINFO_MTIME, pool);
  if (err)
    goto cleanup;

  /* Backdate the file a little bit in case the editor is very fast
     and doesn't change the size.  (Use two seconds, since some
     filesystems have coarse granularity.)  It's OK if this call
     fails, so we don't check its return value.*/
  err = svn_io_set_file_affected_time(finfo_before.mtime
                                              - apr_time_from_sec(2),
                                      tmpfile_name, pool);
  svn_error_clear(err);

  /* Stat it again to get the mtime we actually set. */
  err = svn_io_stat(&finfo_before, tmpfile_name,
                    APR_FINFO_MTIME | APR_FINFO_SIZE, pool);
  if (err)
    goto cleanup;

  /* Prepare the editor command line.  */
  err = svn_path_cstring_from_utf8(&tmpfile_native,
                                   escape_path(pool, tmpfile_name), pool);
  if (err)
    goto cleanup;

  /* editor is explicitly documented as being interpreted by the user's shell,
     and as such should already be quoted/escaped as needed. */
#ifndef WIN32
  cmd = apr_psprintf(pool, "%s %s", editor, tmpfile_native);
#else
  cmd = apr_psprintf(pool, "\"%s %s\"", editor, tmpfile_native);
#endif

  /* If the caller wants us to leave the file around, return the path
     of the file we'll use, and make a note not to destroy it.  */
  if (tmpfile_left)
    {
      *tmpfile_left = svn_dirent_join(base_dir, tmpfile_name, pool);
      remove_file = FALSE;
    }

  /* Now, run the editor command line.  */
#ifndef WIN32
  sys_err = system(cmd);
#else
  SVN_ERR(svn_utf__win32_utf8_to_utf16(&wcmd, cmd, NULL, pool));
  sys_err = _wsystem(wcmd);
#endif
  if (sys_err != 0)
    {
      const char *cmd_utf8;

      /* Extracting any meaning from sys_err is platform specific, so just
         use the raw value. */
      SVN_ERR(svn_path_cstring_to_utf8(&cmd_utf8, cmd, pool));
      err =  svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                               _("system('%s') returned %d"),
                               cmd_utf8, sys_err);
      goto cleanup;
    }

  /* Get information about the temporary file after the assumed editing. */
  err = svn_io_stat(&finfo_after, tmpfile_name,
                    APR_FINFO_MTIME | APR_FINFO_SIZE, pool);
  if (err)
    goto cleanup;

  /* If the file looks changed... */
  if ((finfo_before.mtime != finfo_after.mtime) ||
      (finfo_before.size != finfo_after.size))
    {
      svn_stringbuf_t *edited_contents_s;
      err = svn_stringbuf_from_file2(&edited_contents_s, tmpfile_name, pool);
      if (err)
        goto cleanup;

      *edited_contents = svn_stringbuf__morph_into_string(edited_contents_s);

      /* Translate back to UTF8/LF if desired. */
      if (as_text)
        {
          err = svn_subst_translate_string2(edited_contents, NULL, NULL,
                                            *edited_contents, encoding, FALSE,
                                            pool, pool);
          if (err)
            {
              err = svn_error_quick_wrap
                (err,
                 _("Error normalizing edited contents to internal format"));
              goto cleanup;
            }
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
      err = svn_error_compose_create(
              err,
              svn_io_remove_file2(tmpfile_name, FALSE, pool));
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

  return svn_error_trace(err);
}
