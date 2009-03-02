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
#include "auth_digest.h"
#include "win32_auth_sspi.h"
#include "svn_private_config.h"

/*** Forward declarations. ***/

static svn_error_t *
handle_basic_auth(svn_ra_serf__handler_t *ctx,
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
			 const char *method,
			 const char *uri,
                         serf_bucket_t *hdrs_bkt);

static svn_error_t *
handle_proxy_basic_auth(svn_ra_serf__handler_t *ctx,
                        serf_request_t *request,
                        serf_bucket_t *response,
                        char *auth_hdr,
                        char *auth_attr,
                        apr_pool_t *pool);

static svn_error_t *
init_proxy_basic_connection(svn_ra_serf__session_t *session,
                            svn_ra_serf__connection_t *conn,
                            apr_pool_t *pool);

static svn_error_t *
setup_request_proxy_basic_auth(svn_ra_serf__connection_t *conn,
			       const char *method,
			       const char *uri,
                               serf_bucket_t *hdrs_bkt);

/*** Global variables. ***/
static const svn_ra_serf__auth_protocol_t serf_auth_protocols[] = {
  {
    401,
    "Basic",
    init_basic_connection,
    handle_basic_auth,
    setup_request_basic_auth,
  },
  {
    407,
    "Basic",
    init_proxy_basic_connection,
    handle_proxy_basic_auth,
    setup_request_proxy_basic_auth,
  },
#ifdef SVN_RA_SERF_SSPI_ENABLED
  {
    401,
    "NTLM",
    init_sspi_connection,
    handle_sspi_auth,
    setup_request_sspi_auth,
  },
  {
    407,
    "NTLM",
    init_proxy_sspi_connection,
    handle_proxy_sspi_auth,
    setup_request_proxy_sspi_auth,
  },
#endif /* SVN_RA_SERF_SSPI_ENABLED */
  {
    401,
    "Digest",
    init_digest_connection,
    handle_digest_auth,
    setup_request_digest_auth,
  },

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
svn_ra_serf__encode_auth_header(const char * protocol, char **header,
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

/**
 * Baton passed to the response header callback function
 */
typedef struct {
  int code;
  const char *header;
  svn_ra_serf__handler_t *ctx;
  serf_request_t *request;
  serf_bucket_t *response;
  svn_error_t *err;
  apr_pool_t *pool;
  const svn_ra_serf__auth_protocol_t *prot;
  char *last_prot_name;
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
  char *auth_name, *auth_attr;
  const char *auth_hdr;
  const svn_ra_serf__auth_protocol_t *prot;

  /* We're only interested in xxxx-Authenticate headers. */
  if (ab->code == 401)
    auth_hdr = "WWW-Authenticate";
  else if (ab->code == 407)
    auth_hdr = "Proxy-Authenticate";

  if (strcmp(key, auth_hdr) != 0)
    return 0;

  auth_name = apr_strtok(header, " ", &auth_attr);
  ab->last_prot_name = auth_name;

  /* Find the matching authentication handler.
     Note that we don't reuse the auth protocol stored in the session,
     as that may have changed. (ex. fallback from ntlm to basic.) */
  for (prot = serf_auth_protocols; prot->code != 0; ++prot)
    {
      if (ab->code == prot->code && strcmp(auth_name, prot->auth_name) == 0)
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
  auth_baton_t *ab;
  char *auth_hdr = NULL;

  ab = apr_pcalloc(pool, sizeof(*ab));
  ab->code = code;
  ab->request = request;
  ab->response = response;
  ab->ctx = ctx;
  ab->err = SVN_NO_ERROR;
  ab->pool = pool;

  hdrs = serf_bucket_response_get_headers(response);

  if (code == 401)
    ab->header = "WWW-Authenticate";
  else if (code == 407)
    ab->header = "Proxy-Authenticate";

  /* Before iterating over all authn headers, check if there are any. */
  auth_hdr = (char*)serf_bucket_headers_get(hdrs, ab->header);

  if (!auth_hdr)
    {
      if (session->auth_protocol)
        return svn_error_createf(SVN_ERR_AUTHN_FAILED, NULL,
                                 "%s Authentication failed",
                                 session->auth_protocol->auth_name);
      else
        return svn_error_create(SVN_ERR_AUTHN_FAILED, NULL, NULL);
    }

  /* Iterate over all headers. Try to find a matching authentication protocol
     handler. */
  serf_bucket_headers_do(hdrs,
			 handle_auth_header,
			 ab);                         
  SVN_ERR(ab->err);

  if (!ab->prot || ab->prot->auth_name == NULL)
    {
      /* Support more authentication mechanisms. */
      return svn_error_createf(SVN_ERR_AUTHN_FAILED, NULL,
                               "%s authentication not supported.\n"
                               "Authentication failed", ab->last_prot_name);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
handle_basic_auth(svn_ra_serf__handler_t *ctx,
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
  svn_ra_serf__session_t *session = ctx->session;

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
          return svn_error_create
            (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
             _("Missing 'realm' attribute in Authorization header"));
        }
      if (!realm_name)
        {
          return svn_error_create
            (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
             _("Missing 'realm' attribute in Authorization header"));
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
    }

  /* Use svn_auth_first_credentials if this is the first time we ask for
     credentials during this session OR if the last time we asked
     session->auth_state wasn't set (eg. if the credentials provider was
     cancelled by the user). */
  if (!session->auth_state)
    {
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

  svn_ra_serf__encode_auth_header(session->auth_protocol->auth_name,
                                  &session->auth_value, tmp, tmp_len, pool);
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
			 const char *method,
			 const char *uri,
                         serf_bucket_t *hdrs_bkt)
{
  /* Take the default authentication header for this connection, if any. */
  if (conn->auth_header && conn->auth_value)
    {
      serf_bucket_headers_setn(hdrs_bkt, conn->auth_header, conn->auth_value);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
handle_proxy_basic_auth(svn_ra_serf__handler_t *ctx,
                        serf_request_t *request,
                        serf_bucket_t *response,
                        char *auth_hdr,
                        char *auth_attr,
                        apr_pool_t *pool)
{
  const char *tmp;
  apr_size_t tmp_len;
  int i;
  svn_ra_serf__session_t *session = ctx->session;

  tmp = apr_pstrcat(session->pool,
                    session->proxy_username, ":",
                    session->proxy_password, NULL);
  tmp_len = strlen(tmp);

  session->proxy_auth_attempts++;

  if (session->proxy_auth_attempts > 1)
    {
      /* No more credentials. */
      return svn_error_create(SVN_ERR_AUTHN_FAILED, NULL,
                "Proxy authentication failed");
    }

  svn_ra_serf__encode_auth_header(session->proxy_auth_protocol->auth_name,
                                  &session->proxy_auth_value,
                                  tmp, tmp_len, pool);
  session->proxy_auth_header = "Proxy-Authorization";

  /* FIXME Come up with a cleaner way of changing the connection auth. */
  for (i = 0; i < session->num_conns; i++)
    {
      session->conns[i]->proxy_auth_header = session->proxy_auth_header;
      session->conns[i]->proxy_auth_value = session->proxy_auth_value;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
init_proxy_basic_connection(svn_ra_serf__session_t *session,
                            svn_ra_serf__connection_t *conn,
                            apr_pool_t *pool)
{
  conn->proxy_auth_header = session->proxy_auth_header;
  conn->proxy_auth_value = session->proxy_auth_value;

  return SVN_NO_ERROR;
}

static svn_error_t *
setup_request_proxy_basic_auth(svn_ra_serf__connection_t *conn,
			       const char *method,
			       const char *uri,
                               serf_bucket_t *hdrs_bkt)
{
  /* Take the default authentication header for this connection, if any. */
  if (conn->proxy_auth_header && conn->proxy_auth_value)
    {
      serf_bucket_headers_setn(hdrs_bkt, conn->proxy_auth_header,
                               conn->proxy_auth_value);
    }

  return SVN_NO_ERROR;
}
