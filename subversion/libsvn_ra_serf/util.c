/*
 * util.c : serf utility routines for ra_serf
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



#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_base64.h>

#include <serf.h>
#include <serf_bucket_types.h>

#include "svn_path.h"
#include "svn_private_config.h"

#include "ra_serf.h"


/* Fix for older expat 1.95.x's that do not define
 * XML_STATUS_OK/XML_STATUS_ERROR
 */
#ifndef XML_STATUS_OK
#define XML_STATUS_OK    1
#define XML_STATUS_ERROR 0
#endif

serf_bucket_t *
svn_ra_serf__conn_setup(apr_socket_t *sock,
                        void *baton,
                        apr_pool_t *pool)
{
  serf_bucket_t *bucket;
  svn_ra_serf__connection_t *conn = baton;

  bucket = serf_bucket_socket_create(sock, conn->bkt_alloc);
  if (conn->using_ssl)
    {
      bucket = serf_bucket_ssl_decrypt_create(bucket, conn->ssl_context,
                                              conn->bkt_alloc);
      if (!conn->ssl_context)
        {
          conn->ssl_context = serf_bucket_ssl_decrypt_context_get(bucket);
        }
    }

  return bucket;
}

serf_bucket_t*
svn_ra_serf__accept_response(serf_request_t *request,
                             serf_bucket_t *stream,
                             void *acceptor_baton,
                             apr_pool_t *pool)
{
  serf_bucket_t *c;
  serf_bucket_alloc_t *bkt_alloc;

  bkt_alloc = serf_request_get_alloc(request);
  c = serf_bucket_barrier_create(stream, bkt_alloc);

  return serf_bucket_response_create(c, bkt_alloc);
}

static serf_bucket_t*
accept_head(serf_request_t *request,
            serf_bucket_t *stream,
            void *acceptor_baton,
            apr_pool_t *pool)
{
  serf_bucket_t *response;

  response = svn_ra_serf__accept_response(request, stream, acceptor_baton,
                                          pool);

  /* We know we shouldn't get a response body. */
  serf_bucket_response_set_head(response);

  return response;
}

void
svn_ra_serf__conn_closed(serf_connection_t *conn,
                         void *closed_baton,
                         apr_status_t why,
                         apr_pool_t *pool)
{
  svn_ra_serf__connection_t *our_conn = closed_baton;

  if (why)
    {
      abort();
    }

  if (our_conn->using_ssl)
    {
      our_conn->ssl_context = NULL;
    }
}

apr_status_t
svn_ra_serf__cleanup_serf_session(void *data)
{
  svn_ra_serf__session_t *serf_sess = data;
  int i;

  /* If we are cleaning up due to an error, don't call connection_close
   * as we're already on our way out of here and we'll defer to serf's
   * cleanups.
   */
  if (serf_sess->pending_error)
    {
      return APR_SUCCESS;
    }

  for (i = 0; i < serf_sess->num_conns; i++)
    {
      if (serf_sess->conns[i])
        {
          serf_connection_close(serf_sess->conns[i]->conn);
          serf_sess->conns[i] = NULL;
        }
    }
  return APR_SUCCESS;
}

