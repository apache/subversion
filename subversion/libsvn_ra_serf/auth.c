/* auth.c:  ra_serf authentication handling
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
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

/*** Includes. ***/

#include <serf.h>
#include <apr_base64.h>

#include "ra_serf.h"
#include "auth_digest.h"
#include "auth_kerb.h"
#include "win32_auth_sspi.h"
#include "svn_private_config.h"

/*** Forward declarations. ***/

#if defined(SVN_RA_SERF_SSPI_ENABLED)
static svn_error_t *
default_auth_response_handler(svn_ra_serf__handler_t *ctx,
                              serf_request_t *request,
                              serf_bucket_t *response,
                              apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}
#endif

/*** Global variables. ***/
static const svn_ra_serf__auth_protocol_t serf_auth_protocols[] = {
#ifdef SVN_RA_SERF_SSPI_ENABLED
  {
    401,
    "NTLM",
    svn_ra_serf__authn_ntlm,
    svn_ra_serf__init_sspi_connection,
    svn_ra_serf__handle_sspi_auth,
    svn_ra_serf__setup_request_sspi_auth,
    default_auth_response_handler,
  },
  {
    407,
    "NTLM",
    svn_ra_serf__authn_ntlm,
    svn_ra_serf__init_proxy_sspi_connection,
    svn_ra_serf__handle_proxy_sspi_auth,
    svn_ra_serf__setup_request_proxy_sspi_auth,
    default_auth_response_handler,
  },
#endif /* SVN_RA_SERF_SSPI_ENABLED */
#ifdef SVN_RA_SERF_HAVE_GSSAPI
  {
    401,
    "Negotiate",
    svn_ra_serf__authn_negotiate,
    svn_ra_serf__init_kerb_connection,
    svn_ra_serf__handle_kerb_auth,
    svn_ra_serf__setup_request_kerb_auth,
    svn_ra_serf__validate_response_kerb_auth,
  },
#endif /* SVN_RA_SERF_HAVE_GSSAPI */

  /* ADD NEW AUTHENTICATION IMPLEMENTATIONS HERE (as they're written) */

  /* sentinel */
  { 0 }
};

/*** Code. ***/

/**
 * base64 encode the authentication data and build an authentication
 * header in this format:
 * [PROTOCOL] [BASE64 AUTH DATA]
 */
void
svn_ra_serf__encode_auth_header(const char *protocol, const char **header,
                                const char *data, apr_size_t data_len,
                                apr_pool_t *pool)
{
  int encoded_len;
  size_t proto_len;
  char *ptr;

  encoded_len = apr_base64_encode_len((int) data_len);
  proto_len = strlen(protocol);

  ptr = apr_palloc(pool, encoded_len + proto_len + 1);
  *header = ptr;

  apr_cpystrn(ptr, protocol, proto_len + 1);
  ptr += proto_len;
  *ptr++ = ' ';

  apr_base64_encode(ptr, data, (int) data_len);
}

/**
 * Baton passed to the response header callback function
 */
typedef struct auth_baton_t {
  int code;
  const char *header;
  svn_ra_serf__handler_t *ctx;
  serf_request_t *request;
  serf_bucket_t *response;
  svn_error_t *err;
  apr_pool_t *pool;
  const svn_ra_serf__auth_protocol_t *prot;
  const char *last_prot_name;
} auth_baton_t;

/**
 * handle_auth_header is called for each header in the response. It filters
 * out the Authenticate headers (WWW or Proxy depending on what's needed) and
 * tries to find a matching protocol handler.
 *
 * Returns a non-0 value of a matching handler was found.
 */
