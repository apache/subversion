/*
 * locks.c :  entry point for locking RA functions for ra_serf
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
#include "svn_time.h"
#include "svn_private_config.h"

#include "ra_serf.h"


/*
 * This enum represents the current state of our XML parsing for a REPORT.
 */
typedef enum {
  NONE = 0,
  PROP,
  LOCK_DISCOVERY,
  ACTIVE_LOCK,
  LOCK_TYPE,
  LOCK_SCOPE,
  DEPTH,
  TIMEOUT,
  LOCK_TOKEN,
  COMMENT,
} lock_state_e;

typedef struct {
  const char *data;
  apr_size_t len;
} lock_prop_info_t;

typedef struct {
  apr_pool_t *pool;

  const char *path;

  svn_lock_t *lock;

  svn_boolean_t force;
  svn_revnum_t revision;

  svn_boolean_t read_headers;

  /* Our HTTP status code and reason. */
  int status_code;
  const char *reason;

  /* The currently collected value as we build it up */
  const char *tmp;
  apr_size_t tmp_len;

  /* are we done? */
  svn_boolean_t done;

  /* Any errors. */
  svn_error_t *error;
} lock_info_t;


static lock_prop_info_t*
push_state(svn_ra_serf__xml_parser_t *parser,
           lock_info_t *lock_ctx,
           lock_state_e state)
{
  svn_ra_serf__xml_push_state(parser, state);
  switch (state)
    {
    case LOCK_TYPE:
    case LOCK_SCOPE:
    case DEPTH:
    case TIMEOUT:
    case LOCK_TOKEN:
    case COMMENT:
        parser->state->private = apr_pcalloc(parser->state->pool,
                                             sizeof(lock_prop_info_t));
        break;
      default:
        break;
    }

  return parser->state->private;
}

/*
 * Expat callback invoked on a start element tag for a PROPFIND response.
 */
static svn_error_t *
start_lock(svn_ra_serf__xml_parser_t *parser,
           void *userData,
           svn_ra_serf__dav_props_t name,
           const char **attrs)
{
  lock_info_t *ctx = userData;
  lock_state_e state;

  state = parser->state->current_state;

  if (state == NONE && strcmp(name.name, "prop") == 0)
    {
      svn_ra_serf__xml_push_state(parser, PROP);
    }
  else if (state == PROP &&
           strcmp(name.name, "lockdiscovery") == 0)
    {
      push_state(parser, ctx, LOCK_DISCOVERY);
    }
  else if (state == LOCK_DISCOVERY &&
           strcmp(name.name, "activelock") == 0)
    {
      push_state(parser, ctx, ACTIVE_LOCK);
    }
  else if (state == ACTIVE_LOCK)
    {
      if (strcmp(name.name, "locktype") == 0)
        {
          push_state(parser, ctx, LOCK_TYPE);
        }
      else if (strcmp(name.name, "lockscope") == 0)
        {
          push_state(parser, ctx, LOCK_SCOPE);
        }
      else if (strcmp(name.name, "depth") == 0)
        {
          push_state(parser, ctx, DEPTH);
        }
      else if (strcmp(name.name, "timeout") == 0)
        {
          push_state(parser, ctx, TIMEOUT);
        }
      else if (strcmp(name.name, "locktoken") == 0)
        {
          push_state(parser, ctx, LOCK_TOKEN);
        }
      else if (strcmp(name.name, "owner") == 0)
        {
          push_state(parser, ctx, COMMENT);
        }
    }
  else if (state == LOCK_TYPE)
    {
      if (strcmp(name.name, "write") == 0)
        {
          /* Do nothing. */
        }
      else
        {
          abort();
        }
    }
  else if (state == LOCK_SCOPE)
    {
      if (strcmp(name.name, "exclusive") == 0)
        {
          /* Do nothing. */
        }
      else
        {
          abort();
        }
    }

  return SVN_NO_ERROR;
}

/*
 * Expat callback invoked on an end element tag for a PROPFIND response.
 */
