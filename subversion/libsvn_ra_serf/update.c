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


/**
 * This enum represents the current state of our XML parsing for a REPORT.
 */
typedef enum {
    OPEN_DIR,
    ADD_FILE,
    ADD_DIR,
    PROP,
    IGNORE_PROP_NAME,
    NEED_PROP_NAME,
} report_state_e;

/**
 * This structure represents the information for a directory.
 */
typedef struct report_dir_t
{
  /* The enclosing parent.
   *
   * This value is NULL when we are the root.
   */
  struct report_dir_t *parent_dir;

  /* the containing directory name */
  const char *name;

  /* temporary path buffer for this directory. */
  svn_stringbuf_t *name_buf;

  /* the canonical url for this path. */
  const char *url;

  /* controlling dir baton */
  void *dir_baton;

  /* Our update editor and baton. */
  const svn_delta_editor_t *update_editor;
  void *update_baton;

  /* How many references to this directory do we still have open? */
  apr_size_t ref_count;

  /* hashtable that stores all of the properties (shared globally) */
  apr_hash_t *props;

  /* The propfind request for our current directory */
  propfind_context_t *propfind;

  /* The next directory we have open. */
  struct report_dir_t *next;
} report_dir_t;

/**
 * This structure represents the information for a file.
 *
 * This structure is created as we parse the REPORT response and
 * once the element is completed, we create a report_fetch_t structure
 * to give to serf to retrieve this file.
 */
typedef struct report_info_t
{
  /* The enclosing directory. */
  report_dir_t *dir;

  /* the name of the file. */
  const char *file_name;

  /* file name buffer */
  svn_stringbuf_t *file_name_buf;

  /* the canonical url for this path. */
  const char *file_url;

  /* pool passed to update->add_file, etc. */
  apr_pool_t *editor_pool;

  /* controlling file_baton and textdelta handler */
  void *file_baton;
  svn_txdelta_window_handler_t textdelta;
  void *textdelta_baton;

  /* the in-progress property being parsed */
  const char *prop_ns;
  const char *prop_name;
  const char *prop_val;
  apr_size_t prop_val_len;
} report_info_t;

/**
 * This file structure represents a single file to fetch with its
 * associated Serf session.
 */
typedef struct report_fetch_t {
  /* Our pool. */
  apr_pool_t *pool;

  /* The session we should use to fetch the file. */
  ra_serf_session_t *sess;

  /* Stores the information for the file we want to fetch. */
  report_info_t *info;

  /* Are we done fetching this file? */
  svn_boolean_t done;

  /* The next fetch we have open. */
  struct report_fetch_t *next;
} report_fetch_t;

typedef struct report_state_list_t {
   /* The current state that we are in now. */
  report_state_e state;

  /* Information */
  report_info_t *info;

  /* The previous state we were in. */
  struct report_state_list_t *prev;
} report_state_list_t;

typedef struct {
  ra_serf_session_t *sess;

  const char *target;
  svn_revnum_t target_rev;

  svn_boolean_t recurse;

  const svn_delta_editor_t *update_editor;
  void *update_baton;

  serf_bucket_t *buckets;

  XML_Parser xmlp;
  ns_t *ns_list;

  /* our base rev. */
  svn_revnum_t base_rev;

  /* could allocate this as an array rather than a linked list. */
  report_state_list_t *state;
  report_state_list_t *free_state;

  /* pending GET requests */
  report_fetch_t *active_fetches;

  /* pending PROPFIND requests */
  propfind_context_t *active_propfinds;

  /* potentially pending dir baton closes */
  report_dir_t *pending_dir_close;

  /* free list of info structures */
  report_info_t *free_info;

  svn_boolean_t done;

} report_context_t;


