/*
 * listen.c : Management of incoming connections.
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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

#include <apr_tables.h>
#include <apr_network_io.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_private_config.h"

#include "server.h"

struct listener {
  apr_socket_t *sock;
  apr_sockaddr_t *sa;
};

/* Number of connections allowed to queue up between listen
 * and accept system calls. */
#define CONNECTION_BACKLOG 7

static apr_array_header_t *listeners;

svn_error_t* init_listeners(apr_array_header_t *addresses,
                            apr_pool_t *pool)
{
  apr_status_t status;
  int i;
  apr_pool_t *iterpool;
  int family = AF_INET;
  apr_socket_t *sock;

  /* If no addresses were specified, error out. */
  SVN_ERR_ASSERT(addresses->nelts > 0);

  iterpool = svn_pool_create(pool);
  listeners = apr_array_make(pool, 1, sizeof(struct listener));

#if APR_HAVE_IPV6
  /* Make sure we have IPV6 support first before giving apr_sockaddr_info_get
     APR_UNSPEC, because it may give us back an IPV6 address even if we can't
     create IPV6 sockets. */
#ifdef MAX_SECS_TO_LINGER
  /* ### old APR interface */
  status = apr_socket_create(&sock, APR_INET6, SOCK_STREAM, pool);
#else
  status = apr_socket_create(&sock, APR_INET6, SOCK_STREAM, APR_PROTO_TCP,
                             pool);
#endif
  if (status == APR_SUCCESS)
    {
      apr_socket_close(sock);
      family = APR_UNSPEC;
    }
#endif

  for (i = 0; i < addresses->nelts; i++)
    {
      const char *host;
      char *addr, *scope_id;
      apr_port_t port;
      apr_sockaddr_t *sa;
      struct listener *listener;
      
      svn_pool_clear(iterpool);

      host = APR_ARRAY_IDX(addresses, i, const char*);
      status = apr_parse_addr_port(&addr, &scope_id, &port, host, iterpool);
      if (status)
        return svn_error_wrap_apr(status, _("Cannot parse address '%s'"), host);

      if (addr == NULL)
        return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                   _("Cannot parse address '%s'"), host);
      if (port == 0)
        port = SVN_RA_SVN_PORT;

      status = apr_sockaddr_info_get(&sa, addr, family, port, 0, pool);
      if (status)
        return svn_error_wrap_apr(status, _("Can't get address info"));

      /* Process all addresses returned by apr_sockaddr_info_get()
       * and create a listener for each. */
      while (sa)
        {
          int sock_family;

#if APR_HAVE_IPV6
          /* Make sure we don't try to bind sockaddrs to sockets
           * with mismatching address families. */
          if (sa->family == AF_INET6)
              sock_family = APR_INET6;
          else
#endif
            sock_family = APR_INET;

#ifdef MAX_SECS_TO_LINGER
          /* ### old APR interface */
          status = apr_socket_create(&sock, sock_family, SOCK_STREAM, pool);
#else
          status = apr_socket_create(&sock, sock_family, SOCK_STREAM,
                                     APR_PROTO_TCP, pool);
#endif
          if (status)
            return svn_error_wrap_apr(status, _("Can't create server socket"));

          /* Prevents "socket in use" errors when server is killed and quickly
           * restarted. */
          apr_socket_opt_set(sock, APR_SO_REUSEADDR, 1);

          status = apr_socket_bind(sock, sa);
          if (status)
            return svn_error_wrap_apr(status, _("Can't bind server socket"));

          /* Set up the listener. */
          listener = apr_palloc(pool, sizeof(struct listener));
          listener->sock = sock;
          listener->sa = sa;
          APR_ARRAY_PUSH(listeners, struct listener *) = listener;

          sa = sa->next;
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
wait_for_client(apr_socket_t **usock, apr_pool_t *pool)
{
  struct listener *listener;
  apr_status_t status;

  /* If we have no listener yet, error out. */
  SVN_ERR_ASSERT(listeners->nelts > 0);

  /* If we have only one listener, we can let apr_socket_listen() do our job. */
  if (listeners->nelts == 1)
    {
      listener = APR_ARRAY_IDX(listeners, 0, struct listener *);
      status = apr_socket_listen(listener->sock, CONNECTION_BACKLOG);
      if (status)
        return svn_error_wrap_apr(status, _("Cannot listen on socket"));
      status = apr_socket_accept(*usock, listener->sock, pool);
      if (status)
        return svn_error_wrap_apr(status, _("Cannot accept connection"));
      return SVN_NO_ERROR; 
    }

  /* TODO: multiple addresses */
  SVN_ERR_ASSERT(FALSE);

  return SVN_NO_ERROR; 
}
