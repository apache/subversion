/*
 * commit.c :  entry point for commit RA functions for ra_serf
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



#include <apr_uri.h>

#include <expat.h>

#include <serf.h>

#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_dav.h"
#include "svn_xml.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_config.h"
#include "svn_delta.h"
#include "svn_version.h"
#include "svn_path.h"
#include "svn_private_config.h"

#include "ra_serf.h"


/* Structure associated with a MKACTIVITY request. */
typedef struct {

  ra_serf_session_t *session;

  const char *activity_url;
  apr_size_t activity_url_len;

  int status;

  svn_boolean_t done;

  serf_response_acceptor_t acceptor;
  void *acceptor_baton;
  serf_response_handler_t handler;
} mkactivity_context_t;

/* Structure associated with a CHECKOUT request. */
typedef struct {

  apr_pool_t *pool;

  ra_serf_session_t *session;

  const char *activity_url;
  apr_size_t activity_url_len;

  const char *checkout_url;

  const char *resource_url;

  int status;

  svn_boolean_t done;

  serf_response_acceptor_t acceptor;
  void *acceptor_baton;
  serf_response_handler_t handler;
} checkout_context_t;


/* Structure associated with a DELETE request. */
typedef struct {
  apr_pool_t *pool;

  ra_serf_session_t *session;

  const char *activity_url;
  apr_size_t activity_url_len;

  int status;

  svn_boolean_t done;

  serf_response_acceptor_t acceptor;
  void *acceptor_baton;
  serf_response_handler_t handler;
} delete_context_t;

/* Baton passed back with the commit editor. */
typedef struct {
  /* Pool for our commit. */
  apr_pool_t *pool;

  ra_serf_session_t *session;

  const char *log_msg;

  svn_commit_callback2_t callback;
  void *callback_baton;

  apr_hash_t *lock_tokens;
  svn_boolean_t keep_locks;

  const char *uuid;
  const char *activity_url;
  apr_size_t activity_url_len;
} commit_context_t;

/* Represents a directory. */
typedef struct {
  /* Pool for our directory. */
  apr_pool_t *pool;

  /* The root commit we're in progress for. */
  commit_context_t *commit;

  /* How many pending changes we have left in this directory. */
  unsigned int ref_count;

  /* The directory name; if NULL, we're the 'root' */
  const char *name;

  /* The base revision of the dir. */
  svn_revnum_t base_revision;
} dir_context_t;

/* Represents a file to be committed. */
typedef struct {
  /* Pool for our file. */
  apr_pool_t *pool;

  /* The root commit we're in progress for. */
  commit_context_t *commit;

  dir_context_t *parent_dir;

  const char *name;

  /* The checked out context for this file. */
  checkout_context_t *checkout;

  /* The base revision of the file. */
  svn_revnum_t base_revision;

  /* stream */
  svn_stream_t *stream;

  /* Temporary file containing the svndiff. */
  apr_file_t *svndiff;

  /* Our base checksum as reported by the WC. */
  const char *base_checksum;

  /* Our resulting checksum as reported by the WC. */
  const char *result_checksum;

  /* Is our PUT completed? */
  svn_boolean_t put_done;

  /* What was the status code of our PUT? */
  int put_status;

  /* For the PUT... */
  serf_response_acceptor_t acceptor;
  void *acceptor_baton;
  serf_response_handler_t handler;
} file_context_t;


/* Setup routines and handlers for various requests we'll invoke. */

static apr_status_t
setup_mkactivity(serf_request_t *request,
                 void *setup_baton,
                 serf_bucket_t **req_bkt,
                 serf_response_acceptor_t *acceptor,
                 void **acceptor_baton,
                 serf_response_handler_t *handler,
                 void **handler_baton,
                 apr_pool_t *pool)
{
  mkactivity_context_t *ctx = setup_baton;

  setup_serf_req(request, req_bkt, NULL, ctx->session,
                 "MKACTIVITY", ctx->activity_url, NULL, NULL);

  *acceptor = ctx->acceptor;
  *acceptor_baton = ctx->acceptor_baton;
  *handler = ctx->handler;
  *handler_baton = ctx;

  return APR_SUCCESS;
}

