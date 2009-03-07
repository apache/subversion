/*
 * auth_digest.h : Private declarations for digest authentication.
 *
 * ====================================================================
 * Copyright (c) 2009 CollabNet.  All rights reserved.
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
#ifndef SVN_LIBSVN_RA_SERF_AUTH_DIGEST_H
#define SVN_LIBSVN_RA_SERF_AUTH_DIGEST_H

#include "svn_error.h"
#include "ra_serf.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Stores the context information related to Digest authentication. 
   The context is per connection. */
typedef struct
{
  /* nonce-count for digest authentication */
  unsigned int digest_nc;

  const char *ha1;

  const char *realm;
  const char *cnonce;
  const char *nonce;
  const char *opaque;
  const char *algorithm;
  const char *qop;
  const char *username;

  apr_pool_t *pool;
} serf_digest_context_t;

/* Digest implementation of an ra_serf authentication protocol providor.
   handle_digest_auth prepares the authentication headers for a new request
   based on the response of the server. */
svn_error_t *
svn_ra_serf__handle_digest_auth(svn_ra_serf__handler_t *ctx,
                                serf_request_t *request,
                                serf_bucket_t *response,
                                const char *auth_hdr,
                                const char *auth_attr,
                                apr_pool_t *pool);

/* Initializes a new connection based on the info stored in the session
   object. */
svn_error_t *
svn_ra_serf__init_digest_connection(svn_ra_serf__session_t *session,
                                    svn_ra_serf__connection_t *conn,
                                    apr_pool_t *pool);


svn_error_t *
svn_ra_serf__setup_request_digest_auth(svn_ra_serf__connection_t *conn,
                                       const char *method,
                                       const char *uri,
                                       serf_bucket_t *hdrs_bkt);

svn_error_t *
svn_ra_serf__validate_response_digest_auth(svn_ra_serf__handler_t *ctx,
                                           serf_request_t *request,
                                           serf_bucket_t *response,
                                           apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_RA_SERF_AUTH_DIGEST_H */
