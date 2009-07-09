/*
 * auth_kerb.h : Private declarations for Kerberos authentication.
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */
#ifndef SVN_LIBSVN_RA_SERF_AUTH_KERB_H
#define SVN_LIBSVN_RA_SERF_AUTH_KERB_H

#include "svn_error.h"
#include "ra_serf.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Kerberos implementation of an ra_serf authentication protocol providor.
   handle_kerb_auth prepares the authentication headers for a new request
   based on the response of the server. */
svn_error_t *
svn_ra_serf__handle_kerb_auth(svn_ra_serf__handler_t *ctx,
			      serf_request_t *request,
			      serf_bucket_t *response,
			      const char *auth_hdr,
			      const char *auth_attr,
			      apr_pool_t *pool);

/* Initializes a new connection based on the info stored in the session
   object. */
svn_error_t *
svn_ra_serf__init_kerb_connection(svn_ra_serf__session_t *session,
				  svn_ra_serf__connection_t *conn,
				  apr_pool_t *pool);


svn_error_t *
svn_ra_serf__setup_request_kerb_auth(svn_ra_serf__connection_t *conn,
				     const char *method,
				     const char *uri,
				     serf_bucket_t *hdrs_bkt);

svn_error_t *
svn_ra_serf__validate_response_kerb_auth(svn_ra_serf__handler_t *ctx,
                                         serf_request_t *request,
                                         serf_bucket_t *response,
                                         apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_RA_SERF_AUTH_KERB_H */