static apr_status_t
handle_mkactivity(serf_bucket_t *response,
                  void *handler_baton,
                   apr_pool_t *pool)
{
  mkactivity_context_t *ctx = handler_baton;

  return handle_status_only(response, &ctx->status, &ctx->done, pool);
}

#define CHECKOUT_HEADER "<?xml version=\"1.0\" encoding=\"utf-8\"?><D:checkout xmlns:D=\"DAV:\"><D:activity-set><D:href>"
  
#define CHECKOUT_TRAILER "</D:href></D:activity-set></D:checkout>"

static apr_status_t
setup_checkout(serf_request_t *request,
               void *setup_baton,
               serf_bucket_t **req_bkt,
               serf_response_acceptor_t *acceptor,
               void **acceptor_baton,
               serf_response_handler_t *handler,
               void **handler_baton,
               apr_pool_t *pool)
{
  checkout_context_t *ctx = setup_baton;
  serf_bucket_t *body_bkt, *tmp_bkt;
  serf_bucket_alloc_t *alloc;

  alloc = serf_request_get_alloc(request);

  body_bkt = serf_bucket_aggregate_create(alloc);

  tmp_bkt = SERF_BUCKET_SIMPLE_STRING_LEN(CHECKOUT_HEADER,
                                          sizeof(CHECKOUT_HEADER) - 1,
                                          alloc);
  serf_bucket_aggregate_append(body_bkt, tmp_bkt);

  tmp_bkt = SERF_BUCKET_SIMPLE_STRING_LEN(ctx->activity_url,
                                          ctx->activity_url_len,
                                          alloc);
  serf_bucket_aggregate_append(body_bkt, tmp_bkt);

  tmp_bkt = SERF_BUCKET_SIMPLE_STRING_LEN(CHECKOUT_TRAILER,
                                          sizeof(CHECKOUT_TRAILER) - 1,
                                          alloc);
  serf_bucket_aggregate_append(body_bkt, tmp_bkt);

  setup_serf_req(request, req_bkt, NULL, ctx->session,
                 "CHECKOUT", ctx->checkout_url, body_bkt, "text/xml");

  *acceptor = ctx->acceptor;
  *acceptor_baton = ctx->acceptor_baton;
  *handler = ctx->handler;
  *handler_baton = ctx;

  return APR_SUCCESS;
}

static apr_status_t
handle_checkout(serf_bucket_t *response,
                void *handler_baton,
                apr_pool_t *pool)
{
  checkout_context_t *ctx = handler_baton;
  apr_status_t status;

  status = handle_status_only(response, &ctx->status, &ctx->done, pool);

  /* Get the resulting location. */
  if (ctx->done)
    {
      serf_bucket_t *hdrs;
      apr_uri_t uri;
      const char *location;

      hdrs = serf_bucket_response_get_headers(response);
      location = serf_bucket_headers_get(hdrs, "Location");
      if (!location)
        {
          abort();
        }
      apr_uri_parse(pool, location, &uri);

      ctx->resource_url = apr_pstrdup(ctx->pool, uri.path);
    }

  return status;
}

