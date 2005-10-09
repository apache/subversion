/*
 * streams.c :  Stream encapsulation routines for Subversion protocol
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

typedef struct {
  apr_file_t *file;
  apr_pool_t *pool;
} file_conn_t;

static void file_timeout_cb(void *baton,
                            apr_interval_time_t interval)
{
  file_conn_t *conn = baton;
  apr_file_pipe_timeout_set(conn->file, interval);
}

static svn_boolean_t file_data_pending_cb(void *baton)
{
  file_conn_t *conn = baton;
  apr_pollfd_t pfd;
  int n;

  pfd.desc_type = APR_POLL_FILE;
  pfd.desc.f = conn->file;
  pfd.p = conn->pool;
  pfd.reqevents = APR_POLLIN;
  return ((apr_poll(&pfd, 1, &n, 0) == APR_SUCCESS) && n);
}

static svn_error_t *file_read_cb(void *baton,
                                 char *buffer,
                                 apr_size_t *len)
{
  file_conn_t *conn = baton;
  apr_status_t status = apr_file_read(conn->file, buffer, len);
  if (status && !APR_STATUS_IS_EOF(status))
    return svn_error_wrap_apr(status, _("Can't read from connection"));
  if (*len == 0)
    return svn_error_create(SVN_ERR_RA_SVN_CONNECTION_CLOSED, NULL,
                            _("Connection closed unexpectedly"));
  return SVN_NO_ERROR;
}

static svn_error_t *file_write_cb(void *baton,
                                  const char *buffer,
                                  apr_size_t *len)
{
  file_conn_t *conn = baton;
  apr_status_t status = apr_file_write(conn->file, buffer, len);
  if (status)
    return svn_error_wrap_apr(status, _("Can't write to connection"));
  return SVN_NO_ERROR;
}

void svn_ra_svn__file_streams(apr_file_t *in_file, apr_file_t *out_file,
                              svn_stream_t **in, svn_stream_t **out,
                              apr_pool_t *pool)
{
  file_conn_t *in_file_conn = apr_palloc(pool, sizeof(*in_file_conn));
  file_conn_t *out_file_conn = apr_palloc(pool, sizeof(*out_file_conn));

  in_file_conn->file = in_file;
  in_file_conn->pool = pool;

  out_file_conn->file = out_file;
  out_file_conn->pool = pool;

  *in = svn_stream_empty(pool);
  *out = svn_stream_empty(pool);

  svn_stream_set_baton(*in, in_file_conn);
  svn_stream_set_read(*in, file_read_cb);
  svn_stream_set_timeout(*in, file_timeout_cb);
  svn_stream_set_data_pending(*in, file_data_pending_cb);

  svn_stream_set_baton(*out, out_file_conn);
  svn_stream_set_write(*out, file_write_cb);
  svn_stream_set_timeout(*out, file_timeout_cb);
}

typedef struct {
  apr_socket_t *sock;
  apr_pool_t *pool;
} sock_conn_t;

static void sock_timeout_cb(void *baton,
                            apr_interval_time_t interval)
{
  sock_conn_t *conn = baton;
  apr_socket_timeout_set(conn->sock, interval);
}

static svn_boolean_t sock_data_pending_cb(void *baton)
{
  sock_conn_t *conn = baton;
  apr_pollfd_t pfd;
  int n;

  pfd.desc_type = APR_POLL_SOCKET;
  pfd.desc.s = conn->sock;
  pfd.p = conn->pool;
  pfd.reqevents = APR_POLLIN;
  return ((apr_poll(&pfd, 1, &n, 0) == APR_SUCCESS) && n);
}

static svn_error_t *sock_read_cb(void *baton,
                                 char *buffer,
                                 apr_size_t *len)
{
  sock_conn_t *conn = baton;
  apr_status_t status;
  apr_interval_time_t interval; 
  
  status = apr_socket_timeout_get(conn->sock, &interval);
  if (status)
    return svn_error_wrap_apr(status, _("Can't get socket timeout"));

  /* Always block on read. */
  apr_socket_timeout_set(conn->sock, -1);
  status = apr_socket_recv(conn->sock, buffer, len);
  apr_socket_timeout_set(conn->sock, interval);
  if (status && !APR_STATUS_IS_EOF(status))
    return svn_error_wrap_apr(status, _("Can't read from connection"));
  if (*len == 0)
    return svn_error_create(SVN_ERR_RA_SVN_CONNECTION_CLOSED, NULL,
                            _("Connection closed unexpectedly"));
  return SVN_NO_ERROR;
}

static svn_error_t *sock_write_cb(void *baton,
                                  const char *buffer,
                                  apr_size_t *len)
{
  sock_conn_t *conn = baton;
  apr_status_t status = apr_socket_send(conn->sock, buffer, len);
  if (status)
    return svn_error_wrap_apr(status, _("Can't write to connection"));
  return SVN_NO_ERROR;
}

void svn_ra_svn__sock_streams(apr_socket_t *sock, svn_stream_t **in,
                              svn_stream_t **out, apr_pool_t *pool)
{
  sock_conn_t *sock_conn = apr_palloc(pool, sizeof(*sock_conn));
  sock_conn->sock = sock;
  sock_conn->pool = pool;

  *out = *in = svn_stream_empty(pool);

  svn_stream_set_baton(*in, sock_conn);
  svn_stream_set_read(*in, sock_read_cb);
  svn_stream_set_write(*in, sock_write_cb);
  svn_stream_set_timeout(*in, sock_timeout_cb);
  svn_stream_set_data_pending(*in, sock_data_pending_cb);
}
