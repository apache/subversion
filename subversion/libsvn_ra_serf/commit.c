/*
 * commit.c :  entry point for commit RA functions for ra_serf
 *
 * ====================================================================
 * Copyright (c) 2006, 2008 CollabNet.  All rights reserved.
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
#include "svn_config.h"
#include "svn_delta.h"
#include "svn_base64.h"
#include "svn_version.h"
#include "svn_path.h"
#include "svn_props.h"

#include "svn_private_config.h"
#include "private/svn_dep_compat.h"

#include "ra_serf.h"
#include "../libsvn_ra/ra_loader.h"


/* Structure associated with a CHECKOUT request. */
typedef struct {

  apr_pool_t *pool;

  const char *activity_url;
  apr_size_t activity_url_len;

  const char *checkout_url;

  const char *resource_url;

  svn_ra_serf__simple_request_context_t progress;

} checkout_context_t;

/* Baton passed back with the commit editor. */
typedef struct {
  /* Pool for our commit. */
  apr_pool_t *pool;

  svn_ra_serf__session_t *session;
  svn_ra_serf__connection_t *conn;

  apr_hash_t *revprop_table;

  svn_commit_callback2_t callback;
  void *callback_baton;

  apr_hash_t *lock_tokens;
  svn_boolean_t keep_locks;

  const char *uuid;
  const char *activity_url;
  apr_size_t activity_url_len;

  /* The checkout for the baseline. */
  checkout_context_t *baseline;

  /* The checked-in root to base CHECKOUTs from */
  const char *checked_in_url;

  /* The root baseline collection */
  const char *baseline_url;

  /* Deleted files - so we can detect delete+add (replace) ops. */
  apr_hash_t *deleted_entries;

  /* Copied entries - so we do not checkout these resources. */
  apr_hash_t *copied_entries;
} commit_context_t;

/* Structure associated with a PROPPATCH request. */
typedef struct {
  apr_pool_t *pool;

  const char *name;
  const char *path;

  commit_context_t *commit;

  /* Changed and removed properties. */
  apr_hash_t *changed_props;
  apr_hash_t *removed_props;

  svn_ra_serf__simple_request_context_t progress;
} proppatch_context_t;

typedef struct {
  const char *path;

  svn_revnum_t revision;

  const char *lock_token;
  apr_hash_t *lock_token_hash;
  svn_boolean_t keep_locks;

  svn_ra_serf__simple_request_context_t progress;
} delete_context_t;

/* Represents a directory. */
typedef struct dir_context_t {
  /* Pool for our directory. */
  apr_pool_t *pool;

  /* The root commit we're in progress for. */
  commit_context_t *commit;

  /* The checked out context for this directory.
   *
   * May be NULL; if so call checkout_dir() first.
   */
  checkout_context_t *checkout;

  /* Our URL to CHECKOUT */
  const char *checked_in_url;

  /* How many pending changes we have left in this directory. */
  unsigned int ref_count;

  /* Is this directory being added?  (Otherwise, just opened.) */
  svn_boolean_t added;

  /* Our parent */
  struct dir_context_t *parent_dir;

  /* The directory name; if NULL, we're the 'root' */
  const char *name;

  /* The base revision of the dir. */
  svn_revnum_t base_revision;

  const char *copy_path;
  svn_revnum_t copy_revision;

  /* Changed and removed properties */
  apr_hash_t *changed_props;
  apr_hash_t *removed_props;

} dir_context_t;

/* Represents a file to be committed. */
typedef struct {
  /* Pool for our file. */
  apr_pool_t *pool;

  /* The root commit we're in progress for. */
  commit_context_t *commit;

  /* Is this file being added?  (Otherwise, just opened.) */
  svn_boolean_t added;

  dir_context_t *parent_dir;

  const char *name;

  /* The checked out context for this file. */
  checkout_context_t *checkout;

  /* The base revision of the file. */
  svn_revnum_t base_revision;

  /* Copy path and revision */
  const char *copy_path;
  svn_revnum_t copy_revision;

  /* stream */
  svn_stream_t *stream;

  /* Temporary file containing the svndiff. */
  apr_file_t *svndiff;

  /* Our base checksum as reported by the WC. */
  const char *base_checksum;

  /* Our resulting checksum as reported by the WC. */
  const char *result_checksum;

  /* Changed and removed properties. */
  apr_hash_t *changed_props;
  apr_hash_t *removed_props;

  /* URL to PUT the file at. */
  const char *put_url;

} file_context_t;


/* Setup routines and handlers for various requests we'll invoke. */

static svn_error_t *
return_response_err(svn_ra_serf__handler_t *handler,
                    svn_ra_serf__simple_request_context_t *ctx)
{
  /* Try to return one of the standard errors for 301, 404, etc.,
     then look for an error embedded in the response.  */
  return svn_error_compose_create(
    svn_ra_serf__error_on_status(ctx->status, handler->path),
    svn_error_compose_create(
      ctx->server_error.error,
      svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                        "%s of '%s': %d %s",
                        handler->method, handler->path,
                        ctx->status, ctx->reason)));
}

static serf_bucket_t *
create_checkout_body(void *baton,
                     serf_bucket_alloc_t *alloc,
                     apr_pool_t *pool)
{
  checkout_context_t *ctx = baton;
  serf_bucket_t *body_bkt;

  body_bkt = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_xml_header_buckets(body_bkt, alloc);
  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:checkout",
                                    "xmlns:D", "DAV:",
                                    NULL);
  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:activity-set", NULL);
  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:href", NULL);

  svn_ra_serf__add_cdata_len_buckets(body_bkt, alloc,
                                     ctx->activity_url, ctx->activity_url_len);

  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:href");
  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:activity-set");
  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:checkout");

  return body_bkt;
}

