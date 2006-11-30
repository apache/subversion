/*
 * mergeinfo.c : entry point for mergeinfo RA functions for ra_serf
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




#include "svn_ra.h"
#include "svn_xml.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_private_config.h"
#include "svn_mergeinfo.h"
#include "ra_serf.h"
#include "svn_string.h"
#include <apr_tables.h>
#include <apr_xml.h>




/* The current state of our XML parsing. */
typedef enum {
  NONE = 0,
  MERGE_INFO_REPORT,
  MERGE_INFO_ITEM,
  MERGE_INFO_PATH,
  MERGE_INFO_INFO
} mergeinfo_state_e;

/* Baton for accumulating mergeinfo.  RESULT stores the final
   mergeinfo hash result we are going to hand back to the caller of
   get_mergeinfo.  curr_path and curr_info contain the value of the
   CDATA from the merge info items as we get them from the server.  */

typedef struct {
  apr_pool_t *pool;
  const char *curr_path;
  svn_stringbuf_t *curr_info;
  apr_hash_t *result;
  svn_boolean_t done;
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
  if (state == NONE && strcmp(name.name, "merge-info-report") == 0)
    {
      svn_ra_serf__xml_push_state(parser, MERGE_INFO_REPORT);
    }
  else if (state == MERGE_INFO_REPORT &&
           strcmp(name.name, "merge-info-item") == 0)
    {
      svn_ra_serf__xml_push_state(parser, MERGE_INFO_ITEM);
      mergeinfo_ctx->curr_path = NULL;
      svn_stringbuf_setempty(mergeinfo_ctx->curr_info);
    }
  else if (state == MERGE_INFO_ITEM &&
           strcmp(name.name, "merge-info-path") == 0)
    {
      svn_ra_serf__xml_push_state(parser, MERGE_INFO_PATH);
    }
  else if (state == MERGE_INFO_ITEM &&
           strcmp(name.name, "merge-info-info") == 0)
    {
      svn_ra_serf__xml_push_state(parser, MERGE_INFO_INFO);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
end_element(svn_ra_serf__xml_parser_t *parser, void *userData,
            svn_ra_serf__dav_props_t name)
{
  mergeinfo_context_t *mergeinfo_ctx = userData;
  mergeinfo_state_e state;

  state = parser->state->current_state;

  if (state == MERGE_INFO_REPORT &&
      strcmp(name.name, "merge-info-report") == 0)
    {
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == MERGE_INFO_ITEM 
           && strcmp(name.name, "merge-info-item") == 0)
    {
      if (mergeinfo_ctx->curr_info && mergeinfo_ctx->curr_path)
        {
          apr_hash_t *path_mergeinfo;
          SVN_ERR(svn_mergeinfo_parse(mergeinfo_ctx->curr_info->data,
                                      &path_mergeinfo, mergeinfo_ctx->pool));
          apr_hash_set(mergeinfo_ctx->result, mergeinfo_ctx->curr_path,
                       APR_HASH_KEY_STRING, path_mergeinfo);
        }
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == MERGE_INFO_PATH 
           && strcmp(name.name, "merge-info-path") == 0)
    {
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == MERGE_INFO_INFO 
           && strcmp(name.name, "merge-info-info") == 0)
    {
      svn_ra_serf__xml_pop_state(parser);
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
cdata_handler(svn_ra_serf__xml_parser_t *parser, void *userData,
              const char *data, apr_size_t len)
{
  mergeinfo_context_t *mergeinfo_ctx = userData;
  mergeinfo_state_e state;

  state = parser->state->current_state;
  switch (state)
    {
    case MERGE_INFO_PATH:
      mergeinfo_ctx->curr_path = apr_pstrndup(mergeinfo_ctx->pool, data, len);
      break;

    case MERGE_INFO_INFO:
      if (mergeinfo_ctx->curr_info)
        svn_stringbuf_appendbytes(mergeinfo_ctx->curr_info, data, len);
      break;

    default:
      break;
    }

  return SVN_NO_ERROR;
}

/* Request a merge-info-report from the URL attached to SESSION,
   and fill in the MERGEINFO hash with the results.  */
svn_error_t * svn_ra_serf__get_merge_info(svn_ra_session_t *ra_session,
                                          apr_hash_t **mergeinfo,
                                          const apr_array_header_t *paths,
                                          svn_revnum_t revision,
                                          svn_boolean_t include_parents,
                                          apr_pool_t *pool)
{
  static const char minfo_request_head[]
    = "<S:merge-info-report xmlns:S=\"" SVN_XML_NAMESPACE "\">";

  static const char minfo_request_tail[] = "</S:merge-info-report>";
  int i;

  mergeinfo_context_t *mergeinfo_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_parser_t *parser_ctx;
  serf_bucket_t *buckets, *tmp;

  mergeinfo_ctx = apr_pcalloc(pool, sizeof(*mergeinfo_ctx));
  mergeinfo_ctx->pool = pool;
  mergeinfo_ctx->curr_info = svn_stringbuf_create("", pool);
  mergeinfo_ctx->done = FALSE;
  mergeinfo_ctx->result = apr_hash_make(pool);

  buckets = serf_bucket_aggregate_create(session->bkt_alloc);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(minfo_request_head,
                                      sizeof(minfo_request_head)-1,
                                      session->bkt_alloc);
  serf_bucket_aggregate_append(buckets, tmp);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:revision", apr_ltoa(pool, revision),
                               session->bkt_alloc);

  if (include_parents)
    {
      svn_ra_serf__add_tag_buckets(buckets, "S:include-parents", 
                                   NULL, session->bkt_alloc);
    }

  if (paths)
    {
      for (i = 0; i < paths->nelts; i++)
        {
          const char *this_path =
            apr_xml_quote_string(pool, ((const char **) paths->elts)[i], 0);
          svn_ra_serf__add_tag_buckets(buckets, "S:path", 
                                       this_path, session->bkt_alloc);
        }
    }

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(minfo_request_tail,
                                      sizeof(minfo_request_tail) - 1,
                                      session->bkt_alloc);

  serf_bucket_aggregate_append(buckets, tmp);

  handler = apr_pcalloc(pool, sizeof(*handler));

  handler->method = "REPORT";
  handler->path = session->repos_url_str;
  handler->body_buckets = buckets;
  handler->body_type = "text/xml";
  handler->conn = session->conns[0];
  handler->session = session;

  parser_ctx = apr_pcalloc(pool, sizeof(*parser_ctx));

  parser_ctx->pool = pool;
  parser_ctx->user_data = mergeinfo_ctx;
  parser_ctx->start = start_element;
  parser_ctx->end = end_element;
  parser_ctx->cdata = cdata_handler;
  parser_ctx->done = &mergeinfo_ctx->done;

  handler->response_handler = svn_ra_serf__handle_xml_parser;
  handler->response_baton = parser_ctx;

  svn_ra_serf__request_create(handler);

  SVN_ERR(svn_ra_serf__context_run_wait(&mergeinfo_ctx->done, session, pool));

  if (mergeinfo_ctx->done)
    *mergeinfo = mergeinfo_ctx->result;

  return SVN_NO_ERROR;
}
