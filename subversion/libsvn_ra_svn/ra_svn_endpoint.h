/*
 * svn_transceiver.h :  declarations shared between the ra_svn and
 * svnserve modules.
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



#ifndef RA_SVN_ENDPOINT_H
#define RA_SVN_ENDPOINT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** 
 *
 * Perform the server-side steps of the SSL session handshake
 * on @a conn, using the certificate at @a cert and the key
 * in @a key.  Use @a pool for any allocation.
 *
 * @since New in 1.4.
 */
svn_error_t *svn_ra_svn__conn_ssl_server(svn_ra_svn_conn_t *conn,
                                         const char *cert, const char *key,
                                         apr_pool_t *pool);


/**
 *
 * Initialize the OpenSSL library.
 *
 * @since New in 1.4.
 */
svn_error_t *svn_ra_svn__ssl_initialize(apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* RA_SVN_ENDPOINT_H */
