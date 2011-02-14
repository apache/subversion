/*
 * update.c :  entry point for update RA functions for ra_serf
 *
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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

#include <apr_uri.h>

#include <expat.h>

#include <serf.h>

#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_dav.h"
#include "svn_xml.h"
#include "svn_config.h"
#include "svn_delta.h"
#include "svn_version.h"
#include "svn_path.h"
#include "svn_base64.h"
#include "svn_props.h"

#include "svn_private_config.h"

#include "ra_serf.h"
#include "../libsvn_ra/ra_loader.h"


/*
 * This enum represents the current state of our XML parsing for a REPORT.
 *
 * A little explanation of how the parsing works.  Every time we see
 * an open-directory tag, we enter the OPEN_DIR state.  Likewise, for
 * add-directory, open-file, etc.  When we see the closing variant of the
 * open-directory tag, we'll 'pop' out of that state.
 *
 * Each state has a pool associated with it that can have temporary
 * allocations that will live as long as the tag is opened.  Once
 * the tag is 'closed', the pool will be reused.
 */
typedef enum {
    NONE = 0,
    OPEN_DIR,
    ADD_DIR,
    OPEN_FILE,
    ADD_FILE,
    PROP,
    IGNORE_PROP_NAME,
    NEED_PROP_NAME,
} report_state_e;

/* Forward-declare our report context. */
typedef struct report_context_t report_context_t;

/*
 * This structure represents the information for a directory.
 */
typedef struct report_dir_t
{
  /* Our parent directory.
   *
   * This value is NULL when we are the root.
   */
  struct report_dir_t *parent_dir;

  apr_pool_t *pool;

  /* Pointer back to our original report context. */
  report_context_t *report_context;

  /* Our name sans any parents. */
  const char *base_name;

  /* the expanded directory name (including all parent names) */
  const char *name;

  /* temporary path buffer for this directory. */
  svn_stringbuf_t *name_buf;

  /* the canonical url for this directory. */
  const char *url;

  /* Our base revision - SVN_INVALID_REVNUM if we're adding this dir. */
  svn_revnum_t base_rev;

  /* The target revision we're retrieving. */
  svn_revnum_t target_rev;

  /* controlling dir baton - this is only created in open_dir() */
  void *dir_baton;
  apr_pool_t *dir_baton_pool;

  /* Our master update editor and baton. */
  const svn_delta_editor_t *update_editor;
  void *update_baton;

  /* How many references to this directory do we still have open? */
  apr_size_t ref_count;

  /* Namespace list allocated out of this ->pool. */
  svn_ra_serf__ns_t *ns_list;

  /* hashtable for all of the properties (shared within a dir) */
  apr_hash_t *props;

  /* hashtable for all to-be-removed properties (shared within a dir) */
  apr_hash_t *removed_props;

  /* The propfind request for our current directory */
  svn_ra_serf__propfind_context_t *propfind;

  /* Has the server told us to fetch the dir props? */
  svn_boolean_t fetch_props;

  /* Have we closed the directory tag (meaning no more additions)? */
  svn_boolean_t tag_closed;

  /* The children of this directory  */
  struct report_dir_t *children;

  /* The next sibling of this directory */
  struct report_dir_t *sibling;
} report_dir_t;

/*
 * This structure represents the information for a file.
 *
 * A directory may have a report_info_t associated with it as well.
 *
 * This structure is created as we parse the REPORT response and
 * once the element is completed, we create a report_fetch_t structure
 * to give to serf to retrieve this file.
 */
typedef struct report_info_t
{
  apr_pool_t *pool;

  /* The enclosing directory.
   *
   * If this structure refers to a directory, the dir it points to will be
   * itself.
   */
  report_dir_t *dir;

  /* Our name sans any directory info. */
  const char *base_name;

  /* the expanded file name (including all parent directory names) */
  const char *name;

  /* file name buffer */
  svn_stringbuf_t *name_buf;

  /* the canonical url for this file. */
  const char *url;

  /* lock token, if we had one to start off with. */
  const char *lock_token;

  /* Our base revision - SVN_INVALID_REVNUM if we're adding this file. */
  svn_revnum_t base_rev;

  /* The target revision we're retrieving. */
  svn_revnum_t target_rev;

  /* our delta base, if present (NULL if we're adding the file) */
  const svn_string_t *delta_base;

  /* Path of original item if add with history */
  const char *copyfrom_path;

  /* Revision of original item if add with history */
  svn_revnum_t copyfrom_rev;

  /* The propfind request for our current file (if present) */
  svn_ra_serf__propfind_context_t *propfind;

  /* Has the server told us to fetch the file props? */
  svn_boolean_t fetch_props;

  /* Has the server told us to go fetch - only valid if we had it already */
  svn_boolean_t fetch_file;

  /* The properties for this file */
  apr_hash_t *props;

  /* pool passed to update->add_file, etc. */
  apr_pool_t *editor_pool;

  /* controlling file_baton and textdelta handler */
  void *file_baton;
  const char *base_checksum;
  svn_txdelta_window_handler_t textdelta;
  void *textdelta_baton;

  /* Checksum for close_file */
  const char *final_checksum;

  /* temporary property for this file which is currently being parsed
   * It will eventually be stored in our parent directory's property hash.
   */
  const char *prop_ns;
  const char *prop_name;
  const char *prop_val;
  apr_size_t prop_val_len;
  const char *prop_encoding;
} report_info_t;

/*
 * This structure represents a single request to GET (fetch) a file with
 * its associated Serf session/connection.
 */
typedef struct report_fetch_t {
  /* Our pool. */
  apr_pool_t *pool;

  /* Non-NULL if we received an error during processing. */
  svn_error_t *err;

  /* The session we should use to fetch the file. */
  svn_ra_serf__session_t *sess;

  /* The connection we should use to fetch file. */
  svn_ra_serf__connection_t *conn;

  /* Stores the information for the file we want to fetch. */
  report_info_t *info;

  /* Have we read our response headers yet? */
  svn_boolean_t read_headers;

  /* This flag is set when our response is aborted before we reach the
   * end and we decide to requeue this request.
   */
  svn_boolean_t aborted_read;
  apr_off_t aborted_read_size;

  /* This is the amount of data that we have read so far. */
  apr_off_t read_size;

  /* If we're receiving an svndiff, this will be non-NULL. */
  svn_stream_t *delta_stream;

  /* If we're writing this file to a stream, this will be non-NULL. */
  svn_stream_t *target_stream;

  /* Are we done fetching this file? */
  svn_boolean_t done;
  svn_ra_serf__list_t **done_list;
  svn_ra_serf__list_t done_item;

} report_fetch_t;

/*
 * The master structure for a REPORT request and response.
 */
struct report_context_t {
  apr_pool_t *pool;

  svn_ra_serf__session_t *sess;
  svn_ra_serf__connection_t *conn;

  /* Source path and destination path */
  const char *source;
  const char *destination;

  /* Our update target. */
  const char *update_target;

  /* What is the target revision that we want for this REPORT? */
  svn_revnum_t target_rev;

  /* Have we been asked to ignore ancestry or textdeltas? */
  svn_boolean_t ignore_ancestry;
  svn_boolean_t text_deltas;

  /* Do we want the server to send copyfrom args or not? */
  svn_boolean_t send_copyfrom_args;

  /* Path -> lock token mapping. */
  apr_hash_t *lock_path_tokens;

  /* Our master update editor and baton. */
  const svn_delta_editor_t *update_editor;
  void *update_baton;

  /* The request body for the REPORT. */
  serf_bucket_t *buckets;

  /* root directory object */
  report_dir_t *root_dir;

  /* number of pending GET requests */
  unsigned int active_fetches;

  /* completed fetches (contains report_fetch_t) */
  svn_ra_serf__list_t *done_fetches;

  /* number of pending PROPFIND requests */
  unsigned int active_propfinds;

  /* completed PROPFIND requests (contains propfind_context_t) */
  svn_ra_serf__list_t *done_propfinds;

  /* list of files that only have prop changes (contains report_info_t) */
  svn_ra_serf__list_t *file_propchanges_only;

  /* The path to the REPORT request */
  const char *path;

  /* Are we done parsing the REPORT response? */
  svn_boolean_t done;

};


/** Report state management helper **/

static report_info_t *
push_state(svn_ra_serf__xml_parser_t *parser,
           report_context_t *ctx,
           report_state_e state)
{
  report_info_t *info;
  apr_pool_t *info_parent_pool;

  svn_ra_serf__xml_push_state(parser, state);

  info = parser->state->private;

  /* Our private pool needs to be disjoint from the state pool. */
  if (!info)
    {
      info_parent_pool = ctx->pool;
    }
  else
    {
      info_parent_pool = info->pool;
    }

  if (state == OPEN_DIR || state == ADD_DIR)
    {
      report_info_t *new_info;

      new_info = apr_pcalloc(info_parent_pool, sizeof(*new_info));
      apr_pool_create(&new_info->pool, info_parent_pool);
      new_info->lock_token = NULL;

      new_info->dir = apr_pcalloc(new_info->pool, sizeof(*new_info->dir));
      new_info->dir->pool = new_info->pool;

      /* Create the root property tree. */
      new_info->dir->props = apr_hash_make(new_info->pool);
      new_info->props = new_info->dir->props;
      new_info->dir->removed_props = apr_hash_make(new_info->pool);

      /* Point to the update_editor */
      new_info->dir->update_editor = ctx->update_editor;
      new_info->dir->update_baton = ctx->update_baton;
      new_info->dir->report_context = ctx;

      if (info)
        {
          info->dir->ref_count++;

          new_info->dir->parent_dir = info->dir;

          /* Point our ns_list at our parents to try to reuse it. */
          new_info->dir->ns_list = info->dir->ns_list;

          /* Add ourselves to our parent's list */
          new_info->dir->sibling = info->dir->children;
          info->dir->children = new_info->dir;
        }
      else
        {
          /* Allow us to be found later. */
          ctx->root_dir = new_info->dir;
        }

      parser->state->private = new_info;
    }
  else if (state == OPEN_FILE || state == ADD_FILE)
    {
      report_info_t *new_info;

      new_info = apr_pcalloc(info_parent_pool, sizeof(*new_info));
      apr_pool_create(&new_info->pool, info_parent_pool);
      new_info->file_baton = NULL;
      new_info->lock_token = NULL;
      new_info->fetch_file = FALSE;

      /* Point at our parent's directory state. */
      new_info->dir = info->dir;
      info->dir->ref_count++;

      new_info->props = apr_hash_make(new_info->pool);

      parser->state->private = new_info;
    }

  return parser->state->private;
}