static apr_status_t
setup_put(serf_request_t *request,
          void *setup_baton,
          serf_bucket_t **req_bkt,
          serf_response_acceptor_t *acceptor,
          void **acceptor_baton,
          serf_response_handler_t *handler,
          void **handler_baton,
          apr_pool_t *pool)
{
  file_context_t *ctx = setup_baton;
  serf_bucket_alloc_t *alloc;
  serf_bucket_t *body_bkt, *hdrs_bkt;
  apr_off_t offset;

  alloc = serf_request_get_alloc(request);

  /* We need to flush the file, make it unbuffered (so that it can be
   * zero-copied via mmap), and reset the position before attempting to
   * deliver the file.
   */
  apr_file_flush(ctx->svndiff);
  apr_file_buffer_set(ctx->svndiff, NULL, 0);
  offset = 0;
  apr_file_seek(ctx->svndiff, APR_SET, &offset);

  body_bkt = serf_bucket_file_create(ctx->svndiff, alloc);

  setup_serf_req(request, req_bkt, &hdrs_bkt, ctx->commit->session,
                 "PUT", ctx->checkout->resource_url, body_bkt,
                 "application/vnd.svn-svndiff");

  if (ctx->base_checksum)
  {
    serf_bucket_headers_set(hdrs_bkt, SVN_DAV_BASE_FULLTEXT_MD5_HEADER,
                            ctx->base_checksum);
  }

  if (ctx->result_checksum)
  {
    serf_bucket_headers_set(hdrs_bkt, SVN_DAV_RESULT_FULLTEXT_MD5_HEADER,
                            ctx->result_checksum);
  }

  *acceptor = ctx->acceptor;
  *acceptor_baton = ctx->acceptor_baton;
  *handler = ctx->handler;
  *handler_baton = ctx;

  return APR_SUCCESS;
}

static apr_status_t
handle_put(serf_bucket_t *response,
           void *handler_baton,
           apr_pool_t *pool)
{
  file_context_t *ctx = handler_baton;

  return handle_status_only(response, &ctx->put_status, &ctx->put_done, pool);
}

static apr_status_t
setup_delete(serf_request_t *request,
             void *setup_baton,
             serf_bucket_t **req_bkt,
             serf_response_acceptor_t *acceptor,
             void **acceptor_baton,
             serf_response_handler_t *handler,
             void **handler_baton,
             apr_pool_t *pool)
{
  delete_context_t *ctx = setup_baton;

  setup_serf_req(request, req_bkt, NULL, ctx->session,
                 "DELETE", ctx->activity_url, NULL, NULL);

  *acceptor = ctx->acceptor;
  *acceptor_baton = ctx->acceptor_baton;
  *handler = ctx->handler;
  *handler_baton = ctx;

  return APR_SUCCESS;
}

static apr_status_t
handle_delete(serf_bucket_t *response,
              void *handler_baton,
              apr_pool_t *pool)
{
  delete_context_t *ctx = handler_baton;

  return handle_status_only(response, &ctx->status, &ctx->done, pool);
}

/* Helper function to write the svndiff stream to temporary file. */
static svn_error_t *
svndiff_stream_write(void *file_baton,
                     const char *data,
                     apr_size_t *len)
{
  file_context_t *ctx = file_baton;
  apr_status_t status;

  status = apr_file_write_full(ctx->svndiff, data, *len, NULL);
  if (status)
      return svn_error_wrap_apr(status, _("Failed writing updated file"));

  return SVN_NO_ERROR;
}



/* Commit baton callbacks */

