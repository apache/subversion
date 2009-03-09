/*
 * win32_auth_sspi.h : Private declarations for Windows SSPI authentication.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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
#ifndef SVN_LIBSVN_RA_SERF_WIN32_AUTH_SSPI_H
#define SVN_LIBSVN_RA_SERF_WIN32_AUTH_SSPI_H

/* Define this to use our fake_sspi.h header. This is handy for developers
   on Unix to see if win32_auth_sspi.c will compile after changes are made.
   It won't function, however, so don't keep it in your build.  */
/* #define USE_FAKE_SSPI */

#ifdef USE_FAKE_SSPI
#define SVN_RA_SERF_SSPI_ENABLED
#endif

#ifdef SVN_RA_SERF_SSPI_ENABLED

#ifdef USE_FAKE_SSPI
#include "fake_sspi.h"
#else
#ifndef __SSPI_H__
#define SECURITY_WIN32
#include <sspi.h>
#endif
#endif

#include "svn_error.h"
#include "ra_serf.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum
{
  sspi_auth_not_started,
  sspi_auth_in_progress,
  sspi_auth_completed,
} sspi_auth_state;

/* Stores the context information related to SSPI. The context is per
   connection, it enables SSPI to go through the challenge/response cycle
   of the authentication protocols. */
typedef struct
{
  CtxtHandle ctx;

  /* Current state of the authentication cycle. */
  sspi_auth_state state;

} serf_sspi_context_t;

/* SSPI implementation of an ra_serf authentication protocol providor.
   handle_sspi_auth prepares the authentication headers for a new request
   based on the response of the server. */
svn_error_t *
svn_ra_serf__handle_sspi_auth(svn_ra_serf__handler_t *ctx,
                              serf_request_t *request,
                              serf_bucket_t *response,
                              const char *auth_hdr,
                              const char *auth_attr,
                              apr_pool_t *pool);

/* Initializes a new connection based on the info stored in the session
   object. For SSPI we will not reuse any of the authentication related data
   in the session, as SSPI provides per connection authentication protocols.
 */
svn_error_t *
svn_ra_serf__init_sspi_connection(svn_ra_serf__session_t *session,
                                  svn_ra_serf__connection_t *conn,
                                  apr_pool_t *pool);

svn_error_t *
svn_ra_serf__setup_request_sspi_auth(svn_ra_serf__connection_t *conn,
                                     const char *method,
                                     const char *uri,
                                     serf_bucket_t *hdrs_bkt);

/* Proxy authentication */
svn_error_t *
svn_ra_serf__handle_proxy_sspi_auth(svn_ra_serf__handler_t *ctx,
                                    serf_request_t *request,
                                    serf_bucket_t *response,
                                    const char *auth_hdr,
                                    const char *auth_attr,
                                    apr_pool_t *pool);

svn_error_t *
svn_ra_serf__init_proxy_sspi_connection(svn_ra_serf__session_t *session,
                                        svn_ra_serf__connection_t *conn,
                                        apr_pool_t *pool);

svn_error_t *
svn_ra_serf__setup_request_proxy_sspi_auth(svn_ra_serf__connection_t *conn,
                                           const char *method,
                                           const char *uri,
                                           serf_bucket_t *hdrs_bkt);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_RA_SERF_SSPI_ENABLED */

#endif /* SVN_LIBSVN_RA_SERF_WIN32_AUTH_SSPI_H */