static void push_state(report_context_t *ctx, report_state_e state)
{
  report_state_list_t *new_state;

  if (!ctx->free_state)
    {
      new_state = apr_palloc(ctx->sess->pool, sizeof(*ctx->state));
    }
  else
    {
      new_state = ctx->free_state;
      ctx->free_state = ctx->free_state->prev;
    }
  new_state->state = state;

  if (state == OPEN_DIR)
    {
      new_state->info = apr_palloc(ctx->sess->pool, sizeof(*new_state->info));
      /* Create our root state now. */
      new_state->info->dir = apr_palloc(ctx->sess->pool,
                                        sizeof(*new_state->info->dir));
      /* Create the root property tree. */
      new_state->info->dir->props = apr_hash_make(ctx->sess->pool);
      new_state->info->dir->parent_dir = NULL;
      new_state->info->dir->ref_count = 0;
      new_state->info->dir->next = NULL;
      new_state->info->dir->propfind = NULL;
      /* Point the update_editor */
      new_state->info->dir->update_editor = ctx->update_editor;
      new_state->info->dir->update_baton = ctx->update_baton;
    }
  else if (state == ADD_DIR)
    {
      new_state->info = apr_palloc(ctx->sess->pool, sizeof(*new_state->info));
      new_state->info->dir =
          apr_palloc(ctx->sess->pool, sizeof(*new_state->info->dir));
      new_state->info->dir->parent_dir = ctx->state->info->dir;
      new_state->info->dir->parent_dir->ref_count++;
      new_state->info->dir->ref_count = 0;
      new_state->info->dir->next = NULL;
      new_state->info->dir->props = ctx->state->info->dir->props;
      /* Point the update_editor */
      new_state->info->dir->update_editor = ctx->update_editor;
      new_state->info->dir->update_baton = ctx->update_baton;
    }
  else if (state == ADD_FILE)
    {
      new_state->info = apr_palloc(ctx->sess->pool, sizeof(*new_state->info));
      /* Point at our parent's directory state. */
      new_state->info->dir = ctx->state->info->dir;
      new_state->info->dir->ref_count++;
    }
  /* if we have state info from our parent, reuse it. */
  else if (ctx->state && ctx->state->info)
    {
      new_state->info = ctx->state->info;
    }
  else
    {
      abort();
    }

  /* Add it to the state chain. */
  new_state->prev = ctx->state;
  ctx->state = new_state;
}

static void pop_state(report_context_t *ctx)
{
  report_state_list_t *free_state;
  free_state = ctx->state;
  /* advance the current state */
  ctx->state = ctx->state->prev;
  free_state->prev = ctx->free_state;
  ctx->free_state = free_state;
  ctx->free_state->info = NULL;
}

typedef svn_error_t * (*prop_set_t)(void *baton,
                                    const char *name,
                                    const svn_string_t *value,
                                    apr_pool_t *pool);

static void
set_baton_props(prop_set_t setprop, void *baton,
                const void *ns, apr_ssize_t ns_len,
                const void *name, apr_ssize_t name_len,
                void *val,
                apr_pool_t *pool)
{
  const char *prop_name;
  svn_string_t *prop_val;

  if (strcmp(ns, SVN_DAV_PROP_NS_CUSTOM) == 0)
    prop_name = name;
  else if (strcmp(ns, SVN_DAV_PROP_NS_SVN) == 0)
    prop_name = apr_pstrcat(pool, SVN_PROP_PREFIX, name, NULL);
  else if (strcmp(name, "version-name") == 0)
    prop_name = SVN_PROP_ENTRY_COMMITTED_REV;
  else if (strcmp(name, "creationdate") == 0)
    prop_name = SVN_PROP_ENTRY_COMMITTED_DATE;
  else if (strcmp(name, "creator-displayname") == 0)
    prop_name = SVN_PROP_ENTRY_LAST_AUTHOR;
  else if (strcmp(name, "repository-uuid") == 0)
    prop_name = SVN_PROP_ENTRY_UUID;
  else
    {
      /* do nothing for now? */
      return;
    }

  setprop(baton, prop_name, svn_string_create(val, pool), pool);
}

static void
set_file_props(void *baton,
               const void *ns, apr_ssize_t ns_len,
               const void *name, apr_ssize_t name_len,
               void *val,
               apr_pool_t *pool)
{
  report_info_t *info = baton;
  set_baton_props(info->dir->update_editor->change_file_prop,
                  info->file_baton,
                  ns, ns_len, name, name_len, val, pool);
}