void
svn_ra_serf__setup_serf_req(serf_request_t *request,
                            serf_bucket_t **req_bkt,
                            serf_bucket_t **ret_hdrs_bkt,
                            svn_ra_serf__connection_t *conn,
                            const char *method, const char *url,
                            serf_bucket_t *body_bkt, const char *content_type)
{
  serf_bucket_t *hdrs_bkt;

  *req_bkt = serf_bucket_request_create(method, url, body_bkt,
                                        serf_request_get_alloc(request));

  hdrs_bkt = serf_bucket_request_get_headers(*req_bkt);
  serf_bucket_headers_setn(hdrs_bkt, "Host", conn->hostinfo);
  serf_bucket_headers_setn(hdrs_bkt, "User-Agent", "svn/ra_serf");
  if (content_type)
    {
      serf_bucket_headers_setn(hdrs_bkt, "Content-Type", content_type);
    }
  if (conn->auth_header && conn->auth_value)
    {
      serf_bucket_headers_setn(hdrs_bkt, conn->auth_header, conn->auth_value);
    }

  /* Set up SSL if we need to */
  if (conn->using_ssl)
    {
      *req_bkt = serf_bucket_ssl_encrypt_create(*req_bkt, conn->ssl_context,
                                            serf_request_get_alloc(request));
      if (!conn->ssl_context)
        {
          conn->ssl_context = serf_bucket_ssl_encrypt_context_get(*req_bkt);
        }
    }

  if (ret_hdrs_bkt)
    {
      *ret_hdrs_bkt = hdrs_bkt;
    }
}

svn_error_t *
svn_ra_serf__context_run_wait(svn_boolean_t *done,
                              svn_ra_serf__session_t *sess,
                              apr_pool_t *pool)
{
  apr_status_t status;

  sess->pending_error = SVN_NO_ERROR;

  while (!*done)
    {
      int i;

      status = serf_context_run(sess->context, SERF_DURATION_FOREVER, pool);
      if (APR_STATUS_IS_TIMEUP(status))
        {
          continue;
        }
      if (status)
        {
          if (sess->pending_error)
            { 
              return sess->pending_error;
            }
          return svn_error_wrap_apr(status, "Error running context");
        }
      /* Debugging purposes only! */
      serf_debug__closed_conn(sess->bkt_alloc);
      for (i = 0; i < sess->num_conns; i++)
        {
         serf_debug__closed_conn(sess->conns[i]->bkt_alloc);
        }
    }

  return SVN_NO_ERROR;
}

apr_status_t
svn_ra_serf__is_conn_closing(serf_bucket_t *response)
{
  serf_bucket_t *hdrs;
  const char *val;

  hdrs = serf_bucket_response_get_headers(response);
  val = serf_bucket_headers_get(hdrs, "Connection");
  if (val && strcasecmp("close", val) == 0)
    {
      return SERF_ERROR_CLOSING;
    }

  return APR_EOF;
}

/*
 * Expat callback invoked on a start element tag for an error response.
 */
static svn_error_t *
start_error(svn_ra_serf__xml_parser_t *parser,
            void *userData,
            svn_ra_serf__dav_props_t name,
            const char **attrs)
{
  svn_ra_serf__server_error_t *ctx = userData;

  if (!ctx->in_error && 
      strcmp(name.namespace, "DAV:") == 0 &&
      strcmp(name.name, "error") == 0)
    {
      ctx->in_error = TRUE;
    }
  else if (ctx->in_error && strcmp(name.name, "human-readable") == 0)
    {
      const char *err_code;

      err_code = svn_ra_serf__find_attr(attrs, "errcode");
      if (err_code)
        {
          ctx->error->apr_err = apr_atoi64(err_code);
        }
      else
        {
          ctx->error->apr_err = APR_EGENERAL;
        }
      ctx->collect_message = TRUE;
    }

  return SVN_NO_ERROR;
}

/*
 * Expat callback invoked on an end element tag for a PROPFIND response.
 */
static svn_error_t *
end_error(svn_ra_serf__xml_parser_t *parser,
          void *userData,
          svn_ra_serf__dav_props_t name)
{
  svn_ra_serf__server_error_t *ctx = userData;

  if (ctx->in_error &&
      strcmp(name.namespace, "DAV:") == 0 &&
      strcmp(name.name, "error") == 0)
    {
      ctx->in_error = FALSE;
    }
  if (ctx->in_error && strcmp(name.name, "human-readable") == 0)
    {
      ctx->collect_message = FALSE;
    }

  return SVN_NO_ERROR;
}

/*
 * Expat callback invoked on CDATA elements in an error response.
 *
 * This callback can be called multiple times.
 */