static apr_status_t
handle_checkout(serf_request_t *request,
                serf_bucket_t *response,
                void *handler_baton,
                apr_pool_t *pool)
{
  checkout_context_t *ctx = handler_baton;
  apr_status_t status;

  status = svn_ra_serf__handle_status_only(request, response, &ctx->progress,
                                           pool);

  /* Get the resulting location. */
  if (ctx->progress.done && ctx->progress.status == 201)
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

/* Return the relative path from DIR's topmost parent to DIR, in
   Subversion's internal path style, allocated in POOL.  Use POOL for
   temporary work as well.  */
static const char *
relative_dir_path(dir_context_t *dir, apr_pool_t *pool)
{
  const char *rel_path = "";
  apr_array_header_t *components;
  dir_context_t *dir_ptr = dir;
  int i;

  components = apr_array_make(pool, 1, sizeof(const char *));

  for (dir_ptr = dir; dir_ptr; dir_ptr = dir_ptr->parent_dir)
    APR_ARRAY_PUSH(components, const char *) = dir_ptr->name;

  for (i = 0; i < components->nelts; i++)
    {
      rel_path = svn_path_join(rel_path,
                               APR_ARRAY_IDX(components, i, const char *),
                               pool);
    }

  return rel_path;
}


/* Return the relative path from FILE's topmost parent to FILE, in
   Subversion's internal path style, allocated in POOL.  Use POOL for
   temporary work as well.  */
static const char *
relative_file_path(file_context_t *f, apr_pool_t *pool)
{
  const char *dir_path = relative_dir_path(f->parent_dir, pool);
  return svn_path_join(dir_path, f->name, pool);
}


static svn_error_t *
checkout_dir(dir_context_t *dir)
{
  checkout_context_t *checkout_ctx;
  svn_ra_serf__handler_t *handler;
  svn_error_t *err;

  if (dir->checkout)
    {
      return SVN_NO_ERROR;
    }

  if (dir->parent_dir)
    {
      /* Is our parent a copy?  If so, we're already implicitly checked out. */
      if (apr_hash_get(dir->commit->copied_entries,
                       dir->parent_dir->name, APR_HASH_KEY_STRING))
        {
          /* Implicitly checkout this dir now. */
          dir->checkout = apr_pcalloc(dir->pool, sizeof(*dir->checkout));
          dir->checkout->pool = dir->pool;
          dir->checkout->activity_url = dir->commit->activity_url;
          dir->checkout->activity_url_len = dir->commit->activity_url_len;
          dir->checkout->resource_url =
            svn_path_url_add_component(dir->parent_dir->checkout->resource_url,
                                       svn_path_basename(dir->name, dir->pool),
                                       dir->pool);

          apr_hash_set(dir->commit->copied_entries,
                       apr_pstrdup(dir->commit->pool, dir->name),
                       APR_HASH_KEY_STRING, (void*)1);

          return SVN_NO_ERROR;
        }
    }

  /* Checkout our directory into the activity URL now. */
  handler = apr_pcalloc(dir->pool, sizeof(*handler));
  handler->session = dir->commit->session;
  handler->conn = dir->commit->conn;

  checkout_ctx = apr_pcalloc(dir->pool, sizeof(*checkout_ctx));
  checkout_ctx->pool = dir->pool;

  checkout_ctx->activity_url = dir->commit->activity_url;
  checkout_ctx->activity_url_len = dir->commit->activity_url_len;

  /* We could be called twice for the root: once to checkout the baseline;
   * once to checkout the directory itself if we need to do so.
   */
  if (!dir->parent_dir && !dir->commit->baseline)
    {
      checkout_ctx->checkout_url = dir->commit->baseline_url;
      dir->commit->baseline = checkout_ctx;
    }
  else
    {
      checkout_ctx->checkout_url = dir->checked_in_url;
      dir->checkout = checkout_ctx;
    }

  handler->body_delegate = create_checkout_body;
  handler->body_delegate_baton = checkout_ctx;
  handler->body_type = "text/xml";

  handler->response_handler = handle_checkout;
  handler->response_baton = checkout_ctx;

  handler->method = "CHECKOUT";
  handler->path = checkout_ctx->checkout_url;

  svn_ra_serf__request_create(handler);

  err = svn_ra_serf__context_run_wait(&checkout_ctx->progress.done,
                                      dir->commit->session,
                                      dir->pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_FS_CONFLICT)
        SVN_ERR_W(err, apr_psprintf(dir->pool,
                  _("Directory '%s' is out of date; try updating"),
                  svn_path_local_style(relative_dir_path(dir, dir->pool),
                                       dir->pool)));
      return err;
    }

  if (checkout_ctx->progress.status != 201)
    {
      return return_response_err(handler, &checkout_ctx->progress);
    }

  return SVN_NO_ERROR;
}


/* Set *CHECKED_IN_URL to the appropriate DAV version url for
 * RELPATH (relative to the root of SESSION).
 *
 * Try to find this version url in three ways:
 * First, if SESSION->callbacks->get_wc_prop() is defined, try to read the
 * version url from the working copy properties.
 * Second, if the version url of the parent directory PARENT_VSN_URL is
 * defined, set *CHECKED_IN_URL to the concatenation of PARENT_VSN_URL with
 * RELPATH.
 * Else, fetch the version url for the root of SESSION using CONN and
 * BASE_REVISION, and set *CHECKED_IN_URL to the concatenation of that
 * with RELPATH.
 *
 * Allocate the result in POOL, and use POOL for temporary allocation.
 */
static svn_error_t *
get_version_url(const char **checked_in_url,
                svn_ra_serf__session_t *session,
                svn_ra_serf__connection_t *conn,
                const char *relpath,
                svn_revnum_t base_revision,
                const char *parent_vsn_url,
                apr_pool_t *pool)
{
  const char *root_checkout;

  if (session->wc_callbacks->get_wc_prop)
    {
      const svn_string_t *current_version;

      SVN_ERR(session->wc_callbacks->get_wc_prop(session->wc_callback_baton,
                                                 relpath,
                                                 SVN_RA_SERF__WC_CHECKED_IN_URL,
                                                 &current_version, pool));

      if (current_version)
        {
          *checked_in_url = current_version->data;
          return SVN_NO_ERROR;
        }
    }

  if (parent_vsn_url)
    {
      root_checkout = parent_vsn_url;
    }
  else
    {
      svn_ra_serf__propfind_context_t *propfind_ctx;
      apr_hash_t *props;

      props = apr_hash_make(pool);

      propfind_ctx = NULL;
      svn_ra_serf__deliver_props(&propfind_ctx, props, session,
                                 conn, session->repos_url.path,
                                 base_revision, "0",
                                 checked_in_props, FALSE, NULL, pool);

      SVN_ERR(svn_ra_serf__wait_for_props(propfind_ctx, session, pool));

      /* We wouldn't get here if the url wasn't found (404), so the checked-in
         property should have been set. */
      root_checkout =
          svn_ra_serf__get_ver_prop(props, session->repos_url.path,
                                    base_revision, "DAV:", "checked-in");

      if (!root_checkout)
        return svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                                 _("Path '%s' not present"),
                                 session->repos_url.path);
    }

  *checked_in_url = svn_path_url_add_component(root_checkout, relpath, pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
checkout_file(file_context_t *file)
{
  svn_ra_serf__handler_t *handler;
  svn_error_t *err;

  if (file->parent_dir)
    {
      dir_context_t *dir;

      dir = file->parent_dir;
      while (dir && ! apr_hash_get(file->commit->copied_entries,
                                   dir->name, APR_HASH_KEY_STRING))
        {
          dir = dir->parent_dir;
        }


      /* Is our parent a copy?  If so, we're already implicitly checked out. */
      if (dir)
        {
          const char *diff_path;

          /* Implicitly checkout this dir now. */
          file->checkout = apr_pcalloc(file->pool, sizeof(*file->checkout));
          file->checkout->pool = file->pool;

          file->checkout->activity_url = file->commit->activity_url;
          file->checkout->activity_url_len = file->commit->activity_url_len;
          diff_path = svn_path_is_child(dir->name, file->name, file->pool);
          file->checkout->resource_url =
            svn_path_url_add_component(dir->checkout->resource_url,
                                       diff_path,
                                       file->pool);
          return SVN_NO_ERROR;
        }
    }

  /* Checkout our file into the activity URL now. */
  handler = apr_pcalloc(file->pool, sizeof(*handler));
  handler->session = file->commit->session;
  handler->conn = file->commit->conn;

  file->checkout = apr_pcalloc(file->pool, sizeof(*file->checkout));
  file->checkout->pool = file->pool;

  file->checkout->activity_url = file->commit->activity_url;
  file->checkout->activity_url_len = file->commit->activity_url_len;

  SVN_ERR(get_version_url(&(file->checkout->checkout_url),
                          file->commit->session, file->commit->conn,
                          file->name, file->base_revision,
                          NULL, file->pool));

  handler->body_delegate = create_checkout_body;
  handler->body_delegate_baton = file->checkout;
  handler->body_type = "text/xml";

  handler->response_handler = handle_checkout;
  handler->response_baton = file->checkout;

  handler->method = "CHECKOUT";
  handler->path = file->checkout->checkout_url;

  svn_ra_serf__request_create(handler);

  /* There's no need to wait here as we only need this when we start the
   * PROPPATCH or PUT of the file.
   */
  err = svn_ra_serf__context_run_wait(&file->checkout->progress.done,
                                      file->commit->session,
                                      file->pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_FS_CONFLICT)
        SVN_ERR_W(err, apr_psprintf(file->pool,
                  _("File '%s' is out of date; try updating"),
                  svn_path_local_style(relative_file_path(file, file->pool),
                                       file->pool)));
      return err;
    }

  if (file->checkout->progress.status != 201)
    {
      return return_response_err(handler, &file->checkout->progress);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
proppatch_walker(void *baton,
                 const char *ns, apr_ssize_t ns_len,
                 const char *name, apr_ssize_t name_len,
                 const svn_string_t *val,
                 apr_pool_t *pool)
{
  serf_bucket_t *body_bkt = baton;
  serf_bucket_alloc_t *alloc;
  svn_boolean_t binary_prop;
  char *prop_name;

  if (svn_xml_is_xml_safe(val->data, val->len))
    {
      binary_prop = FALSE;
    }
  else
    {
      binary_prop = TRUE;
    }

  /* Use the namespace prefix instead of adding the xmlns attribute to support
     property names containing ':' */
  if (strcmp(ns, SVN_DAV_PROP_NS_SVN) == 0)
    prop_name = apr_pstrcat(pool, "S:", name, NULL);
  else if (strcmp(ns, SVN_DAV_PROP_NS_CUSTOM) == 0)
    prop_name = apr_pstrcat(pool, "C:", name, NULL);
  name_len = strlen(prop_name);

  alloc = body_bkt->allocator;

  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, prop_name,
                                    "V:encoding", binary_prop ? "base64" : NULL,
                                    NULL);

  if (binary_prop == TRUE)
    {
      serf_bucket_t *tmp_bkt;
      val = svn_base64_encode_string2(val, TRUE, pool);
      tmp_bkt = SERF_BUCKET_SIMPLE_STRING_LEN(val->data, val->len, alloc);
      serf_bucket_aggregate_append(body_bkt, tmp_bkt);
    }
  else
    {
      svn_ra_serf__add_cdata_len_buckets(body_bkt, alloc, val->data, val->len);
    }


  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, prop_name);
  return SVN_NO_ERROR;
}

