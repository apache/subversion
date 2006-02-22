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

struct options_context_t {
  /* pool to allocate memory from */
  apr_pool_t *pool;

  const char *attr_val;
  apr_size_t attr_val_len;
  svn_boolean_t collect_cdata;

  /* XML Parser */
  XML_Parser xmlp;

  /* Current namespace list */
  ns_t *ns_list;

  /* Current state we're in */
  options_state_list_t *state;
  options_state_list_t *free_state;

  /* Return error code */
  svn_error_t *error;

  /* are we done? */
  svn_boolean_t done;

  ra_serf_session_t *session;

  const char *path;

  const char *activity_collection;

  serf_response_acceptor_t acceptor;
  serf_response_handler_t handler;

};

static void
push_state(options_context_t *options_ctx, options_state_e state)
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

static void pop_state(options_context_t *options_ctx)
{
  options_state_list_t *free_state;
  free_state = options_ctx->state;
  /* advance the current state */
  options_ctx->state = options_ctx->state->prev;
  free_state->prev = options_ctx->free_state;
  options_ctx->free_state = free_state;
}

static void XMLCALL
start_options(void *userData, const char *raw_name, const char **attrs)
{
  options_context_t *options_ctx = userData;
  dav_props_t name;

  define_ns(&options_ctx->ns_list, attrs, options_ctx->pool);

  name = expand_ns(options_ctx->ns_list, raw_name);

  if (!options_ctx->state && strcmp(name.name, "options-response") == 0)
    {
      push_state(options_ctx, OPTIONS);
    }
  else if (!options_ctx->state)
    {
      /* Nothing to do. */
      return;
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
}

static void XMLCALL
end_options(void *userData, const char *raw_name)
{
  options_context_t *options_ctx = userData;
  dav_props_t name;
  options_state_list_t *cur_state;

  if (!options_ctx->state)
    {
      return;
    }

  cur_state = options_ctx->state;

  name = expand_ns(options_ctx->ns_list, raw_name);

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
}

static void XMLCALL
cdata_options(void *userData, const char *data, int len)
{
  options_context_t *ctx = userData;
  if (ctx->collect_cdata == TRUE)
    {
      expand_string(&ctx->attr_val, &ctx->attr_val_len,
                    data, len, ctx->pool);
    }
}

#define OPTIONS_BODY "<?xml version=\"1.0\" encoding=\"utf-8\"?><D:options xmlns:D=\"DAV:\"><D:activity-collection-set/></D:options>"

static apr_status_t
setup_options(serf_request_t *request,
              void *setup_baton,
              serf_bucket_t **req_bkt,
              serf_response_acceptor_t *acceptor,
              void **acceptor_baton,
              serf_response_handler_t *handler,
              void **handler_baton,
              apr_pool_t *pool)
{
  options_context_t *ctx = setup_baton;
  serf_bucket_t *body_bkt;

  body_bkt = SERF_BUCKET_SIMPLE_STRING_LEN(OPTIONS_BODY,
                                           sizeof(OPTIONS_BODY) - 1,
                                           ctx->session->bkt_alloc);

  setup_serf_req(request, req_bkt, NULL, ctx->session,
                 "OPTIONS", ctx->path,
                 body_bkt, "text/xml");

  *acceptor = ctx->acceptor;
  *acceptor_baton = ctx->session;
  *handler = ctx->handler;
  *handler_baton = ctx;

  return APR_SUCCESS;
}

static apr_status_t
handle_options(serf_bucket_t *response,
               void *handler_baton,
               apr_pool_t *pool)
{
  options_context_t *ctx = handler_baton;
  apr_status_t status;
 
  if (!ctx->xmlp)
    {
      ctx->xmlp = XML_ParserCreate(NULL);
      XML_SetUserData(ctx->xmlp, ctx);
      XML_SetElementHandler(ctx->xmlp, start_options, end_options);
      XML_SetCharacterDataHandler(ctx->xmlp, cdata_options);
    }

  status = handle_xml_parser(response, ctx->xmlp, &ctx->done, pool);

  if (ctx->done)
    {
      /* TODO: Fetch useful values from response headers. */
      XML_ParserFree(ctx->xmlp);
    }

  return status;
}

svn_boolean_t*
get_options_done_ptr(options_context_t *ctx)
{
  return &ctx->done;
}

const char *
options_get_activity_collection(options_context_t *ctx)
{
  return ctx->activity_collection;
}

svn_error_t *
create_options_req(options_context_t **opt_ctx,
                   ra_serf_session_t *session,
                   serf_connection_t *conn,
                   const char *path,
                   apr_pool_t *pool)
{
  options_context_t *new_ctx;

  new_ctx = apr_pcalloc(pool, sizeof(*new_ctx));

  new_ctx->pool = pool;

  new_ctx->path = path;
  new_ctx->session = session;

  new_ctx->acceptor = accept_response;
  new_ctx->handler = handle_options;

  serf_connection_request_create(conn, setup_options, new_ctx);

  *opt_ctx = new_ctx;

  return SVN_NO_ERROR;
}