static svn_error_t *
cdata_error(svn_ra_serf__xml_parser_t *parser,
            void *userData,
            const char *data,
            apr_size_t len)
{
  svn_ra_serf__server_error_t *ctx = userData;

  /* Skip blank lines in the human-readable error responses. */
  if (ctx->collect_message && (len != 1 || data[0] != '\n'))
    {
      svn_ra_serf__expand_string(&ctx->error->message, &ctx->message_len,
                                 data, len, ctx->error->pool);
    }

  return SVN_NO_ERROR;
}

apr_status_t
svn_ra_serf__handle_discard_body(serf_request_t *request,
                                 serf_bucket_t *response,
                                 void *baton,
                                 apr_pool_t *pool)
{
  apr_status_t status;
  svn_ra_serf__server_error_t *server_err = baton;

  if (server_err)
    {
      if (!server_err->init)
        {
          serf_bucket_t *hdrs;
          const char *val;
          
          server_err->init = TRUE;
          hdrs = serf_bucket_response_get_headers(response);
          val = serf_bucket_headers_get(hdrs, "Content-Type");
          if (val && strncasecmp(val, "text/xml", sizeof("text/xml") - 1) == 0)
            {
              server_err->error = svn_error_create(APR_SUCCESS, NULL, NULL);
              server_err->has_xml_response = TRUE;
              server_err->parser.pool = server_err->error->pool;
              server_err->parser.user_data = server_err;
              server_err->parser.start = start_error;
              server_err->parser.end = end_error;
              server_err->parser.cdata = cdata_error;
              server_err->parser.done = &server_err->done;
              server_err->parser.ignore_errors = TRUE;
            }
          else
            {
              server_err->error = SVN_NO_ERROR;
            }
        }

      if (server_err->has_xml_response)
        {
          status = svn_ra_serf__handle_xml_parser(request, response,
                                                  &server_err->parser, pool);

          if (server_err->done && server_err->error->apr_err == APR_SUCCESS) 
            {
              svn_error_clear(server_err->error);
              server_err->error = SVN_NO_ERROR;
            }

          return status;
        }
      
    }

  /* Just loop through and discard the body. */
  while (1)
    {
      const char *data;
      apr_size_t len;

      status = serf_bucket_read(response, SERF_READ_ALL_AVAIL, &data, &len);

      if (status)
        {
          return status;
        }

      /* feed me */
    }
}

apr_status_t
svn_ra_serf__handle_status_only(serf_request_t *request,
                                serf_bucket_t *response,
                                void *baton,
                                apr_pool_t *pool)
{
  apr_status_t status;
  svn_ra_serf__simple_request_context_t *ctx = baton;

  status = svn_ra_serf__handle_discard_body(request, response,
                                            &ctx->server_error, pool);

  if (APR_STATUS_IS_EOF(status))
    {
      serf_status_line sl;
      apr_status_t rv;

      rv = serf_bucket_response_status(response, &sl);
      
      ctx->status = sl.code;
      ctx->reason = sl.reason;

      ctx->done = TRUE;
    }

  return status;
}

