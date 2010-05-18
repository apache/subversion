/*
 * ra_svn_sasl.h :  SASL-related declarations shared between the
 * ra_svn and svnserve module
 *
 * ====================================================================
 * Copyright (c) 2006-2007 CollabNet.  All rights reserved.
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



#ifndef RA_SVN_SASL_H
#define RA_SVN_SASL_H

#ifdef WIN32
/* This prevents sasl.h from redefining iovec, which is always defined by APR
   on win32. */
#define STRUCT_IOVEC_DEFINED
#include <sasl.h>
#else
#include <sasl/sasl.h>
#endif

#include <apr_errno.h>
#include <apr_pools.h>

#include "svn_error.h"
#include "svn_ra_svn.h"

#include "private/svn_atomic.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** The application and service name used for sasl_client_new,
 * sasl_server_init, and sasl_server_new. */
#define SVN_RA_SVN_SASL_NAME "svn"

extern volatile svn_atomic_t svn_ra_svn__sasl_status;

/* Initialize secprops with default values. */
void
svn_ra_svn__default_secprops(sasl_security_properties_t *secprops);

/* This function is called by the client and the server before
   calling sasl_{client, server}_init, pool is used for allocations. */
apr_status_t
svn_ra_svn__sasl_common_init(apr_pool_t *pool);

/* Sets local_addrport and remote_addrport to a string containing the
   remote and local IP address and port, formatted like this: a.b.c.d;port. */
svn_error_t *
svn_ra_svn__get_addresses(const char **local_addrport,
                          const char **remote_addrport,
                          svn_ra_svn_conn_t *conn,
                          apr_pool_t *pool);

/* If a security layer was negotiated during the authentication exchange,
   create an encrypted stream for conn. */
svn_error_t *
svn_ra_svn__enable_sasl_encryption(svn_ra_svn_conn_t *conn,
                                   sasl_conn_t *sasl_ctx,
                                   apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* RA_SVN_SASL_H */
