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


/* Context for both options_response_handler() and capabilities callback. */
struct options_response_ctx_t {
 /* baton for __handle_xml_parser() */
  svn_ra_serf__xml_parser_t *parser_ctx;

 /* session into which we'll store server capabilities */
  svn_ra_serf__session_t *session;      

  /* For temporary work only. */
  apr_pool_t *pool;
};


/* This implements serf_bucket_headers_do_callback_fn_t.
 */
static int
capabilities_headers_iterator_callback(void *baton,
                                       const char *key,
                                       const char *val)
{
  struct options_response_ctx_t *orc = baton;

  if (svn_cstring_casecmp(key, "dav") == 0)
    {
      /* Each header may contain multiple values, separated by commas, e.g.:
           DAV: version-control,checkout,working-resource
           DAV: merge,baseline,activity,version-controlled-collection
           DAV: http://subversion.tigris.org/xmlns/dav/svn/depth */
      apr_array_header_t *vals = svn_cstring_split(val, ",", TRUE, orc->pool);

      /* Right now we only have a few capabilities to detect, so just
         seek for them directly.  This could be written slightly more
         efficiently, but that wouldn't be worth it until we have many
         more capabilities. */

      if (svn_cstring_match_glob_list(SVN_DAV_NS_DAV_SVN_DEPTH, vals))
        {
          apr_hash_set(orc->session->capabilities, SVN_RA_CAPABILITY_DEPTH,
                       APR_HASH_KEY_STRING, SERF_CAPABILITY_YES);
        }

      if (svn_cstring_match_glob_list(SVN_DAV_NS_DAV_SVN_MERGEINFO, vals))
        {
          /* The server doesn't know what repository we're referring
             to, so it can't just say SERF_CAPABILITY_YES. */
          apr_hash_set(orc->session->capabilities, SVN_RA_CAPABILITY_MERGEINFO,
                       APR_HASH_KEY_STRING, SERF_CAPABILITY_SERVER_YES);
        }

      if (svn_cstring_match_glob_list(SVN_DAV_NS_DAV_SVN_LOG_REVPROPS, vals))
        {
          apr_hash_set(orc->session->capabilities,
                       SVN_RA_CAPABILITY_LOG_REVPROPS,
                       APR_HASH_KEY_STRING, SERF_CAPABILITY_YES);
        }

      if (svn_cstring_match_glob_list(SVN_DAV_NS_DAV_SVN_PARTIAL_REPLAY, vals))
        {
          apr_hash_set(orc->session->capabilities,
                       SVN_RA_CAPABILITY_PARTIAL_REPLAY,
                       APR_HASH_KEY_STRING, SERF_CAPABILITY_YES);
        }
    }

  /* SVN-specific headers -- if present, server supports HTTP protocol v2 */
  if (svn_cstring_casecmp(key, "svn") == 0)
    {
      if (svn_cstring_casecmp(key, SVN_DAV_ROOT_STUB_HEADER) == 0)
        orc->session->root_stub = apr_pstrdup(orc->session->pool, val);

      if (svn_cstring_casecmp(key, SVN_DAV_PEGREV_STUB_HEADER) == 0)
        orc->session->pegrev_stub = apr_pstrdup(orc->session->pool, val);

      if (svn_cstring_casecmp(key, SVN_DAV_REV_STUB_HEADER) == 0)
        orc->session->rev_stub = apr_pstrdup(orc->session->pool, val);

      if (svn_cstring_casecmp(key, SVN_DAV_YOUNGEST_REV_HEADER) == 0)
        orc->session->youngest_rev = SVN_STR_TO_REV(val);
    }

  return 0;
}


/* A custom serf_response_handler_t which is mostly a wrapper around
   svn_ra_serf__handle_xml_parser -- it just notices OPTIONS response
   headers first, before handing off to the xml parser.  */
static apr_status_t
options_response_handler(serf_request_t *request,
                         serf_bucket_t *response,
                         void *baton,
                         apr_pool_t *pool)
{
  struct options_response_ctx_t *orc = baton;
  serf_bucket_t *hdrs = serf_bucket_response_get_headers(response);

  /* Start out assuming all capabilities are unsupported. */
  apr_hash_set(orc->session->capabilities, SVN_RA_CAPABILITY_DEPTH,
               APR_HASH_KEY_STRING, SERF_CAPABILITY_NO);
  apr_hash_set(orc->session->capabilities, SVN_RA_CAPABILITY_MERGEINFO,
               APR_HASH_KEY_STRING, SERF_CAPABILITY_NO);
  apr_hash_set(orc->session->capabilities, SVN_RA_CAPABILITY_LOG_REVPROPS,
               APR_HASH_KEY_STRING, SERF_CAPABILITY_NO);

  /* Then see which ones we can discover. */
  serf_bucket_headers_do(hdrs, capabilities_headers_iterator_callback, orc);

  /* Execute the 'real' response handler to XML-parse the repsonse body. */
  return svn_ra_serf__handle_xml_parser(request, response, orc->parser_ctx, pool);
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
  struct options_response_ctx_t *options_response_ctx;

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

  options_response_ctx = apr_pcalloc(pool, sizeof(*options_response_ctx));
  options_response_ctx->parser_ctx = parser_ctx;
  options_response_ctx->session = session;
  options_response_ctx->pool = pool;

  handler->response_handler = options_response_handler;
  handler->response_baton = options_response_ctx;

  svn_ra_serf__request_create(handler);

  new_ctx->parser_ctx = parser_ctx;

  *opt_ctx = new_ctx;

  return SVN_NO_ERROR;
}