static apr_status_t
handle_auth(svn_ra_serf__session_t *session,
            svn_ra_serf__connection_t *conn,
            serf_request_t *request,
            serf_bucket_t *response,
            apr_pool_t *pool)
{
  void *creds;
  svn_auth_cred_simple_t *simple_creds;
  const char *tmp;
  apr_size_t tmp_len, encoded_len;
  svn_error_t *error;
  int i;

  if (!session->realm)
    {
      serf_bucket_t *hdrs;
      char *cur, *last, *auth_hdr, *realm_name;
      apr_port_t port;

      hdrs = serf_bucket_response_get_headers(response);
      auth_hdr = (char*)serf_bucket_headers_get(hdrs, "WWW-Authenticate");

      if (!auth_hdr)
        {
          abort();
        }

      cur = apr_strtok(auth_hdr, " ", &last);
      while (cur)
        {
          if (strcmp(cur, "Basic") == 0)
            {
              char *attr;

              attr = apr_strtok(NULL, "=", &last);
              if (strcmp(attr, "realm") == 0)
                {
                  realm_name = apr_strtok(NULL, "=", &last);
                  if (realm_name[0] == '\"') 
                    {
                      apr_size_t realm_len;

                      realm_len = strlen(realm_name);
                      if (realm_name[realm_len - 1] == '\"')
                        {
                          realm_name[realm_len - 1] = '\0';
                          realm_name++;
                        }
                    }
                }
              else
                {
                  abort();
                }
            }
          else
            {
              /* Support more authentication mechanisms. */
              abort();
            }
          cur = apr_strtok(NULL, " ", &last);
        }

      if (!realm_name)
        {
          abort();
        }

      if (session->repos_url.port_str)
        {
          port = session->repos_url.port;
        }
      else
        {
          port = apr_uri_port_of_scheme(session->repos_url.scheme);
        }

      session->realm = apr_psprintf(session->pool, "<%s://%s:%d> %s",
                                    session->repos_url.scheme,
                                    session->repos_url.hostname,
                                    port,
                                    realm_name);

      error = svn_auth_first_credentials(&creds,
                                         &session->auth_state,
                                         SVN_AUTH_CRED_SIMPLE,
                                         session->realm,
                                         session->wc_callbacks->auth_baton,
                                         session->pool);
    }
  else
    {
      error = svn_auth_next_credentials(&creds,
                                        session->auth_state,
                                        session->pool);
    }
  
  session->auth_attempts++;

  if (error)
    {
      abort();
    }

  if (!creds || session->auth_attempts > 4)
    {
      /* No more credentials. */
      printf("No more credentials or we tried too many times.  Sorry.\n");
      return APR_EGENERAL;
    }

  simple_creds = creds;

  tmp = apr_pstrcat(session->pool,
                    simple_creds->username, ":", simple_creds->password, NULL);
  tmp_len = strlen(tmp);

  encoded_len = apr_base64_encode_len(tmp_len);

  session->auth_value = apr_palloc(session->pool, encoded_len + 6);

  apr_cpystrn(session->auth_value, "Basic ", 7);

  apr_base64_encode(&session->auth_value[6], tmp, tmp_len);

  session->auth_header = "Authorization";

  /* FIXME Come up with a cleaner way of changing the connection auth. */
  for (i = 0; i < session->num_conns; i++)
    {
      session->conns[i]->auth_header = session->auth_header;
      session->conns[i]->auth_value = session->auth_value;
    }

  return APR_SUCCESS;
}

static void
start_xml(void *userData, const char *raw_name, const char **attrs)
{
  svn_ra_serf__xml_parser_t *parser = userData;
  svn_ra_serf__dav_props_t name;

  if (parser->error)
    return;

  if (!parser->state)
    svn_ra_serf__xml_push_state(parser, 0);

  svn_ra_serf__define_ns(&parser->state->ns_list, attrs, parser->state->pool);

  name = svn_ra_serf__expand_ns(parser->state->ns_list, raw_name);

  parser->error = parser->start(parser, parser->user_data, name, attrs);
}

static void
end_xml(void *userData, const char *raw_name)
{
  svn_ra_serf__xml_parser_t *parser = userData;
  svn_ra_serf__dav_props_t name;

  if (parser->error)
    return;

  name = svn_ra_serf__expand_ns(parser->state->ns_list, raw_name);

  parser->error = parser->end(parser, parser->user_data, name);
}

static void
cdata_xml(void *userData, const char *data, int len)
{
  svn_ra_serf__xml_parser_t *parser = userData;

  if (parser->error)
    return;

  if (!parser->state)
    svn_ra_serf__xml_push_state(parser, 0);

  parser->error = parser->cdata(parser, parser->user_data, data, len);
}