static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *dir_pool,
          void **root_baton)
{
  commit_context_t *ctx = edit_baton;
  options_context_t *opt_ctx;
  mkactivity_context_t *mkact_ctx;
  checkout_context_t *checkout_ctx;
  dir_context_t *dir;
  const char *activity_str;
  const char *vcc_url, *baseline_url, *version_name;
  svn_boolean_t *opt_done;
  apr_hash_t *props;

  dir = apr_pcalloc(dir_pool, sizeof(*dir));

  dir->pool = dir_pool;
  dir->commit = ctx;
  dir->base_revision = base_revision;

  *root_baton = dir;

  /* Create a UUID for this commit. */
  ctx->uuid = svn_uuid_generate(ctx->pool);

  create_options_req(&opt_ctx, ctx->session, ctx->session->conns[0],
                     ctx->session->repos_url.path, ctx->pool);

  SVN_ERR(context_run_wait(get_options_done_ptr(opt_ctx), ctx->session,
                           ctx->pool));

  activity_str = options_get_activity_collection(opt_ctx);

  if (!activity_str)
    {
      abort();
    }

  ctx->activity_url = svn_path_url_add_component(activity_str,
                                                 ctx->uuid, ctx->pool);
  ctx->activity_url_len = strlen(ctx->activity_url);

  /* Create our activity URL now on the server. */
  mkact_ctx = apr_pcalloc(ctx->pool, sizeof(*mkact_ctx));
  mkact_ctx->session = ctx->session;

  mkact_ctx->acceptor = accept_response;
  mkact_ctx->acceptor_baton = ctx->session;
  mkact_ctx->handler = handle_mkactivity;
  mkact_ctx->activity_url = ctx->activity_url;
  mkact_ctx->activity_url_len = ctx->activity_url_len;

  serf_connection_request_create(ctx->session->conns[0],
                                 setup_mkactivity, mkact_ctx);

  SVN_ERR(context_run_wait(&mkact_ctx->done, ctx->session, ctx->pool));

  if (mkact_ctx->status != 201) {
      abort();
  }

  /* Now go fetch our VCC and baseline so we can do a CHECKOUT. */
  props = apr_hash_make(ctx->pool);

  SVN_ERR(retrieve_props(props, ctx->session, ctx->session->conns[0],
                         ctx->session->repos_url.path,
                         SVN_INVALID_REVNUM, "0", base_props, ctx->pool));

  vcc_url = get_prop(props, ctx->session->repos_url.path, "DAV:",
                     "version-controlled-configuration");

  if (!vcc_url)
    {
      abort();
    }

  /* Using the version-controlled-configuration, fetch the checked-in prop. */
  SVN_ERR(retrieve_props(props, ctx->session, ctx->session->conns[0],
                         vcc_url, SVN_INVALID_REVNUM, "0",
                         checked_in_props, ctx->pool));

  baseline_url = get_prop(props, vcc_url, "DAV:", "checked-in");

  if (!baseline_url)
    {
      abort();
    }

  /* Checkout our baseline into our activity URL now. */
  checkout_ctx = apr_pcalloc(ctx->pool, sizeof(*checkout_ctx));

  checkout_ctx->pool = ctx->pool;
  checkout_ctx->session = ctx->session;

  checkout_ctx->acceptor = accept_response;
  checkout_ctx->acceptor_baton = ctx->session;
  checkout_ctx->handler = handle_checkout;
  checkout_ctx->activity_url = ctx->activity_url;
  checkout_ctx->activity_url_len = ctx->activity_url_len;
  checkout_ctx->checkout_url = baseline_url;

  serf_connection_request_create(ctx->session->conns[0],
                                 setup_checkout, checkout_ctx);

  SVN_ERR(context_run_wait(&checkout_ctx->done, ctx->session, ctx->pool));

  if (checkout_ctx->status != 201) {
      abort();
  }


  /* TODO: PROPPATCH the log message on the checked-out baseline */
  printf("PROPPATCH on %s\n", checkout_ctx->resource_url);

  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  dir_context_t *ctx = parent_baton;

  abort();
}

static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *dir_pool,
              void **child_baton)
{
  dir_context_t *ctx = parent_baton;

  abort();
}

static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *dir_pool,
               void **child_baton)
{
  dir_context_t *ctx = parent_baton;

  abort();
}

static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  dir_context_t *ctx = dir_baton;

  abort();
}

static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  dir_context_t *ctx = dir_baton;

  /* Huh?  We're going to be called before the texts are sent.  Ugh.
   * Therefore, just wave politely at our caller.
   */

  return SVN_NO_ERROR;
}

static svn_error_t *
absent_directory(const char *path,
                 void *parent_baton,
                 apr_pool_t *pool)
{
  dir_context_t *ctx = parent_baton;

  abort();
}

static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copy_path,
         svn_revnum_t copy_revision,
         apr_pool_t *file_pool,
         void **file_baton)
{
  dir_context_t *ctx = parent_baton;

  abort();
}

