/*
 * getlocationsegments.c :  entry point for get_location_segments
 *                          RA functions for ra_serf
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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
#include "svn_xml.h"
#include "svn_path.h"
#include "svn_private_config.h"
#include "../libsvn_ra/ra_loader.h"

#include "ra_serf.h"



typedef struct {
  /* location segment callback function/baton */
  svn_location_segment_receiver_t receiver;
  void *receiver_baton;

  /* subpool used only as long as a single receiver invocation */
  apr_pool_t *subpool;

  /* True iff we're looking at a child of the outer report tag */
  svn_boolean_t inside_report;

  /* Return error code */
  svn_error_t *error;

  int status_code;

  svn_boolean_t done;
} gls_context_t;


static svn_error_t *
start_gls(svn_ra_serf__xml_parser_t *parser,
          void *userData,
          svn_ra_serf__dav_props_t name,
          const char **attrs)
{
  gls_context_t *gls_ctx = userData;

  if ((! gls_ctx->inside_report)
      && strcmp(name.name, "get-location-segments-report") == 0)
    {
      gls_ctx->inside_report = TRUE;
    }
  else if (gls_ctx->inside_report
           && strcmp(name.name, "location-segment") == 0)
    {
      const char *rev_str;
      svn_revnum_t range_start = SVN_INVALID_REVNUM;
      svn_revnum_t range_end = SVN_INVALID_REVNUM;
      const char *path = NULL;

      path = svn_xml_get_attr_value("path", attrs);
      rev_str = svn_xml_get_attr_value("range-start", attrs);
      if (rev_str)
        range_start = SVN_STR_TO_REV(rev_str);
      rev_str = svn_xml_get_attr_value("range-end", attrs);
      if (rev_str)
        range_end = SVN_STR_TO_REV(rev_str);

      if (SVN_IS_VALID_REVNUM(range_start) && SVN_IS_VALID_REVNUM(range_end))
        {
          svn_location_segment_t *segment = apr_pcalloc(gls_ctx->subpool,
                                                        sizeof(*segment));
          segment->path = path;
          segment->range_start = range_start;
          segment->range_end = range_end;
          SVN_ERR(gls_ctx->receiver(segment,
                                    gls_ctx->receiver_baton,
                                    gls_ctx->subpool));
          svn_pool_clear(gls_ctx->subpool);
        }
      else
        {
          return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                  _("Expected valid revision range"));
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
end_gls(svn_ra_serf__xml_parser_t *parser,
        void *userData,
        svn_ra_serf__dav_props_t name)
{
  gls_context_t *gls_ctx = userData;

  if (strcmp(name.name, "get-location-segments-report") == 0)
    gls_ctx->inside_report = FALSE;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_location_segments(svn_ra_session_t *ra_session,
                                   const char *path,
                                   svn_revnum_t peg_revision,
                                   svn_revnum_t start_rev,
                                   svn_revnum_t end_rev,
                                   svn_location_segment_receiver_t receiver,
                                   void *receiver_baton,
                                   apr_pool_t *pool)
{
  gls_context_t *gls_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_parser_t *parser_ctx;
  serf_bucket_t *buckets;
  const char *relative_url, *basecoll_url, *req_url;
  svn_error_t *err;

  gls_ctx = apr_pcalloc(pool, sizeof(*gls_ctx));
  gls_ctx->receiver = receiver;
  gls_ctx->receiver_baton = receiver_baton;
  gls_ctx->subpool = svn_pool_create(pool);
  gls_ctx->inside_report = FALSE;
  gls_ctx->error = SVN_NO_ERROR;
  gls_ctx->done = FALSE;

  buckets = serf_bucket_aggregate_create(session->bkt_alloc);

  svn_ra_serf__add_open_tag_buckets(buckets, session->bkt_alloc,
                                    "S:get-location-segments",
                                    "xmlns:S", SVN_XML_NAMESPACE,
                                    NULL);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:path", path,
                               session->bkt_alloc);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:peg-revision",
                               apr_ltoa(pool, peg_revision),
                               session->bkt_alloc);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:start-revision",
                               apr_ltoa(pool, start_rev),
                               session->bkt_alloc);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:end-revision",
                               apr_ltoa(pool, end_rev),
                               session->bkt_alloc);

  svn_ra_serf__add_close_tag_buckets(buckets, session->bkt_alloc,
                                     "S:get-location-segments");

  SVN_ERR(svn_ra_serf__get_baseline_info(&basecoll_url, &relative_url, session,
                                         NULL, NULL, peg_revision, NULL, pool));

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
  parser_ctx->user_data = gls_ctx;
  parser_ctx->start = start_gls;
  parser_ctx->end = end_gls;
  parser_ctx->status_code = &gls_ctx->status_code;
  parser_ctx->done = &gls_ctx->done;

  handler->response_handler = svn_ra_serf__handle_xml_parser;
  handler->response_baton = parser_ctx;

  svn_ra_serf__request_create(handler);

  err = svn_ra_serf__context_run_wait(&gls_ctx->done, session, pool);

  if (gls_ctx->error || parser_ctx->error)
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
      SVN_ERR(gls_ctx->error);
      SVN_ERR(parser_ctx->error);
    }

  if (gls_ctx->inside_report)
    err = svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                            _("Location segment report failed on '%s'@'%ld'"),
                              path, peg_revision);

  SVN_ERR(svn_ra_serf__error_on_status(gls_ctx->status_code, handler->path));

  svn_pool_destroy(gls_ctx->subpool);

  if (err && (err->apr_err == SVN_ERR_UNSUPPORTED_FEATURE))
    return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, err, NULL);

  return err;
}