apr_status_t
svn_ra_serf__handle_xml_parser(serf_request_t *request,
                               serf_bucket_t *response,
                               void *baton,
                               apr_pool_t *pool)
{
  const char *data;
  apr_size_t len;
  serf_status_line sl;
  apr_status_t status;
  int xml_status;
  svn_ra_serf__xml_parser_t *ctx = baton;

  serf_bucket_response_status(response, &sl);

  if (ctx->status_code)
    {
      *ctx->status_code = sl.code;
    }

  /* Woo-hoo.  Nothing here to see.  */
  if (sl.code == 404 && ctx->ignore_errors == FALSE)
    {
      /* If our caller won't know about the 404, abort() for now. */
      if (!ctx->status_code)
        {
          abort();
        }
      if (*ctx->done == FALSE)
        {
          *ctx->done = TRUE;
          if (ctx->done_list)
            {
              ctx->done_item->data = ctx->user_data;
              ctx->done_item->next = *ctx->done_list;
              *ctx->done_list = ctx->done_item;
            }
        }
      return svn_ra_serf__handle_discard_body(request, response, NULL, pool);
    }

  if (!ctx->xmlp)
    {
      ctx->xmlp = XML_ParserCreate(NULL);
      XML_SetUserData(ctx->xmlp, ctx);
      XML_SetElementHandler(ctx->xmlp, start_xml, end_xml);
      if (ctx->cdata)
        {
          XML_SetCharacterDataHandler(ctx->xmlp, cdata_xml);
        }
    }

  while (1)
    {
      status = serf_bucket_read(response, 8000, &data, &len);

      if (SERF_BUCKET_READ_ERROR(status))
        {
          return status;
        }

      xml_status = XML_Parse(ctx->xmlp, data, len, 0);
      if (xml_status == XML_STATUS_ERROR && ctx->ignore_errors == FALSE)
        {
          XML_ParserFree(ctx->xmlp);

          if (!ctx->status_code)
            {
              abort();
            }
          if (*ctx->done == FALSE)
            {
              *ctx->done = TRUE;
              if (ctx->done_list)
                {
                  ctx->done_item->data = ctx->user_data;
                  ctx->done_item->next = *ctx->done_list;
                  *ctx->done_list = ctx->done_item;
                }
            }
          ctx->error = svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                         "XML parsing failed: (%d %s)",
                                         sl.code, sl.reason);
          return ctx->error->apr_err;
        }

      if (ctx->error && ctx->ignore_errors == FALSE)
        {
          XML_ParserFree(ctx->xmlp);
          status = ctx->error->apr_err;

          svn_error_clear(ctx->error);

          return status;
        }

      if (APR_STATUS_IS_EAGAIN(status))
        {
          return status;
        }

      if (APR_STATUS_IS_EOF(status))
        {
          xml_status = XML_Parse(ctx->xmlp, NULL, 0, 1);
          if (xml_status == XML_STATUS_ERROR && ctx->ignore_errors == FALSE)
            {
              abort();
            }

          XML_ParserFree(ctx->xmlp);

          *ctx->done = TRUE;
          if (ctx->done_list)
            {
              ctx->done_item->data = ctx->user_data;
              ctx->done_item->next = *ctx->done_list;
              *ctx->done_list = ctx->done_item;
            }
          return status;
        }

      /* feed me! */
    }
  /* not reached */
}

svn_error_t *
svn_ra_serf__handle_server_error(serf_request_t *request,
                                 serf_bucket_t *response,
                                 apr_pool_t *pool)
{
  svn_ra_serf__server_error_t server_err;
  apr_status_t status;

  memset(&server_err, 0, sizeof(server_err));
  status = svn_ra_serf__handle_discard_body(request, response,
                                            &server_err, pool);

  if (APR_STATUS_IS_EOF(status))
    {
      status = svn_ra_serf__is_conn_closing(response);
      if (status == SERF_ERROR_CLOSING)
        {
          serf_connection_reset(serf_request_get_conn(request));
        }
    }

  SVN_ERR(server_err.error);

  return svn_error_create(APR_EGENERAL, NULL, _("Unspecified error message"));
}