static apr_status_t
setup_proppatch_headers(serf_bucket_t *headers,
                        void *baton,
                        apr_pool_t *pool)
{
  proppatch_context_t *proppatch = baton;

  if (proppatch->name && proppatch->commit->lock_tokens)
    {
      const char *token;

      token = apr_hash_get(proppatch->commit->lock_tokens, proppatch->name,
                           APR_HASH_KEY_STRING);

      if (token)
        {
          const char *token_header;

          token_header = apr_pstrcat(pool, "(<", token, ">)", NULL);

          serf_bucket_headers_set(headers, "If", token_header);
        }
    }

  return APR_SUCCESS;
}

static serf_bucket_t *
create_proppatch_body(void *baton,
                      serf_bucket_alloc_t *alloc,
                      apr_pool_t *pool)
{
  proppatch_context_t *ctx = baton;
  serf_bucket_t *body_bkt;

  body_bkt = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_xml_header_buckets(body_bkt, alloc);
  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:propertyupdate",
                                    "xmlns:D", "DAV:",
                                    "xmlns:V", SVN_DAV_PROP_NS_DAV,
                                    "xmlns:C", SVN_DAV_PROP_NS_CUSTOM,
                                    "xmlns:S", SVN_DAV_PROP_NS_SVN,
                                    NULL);

  if (apr_hash_count(ctx->changed_props) > 0)
    {
      svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:set", NULL);
      svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:prop", NULL);

      svn_ra_serf__walk_all_props(ctx->changed_props, ctx->path,
                                  SVN_INVALID_REVNUM,
                                  proppatch_walker, body_bkt, pool);

      svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:prop");
      svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:set");
    }

  if (apr_hash_count(ctx->removed_props) > 0)
    {
      svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:remove", NULL);
      svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:prop", NULL);

      svn_ra_serf__walk_all_props(ctx->removed_props, ctx->path,
                                  SVN_INVALID_REVNUM,
                                  proppatch_walker, body_bkt, pool);

      svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:prop");
      svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:remove");
    }

  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:propertyupdate");

  return body_bkt;
}

static svn_error_t*
proppatch_resource(proppatch_context_t *proppatch,
                   commit_context_t *commit,
                   apr_pool_t *pool)
{
  svn_ra_serf__handler_t *handler;

  handler = apr_pcalloc(pool, sizeof(*handler));
  handler->method = "PROPPATCH";
  handler->path = proppatch->path;
  handler->conn = commit->conn;
  handler->session = commit->session;

  handler->header_delegate = setup_proppatch_headers;
  handler->header_delegate_baton = proppatch;

  handler->body_delegate = create_proppatch_body;
  handler->body_delegate_baton = proppatch;

  handler->response_handler = svn_ra_serf__handle_multistatus_only;
  handler->response_baton = &proppatch->progress;

  svn_ra_serf__request_create(handler);

  /* If we don't wait for the response, our pool will be gone! */
  SVN_ERR(svn_ra_serf__context_run_wait(&proppatch->progress.done,
                                        commit->session, pool));

  if (proppatch->progress.status != 207 ||
      proppatch->progress.server_error.error)
    {
      return svn_error_create(SVN_ERR_RA_DAV_PROPPATCH_FAILED,
        return_response_err(handler, &proppatch->progress),
        _("At least one property change failed; repository is unchanged"));
    }

  return SVN_NO_ERROR;
}

static serf_bucket_t *
create_put_body(void *baton,
                serf_bucket_alloc_t *alloc,
                apr_pool_t *pool)
{
  file_context_t *ctx = baton;
  apr_off_t offset;

  /* We need to flush the file, make it unbuffered (so that it can be
   * zero-copied via mmap), and reset the position before attempting to
   * deliver the file.
   *
   * N.B. If we have APR 1.3+, we can unbuffer the file to let us use mmap
   * and zero-copy the PUT body.  However, on older APR versions, we can't
   * check the buffer status; but serf will fall through and create a file
   * bucket for us on the buffered svndiff handle.
   */
  apr_file_flush(ctx->svndiff);
#if APR_VERSION_AT_LEAST(1, 3, 0)
  apr_file_buffer_set(ctx->svndiff, NULL, 0);
#endif
  offset = 0;
  apr_file_seek(ctx->svndiff, APR_SET, &offset);

  return serf_bucket_file_create(ctx->svndiff, alloc);
}