/** Wrappers around our various property walkers **/

static svn_error_t *
set_file_props(void *baton,
               const char *ns, apr_ssize_t ns_len,
               const char *name, apr_ssize_t name_len,
               const svn_string_t *val,
               apr_pool_t *pool)
{
  report_info_t *info = baton;
  const svn_delta_editor_t *editor = info->dir->update_editor;

  if (name_len == 12
      && ns_len == 39
      && strcmp(name, "md5-checksum") == 0
      && strcmp(ns, SVN_DAV_PROP_NS_DAV) == 0)
    info->final_checksum = apr_pstrdup(info->pool, val->data);

  return svn_ra_serf__set_baton_props(editor->change_file_prop,
                                      info->file_baton,
                                      ns, ns_len, name, name_len, val, pool);
}

static svn_error_t *
set_dir_props(void *baton,
              const char *ns, apr_ssize_t ns_len,
              const char *name, apr_ssize_t name_len,
              const svn_string_t *val,
              apr_pool_t *pool)
{
  report_dir_t *dir = baton;
  return svn_ra_serf__set_baton_props(dir->update_editor->change_dir_prop,
                                      dir->dir_baton,
                                      ns, ns_len, name, name_len, val, pool);
}

static svn_error_t *
remove_file_props(void *baton,
                  const char *ns, apr_ssize_t ns_len,
                  const char *name, apr_ssize_t name_len,
                  const svn_string_t *val,
                  apr_pool_t *pool)
{
  report_info_t *info = baton;
  const svn_delta_editor_t *editor = info->dir->update_editor;

  return svn_ra_serf__set_baton_props(editor->change_file_prop,
                                      info->file_baton,
                                      ns, ns_len, name, name_len, NULL, pool);
}

static svn_error_t *
remove_dir_props(void *baton,
                 const char *ns, apr_ssize_t ns_len,
                 const char *name, apr_ssize_t name_len,
                 const svn_string_t *val,
                 apr_pool_t *pool)
{
  report_dir_t *dir = baton;
  return svn_ra_serf__set_baton_props(dir->update_editor->change_dir_prop,
                                      dir->dir_baton,
                                      ns, ns_len, name, name_len, NULL, pool);
}


/** Helpers to open and close directories */

