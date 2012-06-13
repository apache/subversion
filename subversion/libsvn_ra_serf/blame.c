/*
 * blame.c :  entry point for blame RA functions for ra_serf
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include <apr_uri.h>
#include <serf.h>

#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_dav.h"
#include "svn_xml.h"
#include "svn_config.h"
#include "svn_delta.h"
#include "svn_path.h"
#include "svn_base64.h"
#include "svn_props.h"

#include "svn_private_config.h"

#include "private/svn_string_private.h"

#include "ra_serf.h"
#include "../libsvn_ra/ra_loader.h"


/*
 * This enum represents the current state of our XML parsing for a REPORT.
 */
typedef enum blame_state_e {
  NONE = 0,
  INITIAL = 0,
  FILE_REVS_REPORT,
  FILE_REV,
  REV_PROP,
  SET_PROP,
  REMOVE_PROP,
  MERGED_REVISION,
  TXDELTA
} blame_state_e;

typedef struct blame_info_t {
  /* Current pool. */
  apr_pool_t *pool;

  /* our suspicious file */
  const char *path;

  /* the intended suspect */
  svn_revnum_t rev;

  /* Hashtable of revision properties */
  apr_hash_t *rev_props;

  /* Added and removed properties (svn_prop_t*'s) */
  apr_array_header_t *prop_diffs;

  /* txdelta */
  svn_txdelta_window_handler_t txdelta;
  void *txdelta_baton;

  /* returned txdelta stream */
  svn_stream_t *stream;

  /* Is this property base64-encoded? */
  svn_boolean_t prop_base64;

  /* The currently collected value as we build it up */
  const char *prop_name;
  svn_stringbuf_t *prop_value;

  /* Merged revision flag */
  svn_boolean_t merged_revision;

} blame_info_t;

typedef struct blame_context_t {
  /* pool passed to get_file_revs */
  apr_pool_t *pool;

  /* parameters set by our caller */
  const char *path;
  svn_revnum_t start;
  svn_revnum_t end;
  svn_boolean_t include_merged_revisions;

  /* blame handler and baton */
  svn_file_rev_handler_t file_rev;
  void *file_rev_baton;
} blame_context_t;


#if 0
/* ### we cannot use this yet since the CDATA is unbounded and cannot be
   ### collected by the parsing context. we need a streamy mechanism for
   ### this report.  */

#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t blame_ttable[] = {
  { INITIAL, S_, "file-revs-report", FILE_REVS_REPORT,
    FALSE, { NULL }, FALSE },

  { FILE_REVS_REPORT, S_, "file-rev", FILE_REV,
    FALSE, { "path", "rev", NULL }, TRUE },

  { FILE_REV, D_, "rev-prop", REV_PROP,
    TRUE, { "name", "?encoding", NULL }, TRUE },

  { FILE_REV, D_, "set-prop", SET_PROP,
    TRUE, { "name", "?encoding", NULL }, TRUE },

  { FILE_REV, D_, "remove-prop", REMOVE_PROP,
    FALSE, { "name", "?encoding", NULL }, TRUE },

  { FILE_REV, D_, "merged-revision", MERGED_REVISION,
    FALSE, { NULL }, FALSE },

  { FILE_REV, D_, "txdelta", TXDELTA,
    TRUE, { NULL }, TRUE },

  { 0 }
};

#endif



static blame_info_t *
push_state(svn_ra_serf__xml_parser_t *parser,
           blame_context_t *blame_ctx,
           blame_state_e state)
{
  svn_ra_serf__xml_push_state(parser, state);

  if (state == FILE_REV)
    {
      blame_info_t *info;

      info = apr_pcalloc(parser->state->pool, sizeof(*info));

      info->pool = parser->state->pool;

      info->rev = SVN_INVALID_REVNUM;

      info->rev_props = apr_hash_make(info->pool);
      info->prop_diffs = apr_array_make(info->pool, 0, sizeof(svn_prop_t));

      info->prop_value = svn_stringbuf_create_empty(info->pool);

      parser->state->private = info;
    }

  return parser->state->private;
}


static const svn_string_t *
create_propval(blame_info_t *info)
{
  if (info->prop_base64)
    {
      const svn_string_t *morph;

      morph = svn_stringbuf__morph_into_string(info->prop_value);
#ifdef SVN_DEBUG
      info->prop_value = NULL;  /* morph killed the stringbuf.  */
#endif
      return svn_base64_decode_string(morph, info->pool);
    }

  return svn_string_create_from_buf(info->prop_value, info->pool);
}