static void
set_dir_props(void *baton,
              const void *ns, apr_ssize_t ns_len,
              const void *name, apr_ssize_t name_len,
              void *val,
              apr_pool_t *pool)
{
  report_dir_t *dir = baton;
  set_baton_props(dir->update_editor->change_dir_prop,
                  dir->dir_baton,
                  ns, ns_len, name, name_len, val, pool);
}

static apr_status_t
handle_fetch (serf_bucket_t *response,
              void *handler_baton,
              apr_pool_t *pool)
{
  const char *data;
  apr_size_t len;
  serf_status_line sl;
  apr_status_t status;
  report_fetch_t *fetch_ctx = handler_baton;

  status = serf_bucket_response_status(response, &sl);
  if (status)
    {
      if (APR_STATUS_IS_EAGAIN(status))
        {
          return APR_SUCCESS;
        }
      abort();
    }

  while (1)
    {
      svn_txdelta_window_t delta_window = { 0 };
      svn_txdelta_op_t delta_op;
      svn_string_t window_data;

      status = serf_bucket_read(response, 2048, &data, &len);
      if (SERF_BUCKET_READ_ERROR(status))
        {
          return status;
        }

      /* construct the text delta window. */
      if (len)
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
          fetch_ctx->info->textdelta(&delta_window,
                                     fetch_ctx->info->textdelta_baton);
        }

      if (APR_STATUS_IS_EOF(status))
        {
          report_info_t *info = fetch_ctx->info;

          info->textdelta(NULL, info->textdelta_baton);

          /* set all of the properties we received */
          walk_all_props(info->dir->props,
                         info->file_url,
                         set_file_props,
                         info, fetch_ctx->sess->pool);

          info->dir->update_editor->close_file(info->file_baton, NULL,
                                               fetch_ctx->sess->pool);

          fetch_ctx->done = TRUE;
          return APR_EOF;
        }
      if (APR_STATUS_IS_EAGAIN(status))
        {
          return APR_SUCCESS;
        }
    }
  /* not reached */

  abort();
}

static void fetch_file(report_context_t *ctx, report_info_t *info)
{
  const char *checked_in_url, *checksum;
  serf_request_t *request;
  serf_bucket_t *req_bkt, *hdrs_bkt;
  report_fetch_t *fetch_ctx;
  propfind_context_t *prop_ctx;
  apr_hash_t *props;

  /* go fetch info->file_name from DAV:checked-in */
  checked_in_url = fetch_prop(info->dir->props, info->file_name,
                         "DAV:", "checked-in");

  if (!checked_in_url)
    {
      abort();
    }

  info->file_url = checked_in_url;

  /* open the file and  */
  /* FIXME subpool */
  info->dir->update_editor->add_file(info->file_name, info->dir->dir_baton,
                                     NULL, SVN_INVALID_REVNUM, ctx->sess->pool,
                                     &info->file_baton);

  info->dir->update_editor->apply_textdelta(info->file_baton,
                                            NULL, ctx->sess->pool,
                                            &info->textdelta,
                                            &info->textdelta_baton);

  /* First, create the PROPFIND to retrieve the properties. */
  deliver_props(&prop_ctx, info->dir->props, ctx->sess,
                info->file_url, "0", all_props, ctx->sess->pool);

  if (!prop_ctx)
    {
      abort();
    }

  prop_ctx->next = ctx->active_propfinds;
  ctx->active_propfinds = prop_ctx;

  /* create and deliver GET request */
  request = serf_connection_request_create(ctx->sess->conn);

  req_bkt = serf_bucket_request_create("GET", checked_in_url, NULL,
                                       serf_request_get_alloc(request));

  hdrs_bkt = serf_bucket_request_get_headers(req_bkt);
  serf_bucket_headers_setn(hdrs_bkt, "Host", ctx->sess->repos_url.hostinfo);
  serf_bucket_headers_setn(hdrs_bkt, "User-Agent", "ra_serf");
  /* serf_bucket_headers_setn(hdrs_bkt, "Accept-Encoding", "gzip"); */

  /* Create the fetch context. */
  fetch_ctx = apr_pcalloc(ctx->sess->pool, sizeof(*fetch_ctx));
  fetch_ctx->pool = ctx->sess->pool;
  fetch_ctx->info = info;
  fetch_ctx->done = FALSE;
  fetch_ctx->sess = ctx->sess;

  serf_request_deliver(request, req_bkt,
                       accept_response, ctx->sess,
                       handle_fetch, fetch_ctx);

  /* add the GET to our active list. */
  fetch_ctx->next = ctx->active_fetches;
  ctx->active_fetches = fetch_ctx;
}

