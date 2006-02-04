/*
 * log.c :  entry point for log RA functions for ra_serf
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
 apr_pool_t *pool;

 /* XML Parser */
 XML_Parser xmlp;

 /* Return error code */
 svn_error_t *error;

 /* are we done? */
 svn_boolean_t done;

 /* receiver function and baton */
 svn_log_message_receiver_t receiver;
 void *receiver_baton;
} log_context_t;


static void XMLCALL
start_log(void *userData, const char *raw_name, const char **attrs)
{
  log_context_t *log_ctx = userData;
  dav_props_t prop_name;

  abort();
}

static void XMLCALL
end_log(void *userData, const char *raw_name)
{
  abort();
}

static void XMLCALL
cdata_log(void *userData, const char *data, int len)
{
  abort();
}

static apr_status_t
handle_log(serf_bucket_t *response,
           void *handler_baton,
           apr_pool_t *pool)
{
  log_context_t *ctx = handler_baton;

  return handle_xml_parser(response, ctx->xmlp, &ctx->done, pool);
}

svn_error_t *
svn_ra_serf__get_log (svn_ra_session_t *ra_session,
                      const apr_array_header_t *paths,
                      svn_revnum_t start,
                      svn_revnum_t end,
                      int limit,
                      svn_boolean_t discover_changed_paths,
                      svn_boolean_t strict_node_history,
                      svn_log_message_receiver_t receiver,
                      void *receiver_baton,
                      apr_pool_t *pool)
{
  log_context_t *log_ctx;
  serf_session_t *session = ra_session->priv;
  serf_request_t *request;
  serf_bucket_t *buckets, *req_bkt, *tmp;
  apr_hash_t *props;
  const char *vcc_url, *relative_url, *baseline_url, *basecoll_url, *req_url;

  log_ctx = apr_pcalloc(pool, sizeof(*log_ctx));
  log_ctx->pool = pool;
  log_ctx->receiver = receiver;
  log_ctx->receiver_baton = receiver_baton;
  log_ctx->error = SVN_NO_ERROR;
  log_ctx->done = FALSE;

  log_ctx->xmlp = XML_ParserCreate(NULL);
  XML_SetUserData(log_ctx->xmlp, log_ctx);
  XML_SetElementHandler(log_ctx->xmlp, start_log, end_log);
  XML_SetCharacterDataHandler(log_ctx->xmlp, cdata_log);

  buckets = serf_bucket_aggregate_create(session->bkt_alloc);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<S:log-report xmlns:S=\"",
                                      sizeof("<S:log-report xmlns:S=\"")-1,
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
                  "S:start-revision", apr_ltoa(pool, start),
                  session->bkt_alloc);
  add_tag_buckets(buckets,
                  "S:end-revision", apr_ltoa(pool, end),
                  session->bkt_alloc);

  if (limit)
    {
      add_tag_buckets(buckets,
                      "S:limit", apr_ltoa(pool, limit),
                      session->bkt_alloc);
    }

  if (discover_changed_paths)
    {
      add_tag_buckets(buckets,
                      "S:discover-changed-paths", NULL,
                      session->bkt_alloc);
    }

  if (strict_node_history)
    {
      add_tag_buckets(buckets,
                      "S:strict-node-history", NULL,
                      session->bkt_alloc);
    }

  if (paths)
    {
      int i;
      for (i = 0; i < paths->nelts; i++)
        {
          add_tag_buckets(buckets,
                          "S:path", ((const char**)paths->elts)[i],
                          session->bkt_alloc);
        }
    }

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</S:log-report>",
                                      sizeof("</S:log-report>")-1,
                                      session->bkt_alloc);
  serf_bucket_aggregate_append(buckets, tmp);

  props = apr_hash_make(pool);

  SVN_ERR(retrieve_props(props, session, session->repos_url.path, "0",
                         base_props, pool));

  /* Send the request to the baseline URL */
  vcc_url = fetch_prop(props, session->repos_url.path, "DAV:",
                       "version-controlled-configuration");

  if (!vcc_url)
    {
      abort();
    }

  /* Send the request to the baseline URL */
  relative_url = fetch_prop(props, session->repos_url.path,
                            SVN_DAV_PROP_NS_DAV, "baseline-relative-path");

  if (!relative_url)
    {
      abort();
    }

  SVN_ERR(retrieve_props(props, session, vcc_url, "0", checked_in_props,
                                                  pool));

  baseline_url = fetch_prop(props, vcc_url, "DAV:", "checked-in");

  if (!baseline_url)
    {
      abort();
    }

  SVN_ERR(retrieve_props(props, session, baseline_url, "0",
                         baseline_props, pool));

  basecoll_url = fetch_prop(props, baseline_url, "DAV:", "baseline-collection");

  if (!basecoll_url)
    {
      abort();
    }

  req_url = svn_path_url_add_component(basecoll_url, relative_url, pool);

  create_serf_req(&request, &req_bkt, NULL, session,
                  "REPORT", req_url,
                  buckets, "text/xml");

  serf_request_deliver(request, req_bkt, accept_response, session,
                       handle_log, log_ctx);

  SVN_ERR(context_run_wait(&log_ctx->done, session, pool));

  return SVN_NO_ERROR;
}
