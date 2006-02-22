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
} commit_context_t;

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

typedef struct {
  /* Pool for our file. */
  apr_pool_t *pool;

  /* The root commit we're in progress for. */
  commit_context_t *commit;

  dir_context_t *parent_dir;

  const char *name;

  /* The base revision of the file. */
  svn_revnum_t base_revision;

  /* stream */
  svn_stream_t *stream;

  /* Temporary file containing the svndiff. */
  apr_file_t *svndiff;

} file_context_t;

static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *dir_pool,
          void **root_baton)
{
  commit_context_t *ctx = edit_baton;
  options_context_t *opt_ctx;
  dir_context_t *dir;
  const char *activity_str;
  svn_boolean_t *opt_done;

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

  /* TODO: MKCOL on the activity_url */
  /* TODO: PROPGET to determine our baseline collection */
  /* TODO: CHECKOUT on the baseline collection */
  /* TODO: PROPPATCH the log message on the checked-out baseline */

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

  new_file = apr_pcalloc(file_pool, sizeof(*new_file));

  new_file->pool = file_pool;

  ctx->ref_count++;
  new_file->parent_dir = ctx;

  new_file->commit = ctx->commit;
  
  /* TODO: Remove directory names? */
  new_file->name = path;

  new_file->base_revision = base_revision;

  *file_baton = new_file;

  /* TODO: CHECKOUT the file into our activity. */

  return SVN_NO_ERROR;
}
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
  abort();
}

static apr_status_t
handle_put(serf_bucket_t *response,
           void *handler_baton,
           apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  file_context_t *ctx = file_baton;

  /* If we had a stream of changes, push them to the server... */
  if (ctx->stream) {
      serf_connection_request_create(ctx->commit->session->conns[0],
                                     setup_put, ctx);
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
  apr_status_t status;

  /* TODO: MERGE our activity */
  /* TODO: DELETE our activity */

  /* At this point, we want to run through all of our pending requests. */
  while (1)
    {
      status = serf_context_run(ctx->session->context, SERF_DURATION_FOREVER,
                                pool);
      if (APR_STATUS_IS_TIMEUP(status))
        {
          continue;
        }
      if (status)
        {
          return svn_error_wrap_apr(status, _("Error committing"));
        }

      /* Are we done or something? */

    }

  abort();
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