static void XMLCALL
start_report(void *userData, const char *name, const char **attrs)
{
  report_context_t *ctx = userData;
  dav_props_t prop_name;

  /* check for new namespaces */
  define_ns(&ctx->ns_list, attrs, ctx->sess->pool);

  /* look up name space if present */
  prop_name = expand_ns(ctx->ns_list, name, ctx->sess->pool);

  if (!ctx->state && strcmp(prop_name.name, "open-directory") == 0)
    {
      const char *rev = NULL;

      rev = find_attr(attrs, "rev");

      if (!rev)
        {
          abort();
        }

      push_state(ctx, OPEN_DIR);

      ctx->base_rev = apr_atoi64(rev);

      /* FIXME subpool */
      ctx->update_editor->open_root(ctx->update_baton, ctx->base_rev,
                                    ctx->sess->pool,
                                    &ctx->state->info->dir->dir_baton);
      ctx->state->info->dir->name_buf = svn_stringbuf_create("",
                                                             ctx->sess->pool);
      ctx->state->info->dir->name = "";
      ctx->state->info->file_name = "";
    }
  else if (ctx->state && 
           (ctx->state->state == OPEN_DIR || ctx->state->state == ADD_DIR) &&
           strcmp(prop_name.name, "add-directory") == 0)
    {
      const char *dir_name = NULL;
      report_dir_t *dir_info;

      dir_name = find_attr(attrs, "name");

      push_state(ctx, ADD_DIR);

      dir_info = ctx->state->info->dir;

      dir_info->name_buf = svn_stringbuf_dup(dir_info->parent_dir->name_buf,
                                             ctx->sess->pool);
      svn_path_add_component(dir_info->name_buf, dir_name);
      dir_info->name = dir_info->name_buf->data;
      ctx->state->info->file_name = dir_info->name_buf->data;

      ctx->update_editor->add_directory(dir_info->name_buf->data,
                                        dir_info->parent_dir->dir_baton,
                                        NULL, SVN_INVALID_REVNUM,
                                        ctx->sess->pool,
                                        &dir_info->dir_baton);
    }
  else if (ctx->state &&
           (ctx->state->state == OPEN_DIR || ctx->state->state == ADD_DIR) &&
           strcmp(prop_name.name, "add-file") == 0)
    {
      const char *file_name = NULL;
      report_info_t *info;

      file_name = find_attr(attrs, "name");

      if (!file_name)
        {
          abort();
        }

      push_state(ctx, ADD_FILE);

      info = ctx->state->info;

      info->file_name_buf = svn_stringbuf_dup(info->dir->name_buf,
                                              ctx->sess->pool);
      svn_path_add_component(info->file_name_buf, file_name);
      info->file_name = info->file_name_buf->data;
    }
  else if (ctx->state && ctx->state->state == ADD_FILE)
    {
      if (strcmp(prop_name.name, "checked-in") == 0)
        {
          ctx->state->info->prop_ns = prop_name.namespace;
          ctx->state->info->prop_name = prop_name.name;
          ctx->state->info->prop_val = NULL;
          push_state(ctx, IGNORE_PROP_NAME);
        }
      else if (strcmp(prop_name.name, "prop") == 0)
        {
          /* need to fetch it. */
          push_state(ctx, NEED_PROP_NAME);
        }
    }
  else if (ctx->state &&
           (ctx->state->state == OPEN_DIR || ctx->state->state == ADD_DIR))
    {
      if (strcmp(prop_name.name, "checked-in") == 0)
        {
          ctx->state->info->prop_ns = prop_name.namespace;
          ctx->state->info->prop_name = prop_name.name;
          ctx->state->info->prop_val = NULL;
          push_state(ctx, IGNORE_PROP_NAME);
        }
      else if (strcmp(prop_name.name, "set-prop") == 0)
        {
          const char *full_prop_name;
          dav_props_t new_prop_name;

          full_prop_name = find_attr(attrs, "name");
          new_prop_name = expand_ns(ctx->ns_list, full_prop_name,
                                    ctx->sess->pool);

          ctx->state->info->prop_ns = new_prop_name.namespace;
          ctx->state->info->prop_name = new_prop_name.name;
          ctx->state->info->prop_val = NULL;
          push_state(ctx, PROP);
        }
    }
  else if (ctx->state && ctx->state->state == IGNORE_PROP_NAME)
    {
      push_state(ctx, PROP);
    }
  else if (ctx->state && ctx->state->state == NEED_PROP_NAME)
    {
      ctx->state->info->prop_ns = prop_name.namespace;
      ctx->state->info->prop_name = prop_name.name;
      ctx->state->info->prop_val = NULL;
      push_state(ctx, PROP);
    }
}