static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *file_pool,
          void **file_baton)
{
  dir_context_t *ctx = parent_baton;
  file_context_t *new_file;
  checkout_context_t *checkout_ctx;
  const char *baseline_url;
  apr_hash_t *props;

  new_file = apr_pcalloc(file_pool, sizeof(*new_file));

  new_file->pool = file_pool;

  ctx->ref_count++;
  new_file->parent_dir = ctx;

  new_file->commit = ctx->commit;
  
  /* TODO: Remove directory names? */
  new_file->name = path;

  new_file->base_revision = base_revision;

  /* Set up so that we can perform the PUT later. */
  new_file->acceptor = accept_response;
  new_file->acceptor_baton = ctx->commit->session;
  new_file->handler = handle_put;

  /* Fetch the root checked-in property. */
  props = apr_hash_make(new_file->pool);

  SVN_ERR(retrieve_props(props, new_file->commit->session,
                         new_file->commit->session->conns[0],
                         new_file->commit->session->repos_url.path,
                         SVN_INVALID_REVNUM, "0", checked_in_props,
                         new_file->pool));

  baseline_url = get_prop(props, new_file->commit->session->repos_url.path,
                          "DAV:", "checked-in");

  if (!baseline_url)
    {
      abort();
    }

  
  /* CHECKOUT the file into our activity. */
  checkout_ctx = apr_pcalloc(new_file->pool, sizeof(*checkout_ctx));

  checkout_ctx->pool = new_file->pool;
  checkout_ctx->session = new_file->commit->session;

  checkout_ctx->acceptor = accept_response;
  checkout_ctx->acceptor_baton = new_file->commit->session;
  checkout_ctx->handler = handle_checkout;
  checkout_ctx->activity_url = new_file->commit->activity_url;
  checkout_ctx->activity_url_len = new_file->commit->activity_url_len;

  /* Append our file name to the baseline to get the resulting checkout. */
  checkout_ctx->checkout_url = apr_pstrcat(new_file->pool,
                                           baseline_url, path, NULL);

  serf_connection_request_create(new_file->commit->session->conns[0],
                                 setup_checkout, checkout_ctx);

  /* There's no need to wait here as we only need this when we start the
   * PROPPATCH or PUT of the file.
   */
  SVN_ERR(context_run_wait(&checkout_ctx->done, new_file->commit->session,
                           new_file->pool));

  if (checkout_ctx->status != 201) {
      abort();
  }

  new_file->checkout = checkout_ctx;

  *file_baton = new_file;

  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  file_context_t *ctx = file_baton;
  const svn_ra_callbacks2_t *wc_callbacks;
  void *wc_callback_baton;

  /* Store the stream in a temporary file; we'll give it to serf when we
   * close this file.
   *
   * TODO: There should be a way we can stream the request body instead of
   * writing to a temporary file (ugh). A special svn stream serf bucket
   * that returns EAGAIN until we receive the done call?  But, when
   * would we run through the serf context?  Grr.
   */
  wc_callbacks = ctx->commit->session->wc_callbacks;
  wc_callback_baton = ctx->commit->session->wc_callback_baton;
  SVN_ERR(wc_callbacks->open_tmp_file(&ctx->svndiff,
                                      wc_callback_baton,
                                      ctx->pool));

  ctx->stream = svn_stream_create(ctx, pool);
  svn_stream_set_write(ctx->stream, svndiff_stream_write);

  svn_txdelta_to_svndiff(ctx->stream, pool, handler, handler_baton);

  ctx->base_checksum = base_checksum;

  return SVN_NO_ERROR;
}

static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  file_context_t *ctx = file_baton;

  abort();
}