/* Implements the serf_response_handler_t interface.  Wait for HTTP
   response status and headers, and invoke CTX->response_handler() to
   carry out operation-specific processing.  Afterwards, check for
   connection close. */
static apr_status_t
handle_response(serf_request_t *request,
                serf_bucket_t *response,
                void *baton,
                apr_pool_t *pool)
{
  svn_ra_serf__handler_t *ctx = baton;
  serf_status_line sl;
  apr_status_t status;

  if (!response)
    {
      /* Uh-oh.  Our connection died.  Requeue. */
      if (ctx->response_error)
        {
          status = ctx->response_error(request, response, 0,
                                       ctx->response_error_baton);
          if (status)
            {
              return status;
            }
        }

      svn_ra_serf__request_create(ctx);

      return APR_SUCCESS;
    }

  status = serf_bucket_response_status(response, &sl);
  if (SERF_BUCKET_READ_ERROR(status))
    {
      return status;
    }

  status = serf_bucket_response_wait_for_headers(response);
  if (status)
    {
      if (!APR_STATUS_IS_EOF(status))
        {
          return status;
        }

      /* Cases where a lack of a response body (via EOF) is okay:
       *  - A HEAD request
       *  - 204/304 response
       *
       * Otherwise, if we get an EOF here, something went really wrong: either
       * the server closed on us early or we're reading too much.  Either way,
       * scream loudly.
       */
      if (strcmp(ctx->method, "HEAD") != 0 && sl.code != 204 && sl.code != 304)
        {
          ctx->session->pending_error =
              svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                               _("Premature EOF seen from server"));
          return ctx->session->pending_error->apr_err;
        }
    }

  if (ctx->conn->last_status_code == 401 && sl.code < 400)
    {
      svn_auth_save_credentials(ctx->session->auth_state, ctx->session->pool);
      ctx->session->auth_attempts = 0;
      ctx->session->auth_state = NULL;
    }

  ctx->conn->last_status_code = sl.code;

  if (sl.code == 401)
    {
      handle_auth(ctx->session, ctx->conn, request, response, pool);
      svn_ra_serf__request_create(ctx);
      status = svn_ra_serf__handle_discard_body(request, response, NULL, pool);
    }
  else if (sl.code >= 500)
    {
      ctx->session->pending_error =
          svn_ra_serf__handle_server_error(request, response, pool);
      return ctx->session->pending_error->apr_err;
    }
  else
    {
      status = ctx->response_handler(request, response, ctx->response_baton,
                                     pool);
    }

  if (APR_STATUS_IS_EOF(status))
    {
      status = svn_ra_serf__is_conn_closing(response);
    }

  return status;
}

/* Implements the serf_request_setup_t interface (which sets up both a
   request and its response handler callback).  If the CTX->delegate()
   callback is non-NULL, invoke it to carry out the majority of the
   serf_request_setup_t implementation.  Otherwise, perform default
   setup, with special handling for HEAD requests, and finer-grained
   callbacks invoked (if non-NULL) to produce the request headers and
   body. */
