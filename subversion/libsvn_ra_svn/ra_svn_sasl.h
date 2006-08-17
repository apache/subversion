/*
 * ra_svn_sasl.h :  SASL-related declarations shared between the 
 * ra_svn and svnserve module
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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "ra_svn.h" /* for SVN_RA_SVN__READBUF_SIZE */

/* Define sane defaults for a sasl_security_properties_t structure.
   The first two values are the minimum and maximum encryption strengths
   that the chosen SASL mechanism should provide.  0 means 'no encryption',
   256 means '256-bit encryption', which is about the best that any SASL
   mechanism can provide.  Using these values effectively means 'use whatever
   encryption the other side wants'.  Note that SASL will try to use better
   encryption whenever possible, so if both the server and the client use
   these values the highest possible encryption strength will be used.
   The third value, the connection's read buffer size, needs to be
   commmunicated to the peer if a security layer is negotiated. */
#define SVN_RA_SVN__DEFAULT_SECPROPS {0, 256, SVN_RA_SVN__READBUF_SIZE, \
                                      0, NULL, NULL}

/* This function is called by the client and the server before
   calling sasl_{client, server}_init. */
apr_status_t svn_ra_svn__sasl_common_init(void);

/* Sets local_addrport and remote_addrport to a string containing the 
   remote and local IP address and port, formatted like this: a.b.c.d;port. */
svn_error_t *svn_ra_svn__get_addresses(const char **local_addrport,
                                       const char **remote_addrport,
                                       apr_socket_t *sock,
                                       apr_pool_t *pool);
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* RA_SVN_SASL_H */
