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



#include <serf.h>

#include "ra_serf.h"


serf_bucket_t *
conn_setup(apr_socket_t *sock,
           void *baton,
           apr_pool_t *pool)
{
  serf_bucket_t *bucket;
  ra_serf_session_t *sess = baton;

  bucket = serf_bucket_socket_create(sock, sess->bkt_alloc);
  if (sess->using_ssl)
    {
      bucket = serf_bucket_ssl_decrypt_create(bucket, sess->ssl_context,
                                              sess->bkt_alloc);
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
  if (why)
    {
      abort();
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
          serf_connection_close(serf_sess->conns[i]);
          serf_sess->conns[i] = NULL;
        }
    }
  return APR_SUCCESS;
}

void
setup_serf_req(serf_request_t *request,
               serf_bucket_t **req_bkt, serf_bucket_t **ret_hdrs_bkt,
               ra_serf_session_t *session,
               const char *method, const char *url,
               serf_bucket_t *body_bkt, const char *content_type)
{
  serf_bucket_t *hdrs_bkt;

  *req_bkt = serf_bucket_request_create(method, url, body_bkt,
                                        serf_request_get_alloc(request));

  hdrs_bkt = serf_bucket_request_get_headers(*req_bkt);
  serf_bucket_headers_setn(hdrs_bkt, "Host", session->repos_url.hostinfo);
  serf_bucket_headers_setn(hdrs_bkt, "User-Agent", "svn/ra_serf");
  if (content_type)
    {
      serf_bucket_headers_setn(hdrs_bkt, "Content-Type", content_type);
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

apr_status_t
handle_status_xml_parser(serf_bucket_t *response,
                         int *status_code,
                         XML_Parser xmlp,
                         svn_boolean_t *done,
                         apr_pool_t *pool)
{
  const char *data;
  apr_size_t len;
  serf_status_line sl;
  apr_status_t status;
  enum XML_Status xml_status;

  status = serf_bucket_response_status(response, &sl);
  if (status)
    {
      if (APR_STATUS_IS_EAGAIN(status))
        {
          return APR_SUCCESS;
        }
      abort();
    }

  if (status_code)
    {
      *status_code = sl.code;
    }

  while (1)
    {
      status = serf_bucket_read(response, 8000, &data, &len);

      if (SERF_BUCKET_READ_ERROR(status))
        {
          return status;
        }

      if (xmlp)
        {
          xml_status = XML_Parse(xmlp, data, len, 0);
          if (xml_status == XML_STATUS_ERROR)
            {
              abort();
            }
        }

      if (APR_STATUS_IS_EOF(status))
        {
          if (xmlp)
            {
              xml_status = XML_Parse(xmlp, NULL, 0, 1);
              if (xml_status == XML_STATUS_ERROR)
                {
                  abort();
                }
            }

          *done = TRUE;
          return is_conn_closing(response);
        }
      if (APR_STATUS_IS_EAGAIN(status))
        {
          return APR_SUCCESS;
        }

      /* feed me! */
    }
  /* not reached */
}

apr_status_t
handle_xml_parser(serf_bucket_t *response,
                  XML_Parser xmlp,
                  svn_boolean_t *done,
                  apr_pool_t *pool)
{
  return handle_status_xml_parser(response, NULL, xmlp, done, pool);
}
apr_status_t
handle_status_only(serf_bucket_t *response,
                   int *status_code,
                   svn_boolean_t *done,
                   apr_pool_t *pool)
{
  return handle_status_xml_parser(response, status_code, NULL, done, pool);
}
