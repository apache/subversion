/*
 * commit_and_merge_ranges.c :  routines for requesting and parsing
 * commit-and-merge-ranges-report.
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




#include "svn_ra.h"
#include "svn_xml.h"
#include "private/svn_dav_protocol.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_private_config.h"
#include "svn_mergeinfo.h"
#include "ra_serf.h"
#include "svn_path.h"
#include "svn_string.h"
#include <apr_tables.h>
#include <apr_xml.h>
#include "private/svn_mergeinfo_private.h"




/* The current state of our XML parsing. */
typedef enum {
  NONE = 0,
  MERGEINFO_REPORT,
  COMMIT_RANGES,
  MERGE_RANGES,
} mergeinfo_state_e;

/* Baton for accumulating commit revs and corresponding merge ranges. */

typedef struct {
  /* Needed by both mergeinfo reports. */
  apr_pool_t *pool;
  svn_boolean_t done;
  svn_mergeinfo_inheritance_t inherit;
  const char *merge_target;
  const char *merge_source;
  svn_revnum_t min_commit_rev;
  svn_revnum_t max_commit_rev;
  apr_array_header_t **merge_ranges_list;
  apr_array_header_t **commit_rangelist;
} mergeinfo_context_t;

static svn_error_t *
start_element(svn_ra_serf__xml_parser_t *parser,
              void *userData,
              svn_ra_serf__dav_props_t name,
              const char **attrs)
{
  mergeinfo_context_t *mergeinfo_ctx = userData;
  mergeinfo_state_e state;

  state = parser->state->current_state;
  if (state == NONE && 
      strcmp(name.name, SVN_DAV__COMMIT_AND_MERGE_RANGES_REPORT) == 0)
    {
      svn_ra_serf__xml_push_state(parser, MERGEINFO_REPORT);
    }
  else if (state == MERGEINFO_REPORT &&
           strcmp(name.name, SVN_DAV__MERGE_RANGES) == 0)
    {
      svn_ra_serf__xml_push_state(parser, MERGE_RANGES);
    }
  else if (state == MERGEINFO_REPORT &&
           strcmp(name.name, SVN_DAV__COMMIT_RANGES) == 0)
    {
      svn_ra_serf__xml_push_state(parser, COMMIT_RANGES);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
end_element(svn_ra_serf__xml_parser_t *parser, void *userData,
            svn_ra_serf__dav_props_t name)
{
  svn_ra_serf__xml_pop_state(parser);
  return SVN_NO_ERROR;
}


static svn_error_t *
cdata_handler(svn_ra_serf__xml_parser_t *parser, void *userData,
              const char *data, apr_size_t len)
{
  mergeinfo_context_t *mergeinfo_ctx = userData;
  mergeinfo_state_e state;
  const char *cdata_local = apr_pstrndup(mergeinfo_ctx->pool, data, len);

  state = parser->state->current_state;
  switch (state)
    {
    case MERGE_RANGES:
      SVN_ERR(svn_rangelist__parse(mergeinfo_ctx->merge_ranges_list, 
                                   cdata_local, FALSE, FALSE,
                                   mergeinfo_ctx->pool));
      break;

    case COMMIT_RANGES:
      SVN_ERR(svn_rangelist__parse(mergeinfo_ctx->commit_rangelist,
                                   cdata_local, FALSE, FALSE, 
                                   mergeinfo_ctx->pool));
      break;

    default:
      break;
    }

  return SVN_NO_ERROR;
}

static serf_bucket_t *
create_commit_and_merge_ranges_body(void *baton,
                                    serf_bucket_alloc_t *alloc,
                                    apr_pool_t *pool)
{
  mergeinfo_context_t *mergeinfo_ctx = baton;
  serf_bucket_t *body_bkt, *tmp_bkt;
  static const char minfo_report_head[] =
    "<S:" SVN_DAV__COMMIT_AND_MERGE_RANGES_REPORT
     " xmlns:S=\"" SVN_XML_NAMESPACE "\">";

  static const char minfo_report_tail[] =
    "</S:" SVN_DAV__COMMIT_AND_MERGE_RANGES_REPORT ">";

  body_bkt = serf_bucket_aggregate_create(alloc);

  tmp_bkt = SERF_BUCKET_SIMPLE_STRING_LEN(minfo_report_head,
                                          strlen(minfo_report_head), alloc);
  serf_bucket_aggregate_append(body_bkt, tmp_bkt);
  svn_ra_serf__add_tag_buckets(body_bkt, "S:" SVN_DAV__MERGE_TARGET,
                              apr_xml_quote_string(pool, 
                                                   mergeinfo_ctx->merge_target,
                                                   0),
                              alloc);

  svn_ra_serf__add_tag_buckets(body_bkt, "S:" SVN_DAV__MERGE_SOURCE,
                              apr_xml_quote_string(pool, 
                                                   mergeinfo_ctx->merge_source,
                                                   0),
                              alloc);

  svn_ra_serf__add_tag_buckets(body_bkt,
                               "S:" SVN_DAV__MIN_COMMIT_REVISION,
                               apr_ltoa(pool, mergeinfo_ctx->min_commit_rev),
                               alloc);
  svn_ra_serf__add_tag_buckets(body_bkt,
                               "S:" SVN_DAV__MAX_COMMIT_REVISION,
                               apr_ltoa(pool, mergeinfo_ctx->max_commit_rev),
                               alloc);
  svn_ra_serf__add_tag_buckets(body_bkt, "S:" SVN_DAV__INHERIT,
                               svn_inheritance_to_word(mergeinfo_ctx->inherit),
                               alloc);
  tmp_bkt = SERF_BUCKET_SIMPLE_STRING_LEN(minfo_report_tail,
                                          strlen(minfo_report_tail), alloc);
  serf_bucket_aggregate_append(body_bkt, tmp_bkt);

  return body_bkt;
}

svn_error_t *
svn_ra_serf__get_commit_and_merge_ranges(svn_ra_session_t *ra_session,
                                        apr_array_header_t **merge_ranges_list,
                                        apr_array_header_t **commit_rangelist,
                                        const char* merge_target,
                                        const char* merge_source,
                                        svn_revnum_t min_commit_rev,
                                        svn_revnum_t max_commit_rev,
                                        svn_mergeinfo_inheritance_t inherit,
                                        apr_pool_t *pool)
{
  svn_error_t *err;
  int status_code;

  mergeinfo_context_t *mergeinfo_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_parser_t *parser_ctx;
  const char *relative_url, *basecoll_url;
  const char *path;

  SVN_ERR(svn_ra_serf__get_baseline_info(&basecoll_url, &relative_url,
                                         session, NULL, max_commit_rev, pool));

  path = svn_path_url_add_component(basecoll_url, relative_url, pool);

  mergeinfo_ctx = apr_pcalloc(pool, sizeof(*mergeinfo_ctx));
  mergeinfo_ctx->pool = pool;
  mergeinfo_ctx->done = FALSE;
  mergeinfo_ctx->merge_target = merge_target;
  mergeinfo_ctx->merge_source = merge_source;
  mergeinfo_ctx->min_commit_rev = min_commit_rev;
  mergeinfo_ctx->max_commit_rev = max_commit_rev;
  mergeinfo_ctx->inherit = inherit;
  mergeinfo_ctx->merge_ranges_list = merge_ranges_list;
  mergeinfo_ctx->commit_rangelist = commit_rangelist;

  handler = apr_pcalloc(pool, sizeof(*handler));

  handler->method = "REPORT";
  handler->path = path;
  handler->conn = session->conns[0];
  handler->session = session;
  handler->body_delegate = create_commit_and_merge_ranges_body;
  handler->body_delegate_baton = mergeinfo_ctx;
  handler->body_type = "text/xml";

  parser_ctx = apr_pcalloc(pool, sizeof(*parser_ctx));

  parser_ctx->pool = pool;
  parser_ctx->user_data = mergeinfo_ctx;
  parser_ctx->start = start_element;
  parser_ctx->end = end_element;
  parser_ctx->cdata = cdata_handler;
  parser_ctx->done = &mergeinfo_ctx->done;
  parser_ctx->status_code = &status_code;

  handler->response_handler = svn_ra_serf__handle_xml_parser;
  handler->response_baton = parser_ctx;

  svn_ra_serf__request_create(handler);

  err = svn_ra_serf__context_run_wait(&mergeinfo_ctx->done, session, pool);

  if (status_code == 404)
    {
      svn_error_clear(err);
      return svn_error_createf(SVN_ERR_RA_DAV_PATH_NOT_FOUND, NULL,
                               "'%s' path not found",
                               handler->path);
    }

  /* If the server responds with HTTP_NOT_IMPLEMENTED (which ra_serf
     translates into a Subversion error), assume its mod_dav_svn is
     too old to understand the mergeinfo-report REPORT.

     ### It would be less expensive if we knew the server's
     ### capabilities *before* sending our REPORT.

     ### We can do that, with svn_ra_has_capability()...  -Karl */
  if (err)
    {
      if (err->apr_err == SVN_ERR_UNSUPPORTED_FEATURE)
        {
          *commit_rangelist = apr_array_make(pool, 0,
                                             sizeof(svn_merge_range_t *));
          *merge_ranges_list = apr_array_make(pool, 0,
                                              sizeof(svn_merge_range_t *));

          svn_error_clear(err);
        }
      else
        return err;
    }

  return SVN_NO_ERROR;
}
