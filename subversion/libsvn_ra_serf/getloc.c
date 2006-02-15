/*
 * getloc.c :  entry point for get_locations RA functions for ra_serf
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

  /* XML Parser */
  XML_Parser xmlp;

  /* Current namespace list */
  ns_t *ns_list;

  /* Current state we're in */
  loc_state_list_t *state;
  loc_state_list_t *free_state;

  /* Return error code */
  svn_error_t *error;

  /* are we done? */
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

static void XMLCALL
start_getloc(void *userData, const char *raw_name, const char **attrs)
{
  loc_context_t *loc_ctx = userData;
  dav_props_t name;

  define_ns(&loc_ctx->ns_list, attrs, loc_ctx->pool);

  name = expand_ns(loc_ctx->ns_list, raw_name, loc_ctx->pool);

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

      revstr = find_attr(attrs, "rev");
      if (revstr)
        {
          rev = SVN_STR_TO_REV(revstr);
        }

      path = find_attr(attrs, "path");

      if (SVN_IS_VALID_REVNUM(rev) && path)
        {
          apr_hash_set(loc_ctx->paths,
                       apr_pmemdup(loc_ctx->pool, &rev, sizeof(rev)),
                       sizeof(rev),
                       apr_pstrdup(loc_ctx->pool, path));
        }
    }
}

static void XMLCALL
end_getloc(void *userData, const char *raw_name)
{
  loc_context_t *loc_ctx = userData;
  dav_props_t name;
  loc_state_list_t *cur_state;

  if (!loc_ctx->state)
    {
      return;
    }

  cur_state = loc_ctx->state;

  name = expand_ns(loc_ctx->ns_list, raw_name, loc_ctx->pool);

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
}

static apr_status_t
handle_getloc(serf_bucket_t *response,
           void *handler_baton,
           apr_pool_t *pool)
{
  loc_context_t *ctx = handler_baton;

  /* FIXME If we lost our connection, redeliver it. */
  if (!response)
    {
      abort();
    }

  return handle_xml_parser(response, ctx->xmlp, &ctx->done, pool);
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
  ra_serf_session_t *session = ra_session->priv;
  serf_request_t *request;
  serf_bucket_t *buckets, *req_bkt, *tmp;
  apr_hash_t *props;
  const char *vcc_url, *relative_url, *baseline_url, *basecoll_url, *req_url;
  int i;

  loc_ctx = apr_pcalloc(pool, sizeof(*loc_ctx));
  loc_ctx->pool = pool;
  loc_ctx->error = SVN_NO_ERROR;
  loc_ctx->done = FALSE;
  loc_ctx->paths = apr_hash_make(loc_ctx->pool);

  loc_ctx->xmlp = XML_ParserCreate(NULL);
  XML_SetUserData(loc_ctx->xmlp, loc_ctx);
  XML_SetElementHandler(loc_ctx->xmlp, start_getloc, NULL);

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

  add_tag_buckets(buckets,
                  "S:path", path,
                  session->bkt_alloc);

  add_tag_buckets(buckets,
                  "S:peg-revision", apr_ltoa(pool, peg_revision),
                  session->bkt_alloc);

  for (i = 0; i < location_revisions->nelts; i++)
    {
      svn_revnum_t rev = APR_ARRAY_IDX(location_revisions, i, svn_revnum_t);
      add_tag_buckets(buckets,
                      "S:location-revision", apr_ltoa(pool, rev),
                      session->bkt_alloc);
    }

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</S:get-locations>",
                                      sizeof("</S:get-locations>")-1,
                                      session->bkt_alloc);
  serf_bucket_aggregate_append(buckets, tmp);

  props = apr_hash_make(pool);

  SVN_ERR(retrieve_props(props, session, session->repos_url.path,
                         SVN_INVALID_REVNUM, "0", base_props, pool));

  /* Send the request to the baseline URL */
  vcc_url = get_prop(props, session->repos_url.path, "DAV:",
                       "version-controlled-configuration");

  if (!vcc_url)
    {
      abort();
    }

  /* Send the request to the baseline URL */
  relative_url = get_prop(props, session->repos_url.path,
                            SVN_DAV_PROP_NS_DAV, "baseline-relative-path");

  if (!relative_url)
    {
      abort();
    }

  SVN_ERR(retrieve_props(props, session, vcc_url, SVN_INVALID_REVNUM, "0",
                         checked_in_props, pool));

  baseline_url = get_prop(props, vcc_url, "DAV:", "checked-in");

  if (!baseline_url)
    {
      abort();
    }

  SVN_ERR(retrieve_props(props, session, baseline_url, SVN_INVALID_REVNUM, "0",
                         baseline_props, pool));

  basecoll_url = get_prop(props, baseline_url, "DAV:", "baseline-collection");

  if (!basecoll_url)
    {
      abort();
    }

  req_url = svn_path_url_add_component(basecoll_url, relative_url, pool);

  create_serf_req(&request, &req_bkt, NULL, session,
                  "REPORT", req_url,
                  buckets, "text/xml");

  serf_request_deliver(request, req_bkt, accept_response, session,
                       handle_getloc, loc_ctx);

  SVN_ERR(context_run_wait(&loc_ctx->done, session, pool));

  return loc_ctx->error;
}