static svn_error_t *
end_lock(svn_ra_serf__xml_parser_t *parser,
         void *userData,
         svn_ra_serf__dav_props_t name)
{
  lock_info_t *ctx = userData;
  lock_state_e state;

  state = parser->state->current_state;

  if (state == PROP &&
      strcmp(name.name, "prop") == 0)
    {
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == LOCK_DISCOVERY &&
           strcmp(name.name, "lockdiscovery") == 0)
    {
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == ACTIVE_LOCK &&
           strcmp(name.name, "activelock") == 0)
    {
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == LOCK_TYPE &&
           strcmp(name.name, "locktype") == 0)
    {
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == LOCK_SCOPE &&
           strcmp(name.name, "lockscope") == 0)
    {
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == DEPTH &&
           strcmp(name.name, "depth") == 0)
    {
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == TIMEOUT &&
           strcmp(name.name, "timeout") == 0)
    {
      lock_prop_info_t *info = parser->state->private;

      if (strcmp(info->data, "Infinite") == 0)
        {
          ctx->lock->expiration_date = 0;
        }
      else
        {
          SVN_ERR(svn_time_from_cstring(&ctx->lock->creation_date,
                                        info->data, ctx->pool));
        }
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == LOCK_TOKEN &&
           strcmp(name.name, "locktoken") == 0)
    {
      lock_prop_info_t *info = parser->state->private;

      if (!ctx->lock->token && info->len)
        {
          apr_collapse_spaces((char*)info->data, info->data);
          ctx->lock->token = apr_pstrndup(ctx->pool, info->data, info->len);
        }
      /* We don't actually need the lock token. */
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == COMMENT &&
           strcmp(name.name, "owner") == 0)
    {
      lock_prop_info_t *info = parser->state->private;

      if (info->len)
        {
          ctx->lock->comment = apr_pstrndup(ctx->pool, info->data, info->len);
        }
      svn_ra_serf__xml_pop_state(parser);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
cdata_lock(svn_ra_serf__xml_parser_t *parser,
           void *userData,
           const char *data,
           apr_size_t len)
{
  lock_info_t *lock_ctx = userData;
  lock_state_e state;
  lock_prop_info_t *info;

  state = parser->state->current_state;
  info = parser->state->private;

  switch (state)
    {
    case LOCK_TYPE:
    case LOCK_SCOPE:
    case DEPTH:
    case TIMEOUT:
    case LOCK_TOKEN:
    case COMMENT:
        svn_ra_serf__expand_string(&info->data, &info->len,
                                   data, len, parser->state->pool);
        break;
      default:
        break;
    }

  return SVN_NO_ERROR;
}

static const svn_ra_serf__dav_props_t lock_props[] =
{
  { "DAV:", "lockdiscovery" },
  { NULL }
};

static apr_status_t
set_lock_headers(serf_bucket_t *headers,
                 void *baton,
                 apr_pool_t *pool)
{
  lock_info_t *lock_ctx = baton;

  if (lock_ctx->force == TRUE)
    {
      serf_bucket_headers_set(headers, SVN_DAV_OPTIONS_HEADER,
                              SVN_DAV_OPTION_LOCK_STEAL);
    }

  if (SVN_IS_VALID_REVNUM(lock_ctx->revision))
    {
      serf_bucket_headers_set(headers, SVN_DAV_VERSION_NAME_HEADER,
                              apr_ltoa(pool, lock_ctx->revision));
    }

  return APR_SUCCESS;
}

static apr_status_t
handle_lock(serf_request_t *request,
            serf_bucket_t *response,
            void *handler_baton,
            apr_pool_t *pool)
{
  svn_ra_serf__xml_parser_t *xml_ctx = handler_baton;
  lock_info_t *ctx = xml_ctx->user_data;
  apr_status_t status;

  if (ctx->read_headers == FALSE)
    {
      serf_bucket_t *headers;
      const char *val;

      serf_status_line sl;
      apr_status_t rv;

      rv = serf_bucket_response_status(response, &sl);

      ctx->status_code = sl.code;
      ctx->reason = sl.reason;

      /* 423 == Locked */
      if (sl.code == 423)
        {
          ctx->error = svn_ra_serf__handle_server_error(request, response,
                                                        pool);
          return ctx->error->apr_err;
        }

      headers = serf_bucket_response_get_headers(response);

      val = serf_bucket_headers_get(headers, SVN_DAV_LOCK_OWNER_HEADER);
      if (val)
        {
          ctx->lock->owner = apr_pstrdup(ctx->pool, val);
        }

      val = serf_bucket_headers_get(headers, SVN_DAV_CREATIONDATE_HEADER);
      if (val)
        {
          svn_error_t *err;

          err = svn_time_from_cstring(&ctx->lock->creation_date, val,
                                      ctx->pool);
          if (err)
            {
              xml_ctx->error = err;
              return err->apr_err;
            }
        }

      ctx->read_headers = TRUE;
    }

  /* Forbidden when a lock doesn't exist. */
  if (ctx->status_code == 403)
    {
      status = svn_ra_serf__handle_discard_body(request, response, NULL, pool);
      if (APR_STATUS_IS_EOF(status))
        {
          ctx->done = TRUE;
          ctx->error = svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                                         _("Lock request failed: %d %s"),
                                         ctx->status_code, ctx->reason);
        }
    }
  else
    {
      status = svn_ra_serf__handle_xml_parser(request, response, handler_baton,
                                              pool);
    }

  return status;
}

#define GET_LOCK "<?xml version=\"1.0\" encoding=\"utf-8\"?><propfind xmlns=\"DAV:\"><prop><lockdiscovery/></prop></propfind>"

static serf_bucket_t*
create_getlock_body(void *baton,
                    serf_bucket_alloc_t *alloc,
                    apr_pool_t *pool)
{
  serf_bucket_t *buckets, *tmp;
      
  buckets = serf_bucket_aggregate_create(alloc);
  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(GET_LOCK, sizeof(GET_LOCK)-1, alloc);
  serf_bucket_aggregate_append(buckets, tmp);

  return buckets;
}

#define LOCK_HEADER "<?xml version=\"1.0\" encoding=\"utf-8\"?><lockinfo xmlns=\"DAV:\">"
#define LOCK_TRAILER "</lockinfo>"

static serf_bucket_t*
create_lock_body(void *baton,
                 serf_bucket_alloc_t *alloc,
                 apr_pool_t *pool)
{
  lock_info_t *ctx = baton;
  serf_bucket_t *buckets, *tmp;
      
  buckets = serf_bucket_aggregate_create(alloc);
      
  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(LOCK_HEADER, sizeof(LOCK_HEADER)-1,
                                      alloc);
  serf_bucket_aggregate_append(buckets, tmp);

  svn_ra_serf__add_tag_buckets(buckets, "lockscope", "<exclusive/>", alloc);

  svn_ra_serf__add_tag_buckets(buckets, "locktype", "<write/>", alloc);

  if (ctx->lock->comment)
    {
      svn_stringbuf_t *xml_esc = NULL;
      svn_string_t val;

      val.data = ctx->lock->comment;
      val.len = strlen(ctx->lock->comment);

      svn_xml_escape_cdata_string(&xml_esc, &val, pool);
      svn_ra_serf__add_tag_buckets(buckets, "owner", xml_esc->data, alloc);
    }

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(LOCK_TRAILER, sizeof(LOCK_TRAILER)-1,
                                      alloc);
  serf_bucket_aggregate_append(buckets, tmp);

  return buckets;
}

svn_error_t *
svn_ra_serf__get_lock(svn_ra_session_t *ra_session,
                      svn_lock_t **lock,
                      const char *path,
                      apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_parser_t *parser_ctx;
  lock_info_t *lock_ctx;
  const char *req_url;
  svn_error_t *err;

  req_url = svn_path_url_add_component(session->repos_url.path, path, pool);

  lock_ctx = apr_pcalloc(pool, sizeof(*lock_ctx));

  lock_ctx->pool = pool;
  lock_ctx->path = req_url;
  lock_ctx->lock = svn_lock_create(pool);
  lock_ctx->lock->path = path;

  handler = apr_pcalloc(pool, sizeof(*handler));
      
  handler->method = "PROPFIND";
  handler->path = req_url;
  handler->body_type = "text/xml";
  handler->conn = session->conns[0];
  handler->session = session;
      
  parser_ctx = apr_pcalloc(pool, sizeof(*parser_ctx));
      
  parser_ctx->pool = pool;
  parser_ctx->user_data = lock_ctx;
  parser_ctx->start = start_lock;
  parser_ctx->end = end_lock;
  parser_ctx->cdata = cdata_lock;
  parser_ctx->done = &lock_ctx->done;
  
  handler->body_delegate = create_getlock_body;
  handler->body_delegate_baton = lock_ctx;

  handler->response_handler = handle_lock;
  handler->response_baton = parser_ctx;
      
  svn_ra_serf__request_create(handler);
  err = svn_ra_serf__context_run_wait(&lock_ctx->done, session, pool);

  if (err)
    {
      /* TODO Shh.  We're telling a white lie for now. */
      return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, err,
                              _("Server does not support locking features"));
    }

  *lock = lock_ctx->lock;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__lock(svn_ra_session_t *ra_session,
                  apr_hash_t *path_revs,
                  const char *comment,
                  svn_boolean_t force,
                  svn_ra_lock_callback_t lock_func,
                  void *lock_baton,
                  apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_index_t *hi;
  apr_pool_t *subpool;

  apr_pool_create(&subpool, pool);

  for (hi = apr_hash_first(pool, path_revs); hi; hi = apr_hash_next(hi))
    {
      svn_ra_serf__handler_t *handler;
      svn_ra_serf__xml_parser_t *parser_ctx;
      const char *req_url;
      lock_info_t *lock_ctx;
      const void *key;
      void *val;
      svn_error_t *error;

      apr_pool_clear(subpool);

      lock_ctx = apr_pcalloc(subpool, sizeof(*lock_ctx));

      apr_hash_this(hi, &key, NULL, &val);
      lock_ctx->pool = subpool;
      lock_ctx->path = key;
      lock_ctx->revision = *((svn_revnum_t*)val);
      lock_ctx->lock = svn_lock_create(subpool);
      lock_ctx->lock->path = key;
      lock_ctx->lock->comment = comment;

      lock_ctx->force = force;
      req_url = svn_path_url_add_component(session->repos_url.path,
                                           lock_ctx->path, subpool);

      handler = apr_pcalloc(subpool, sizeof(*handler));
      
      handler->method = "LOCK";
      handler->path = req_url;
      handler->body_type = "text/xml";
      handler->conn = session->conns[0];
      handler->session = session;
      
      parser_ctx = apr_pcalloc(subpool, sizeof(*parser_ctx));
      
      parser_ctx->pool = subpool;
      parser_ctx->user_data = lock_ctx;
      parser_ctx->start = start_lock;
      parser_ctx->end = end_lock;
      parser_ctx->cdata = cdata_lock;
      parser_ctx->done = &lock_ctx->done;
     
      handler->header_delegate = set_lock_headers;
      handler->header_delegate_baton = lock_ctx;

      handler->body_delegate = create_lock_body;
      handler->body_delegate_baton = lock_ctx;

      handler->response_handler = handle_lock;
      handler->response_baton = parser_ctx;
      
      svn_ra_serf__request_create(handler);
      error = svn_ra_serf__context_run_wait(&lock_ctx->done, session, subpool);
      SVN_ERR(lock_ctx->error);
      SVN_ERR(parser_ctx->error);
      if (error)
        {
          return svn_error_create(SVN_ERR_RA_DAV_REQUEST_FAILED, error,
                                  _("Lock request failed"));
        }

      SVN_ERR(lock_func(lock_baton, lock_ctx->path, TRUE, lock_ctx->lock, NULL,
                        subpool));
    }

  return SVN_NO_ERROR;
}

struct unlock_context_t {
  const char *token;
  svn_boolean_t force;
};

static apr_status_t
set_unlock_headers(serf_bucket_t *headers,
                   void *baton,
                   apr_pool_t *pool)
{
  struct unlock_context_t *ctx = baton;

  serf_bucket_headers_set(headers, "Lock-Token", ctx->token);
  if (ctx->force)
    {
      serf_bucket_headers_set(headers, SVN_DAV_OPTIONS_HEADER,
                              SVN_DAV_OPTION_LOCK_BREAK);
    }

  return APR_SUCCESS;
}

svn_error_t *
svn_ra_serf__unlock(svn_ra_session_t *ra_session,
                    apr_hash_t *path_tokens,
                    svn_boolean_t force,
                    svn_ra_lock_callback_t lock_func,
                    void *lock_baton,
                    apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_index_t *hi;
  apr_pool_t *subpool;

  apr_pool_create(&subpool, pool);

  for (hi = apr_hash_first(pool, path_tokens); hi; hi = apr_hash_next(hi))
    {
      svn_ra_serf__handler_t *handler;
      svn_ra_serf__simple_request_context_t *ctx;
      const char *req_url, *path, *token;
      const void *key;
      void *val;
      struct unlock_context_t unlock_ctx;

      apr_pool_clear(subpool);

      ctx = apr_pcalloc(subpool, sizeof(*ctx));

      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      token = val;

      if (force == TRUE && (!token || token[0] == '\0'))
        {
          svn_lock_t *lock;

          SVN_ERR(svn_ra_serf__get_lock(ra_session, &lock, path, subpool));
          token = lock->token;
          if (!token)
            {
              svn_error_t *err;

              err = svn_error_createf(SVN_ERR_RA_NOT_LOCKED, NULL,
                                      _("'%s' is not locked in the repository"),
                                      path);

              if (lock_func)
                SVN_ERR(lock_func(lock_baton, path, FALSE, NULL, err, subpool));

              continue;
            }
        }

      unlock_ctx.force = force;
      unlock_ctx.token = apr_pstrcat(subpool, "<", token, ">", NULL);

      req_url = svn_path_url_add_component(session->repos_url.path, path,
                                           subpool);

      handler = apr_pcalloc(subpool, sizeof(*handler));
      
      handler->method = "UNLOCK";
      handler->path = req_url;
      handler->conn = session->conns[0];
      handler->session = session;
      
      handler->header_delegate = set_unlock_headers;
      handler->header_delegate_baton = &unlock_ctx;

      handler->response_handler = svn_ra_serf__handle_status_only;
      handler->response_baton = ctx;
      
      svn_ra_serf__request_create(handler);
      SVN_ERR(svn_ra_serf__context_run_wait(&ctx->done, session, subpool));

      if (ctx->status != 204)
        {
           return svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                                    _("Unlock request failed: %d %s"),
                                    ctx->status, ctx->reason);
        }

      if (lock_func)
        SVN_ERR(lock_func(lock_baton, path, FALSE, NULL, NULL, subpool));
    }

  return SVN_NO_ERROR;
}