static void XMLCALL
end_report(void *userData, const char *raw_name)
{
  report_context_t *ctx = userData;
  dav_props_t name;

  name = expand_ns(ctx->ns_list, raw_name, ctx->sess->pool);

  if (ctx->state &&
      ((ctx->state->state == OPEN_DIR &&
        (strcmp(name.name, "open-directory") == 0)) ||
       (ctx->state->state == ADD_DIR &&
        (strcmp(name.name, "add-directory") == 0))))
    {
      /* At this point, we should have the checked-in href.
       * We need to go do a PROPFIND to get the dir props.
       */
      propfind_context_t *prop_ctx;
      const char *checked_in_url;
      report_info_t *info = ctx->state->info;

      /* go fetch info->file_name from DAV:checked-in */
      checked_in_url = fetch_prop(info->dir->props, info->file_name,
                                  "DAV:", "checked-in");

      if (!checked_in_url)
        {
          abort();
        }

      info->dir->url = checked_in_url;

      /* First, create the PROPFIND to retrieve the properties. */
      deliver_props(&prop_ctx, info->dir->props, ctx->sess,
                    info->dir->url, "0", all_props, ctx->sess->pool);

      if (!prop_ctx)
        {
          abort();
        }

      prop_ctx->next = ctx->active_propfinds;
      ctx->active_propfinds = prop_ctx;

      info->dir->propfind = prop_ctx;

      pop_state(ctx);
    }
  else if (ctx->state && ctx->state->state == ADD_FILE)
    {
      /* We should have everything we need to fetch the file. */
      fetch_file(ctx, ctx->state->info);
      pop_state(ctx);
    }
  else if (ctx->state && ctx->state->state == PROP)
    {
      set_prop(ctx->state->info->dir->props,
               ctx->state->info->file_name,
               ctx->state->info->prop_ns, ctx->state->info->prop_name,
               ctx->state->info->prop_val,
               ctx->sess->pool);
      pop_state(ctx);
    }
  else if (ctx->state &&
           (ctx->state->state == IGNORE_PROP_NAME ||
            ctx->state->state == NEED_PROP_NAME))
    {
      pop_state(ctx);
    }
}

static void XMLCALL
cdata_report(void *userData, const char *data, int len)
{
  report_context_t *ctx = userData;
  if (ctx->state && ctx->state->state == PROP)
    {
      expand_string(&ctx->state->info->prop_val,
                    &ctx->state->info->prop_val_len,
                    data, len, ctx->sess->pool);

    }
}

static apr_status_t
handle_report (serf_bucket_t *response,
               void *handler_baton,
               apr_pool_t *pool)
{
  report_context_t *ctx = handler_baton;

  return handle_xml_parser(response,
                           ctx->xmlp, &ctx->done,
                           pool);
}

static svn_error_t *
close_dir(report_dir_t *dir,
          apr_pool_t *pool)
{
  walk_all_props(dir->props, dir->url, set_dir_props, dir, pool);
  SVN_ERR(dir->update_editor->close_directory(dir->dir_baton, pool));
}

