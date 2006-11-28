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


/*
 * This enum represents the current state of our XML parsing for a REPORT.
 */
typedef enum {
  NONE = 0,
  REPORT,
  ITEM,
  VERSION,
  CREATOR,
  DATE,
  COMMENT,
  ADDED_PATH,
  REPLACED_PATH,
  DELETED_PATH,
  MODIFIED_PATH,
} log_state_e;

typedef struct {
  apr_pool_t *pool;

  /* The currently collected value as we build it up */
  const char *tmp;
  apr_size_t tmp_len;

  /* Temporary change path - ultimately inserted into changed_paths hash. */
  svn_log_changed_path_t *tmp_path;

  /* Hashtable of paths */
  apr_hash_t *changed_paths;

  /* Other log fields */
  svn_revnum_t version;
  const char *creator;
  const char *date;
  const char *comment;
} log_info_t;

typedef struct {
  apr_pool_t *pool;

  /* parameters set by our caller */
  int limit;
  int count;
  svn_boolean_t changed_paths;

  /* are we done? */
  svn_boolean_t done;

  /* log receiver function and baton */
  svn_log_message_receiver_t receiver;
  void *receiver_baton;
} log_context_t;


static log_info_t *
push_state(svn_ra_serf__xml_parser_t *parser,
           log_context_t *log_ctx,
           log_state_e state)
{
  svn_ra_serf__xml_push_state(parser, state);

  if (state == ITEM)
    {
      log_info_t *info;

      info = apr_pcalloc(parser->state->pool, sizeof(*info));

      info->pool = parser->state->pool;
      info->version = SVN_INVALID_REVNUM;

      parser->state->private = info;
    }

  if (state == ADDED_PATH || state == REPLACED_PATH ||
      state == DELETED_PATH || state == MODIFIED_PATH)
    {
      log_info_t *info = parser->state->private;

      if (!info->changed_paths)
        {
          info->changed_paths = apr_hash_make(info->pool);
        }

      info->tmp_path = apr_pcalloc(info->pool, sizeof(*info->tmp_path));
      info->tmp_path->copyfrom_rev = SVN_INVALID_REVNUM;
    }

  return parser->state->private;
}