static serf_bucket_t *
create_empty_put_body(void *baton,
                      serf_bucket_alloc_t *alloc,
                      apr_pool_t *pool)
{
  return SERF_BUCKET_SIMPLE_STRING("", alloc);
}

static apr_status_t
setup_put_headers(serf_bucket_t *headers,
                  void *baton,
                  apr_pool_t *pool)
{
  file_context_t *ctx = baton;

  if (ctx->base_checksum)
    {
      serf_bucket_headers_set(headers, SVN_DAV_BASE_FULLTEXT_MD5_HEADER,
                              ctx->base_checksum);
    }

  if (ctx->result_checksum)
    {
      serf_bucket_headers_set(headers, SVN_DAV_RESULT_FULLTEXT_MD5_HEADER,
                              ctx->result_checksum);
    }

  if (ctx->commit->lock_tokens)
    {
      const char *token;

      token = apr_hash_get(ctx->commit->lock_tokens, ctx->name,
                           APR_HASH_KEY_STRING);

      if (token)
        {
          const char *token_header;

          token_header = apr_pstrcat(pool, "(<", token, ">)", NULL);

          serf_bucket_headers_set(headers, "If", token_header);
        }
    }

  return APR_SUCCESS;
}

static apr_status_t
setup_copy_file_headers(serf_bucket_t *headers,
                        void *baton,
                        apr_pool_t *pool)
{
  file_context_t *file = baton;
  apr_uri_t uri;
  const char *absolute_uri;

  /* The Dest URI must be absolute.  Bummer. */
  uri = file->commit->session->repos_url;
  uri.path = (char*)file->put_url;
  absolute_uri = apr_uri_unparse(pool, &uri, 0);

  serf_bucket_headers_set(headers, "Destination", absolute_uri);

  serf_bucket_headers_set(headers, "Depth", "0");
  serf_bucket_headers_set(headers, "Overwrite", "T");

  return APR_SUCCESS;
}

static apr_status_t
setup_copy_dir_headers(serf_bucket_t *headers,
                       void *baton,
                       apr_pool_t *pool)
{
  dir_context_t *dir = baton;
  apr_uri_t uri;
  const char *absolute_uri;

  /* The Dest URI must be absolute.  Bummer. */
  uri = dir->commit->session->repos_url;
  uri.path =
      (char*)svn_path_url_add_component(dir->parent_dir->checkout->resource_url,
                                        svn_path_basename(dir->name, pool),
                                        pool);

  absolute_uri = apr_uri_unparse(pool, &uri, 0);

  serf_bucket_headers_set(headers, "Destination", absolute_uri);

  serf_bucket_headers_set(headers, "Depth", "infinity");
  serf_bucket_headers_set(headers, "Overwrite", "T");

  /* Implicitly checkout this dir now. */
  dir->checkout = apr_pcalloc(dir->pool, sizeof(*dir->checkout));
  dir->checkout->pool = dir->pool;
  dir->checkout->activity_url = dir->commit->activity_url;
  dir->checkout->activity_url_len = dir->commit->activity_url_len;
  dir->checkout->resource_url = apr_pstrdup(dir->checkout->pool, uri.path);

  apr_hash_set(dir->commit->copied_entries,
               apr_pstrdup(dir->commit->pool, dir->name), APR_HASH_KEY_STRING,
               (void*)1);

  return APR_SUCCESS;
}

static apr_status_t
setup_delete_headers(serf_bucket_t *headers,
                     void *baton,
                     apr_pool_t *pool)
{
  delete_context_t *ctx = baton;

  serf_bucket_headers_set(headers, SVN_DAV_VERSION_NAME_HEADER,
                          apr_ltoa(pool, ctx->revision));

  if (ctx->lock_token_hash)
    {
      ctx->lock_token = apr_hash_get(ctx->lock_token_hash, ctx->path,
                                     APR_HASH_KEY_STRING);

      if (ctx->lock_token)
        {
          const char *token_header;

          token_header = apr_pstrcat(pool, "<", ctx->path, "> (<",
                                     ctx->lock_token, ">)", NULL);

          serf_bucket_headers_set(headers, "If", token_header);

          if (ctx->keep_locks)
            serf_bucket_headers_set(headers, SVN_DAV_OPTIONS_HEADER,
                                    SVN_DAV_OPTION_KEEP_LOCKS);
        }
    }

  return APR_SUCCESS;
}

