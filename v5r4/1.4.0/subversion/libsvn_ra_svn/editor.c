/*
 * editor.c :  Driving and consuming an editor across an svn connection
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



#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_strings.h>
#include <apr_md5.h>

#include <assert.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_ra_svn.h"
#include "svn_pools.h"
#include "svn_private_config.h"

#include "ra_svn.h"

/*
 * Both the client and server in the svn protocol need to drive and
 * consume editors.  For a commit, the client drives and the server
 * consumes; for an update/switch/status/diff, the server drives and
 * the client consumes.  This file provides a generic framework for
 * marshalling and unmarshalling editor operations over an svn
 * connection; both ends are useful for both server and client.
 */

typedef struct {
  svn_ra_svn_conn_t *conn;
  svn_ra_svn_edit_callback callback;    /* Called on successful completion. */
  void *callback_baton;
  int next_token;
} ra_svn_edit_baton_t;

/* Works for both directories and files. */
typedef struct {
  svn_ra_svn_conn_t *conn;
  apr_pool_t *pool;
  ra_svn_edit_baton_t *eb;
  const char *token;
} ra_svn_baton_t;

typedef struct {
  const svn_delta_editor_t *editor;
  void *edit_baton;
  apr_hash_t *tokens;
  svn_boolean_t *aborted;
  apr_pool_t *pool;
  svn_boolean_t for_replay;
} ra_svn_driver_state_t;

typedef struct {
  const char *token;
  void *baton;
  svn_error_t *err;                     /* Tracks delayed errors. */
  apr_pool_t *pool;
} ra_svn_token_entry_t;

/* --- CONSUMING AN EDITOR BY PASSING EDIT OPERATIONS OVER THE NET --- */

static const char *make_token(char type, ra_svn_edit_baton_t *eb,
                              apr_pool_t *pool)
{
  return apr_psprintf(pool, "%c%d", type, eb->next_token++);
}

static ra_svn_baton_t *ra_svn_make_baton(svn_ra_svn_conn_t *conn,
                                         apr_pool_t *pool,
                                         ra_svn_edit_baton_t *eb,
                                         const char *token)
{
  ra_svn_baton_t *b;

  b = apr_palloc(pool, sizeof(*b));
  b->conn = conn;
  b->pool = pool;
  b->eb = eb;
  b->token = token;
  return b;
}

