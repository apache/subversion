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
  serf_session_t *sess = baton;

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
conn_closed (serf_connection_t *conn,
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
  serf_session_t *serf_sess = data;
  if (serf_sess->conn)
    {
      serf_connection_close(serf_sess->conn);
      serf_sess->conn = NULL;
    }
  return APR_SUCCESS;
}