static serf_bucket_t *
create_delete_body(void *baton,
                   serf_bucket_alloc_t *alloc,
                   apr_pool_t *pool)
{
  delete_context_t *ctx = baton;
  serf_bucket_t *body;

  body = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_xml_header_buckets(body, alloc);

  svn_ra_serf__merge_lock_token_list(ctx->lock_token_hash, ctx->path,
                                     body, alloc, pool);

  return body;
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
  svn_ra_serf__options_context_t *opt_ctx;
  svn_ra_serf__propfind_context_t *propfind_ctx;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__simple_request_context_t *mkact_ctx;
  proppatch_context_t *proppatch_ctx;
  dir_context_t *dir;
  const char *activity_str;
  const char *vcc_url;
  apr_hash_t *props;
  apr_hash_index_t *hi;
  svn_error_t *err;

  /* Create a UUID for this commit. */
  ctx->uuid = svn_uuid_generate(ctx->pool);

  svn_ra_serf__create_options_req(&opt_ctx, ctx->session,
                                  ctx->session->conns[0],
                                  ctx->session->repos_url.path, ctx->pool);

  err = svn_ra_serf__context_run_wait(
                                svn_ra_serf__get_options_done_ptr(opt_ctx),
                                ctx->session, ctx->pool);
  if (svn_ra_serf__get_options_error(opt_ctx) ||
      svn_ra_serf__get_options_parser_error(opt_ctx))
    {
      svn_error_clear(err);
      SVN_ERR(svn_ra_serf__get_options_error(opt_ctx));
      SVN_ERR(svn_ra_serf__get_options_parser_error(opt_ctx));
    }
  SVN_ERR(err);

  activity_str = svn_ra_serf__options_get_activity_collection(opt_ctx);

  if (!activity_str)
    {
      return svn_error_create(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
                              _("The OPTIONS response did not include the "
                                "requested activity-collection-set value"));
    }

  ctx->activity_url = svn_path_url_add_component(activity_str,
                                                 ctx->uuid, ctx->pool);
  ctx->activity_url_len = strlen(ctx->activity_url);

  /* Create our activity URL now on the server. */
  handler = apr_pcalloc(ctx->pool, sizeof(*handler));
  handler->method = "MKACTIVITY";
  handler->path = ctx->activity_url;
  handler->conn = ctx->session->conns[0];
  handler->session = ctx->session;

  mkact_ctx = apr_pcalloc(ctx->pool, sizeof(*mkact_ctx));

  handler->response_handler = svn_ra_serf__handle_status_only;
  handler->response_baton = mkact_ctx;

  svn_ra_serf__request_create(handler);

  SVN_ERR(svn_ra_serf__context_run_wait(&mkact_ctx->done, ctx->session,
                                        ctx->pool));

  if (mkact_ctx->status != 201)
    {
      return svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                               _("%s of '%s': %d %s (%s://%s)"),
                               handler->method, handler->path,
                               mkact_ctx->status, mkact_ctx->reason,
                               ctx->session->repos_url.scheme,
                               ctx->session->repos_url.hostinfo);
    }

  SVN_ERR(svn_ra_serf__discover_root(&vcc_url, NULL,
                                     ctx->session, ctx->conn,
                                     ctx->session->repos_url.path,
                                     ctx->pool));

  /* Now go fetch our VCC and baseline so we can do a CHECKOUT. */
  props = apr_hash_make(ctx->pool);
  propfind_ctx = NULL;
  svn_ra_serf__deliver_props(&propfind_ctx, props, ctx->session,
                             ctx->conn, vcc_url, SVN_INVALID_REVNUM, "0",
                             checked_in_props, FALSE, NULL, ctx->pool);

  SVN_ERR(svn_ra_serf__wait_for_props(propfind_ctx, ctx->session, ctx->pool));

  ctx->baseline_url = svn_ra_serf__get_ver_prop(props, vcc_url,
                                                SVN_INVALID_REVNUM,
                                                "DAV:", "checked-in");

  if (!ctx->baseline_url)
    {
      return svn_error_create(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
                              _("The OPTIONS response did not include the "
                                "requested checked-in value"));
    }

  dir = apr_pcalloc(dir_pool, sizeof(*dir));

  dir->pool = dir_pool;
  dir->commit = ctx;
  dir->base_revision = base_revision;
  dir->name = "";
  dir->changed_props = apr_hash_make(dir->pool);
  dir->removed_props = apr_hash_make(dir->pool);

  SVN_ERR(get_version_url(&dir->checked_in_url,
                          dir->commit->session, dir->commit->conn,
                          dir->name, dir->base_revision,
                          dir->commit->checked_in_url, dir->pool));
  ctx->checked_in_url = dir->checked_in_url;

  /* Checkout our root dir */
  SVN_ERR(checkout_dir(dir));

  /* PROPPATCH our revprops and pass them along.  */
  proppatch_ctx = apr_pcalloc(ctx->pool, sizeof(*proppatch_ctx));
  proppatch_ctx->pool = dir_pool;
  proppatch_ctx->commit = ctx;
  proppatch_ctx->path = ctx->baseline->resource_url;
  proppatch_ctx->changed_props = apr_hash_make(proppatch_ctx->pool);
  proppatch_ctx->removed_props = apr_hash_make(proppatch_ctx->pool);

  for (hi = apr_hash_first(ctx->pool, ctx->revprop_table); hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *name;
      svn_string_t *value;
      const char *ns;

      apr_hash_this(hi, &key, NULL, &val);
      name = key;
      value = val;

      if (strncmp(name, SVN_PROP_PREFIX, sizeof(SVN_PROP_PREFIX) - 1) == 0)
        {
          ns = SVN_DAV_PROP_NS_SVN;
          name += sizeof(SVN_PROP_PREFIX) - 1;
        }
      else
        {
          ns = SVN_DAV_PROP_NS_CUSTOM;
        }

      svn_ra_serf__set_prop(proppatch_ctx->changed_props, proppatch_ctx->path,
                            ns, name, value, proppatch_ctx->pool);
    }

  SVN_ERR(proppatch_resource(proppatch_ctx, dir->commit, ctx->pool));

  *root_baton = dir;

  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  dir_context_t *dir = parent_baton;
  delete_context_t *delete_ctx;
  svn_ra_serf__handler_t *handler;
  svn_error_t *err;

  /* Ensure our directory has been checked out */
  SVN_ERR(checkout_dir(dir));

  /* DELETE our entry */
  delete_ctx = apr_pcalloc(pool, sizeof(*delete_ctx));
  delete_ctx->path = apr_pstrdup(pool, path);
  delete_ctx->revision = revision;
  delete_ctx->lock_token_hash = dir->commit->lock_tokens;
  delete_ctx->keep_locks = dir->commit->keep_locks;

  handler = apr_pcalloc(pool, sizeof(*handler));
  handler->session = dir->commit->session;
  handler->conn = dir->commit->conn;

  handler->response_handler = svn_ra_serf__handle_status_only;
  handler->response_baton = &delete_ctx->progress;

  handler->header_delegate = setup_delete_headers;
  handler->header_delegate_baton = delete_ctx;

  handler->method = "DELETE";
  handler->path =
      svn_path_url_add_component(dir->checkout->resource_url,
                                 svn_path_basename(path, pool),
                                 pool);

  svn_ra_serf__request_create(handler);

  err = svn_ra_serf__context_run_wait(&delete_ctx->progress.done,
                                      dir->commit->session, pool);

  if (err &&
      (err->apr_err == SVN_ERR_FS_BAD_LOCK_TOKEN ||
       err->apr_err == SVN_ERR_FS_NO_LOCK_TOKEN ||
       err->apr_err == SVN_ERR_FS_LOCK_OWNER_MISMATCH ||
       err->apr_err == SVN_ERR_FS_PATH_ALREADY_LOCKED))
    {
      svn_error_clear(err);

      handler->body_delegate = create_delete_body;
      handler->body_delegate_baton = delete_ctx;
      handler->body_type = "text/xml";

      svn_ra_serf__request_create(handler);

      delete_ctx->progress.done = 0;

      SVN_ERR(svn_ra_serf__context_run_wait(&delete_ctx->progress.done,
                                            dir->commit->session, pool));
    }
  else if (err)
    {
      return err;
    }

  /* 204 No Content: item successfully deleted */
  if (delete_ctx->progress.status != 204)
    {
      return return_response_err(handler, &delete_ctx->progress);
    }

  apr_hash_set(dir->commit->deleted_entries,
               apr_pstrdup(dir->commit->pool, path), APR_HASH_KEY_STRING,
               (void*)1);

  return SVN_NO_ERROR;
}