static apr_status_t
setup_request(serf_request_t *request,
              void *setup_baton,
              serf_bucket_t **req_bkt,
              serf_response_acceptor_t *acceptor,
              void **acceptor_baton,
              serf_response_handler_t *handler,
              void **handler_baton,
              apr_pool_t *pool)
{
  svn_ra_serf__handler_t *ctx = setup_baton;
  serf_bucket_t *headers_bkt;

  *acceptor = svn_ra_serf__accept_response;
  *acceptor_baton = ctx->session;

  if (ctx->delegate)
    {
      apr_status_t status;

      status = ctx->delegate(request, ctx->delegate_baton, req_bkt,
                             acceptor, acceptor_baton, handler, handler_baton,
                             pool);
      if (status)
        {
          return status;
        }

      ctx->response_handler = *handler;
      ctx->response_baton = *handler_baton;
    }
  else
    {
      if (strcmp(ctx->method, "HEAD") == 0)
        {
          *acceptor = accept_head;
        }
 
      if (ctx->body_delegate)
        {
          ctx->body_buckets =
              ctx->body_delegate(ctx->body_delegate_baton,
                                 serf_request_get_alloc(request),
                                 pool);
        }

      svn_ra_serf__setup_serf_req(request, req_bkt, &headers_bkt, ctx->conn,
                                  ctx->method, ctx->path,
                                  ctx->body_buckets, ctx->body_type);

      if (ctx->header_delegate)
        {
          ctx->header_delegate(headers_bkt, ctx->header_delegate_baton, pool);
        }
    }

  *handler = handle_response;
  *handler_baton = ctx;

  return APR_SUCCESS;
}

serf_request_t *
svn_ra_serf__request_create(svn_ra_serf__handler_t *handler)
{
  return serf_connection_request_create(handler->conn->conn,
                                        setup_request, handler);
}

svn_error_t *
svn_ra_serf__discover_root(const char **vcc_url,
                           const char **rel_path,
                           svn_ra_serf__session_t *session,
                           svn_ra_serf__connection_t *conn,
                           const char *orig_path,
                           apr_pool_t *pool)
{
  apr_hash_t *props;
  const char *path, *relative_path, *present_path = "";

  /* If we're only interested in our VCC, just return it. */
  if (session->vcc_url && !rel_path)
    {
      *vcc_url = session->vcc_url;
      return SVN_NO_ERROR;
    }

  props = apr_hash_make(pool);
  path = orig_path;
  *vcc_url = NULL;

  do
    {
      SVN_ERR(svn_ra_serf__retrieve_props(props, session, conn,
                                          path, SVN_INVALID_REVNUM,
                                          "0", base_props, pool));
      *vcc_url =
          svn_ra_serf__get_ver_prop(props, path,
                                    SVN_INVALID_REVNUM,
                                    "DAV:",
                                    "version-controlled-configuration");

      if (*vcc_url)
        {
          relative_path = svn_ra_serf__get_ver_prop(props, path,
                                                    SVN_INVALID_REVNUM,
                                                    SVN_DAV_PROP_NS_DAV,
                                                    "baseline-relative-path");
          break;
        }

      /* This happens when the file is missing in HEAD. */

      /* Okay, strip off. */
      present_path = svn_path_join(svn_path_basename(path, pool),
                                   present_path, pool);
      path = svn_path_dirname(path, pool);
    }
  while (!svn_path_is_empty(path));

  if (!*vcc_url)
    {
      abort();
    }

  /* Store our VCC in our cache. */
  if (!session->vcc_url)
    {
      session->vcc_url = apr_pstrdup(session->pool, *vcc_url);
    }

  /* Update our cached repository root URL. */
  if (!session->repos_root_str)
    {
      svn_stringbuf_t *url_buf;

      url_buf = svn_stringbuf_create(path, pool);

      svn_path_remove_components(url_buf,
                                 svn_path_component_count(relative_path));

      /* Now recreate the root_url. */
      session->repos_root = session->repos_url;
      session->repos_root.path = apr_pstrdup(session->pool, url_buf->data);
      session->repos_root_str = apr_uri_unparse(session->pool,
                                                &session->repos_root, 0);
    }

  if (rel_path)
    {
      if (present_path[0] != '\0')
        {
          *rel_path = svn_path_url_add_component(relative_path,
                                                 present_path, pool);
        }
      else
        {
          *rel_path = relative_path;
        }
    }

  return SVN_NO_ERROR;
}