static svn_error_t*
open_dir(report_dir_t *dir)
{
  /* if we're already open, return now */
  if (dir->dir_baton)
    {
      return SVN_NO_ERROR;
    }

  if (dir->base_name[0] == '\0')
    {
      apr_pool_create(&dir->dir_baton_pool, dir->pool);

      if (dir->report_context->destination &&
          dir->report_context->sess->wc_callbacks->invalidate_wc_props)
        {
          SVN_ERR(dir->report_context->sess->wc_callbacks->invalidate_wc_props(
                      dir->report_context->sess->wc_callback_baton,
                      dir->report_context->update_target,
                      SVN_RA_SERF__WC_CHECKED_IN_URL, dir->pool));
        }

      SVN_ERR(dir->update_editor->open_root(dir->update_baton, dir->base_rev,
                                            dir->dir_baton_pool,
                                            &dir->dir_baton));
    }
  else
    {
      SVN_ERR(open_dir(dir->parent_dir));

      apr_pool_create(&dir->dir_baton_pool, dir->parent_dir->dir_baton_pool);

      if (SVN_IS_VALID_REVNUM(dir->base_rev))
        {
          SVN_ERR(dir->update_editor->open_directory(dir->name,
                                                     dir->parent_dir->dir_baton,
                                                     dir->base_rev,
                                                     dir->dir_baton_pool,
                                                     &dir->dir_baton));
        }
      else
        {
          SVN_ERR(dir->update_editor->add_directory(dir->name,
                                                    dir->parent_dir->dir_baton,
                                                    NULL, SVN_INVALID_REVNUM,
                                                    dir->dir_baton_pool,
                                                    &dir->dir_baton));
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
close_dir(report_dir_t *dir)
{
  report_dir_t *prev, *sibling;

  SVN_ERR_ASSERT(! dir->ref_count);

  svn_ra_serf__walk_all_props(dir->props, dir->base_name, dir->base_rev,
                              set_dir_props, dir, dir->dir_baton_pool);

  svn_ra_serf__walk_all_props(dir->removed_props, dir->base_name,
                              dir->base_rev, remove_dir_props, dir,
                              dir->dir_baton_pool);

  if (dir->fetch_props)
    {
      svn_ra_serf__walk_all_props(dir->props, dir->url, dir->target_rev,
                                  set_dir_props, dir, dir->dir_baton_pool);
    }

  SVN_ERR(dir->update_editor->close_directory(dir->dir_baton,
                                              dir->dir_baton_pool));

  /* remove us from our parent's children list */
  if (dir->parent_dir)
    {
      prev = NULL;
      sibling = dir->parent_dir->children;

      while (sibling != dir)
        {
          prev = sibling;
          sibling = sibling->sibling;
          if (!sibling)
            SVN_ERR_MALFUNCTION();
        }

      if (!prev)
        {
          dir->parent_dir->children = dir->sibling;
        }
      else
        {
          prev->sibling = dir->sibling;
        }
    }

  svn_pool_destroy(dir->dir_baton_pool);
  svn_pool_destroy(dir->pool);

  return SVN_NO_ERROR;
}

static svn_error_t *close_all_dirs(report_dir_t *dir)
{
  while (dir->children)
    {
      SVN_ERR(close_all_dirs(dir->children));
      dir->ref_count--;
    }

  SVN_ERR_ASSERT(! dir->ref_count);

  SVN_ERR(open_dir(dir));

  return close_dir(dir);
}


/** Routines called when we are fetching a file */

/* This function works around a bug in some older versions of
 * mod_dav_svn in that it will not send remove-prop in the update
 * report when a lock property disappears when send-all is false.
 *
 * Therefore, we'll try to look at our properties and see if there's
 * an active lock.  If not, then we'll assume there isn't a lock
 * anymore.
 */
static void
check_lock(report_info_t *info)
{
  const char *lock_val;

  lock_val = svn_ra_serf__get_ver_prop(info->props, info->url,
                                       info->target_rev,
                                       "DAV:", "lockdiscovery");

  if (lock_val)
    {
      char *new_lock;
      new_lock = apr_pstrdup(info->editor_pool, lock_val);
      apr_collapse_spaces(new_lock, new_lock);
      lock_val = new_lock;
    }

  if (!lock_val || lock_val[0] == '\0')
    {
      svn_string_t *str;

      str = svn_string_ncreate("", 1, info->editor_pool);

      svn_ra_serf__set_ver_prop(info->dir->removed_props, info->base_name,
                                info->base_rev, "DAV:", "lock-token",
                                str, info->dir->pool);
    }
}

static apr_status_t
headers_fetch(serf_bucket_t *headers,
              void *baton,
              apr_pool_t *pool)
{
  report_fetch_t *fetch_ctx = baton;

  /* note that we have old VC URL */
  if (SVN_IS_VALID_REVNUM(fetch_ctx->info->base_rev) &&
      fetch_ctx->info->delta_base)
    {
      serf_bucket_headers_setn(headers, SVN_DAV_DELTA_BASE_HEADER,
                               fetch_ctx->info->delta_base->data);
      serf_bucket_headers_setn(headers, "Accept-Encoding",
                               "svndiff1;q=0.9,svndiff;q=0.8");
    }
  else if (fetch_ctx->conn->using_compression == TRUE)
    {
      serf_bucket_headers_setn(headers, "Accept-Encoding", "gzip");
    }

  return APR_SUCCESS;
}

static apr_status_t
cancel_fetch(serf_request_t *request,
             serf_bucket_t *response,
             int status_code,
             void *baton)
{
  report_fetch_t *fetch_ctx = baton;

  /* Uh-oh.  Our connection died on us.
   *
   * The core ra_serf layer will requeue our request - we just need to note
   * that we got cut off in the middle of our song.
   */
  if (!response)
    {
      /* If we already started the fetch and opened the file handle, we need
       * to hold subsequent read() ops until we get back to where we were
       * before the close and we can then resume the textdelta() calls.
       */
      if (fetch_ctx->read_headers == TRUE)
        {
          if (fetch_ctx->aborted_read == FALSE && fetch_ctx->read_size)
            {
              fetch_ctx->aborted_read = TRUE;
              fetch_ctx->aborted_read_size = fetch_ctx->read_size;
            }
          fetch_ctx->read_size = 0;
        }

      return APR_SUCCESS;
    }

  /* We have no idea what went wrong. */
  SVN_ERR_MALFUNCTION_NO_RETURN();
}

static apr_status_t
error_fetch(serf_request_t *request,
            report_fetch_t *fetch_ctx,
            svn_error_t *err)
{
  fetch_ctx->err = err;

  fetch_ctx->done = TRUE;

  fetch_ctx->done_item.data = fetch_ctx;
  fetch_ctx->done_item.next = *fetch_ctx->done_list;
  *fetch_ctx->done_list = &fetch_ctx->done_item;

  serf_request_set_handler(request, svn_ra_serf__handle_discard_body, NULL);

  return APR_SUCCESS;
}

static apr_status_t
handle_fetch(serf_request_t *request,
             serf_bucket_t *response,
             void *handler_baton,
             apr_pool_t *pool)
{
  const char *data;
  apr_size_t len;
  apr_status_t status;
  report_fetch_t *fetch_ctx = handler_baton;
  svn_error_t *err;
  serf_status_line sl;

  if (fetch_ctx->read_headers == FALSE)
    {
      serf_bucket_t *hdrs;
      const char *val;
      report_info_t *info;

      hdrs = serf_bucket_response_get_headers(response);
      val = serf_bucket_headers_get(hdrs, "Content-Type");
      info = fetch_ctx->info;

      err = open_dir(info->dir);
      if (err)
        {
          return error_fetch(request, fetch_ctx, err);
        }

      apr_pool_create(&info->editor_pool, info->dir->dir_baton_pool);

      /* Expand our full name now if we haven't done so yet. */
      if (!info->name)
        {
          info->name_buf = svn_stringbuf_dup(info->dir->name_buf,
                                             info->editor_pool);
          svn_path_add_component(info->name_buf, info->base_name);
          info->name = info->name_buf->data;
        }

      if (SVN_IS_VALID_REVNUM(info->base_rev))
        {
          err = info->dir->update_editor->open_file(info->name,
                                                    info->dir->dir_baton,
                                                    info->base_rev,
                                                    info->editor_pool,
                                                    &info->file_baton);
        }
      else
        {
          err = info->dir->update_editor->add_file(info->name,
                                                   info->dir->dir_baton,
                                                   info->copyfrom_path,
                                                   info->copyfrom_rev,
                                                   info->editor_pool,
                                                   &info->file_baton);
        }

      if (err)
        {
          return error_fetch(request, fetch_ctx, err);
        }

      err = info->dir->update_editor->apply_textdelta(info->file_baton,
                                                      info->base_checksum,
                                                      info->editor_pool,
                                                      &info->textdelta,
                                                      &info->textdelta_baton);

      if (err)
        {
          return error_fetch(request, fetch_ctx, err);
        }

      if (val && svn_cstring_casecmp(val, "application/vnd.svn-svndiff") == 0)
        {
          fetch_ctx->delta_stream =
              svn_txdelta_parse_svndiff(info->textdelta,
                                        info->textdelta_baton,
                                        TRUE, info->editor_pool);
        }
      else
        {
          fetch_ctx->delta_stream = NULL;
        }

      fetch_ctx->read_headers = TRUE;
    }

  /* If the error code wasn't 200, something went wrong. Don't use the returned
     data as its probably an error message. Just bail out instead. */
  status = serf_bucket_response_status(response, &sl);
  if (SERF_BUCKET_READ_ERROR(status))
    {
      return status;
    }
  if (sl.code != 200)
    {
      err = svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                              _("GET request failed: %d %s"),
                              sl.code, sl.reason);
      return error_fetch(request, fetch_ctx, err);
    }

  while (1)
    {
      svn_txdelta_window_t delta_window = { 0 };
      svn_txdelta_op_t delta_op;
      svn_string_t window_data;

      status = serf_bucket_read(response, 8000, &data, &len);
      if (SERF_BUCKET_READ_ERROR(status))
        {
          return status;
        }

      fetch_ctx->read_size += len;

      if (fetch_ctx->aborted_read == TRUE)
        {
          /* We haven't caught up to where we were before. */
          if (fetch_ctx->read_size < fetch_ctx->aborted_read_size)
            {
              /* Eek.  What did the file shrink or something? */
              if (APR_STATUS_IS_EOF(status))
                {
                  SVN_ERR_MALFUNCTION_NO_RETURN();
                }

              /* Skip on to the next iteration of this loop. */
              if (APR_STATUS_IS_EAGAIN(status))
                {
                  return status;
                }
              continue;
            }

          /* Woo-hoo.  We're back. */
          fetch_ctx->aborted_read = FALSE;

          /* Increment data and len by the difference. */
          data += fetch_ctx->read_size - fetch_ctx->aborted_read_size;
          len = fetch_ctx->read_size - fetch_ctx->aborted_read_size;
        }

      if (fetch_ctx->delta_stream)
        {
          err = svn_stream_write(fetch_ctx->delta_stream, data, &len);
          if (err)
            {
              return error_fetch(request, fetch_ctx, err);
            }
        }
      /* otherwise, manually construct the text delta window. */
      else if (len)
        {
          window_data.data = data;
          window_data.len = len;

          delta_op.action_code = svn_txdelta_new;
          delta_op.offset = 0;
          delta_op.length = len;

          delta_window.tview_len = len;
          delta_window.num_ops = 1;
          delta_window.ops = &delta_op;
          delta_window.new_data = &window_data;

          /* write to the file located in the info. */
          err = fetch_ctx->info->textdelta(&delta_window,
                                           fetch_ctx->info->textdelta_baton);
          if (err)
            {
              return error_fetch(request, fetch_ctx, err);
            }
        }

      if (APR_STATUS_IS_EOF(status))
        {
          report_info_t *info = fetch_ctx->info;

          err = info->textdelta(NULL, info->textdelta_baton);
          if (err)
            {
              return error_fetch(request, fetch_ctx, err);
            }

          if (info->lock_token)
            check_lock(info);

          /* set all of the properties we received */
          svn_ra_serf__walk_all_props(info->props,
                                      info->base_name,
                                      info->base_rev,
                                      set_file_props,
                                      info, info->editor_pool);
          svn_ra_serf__walk_all_props(info->dir->removed_props,
                                      info->base_name,
                                      info->base_rev,
                                      remove_file_props,
                                      info, info->editor_pool);
          if (info->fetch_props)
            {
              svn_ra_serf__walk_all_props(info->props,
                                          info->url,
                                          info->target_rev,
                                          set_file_props,
                                          info, info->editor_pool);
            }

          err = info->dir->update_editor->close_file(info->file_baton,
                                                     info->final_checksum,
                                                     info->editor_pool);

          if (err)
            {
              return error_fetch(request, fetch_ctx, err);
            }

          fetch_ctx->done = TRUE;

          fetch_ctx->done_item.data = fetch_ctx;
          fetch_ctx->done_item.next = *fetch_ctx->done_list;
          *fetch_ctx->done_list = &fetch_ctx->done_item;

          /* We're done with our pools. */
          svn_pool_destroy(info->editor_pool);
          svn_pool_destroy(info->pool);

          return status;
        }
      if (APR_STATUS_IS_EAGAIN(status))
        {
          return status;
        }
    }
  /* not reached */
}

static apr_status_t
handle_stream(serf_request_t *request,
              serf_bucket_t *response,
              void *handler_baton,
              apr_pool_t *pool)
{
  report_fetch_t *fetch_ctx = handler_baton;
  serf_status_line sl;

  serf_bucket_response_status(response, &sl);

  /* Woo-hoo.  Nothing here to see.  */
  fetch_ctx->err = svn_ra_serf__error_on_status(sl.code, fetch_ctx->info->name);
  if (fetch_ctx->err)
    {
      fetch_ctx->done = TRUE;

      return svn_ra_serf__handle_discard_body(request, response, NULL, pool);
    }

  while (1)
    {
      const char *data;
      apr_size_t len;
      apr_status_t status;

      status = serf_bucket_read(response, 8000, &data, &len);
      if (SERF_BUCKET_READ_ERROR(status))
        {
          return status;
        }

      fetch_ctx->read_size += len;

      if (fetch_ctx->aborted_read == TRUE)
        {
          /* We haven't caught up to where we were before. */
          if (fetch_ctx->read_size < fetch_ctx->aborted_read_size)
            {
              /* Eek.  What did the file shrink or something? */
              if (APR_STATUS_IS_EOF(status))
                {
                  SVN_ERR_MALFUNCTION_NO_RETURN();
                }

              /* Skip on to the next iteration of this loop. */
              if (APR_STATUS_IS_EAGAIN(status))
                {
                  return status;
                }
              continue;
            }

          /* Woo-hoo.  We're back. */
          fetch_ctx->aborted_read = FALSE;

          /* Increment data and len by the difference. */
          data += fetch_ctx->read_size - fetch_ctx->aborted_read_size;
          len += fetch_ctx->read_size - fetch_ctx->aborted_read_size;
        }

      if (len)
        {
          apr_size_t written_len;

          written_len = len;

          svn_stream_write(fetch_ctx->target_stream, data, &written_len);
        }

      if (APR_STATUS_IS_EOF(status))
        {
          fetch_ctx->done = TRUE;
        }

      if (status)
        {
          return status;
        }
    }
  /* not reached */
}

static svn_error_t *
handle_propchange_only(report_info_t *info)
{
  /* Ensure our parent is open. */
  SVN_ERR(open_dir(info->dir));

  apr_pool_create(&info->editor_pool, info->dir->dir_baton_pool);

  /* Expand our full name now if we haven't done so yet. */
  if (!info->name)
    {
      info->name_buf = svn_stringbuf_dup(info->dir->name_buf,
                                         info->editor_pool);
      svn_path_add_component(info->name_buf, info->base_name);
      info->name = info->name_buf->data;
    }

  if (SVN_IS_VALID_REVNUM(info->base_rev))
    {
      SVN_ERR(info->dir->update_editor->open_file(info->name,
                                                  info->dir->dir_baton,
                                                  info->base_rev,
                                                  info->editor_pool,
                                                  &info->file_baton));
    }
  else
    {
      SVN_ERR(info->dir->update_editor->add_file(info->name,
                                                 info->dir->dir_baton,
                                                 info->copyfrom_path,
                                                 info->copyfrom_rev,
                                                 info->editor_pool,
                                                 &info->file_baton));
    }

  if (info->fetch_file)
    {
      SVN_ERR(info->dir->update_editor->apply_textdelta(info->file_baton,
                                                    info->base_checksum,
                                                    info->editor_pool,
                                                    &info->textdelta,
                                                    &info->textdelta_baton));
    }

  if (info->lock_token)
    check_lock(info);

  /* set all of the properties we received */
  svn_ra_serf__walk_all_props(info->props,
                              info->base_name, info->base_rev,
                              set_file_props, info, info->editor_pool);
  svn_ra_serf__walk_all_props(info->dir->removed_props,
                              info->base_name, info->base_rev,
                              remove_file_props, info, info->editor_pool);
  if (info->fetch_props)
    {
      svn_ra_serf__walk_all_props(info->props, info->url, info->target_rev,
                                  set_file_props, info, info->editor_pool);
    }

  SVN_ERR(info->dir->update_editor->close_file(info->file_baton,
                                               info->final_checksum,
                                               info->editor_pool));

  /* We're done with our pools. */
  svn_pool_destroy(info->editor_pool);
  svn_pool_destroy(info->pool);

  info->dir->ref_count--;

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_file(report_context_t *ctx, report_info_t *info)
{
  svn_ra_serf__connection_t *conn;
  svn_ra_serf__handler_t *handler;

  /* What connection should we go on? */
  conn = ctx->sess->conns[ctx->sess->cur_conn];

  /* go fetch info->name from DAV:checked-in */
  info->url =
      svn_ra_serf__get_ver_prop(info->props, info->base_name,
                                info->base_rev, "DAV:", "checked-in");

  if (!info->url)
    {
      return svn_error_create(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
                        _("The OPTIONS response did not include the "
                          "requested checked-in value"));
    }

  /* If needed, create the PROPFIND to retrieve the file's properties. */
  info->propfind = NULL;
  if (info->fetch_props)
    {
      svn_ra_serf__deliver_props(&info->propfind, info->props,
                                 ctx->sess, conn,
                                 info->url, info->target_rev, "0", all_props,
                                 FALSE, &ctx->done_propfinds, info->dir->pool);

      SVN_ERR_ASSERT(info->propfind);

      ctx->active_propfinds++;
    }

  /* If we've been asked to fetch the file or its an add, do so.
   * Otherwise, handle the case where only the properties changed.
   */
  if (info->fetch_file && ctx->text_deltas == TRUE)
    {
      report_fetch_t *fetch_ctx;

      fetch_ctx = apr_pcalloc(info->dir->pool, sizeof(*fetch_ctx));
      fetch_ctx->pool = info->pool;
      fetch_ctx->info = info;
      fetch_ctx->done_list = &ctx->done_fetches;
      fetch_ctx->sess = ctx->sess;
      fetch_ctx->conn = conn;

      handler = apr_pcalloc(info->dir->pool, sizeof(*handler));

      handler->method = "GET";
      handler->path = fetch_ctx->info->url;

      handler->conn = conn;
      handler->session = ctx->sess;

      handler->header_delegate = headers_fetch;
      handler->header_delegate_baton = fetch_ctx;

      handler->response_handler = handle_fetch;
      handler->response_baton = fetch_ctx;

      handler->response_error = cancel_fetch;
      handler->response_error_baton = fetch_ctx;

      svn_ra_serf__request_create(handler);

      ctx->active_fetches++;
    }
  else if (info->propfind)
    {
      svn_ra_serf__list_t *list_item;

      list_item = apr_pcalloc(info->dir->pool, sizeof(*list_item));
      list_item->data = info;
      list_item->next = ctx->file_propchanges_only;
      ctx->file_propchanges_only = list_item;
    }
  else
    {
      /* No propfind or GET request.  Just handle the prop changes now. */
      SVN_ERR(handle_propchange_only(info));
    }

  return SVN_NO_ERROR;
}


/** XML callbacks for our update-report response parsing */

static svn_error_t *
start_report(svn_ra_serf__xml_parser_t *parser,
             void *userData,
             svn_ra_serf__dav_props_t name,
             const char **attrs)
{
  report_context_t *ctx = userData;
  report_state_e state;

  state = parser->state->current_state;

  if (state == NONE && strcmp(name.name, "target-revision") == 0)
    {
      const char *rev;

      rev = svn_xml_get_attr_value("rev", attrs);

      if (!rev)
        {
          return svn_error_create
            (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
             _("Missing revision attr in target-revision element"));
        }

      ctx->update_editor->set_target_revision(ctx->update_baton,
                                              SVN_STR_TO_REV(rev),
                                              ctx->sess->pool);
    }
  else if (state == NONE && strcmp(name.name, "open-directory") == 0)
    {
      const char *rev;
      report_info_t *info;

      rev = svn_xml_get_attr_value("rev", attrs);

      if (!rev)
        {
          return svn_error_create
            (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
             _("Missing revision attr in open-directory element"));
        }

      info = push_state(parser, ctx, OPEN_DIR);

      info->base_rev = apr_atoi64(rev);
      info->dir->base_rev = info->base_rev;
      info->dir->target_rev = ctx->target_rev;
      info->fetch_props = TRUE;

      info->dir->base_name = "";
      /* Create empty stringbuf with estimated max. path size. */
      info->dir->name_buf = svn_stringbuf_create_ensure(256, info->pool);
      info->dir->name = info->dir->name_buf->data;

      info->base_name = info->dir->base_name;
      info->name = info->dir->name;
      info->name_buf = info->dir->name_buf;
    }
  else if (state == NONE)
    {
      /* do nothing as we haven't seen our valid start tag yet. */
    }
  else if ((state == OPEN_DIR || state == ADD_DIR) &&
           strcmp(name.name, "open-directory") == 0)
    {
      const char *rev, *dirname;
      report_dir_t *dir;
      report_info_t *info;

      rev = svn_xml_get_attr_value("rev", attrs);

      if (!rev)
        {
          return svn_error_create
            (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
             _("Missing revision attr in open-directory element"));
        }

      dirname = svn_xml_get_attr_value("name", attrs);

      if (!dirname)
        {
          return svn_error_create
            (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
             _("Missing name attr in open-directory element"));
        }

      info = push_state(parser, ctx, OPEN_DIR);

      dir = info->dir;

      info->base_rev = apr_atoi64(rev);
      dir->base_rev = info->base_rev;
      dir->target_rev = ctx->target_rev;

      info->fetch_props = FALSE;

      dir->base_name = apr_pstrdup(dir->pool, dirname);
      info->base_name = dir->base_name;

      /* Expand our name. */
      dir->name_buf = svn_stringbuf_dup(dir->parent_dir->name_buf, dir->pool);
      svn_path_add_component(dir->name_buf, dir->base_name);

      dir->name = dir->name_buf->data;
      info->name = dir->name;
    }
  else if ((state == OPEN_DIR || state == ADD_DIR) &&
           strcmp(name.name, "add-directory") == 0)
    {
      const char *dir_name, *cf, *cr;
      report_dir_t *dir;
      report_info_t *info;

      dir_name = svn_xml_get_attr_value("name", attrs);
      if (!dir_name)
        {
          return svn_error_create
            (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
             _("Missing name attr in add-directory element"));
        }
      cf = svn_xml_get_attr_value("copyfrom-path", attrs);
      cr = svn_xml_get_attr_value("copyfrom-rev", attrs);

      info = push_state(parser, ctx, ADD_DIR);

      dir = info->dir;

      dir->base_name = apr_pstrdup(dir->pool, dir_name);
      info->base_name = dir->base_name;

      /* Expand our name. */
      dir->name_buf = svn_stringbuf_dup(dir->parent_dir->name_buf, dir->pool);
      svn_path_add_component(dir->name_buf, dir->base_name);

      dir->name = dir->name_buf->data;
      info->name = dir->name;

      info->copyfrom_path = cf ? apr_pstrdup(info->pool, cf) : NULL;
      info->copyfrom_rev = cr ? apr_atoi64(cr) : SVN_INVALID_REVNUM;

      /* Mark that we don't have a base. */
      info->base_rev = SVN_INVALID_REVNUM;
      dir->base_rev = info->base_rev;
      dir->target_rev = ctx->target_rev;
      dir->fetch_props = TRUE;
    }
  else if ((state == OPEN_DIR || state == ADD_DIR) &&
           strcmp(name.name, "open-file") == 0)
    {
      const char *file_name, *rev;
      report_info_t *info;

      file_name = svn_xml_get_attr_value("name", attrs);

      if (!file_name)
        {
          return svn_error_create
            (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
             _("Missing name attr in open-file element"));
        }

      rev = svn_xml_get_attr_value("rev", attrs);

      if (!rev)
        {
          return svn_error_create
            (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
             _("Missing revision attr in open-file element"));
        }

      info = push_state(parser, ctx, OPEN_FILE);

      info->base_rev = apr_atoi64(rev);
      info->target_rev = ctx->target_rev;
      info->fetch_props = FALSE;

      info->base_name = apr_pstrdup(info->pool, file_name);
      info->name = NULL;
    }
  else if ((state == OPEN_DIR || state == ADD_DIR) &&
           strcmp(name.name, "add-file") == 0)
    {
      const char *file_name, *cf, *cr;
      report_info_t *info;

      file_name = svn_xml_get_attr_value("name", attrs);
      cf = svn_xml_get_attr_value("copyfrom-path", attrs);
      cr = svn_xml_get_attr_value("copyfrom-rev", attrs);

      if (!file_name)
        {
          return svn_error_create
            (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
             _("Missing name attr in add-file element"));
        }

      info = push_state(parser, ctx, ADD_FILE);

      info->base_rev = SVN_INVALID_REVNUM;
      info->target_rev = ctx->target_rev;
      info->fetch_props = TRUE;
      info->fetch_file = TRUE;

      info->base_name = apr_pstrdup(info->pool, file_name);
      info->name = NULL;

      info->copyfrom_path = cf ? apr_pstrdup(info->pool, cf) : NULL;
      info->copyfrom_rev = cr ? apr_atoi64(cr) : SVN_INVALID_REVNUM;
    }
  else if ((state == OPEN_DIR || state == ADD_DIR) &&
           strcmp(name.name, "delete-entry") == 0)
    {
      const char *file_name;
      svn_stringbuf_t *name_buf;
      report_info_t *info;
      apr_pool_t *tmppool;

      file_name = svn_xml_get_attr_value("name", attrs);

      if (!file_name)
        {
          return svn_error_create
            (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
             _("Missing name attr in delete-entry element"));
        }

      info = parser->state->private;

      SVN_ERR(open_dir(info->dir));

      apr_pool_create(&tmppool, info->dir->dir_baton_pool);

      name_buf = svn_stringbuf_dup(info->dir->name_buf, tmppool);
      svn_path_add_component(name_buf, file_name);

      SVN_ERR(info->dir->update_editor->delete_entry(name_buf->data,
                                                     SVN_INVALID_REVNUM,
                                                     info->dir->dir_baton,
                                                     tmppool));

      svn_pool_destroy(tmppool);
    }
  else if ((state == OPEN_DIR || state == ADD_DIR) &&
           strcmp(name.name, "absent-directory") == 0)
    {
      const char *file_name;
      report_info_t *info;

      file_name = svn_xml_get_attr_value("name", attrs);

      if (!file_name)
        {
          return svn_error_create
            (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
             _("Missing name attr in absent-directory element"));
        }

      info = parser->state->private;

      SVN_ERR(open_dir(info->dir));

      ctx->update_editor->absent_directory(file_name,
                                           info->dir->dir_baton,
                                           info->dir->pool);
    }
  else if ((state == OPEN_DIR || state == ADD_DIR) &&
           strcmp(name.name, "absent-file") == 0)
    {
      const char *file_name;
      report_info_t *info;

      file_name = svn_xml_get_attr_value("name", attrs);

      if (!file_name)
        {
          return svn_error_create
            (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
             _("Missing name attr in absent-file element"));
        }

      info = parser->state->private;

      SVN_ERR(open_dir(info->dir));

      ctx->update_editor->absent_file(file_name,
                                      info->dir->dir_baton,
                                      info->dir->pool);
    }
  else if (state == OPEN_DIR || state == ADD_DIR)
    {
      report_info_t *info;

      if (strcmp(name.name, "checked-in") == 0)
        {
          info = push_state(parser, ctx, IGNORE_PROP_NAME);
          info->prop_ns = name.namespace;
          info->prop_name = apr_pstrdup(parser->state->pool, name.name);
          info->prop_encoding = NULL;
          info->prop_val = NULL;
          info->prop_val_len = 0;
        }
      else if (strcmp(name.name, "set-prop") == 0 ||
               strcmp(name.name, "remove-prop") == 0)
        {
          const char *full_prop_name;
          const char *colon;

          info = push_state(parser, ctx, PROP);

          full_prop_name = svn_xml_get_attr_value("name", attrs);
          if (!full_prop_name)
            {
              return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                       _("Missing name attr in %s element"),
                                       name.name);
            }

          colon = strchr(full_prop_name, ':');

          if (colon)
            colon++;
          else
            colon = full_prop_name;

          info->prop_ns = apr_pstrmemdup(info->dir->pool, full_prop_name,
                                         colon - full_prop_name);
          info->prop_name = apr_pstrdup(parser->state->pool, colon);
          info->prop_encoding = svn_xml_get_attr_value("encoding", attrs);
          info->prop_val = NULL;
          info->prop_val_len = 0;
        }
      else if (strcmp(name.name, "prop") == 0)
        {
          /* need to fetch it. */
          push_state(parser, ctx, NEED_PROP_NAME);
        }
      else if (strcmp(name.name, "fetch-props") == 0)
        {
          info = parser->state->private;

          info->dir->fetch_props = TRUE;
        }
      else
        {
          SVN_ERR_MALFUNCTION();
        }

    }
  else if (state == OPEN_FILE || state == ADD_FILE)
    {
      report_info_t *info;

      if (strcmp(name.name, "checked-in") == 0)
        {
          info = push_state(parser, ctx, IGNORE_PROP_NAME);
          info->prop_ns = name.namespace;
          info->prop_name = apr_pstrdup(parser->state->pool, name.name);
          info->prop_encoding = NULL;
          info->prop_val = NULL;
          info->prop_val_len = 0;
        }
      else if (strcmp(name.name, "prop") == 0)
        {
          /* need to fetch it. */
          push_state(parser, ctx, NEED_PROP_NAME);
        }
      else if (strcmp(name.name, "fetch-props") == 0)
        {
          info = parser->state->private;

          info->fetch_props = TRUE;
        }
      else if (strcmp(name.name, "fetch-file") == 0)
        {
          info = parser->state->private;
          info->base_checksum = svn_xml_get_attr_value("base-checksum", attrs);

          if (info->base_checksum)
            info->base_checksum = apr_pstrdup(info->pool, info->base_checksum);

          info->fetch_file = TRUE;

        }
      else if (strcmp(name.name, "set-prop") == 0 ||
               strcmp(name.name, "remove-prop") == 0)
        {
          const char *full_prop_name;
          const char *colon;

          info = push_state(parser, ctx, PROP);

          full_prop_name = svn_xml_get_attr_value("name", attrs);
          if (!full_prop_name)
            {
              return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                       _("Missing name attr in %s element"),
                                       name.name);
            }
          colon = strchr(full_prop_name, ':');

          if (colon)
            colon++;
          else
            colon = full_prop_name;

          info->prop_ns = apr_pstrmemdup(info->dir->pool, full_prop_name,
                                         colon - full_prop_name);
          info->prop_name = apr_pstrdup(parser->state->pool, colon);
          info->prop_encoding = svn_xml_get_attr_value("encoding", attrs);
          info->prop_val = NULL;
          info->prop_val_len = 0;
        }
      else
        {
          SVN_ERR_MALFUNCTION();
        }
    }
  else if (state == IGNORE_PROP_NAME)
    {
      report_info_t *info = push_state(parser, ctx, PROP);
      info->prop_encoding = svn_xml_get_attr_value("encoding", attrs);
    }
  else if (state == NEED_PROP_NAME)
    {
      report_info_t *info;

      info = push_state(parser, ctx, PROP);

      info->prop_ns = name.namespace;
      info->prop_name = apr_pstrdup(parser->state->pool, name.name);
      info->prop_encoding = svn_xml_get_attr_value("encoding", attrs);
      info->prop_val = NULL;
      info->prop_val_len = 0;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
end_report(svn_ra_serf__xml_parser_t *parser,
           void *userData,
           svn_ra_serf__dav_props_t name)
{
  report_context_t *ctx = userData;
  report_state_e state;

  state = parser->state->current_state;

  if (state == NONE)
    {
      /* nothing to close yet. */
      return SVN_NO_ERROR;
    }

  if (((state == OPEN_DIR && (strcmp(name.name, "open-directory") == 0)) ||
       (state == ADD_DIR && (strcmp(name.name, "add-directory") == 0))))
    {
      const char *checked_in_url;
      report_info_t *info = parser->state->private;

      /* We've now closed this directory; note it. */
      info->dir->tag_closed = TRUE;

      /* go fetch info->file_name from DAV:checked-in */
      checked_in_url =
          svn_ra_serf__get_ver_prop(info->dir->props, info->base_name,
                                    info->base_rev, "DAV:", "checked-in");

      /* If we were expecting to have the properties and we aren't able to
       * get it, bail.
       */
      if (!checked_in_url &&
          (!SVN_IS_VALID_REVNUM(info->dir->base_rev) || info->dir->fetch_props))
        {
          return svn_error_create(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
                                  _("The OPTIONS response did not include the "
                                    "requested checked-in value"));
        }

      info->dir->url = checked_in_url;

      /* At this point, we should have the checked-in href.
       * If needed, create the PROPFIND to retrieve the dir's properties.
       */
      if (!SVN_IS_VALID_REVNUM(info->dir->base_rev) || info->dir->fetch_props)
        {
          /* Unconditionally set fetch_props now. */
          info->dir->fetch_props = TRUE;

          svn_ra_serf__deliver_props(&info->dir->propfind, info->dir->props,
                                     ctx->sess,
                                     ctx->sess->conns[ctx->sess->cur_conn],
                                     info->dir->url, info->dir->target_rev,
                                     "0", all_props, FALSE,
                                     &ctx->done_propfinds, info->dir->pool);

          SVN_ERR_ASSERT(info->dir->propfind);

          ctx->active_propfinds++;
        }
      else
        {
          info->dir->propfind = NULL;
        }

      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == OPEN_FILE && strcmp(name.name, "open-file") == 0)
    {
      report_info_t *info = parser->state->private;

      /* Expand our full name now if we haven't done so yet. */
      if (!info->name)
        {
          info->name_buf = svn_stringbuf_dup(info->dir->name_buf, info->pool);
          svn_path_add_component(info->name_buf, info->base_name);
          info->name = info->name_buf->data;
        }

      info->lock_token = apr_hash_get(ctx->lock_path_tokens, info->name,
                                      APR_HASH_KEY_STRING);

      if (info->lock_token && info->fetch_props == FALSE)
        info->fetch_props = TRUE;

      /* If we have a WC, we can dive all the way into the WC to get the
       * previous URL so we can do an differential GET with the base URL.
       *
       * If we don't have a WC (as is the case for URL<->URL diff), we can
       * manually reconstruct the base URL.  This avoids us having to grab
       * two full-text for URL<->URL diffs.  Instead, we can just grab one
       * full-text and a diff from the server against that other file.
       */
      if (ctx->sess->wc_callbacks->get_wc_prop)
        {
          ctx->sess->wc_callbacks->get_wc_prop(ctx->sess->wc_callback_baton,
                                               info->name,
                                               SVN_RA_SERF__WC_CHECKED_IN_URL,
                                               &info->delta_base,
                                               info->pool);
        }
      else
        {
          const char *c;
          apr_size_t comp_count;
          svn_stringbuf_t *path;

          c = svn_ra_serf__get_ver_prop(info->props, info->base_name,
                                        info->base_rev, "DAV:", "checked-in");

          path = svn_stringbuf_create(c, info->pool);

          comp_count = svn_path_component_count(info->name_buf->data);

          svn_path_remove_components(path, comp_count);

          /* Find out the difference of the destination compared to the repos
           * root url. Cut of this difference from path, which will give us our
           * version resource root path.
           *
           * Example:
           * path:
           *  /repositories/log_tests-17/!svn/ver/4/branches/a
           * repos_root:
           *  http://localhost/repositories/log_tests-17
           * destination:
           *  http://localhost/repositories/log_tests-17/branches/a
           *
           * So, find 'branches/a' as the difference. Cut it of path, gives us:
           *  /repositories/log_tests-17/!svn/ver/4
           */
          if (ctx->destination &&
              strcmp(ctx->destination, ctx->sess->repos_root_str) != 0)
            {
              apr_size_t root_count, src_count;

              src_count = svn_path_component_count(ctx->destination);
              root_count = svn_path_component_count(ctx->sess->repos_root_str);

              svn_path_remove_components(path, src_count - root_count);
            }

          /* At this point, we should just have the version number
           * remaining.  We know our target revision, so we'll replace it
           * and recreate what we just chopped off.
           */
          svn_path_remove_component(path);

          svn_path_add_component(path, apr_ltoa(info->pool, info->base_rev));

          /* Similar as above, we now have to add the relative path between
           * source and root path.
           *
           * Example:
           * path:
           *  /repositories/log_tests-17/!svn/ver/2
           * repos_root path:
           *  /repositories/log_tests-17
           * source:
           *  /repositories/log_tests-17/trunk
           *
           * So, find 'trunk' as the difference. Addding it to path, gives us:
           *  /repositories/log_tests-17/!svn/ver/2/trunk
           */
          if (strcmp(ctx->source, ctx->sess->repos_root.path) != 0)
            {
              apr_size_t root_len;

              root_len = strlen(ctx->sess->repos_root.path) + 1;

              svn_path_add_component(path, &ctx->source[root_len]);
            }

          /* Re-add the filename. */
          svn_path_add_component(path, info->name);

          info->delta_base = svn_string_create_from_buf(path, info->pool);
        }

      SVN_ERR(fetch_file(ctx, info));
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == ADD_FILE && strcmp(name.name, "add-file") == 0)
    {
      /* We should have everything we need to fetch the file. */
      SVN_ERR(fetch_file(ctx, parser->state->private));
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == PROP)
    {
      /* We need to move the prop_ns, prop_name, and prop_val into the
       * same lifetime as the dir->pool.
       */
      svn_ra_serf__ns_t *ns, *ns_name_match;
      int found = 0;
      report_info_t *info;
      report_dir_t *dir;
      apr_hash_t *props;
      const char *set_val;
      svn_string_t *set_val_str;
      apr_pool_t *pool;

      info = parser->state->private;
      dir = info->dir;

      /* We're going to be slightly tricky.  We don't care what the ->url
       * field is here at this point.  So, we're going to stick a single
       * copy of the property name inside of the ->url field.
       */
      ns_name_match = NULL;
      for (ns = dir->ns_list; ns; ns = ns->next)
        {
          if (strcmp(ns->namespace, info->prop_ns) == 0)
            {
              ns_name_match = ns;
              if (strcmp(ns->url, info->prop_name) == 0)
                {
                  found = 1;
                  break;
                }
            }
        }

      if (!found)
        {
          ns = apr_palloc(dir->pool, sizeof(*ns));
          if (!ns_name_match)
            {
              ns->namespace = apr_pstrdup(dir->pool, info->prop_ns);
            }
          else
            {
              ns->namespace = ns_name_match->namespace;
            }
          ns->url = apr_pstrdup(dir->pool, info->prop_name);

          ns->next = dir->ns_list;
          dir->ns_list = ns;
        }

      if (strcmp(name.name, "remove-prop") != 0)
        {
          props = info->props;
          pool = info->pool;
        }
      else
        {
          props = dir->removed_props;
          pool = dir->pool;
          info->prop_val = "";
          info->prop_val_len = 1;
        }

      if (info->prop_encoding)
        {
          if (strcmp(info->prop_encoding, "base64") == 0)
            {
              svn_string_t encoded;
              const svn_string_t *decoded;

              encoded.data = info->prop_val;
              encoded.len = info->prop_val_len;

              decoded = svn_base64_decode_string(&encoded, parser->state->pool);

              info->prop_val = decoded->data;
              info->prop_val_len = decoded->len;
            }
          else
            {
              return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA,
                                       NULL,
                                       _("Got unrecognized encoding '%s'"),
                                       info->prop_encoding);
            }

        }

      set_val = apr_pmemdup(pool, info->prop_val, info->prop_val_len);
      set_val_str = svn_string_ncreate(set_val, info->prop_val_len, pool);

      svn_ra_serf__set_ver_prop(props, info->base_name, info->base_rev,
                                ns->namespace, ns->url, set_val_str, pool);
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == IGNORE_PROP_NAME || state == NEED_PROP_NAME)
    {
      svn_ra_serf__xml_pop_state(parser);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
cdata_report(svn_ra_serf__xml_parser_t *parser,
             void *userData,
             const char *data,
             apr_size_t len)
{
  report_context_t *ctx = userData;

  UNUSED_CTX(ctx);

  if (parser->state->current_state == PROP)
    {
      report_info_t *info = parser->state->private;

      svn_ra_serf__expand_string(&info->prop_val, &info->prop_val_len,
                                 data, len, parser->state->pool);
    }

  return SVN_NO_ERROR;
}


/** Editor callbacks given to callers to create request body */

static svn_error_t *
set_path(void *report_baton,
         const char *path,
         svn_revnum_t revision,
         svn_depth_t depth,
         svn_boolean_t start_empty,
         const char *lock_token,
         apr_pool_t *pool)
{
  report_context_t *report = report_baton;

  /* Copy data to report pool. */
  lock_token = apr_pstrdup(report->pool, lock_token);
  path  = apr_pstrdup(report->pool, path);

  svn_ra_serf__add_open_tag_buckets(report->buckets, report->sess->bkt_alloc,
                                    "S:entry",
                                    "rev", apr_ltoa(report->pool, revision),
                                    "lock-token", lock_token,
                                    "depth", svn_depth_to_word(depth),
                                    "start-empty", start_empty ? "true" : NULL,
                                    NULL);

  if (lock_token)
    {
      apr_hash_set(report->lock_path_tokens,
                   path,
                   APR_HASH_KEY_STRING,
                   lock_token);
    }

  svn_ra_serf__add_cdata_len_buckets(report->buckets, report->sess->bkt_alloc,
                                     path, strlen(path));

  svn_ra_serf__add_close_tag_buckets(report->buckets, report->sess->bkt_alloc,
                                     "S:entry");

  return APR_SUCCESS;
}

static svn_error_t *
delete_path(void *report_baton,
            const char *path,
            apr_pool_t *pool)
{
  report_context_t *report = report_baton;

  svn_ra_serf__add_tag_buckets(report->buckets, "S:missing",
                               apr_pstrdup(report->pool, path),
                               report->sess->bkt_alloc);
  return APR_SUCCESS;
}

static svn_error_t *
link_path(void *report_baton,
          const char *path,
          const char *url,
          svn_revnum_t revision,
          svn_depth_t depth,
          svn_boolean_t start_empty,
          const char *lock_token,
          apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  const char *link, *vcc_url;
  apr_uri_t uri;
  apr_status_t status;

  /* We need to pass in the baseline relative path.
   *
   * TODO Confirm that it's on the same server?
   */
  status = apr_uri_parse(pool, url, &uri);
  if (status)
    {
      return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                               _("Unable to parse URL '%s'"), url);
    }

  SVN_ERR(svn_ra_serf__discover_root(&vcc_url, &link, report->sess,
                                     report->sess->conns[0], uri.path, pool));

  /* Copy parameters to reporter's pool. */
  lock_token = apr_pstrdup(report->pool, lock_token);
  link = apr_pstrcat(report->pool, "/", link, NULL);
  path = apr_pstrdup(report->pool, path);

  svn_ra_serf__add_open_tag_buckets(report->buckets, report->sess->bkt_alloc,
                                    "S:entry",
                                    "rev", apr_ltoa(report->pool, revision),
                                    "lock-token", lock_token,
                                    "depth", svn_depth_to_word(depth),
                                    "start-empty", start_empty ? "true" : NULL,
                                    "linkpath", link,
                                    NULL);

  if (lock_token)
    {
      apr_hash_set(report->lock_path_tokens,
                   path,
                   APR_HASH_KEY_STRING,
                   lock_token);
    }

  svn_ra_serf__add_cdata_len_buckets(report->buckets, report->sess->bkt_alloc,
                                     path, strlen(path));

  svn_ra_serf__add_close_tag_buckets(report->buckets, report->sess->bkt_alloc,
                                     "S:entry");

  return APR_SUCCESS;
}

/** Max. number of connctions we'll open to the server. */
#define MAX_NR_OF_CONNS 4
/** Minimum nr. of outstanding requests needed before a new connection is
 *  opened. */
#define REQS_PER_CONN 8

/** This function creates a new connection for this serf session, but only
 * if the number of ACTIVE_REQS > REQS_PER_CONN or if there currently is
 * only one main connection open.
 */
static void
open_connection_if_needed(svn_ra_serf__session_t *sess, int active_reqs)
{
  /* For each REQS_PER_CONN outstanding requests open a new connection, with
   * a minimum of 1 extra connection. */
  if (sess->num_conns == 1 ||
      ((active_reqs / REQS_PER_CONN) > sess->num_conns))
    {
      int cur = sess->num_conns;

      sess->conns[cur] = apr_palloc(sess->pool, sizeof(*sess->conns[cur]));
      sess->conns[cur]->bkt_alloc = serf_bucket_allocator_create(sess->pool,
                                                                 NULL, NULL);
      sess->conns[cur]->address = sess->conns[0]->address;
      sess->conns[cur]->hostinfo = sess->conns[0]->hostinfo;
      sess->conns[cur]->using_ssl = sess->conns[0]->using_ssl;
      sess->conns[cur]->using_compression = sess->conns[0]->using_compression;
      sess->conns[cur]->proxy_auth_header = sess->conns[0]->proxy_auth_header;
      sess->conns[cur]->proxy_auth_value = sess->conns[0]->proxy_auth_value;
      sess->conns[cur]->useragent = sess->conns[0]->useragent;
      sess->conns[cur]->last_status_code = -1;
      sess->conns[cur]->ssl_context = NULL;
      sess->conns[cur]->session = sess;
      sess->conns[cur]->conn = serf_connection_create(sess->context,
                                                      sess->conns[cur]->address,
                                                      svn_ra_serf__conn_setup,
                                                      sess->conns[cur],
                                                      svn_ra_serf__conn_closed,
                                                      sess->conns[cur],
                                                      sess->pool);
      sess->num_conns++;

      /* Authentication protocol specific initalization. */
      if (sess->auth_protocol)
        sess->auth_protocol->init_conn_func(sess, sess->conns[cur], sess->pool);
      if (sess->proxy_auth_protocol)
        sess->proxy_auth_protocol->init_conn_func(sess, sess->conns[cur],
                                                  sess->pool);
    }
}

static svn_error_t *
finish_report(void *report_baton,
              apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  svn_ra_serf__session_t *sess = report->sess;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_parser_t *parser_ctx;
  svn_ra_serf__list_t *done_list;
  const char *vcc_url;
  apr_hash_t *props;
  apr_status_t status;
  svn_boolean_t closed_root;
  int status_code, i;

  svn_ra_serf__add_close_tag_buckets(report->buckets, report->sess->bkt_alloc,
                                     "S:update-report");

  props = apr_hash_make(pool);

  SVN_ERR(svn_ra_serf__discover_root(&vcc_url, NULL, sess, sess->conns[0],
                                      sess->repos_url.path, pool));

  if (!vcc_url)
    {
      return svn_error_create(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
                              _("The OPTIONS response did not include the "
                                "requested version-controlled-configuration "
                                "value"));
    }

  /* create and deliver request */
  report->path = vcc_url;

  handler = apr_pcalloc(pool, sizeof(*handler));

  handler->method = "REPORT";
  handler->path = report->path;
  handler->body_buckets = report->buckets;
  handler->body_type = "text/xml";
  handler->conn = sess->conns[0];
  handler->session = sess;

  parser_ctx = apr_pcalloc(pool, sizeof(*parser_ctx));

  parser_ctx->pool = pool;
  parser_ctx->user_data = report;
  parser_ctx->start = start_report;
  parser_ctx->end = end_report;
  parser_ctx->cdata = cdata_report;
  parser_ctx->done = &report->done;
  /* While we provide a location here to store the status code, we don't
     do anything with it. The error in parser_ctx->error is sufficient. */
  parser_ctx->status_code = &status_code;

  handler->response_handler = svn_ra_serf__handle_xml_parser;
  handler->response_baton = parser_ctx;

  svn_ra_serf__request_create(handler);

  /* Open the first extra connection. */
  open_connection_if_needed(sess, 0);

  sess->cur_conn = 1;
  closed_root = FALSE;

  while (!report->done || report->active_fetches || report->active_propfinds)
    {
      status = serf_context_run(sess->context, SERF_DURATION_FOREVER, pool);
      if (APR_STATUS_IS_TIMEUP(status))
        {
          continue;
        }
      if (status)
        {
          if (parser_ctx->error)
            svn_error_clear(sess->pending_error);

          SVN_ERR(parser_ctx->error);
          SVN_ERR(sess->pending_error);

          return svn_error_wrap_apr(status, _("Error retrieving REPORT (%d)"),
                                    status);
        }

      /* Open extra connections if we have enough requests to send. */
      if (sess->num_conns < MAX_NR_OF_CONNS)
        open_connection_if_needed(sess, report->active_fetches +
                                        report->active_propfinds);

      /* Switch our connection. */
      if (!report->done)
         if (++sess->cur_conn == sess->num_conns)
             sess->cur_conn = 1;

      /* prune our propfind list if they are done. */
      done_list = report->done_propfinds;
      while (done_list)
        {
          report->active_propfinds--;

          /* If we have some files that we won't be fetching the content
           * for, ensure that we update the file with any altered props.
           */
          if (report->file_propchanges_only)
            {
              svn_ra_serf__list_t *cur, *prev;

              prev = NULL;
              cur = report->file_propchanges_only;

              while (cur)
                {
                  report_info_t *item = cur->data;

                  if (item->propfind == done_list->data)
                    {
                      break;
                    }

                  prev = cur;
                  cur = cur->next;
                }

              /* If we found a match, set the new props and remove this
               * propchange from our list.
               */
              if (cur)
                {
                  SVN_ERR(handle_propchange_only(cur->data));

                  if (!prev)
                    {
                      report->file_propchanges_only = cur->next;
                    }
                  else
                    {
                      prev->next = cur->next;
                    }
                }
            }

          done_list = done_list->next;
        }
      report->done_propfinds = NULL;

      /* prune our fetches list if they are done. */
      done_list = report->done_fetches;
      while (done_list)
        {
          report_fetch_t *done_fetch = done_list->data;
          report_dir_t *cur_dir;

          if (done_fetch->err)
            {
              svn_error_t *err = done_fetch->err;
              /* Error found. There might be more, clear those first. */
              done_list = done_list->next;
              while (done_list)
                {
                  done_fetch = done_list->data;
                  if (done_fetch->err)
                    svn_error_clear(done_fetch->err);
                  done_list = done_list->next;
                }
              return err;
            }

          /* decrease our parent's directory refcount. */
          cur_dir = done_fetch->info->dir;
          cur_dir->ref_count--;

          /* Decrement our active fetch count. */
          report->active_fetches--;

          done_list = done_list->next;

          /* If we have a valid directory and
           * we have no open items in this dir and
           * we've closed the directory tag (no more children can be added)
           * and either:
           *   we know we won't be fetching props or
           *   we've already completed the propfind
           * then, we know it's time for us to close this directory.
           */
          while (cur_dir && !cur_dir->ref_count && cur_dir->tag_closed &&
                 (!cur_dir->fetch_props ||
                  svn_ra_serf__propfind_is_done(cur_dir->propfind)))
            {
              report_dir_t *parent = cur_dir->parent_dir;

              SVN_ERR(close_dir(cur_dir));
              if (parent)
                {
                  parent->ref_count--;
                }
              else
                {
                  closed_root = TRUE;
                }
              cur_dir = parent;
            }
        }
      report->done_fetches = NULL;

      /* Debugging purposes only! */
      serf_debug__closed_conn(sess->bkt_alloc);
      for (i = 0; i < sess->num_conns; i++)
        {
         serf_debug__closed_conn(sess->conns[i]->bkt_alloc);
        }
    }

  /* Ensure that we opened and closed our root dir and that we closed
   * all of our children. */
  if (closed_root == FALSE && report->root_dir != NULL)
    {
      SVN_ERR(close_all_dirs(report->root_dir));
    }

  /* FIXME subpool */
  return report->update_editor->close_edit(report->update_baton, sess->pool);
}
#undef MAX_NR_OF_CONNS
#undef EXP_REQS_PER_CONN

static svn_error_t *
abort_report(void *report_baton,
             apr_pool_t *pool)
{
#if 0
  report_context_t *report = report_baton;
#endif
  SVN_ERR_MALFUNCTION();
}

static const svn_ra_reporter3_t ra_serf_reporter = {
  set_path,
  delete_path,
  link_path,
  finish_report,
  abort_report
};


/** RA function implementations and body */

static svn_error_t *
make_update_reporter(svn_ra_session_t *ra_session,
                     const svn_ra_reporter3_t **reporter,
                     void **report_baton,
                     svn_revnum_t revision,
                     const char *src_path,
                     const char *dest_path,
                     const char *update_target,
                     svn_depth_t depth,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t text_deltas,
                     svn_boolean_t send_copyfrom_args,
                     const svn_delta_editor_t *update_editor,
                     void *update_baton,
                     apr_pool_t *pool)
{
  report_context_t *report;
  const svn_delta_editor_t *filter_editor;
  void *filter_baton;
  svn_boolean_t has_target = *update_target != '\0';
  svn_boolean_t server_supports_depth;
  svn_ra_serf__session_t *sess = ra_session->priv;

  SVN_ERR(svn_ra_serf__has_capability(ra_session, &server_supports_depth,
                                      SVN_RA_CAPABILITY_DEPTH, pool));
  /* We can skip the depth filtering when the user requested
     depth_files or depth_infinity because the server will
     transmit the right stuff anyway. */
  if ((depth != svn_depth_files)
      && (depth != svn_depth_infinity)
      && ! server_supports_depth)
    {
      SVN_ERR(svn_delta_depth_filter_editor(&filter_editor,
                                            &filter_baton,
                                            update_editor,
                                            update_baton,
                                            depth, has_target,
                                            sess->pool));
      update_editor = filter_editor;
      update_baton = filter_baton;
    }

  report = apr_pcalloc(pool, sizeof(*report));
  report->pool = pool;
  report->sess = sess;
  report->conn = report->sess->conns[0];
  report->target_rev = revision;
  report->ignore_ancestry = ignore_ancestry;
  report->send_copyfrom_args = send_copyfrom_args;
  report->text_deltas = text_deltas;
  report->lock_path_tokens = apr_hash_make(pool);

  report->source = src_path;
  report->destination = dest_path;
  report->update_target = update_target;

  report->update_editor = update_editor;
  report->update_baton = update_baton;
  report->done = FALSE;

  *reporter = &ra_serf_reporter;
  *report_baton = report;

  report->buckets = serf_bucket_aggregate_create(report->sess->bkt_alloc);

  svn_ra_serf__add_open_tag_buckets(report->buckets, report->sess->bkt_alloc,
                                    "S:update-report",
                                    "xmlns:S", SVN_XML_NAMESPACE,
                                    NULL);

  svn_ra_serf__add_tag_buckets(report->buckets,
                               "S:src-path", report->source,
                               report->sess->bkt_alloc);

  if (SVN_IS_VALID_REVNUM(report->target_rev))
    {
      svn_ra_serf__add_tag_buckets(report->buckets,
                                   "S:target-revision",
                                   apr_ltoa(pool, report->target_rev),
                                   report->sess->bkt_alloc);
    }

  if (report->destination && *report->destination)
    {
      svn_ra_serf__add_tag_buckets(report->buckets,
                                   "S:dst-path",
                                   report->destination,
                                   report->sess->bkt_alloc);
    }

  if (report->update_target && *report->update_target)
    {
      svn_ra_serf__add_tag_buckets(report->buckets,
                                   "S:update-target", report->update_target,
                                   report->sess->bkt_alloc);
    }

  if (report->ignore_ancestry)
    {
      svn_ra_serf__add_tag_buckets(report->buckets,
                                   "S:ignore-ancestry", "yes",
                                   report->sess->bkt_alloc);
    }

  if (report->send_copyfrom_args)
    {
      svn_ra_serf__add_tag_buckets(report->buckets,
                                   "S:send-copyfrom-args", "yes",
                                   report->sess->bkt_alloc);
    }

  /* Old servers know "recursive" but not "depth"; help them DTRT. */
  if (depth == svn_depth_files || depth == svn_depth_empty)
    {
      svn_ra_serf__add_tag_buckets(report->buckets,
                                   "S:recursive", "no",
                                   report->sess->bkt_alloc);
    }

  svn_ra_serf__add_tag_buckets(report->buckets,
                               "S:depth", svn_depth_to_word(depth),
                               report->sess->bkt_alloc);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__do_update(svn_ra_session_t *ra_session,
                       const svn_ra_reporter3_t **reporter,
                       void **report_baton,
                       svn_revnum_t revision_to_update_to,
                       const char *update_target,
                       svn_depth_t depth,
                       svn_boolean_t send_copyfrom_args,
                       const svn_delta_editor_t *update_editor,
                       void *update_baton,
                       apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;

  return make_update_reporter(ra_session, reporter, report_baton,
                              revision_to_update_to,
                              session->repos_url.path, NULL, update_target,
                              depth, FALSE, TRUE, send_copyfrom_args,
                              update_editor, update_baton, pool);
}

svn_error_t *
svn_ra_serf__do_diff(svn_ra_session_t *ra_session,
                     const svn_ra_reporter3_t **reporter,
                     void **report_baton,
                     svn_revnum_t revision,
                     const char *diff_target,
                     svn_depth_t depth,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t text_deltas,
                     const char *versus_url,
                     const svn_delta_editor_t *diff_editor,
                     void *diff_baton,
                     apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;

  return make_update_reporter(ra_session, reporter, report_baton,
                              revision,
                              session->repos_url.path, versus_url, diff_target,
                              depth, ignore_ancestry, text_deltas, FALSE,
                              diff_editor, diff_baton, pool);
}

svn_error_t *
svn_ra_serf__do_status(svn_ra_session_t *ra_session,
                       const svn_ra_reporter3_t **reporter,
                       void **report_baton,
                       const char *status_target,
                       svn_revnum_t revision,
                       svn_depth_t depth,
                       const svn_delta_editor_t *status_editor,
                       void *status_baton,
                       apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;

  return make_update_reporter(ra_session, reporter, report_baton,
                              revision,
                              session->repos_url.path, NULL, status_target,
                              depth, FALSE, FALSE, FALSE,
                              status_editor, status_baton, pool);
}

svn_error_t *
svn_ra_serf__do_switch(svn_ra_session_t *ra_session,
                       const svn_ra_reporter3_t **reporter,
                       void **report_baton,
                       svn_revnum_t revision_to_switch_to,
                       const char *switch_target,
                       svn_depth_t depth,
                       const char *switch_url,
                       const svn_delta_editor_t *switch_editor,
                       void *switch_baton,
                       apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;

  return make_update_reporter(ra_session, reporter, report_baton,
                              revision_to_switch_to,
                              session->repos_url.path,
                              switch_url, switch_target,
                              depth, TRUE, TRUE, FALSE /* TODO(sussman) */,
                              switch_editor, switch_baton, pool);
}

svn_error_t *
svn_ra_serf__get_file(svn_ra_session_t *ra_session,
                      const char *path,
                      svn_revnum_t revision,
                      svn_stream_t *stream,
                      svn_revnum_t *fetched_rev,
                      apr_hash_t **props,
                      apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__connection_t *conn;
  svn_ra_serf__handler_t *handler;
  const char *fetch_url;
  apr_hash_t *fetch_props;

  /* What connection should we go on? */
  conn = session->conns[session->cur_conn];

  /* Fetch properties. */
  fetch_props = apr_hash_make(pool);

  fetch_url = svn_path_url_add_component(session->repos_url.path, path, pool);

  /* The simple case is if we want HEAD - then a GET on the fetch_url is fine.
   *
   * Otherwise, we need to get the baseline version for this particular
   * revision and then fetch that file.
   */
  if (SVN_IS_VALID_REVNUM(revision))
    {
      const char *vcc_url, *rel_path, *baseline_url;

      SVN_ERR(svn_ra_serf__discover_root(&vcc_url, &rel_path,
                                         session, conn, fetch_url, pool));

      SVN_ERR(svn_ra_serf__retrieve_props(fetch_props, session, conn, vcc_url,
                                          revision, "0", baseline_props, pool));

      baseline_url = svn_ra_serf__get_ver_prop(fetch_props, vcc_url, revision,
                                               "DAV:", "baseline-collection");

      fetch_url = svn_path_url_add_component(baseline_url, rel_path, pool);
      revision = SVN_INVALID_REVNUM;
    }

  /* TODO Filter out all of our props into a usable format. */
  if (props)
    {
      *props = apr_hash_make(pool);

      SVN_ERR(svn_ra_serf__retrieve_props(fetch_props, session, conn, fetch_url,
                                          revision, "0", all_props, pool));

      svn_ra_serf__walk_all_props(fetch_props, fetch_url, revision,
                                  svn_ra_serf__set_flat_props, *props, pool);
    }

  if (stream)
    {
      report_fetch_t *stream_ctx;

      /* Create the fetch context. */
      stream_ctx = apr_pcalloc(pool, sizeof(*stream_ctx));
      stream_ctx->pool = pool;
      stream_ctx->target_stream = stream;
      stream_ctx->sess = session;
      stream_ctx->conn = conn;
      stream_ctx->info = apr_pcalloc(pool, sizeof(*stream_ctx->info));
      stream_ctx->info->name = fetch_url;

      handler = apr_pcalloc(pool, sizeof(*handler));
      handler->method = "GET";
      handler->path = fetch_url;
      handler->conn = conn;
      handler->session = session;

      handler->response_handler = handle_stream;
      handler->response_baton = stream_ctx;

      handler->response_error = cancel_fetch;
      handler->response_error_baton = stream_ctx;

      svn_ra_serf__request_create(handler);

      SVN_ERR(svn_ra_serf__context_run_wait(&stream_ctx->done, session, pool));
      SVN_ERR(stream_ctx->err);
    }

  return SVN_NO_ERROR;
}