static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *dir_pool,
              void **child_baton)
{
  dir_context_t *parent = parent_baton;
  dir_context_t *dir;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__simple_request_context_t *add_dir_ctx;
  apr_status_t status;

  /* Ensure our parent is checked out. */
  SVN_ERR(checkout_dir(parent));

  dir = apr_pcalloc(dir_pool, sizeof(*dir));

  dir->pool = dir_pool;

  dir->parent_dir = parent;
  dir->commit = parent->commit;

  dir->added = TRUE;
  dir->base_revision = SVN_INVALID_REVNUM;
  dir->copy_revision = copyfrom_revision;
  dir->copy_path = copyfrom_path;
  dir->name = apr_pstrdup(dir->pool, path);
  dir->checked_in_url =
      svn_path_url_add_component(parent->commit->checked_in_url,
                                 path, dir->pool);
  dir->changed_props = apr_hash_make(dir->pool);
  dir->removed_props = apr_hash_make(dir->pool);

  handler = apr_pcalloc(dir->pool, sizeof(*handler));
  handler->conn = dir->commit->conn;
  handler->session = dir->commit->session;

  add_dir_ctx = apr_pcalloc(dir->pool, sizeof(*add_dir_ctx));

  handler->response_handler = svn_ra_serf__handle_status_only;
  handler->response_baton = add_dir_ctx;
  if (!dir->copy_path)
    {
      handler->method = "MKCOL";
      handler->path = svn_path_url_add_component(parent->checkout->resource_url,
                                                 svn_path_basename(path,
                                                                   dir->pool),
                                                 dir->pool);
    }
  else
    {
      apr_uri_t uri;
      apr_hash_t *props;
      const char *vcc_url, *rel_copy_path, *basecoll_url, *req_url;

      props = apr_hash_make(dir->pool);

      status = apr_uri_parse(dir->pool, dir->copy_path, &uri);
      if (status)
        {
          return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                   _("Unable to parse URL '%s'"),
                                   dir->copy_path);
        }

      SVN_ERR(svn_ra_serf__discover_root(&vcc_url, &rel_copy_path,
                                         dir->commit->session,
                                         dir->commit->conn,
                                         uri.path, dir->pool));
      SVN_ERR(svn_ra_serf__retrieve_props(props,
                                          dir->commit->session,
                                          dir->commit->conn,
                                          vcc_url, dir->copy_revision, "0",
                                          baseline_props, dir->pool));
      basecoll_url = svn_ra_serf__get_ver_prop(props,
                                               vcc_url, dir->copy_revision,
                                               "DAV:", "baseline-collection");

      if (!basecoll_url)
        {
          return svn_error_create(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
                                  _("The OPTIONS response did not include the "
                                    "requested baseline-collection value"));
        }

      req_url = svn_path_url_add_component(basecoll_url, rel_copy_path,
                                           dir->pool);

      handler->method = "COPY";
      handler->path = req_url;

      handler->header_delegate = setup_copy_dir_headers;
      handler->header_delegate_baton = dir;
    }

  svn_ra_serf__request_create(handler);

  SVN_ERR(svn_ra_serf__context_run_wait(&add_dir_ctx->done,
                                        dir->commit->session, dir->pool));

  /* 201 Created:    item was successfully copied
     204 No Content: item successfully replaced an existing target */
  if (add_dir_ctx->status != 201 &&
      add_dir_ctx->status != 204)
    {
      SVN_ERR(add_dir_ctx->server_error.error);
      return svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                               _("Adding a directory failed: %s on %s (%d)"),
                               handler->method, handler->path,
                               add_dir_ctx->status);
    }

  *child_baton = dir;

  return SVN_NO_ERROR;
}

static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *dir_pool,
               void **child_baton)
{
  dir_context_t *parent = parent_baton;
  dir_context_t *dir;

  dir = apr_pcalloc(dir_pool, sizeof(*dir));

  dir->pool = dir_pool;

  dir->parent_dir = parent;
  dir->commit = parent->commit;

  dir->added = FALSE;
  dir->base_revision = base_revision;
  dir->name = apr_pstrdup(dir->pool, path);
  dir->changed_props = apr_hash_make(dir->pool);
  dir->removed_props = apr_hash_make(dir->pool);

  SVN_ERR(get_version_url(&dir->checked_in_url,
                          dir->commit->session, dir->commit->conn,
                          dir->name, dir->base_revision,
                          dir->commit->checked_in_url, dir->pool));
  *child_baton = dir;

  return SVN_NO_ERROR;
}