static svn_error_t *
start_log(svn_ra_serf__xml_parser_t *parser,
          void *userData,
          svn_ra_serf__dav_props_t name,
          const char **attrs)
{
  log_context_t *log_ctx = userData;
  log_state_e state;

  state = parser->state->current_state;

  if (state == NONE &&
      strcmp(name.name, "log-report") == 0)
    {
      push_state(parser, log_ctx, REPORT);
    }
  else if (state == REPORT &&
           strcmp(name.name, "log-item") == 0)
    {
      log_ctx->count++;
      if (log_ctx->limit && log_ctx->count > log_ctx->limit)
        {
          return SVN_NO_ERROR;
        }

      push_state(parser, log_ctx, ITEM);
    }
  else if (state == ITEM)
    {
      log_info_t *info;

      if (strcmp(name.name, "version-name") == 0)
        {
          push_state(parser, log_ctx, VERSION);
        }
      else if (strcmp(name.name, "creator-displayname") == 0)
        {
          push_state(parser, log_ctx, CREATOR);
        }
      else if (strcmp(name.name, "date") == 0)
        {
          push_state(parser, log_ctx, DATE);
        }
      else if (strcmp(name.name, "comment") == 0)
        {
          push_state(parser, log_ctx, COMMENT);
        }
      else if (strcmp(name.name, "added-path") == 0)
        {
          const char *copy_path, *copy_rev_str;

          info = push_state(parser, log_ctx, ADDED_PATH);
          info->tmp_path->action = 'A';

          copy_path = svn_ra_serf__find_attr(attrs, "copyfrom-path");
          copy_rev_str = svn_ra_serf__find_attr(attrs, "copyfrom-rev");
          if (copy_path && copy_rev_str)
            {
              svn_revnum_t copy_rev;

              copy_rev = SVN_STR_TO_REV(copy_rev_str);
              if (SVN_IS_VALID_REVNUM(copy_rev))
                {
                  info->tmp_path->copyfrom_path = apr_pstrdup(info->pool,
                                                              copy_path);
                  info->tmp_path->copyfrom_rev = copy_rev;
                }
            }
        }
      else if (strcmp(name.name, "replaced-path") == 0)
        {
          const char *copy_path, *copy_rev_str;

          info = push_state(parser, log_ctx, REPLACED_PATH);
          info->tmp_path->action = 'R';

          copy_path = svn_ra_serf__find_attr(attrs, "copyfrom-path");
          copy_rev_str = svn_ra_serf__find_attr(attrs, "copyfrom-rev");
          if (copy_path && copy_rev_str)
            {
              svn_revnum_t copy_rev;

              copy_rev = SVN_STR_TO_REV(copy_rev_str);
              if (SVN_IS_VALID_REVNUM(copy_rev))
                {
                  info->tmp_path->copyfrom_path = apr_pstrdup(info->pool,
                                                              copy_path);
                  info->tmp_path->copyfrom_rev = copy_rev;
                }
            }
        }
      else if (strcmp(name.name, "deleted-path") == 0)
        {
          info = push_state(parser, log_ctx, DELETED_PATH);
          info->tmp_path->action = 'D';
        }
      else if (strcmp(name.name, "modified-path") == 0)
        {
          info = push_state(parser, log_ctx, MODIFIED_PATH);
          info->tmp_path->action = 'M';
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
end_log(svn_ra_serf__xml_parser_t *parser,
        void *userData,
        svn_ra_serf__dav_props_t name)
{
  log_context_t *log_ctx = userData;
  log_state_e state;
  log_info_t *info;

  state = parser->state->current_state;
  info = parser->state->private;

  if (state == REPORT &&
      strcmp(name.name, "log-report") == 0)
    {
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == ITEM &&
           strcmp(name.name, "log-item") == 0)
    {
      /* Give the info to the reporter */
      SVN_ERR(log_ctx->receiver(log_ctx->receiver_baton,
                                info->changed_paths,
                                info->version,
                                info->creator,
                                info->date,
                                info->comment,
                                info->pool));

      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == VERSION &&
           strcmp(name.name, "version-name") == 0)
    {
      info->version = SVN_STR_TO_REV(info->tmp);
      info->tmp_len = 0;
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == CREATOR &&
           strcmp(name.name, "creator-displayname") == 0)
    {
      info->creator = apr_pstrmemdup(info->pool, info->tmp, info->tmp_len);
      info->tmp_len = 0;
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == DATE &&
           strcmp(name.name, "date") == 0)
    {
      info->date = apr_pstrmemdup(info->pool, info->tmp, info->tmp_len);
      info->tmp_len = 0;
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == COMMENT &&
           strcmp(name.name, "comment") == 0)
    {
      info->comment = apr_pstrmemdup(info->pool, info->tmp, info->tmp_len);
      info->tmp_len = 0;
      svn_ra_serf__xml_pop_state(parser);
    }
  else if ((state == ADDED_PATH &&
            strcmp(name.name, "added-path") == 0) ||
           (state == DELETED_PATH &&
            strcmp(name.name, "deleted-path") == 0) ||
           (state == MODIFIED_PATH &&
            strcmp(name.name, "modified-path") == 0) ||
           (state == REPLACED_PATH &&
            strcmp(name.name, "replaced-path") == 0))
    {
      char *path;

      path = apr_pstrmemdup(info->pool, info->tmp, info->tmp_len);
      info->tmp_len = 0;

      apr_hash_set(info->changed_paths, path, APR_HASH_KEY_STRING,
                   info->tmp_path);
      svn_ra_serf__xml_pop_state(parser);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
cdata_log(svn_ra_serf__xml_parser_t *parser,
          void *userData,
          const char *data,
          apr_size_t len)
{
  log_context_t *log_ctx = userData;
  log_state_e state;
  log_info_t *info;

  state = parser->state->current_state;
  info = parser->state->private;

  switch (state)
    {
      case VERSION:
      case CREATOR:
      case DATE:
      case COMMENT:
      case ADDED_PATH:
      case REPLACED_PATH:
      case DELETED_PATH:
      case MODIFIED_PATH:
        svn_ra_serf__expand_string(&info->tmp, &info->tmp_len,
                                   data, len, parser->state->pool);
        break;
      default:
        break;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_log(svn_ra_session_t *ra_session,
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
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_parser_t *parser_ctx;
  serf_bucket_t *buckets, *tmp;
  apr_hash_t *props;
  svn_revnum_t peg_rev;
  const char *vcc_url, *relative_url, *baseline_url, *basecoll_url, *req_url;

  log_ctx = apr_pcalloc(pool, sizeof(*log_ctx));
  log_ctx->pool = pool;
  log_ctx->receiver = receiver;
  log_ctx->receiver_baton = receiver_baton;
  log_ctx->limit = limit;
  log_ctx->changed_paths = discover_changed_paths;
  log_ctx->done = FALSE;

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

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:start-revision", apr_ltoa(pool, start),
                               session->bkt_alloc);
  svn_ra_serf__add_tag_buckets(buckets,
                               "S:end-revision", apr_ltoa(pool, end),
                               session->bkt_alloc);

  if (limit)
    {
      svn_ra_serf__add_tag_buckets(buckets,
                                   "S:limit", apr_ltoa(pool, limit),
                                   session->bkt_alloc);
    }

  if (discover_changed_paths)
    {
      svn_ra_serf__add_tag_buckets(buckets,
                                   "S:discover-changed-paths", NULL,
                                   session->bkt_alloc);
    }

  if (strict_node_history)
    {
      svn_ra_serf__add_tag_buckets(buckets,
                                   "S:strict-node-history", NULL,
                                   session->bkt_alloc);
    }

  if (paths)
    {
      int i;
      for (i = 0; i < paths->nelts; i++)
        {
          svn_ra_serf__add_tag_buckets(buckets,
                                       "S:path", ((const char**)paths->elts)[i],
                                       session->bkt_alloc);
        }
    }

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</S:log-report>",
                                      sizeof("</S:log-report>")-1,
                                      session->bkt_alloc);
  serf_bucket_aggregate_append(buckets, tmp);

  props = apr_hash_make(pool);

  SVN_ERR(svn_ra_serf__discover_root(&vcc_url, &relative_url,
                                     session, session->conns[0],
                                     session->repos_url.path, pool));

  /* At this point, we may have a deleted file.  So, we'll match ra_dav's
   * behavior and use the larger of start or end as our 'peg' rev.
   */
  peg_rev = (start > end) ? start : end;

  SVN_ERR(svn_ra_serf__retrieve_props(props, session, session->conns[0],
                                      vcc_url, peg_rev, "0",
                                      checked_in_props, pool));

  baseline_url = svn_ra_serf__get_ver_prop(props, vcc_url, peg_rev,
                                           "DAV:", "href");

  if (!baseline_url)
    {
      abort();
    }

  SVN_ERR(svn_ra_serf__retrieve_props(props, session, session->conns[0],
                                      baseline_url, peg_rev, "0",
                                      baseline_props, pool));

  basecoll_url = svn_ra_serf__get_ver_prop(props, baseline_url, peg_rev,
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
  parser_ctx->user_data = log_ctx;
  parser_ctx->start = start_log;
  parser_ctx->end = end_log;
  parser_ctx->cdata = cdata_log;
  parser_ctx->done = &log_ctx->done;

  handler->response_handler = svn_ra_serf__handle_xml_parser;
  handler->response_baton = parser_ctx;

  svn_ra_serf__request_create(handler);

  SVN_ERR(svn_ra_serf__context_run_wait(&log_ctx->done, session, pool));

  return SVN_NO_ERROR;
}