static svn_error_t *
start_blame(svn_ra_serf__xml_parser_t *parser,
            svn_ra_serf__dav_props_t name,
            const char **attrs,
            apr_pool_t *scratch_pool)
{
  blame_context_t *blame_ctx = parser->user_data;
  blame_state_e state;

  state = parser->state->current_state;

  if (state == NONE && strcmp(name.name, "file-revs-report") == 0)
    {
      push_state(parser, blame_ctx, FILE_REVS_REPORT);
    }
  else if (state == FILE_REVS_REPORT &&
           strcmp(name.name, "file-rev") == 0)
    {
      blame_info_t *info;

      info = push_state(parser, blame_ctx, FILE_REV);

      info->path = apr_pstrdup(info->pool,
                               svn_xml_get_attr_value("path", attrs));
      info->rev = SVN_STR_TO_REV(svn_xml_get_attr_value("rev", attrs));
    }
  else if (state == FILE_REV)
    {
      blame_info_t *info;
      const char *enc;

      info = parser->state->private;

      if (strcmp(name.name, "rev-prop") == 0)
        {
          push_state(parser, blame_ctx, REV_PROP);
        }
      else if (strcmp(name.name, "set-prop") == 0)
        {
          push_state(parser, blame_ctx, SET_PROP);
        }
      if (strcmp(name.name, "remove-prop") == 0)
        {
          push_state(parser, blame_ctx, REMOVE_PROP);
        }
      else if (strcmp(name.name, "merged-revision") == 0)
        {
          push_state(parser, blame_ctx, MERGED_REVISION);
        }
      else if (strcmp(name.name, "txdelta") == 0)
        {
          SVN_ERR(blame_ctx->file_rev(blame_ctx->file_rev_baton,
                                      info->path, info->rev,
                                      info->rev_props, info->merged_revision,
                                      &info->txdelta, &info->txdelta_baton,
                                      info->prop_diffs, info->pool));

          info->stream = svn_base64_decode
              (svn_txdelta_parse_svndiff(info->txdelta, info->txdelta_baton,
                                         TRUE, info->pool), info->pool);

          push_state(parser, blame_ctx, TXDELTA);
        }

      state = parser->state->current_state;

      switch (state)
        {
        case REV_PROP:
        case SET_PROP:
        case REMOVE_PROP:
          info->prop_name = apr_pstrdup(info->pool,
                                        svn_xml_get_attr_value("name", attrs));
          svn_stringbuf_setempty(info->prop_value);

          enc = svn_xml_get_attr_value("encoding", attrs);
          if (enc && strcmp(enc, "base64") == 0)
            {
              info->prop_base64 = TRUE;
            }
          else
            {
              info->prop_base64 = FALSE;
            }
          break;
        case MERGED_REVISION:
            info->merged_revision = TRUE;
          break;
        default:
          break;
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
end_blame(svn_ra_serf__xml_parser_t *parser,
          svn_ra_serf__dav_props_t name,
          apr_pool_t *scratch_pool)
{
  blame_context_t *blame_ctx = parser->user_data;
  blame_state_e state;
  blame_info_t *info;

  state = parser->state->current_state;
  info = parser->state->private;

  if (state == NONE)
    {
      return SVN_NO_ERROR;
    }

  if (state == FILE_REVS_REPORT &&
      strcmp(name.name, "file-revs-report") == 0)
    {
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == FILE_REV &&
           strcmp(name.name, "file-rev") == 0)
    {
      /* no file changes. */
      if (!info->stream)
        {
          SVN_ERR(blame_ctx->file_rev(blame_ctx->file_rev_baton,
                                      info->path, info->rev,
                                      info->rev_props, FALSE,
                                      NULL, NULL,
                                      info->prop_diffs, info->pool));
        }
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == REV_PROP &&
           strcmp(name.name, "rev-prop") == 0)
    {
      apr_hash_set(info->rev_props,
                   info->prop_name, APR_HASH_KEY_STRING,
                   create_propval(info));

      svn_ra_serf__xml_pop_state(parser);
    }
  else if ((state == SET_PROP &&
            strcmp(name.name, "set-prop") == 0) ||
           (state == REMOVE_PROP &&
            strcmp(name.name, "remove-prop") == 0))
    {
      svn_prop_t *prop = apr_array_push(info->prop_diffs);
      prop->name = info->prop_name;
      prop->value = create_propval(info);

      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == MERGED_REVISION &&
           strcmp(name.name, "merged-revision") == 0)
    {
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == TXDELTA &&
           strcmp(name.name, "txdelta") == 0)
    {
      SVN_ERR(svn_stream_close(info->stream));

      svn_ra_serf__xml_pop_state(parser);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
cdata_blame(svn_ra_serf__xml_parser_t *parser,
            const char *data,
            apr_size_t len,
            apr_pool_t *scratch_pool)
{
  blame_context_t *blame_ctx = parser->user_data;
  blame_state_e state;
  blame_info_t *info;

  UNUSED_CTX(blame_ctx);

  state = parser->state->current_state;
  info = parser->state->private;

  if (state == NONE)
    {
      return SVN_NO_ERROR;
    }

  switch (state)
    {
      case REV_PROP:
      case SET_PROP:
        svn_stringbuf_appendbytes(info->prop_value, data, len);
        break;
      case TXDELTA:
        if (info->stream)
          {
            apr_size_t ret_len;

            ret_len = len;

            SVN_ERR(svn_stream_write(info->stream, data, &ret_len));
          }
        break;
      default:
        break;
    }

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_file_revs_body(serf_bucket_t **body_bkt,
                      void *baton,
                      serf_bucket_alloc_t *alloc,
                      apr_pool_t *pool)
{
  serf_bucket_t *buckets;
  blame_context_t *blame_ctx = baton;

  buckets = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_open_tag_buckets(buckets, alloc,
                                    "S:file-revs-report",
                                    "xmlns:S", SVN_XML_NAMESPACE,
                                    NULL);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:start-revision", apr_ltoa(pool, blame_ctx->start),
                               alloc);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:end-revision", apr_ltoa(pool, blame_ctx->end),
                               alloc);

  if (blame_ctx->include_merged_revisions)
    {
      svn_ra_serf__add_tag_buckets(buckets,
                                   "S:include-merged-revisions", NULL,
                                   alloc);
    }

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:path", blame_ctx->path,
                               alloc);

  svn_ra_serf__add_close_tag_buckets(buckets, alloc,
                                     "S:file-revs-report");

  *body_bkt = buckets;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_file_revs(svn_ra_session_t *ra_session,
                           const char *path,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           svn_boolean_t include_merged_revisions,
                           svn_file_rev_handler_t rev_handler,
                           void *rev_handler_baton,
                           apr_pool_t *pool)
{
  blame_context_t *blame_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_parser_t *parser_ctx;
  const char *req_url;
  svn_error_t *err;

  blame_ctx = apr_pcalloc(pool, sizeof(*blame_ctx));
  blame_ctx->pool = pool;
  blame_ctx->path = path;
  blame_ctx->file_rev = rev_handler;
  blame_ctx->file_rev_baton = rev_handler_baton;
  blame_ctx->start = start;
  blame_ctx->end = end;
  blame_ctx->include_merged_revisions = include_merged_revisions;

  SVN_ERR(svn_ra_serf__get_stable_url(&req_url, NULL /* latest_revnum */,
                                      session, NULL /* conn */,
                                      NULL /* url */, end,
                                      pool, pool));

  handler = apr_pcalloc(pool, sizeof(*handler));

  handler->handler_pool = pool;
  handler->method = "REPORT";
  handler->path = req_url;
  handler->body_type = "text/xml";
  handler->body_delegate = create_file_revs_body;
  handler->body_delegate_baton = blame_ctx;
  handler->conn = session->conns[0];
  handler->session = session;

  parser_ctx = apr_pcalloc(pool, sizeof(*parser_ctx));

  parser_ctx->pool = pool;
  parser_ctx->user_data = blame_ctx;
  parser_ctx->start = start_blame;
  parser_ctx->end = end_blame;
  parser_ctx->cdata = cdata_blame;
  parser_ctx->done = &handler->done;

  handler->response_handler = svn_ra_serf__handle_xml_parser;
  handler->response_baton = parser_ctx;

  err = svn_ra_serf__context_run_one(handler, pool);

  err = svn_error_compose_create(
            svn_ra_serf__error_on_status(handler->sline.code,
                                         handler->path,
                                         handler->location),
            err);

  return svn_error_trace(err);
}