static int
handle_auth_header(void *baton,
                   const char *key,
                   const char *header)
{
  auth_baton_t *ab = (auth_baton_t *)baton;
  svn_ra_serf__session_t *session = ab->ctx->session;
  svn_ra_serf__connection_t *conn = ab->ctx->conn;
  svn_boolean_t proto_found = FALSE;
  const char *auth_name;
  const char *auth_attr;
  const svn_ra_serf__auth_protocol_t *prot = NULL;

  /* We're only interested in xxxx-Authenticate headers. */
  if (strcmp(key, ab->header) != 0)
    return 0;

  auth_attr = strchr(header, ' ');
  if (auth_attr)
    {
      /* Extract the authentication protocol name, and set up the pointer
         to the attributes.  */
      auth_name = apr_pstrmemdup(ab->pool, header, auth_attr - header);
      ++auth_attr;
    }
  else
    auth_name = header;

  ab->last_prot_name = auth_name;

  /* Find the matching authentication handler.
     Note that we don't reuse the auth protocol stored in the session,
     as that may have changed. (ex. fallback from ntlm to basic.) */
  for (prot = serf_auth_protocols; prot->code != 0; ++prot)
    {
      if (ab->code == prot->code &&
          svn_cstring_casecmp(auth_name, prot->auth_name) == 0 &&
          session->authn_types & prot->auth_type)
        {
          svn_serf__auth_handler_func_t handler = prot->handle_func;
          svn_error_t *err = NULL;

          /* If this is the first time we use this protocol in this session,
             make sure to initialize the authentication part of the session
             first. */
          if (ab->code == 401 && session->auth_protocol != prot)
            {
              err = prot->init_conn_func(session, conn, session->pool);
              if (err == SVN_NO_ERROR)
                session->auth_protocol = prot;
              else
                session->auth_protocol = NULL;
            }
          else if (ab->code == 407 && session->proxy_auth_protocol != prot)
            {
              err = prot->init_conn_func(session, conn, session->pool);
              if (err == SVN_NO_ERROR)
                session->proxy_auth_protocol = prot;
              else
                session->proxy_auth_protocol = NULL;
            }

          if (err == SVN_NO_ERROR)
            {
              proto_found = TRUE;
              ab->prot = prot;
              err = handler(ab->ctx, ab->request, ab->response,
                            header, auth_attr, session->pool);
            }
          if (err)
            {
              /* If authentication fails, cache the error for now. Try the
                 next available scheme. If there's none raise the error. */
              proto_found = FALSE;
              prot = NULL;
              if (ab->err)
                svn_error_clear(ab->err);
              ab->err = err;
            }

          break;
        }
    }

  /* If a matching protocol handler was found, we can stop iterating
     over the response headers - so return a non-0 value. */
  return proto_found;
}


/* Dispatch authentication handling based on server <-> proxy authentication
   and the list of allowed authentication schemes as passed back from the
   server or proxy in the Authentication headers. */
svn_error_t *
svn_ra_serf__handle_auth(int code,
                         svn_ra_serf__handler_t *ctx,
                         serf_request_t *request,
                         serf_bucket_t *response,
                         apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ctx->session;
  serf_bucket_t *hdrs;
  auth_baton_t ab = { 0 };
  const char *auth_hdr;

  ab.code = code;
  ab.request = request;
  ab.response = response;
  ab.ctx = ctx;
  ab.err = SVN_NO_ERROR;
  ab.pool = pool;

  hdrs = serf_bucket_response_get_headers(response);

  if (code == 401)
    ab.header = "WWW-Authenticate";
  else if (code == 407)
    ab.header = "Proxy-Authenticate";

  /* Before iterating over all authn headers, check if there are any. */
  auth_hdr = serf_bucket_headers_get(hdrs, ab.header);
  if (!auth_hdr)
    {
      if (session->auth_protocol)
        return svn_error_createf(SVN_ERR_AUTHN_FAILED, NULL,
                                 _("%s Authentication failed"),
                                 session->auth_protocol->auth_name);
      else
        return svn_error_create(SVN_ERR_AUTHN_FAILED, NULL, NULL);
    }

  /* Iterate over all headers. Try to find a matching authentication protocol
     handler.

     Note: it is possible to have multiple Authentication: headers. We do
     not want to combine them (per normal header combination rules) as that
     would make it hard to parse. Instead, we want to individually parse
     and handle each header in the response, looking for one that we can
     work with.
  */
  serf_bucket_headers_do(hdrs,
                         handle_auth_header,
                         &ab);
  SVN_ERR(ab.err);

  if (!ab.prot || ab.prot->auth_name == NULL)
    {
      /* Support more authentication mechanisms. */
      return svn_error_createf(SVN_ERR_AUTHN_FAILED, NULL,
                               _("%s authentication not supported.\n"
                               "Authentication failed"),
                               ab.last_prot_name
                                 ? ab.last_prot_name
                                 : "Unknown");
    }

  return SVN_NO_ERROR;
}