static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  file_context_t *ctx = file_baton;

  ctx->result_checksum = text_checksum;

  /* If we had a stream of changes, push them to the server... */
  if (ctx->stream) {
      serf_connection_request_create(ctx->commit->session->conns[0],
                                     setup_put, ctx);

      SVN_ERR(context_run_wait(&ctx->put_done, ctx->commit->session,
                               ctx->pool));

      if (ctx->put_status != 204)
        {
          abort();
        }
  }

  /* TODO: If we had any prop changes, push them via PROPPATCH. */

  return SVN_NO_ERROR;
}

static svn_error_t *
absent_file(const char *path,
            void *parent_baton,
            apr_pool_t *pool)
{
  dir_context_t *ctx = parent_baton;

  abort();
}

static svn_error_t *
close_edit(void *edit_baton, 
           apr_pool_t *pool)
{
  commit_context_t *ctx = edit_baton;
  merge_context_t *merge_ctx;
  delete_context_t *delete_ctx;
  svn_boolean_t *merge_done;
  apr_status_t status;

  /* MERGE our activity */
  SVN_ERR(merge_create_req(&merge_ctx, ctx->session,
                           ctx->session->conns[0],
                           ctx->session->repos_url.path,
                           ctx->activity_url, ctx->activity_url_len,
                           pool));

  merge_done = merge_get_done_ptr(merge_ctx);
 
  SVN_ERR(context_run_wait(merge_done, ctx->session, pool));

  if (merge_get_status(merge_ctx) != 200)
    {
      abort();
    }

  /* Inform the WC that we did a commit.  */
  ctx->callback(merge_get_commit_info(merge_ctx), ctx->callback_baton, pool);

  /* DELETE our activity */
  delete_ctx = apr_pcalloc(pool, sizeof(*delete_ctx));

  delete_ctx->pool = pool;
  delete_ctx->session = ctx->session;

  delete_ctx->acceptor = accept_response;
  delete_ctx->acceptor_baton = ctx->session;
  delete_ctx->handler = handle_delete;
  delete_ctx->activity_url = ctx->activity_url;
  delete_ctx->activity_url_len = ctx->activity_url_len;

  serf_connection_request_create(ctx->session->conns[0],
                                 setup_delete, delete_ctx);

  SVN_ERR(context_run_wait(&delete_ctx->done, ctx->session, pool));

  if (delete_ctx->status != 204)
    {
      abort();
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
abort_edit(void *edit_baton,
           apr_pool_t *pool)
{
  commit_context_t *ctx = edit_baton;

  abort();
}

svn_error_t *
svn_ra_serf__get_commit_editor(svn_ra_session_t *ra_session,
                               const svn_delta_editor_t **ret_editor,
                               void **edit_baton,
                               const char *log_msg,
                               svn_commit_callback2_t callback,
                               void *callback_baton,
                               apr_hash_t *lock_tokens,
                               svn_boolean_t keep_locks,
                               apr_pool_t *pool)
{
  ra_serf_session_t *session = ra_session->priv;
  svn_delta_editor_t *editor;
  commit_context_t *ctx;

  ctx = apr_pcalloc(pool, sizeof(commit_context_t));

  ctx->pool = pool;

  ctx->session = session;
  ctx->log_msg = log_msg;

  ctx->callback = callback;
  ctx->callback_baton = callback_baton;

  ctx->lock_tokens = lock_tokens;
  ctx->keep_locks = keep_locks;

  editor = svn_delta_default_editor(pool);
  editor->open_root = open_root;
  editor->delete_entry = delete_entry;
  editor->add_directory = add_directory;
  editor->open_directory = open_directory;
  editor->change_dir_prop = change_dir_prop;
  editor->close_directory = close_directory;
  editor->absent_directory = absent_directory;
  editor->add_file = add_file;
  editor->open_file = open_file;
  editor->apply_textdelta = apply_textdelta;
  editor->change_file_prop = change_file_prop;
  editor->close_file = close_file;
  editor->absent_file = absent_file;
  editor->close_edit = close_edit;
  editor->abort_edit = abort_edit;

  *ret_editor = editor;
  *edit_baton = ctx;

  return SVN_NO_ERROR;
}
