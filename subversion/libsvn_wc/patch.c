/*
 * patch.c:  apply a patch to a working tree.
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

#include <assert.h>

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_config.h"
#include "svn_io.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_string.h"

#include "private/svn_wc_private.h"

#include "svn_private_config.h"

/* Note: although 'svn patch' application is an offline operation, we'll
 * find some ra_svn* structures below.  This is because svnpatch is made
 * of Editor Commands that ra_svn uses, thus it makes sense to use
 * those here. */


/*** Code. ***/

typedef struct {
  const svn_delta_editor_t *editor;
  void *edit_baton;
  apr_hash_t *tokens;
  apr_pool_t *pool;
  apr_pool_t *file_pool;
  int file_refs;
} ra_svn_driver_state_t;

typedef struct {
  const char *token;
  void *baton;
  svn_boolean_t is_file;
  svn_stream_t *dstream;  /* svndiff stream for apply_textdelta */
  apr_pool_t *pool;
} ra_svn_token_entry_t;

/* Store a token entry.  The token string will be copied into pool. */
static ra_svn_token_entry_t *
store_token(ra_svn_driver_state_t *ds,
            void *baton,
            const char *token,
            svn_boolean_t is_file,
            apr_pool_t *pool)
{
  ra_svn_token_entry_t *entry;

  entry = apr_palloc(pool, sizeof(*entry));
  entry->token = apr_pstrdup(pool, token);
  entry->baton = baton;
  entry->is_file = is_file;
  entry->dstream = NULL;
  entry->pool = pool;
  apr_hash_set(ds->tokens, entry->token, APR_HASH_KEY_STRING, entry);
  return entry;
}

static svn_error_t *
lookup_token(ra_svn_driver_state_t *ds,
             const char *token,
             svn_boolean_t is_file,
             ra_svn_token_entry_t **entry)
{
  *entry = apr_hash_get(ds->tokens, token, APR_HASH_KEY_STRING);
  if (!*entry || (*entry)->is_file != is_file)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Invalid file or dir token during edit"));
  return SVN_NO_ERROR;
}


