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

#include <serf.h>
#include <serf_bucket_types.h>

#include "ra_serf.h"


serf_bucket_t *
conn_setup(apr_socket_t *sock,
           void *baton,
           apr_pool_t *pool)
{
  serf_bucket_t *bucket;
  ra_serf_connection_t *conn = baton;

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
accept_response(serf_request_t *request,
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

void
conn_closed(serf_connection_t *conn,
            void *closed_baton,
            apr_status_t why,
            apr_pool_t *pool)
{
  ra_serf_connection_t *our_conn = closed_baton;

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
cleanup_serf_session(void *data)
{
  ra_serf_session_t *serf_sess = data;
  int i;

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
setup_serf_req(serf_request_t *request,
               serf_bucket_t **req_bkt, serf_bucket_t **ret_hdrs_bkt,
               ra_serf_connection_t *conn,
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
context_run_wait(svn_boolean_t *done,
                 ra_serf_session_t *sess,
                 apr_pool_t *pool)
{
  apr_status_t status;

  while (!*done)
    {
      status = serf_context_run(sess->context, SERF_DURATION_FOREVER, pool);
      if (APR_STATUS_IS_TIMEUP(status))
        {
          continue;
        }
      if (status)
        {
          return svn_error_wrap_apr(status, "Error running context");
        }
      /* Debugging purposes only! */
      serf_debug__closed_conn(sess->bkt_alloc);
    }

  return SVN_NO_ERROR;
}

apr_status_t
is_conn_closing(serf_bucket_t *response)
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

SERF_DECLARE(apr_status_t)
handler_discard_body(serf_request_t *request,
                     serf_bucket_t *response,
                     void *baton,
                     apr_pool_t *pool)
{
  apr_status_t status;

  /* Just loop through and discard the body. */
  while (1)
    {
      const char *data;
      apr_size_t len;
      apr_status_t status;

      status = serf_bucket_read(response, SERF_READ_ALL_AVAIL, &data, &len);

      if (status)
        {
          return status;
        }

      /* feed me */
    }
}

apr_status_t
handle_auth(ra_serf_session_t *session,
            ra_serf_connection_t *conn,
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
      char *cur, *last, *auth_hdr, *realm_name, *realmstring;

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

      session->realm = apr_psprintf(session->pool, "<%s://%s> %s",
                                    session->repos_url.scheme,
                                    session->repos_url.hostinfo,
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

apr_status_t
handle_xml_parser(serf_request_t *request,
                  serf_bucket_t *response,
                  void *baton,
                  apr_pool_t *pool)
{
  const char *data;
  apr_size_t len;
  serf_status_line sl;
  apr_status_t status;
  enum XML_Status xml_status;
  ra_serf_xml_parser_t *ctx = baton;

  if (!ctx->xmlp)
    {
      ctx->xmlp = XML_ParserCreate(NULL);
      XML_SetUserData(ctx->xmlp, ctx->user_data);
      XML_SetElementHandler(ctx->xmlp, ctx->start, ctx->end);
      XML_SetCharacterDataHandler(ctx->xmlp, ctx->cdata);
    }

  if (ctx->status_code)
    {
      serf_status_line sl;
      serf_bucket_response_status(response, &sl);
      *ctx->status_code = sl.code;
    }

  while (1)
    {
      status = serf_bucket_read(response, 8000, &data, &len);

      if (SERF_BUCKET_READ_ERROR(status))
        {
          return status;
        }

      xml_status = XML_Parse(ctx->xmlp, data, len, 0);
      if (xml_status == XML_STATUS_ERROR)
        {
          abort();
        }

      if (APR_STATUS_IS_EAGAIN(status))
        {
          return status;
        }

      if (APR_STATUS_IS_EOF(status))
        {
          xml_status = XML_Parse(ctx->xmlp, NULL, 0, 1);
          if (xml_status == XML_STATUS_ERROR)
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

apr_status_t
handler_default(serf_request_t *request,
                serf_bucket_t *response,
                void *baton,
                apr_pool_t *pool)
{
  ra_serf_handler_t *ctx = baton;
  serf_bucket_t *headers;
  serf_status_line sl;
  apr_status_t status;

  /* Uh-oh.  Our connection died.  Requeue. */
  if (!response)
    {
      if (ctx->response_error)
        {
          status = ctx->response_error(request, response, 0,
                                       ctx->response_error_baton);
          if (status)
            {
              return status;
            }
        }

      ra_serf_request_create(ctx);

      return APR_SUCCESS;
    }

  status = serf_bucket_response_wait_for_headers(response);
  if (status)
    {
      return status;
    }

  status = serf_bucket_response_status(response, &sl);
  if (status)
    {
      return status;
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
      ra_serf_request_create(ctx);
      status = handler_discard_body(request, response, NULL, pool);
    }
  else
    {
      headers = serf_bucket_response_get_headers(response);
      status = ctx->response_handler(request, response, ctx->response_baton,
                                     pool);
    }

  if (APR_STATUS_IS_EOF(status)) {
      status = is_conn_closing(response);
  }

  return status;

}

static apr_status_t
setup_default(serf_request_t *request,
              void *setup_baton,
              serf_bucket_t **req_bkt,
              serf_response_acceptor_t *acceptor,
              void **acceptor_baton,
              serf_response_handler_t *handler,
              void **handler_baton,
              apr_pool_t *pool)
{
  ra_serf_handler_t *ctx = setup_baton;
  serf_bucket_t *body_bkt, *headers_bkt;

  *acceptor = accept_response;
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
      if (ctx->body_delegate)
        {
          ctx->body_buckets =
              ctx->body_delegate(ctx->body_delegate_baton,
                                 serf_request_get_alloc(request),
                                 pool);
        }

      setup_serf_req(request, req_bkt, &headers_bkt, ctx->conn,
                     ctx->method, ctx->path, ctx->body_buckets, ctx->body_type);

      if (ctx->header_delegate)
        {
          ctx->header_delegate(headers_bkt, ctx->header_delegate_baton, pool);
        }
    }

  *handler = handler_default;
  *handler_baton = ctx;

  return APR_SUCCESS;
}

serf_request_t*
ra_serf_request_create(ra_serf_handler_t *handler)
{
  return serf_connection_request_create(handler->conn->conn,
                                        setup_default, handler);
}
