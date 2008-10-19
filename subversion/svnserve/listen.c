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

struct parsed_address {
  const char *host;
  apr_port_t port;
};

/* Number of connections allowed to queue up between listen
 * and accept system calls. */
#define CONNECTION_BACKLOG 7

/* Parse each address in ADDRESSES, and return an array with
 * elements of type struct parsed_address in *PARSED_ADDRESSES.
 *
 * Do all allocations in POOL.
 */
static svn_error_t*
parse_addresses(apr_array_header_t **parsed_addresses,
                apr_array_header_t *addresses,
                apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(pool);
  *parsed_addresses = apr_array_make(pool, 1, sizeof(struct parsed_address *));

  for (i = 0; i < addresses->nelts; i++)
    {
      const char *address;
      char *host, *scope_id;
      apr_port_t port;
      struct parsed_address *parsed_address;
      apr_status_t status;

      svn_pool_clear(iterpool);

      address = APR_ARRAY_IDX(addresses, i, const char*);
      status = apr_parse_addr_port(&host, &scope_id, &port, address, iterpool);
      if (status)
        return svn_error_wrap_apr(status,
                                  _("Cannot parse address '%s'"), address);

      if (port == 0) /* unspecified */
        port = SVN_RA_SVN_PORT;

      if (host == NULL)
        {
          /* Looks like only a port was specified, which is legal from
           * apr_parse_addr_port()'s point of view. Fall back to the
           * unspecified address in all available address families. */
#if APR_HAVE_IPV6
          parsed_address = apr_palloc(pool, sizeof(struct parsed_address));
          parsed_address->host = "::"; /* unspecified */
          parsed_address->port = port;
          APR_ARRAY_PUSH(*parsed_addresses, struct parsed_address *)
            = parsed_address;
#endif
          host = (char *)APR_ANYADDR;
          /* fall through */
        }

      parsed_address = apr_palloc(pool, sizeof(struct parsed_address));
      parsed_address->host = host;
      parsed_address->port = port;
      APR_ARRAY_PUSH(*parsed_addresses, struct parsed_address *)
        = parsed_address;
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t* init_listeners(apr_array_header_t **listeners,
                            apr_array_header_t *addresses,
                            apr_pool_t *pool)
{
  apr_status_t status;
  int i;
  int family = AF_INET;
  apr_socket_t *sock;
  apr_array_header_t *parsed_addresses;
  apr_pool_t *parse_pool;
  apr_array_header_t *new_listeners;

  /* If no addresses were specified, error out. */
  SVN_ERR_ASSERT(addresses->nelts > 0);

  *listeners = NULL;
  new_listeners = apr_array_make(pool, 1, sizeof(struct listener));

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

  parse_pool = svn_pool_create(pool);
  SVN_ERR(parse_addresses(&parsed_addresses, addresses, parse_pool));
  SVN_ERR_ASSERT(parsed_addresses->nelts > 0);

  /* Set up listeners */
  for (i = 0; i < parsed_addresses->nelts; i++)
    {
      struct parsed_address *parsed_address;
      apr_sockaddr_t *sa;
      struct listener *listener;

      parsed_address = APR_ARRAY_IDX(parsed_addresses, i,
                                     struct parsed_address *);
      status = apr_sockaddr_info_get(&sa, parsed_address->host, family,
                                     parsed_address->port, 0, pool);
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
          APR_ARRAY_PUSH(new_listeners, struct listener *) = listener;

          sa = sa->next;
        }
    }

  *listeners = new_listeners;

  svn_pool_destroy(parse_pool);
  return SVN_NO_ERROR;
}

svn_error_t *
wait_for_client(apr_socket_t **usock,
                apr_array_header_t *listeners,
                apr_pool_t *pool)
{
  struct listener *listener;
  apr_status_t status;

  /* If we have no listener yet, error out. */
  SVN_ERR_ASSERT(listeners->nelts > 0);

  /* Straightforward case: If we have only one listener,
   * we do not need to poll across multiple sockets. */
  if (listeners->nelts == 1)
    {
      listener = APR_ARRAY_IDX(listeners, 0, struct listener *);
      status = apr_socket_listen(listener->sock, CONNECTION_BACKLOG);
      if (status)
        return svn_error_wrap_apr(status, _("Cannot listen on socket"));
      status = apr_socket_accept(usock, listener->sock, pool);
      if (status)
        return svn_error_wrap_apr(status, _("Cannot accept connection"));
      return SVN_NO_ERROR; 
    }

  /* TODO: multiple addresses */
  SVN_ERR_ASSERT(FALSE);

  return SVN_NO_ERROR; 
}