static svn_error_t *
set_path(void *report_baton,
         const char *path,
         svn_revnum_t revision,
         svn_boolean_t start_empty,
         const char *lock_token,
         apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  serf_bucket_t *tmp;

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<S:entry rev=\"",
                                      sizeof("<S:entry rev=\"")-1,
                                      report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING(apr_ltoa(pool, revision),
                                  report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("\"", sizeof("\"")-1,
                                      report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  if (lock_token)
    {
      tmp = SERF_BUCKET_SIMPLE_STRING_LEN(" lock-token=\"",
                                          sizeof(" lock-token=\"")-1,
                                          report->sess->bkt_alloc);
      serf_bucket_aggregate_append(report->buckets, tmp);

      tmp = SERF_BUCKET_SIMPLE_STRING(lock_token,
                                      report->sess->bkt_alloc);
      serf_bucket_aggregate_append(report->buckets, tmp);

      tmp = SERF_BUCKET_SIMPLE_STRING_LEN("\"", sizeof("\"")-1,
                                          report->sess->bkt_alloc);
      serf_bucket_aggregate_append(report->buckets, tmp);
    }

  if (start_empty)
    {
      tmp = SERF_BUCKET_SIMPLE_STRING_LEN(" start-empty=\"true\"",
                                          sizeof(" start-empty=\"true\"")-1,
                                          report->sess->bkt_alloc);
      serf_bucket_aggregate_append(report->buckets, tmp);
    }

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(">", sizeof(">")-1,
                                      report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING(path, report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</S:entry>",
                                      sizeof("</S:entry>")-1,
                                      report->sess->bkt_alloc);

  serf_bucket_aggregate_append(report->buckets, tmp);
  return APR_SUCCESS;
}

static svn_error_t *
delete_path(void *report_baton,
            const char *path,
            apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  abort();
}

static svn_error_t *
link_path(void *report_baton,
          const char *path,
          const char *url,
          svn_revnum_t revision,
          svn_boolean_t start_empty,
          const char *lock_token,
          apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  abort();
}

static svn_error_t *
finish_report(void *report_baton,
              apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  ra_serf_session_t *sess = report->sess;
  serf_request_t *request;
  serf_bucket_t *req_bkt, *hdrs_bkt, *tmp;
  report_fetch_t *active_fetch, *prev_fetch;
  propfind_context_t *active_propfind, *prev_propfind;
  const char *vcc_url;
  apr_hash_t *props;
  apr_status_t status;

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</S:update-report>",
                                      sizeof("</S:update-report>")-1,
                                      report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  props = apr_hash_make(pool);

  SVN_ERR(retrieve_props(props, sess, sess->repos_url.path, "0",
                         vcc_props, pool));

  vcc_url = fetch_prop(props, sess->repos_url.path, "DAV:",
                       "version-controlled-configuration");

  if (!vcc_url)
    {
      abort();
    }

  /* create and deliver request */
  create_serf_req(&request, &req_bkt, NULL,
                  sess,
                  "REPORT", vcc_url,
                  report->buckets, "text/xml");

  serf_request_deliver(request, req_bkt,
                       accept_response, sess,
                       handle_report, report);

  while (!report->done || report->active_fetches || report->active_propfinds)
    {
      status = serf_context_run(sess->context, SERF_DURATION_FOREVER, pool);
      if (APR_STATUS_IS_TIMEUP(status))
        {
          continue;
        }
      if (status)
        {
          return svn_error_wrap_apr(status, _("Error retrieving REPORT"));
        }

      /* prune our propfind list if they are done. */
      active_propfind = report->active_propfinds;
      prev_propfind = NULL;
      while (active_propfind)
        {
          if (active_propfind->done == TRUE)
            {
              /* Remove us from the list. */
              if (prev_propfind)
                {
                  prev_propfind->next = active_propfind->next;
                }
              else
                {
                  report->active_propfinds = active_propfind->next;
                }
            }
          else
            {
              prev_propfind = active_propfind;
            }
          active_propfind = active_propfind->next;
        }

      /* prune our fetches list if they are done. */
      active_fetch = report->active_fetches;
      prev_fetch = NULL;
      while (active_fetch)
        {
          if (active_fetch->done == TRUE)
            {
              report_dir_t *parent_dir = active_fetch->info->dir;

              /* walk up and decrease our directory refcount. */
              do
                {
                  parent_dir->ref_count--;

                  if (parent_dir->ref_count)
                     break;

                  /* The easy path here is that we've finished the report;
                   * which means that there aren't any new files to add.
                   */
                  if (report->done == TRUE &&
                      parent_dir->propfind->done == TRUE)
                    {
                      SVN_ERR(close_dir(parent_dir, sess->pool));
                    }
                  else if (!parent_dir->next)
                    {
                      parent_dir->next = report->pending_dir_close;
                      report->pending_dir_close = parent_dir;
                    }

                  parent_dir = parent_dir->parent_dir;
                }
              while (parent_dir);

              /* Remove us from the list. */
              if (prev_fetch)
                {
                  prev_fetch->next = active_fetch->next;
                }
              else
                {
                  report->active_fetches = active_fetch->next;
                }
            }
          else
            {
              prev_fetch = active_fetch;
            }
          active_fetch = active_fetch->next;
        }

      if (report->done == TRUE)
        {
          report_dir_t *dir = report->pending_dir_close;

          while (dir)
            {
              if (!dir->ref_count && dir->propfind->done == TRUE)
                {
                  SVN_ERR(close_dir(dir, sess->pool));
                }

              dir = dir->next;
            }
        }
      /* Debugging purposes only! */
      serf_debug__closed_conn(sess->bkt_alloc);
    }

  /* FIXME subpool */
  SVN_ERR(report->update_editor->close_edit(report->update_baton, sess->pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
abort_report(void *report_baton,
             apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  abort();
}

static const svn_ra_reporter2_t ra_serf_reporter = {
  set_path,
  delete_path,
  link_path,
  finish_report,
  abort_report
};

svn_error_t *
svn_ra_serf__do_update (svn_ra_session_t *ra_session,
                        const svn_ra_reporter2_t **reporter,
                        void **report_baton,
                        svn_revnum_t revision_to_update_to,
                        const char *update_target,
                        svn_boolean_t recurse,
                        const svn_delta_editor_t *update_editor,
                        void *update_baton,
                        apr_pool_t *pool)
{
  report_context_t *report;
  ra_serf_session_t *session = ra_session->priv;
  serf_bucket_t *tmp;

  report = apr_palloc(pool, sizeof(*report));
  report->sess = ra_session->priv;
  report->target = update_target;
  report->target_rev = revision_to_update_to;
  report->recurse = recurse;
  report->update_editor = update_editor;
  report->update_baton = update_baton;
  report->active_fetches = NULL;
  report->active_propfinds = NULL;
  report->done = FALSE;
  report->free_info = 0;

  /* Create our XML parser */
  report->xmlp = XML_ParserCreate(NULL);
  XML_SetUserData(report->xmlp, report);
  XML_SetElementHandler(report->xmlp, start_report, end_report);
  XML_SetCharacterDataHandler(report->xmlp, cdata_report);

  *reporter = &ra_serf_reporter;
  *report_baton = report;

  report->buckets = serf_bucket_aggregate_create(report->sess->bkt_alloc);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<S:update-report xmlns:S=\"",
                                      sizeof("<S:update-report xmlns:S=\"")-1,
                                      report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(SVN_XML_NAMESPACE,
                                      sizeof(SVN_XML_NAMESPACE)-1,
                                      report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("\">",
                                      sizeof("\">")-1,
                                      report->sess->bkt_alloc);
  serf_bucket_aggregate_append(report->buckets, tmp);

  add_tag_buckets(report->buckets,
                  "S:src-path", report->sess->repos_url.path,
                  report->sess->bkt_alloc);

  if (SVN_IS_VALID_REVNUM(revision_to_update_to))
    {
      add_tag_buckets(report->buckets,
                      "S:target-revision",
                      apr_ltoa(pool, revision_to_update_to),
                      report->sess->bkt_alloc);
    }

  if (*update_target)
    {
      add_tag_buckets(report->buckets,
                      "S:update-target", update_target,
                      report->sess->bkt_alloc);
    }

  if (!recurse)
    {
      add_tag_buckets(report->buckets,
                      "S:recursive", "no",
                      report->sess->bkt_alloc);
    }

  return SVN_NO_ERROR;
}
