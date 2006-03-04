/*
 * blame.c :  entry point for blame RA functions for ra_serf
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
#include "svn_base64.h"
#include "svn_private_config.h"

#include "ra_serf.h"


/*
 * This enum represents the current state of our XML parsing for a REPORT.
 */
typedef enum {
  FILE_REVS_REPORT,
  FILE_REV,
  REV_PROP,
  SET_PROP,
  REMOVE_PROP,
  TXDELTA,
} blame_state_e;

typedef struct {
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
  const char *prop_attr;
  apr_size_t prop_attr_len;

  svn_string_t *prop_string;

} blame_info_t;

typedef struct blame_state_list_t {
  /* The current state that we are in now. */
  blame_state_e state;

  /* temporary pool for this state's life time. */
  apr_pool_t *pool;

  /* Information */
  blame_info_t *info;

  /* The previous state we were in. */
  struct blame_state_list_t *prev;
} blame_state_list_t;

typedef struct {
  /* pool passed to get_file_revs */
  apr_pool_t *pool;

  /* parameters set by our caller */
  const char *path;
  svn_revnum_t start;
  svn_revnum_t end;

  /* Current namespace list */
  ns_t *ns_list;

  /* Current state we're in */
  blame_state_list_t *state;
  blame_state_list_t *free_state;

  /* Return error code if needed */
  svn_error_t *error;

  /* are we done? */
  svn_boolean_t done;

  /* blame handler and baton */
  svn_ra_file_rev_handler_t file_rev;
  void *file_rev_baton;
} blame_context_t;


static void
push_state(blame_context_t *blame_ctx, blame_state_e state)
{
  blame_state_list_t *new_state;

  if (!blame_ctx->free_state)
    {
      new_state = apr_palloc(blame_ctx->pool, sizeof(*blame_ctx->state));
      apr_pool_create(&new_state->pool, blame_ctx->pool);
      new_state->info = NULL;
    }
  else
    {
      new_state = blame_ctx->free_state;
      blame_ctx->free_state = blame_ctx->free_state->prev;
    }
  new_state->state = state;

  apr_pool_clear(new_state->pool);

  if (state == FILE_REVS_REPORT)
    {
      /* do nothing for now */
    }
  else if (state == FILE_REV)
    {
      new_state->info = apr_palloc(new_state->pool, sizeof(*new_state->info));

      new_state->info->pool = new_state->pool;

      new_state->info->rev = SVN_INVALID_REVNUM;
      new_state->info->path = NULL;

      new_state->info->rev_props = apr_hash_make(new_state->info->pool);
      new_state->info->prop_diffs = apr_array_make(new_state->info->pool,
                                                   0, sizeof(svn_prop_t));

      new_state->info->stream = NULL;
    }
  /* if we have state info from our parent, reuse it. */
  else if (blame_ctx->state && blame_ctx->state->info)
    {
      new_state->info = blame_ctx->state->info;
    }
  else
    {
      abort();
    }

  /* Add it to the state chain. */
  new_state->prev = blame_ctx->state;
  blame_ctx->state = new_state;
}

static void pop_state(blame_context_t *blame_ctx)
{
  blame_state_list_t *free_state;
  free_state = blame_ctx->state;
  /* advance the current state */
  blame_ctx->state = blame_ctx->state->prev;
  free_state->prev = blame_ctx->free_state;
  blame_ctx->free_state = free_state;
  /* It's okay to reuse our info. */
  /* ctx->free_state->info = NULL; */
}

static const svn_string_t *
create_propval(blame_info_t *info)
{
  const svn_string_t *s;

  /* Include the null term. */
  s = svn_string_ncreate(info->prop_attr, info->prop_attr_len + 1, info->pool);
  if (info->prop_base64 == TRUE)
    {
      s = svn_base64_decode_string(s, info->pool);
    }
  return s;
}

