/*
 * streams.c :  Stream encapsulation routines for Subversion protocol
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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



#include <assert.h>
#include <stdlib.h>

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_lib.h>
#include <apr_strings.h>
#include <apr_network_io.h>
#include <apr_poll.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_io.h"
#include "svn_private_config.h"

#include "ra_svn.h"

struct svn_ra_svn_stream {
  svn_stream_t *stream;
  void *baton;
  pending_fn_t pending_fn;
  timeout_fn_t timeout_fn;
};

typedef struct {
  apr_descriptor d;
  apr_pool_t *pool;
} baton_t;

/* Return whether or not there is data available for reading
   descriptor DESC of type TYPE, using POOL for any necessary
   allocations */
static svn_boolean_t
pending(apr_descriptor *desc, apr_datatype_e type, apr_pool_t *pool)
{
  apr_pollfd_t pfd;
  apr_status_t status;
  int n;

  pfd.desc_type = type;
  pfd.desc = *desc;
  pfd.p = pool;
  pfd.reqevents = APR_POLLIN;
#ifdef AS400
  status = apr_poll(&pfd, 1, &n, 0, pool);
#else
  status = apr_poll(&pfd, 1, &n, 0);
#endif
  return (status == APR_SUCCESS && n);
}

static svn_error_t *
file_read_cb(void *baton, char *buffer, apr_size_t *len)
{
  baton_t *b = baton;
  apr_status_t status = apr_file_read(b->d.f, buffer, len);
  if (status && !APR_STATUS_IS_EOF(status))
    return svn_error_wrap_apr(status, _("Can't read from connection"));
  if (*len == 0)
    return svn_error_create(SVN_ERR_RA_SVN_CONNECTION_CLOSED, NULL,
                            _("Connection closed unexpectedly"));
  return SVN_NO_ERROR;
}


static svn_error_t *
file_write_cb(void *baton, const char *buffer, apr_size_t *len)
{
  baton_t *b = baton;
  apr_status_t status = apr_file_write(b->d.f, buffer, len);
  if (status)
    return svn_error_wrap_apr(status, _("Can't write to connection"));
  return SVN_NO_ERROR;
}


static void
file_timeout_cb(void *baton, apr_interval_time_t interval)
{
  baton_t *b = baton;
  apr_file_pipe_timeout_set(b->d.f, interval);
}

static svn_boolean_t
file_pending_cb(void *baton)
{
  baton_t *b = baton;
  return pending(&b->d, APR_POLL_FILE, b->pool);
}


void
svn_ra_svn__stream_pair_from_files(apr_file_t *in_file, apr_file_t *out_file,
                                   svn_ra_svn_stream_t **in_stream,
                                   svn_ra_svn_stream_t **out_stream,
                                   apr_pool_t *pool)
{
  baton_t *inb = apr_palloc(pool, sizeof(*inb));
  baton_t *outb = apr_palloc(pool, sizeof(*outb));

  inb->d.f = in_file;
  inb->pool = pool;

  outb->d.f = out_file;
  outb->pool = pool;

  *in_stream = svn_ra_svn__stream_create(inb, file_read_cb, NULL,
                                         file_timeout_cb, file_pending_cb,
                                         pool);
  *out_stream = svn_ra_svn__stream_create(outb, NULL, file_write_cb,
                                         file_timeout_cb, file_pending_cb,
                                         pool);
}


static svn_error_t *
sock_read_cb(void *baton, char *buffer, apr_size_t *len)
{
  baton_t *sb = baton;
  apr_status_t status;
  apr_socket_t *sock = sb->d.s;
  
  /* Always block on read. */
  apr_socket_timeout_set(sock, -1);
  status = apr_socket_recv(sock, buffer, len);
  apr_socket_timeout_set(sock, 0);
  if (status && !APR_STATUS_IS_EOF(status))
    return svn_error_wrap_apr(status, _("Can't read from connection"));
  if (*len == 0)
    return svn_error_create(SVN_ERR_RA_SVN_CONNECTION_CLOSED, NULL,
                            _("Connection closed unexpectedly"));
  return SVN_NO_ERROR;
}


static svn_error_t *
sock_write_cb(void *baton, const char *buffer, apr_size_t *len)
{
  baton_t *sb = baton;
  apr_status_t status = apr_socket_send(sb->d.s, buffer, len);
  if (status)
    return svn_error_wrap_apr(status, _("Can't write to connection"));
  return SVN_NO_ERROR;
}


static void
sock_timeout_cb(void *baton, apr_interval_time_t interval)
{
  baton_t *b = baton;
  apr_socket_timeout_set(b->d.s, interval);
}


static svn_boolean_t
sock_pending_cb(void *baton)
{
  baton_t *b = baton;
  return pending(&b->d, APR_POLL_FILE, b->pool);
}


void
svn_ra_svn__stream_pair_from_sock(apr_socket_t *sock,
                                  svn_ra_svn_stream_t **in_stream,
                                  svn_ra_svn_stream_t **out_stream,
                                  apr_pool_t *pool)
{
  baton_t *sb = apr_palloc(pool, sizeof(*sb));

  sb->d.s = sock;
  sb->pool = pool;

  *in_stream = svn_ra_svn__stream_create(sb, sock_read_cb, sock_write_cb,
                                         sock_timeout_cb, sock_pending_cb,
                                         pool);
  *out_stream = *in_stream;
}

svn_ra_svn_stream_t *
svn_ra_svn__stream_create(void *baton, svn_read_fn_t read_cb,
                          svn_write_fn_t write_cb, timeout_fn_t timeout_cb,
                          pending_fn_t pending_cb, apr_pool_t *pool)
{
  svn_ra_svn_stream_t *s = apr_palloc(pool, sizeof(*s));
  s->stream = svn_stream_empty(pool);
  svn_stream_set_baton(s->stream, baton);
  if (read_cb)
    svn_stream_set_read(s->stream, read_cb);
  if (write_cb)
    svn_stream_set_write(s->stream, write_cb);
  s->baton = baton;
  s->timeout_fn = timeout_cb;
  s->pending_fn = pending_cb;
  return s;
}

svn_error_t *
svn_ra_svn__stream_write(svn_ra_svn_stream_t *stream,
                         const char *data, apr_size_t *len)
{
  return svn_stream_write(stream->stream, data, len);
}


svn_error_t *
svn_ra_svn__stream_read(svn_ra_svn_stream_t *stream, char *data,
                        apr_size_t *len)
{
  return svn_stream_read(stream->stream, data, len);
}


void
svn_ra_svn__stream_timeout(svn_ra_svn_stream_t *stream,
                           apr_interval_time_t interval)
{
  stream->timeout_fn(stream->baton, interval);  
}


svn_boolean_t
svn_ra_svn__stream_pending(svn_ra_svn_stream_t *stream)
{
  return stream->pending_fn(stream->baton);  
}
