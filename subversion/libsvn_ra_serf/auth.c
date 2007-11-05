/* auth.c:  ra_serf authentication handling
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

/*** Includes. ***/

#include <serf.h>
#include <apr_base64.h>

#include "ra_serf.h"

/*** Forward declarations. ***/

static svn_error_t *
handle_basic_auth(svn_ra_serf__session_t *session,
                  svn_ra_serf__connection_t *conn,
                  serf_request_t *request,
                  serf_bucket_t *response,
                  char *auth_hdr,
                  char *auth_attr,
                  apr_pool_t *pool);

static svn_error_t *
init_basic_connection(svn_ra_serf__session_t *session,
                      svn_ra_serf__connection_t *conn,
                      apr_pool_t *pool);

static svn_error_t *
setup_request_basic_auth(svn_ra_serf__connection_t *conn,
                         serf_bucket_t *hdrs_bkt);

/*** Global variables. ***/
static const svn_ra_serf__auth_protocol_t serf_auth_protocols[] = {
  {
    "Basic",
    init_basic_connection,
    handle_basic_auth,
    setup_request_basic_auth,
  },

  /* ADD NEW AUTHENTICATION IMPLEMENTATIONS HERE (as they're written) */

  /* sentinel */
  { NULL }
};

/*** Code. ***/

/**
 * base64 encode the authentication data and build an authentication
 * header in this format:
 * [PROTOCOL] [BASE64 AUTH DATA]
 */
static void
encode_auth_header(const char * protocol, char **header,
                   const char * data, apr_size_t data_len,
                   apr_pool_t *pool)
{
  apr_size_t encoded_len, proto_len;
  char * ptr;

  encoded_len = apr_base64_encode_len(data_len);
  proto_len = strlen(protocol);

  *header = apr_palloc(pool, encoded_len + proto_len + 1);
  ptr = *header;

  apr_cpystrn(ptr, protocol, proto_len + 1);
  ptr += proto_len;
  *ptr++ = ' ';

  apr_base64_encode(ptr, data, data_len);
}


svn_error_t *
svn_ra_serf__handle_auth(svn_ra_serf__session_t *session,
                         svn_ra_serf__connection_t *conn,
                         serf_request_t *request,
                         serf_bucket_t *response,
                         apr_pool_t *pool)
{
  serf_bucket_t *hdrs;
  const svn_ra_serf__auth_protocol_t *prot;
  char *auth_name, *auth_attr, *auth_hdr;

  hdrs = serf_bucket_response_get_headers(response);
  auth_hdr = (char*)serf_bucket_headers_get(hdrs, "WWW-Authenticate");

  if (!auth_hdr)
    {
      if (session->auth_protocol)
        return svn_error_createf(SVN_ERR_AUTHN_FAILED, NULL,
                                 "%s Authentication failed",
                                 session->auth_protocol->auth_name);
      else
        return svn_error_create(SVN_ERR_AUTHN_FAILED, NULL, NULL);
    }

  auth_name = apr_strtok(auth_hdr, " ", &auth_attr);

  /* Find the matching authentication handler.
     Note that we don't reuse the auth protocol stored in the session,
     as that may have changed. (ex. fallback from ntlm to basic.) */
  for (prot = serf_auth_protocols; prot->auth_name != NULL; ++prot)
    {
      if (strcmp(auth_name, prot->auth_name) == 0)
        {
          svn_serf__auth_handler_func_t handler = prot->handle_func;
          /* If this is the first time we use this protocol in this session,
             make sure to initialize the authentication part of the session
             first. */
          if (session->auth_protocol != prot)
            SVN_ERR(prot->init_conn_func(session, conn, session->pool));
          session->auth_protocol = prot;
          SVN_ERR(handler(session, conn, request, response,
                          auth_hdr, auth_attr, session->pool));
          break;
        }
    }
  if (prot->auth_name == NULL)
    {
      /* Support more authentication mechanisms. */
      return svn_error_createf(SVN_ERR_AUTHN_FAILED, NULL,
                               "%s authentication not supported.\n"
                               "Authentication failed", auth_name);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
handle_basic_auth(svn_ra_serf__session_t *session,
                  svn_ra_serf__connection_t *conn,
                  serf_request_t *request,
                  serf_bucket_t *response,
                  char *auth_hdr,
                  char *auth_attr,
                  apr_pool_t *pool)
{
  void *creds;
  char *last, *realm_name;
  svn_auth_cred_simple_t *simple_creds;
  const char *tmp;
  apr_size_t tmp_len;
  apr_port_t port;
  int i;

  if (!session->realm)
    {
      char *attr;

      attr = apr_strtok(auth_attr, "=", &last);
      if (strcmp(attr, "realm") == 0)
        {
          realm_name = apr_strtok(NULL, "=", &last);
          if (realm_name[0] == '\"')
            {
              apr_size_t realm_len;

              realm_len = strlen(realm_name);
              if (realm_name[realm_len - 1] == '\"')
                {
                  realm_name[realm_len - 1] = '\0';
                  realm_name++;
                }
            }
        }
      else
        {
          abort();
        }
      if (!realm_name)
        {
          abort();
        }

      if (session->repos_url.port_str)
        {
          port = session->repos_url.port;
        }
      else
        {
          port = apr_uri_port_of_scheme(session->repos_url.scheme);
        }

      session->realm = apr_psprintf(session->pool, "<%s://%s:%d> %s",
                                    session->repos_url.scheme,
                                    session->repos_url.hostname,
                                    port,
                                    realm_name);

      SVN_ERR(svn_auth_first_credentials(&creds,
                                         &session->auth_state,
                                         SVN_AUTH_CRED_SIMPLE,
                                         session->realm,
                                         session->wc_callbacks->auth_baton,
                                         session->pool));
    }
  else
    {
      SVN_ERR(svn_auth_next_credentials(&creds,
                                        session->auth_state,
                                        session->pool));
    }

  session->auth_attempts++;

  if (!creds || session->auth_attempts > 4)
    {
      /* No more credentials. */
      return svn_error_create(SVN_ERR_AUTHN_FAILED, NULL,
                "No more credentials or we tried too many times.\n"
                "Authentication failed");
    }

  simple_creds = creds;

  tmp = apr_pstrcat(session->pool,
                    simple_creds->username, ":", simple_creds->password, NULL);
  tmp_len = strlen(tmp);

  encode_auth_header(session->auth_protocol->auth_name, &session->auth_value,
                     tmp, tmp_len, pool);
  session->auth_header = "Authorization";

  /* FIXME Come up with a cleaner way of changing the connection auth. */
  for (i = 0; i < session->num_conns; i++)
    {
      session->conns[i]->auth_header = session->auth_header;
      session->conns[i]->auth_value = session->auth_value;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
init_basic_connection(svn_ra_serf__session_t *session,
                      svn_ra_serf__connection_t *conn,
                      apr_pool_t *pool)
{
  conn->auth_header = session->auth_header;
  conn->auth_value = session->auth_value;

  return SVN_NO_ERROR;
}

static svn_error_t *
setup_request_basic_auth(svn_ra_serf__connection_t *conn,
                         serf_bucket_t *hdrs_bkt)
{
  /* Take the default authentication header for this connection, if any. */
  if (conn->auth_header && conn->auth_value)
    {
      serf_bucket_headers_setn(hdrs_bkt, conn->auth_header, conn->auth_value);
    }

  return SVN_NO_ERROR;
}