static void XMLCALL
start_blame(void *userData, const char *raw_name, const char **attrs)
{
  blame_context_t *blame_ctx = userData;
  dav_props_t name;

  define_ns(&blame_ctx->ns_list, attrs, blame_ctx->pool);

  name = expand_ns(blame_ctx->ns_list, raw_name);

  if (!blame_ctx->state && strcmp(name.name, "file-revs-report") == 0)
    {
      push_state(blame_ctx, FILE_REVS_REPORT);
    }
  else if (blame_ctx->state &&
           blame_ctx->state->state == FILE_REVS_REPORT &&
           strcmp(name.name, "file-rev") == 0)
    {
      blame_info_t *info;

      push_state(blame_ctx, FILE_REV);

      info = blame_ctx->state->info;

      info->path = apr_pstrdup(info->pool, find_attr(attrs, "name"));
      info->rev = SVN_STR_TO_REV(find_attr(attrs, "rev"));
    }
  else if (blame_ctx->state &&
           blame_ctx->state->state == FILE_REV)
    {
      blame_info_t *info;
      const char *enc;

      info = blame_ctx->state->info;

      if (strcmp(name.name, "rev-prop") == 0)
        {
          push_state(blame_ctx, REV_PROP);
        }
      else if (strcmp(name.name, "set-prop") == 0)
        {
          push_state(blame_ctx, SET_PROP);
        }
      if (strcmp(name.name, "remove-prop") == 0)
        {
          push_state(blame_ctx, REMOVE_PROP);
        }
      else if (strcmp(name.name, "txdelta") == 0)
        {
          blame_ctx->file_rev(blame_ctx->file_rev_baton,
                              info->path, info->rev,
                              info->rev_props,
                              &info->txdelta, &info->txdelta_baton,
                              info->prop_diffs, info->pool);

          info->stream = svn_base64_decode
              (svn_txdelta_parse_svndiff(info->txdelta, info->txdelta_baton,
                                         TRUE, info->pool), info->pool);

          push_state(blame_ctx, TXDELTA);
        }

      switch (blame_ctx->state->state)
        {
        case REV_PROP:
        case SET_PROP:
        case REMOVE_PROP:
          info->prop_name = apr_pstrdup(info->pool, find_attr(attrs, "name"));
          info->prop_attr = NULL;
          info->prop_attr_len = 0;

          enc =  find_attr(attrs, "encoding");
          if (enc && strcmp(enc, "base64") == 0)
            {
              info->prop_base64 = TRUE;
            }
          else 
            {
              info->prop_base64 = FALSE;
            }
          break;
        }
    }
}

static void XMLCALL
end_blame(void *userData, const char *raw_name)
{
  blame_context_t *blame_ctx = userData;
  dav_props_t name;
  blame_state_list_t *cur_state;
  blame_info_t *info;

  if (!blame_ctx->state)
    {
      return;
    }

  cur_state = blame_ctx->state;
  info = cur_state->info;

  name = expand_ns(blame_ctx->ns_list, raw_name);

  if (cur_state->state == FILE_REVS_REPORT &&
      strcmp(name.name, "file-revs-report") == 0)
    {
      pop_state(blame_ctx);
    }
  else if (cur_state->state == FILE_REV &&
           strcmp(name.name, "file-rev") == 0)
    {
      /* no file changes. */
      if (!info->stream)
        {
          blame_ctx->file_rev(blame_ctx->file_rev_baton,
                              info->path, info->rev,
                              info->rev_props,
                              NULL, NULL,
                              info->prop_diffs, info->pool);
        }
      pop_state(blame_ctx);
    }
  else if ((cur_state->state == REV_PROP &&
            strcmp(name.name, "rev-prop") == 0))
    {
      apr_hash_set(info->rev_props,
                   info->prop_name, APR_HASH_KEY_STRING,
                   create_propval(info));

      pop_state(blame_ctx);
    }
  else if ((cur_state->state == SET_PROP &&
            strcmp(name.name, "set-prop") == 0) ||
           (cur_state->state == REMOVE_PROP &&
            strcmp(name.name, "remove-prop") == 0))
    {
      svn_prop_t *prop = apr_array_push(info->prop_diffs);
      prop->name = info->prop_name;
      prop->value = create_propval(info);

      pop_state(blame_ctx);
    }
  else if (cur_state->state == TXDELTA &&
           strcmp(name.name, "txdelta") == 0)
    {
      svn_stream_close(info->stream);

      pop_state(blame_ctx);
    }
}