static svn_error_t *
handle_open_root(apr_pool_t *pool,
                 apr_array_header_t *params,
                 ra_svn_driver_state_t *ds)
{
  apr_pool_t *subpool;
  const char *token;
  void *root_baton;

  SVN_ERR(svn_wc_parse_tuple(params, pool, "c", &token));
  subpool = svn_pool_create(ds->pool);
  SVN_CMD_ERR(ds->editor->open_root(ds->edit_baton,
                                    SVN_INVALID_REVNUM, subpool,
                                    &root_baton));
  store_token(ds, root_baton, token, FALSE, subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
handle_delete_entry(apr_pool_t *pool,
                    apr_array_header_t *params,
                    ra_svn_driver_state_t *ds)
{
  const char *path, *token;
  ra_svn_token_entry_t *entry;

  SVN_ERR(svn_wc_parse_tuple(params, pool, "cc", &path, &token));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));
  path = svn_path_canonicalize(path, pool);
  SVN_CMD_ERR(ds->editor->delete_entry(path, SVN_INVALID_REVNUM,
                                       entry->baton, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
handle_add_dir(apr_pool_t *pool,
               apr_array_header_t *params,
               ra_svn_driver_state_t *ds)
{
  const char *path, *token, *child_token, *copy_path;
  ra_svn_token_entry_t *entry;
  apr_pool_t *subpool;
  void *child_baton;

  SVN_ERR(svn_wc_parse_tuple(params, pool, "ccc(?c)", &path, &token,
                             &child_token, &copy_path));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));
  subpool = svn_pool_create(entry->pool);
  path = svn_path_canonicalize(path, pool);
  if (copy_path)
    copy_path = svn_path_canonicalize(copy_path, pool);
  SVN_CMD_ERR(ds->editor->add_directory(path, entry->baton, copy_path,
                                        SVN_INVALID_REVNUM, subpool,
                                        &child_baton));
  store_token(ds, child_baton, child_token, FALSE, subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
handle_open_dir(apr_pool_t *pool,
                apr_array_header_t *params,
                ra_svn_driver_state_t *ds)
{
  const char *path, *token, *child_token;
  ra_svn_token_entry_t *entry;
  apr_pool_t *subpool;
  void *child_baton;

  SVN_ERR(svn_wc_parse_tuple(params, pool, "ccc", &path, &token,
                             &child_token));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));
  subpool = svn_pool_create(entry->pool);
  path = svn_path_canonicalize(path, pool);
  SVN_CMD_ERR(ds->editor->open_directory(path, entry->baton,
                                         SVN_INVALID_REVNUM, subpool,
                                         &child_baton));
  store_token(ds, child_baton, child_token, FALSE, subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
handle_change_dir_prop(apr_pool_t *pool,
                       apr_array_header_t *params,
                       ra_svn_driver_state_t *ds)
{
  const char *token, *name;
  svn_string_t *value;
  ra_svn_token_entry_t *entry;

  SVN_ERR(svn_wc_parse_tuple(params, pool, "cc(?s)", &token, &name,
                             &value));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));
  SVN_CMD_ERR(ds->editor->change_dir_prop(entry->baton, name, value,
                                          entry->pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
handle_close_dir(apr_pool_t *pool,
                 apr_array_header_t *params,
                 ra_svn_driver_state_t *ds)
{
  const char *token;
  ra_svn_token_entry_t *entry;

  /* Parse and look up the directory token. */
  SVN_ERR(svn_wc_parse_tuple(params, pool, "c", &token));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));

  /* Close the directory and destroy the baton. */
  SVN_CMD_ERR(ds->editor->close_directory(entry->baton, pool));
  apr_hash_set(ds->tokens, token, APR_HASH_KEY_STRING, NULL);
  apr_pool_destroy(entry->pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
handle_add_file(apr_pool_t *pool,
                apr_array_header_t *params,
                ra_svn_driver_state_t *ds)
{
  const char *path, *token, *file_token, *copy_path;
  ra_svn_token_entry_t *entry, *file_entry;

  SVN_ERR(svn_wc_parse_tuple(params, pool, "ccc(?c)", &path, &token,
                             &file_token, &copy_path));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));
  ds->file_refs++;
  path = svn_path_canonicalize(path, pool);
  if (copy_path)
    copy_path = svn_path_canonicalize(copy_path, pool);
  file_entry = store_token(ds, NULL, file_token, TRUE, ds->file_pool);
  SVN_CMD_ERR(ds->editor->add_file(path, entry->baton, copy_path,
                                   SVN_INVALID_REVNUM, ds->file_pool,
                                   &file_entry->baton));
  return SVN_NO_ERROR;
}

static svn_error_t *
handle_open_file(apr_pool_t *pool,
                 apr_array_header_t *params,
                 ra_svn_driver_state_t *ds)
{
  const char *path, *token, *file_token;
  ra_svn_token_entry_t *entry, *file_entry;

  SVN_ERR(svn_wc_parse_tuple(params, pool, "ccc", &path, &token,
                             &file_token));
  SVN_ERR(lookup_token(ds, token, FALSE, &entry));
  ds->file_refs++;
  path = svn_path_canonicalize(path, pool);
  file_entry = store_token(ds, NULL, file_token, TRUE, ds->file_pool);
  SVN_CMD_ERR(ds->editor->open_file(path, entry->baton,
                                    SVN_INVALID_REVNUM, ds->file_pool,
                                    &file_entry->baton));
  return SVN_NO_ERROR;
}

static svn_error_t *
handle_apply_textdelta(apr_pool_t *pool,
                       apr_array_header_t *params,
                       ra_svn_driver_state_t *ds)
{
  const char *token;
  ra_svn_token_entry_t *entry;
  svn_txdelta_window_handler_t wh;
  void *wh_baton;
  char *base_checksum;

  /* Parse arguments and look up the token. */
  SVN_ERR(svn_wc_parse_tuple(params, pool, "c(?c)",
                             &token, &base_checksum));
  SVN_ERR(lookup_token(ds, token, TRUE, &entry));
  if (entry->dstream)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Apply-textdelta already active"));
  entry->pool = svn_pool_create(ds->file_pool);
  SVN_CMD_ERR(ds->editor->apply_textdelta(entry->baton, base_checksum,
                                          entry->pool, &wh, &wh_baton));
  entry->dstream = svn_txdelta_parse_svndiff(wh, wh_baton, TRUE, entry->pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
handle_textdelta_chunk(apr_pool_t *pool,
                       apr_array_header_t *params,
                       ra_svn_driver_state_t *ds)
{
  const char *token;
  ra_svn_token_entry_t *entry;
  svn_string_t *str;

  /* Parse arguments and look up the token. */
  SVN_ERR(svn_wc_parse_tuple(params, pool, "cs", &token, &str));
  SVN_ERR(lookup_token(ds, token, TRUE, &entry));
  if (!entry->dstream)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Apply-textdelta not active"));
  SVN_CMD_ERR(svn_stream_write(entry->dstream, str->data, &str->len));
  return SVN_NO_ERROR;
}

static svn_error_t *
handle_textdelta_end(apr_pool_t *pool,
                     apr_array_header_t *params,
                     ra_svn_driver_state_t *ds)
{
  const char *token;
  ra_svn_token_entry_t *entry;

  /* Parse arguments and look up the token. */
  SVN_ERR(svn_wc_parse_tuple(params, pool, "c", &token));
  SVN_ERR(lookup_token(ds, token, TRUE, &entry));
  if (!entry->dstream)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Apply-textdelta not active"));
  SVN_CMD_ERR(svn_stream_close(entry->dstream));
  entry->dstream = NULL;
  apr_pool_destroy(entry->pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
handle_change_file_prop(apr_pool_t *pool,
                        apr_array_header_t *params,
                        ra_svn_driver_state_t *ds)
{
  const char *token, *name;
  svn_string_t *value;
  ra_svn_token_entry_t *entry;

  SVN_ERR(svn_wc_parse_tuple(params, pool, "cc(?s)", &token, &name,
                                 &value));
  SVN_ERR(lookup_token(ds, token, TRUE, &entry));
  SVN_CMD_ERR(ds->editor->change_file_prop(entry->baton, name, value, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
handle_close_file(apr_pool_t *pool,
                  apr_array_header_t *params,
                  ra_svn_driver_state_t *ds)
{
  const char *token;
  ra_svn_token_entry_t *entry;
  const char *text_checksum;

  /* Parse arguments and look up the file token. */
  SVN_ERR(svn_wc_parse_tuple(params, pool, "c(?c)",
                             &token, &text_checksum));
  SVN_ERR(lookup_token(ds, token, TRUE, &entry));

  /* Close the file and destroy the baton. */
  SVN_CMD_ERR(ds->editor->close_file(entry->baton, text_checksum, pool));
  apr_hash_set(ds->tokens, token, APR_HASH_KEY_STRING, NULL);
  if (--ds->file_refs == 0)
    apr_pool_clear(ds->file_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
handle_close_edit(apr_pool_t *pool,
                  apr_array_header_t *params,
                  ra_svn_driver_state_t *ds)
{
  SVN_CMD_ERR(ds->editor->close_edit(ds->edit_baton, pool));
  return SVN_NO_ERROR;
}

static const struct {
  const char *cmd;
  svn_error_t *(*handler)(apr_pool_t *pool,
                          apr_array_header_t *params,
                          ra_svn_driver_state_t *ds);
} edit_cmds[] = {
  { "open-root",        handle_open_root },
  { "delete-entry",     handle_delete_entry },
  { "add-dir",          handle_add_dir },
  { "open-dir",         handle_open_dir },
  { "change-dir-prop",  handle_change_dir_prop },
  { "close-dir",        handle_close_dir },
  { "add-file",         handle_add_file },
  { "open-file",        handle_open_file },
  { "apply-textdelta",  handle_apply_textdelta },
  { "textdelta-chunk",  handle_textdelta_chunk },
  { "textdelta-end",    handle_textdelta_end },
  { "change-file-prop", handle_change_file_prop },
  { "close-file",       handle_close_file },
  { "close-edit",       handle_close_edit },
  { NULL }
};


svn_error_t *
svn_wc_apply_svnpatch(apr_file_t *decoded_patch_file,
                      const svn_delta_editor_t *diff_editor,
                      void *diff_edit_baton,
                      apr_pool_t *pool)
{
  svn_stream_t *patch_stream;
  ra_svn_driver_state_t state;
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *cmd;
  int i;
  svn_error_t *err;
  apr_array_header_t *params;

  patch_stream = svn_stream_from_aprfile2(decoded_patch_file, FALSE, pool);

  state.editor = diff_editor;
  state.edit_baton = diff_edit_baton;
  state.tokens = apr_hash_make(pool);
  state.pool = pool;
  state.file_pool = svn_pool_create(pool);
  state.file_refs = 0;

  while (1)
    {
      apr_pool_clear(subpool);
      SVN_ERR(svn_wc_read_tuple(patch_stream, subpool, "wl", &cmd, &params));
      for (i = 0; edit_cmds[i].cmd; i++)
        if (strcmp(cmd, edit_cmds[i].cmd) == 0)
          break;
      if (edit_cmds[i].cmd)
        err = (*edit_cmds[i].handler)(subpool, params, &state);
      else
        {
          err = svn_error_createf(SVN_ERR_RA_SVN_UNKNOWN_CMD, NULL,
                                  _("Unknown command '%s'"), cmd);
          err = svn_error_create(SVN_ERR_RA_SVN_CMD_ERR, err, NULL);
        }
      SVN_ERR(err);
      /* TODO: handle & wrap errors */

      if (strcmp(edit_cmds[i].cmd, "close-edit") == 0)
        break;
    }

  apr_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_apply_unidiff(const char *patch_path,
                     svn_boolean_t force,
                     apr_file_t *outfile,
                     apr_file_t *errfile,
                     apr_hash_t *config,
                     apr_pool_t *pool)
{
  const char *patch_cmd = NULL;
  const char **patch_cmd_args;
  const char *patch_cmd_tmp = NULL;
  int exitcode = 0;
  apr_exit_why_e exitwhy = 0;
  svn_boolean_t patch_bin_guess = TRUE; 
  apr_file_t *patchfile;
  int nargs = 3; /* the command, the prefix arg and NULL at least */
  int i = 0;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_boolean_t dry_run = FALSE; /* disable dry_run for now */

  if (force)
    ++nargs;

  if (dry_run)
    ++nargs;

  patch_cmd_args = apr_palloc(subpool, nargs * sizeof(char *));

  if (config)
    {
      svn_config_t *cfg = apr_hash_get(config,
                                       SVN_CONFIG_CATEGORY_CONFIG,
                                       APR_HASH_KEY_STRING);
      svn_config_get(cfg, &patch_cmd_tmp, SVN_CONFIG_SECTION_HELPERS,
                     SVN_CONFIG_OPTION_PATCH_CMD, NULL);
    }

  if (patch_cmd_tmp)
    {
      patch_bin_guess = FALSE;
      SVN_ERR(svn_path_cstring_to_utf8(&patch_cmd, patch_cmd_tmp,
                                       subpool));
    }
  else
    patch_cmd = "patch";

  patch_cmd_args[i++] = patch_cmd;
  patch_cmd_args[i++] = "-p0"; /* TODO: make it smarter in detecting CWD */

  if (force)
    patch_cmd_args[i++] = "--force";

  if (dry_run)
    patch_cmd_args[i++] = "--dry-run";

  patch_cmd_args[i++] = NULL;

  assert(i == nargs);

  /* We want to feed the external program's stdin with the patch itself. */
  SVN_ERR(svn_io_file_open(&patchfile, patch_path,
                           APR_READ, APR_OS_DEFAULT, subpool));

  /* Now run the external program.  The parent process should close
   * opened pipes/files. */
  SVN_ERR(svn_io_run_cmd(".", patch_cmd, patch_cmd_args,
                         &exitcode, &exitwhy, TRUE, patchfile, outfile,
                         errfile, subpool));

  /* This is where we have to make the assumption that if the exitcode
   * isn't 0 nor 1 then the external program got into trouble or wasn't
   * even executed--command not found (see below).  Basically we're
   * trying to stick with patch(1) behaviour as stated in the man page:
   * "patch's exit  status is 0 if all hunks are applied successfully, 1
   * if some hunks cannot be applied, and 2 if there is more serious
   * trouble." */
  if (exitcode != 0 && exitcode != 1)
    {
      /* This is the case when we're trying to execute 'patch' and got
       * some weird exitcode.
       * XXX: I haven't figured out how to check against the 'command
       * not found' error, which returns exitcode == 255.  Is there a
       * macro somewhere to compare with?  Let's use > 2 for now. */
      if (patch_bin_guess && exitcode > 2)
        return svn_error_create
                (SVN_ERR_EXTERNAL_PROGRAM_MISSING, NULL, NULL);
      else
        /* patch(1) uses exitcode 2 along with the message "Only garbage
         * was found in the patch input.".  This falls here along with
         * every other errors. */
        return svn_error_createf
                (SVN_ERR_EXTERNAL_PROGRAM, NULL,
                 _("'%s' returned error exitcode %d"),
                 svn_path_local_style(patch_cmd, subpool),
                 exitcode);
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}
