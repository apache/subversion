/*
 * svn_server.h :  declarations for the svn server
 *
 * ====================================================================
 * Copyright (c) 2002 CollabNet.  All rights reserved.
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

svn_error_t *serve(svn_ra_svn_conn_t *conn, const char *root,
                   svn_boolean_t tunnel, apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SERVER_H */