static void XMLCALL
cdata_blame(void *userData, const char *data, int len)
{
  blame_context_t *blame_ctx = userData;

  if (!blame_ctx->state)
    {
      return;
    }

  switch (blame_ctx->state->state)
    {
      case REV_PROP:
        expand_string(&blame_ctx->state->info->prop_attr,
                      &blame_ctx->state->info->prop_attr_len,
                      data, len, blame_ctx->state->info->pool);
        break;
      case TXDELTA:
        if (blame_ctx->state->info->stream)
          {
            apr_size_t ret_len;

            ret_len = len;

            svn_stream_write(blame_ctx->state->info->stream, data, &ret_len);
            if (ret_len != len)
              {
                abort();
              }
          }
        break;
      default:
        break;
    }
}

svn_error_t *
svn_ra_serf__get_file_revs(svn_ra_session_t *ra_session,
                           const char *path,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           svn_ra_file_rev_handler_t rev_handler,
                           void *rev_handler_baton,
                           apr_pool_t *pool)
{
  blame_context_t *blame_ctx;
  ra_serf_session_t *session = ra_session->priv;
  ra_serf_handler_t *handler;
  ra_serf_xml_parser_t *parser_ctx;
  serf_bucket_t *buckets, *tmp;
  apr_hash_t *props;
  const char *vcc_url, *relative_url, *baseline_url, *basecoll_url, *req_url;

  blame_ctx = apr_pcalloc(pool, sizeof(*blame_ctx));
  blame_ctx->pool = pool;
  blame_ctx->file_rev = rev_handler;
  blame_ctx->file_rev_baton = rev_handler_baton;
  blame_ctx->start = start;
  blame_ctx->end = end;
  blame_ctx->done = FALSE;
  blame_ctx->error = SVN_NO_ERROR;

  buckets = serf_bucket_aggregate_create(session->bkt_alloc);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<S:file-revs-report xmlns:S=\"",
                                  sizeof("<S:file-revs-report xmlns:S=\"")-1,
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

  add_tag_buckets(buckets,
                  "S:path", path,
                  session->bkt_alloc);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</S:file-revs-report>",
                                      sizeof("</S:file-revs-report>")-1,
                                      session->bkt_alloc);
  serf_bucket_aggregate_append(buckets, tmp);

  props = apr_hash_make(pool);

  SVN_ERR(retrieve_props(props, session, session->conns[0],
                         session->repos_url.path,
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

  SVN_ERR(retrieve_props(props, session, session->conns[0], vcc_url,
                         SVN_INVALID_REVNUM, "0",
                         checked_in_props, pool));

  baseline_url = get_prop(props, vcc_url, "DAV:", "checked-in");

  if (!baseline_url)
    {
      abort();
    }

  SVN_ERR(retrieve_props(props, session, session->conns[0], baseline_url,
                         SVN_INVALID_REVNUM, "0", baseline_props, pool));

  basecoll_url = get_prop(props, baseline_url, "DAV:", "baseline-collection");

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

  parser_ctx->user_data = blame_ctx;
  parser_ctx->start = start_blame;
  parser_ctx->end = end_blame;
  parser_ctx->cdata = cdata_blame;
  parser_ctx->done = &blame_ctx->done;

  handler->response_handler = handle_xml_parser;
  handler->response_baton = parser_ctx;

  ra_serf_request_create(handler);

  SVN_ERR(context_run_wait(&blame_ctx->done, session, pool));

  return blame_ctx->error;
}
