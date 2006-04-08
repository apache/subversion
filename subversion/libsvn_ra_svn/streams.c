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

typedef struct {
  apr_descriptor d;
  apr_pool_t *pool;
} baton_t;

/* set PENDING to indicate whether or not there is data available for
   reading descriptor DESC of type TYPE, using POOL for any necessary
   allocations */
static void
pending(apr_descriptor *desc, apr_datatype_e type, svn_boolean_t *pending,
        apr_pool_t *pool)
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
  *pending = (status == APR_SUCCESS && n);
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


static svn_error_t *
file_ioctl_cb(void *baton, int cmd, void *arg)
{
  baton_t *b = baton;
  if (cmd == SVN_RA_SVN__IOCTL_TIMEOUT)
    apr_file_pipe_timeout_set(b->d.f, *(apr_interval_time_t *)arg);
  else /* must be SVN_RA_SVN__IOCTL_PENDING */
    pending(&b->d, APR_POLL_FILE, arg, b->pool);
  return SVN_NO_ERROR;
}


void
svn_ra_svn__stream_pair_from_files(apr_file_t *in_file, apr_file_t *out_file,
                                   svn_stream_t **in, svn_stream_t **out,
                                   apr_pool_t *pool)
{
  baton_t *inb = apr_palloc(pool, sizeof(*inb));
  baton_t *outb = apr_palloc(pool, sizeof(*outb));

  inb->d.f = in_file;
  inb->pool = pool;

  outb->d.f = out_file;
  outb->pool = pool;

  *in = svn_stream_empty(pool);
  *out = svn_stream_empty(pool);

  svn_stream_set_baton(*in, inb);
  svn_stream_set_read(*in, file_read_cb);
  svn_stream_set_ioctl(*in, file_ioctl_cb);

  svn_stream_set_baton(*out, outb);
  svn_stream_set_write(*out, file_write_cb);
  svn_stream_set_ioctl(*out, file_ioctl_cb);
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


static svn_error_t *
sock_ioctl_cb(void *baton, int cmd, void *arg)
{
  baton_t *sb = baton;
  if (cmd == SVN_RA_SVN__IOCTL_TIMEOUT)
    apr_socket_timeout_set(sb->d.s, *(apr_interval_time_t *)arg);
  else /* must be SVN_RA_SVN__IOCTL_PENDING */
    pending(&sb->d, APR_POLL_SOCKET, arg, sb->pool);
  return SVN_NO_ERROR;
}


void
svn_ra_svn__stream_pair_from_sock(apr_socket_t *sock, svn_stream_t **in,
                                  svn_stream_t **out, apr_pool_t *pool)
{
  baton_t *sb = apr_palloc(pool, sizeof(*sb));
  sb->d.s = sock;
  sb->pool = pool;

  *out = *in = svn_stream_empty(pool);

  svn_stream_set_baton(*in, sb);
  svn_stream_set_read(*in, sock_read_cb);
  svn_stream_set_write(*in, sock_write_cb);
  svn_stream_set_ioctl(*in, sock_ioctl_cb);
}