static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  dir_context_t *dir = dir_baton;
  const char *ns;

  /* Ensure we have a checked out dir. */
  SVN_ERR(checkout_dir(dir));

  name = apr_pstrdup(dir->pool, name);

  if (strncmp(name, SVN_PROP_PREFIX, sizeof(SVN_PROP_PREFIX) - 1) == 0)
    {
      ns = SVN_DAV_PROP_NS_SVN;
      name += sizeof(SVN_PROP_PREFIX) - 1;
    }
  else
    {
      ns = SVN_DAV_PROP_NS_CUSTOM;
    }

  if (value)
    {
      value = svn_string_dup(value, dir->pool);
      svn_ra_serf__set_prop(dir->changed_props, dir->checkout->resource_url,
                            ns, name, value, dir->pool);
    }
  else
    {
      value = svn_string_create("", dir->pool);

      svn_ra_serf__set_prop(dir->removed_props, dir->checkout->resource_url,
                            ns, name, value, dir->pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  dir_context_t *dir = dir_baton;

  /* Huh?  We're going to be called before the texts are sent.  Ugh.
   * Therefore, just wave politely at our caller.
   */

  /* PROPPATCH our prop change and pass it along.  */
  if (apr_hash_count(dir->changed_props) ||
      apr_hash_count(dir->removed_props))
    {
      proppatch_context_t *proppatch_ctx;

      proppatch_ctx = apr_pcalloc(pool, sizeof(*proppatch_ctx));
      proppatch_ctx->pool = pool;
      proppatch_ctx->commit = dir->commit;
      proppatch_ctx->name = dir->name;
      proppatch_ctx->path = dir->checkout->resource_url;
      proppatch_ctx->changed_props = dir->changed_props;
      proppatch_ctx->removed_props = dir->removed_props;

      SVN_ERR(proppatch_resource(proppatch_ctx, dir->commit, dir->pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
absent_directory(const char *path,
                 void *parent_baton,
                 apr_pool_t *pool)
{
#if 0
  dir_context_t *ctx = parent_baton;
#endif

  SVN_ERR_MALFUNCTION();
}

static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copy_path,
         svn_revnum_t copy_revision,
         apr_pool_t *file_pool,
         void **file_baton)
{
  dir_context_t *dir = parent_baton;
  file_context_t *new_file;
  const char *deleted_parent = path;

  /* Ensure our directory has been checked out */
  SVN_ERR(checkout_dir(dir));

  new_file = apr_pcalloc(file_pool, sizeof(*new_file));

  new_file->pool = file_pool;

  dir->ref_count++;
  new_file->parent_dir = dir;

  new_file->commit = dir->commit;

  new_file->name = apr_pstrdup(new_file->pool, path);

  new_file->added = TRUE;
  new_file->base_revision = SVN_INVALID_REVNUM;
  new_file->copy_path = copy_path;
  new_file->copy_revision = copy_revision;

  new_file->changed_props = apr_hash_make(new_file->pool);
  new_file->removed_props = apr_hash_make(new_file->pool);

  /* Ensure that the file doesn't exist by doing a HEAD on the
   * resource, but only if we haven't deleted it in this commit
   * already - directly, or indirectly through its parent directories -
   * or if the parent directory was also added (without history)
   * in this commit.
   */
  while (deleted_parent && deleted_parent[0] != '\0')
    {
      if (apr_hash_get(dir->commit->deleted_entries,
                       deleted_parent, APR_HASH_KEY_STRING))
        {
          break;
        }
      deleted_parent = svn_path_dirname(deleted_parent, file_pool);
    };

  if (! ((dir->added && !dir->copy_path) ||
         (deleted_parent && deleted_parent[0] != '\0')))
    {
      svn_ra_serf__simple_request_context_t *head_ctx;
      svn_ra_serf__handler_t *handler;

      handler = apr_pcalloc(new_file->pool, sizeof(*handler));

      handler->session = new_file->commit->session;
      handler->conn = new_file->commit->conn;

      handler->method = "HEAD";
      handler->path =
          svn_path_url_add_component(new_file->commit->session->repos_url.path,
                                     path, new_file->pool);

      head_ctx = apr_pcalloc(new_file->pool, sizeof(*head_ctx));

      handler->response_handler = svn_ra_serf__handle_status_only;
      handler->response_baton = head_ctx;

      svn_ra_serf__request_create(handler);

      SVN_ERR(svn_ra_serf__context_run_wait(&head_ctx->done,
                                            new_file->commit->session,
                                            new_file->pool));

      if (head_ctx->status != 404)
        {
          return svn_error_createf(SVN_ERR_RA_DAV_ALREADY_EXISTS, NULL,
                                   _("File '%s' already exists"), path);
        }
    }

  new_file->put_url =
      svn_path_url_add_component(dir->checkout->resource_url,
                                 svn_path_basename(path, new_file->pool),
                                 new_file->pool);

  *file_baton = new_file;

  return SVN_NO_ERROR;
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
  new_file->name = apr_pstrdup(new_file->pool, path);

  new_file->added = FALSE;
  new_file->base_revision = base_revision;

  new_file->changed_props = apr_hash_make(new_file->pool);
  new_file->removed_props = apr_hash_make(new_file->pool);

  /* CHECKOUT the file into our activity. */
  SVN_ERR(checkout_file(new_file));

  new_file->put_url = new_file->checkout->resource_url;

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

  SVN_ERR(svn_io_open_unique_file3(&ctx->svndiff, NULL, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   ctx->pool, ctx->pool));

  ctx->stream = svn_stream_create(ctx, pool);
  svn_stream_set_write(ctx->stream, svndiff_stream_write);

  svn_txdelta_to_svndiff2(handler, handler_baton, ctx->stream, 0, pool);

  ctx->base_checksum = base_checksum;

  return SVN_NO_ERROR;
}

static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  file_context_t *file = file_baton;
  const char *ns;

  name = apr_pstrdup(file->pool, name);

  if (strncmp(name, SVN_PROP_PREFIX, sizeof(SVN_PROP_PREFIX) - 1) == 0)
    {
      ns = SVN_DAV_PROP_NS_SVN;
      name += sizeof(SVN_PROP_PREFIX) - 1;
    }
  else
    {
      ns = SVN_DAV_PROP_NS_CUSTOM;
    }

  if (value)
    {
      value = svn_string_dup(value, file->pool);
      svn_ra_serf__set_prop(file->changed_props, file->put_url,
                            ns, name, value, file->pool);
    }
  else
    {
      value = svn_string_create("", file->pool);

      svn_ra_serf__set_prop(file->removed_props, file->put_url,
                            ns, name, value, file->pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  file_context_t *ctx = file_baton;
  svn_boolean_t put_empty_file = FALSE;
  apr_status_t status;

  ctx->result_checksum = text_checksum;

  if (ctx->copy_path)
    {
      svn_ra_serf__handler_t *handler;
      svn_ra_serf__simple_request_context_t *copy_ctx;
      apr_uri_t uri;
      apr_hash_t *props;
      const char *vcc_url, *rel_copy_path, *basecoll_url, *req_url;

      props = apr_hash_make(pool);

      status = apr_uri_parse(pool, ctx->copy_path, &uri);
      if (status)
        {
          return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                   _("Unable to parse URL '%s'"),
                                   ctx->copy_path);
        }

      SVN_ERR(svn_ra_serf__discover_root(&vcc_url, &rel_copy_path,
                                         ctx->commit->session,
                                         ctx->commit->conn,
                                         uri.path, pool));
      SVN_ERR(svn_ra_serf__retrieve_props(props,
                                          ctx->commit->session,
                                          ctx->commit->conn,
                                          vcc_url, ctx->copy_revision, "0",
                                          baseline_props, pool));
      basecoll_url = svn_ra_serf__get_ver_prop(props,
                                               vcc_url, ctx->copy_revision,
                                               "DAV:", "baseline-collection");

      if (!basecoll_url)
        {
          return svn_error_create(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
                                  _("The OPTIONS response did not include the "
                                    "requested baseline-collection value"));
        }

      req_url = svn_path_url_add_component(basecoll_url, rel_copy_path, pool);

      handler = apr_pcalloc(pool, sizeof(*handler));
      handler->method = "COPY";
      handler->path = req_url;
      handler->conn = ctx->commit->conn;
      handler->session = ctx->commit->session;

      copy_ctx = apr_pcalloc(pool, sizeof(*copy_ctx));

      handler->response_handler = svn_ra_serf__handle_status_only;
      handler->response_baton = copy_ctx;

      handler->header_delegate = setup_copy_file_headers;
      handler->header_delegate_baton = ctx;

      svn_ra_serf__request_create(handler);

      SVN_ERR(svn_ra_serf__context_run_wait(&copy_ctx->done,
                                            ctx->commit->session, pool));

      if (copy_ctx->status != 201 && copy_ctx->status != 204)
        {
          return return_response_err(handler, copy_ctx);
        }
    }

  /* If we got no stream of changes, but this is an added-without-history
   * file, make a note that we'll be PUTting a zero-byte file to the server.
   */
  if ((!ctx->stream) && ctx->added && (!ctx->copy_path))
    put_empty_file = TRUE;

  /* If we had a stream of changes, push them to the server... */
  if (ctx->stream || put_empty_file)
    {
      svn_ra_serf__handler_t *handler;
      svn_ra_serf__simple_request_context_t *put_ctx;

      handler = apr_pcalloc(pool, sizeof(*handler));
      handler->method = "PUT";
      handler->path = ctx->put_url;
      handler->conn = ctx->commit->conn;
      handler->session = ctx->commit->session;

      put_ctx = apr_pcalloc(pool, sizeof(*put_ctx));

      handler->response_handler = svn_ra_serf__handle_status_only;
      handler->response_baton = put_ctx;

      if (put_empty_file)
        {
          handler->body_delegate = create_empty_put_body;
          handler->body_delegate_baton = ctx;
          handler->body_type = "text/plain";
        }
      else
        {
          handler->body_delegate = create_put_body;
          handler->body_delegate_baton = ctx;
          handler->body_type = "application/vnd.svn-svndiff";
        }

      handler->header_delegate = setup_put_headers;
      handler->header_delegate_baton = ctx;

      svn_ra_serf__request_create(handler);

      SVN_ERR(svn_ra_serf__context_run_wait(&put_ctx->done,
                                            ctx->commit->session, pool));

      if (put_ctx->status != 204 && put_ctx->status != 201)
        {
          return return_response_err(handler, put_ctx);
        }
    }

  /* If we had any prop changes, push them via PROPPATCH. */
  if (apr_hash_count(ctx->changed_props) ||
      apr_hash_count(ctx->removed_props))
    {
      proppatch_context_t *proppatch;

      proppatch = apr_pcalloc(ctx->pool, sizeof(*proppatch));
      proppatch->pool = ctx->pool;
      proppatch->name = ctx->name;
      proppatch->path = ctx->put_url;
      proppatch->commit = ctx->commit;
      proppatch->changed_props = ctx->changed_props;
      proppatch->removed_props = ctx->removed_props;

      SVN_ERR(proppatch_resource(proppatch, ctx->commit, ctx->pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
absent_file(const char *path,
            void *parent_baton,
            apr_pool_t *pool)
{
#if 0
  dir_context_t *ctx = parent_baton;
#endif

  SVN_ERR_MALFUNCTION();
}

static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  commit_context_t *ctx = edit_baton;
  svn_ra_serf__merge_context_t *merge_ctx;
  svn_ra_serf__simple_request_context_t *delete_ctx;
  svn_ra_serf__handler_t *handler;
  svn_boolean_t *merge_done;

  /* MERGE our activity */
  SVN_ERR(svn_ra_serf__merge_create_req(&merge_ctx, ctx->session,
                                        ctx->session->conns[0],
                                        ctx->session->repos_url.path,
                                        ctx->activity_url,
                                        ctx->activity_url_len,
                                        ctx->lock_tokens,
                                        ctx->keep_locks,
                                        pool));

  merge_done = svn_ra_serf__merge_get_done_ptr(merge_ctx);

  SVN_ERR(svn_ra_serf__context_run_wait(merge_done, ctx->session, pool));

  if (svn_ra_serf__merge_get_status(merge_ctx) != 200)
    {
      SVN_ERR_MALFUNCTION();
    }

  /* Inform the WC that we did a commit.  */
  SVN_ERR(ctx->callback(svn_ra_serf__merge_get_commit_info(merge_ctx),
                        ctx->callback_baton, pool));

  /* DELETE our completed activity */
  handler = apr_pcalloc(pool, sizeof(*handler));
  handler->method = "DELETE";
  handler->path = ctx->activity_url;
  handler->conn = ctx->conn;
  handler->session = ctx->session;

  delete_ctx = apr_pcalloc(pool, sizeof(*delete_ctx));

  handler->response_handler = svn_ra_serf__handle_status_only;
  handler->response_baton = delete_ctx;

  svn_ra_serf__request_create(handler);

  SVN_ERR(svn_ra_serf__context_run_wait(&delete_ctx->done, ctx->session,
                                        pool));

  SVN_ERR_ASSERT(delete_ctx->status == 204);

  return SVN_NO_ERROR;
}

static svn_error_t *
abort_edit(void *edit_baton,
           apr_pool_t *pool)
{
  commit_context_t *ctx = edit_baton;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__simple_request_context_t *delete_ctx;

  /* If an activity wasn't even created, don't bother trying to delete it. */
  if (! ctx->activity_url)
    return SVN_NO_ERROR;

  /* DELETE our aborted activity */
  handler = apr_pcalloc(pool, sizeof(*handler));
  handler->method = "DELETE";
  handler->path = ctx->activity_url;
  handler->conn = ctx->session->conns[0];
  handler->session = ctx->session;

  delete_ctx = apr_pcalloc(pool, sizeof(*delete_ctx));

  handler->response_handler = svn_ra_serf__handle_status_only;
  handler->response_baton = delete_ctx;

  svn_ra_serf__request_create(handler);

  SVN_ERR(svn_ra_serf__context_run_wait(&delete_ctx->done, ctx->session,
                                        pool));

  /* 204 if deleted,
     403 if DELETE was forbidden (indicates MKACTIVITY was forbidden too),
     404 if the activity wasn't found. */
  if (delete_ctx->status != 204 &&
      delete_ctx->status != 403 &&
      delete_ctx->status != 404
      )
    {
      SVN_ERR_MALFUNCTION();
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_commit_editor(svn_ra_session_t *ra_session,
                               const svn_delta_editor_t **ret_editor,
                               void **edit_baton,
                               apr_hash_t *revprop_table,
                               svn_commit_callback2_t callback,
                               void *callback_baton,
                               apr_hash_t *lock_tokens,
                               svn_boolean_t keep_locks,
                               apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_delta_editor_t *editor;
  commit_context_t *ctx;
  apr_hash_index_t *hi;

  ctx = apr_pcalloc(pool, sizeof(*ctx));

  ctx->pool = pool;

  ctx->session = session;
  ctx->conn = session->conns[0];

  ctx->revprop_table = apr_hash_make(pool);
  for (hi = apr_hash_first(pool, revprop_table); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;

      apr_hash_this(hi, &key, &klen, &val);
      apr_hash_set(ctx->revprop_table, apr_pstrdup(pool, key), klen,
                   svn_string_dup(val, pool));
    }

  ctx->callback = callback;
  ctx->callback_baton = callback_baton;

  ctx->lock_tokens = lock_tokens;
  ctx->keep_locks = keep_locks;

  ctx->deleted_entries = apr_hash_make(ctx->pool);
  ctx->copied_entries = apr_hash_make(ctx->pool);

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

svn_error_t *
svn_ra_serf__change_rev_prop(svn_ra_session_t *ra_session,
                             svn_revnum_t rev,
                             const char *name,
                             const svn_string_t *value,
                             apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__propfind_context_t *propfind_ctx;
  proppatch_context_t *proppatch_ctx;
  commit_context_t *commit;
  const char *vcc_url, *checked_in_href, *ns;
  apr_hash_t *props;
  svn_error_t *err;

  commit = apr_pcalloc(pool, sizeof(*commit));

  commit->pool = pool;

  commit->session = session;
  commit->conn = session->conns[0];

  SVN_ERR(svn_ra_serf__discover_root(&vcc_url, NULL,
                                     commit->session,
                                     commit->conn,
                                     commit->session->repos_url.path, pool));

  props = apr_hash_make(pool);

  propfind_ctx = NULL;
  svn_ra_serf__deliver_props(&propfind_ctx, props, commit->session,
                             commit->conn, vcc_url, rev, "0",
                             checked_in_props, FALSE, NULL, pool);

  SVN_ERR(svn_ra_serf__wait_for_props(propfind_ctx, commit->session, pool));

  checked_in_href = svn_ra_serf__get_ver_prop(props, vcc_url, rev,
                                              "DAV:", "href");

  if (strncmp(name, SVN_PROP_PREFIX, sizeof(SVN_PROP_PREFIX) - 1) == 0)
    {
      ns = SVN_DAV_PROP_NS_SVN;
      name += sizeof(SVN_PROP_PREFIX) - 1;
    }
  else
    {
      ns = SVN_DAV_PROP_NS_CUSTOM;
    }

  /* PROPPATCH our log message and pass it along.  */
  proppatch_ctx = apr_pcalloc(pool, sizeof(*proppatch_ctx));
  proppatch_ctx->pool = pool;
  proppatch_ctx->commit = commit;
  proppatch_ctx->path = checked_in_href;
  proppatch_ctx->changed_props = apr_hash_make(proppatch_ctx->pool);
  proppatch_ctx->removed_props = apr_hash_make(proppatch_ctx->pool);

  if (value)
    {
      svn_ra_serf__set_prop(proppatch_ctx->changed_props, proppatch_ctx->path,
                            ns, name, value, proppatch_ctx->pool);
    }
  else
    {
      value = svn_string_create("", proppatch_ctx->pool);

      svn_ra_serf__set_prop(proppatch_ctx->removed_props, proppatch_ctx->path,
                            ns, name, value, proppatch_ctx->pool);
    }

  err = proppatch_resource(proppatch_ctx, commit, proppatch_ctx->pool);
  if (err)
    return
      svn_error_create
      (SVN_ERR_RA_DAV_REQUEST_FAILED, err,
       _("DAV request failed; it's possible that the repository's "
         "pre-revprop-change hook either failed or is non-existent"));

  return SVN_NO_ERROR;
}