static svn_error_t *ra_svn_target_rev(void *edit_baton, svn_revnum_t rev,
                                      apr_pool_t *pool)
{
  ra_svn_edit_baton_t *eb = edit_baton;

  SVN_ERR(svn_ra_svn_write_cmd(eb->conn, pool, "target-rev", "r", rev));
  SVN_ERR(svn_ra_svn_read_cmd_response(eb->conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_open_root(void *edit_baton, svn_revnum_t rev,
                                     apr_pool_t *pool, void **root_baton)
{
  ra_svn_edit_baton_t *eb = edit_baton;
  const char *token = make_token('d', eb, pool);

  SVN_ERR(svn_ra_svn_write_cmd(eb->conn, pool, "open-root", "(?r)c", rev,
                               token));
  SVN_ERR(svn_ra_svn_read_cmd_response(eb->conn, pool, ""));
  *root_baton = ra_svn_make_baton(eb->conn, pool, eb, token);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_delete_entry(const char *path, svn_revnum_t rev,
                                        void *parent_baton, apr_pool_t *pool)
{
  ra_svn_baton_t *b = parent_baton;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "delete-entry", "c(?r)c",
                               path, rev, b->token));
  SVN_ERR(svn_ra_svn_read_cmd_response(b->conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_add_dir(const char *path, void *parent_baton,
                                   const char *copy_path,
                                   svn_revnum_t copy_rev,
                                   apr_pool_t *pool, void **child_baton)
{
  ra_svn_baton_t *b = parent_baton;
  const char *token = make_token('d', b->eb, pool);

  assert((copy_path && SVN_IS_VALID_REVNUM(copy_rev))
         || (!copy_path && !SVN_IS_VALID_REVNUM(copy_rev)));
  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "add-dir", "ccc(?cr)", path,
                               b->token, token, copy_path, copy_rev));
  SVN_ERR(svn_ra_svn_read_cmd_response(b->conn, pool, ""));
  *child_baton = ra_svn_make_baton(b->conn, pool, b->eb, token);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_open_dir(const char *path, void *parent_baton,
                                    svn_revnum_t rev, apr_pool_t *pool,
                                    void **child_baton)
{
  ra_svn_baton_t *b = parent_baton;
  const char *token = make_token('d', b->eb, pool);

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "open-dir", "ccc(?r)",
                               path, b->token, token, rev));
  SVN_ERR(svn_ra_svn_read_cmd_response(b->conn, pool, ""));
  *child_baton = ra_svn_make_baton(b->conn, pool, b->eb, token);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_change_dir_prop(void *dir_baton, const char *name,
                                           const svn_string_t *value,
                                           apr_pool_t *pool)
{
  ra_svn_baton_t *b = dir_baton;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "change-dir-prop", "cc(?s)",
                               b->token, name, value));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_close_dir(void *dir_baton, apr_pool_t *pool)
{
  ra_svn_baton_t *b = dir_baton;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "close-dir", "c", b->token));
  SVN_ERR(svn_ra_svn_read_cmd_response(b->conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_add_file(const char *path,
                                    void *parent_baton,
                                    const char *copy_path,
                                    svn_revnum_t copy_rev,
                                    apr_pool_t *pool,
                                    void **file_baton)
{
  ra_svn_baton_t *b = parent_baton;
  const char *token = make_token('c', b->eb, pool);

  assert((copy_path && SVN_IS_VALID_REVNUM(copy_rev))
         || (!copy_path && !SVN_IS_VALID_REVNUM(copy_rev)));
  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "add-file", "ccc(?cr)", path,
                               b->token, token, copy_path, copy_rev));
  *file_baton = ra_svn_make_baton(b->conn, pool, b->eb, token);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_open_file(const char *path,
                                     void *parent_baton,
                                     svn_revnum_t rev,
                                     apr_pool_t *pool,
                                     void **file_baton)
{
  ra_svn_baton_t *b = parent_baton;
  const char *token = make_token('c', b->eb, pool);

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "open-file", "ccc(?r)",
                               path, b->token, token, rev));
  *file_baton = ra_svn_make_baton(b->conn, pool, b->eb, token);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_svndiff_handler(void *baton, const char *data,
                                           apr_size_t *len)
{
  ra_svn_baton_t *b = baton;
  svn_string_t str;

  str.data = data;
  str.len = *len;
  return svn_ra_svn_write_string(b->conn, b->pool, &str);
}

static svn_error_t *ra_svn_svndiff_close_handler(void *baton)
{
  ra_svn_baton_t *b = baton;

  SVN_ERR(svn_ra_svn_write_cstring(b->conn, b->pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_apply_textdelta(void *file_baton,
                                           const char *base_checksum,
                                           apr_pool_t *pool,
                                           svn_txdelta_window_handler_t *wh,
                                           void **wh_baton)
{
  ra_svn_baton_t *b = file_baton;
  svn_stream_t *diff_stream;

  /* Tell the other side we're starting a text delta. */
  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "apply-textdelta", "c(?c)",
                               b->token, base_checksum));

  /* Transform the window stream to an svndiff stream.  Reuse the
   * file baton for the stream handler, since it has all the
   * needed information. */
  diff_stream = svn_stream_create(b, pool);
  svn_stream_set_write(diff_stream, ra_svn_svndiff_handler);
  svn_stream_set_close(diff_stream, ra_svn_svndiff_close_handler);
  if (svn_ra_svn_has_capability(b->conn, SVN_RA_SVN_CAP_SVNDIFF1))
    svn_txdelta_to_svndiff2(wh, wh_baton, diff_stream, 1, pool);
  else
    svn_txdelta_to_svndiff2(wh, wh_baton, diff_stream, 0, pool);
  return SVN_NO_ERROR;
}
  
static svn_error_t *ra_svn_change_file_prop(void *file_baton,
                                            const char *name,
                                            const svn_string_t *value,
                                            apr_pool_t *pool)
{
  ra_svn_baton_t *b = file_baton;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "change-file-prop", "cc(?s)",
                               b->token, name, value));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_close_file(void *file_baton,
                                      const char *text_checksum,
                                      apr_pool_t *pool)
{
  ra_svn_baton_t *b = file_baton;

  SVN_ERR(svn_ra_svn_write_cmd(b->conn, pool, "close-file", "c(?c)",
                               b->token, text_checksum));
  SVN_ERR(svn_ra_svn_read_cmd_response(b->conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_close_edit(void *edit_baton, apr_pool_t *pool)
{
  ra_svn_edit_baton_t *eb = edit_baton;

  SVN_ERR(svn_ra_svn_write_cmd(eb->conn, pool, "close-edit", ""));
  SVN_ERR(svn_ra_svn_read_cmd_response(eb->conn, pool, ""));
  if (eb->callback)
    SVN_ERR(eb->callback(eb->callback_baton));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_abort_edit(void *edit_baton, apr_pool_t *pool)
{
  ra_svn_edit_baton_t *eb = edit_baton;

  SVN_ERR(svn_ra_svn_write_cmd(eb->conn, pool, "abort-edit", ""));
  SVN_ERR(svn_ra_svn_read_cmd_response(eb->conn, pool, ""));
  return SVN_NO_ERROR;
}

void svn_ra_svn_get_editor(const svn_delta_editor_t **editor,
                           void **edit_baton, svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool, svn_ra_svn_edit_callback callback,
                           void *callback_baton)
{
  svn_delta_editor_t *ra_svn_editor;
  ra_svn_edit_baton_t *eb;

  if (svn_ra_svn_has_capability(conn, SVN_RA_SVN_CAP_EDIT_PIPELINE))
    {
      svn_ra_svn__get_editorp(editor, edit_baton, conn, pool, callback,
                              callback_baton);
      return;
    }

  eb = apr_palloc(pool, sizeof(*eb));
  eb->conn = conn;
  eb->callback = callback;
  eb->callback_baton = callback_baton;
  eb->next_token = 0;

  ra_svn_editor = svn_delta_default_editor(pool);
  ra_svn_editor->set_target_revision = ra_svn_target_rev;
  ra_svn_editor->open_root = ra_svn_open_root;
  ra_svn_editor->delete_entry = ra_svn_delete_entry;
  ra_svn_editor->add_directory = ra_svn_add_dir;
  ra_svn_editor->open_directory = ra_svn_open_dir;
  ra_svn_editor->change_dir_prop = ra_svn_change_dir_prop;
  ra_svn_editor->close_directory = ra_svn_close_dir;
  ra_svn_editor->add_file = ra_svn_add_file;
  ra_svn_editor->open_file = ra_svn_open_file;
  ra_svn_editor->apply_textdelta = ra_svn_apply_textdelta;
  ra_svn_editor->change_file_prop = ra_svn_change_file_prop;
  ra_svn_editor->close_file = ra_svn_close_file;
  ra_svn_editor->close_edit = ra_svn_close_edit;
  ra_svn_editor->abort_edit = ra_svn_abort_edit;

  *editor = ra_svn_editor;
  *edit_baton = eb;
}

/* --- DRIVING AN EDITOR --- */

static apr_status_t clear_token_err(void *arg)
{
  ra_svn_token_entry_t *entry = arg;

  svn_error_clear(entry->err);
  return APR_SUCCESS;
}

/* Store a token entry.  The token string will be copied into pool. */
static ra_svn_token_entry_t *store_token(ra_svn_driver_state_t *ds,
                                         void *baton, const char *token,
                                         apr_pool_t *pool)
{
  ra_svn_token_entry_t *entry;

  entry = apr_palloc(pool, sizeof(*entry));
  entry->token = apr_pstrdup(pool, token);
  entry->baton = baton;
  entry->err = NULL;
  entry->pool = pool;
  apr_hash_set(ds->tokens, entry->token, APR_HASH_KEY_STRING, entry);
  apr_pool_cleanup_register(pool, entry, clear_token_err,
                            apr_pool_cleanup_null);
  return entry;
}

static svn_error_t *lookup_token(ra_svn_driver_state_t *ds, const char *token,
                                 ra_svn_token_entry_t **entry,
                                 apr_pool_t *pool)

{
  *entry = apr_hash_get(ds->tokens, token, APR_HASH_KEY_STRING);
  if (!*entry)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Invalid file or dir token during edit"));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_handle_target_rev(svn_ra_svn_conn_t *conn,
                                             apr_pool_t *pool,
                                             apr_array_header_t *params,
                                             void *baton)
{
  ra_svn_driver_state_t *ds = baton;
  svn_revnum_t rev;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "r", &rev));
  SVN_CMD_ERR(ds->editor->set_target_revision(ds->edit_baton, rev, pool));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_handle_open_root(svn_ra_svn_conn_t *conn,
                                            apr_pool_t *pool,
                                            apr_array_header_t *params,
                                            void *baton)
{
  ra_svn_driver_state_t *ds = baton;
  svn_revnum_t rev;
  apr_pool_t *subpool;
  const char *token;
  void *root_baton;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "(?r)c", &rev, &token));
  subpool = svn_pool_create(ds->pool);
  SVN_CMD_ERR(ds->editor->open_root(ds->edit_baton, rev, subpool,
                                    &root_baton));
  store_token(ds, root_baton, token, subpool);
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_handle_delete_entry(svn_ra_svn_conn_t *conn,
                                               apr_pool_t *pool,
                                               apr_array_header_t *params,
                                               void *baton)
{
  ra_svn_driver_state_t *ds = baton;
  const char *path, *token;
  svn_revnum_t rev;
  ra_svn_token_entry_t *entry;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c(?r)c", &path, &rev, &token));
  SVN_ERR(lookup_token(ds, token, &entry, pool));
  path = svn_path_canonicalize(path, pool);
  SVN_CMD_ERR(ds->editor->delete_entry(path, rev, entry->baton, entry->pool));
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_handle_add_dir(svn_ra_svn_conn_t *conn,
                                          apr_pool_t *pool,
                                          apr_array_header_t *params,
                                          void *baton)
{
  ra_svn_driver_state_t *ds = baton;
  const char *path, *token, *child_token, *copy_path;
  svn_revnum_t copy_rev;
  ra_svn_token_entry_t *entry;
  apr_pool_t *subpool;
  void *child_baton;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "ccc(?cr)", &path, &token,
                                 &child_token, &copy_path, &copy_rev));
  SVN_ERR(lookup_token(ds, token, &entry, pool));
  subpool = svn_pool_create(entry->pool);
  path = svn_path_canonicalize(path, pool);
  if (copy_path)
    copy_path = svn_path_canonicalize(copy_path, pool);
  SVN_CMD_ERR(ds->editor->add_directory(path, entry->baton, copy_path,
                                        copy_rev, subpool, &child_baton));
  store_token(ds, child_baton, child_token, subpool);
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_handle_open_dir(svn_ra_svn_conn_t *conn,
                                           apr_pool_t *pool,
                                           apr_array_header_t *params,
                                           void *baton)
{
  ra_svn_driver_state_t *ds = baton;
  const char *path, *token, *child_token;
  svn_revnum_t rev;
  ra_svn_token_entry_t *entry;
  apr_pool_t *subpool;
  void *child_baton;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "ccc(?r)", &path, &token,
                                 &child_token, &rev));
  SVN_ERR(lookup_token(ds, token, &entry, pool));
  subpool = svn_pool_create(entry->pool);
  path = svn_path_canonicalize(path, pool);
  SVN_CMD_ERR(ds->editor->open_directory(path, entry->baton, rev, subpool,
                                         &child_baton));
  store_token(ds, child_baton, child_token, subpool);
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_handle_change_dir_prop(svn_ra_svn_conn_t *conn,
                                                  apr_pool_t *pool,
                                                  apr_array_header_t *params,
                                                  void *baton)
{
  ra_svn_driver_state_t *ds = baton;
  const char *token, *name;
  svn_string_t *value;
  ra_svn_token_entry_t *entry;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "cc(?s)", &token, &name,
                                 &value));
  SVN_ERR(lookup_token(ds, token, &entry, pool));
  if (!entry->err)
    entry->err = ds->editor->change_dir_prop(entry->baton, name, value,
                                             entry->pool);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_handle_close_dir(svn_ra_svn_conn_t *conn,
                                            apr_pool_t *pool,
                                            apr_array_header_t *params,
                                            void *baton)
{
  ra_svn_driver_state_t *ds = baton;
  const char *token;
  ra_svn_token_entry_t *entry;

  /* Parse and look up the directory token. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c", &token));
  SVN_ERR(lookup_token(ds, token, &entry, pool));

  /* Return any delayed errors. */
  apr_pool_cleanup_kill(entry->pool, entry, clear_token_err);
  SVN_CMD_ERR(entry->err);

  /* Close the directory and destroy the baton. */
  SVN_CMD_ERR(ds->editor->close_directory(entry->baton, pool));
  apr_hash_set(ds->tokens, token, APR_HASH_KEY_STRING, NULL);
  apr_pool_destroy(entry->pool);
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_handle_add_file(svn_ra_svn_conn_t *conn,
                                           apr_pool_t *pool,
                                           apr_array_header_t *params,
                                           void *baton)
{
  ra_svn_driver_state_t *ds = baton;
  const char *path, *token, *file_token, *copy_path;
  svn_revnum_t copy_rev;
  ra_svn_token_entry_t *entry, *file_entry;
  apr_pool_t *subpool;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "ccc(?cr)", &path, &token,
                                 &file_token, &copy_path, &copy_rev));
  SVN_ERR(lookup_token(ds, token, &entry, pool));

  /* File may outlive parent directory, so use ds->pool here. */
  subpool = svn_pool_create(ds->pool);
  path = svn_path_canonicalize(path, pool);
  if (copy_path)
    copy_path = svn_path_canonicalize(copy_path, pool);
  file_entry = store_token(ds, NULL, file_token, subpool);
  file_entry->err = ds->editor->add_file(path, entry->baton, copy_path,
                                         copy_rev, subpool,
                                         &file_entry->baton);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_handle_open_file(svn_ra_svn_conn_t *conn,
                                            apr_pool_t *pool,
                                            apr_array_header_t *params,
                                            void *baton)
{
  ra_svn_driver_state_t *ds = baton;
  const char *path, *token, *file_token;
  svn_revnum_t rev;
  ra_svn_token_entry_t *entry, *file_entry;
  apr_pool_t *subpool;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "ccc(?r)", &path, &token,
                                 &file_token, &rev));
  SVN_ERR(lookup_token(ds, token, &entry, pool));

  /* File may outlive parent directory, so use ds->pool here. */
  subpool = svn_pool_create(ds->pool);
  path = svn_path_canonicalize(path, pool);
  file_entry = store_token(ds, NULL, file_token, subpool);
  file_entry->err = ds->editor->open_file(path, entry->baton, rev, subpool,
                                          &file_entry->baton);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_handle_apply_textdelta(svn_ra_svn_conn_t *conn,
                                                  apr_pool_t *pool,
                                                  apr_array_header_t *params,
                                                  void *baton)
{
  ra_svn_driver_state_t *ds = baton;
  const char *token;
  ra_svn_token_entry_t *entry;
  svn_txdelta_window_handler_t wh;
  void *wh_baton;
  svn_stream_t *stream;
  apr_pool_t *subpool;
  svn_ra_svn_item_t *item;
  char *base_checksum;

  /* Parse arguments and look up the token. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c(?c)",
                                 &token, &base_checksum));
  SVN_ERR(lookup_token(ds, token, &entry, pool));

  if (!entry->err)
    entry->err = ds->editor->apply_textdelta(entry->baton, base_checksum, pool,
                                             &wh, &wh_baton);

  if (!entry->err)
    stream = svn_txdelta_parse_svndiff(wh, wh_baton, TRUE, entry->pool);
  subpool = svn_pool_create(entry->pool);
  while (1)
    {
      apr_pool_clear(subpool);
      SVN_ERR(svn_ra_svn_read_item(conn, subpool, &item));
      if (item->kind != SVN_RA_SVN_STRING)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Non-string as part of text delta"));
      if (item->u.string->len == 0)
          break;
      if (!entry->err)
        entry->err = svn_stream_write(stream, item->u.string->data,
                                      &item->u.string->len);
    }
  if (!entry->err)
    entry->err = svn_stream_close(stream);
  apr_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_handle_change_file_prop(svn_ra_svn_conn_t *conn,
                                                   apr_pool_t *pool,
                                                   apr_array_header_t *params,
                                                   void *baton)
{
  ra_svn_driver_state_t *ds = baton;
  const char *token, *name;
  svn_string_t *value;
  ra_svn_token_entry_t *entry;

  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "cc(?s)", &token, &name,
                                 &value));
  SVN_ERR(lookup_token(ds, token, &entry, pool));
  if (!entry->err)
    entry->err = ds->editor->change_file_prop(entry->baton, name, value,
                                              entry->pool);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_handle_close_file(svn_ra_svn_conn_t *conn,
                                             apr_pool_t *pool,
                                             apr_array_header_t *params,
                                             void *baton)
{
  ra_svn_driver_state_t *ds = baton;
  const char *token;
  ra_svn_token_entry_t *entry;
  const char *text_checksum;

  /* Parse arguments and look up the file token. */
  SVN_ERR(svn_ra_svn_parse_tuple(params, pool, "c(?c)",
                                 &token, &text_checksum));
  SVN_ERR(lookup_token(ds, token, &entry, pool));

  /* Return any delayed errors. */
  apr_pool_cleanup_kill(entry->pool, entry, clear_token_err);
  SVN_CMD_ERR(entry->err);

  /* Close the file and destroy the baton. */
  SVN_CMD_ERR(ds->editor->close_file(entry->baton, text_checksum, pool));
  apr_hash_set(ds->tokens, token, APR_HASH_KEY_STRING, NULL);
  apr_pool_destroy(entry->pool);
  SVN_ERR(svn_ra_svn_write_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_handle_close_edit(svn_ra_svn_conn_t *conn,
                                             apr_pool_t *pool,
                                             apr_array_header_t *params,
                                             void *baton)
{
  ra_svn_driver_state_t *ds = baton;
  svn_error_t *err;

  err = ds->editor->close_edit(ds->edit_baton, pool);
  if (ds->aborted)
    *ds->aborted = (err != SVN_NO_ERROR);
  SVN_CMD_ERR(err);
  return svn_ra_svn_write_cmd_response(conn, pool, "");
}

static svn_error_t *ra_svn_handle_abort_edit(svn_ra_svn_conn_t *conn,
                                             apr_pool_t *pool,
                                             apr_array_header_t *params,
                                             void *baton)
{
  ra_svn_driver_state_t *ds = baton;

  if (ds->aborted)
    *ds->aborted = TRUE;
  SVN_CMD_ERR(ds->editor->abort_edit(ds->edit_baton, pool));
  return svn_ra_svn_write_cmd_response(conn, pool, "");
}

static svn_error_t *ra_svn_handle_finish_replay(svn_ra_svn_conn_t *conn,
                                                apr_pool_t *pool,
                                                apr_array_header_t *params,
                                                void *baton)
{
  ra_svn_driver_state_t *ds = baton;
  if (!ds->for_replay)
    return svn_error_createf
      (SVN_ERR_RA_SVN_UNKNOWN_CMD, NULL,
       _("Command 'finish-replay' invalid outside of replays"));
  return SVN_NO_ERROR;
}

static const svn_ra_svn_cmd_entry_t ra_svn_edit_commands[] = {
  { "target-rev",       ra_svn_handle_target_rev },
  { "open-root",        ra_svn_handle_open_root },
  { "delete-entry",     ra_svn_handle_delete_entry },
  { "add-dir",          ra_svn_handle_add_dir },
  { "open-dir",         ra_svn_handle_open_dir },
  { "change-dir-prop",  ra_svn_handle_change_dir_prop },
  { "close-dir",        ra_svn_handle_close_dir },
  { "add-file",         ra_svn_handle_add_file },
  { "open-file",        ra_svn_handle_open_file },
  { "apply-textdelta",  ra_svn_handle_apply_textdelta },
  { "change-file-prop", ra_svn_handle_change_file_prop },
  { "close-file",       ra_svn_handle_close_file },
  { "close-edit",       ra_svn_handle_close_edit, TRUE },
  { "abort-edit",       ra_svn_handle_abort_edit, TRUE },
  { "finish-replay",    ra_svn_handle_finish_replay, TRUE },
  { NULL }
};

svn_error_t *svn_ra_svn_drive_editor2(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool,
                                      const svn_delta_editor_t *editor,
                                      void *edit_baton,
                                      svn_boolean_t *aborted,
                                      svn_boolean_t for_replay)
{
  ra_svn_driver_state_t state;

  if (svn_ra_svn_has_capability(conn, SVN_RA_SVN_CAP_EDIT_PIPELINE))
    return svn_ra_svn__drive_editorp(conn, pool, editor, edit_baton, aborted,
                                     for_replay);

  state.editor = editor;
  state.edit_baton = edit_baton;
  state.tokens = apr_hash_make(pool);
  state.aborted = aborted;
  state.pool = pool;
  state.for_replay = for_replay;
  return svn_ra_svn_handle_commands(conn, pool, ra_svn_edit_commands, &state);
}

svn_error_t *svn_ra_svn_drive_editor(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                     const svn_delta_editor_t *editor,
                                     void *edit_baton,
                                     svn_boolean_t *aborted)
{
  return svn_ra_svn_drive_editor2(conn,
                                  pool,
                                  editor,
                                  edit_baton,
                                  aborted,
                                  FALSE);
}
