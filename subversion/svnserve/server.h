/*
 * svn_server.h :  declarations for the svn server
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



#ifndef SERVER_H
#define SERVER_H

#include <apr_network_io.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct serve_params_t {
  /* The virtual root of the repositories to serve.  The client URL
     path is interpreted relative to this root and is not allowed to
     escape it. */
  const char *root;

  /* True if the connection is tunneled over an ssh-like transport,
     such that the client may use EXTERNAL to authenticate as the
     current uid's username. */
  svn_boolean_t tunnel;

  /* If tunnel is true, overrides the current uid's username as the
     identity EXTERNAL authenticates as. */
  const char *tunnel_user;

  /* True if the deprecated read-only flag was specified on the
     command-line, which forces all connections to be read-only. */
  svn_boolean_t read_only;
} serve_params_t;

/* Serve the connection CONN according to the parameters PARAMS. */
svn_error_t *serve(svn_ra_svn_conn_t *conn, serve_params_t *params,
                   apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SERVER_H */
