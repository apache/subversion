/*
 * getlocations.c :  entry point for get_locations RA functions for ra_serf
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
 * This enum represents the current state of our XML parsing for a REPORT.
 */
typedef enum {
  REPORT,
  LOCATION,
} loc_state_e;

typedef struct loc_state_list_t {
  /* The current state that we are in now. */
  loc_state_e state;

  /* The previous state we were in. */
  struct loc_state_list_t *prev;
} loc_state_list_t;

typedef struct {
  /* pool to allocate memory from */
  apr_pool_t *pool;

  /* Returned location hash */
  apr_hash_t *paths;

  /* Current state we're in */
  loc_state_list_t *state;
  loc_state_list_t *free_state;

  /* Return error code */
  svn_error_t *error;

  int status_code;

  svn_boolean_t done;
} loc_context_t;


static void
push_state(loc_context_t *loc_ctx, loc_state_e state)
{
  loc_state_list_t *new_state;

  if (!loc_ctx->free_state)
    {
      new_state = apr_palloc(loc_ctx->pool, sizeof(*loc_ctx->state));
    }
  else
    {
      new_state = loc_ctx->free_state;
      loc_ctx->free_state = loc_ctx->free_state->prev;
    }
  new_state->state = state;

  /* Add it to the state chain. */
  new_state->prev = loc_ctx->state;
  loc_ctx->state = new_state;
}

static void pop_state(loc_context_t *loc_ctx)
{
  loc_state_list_t *free_state;
  free_state = loc_ctx->state;
  /* advance the current state */
  loc_ctx->state = loc_ctx->state->prev;
  free_state->prev = loc_ctx->free_state;
  loc_ctx->free_state = free_state;
}

static svn_error_t *
start_getloc(svn_ra_serf__xml_parser_t *parser,
             void *userData,
             svn_ra_serf__dav_props_t name,
             const char **attrs)
{
  loc_context_t *loc_ctx = userData;

  if (!loc_ctx->state && strcmp(name.name, "get-locations-report") == 0)
    {
      push_state(loc_ctx, REPORT);
    }
  else if (loc_ctx->state &&
           loc_ctx->state->state == REPORT &&
           strcmp(name.name, "location") == 0)
    {
      svn_revnum_t rev = SVN_INVALID_REVNUM;
      const char *revstr, *path;

      revstr = svn_ra_serf__find_attr(attrs, "rev");
      if (revstr)
        {
          rev = SVN_STR_TO_REV(revstr);
        }

      path = svn_ra_serf__find_attr(attrs, "path");

      if (SVN_IS_VALID_REVNUM(rev) && path)
        {
          apr_hash_set(loc_ctx->paths,
                       apr_pmemdup(loc_ctx->pool, &rev, sizeof(rev)),
                       sizeof(rev),
                       apr_pstrdup(loc_ctx->pool, path));
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
end_getloc(svn_ra_serf__xml_parser_t *parser,
           void *userData,
           svn_ra_serf__dav_props_t name)
{
  loc_context_t *loc_ctx = userData;
  loc_state_list_t *cur_state;

  if (!loc_ctx->state)
    {
      return SVN_NO_ERROR;
    }

  cur_state = loc_ctx->state;

  if (cur_state->state == REPORT &&
      strcmp(name.name, "get-locations-report") == 0)
    {
      pop_state(loc_ctx);
    }
  else if (cur_state->state == LOCATION &&
           strcmp(name.name, "location") == 0)
    {
      pop_state(loc_ctx);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_locations(svn_ra_session_t *ra_session,
                           apr_hash_t **locations,
                           const char *path,
                           svn_revnum_t peg_revision,
                           apr_array_header_t *location_revisions,
                           apr_pool_t *pool)
{
  loc_context_t *loc_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_parser_t *parser_ctx;
  serf_bucket_t *buckets, *tmp;
  apr_hash_t *props;
  const char *vcc_url, *relative_url, *basecoll_url, *req_url;
  int i;

  loc_ctx = apr_pcalloc(pool, sizeof(*loc_ctx));
  loc_ctx->pool = pool;
  loc_ctx->error = SVN_NO_ERROR;
  loc_ctx->done = FALSE;
  loc_ctx->paths = apr_hash_make(loc_ctx->pool);

  *locations = loc_ctx->paths;

  buckets = serf_bucket_aggregate_create(session->bkt_alloc);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<S:get-locations xmlns:S=\"",
                                      sizeof("<S:get-locations xmlns:S=\"")-1,
                                      session->bkt_alloc);
  serf_bucket_aggregate_append(buckets, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(SVN_XML_NAMESPACE,
                                      sizeof(SVN_XML_NAMESPACE)-1,
                                      session->bkt_alloc);
  serf_bucket_aggregate_append(buckets, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("\">",
                                      sizeof("\">")-1,
                                      session->bkt_alloc);
  serf_bucket_aggregate_append(buckets, tmp);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:path", path,
                               session->bkt_alloc);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:peg-revision", apr_ltoa(pool, peg_revision),
                               session->bkt_alloc);

  for (i = 0; i < location_revisions->nelts; i++)
    {
      svn_revnum_t rev = APR_ARRAY_IDX(location_revisions, i, svn_revnum_t);
      svn_ra_serf__add_tag_buckets(buckets,
                                   "S:location-revision", apr_ltoa(pool, rev),
                                   session->bkt_alloc);
    }

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</S:get-locations>",
                                      sizeof("</S:get-locations>")-1,
                                      session->bkt_alloc);
  serf_bucket_aggregate_append(buckets, tmp);

  props = apr_hash_make(pool);

  SVN_ERR(svn_ra_serf__discover_root(&vcc_url, &relative_url,
                                     session, session->conns[0],
                                     session->repos_url.path, pool));

  SVN_ERR(svn_ra_serf__retrieve_props(props,
                                      session, session->conns[0],
                                      vcc_url, peg_revision, "0",
                                      baseline_props, pool));

  basecoll_url = svn_ra_serf__get_ver_prop(props, vcc_url, peg_revision,
                                           "DAV:", "baseline-collection");

  if (!basecoll_url)
    {
      abort();
    }

  req_url = svn_path_url_add_component(basecoll_url, relative_url, pool);

  handler = apr_pcalloc(pool, sizeof(*handler));

  handler->method = "REPORT";
  handler->path = req_url;
  handler->body_buckets = buckets;
  handler->body_type = "text/xml";
  handler->conn = session->conns[0];
  handler->session = session;

  parser_ctx = apr_pcalloc(pool, sizeof(*parser_ctx));

  parser_ctx->pool = pool;
  parser_ctx->user_data = loc_ctx;
  parser_ctx->start = start_getloc;
  parser_ctx->end = end_getloc;
  parser_ctx->status_code = &loc_ctx->status_code;
  parser_ctx->done = &loc_ctx->done;

  handler->response_handler = svn_ra_serf__handle_xml_parser;
  handler->response_baton = parser_ctx;

  svn_ra_serf__request_create(handler);

  SVN_ERR(svn_ra_serf__context_run_wait(&loc_ctx->done, session, pool));

  if (loc_ctx->status_code == 404)
    {
      /* TODO Teach the parser to handle our custom error message. */
      return svn_error_create(SVN_ERR_FS_NOT_FOUND, NULL,
                              _("File doesn't exist on HEAD"));
    }

  return loc_ctx->error;
}
