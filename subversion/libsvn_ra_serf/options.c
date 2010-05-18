/*
 * options.c :  entry point for OPTIONS RA functions for ra_serf
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


/*
 * This enum represents the current state of our XML parsing for an OPTIONS.
 */
typedef enum {
  OPTIONS,
  ACTIVITY_COLLECTION,
  HREF,
} options_state_e;

typedef struct options_state_list_t {
  /* The current state that we are in now. */
  options_state_e state;

  /* The previous state we were in. */
  struct options_state_list_t *prev;
} options_state_list_t;

struct svn_ra_serf__options_context_t {
  /* pool to allocate memory from */
  apr_pool_t *pool;

  const char *attr_val;
  apr_size_t attr_val_len;
  svn_boolean_t collect_cdata;

  /* Current state we're in */
  options_state_list_t *state;
  options_state_list_t *free_state;

  /* Return error code */
  svn_error_t *error;

  /* HTTP Status code */
  int status_code;

  /* are we done? */
  svn_boolean_t done;

  svn_ra_serf__session_t *session;
  svn_ra_serf__connection_t *conn;

  const char *path;

  const char *activity_collection;

  serf_response_acceptor_t acceptor;
  serf_response_handler_t handler;
  svn_ra_serf__xml_parser_t *parser_ctx;

};

static void
push_state(svn_ra_serf__options_context_t *options_ctx, options_state_e state)
{
  options_state_list_t *new_state;

  if (!options_ctx->free_state)
    {
      new_state = apr_palloc(options_ctx->pool, sizeof(*options_ctx->state));
    }
  else
    {
      new_state = options_ctx->free_state;
      options_ctx->free_state = options_ctx->free_state->prev;
    }
  new_state->state = state;

  /* Add it to the state chain. */
  new_state->prev = options_ctx->state;
  options_ctx->state = new_state;
}

static void pop_state(svn_ra_serf__options_context_t *options_ctx)
{
  options_state_list_t *free_state;
  free_state = options_ctx->state;
  /* advance the current state */
  options_ctx->state = options_ctx->state->prev;
  free_state->prev = options_ctx->free_state;
  options_ctx->free_state = free_state;
}

static svn_error_t *
start_options(svn_ra_serf__xml_parser_t *parser,
              void *userData,
              svn_ra_serf__dav_props_t name,
              const char **attrs)
{
  svn_ra_serf__options_context_t *options_ctx = userData;

  if (!options_ctx->state && strcmp(name.name, "options-response") == 0)
    {
      push_state(options_ctx, OPTIONS);
    }
  else if (!options_ctx->state)
    {
      /* Nothing to do. */
      return SVN_NO_ERROR;
    }
  else if (options_ctx->state->state == OPTIONS &&
           strcmp(name.name, "activity-collection-set") == 0)
    {
      push_state(options_ctx, ACTIVITY_COLLECTION);
    }
  else if (options_ctx->state->state == ACTIVITY_COLLECTION &&
           strcmp(name.name, "href") == 0)
    {
      options_ctx->collect_cdata = TRUE;
      push_state(options_ctx, HREF);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
end_options(svn_ra_serf__xml_parser_t *parser,
            void *userData,
            svn_ra_serf__dav_props_t name)
{
  svn_ra_serf__options_context_t *options_ctx = userData;
  options_state_list_t *cur_state;

  if (!options_ctx->state)
    {
      return SVN_NO_ERROR;
    }

  cur_state = options_ctx->state;

  if (cur_state->state == OPTIONS &&
      strcmp(name.name, "options-response") == 0)
    {
      pop_state(options_ctx);
    }
  else if (cur_state->state == ACTIVITY_COLLECTION &&
           strcmp(name.name, "activity-collection-set") == 0)
    {
      pop_state(options_ctx);
    }
  else if (cur_state->state == HREF &&
           strcmp(name.name, "href") == 0)
    {
      options_ctx->collect_cdata = FALSE;
      options_ctx->activity_collection = options_ctx->attr_val;
      pop_state(options_ctx);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
cdata_options(svn_ra_serf__xml_parser_t *parser,
              void *userData,
              const char *data,
              apr_size_t len)
{
  svn_ra_serf__options_context_t *ctx = userData;
  if (ctx->collect_cdata == TRUE)
    {
      svn_ra_serf__expand_string(&ctx->attr_val, &ctx->attr_val_len,
                    data, len, ctx->pool);
    }

  return SVN_NO_ERROR;
}

static serf_bucket_t*
create_options_body(void *baton,
                    serf_bucket_alloc_t *alloc,
                    apr_pool_t *pool)
{
  serf_bucket_t *body;
  body = serf_bucket_aggregate_create(alloc);
  svn_ra_serf__add_xml_header_buckets(body, alloc);
  svn_ra_serf__add_open_tag_buckets(body, alloc, "D:options",
                                    "xmlns:D", "DAV:",
                                    NULL);
  svn_ra_serf__add_tag_buckets(body, "D:activity-collection-set", NULL, alloc);
  svn_ra_serf__add_close_tag_buckets(body, alloc, "D:options");

  return body;
}

svn_boolean_t*
svn_ra_serf__get_options_done_ptr(svn_ra_serf__options_context_t *ctx)
{
  return &ctx->done;
}

const char *
svn_ra_serf__options_get_activity_collection(svn_ra_serf__options_context_t *ctx)
{
  return ctx->activity_collection;
}

svn_error_t *
svn_ra_serf__get_options_error(svn_ra_serf__options_context_t *ctx)
{
  return ctx->error;
}

svn_error_t *
svn_ra_serf__get_options_parser_error(svn_ra_serf__options_context_t *ctx)
{
  return ctx->parser_ctx->error;
}

svn_error_t *
svn_ra_serf__create_options_req(svn_ra_serf__options_context_t **opt_ctx,
                                svn_ra_serf__session_t *session,
                                svn_ra_serf__connection_t *conn,
                                const char *path,
                                apr_pool_t *pool)
{
  svn_ra_serf__options_context_t *new_ctx;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_parser_t *parser_ctx;

  new_ctx = apr_pcalloc(pool, sizeof(*new_ctx));

  new_ctx->pool = pool;

  new_ctx->path = path;

  new_ctx->session = session;
  new_ctx->conn = conn;

  handler = apr_pcalloc(pool, sizeof(*handler));

  handler->method = "OPTIONS";
  handler->path = path;
  handler->body_delegate = create_options_body;
  handler->body_type = "text/xml";
  handler->conn = conn;
  handler->session = session;

  parser_ctx = apr_pcalloc(pool, sizeof(*parser_ctx));

  parser_ctx->pool = pool;
  parser_ctx->user_data = new_ctx;
  parser_ctx->start = start_options;
  parser_ctx->end = end_options;
  parser_ctx->cdata = cdata_options;
  parser_ctx->done = &new_ctx->done;
  parser_ctx->status_code = &new_ctx->status_code;

  handler->response_handler = svn_ra_serf__handle_xml_parser;
  handler->response_baton = parser_ctx;

  svn_ra_serf__request_create(handler);

  new_ctx->parser_ctx = parser_ctx;

  *opt_ctx = new_ctx;

  return SVN_NO_ERROR;
}
