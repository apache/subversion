/*
 * ra_svn_ssl.h :  private SSL declarations for the ra_svn module
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



#ifndef RA_SVN__SSL_H
#define RA_SVN_SSL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "svn_auth.h"
#include "ra_svn.h"

typedef struct ssl_conn ssl_conn_t;

/* Setup the svn_stream_t members of conn to use the SSL callbacks.
 * Creates and initializes an SSL that is to be used for this connection.
 * Internally, a BIO pair will be used to transfer data to/from Subversion
 * and the network.
 *
 * Subversion  |   TLS-engine
 *    |        |
 *    +----------> SSL_operations()
 *             |     /\    ||
 *             |     ||    \/
 *             |   BIO-pair (internal_bio)
 *    +----------< BIO-pair (network_bio)
 *    |        |
 *  socket     |
 */
svn_error_t *svn_ra_svn__setup_ssl_conn(svn_ra_svn_conn_t *conn,
                                        void *ssl_ctx,
                                        ssl_conn_t **ssl_conn,
                                        apr_pool_t *pool);

/* Fill in the server certificate information, as well as check for some
 * errors in the certificate. */
svn_error_t *svn_ra_svn__fill_server_cert_info(
                                   ssl_conn_t *ssl_conn,
                                   apr_pool_t *pool,
                                   const char *hostname,
                                   svn_auth_ssl_server_cert_info_t *cert_info,
                                   apr_uint32_t *cert_failures);


/* Do a SSL connect on the underlying socket connection. */
svn_error_t *svn_ra_svn__ssl_connect(ssl_conn_t *ssl_conn, apr_pool_t *pool);

/* Initializes the SSL context to be used by the client. */
svn_error_t *svn_ra_svn__init_ssl_ctx(void **ssl_ctx, apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* RA_SVN_SSL_H */
