/*
 * util.c : serf utility routines for ra_serf
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



#include <assert.h>

#define APR_WANT_STRFUNC
#include <apr.h>
#include <apr_want.h>
#include <apr_fnmatch.h>

#include <serf.h>
#include <serf_bucket_types.h>

#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_private_config.h"
#include "svn_string.h"
#include "svn_xml.h"
#include "private/svn_dep_compat.h"
#include "private/svn_fspath.h"

#include "ra_serf.h"


/* Fix for older expat 1.95.x's that do not define
 * XML_STATUS_OK/XML_STATUS_ERROR
 */
#ifndef XML_STATUS_OK
#define XML_STATUS_OK    1
#define XML_STATUS_ERROR 0
#endif


static const apr_uint32_t serf_failure_map[][2] =
{
  { SERF_SSL_CERT_NOTYETVALID,   SVN_AUTH_SSL_NOTYETVALID },
  { SERF_SSL_CERT_EXPIRED,       SVN_AUTH_SSL_EXPIRED },
  { SERF_SSL_CERT_SELF_SIGNED,   SVN_AUTH_SSL_UNKNOWNCA },
  { SERF_SSL_CERT_UNKNOWNCA,     SVN_AUTH_SSL_UNKNOWNCA }
};

/* Return a Subversion failure mask based on FAILURES, a serf SSL
   failure mask.  If anything in FAILURES is not directly mappable to
   Subversion failures, set SVN_AUTH_SSL_OTHER in the returned mask. */
static apr_uint32_t
ssl_convert_serf_failures(int failures)
{
  apr_uint32_t svn_failures = 0;
  apr_size_t i;

  for (i = 0; i < sizeof(serf_failure_map) / (2 * sizeof(apr_uint32_t)); ++i)
    {
      if (failures & serf_failure_map[i][0])
        {
          svn_failures |= serf_failure_map[i][1];
          failures &= ~serf_failure_map[i][0];
        }
    }

  /* Map any remaining failure bits to our OTHER bit. */
  if (failures)
    {
      svn_failures |= SVN_AUTH_SSL_OTHER;
    }

  return svn_failures;
}

/* Construct the realmstring, e.g. https://svn.collab.net:443. */
static const char *
construct_realm(svn_ra_serf__session_t *session,
                apr_pool_t *pool)
{
  const char *realm;
  apr_port_t port;

  if (session->repos_url.port_str)
    {
      port = session->repos_url.port;
    }
  else
    {
      port = apr_uri_port_of_scheme(session->repos_url.scheme);
    }

  realm = apr_psprintf(pool, "%s://%s:%d",
                       session->repos_url.scheme,
                       session->repos_url.hostname,
                       port);

  return realm;
}

/* Convert a hash table containing the fields (as documented in X.509) of an
   organisation to a string ORG, allocated in POOL. ORG is as returned by
   serf_ssl_cert_issuer() and serf_ssl_cert_subject(). */
static char *
convert_organisation_to_str(apr_hash_t *org, apr_pool_t *pool)
{
  return apr_psprintf(pool, "%s, %s, %s, %s, %s (%s)",
                      (char*)apr_hash_get(org, "OU", APR_HASH_KEY_STRING),
                      (char*)apr_hash_get(org, "O", APR_HASH_KEY_STRING),
                      (char*)apr_hash_get(org, "L", APR_HASH_KEY_STRING),
                      (char*)apr_hash_get(org, "ST", APR_HASH_KEY_STRING),
                      (char*)apr_hash_get(org, "C", APR_HASH_KEY_STRING),
                      (char*)apr_hash_get(org, "E", APR_HASH_KEY_STRING));
}

/* This function is called on receiving a ssl certificate of a server when
   opening a https connection. It allows Subversion to override the initial
   validation done by serf.
   Serf provides us the @a baton as provided in the call to
   serf_ssl_server_cert_callback_set. The result of serf's initial validation
   of the certificate @a CERT is returned as a bitmask in FAILURES. */
static svn_error_t *
ssl_server_cert(void *baton, int failures,
                const serf_ssl_certificate_t *cert,
                apr_pool_t *scratch_pool)
{
  svn_ra_serf__connection_t *conn = baton;
  svn_auth_ssl_server_cert_info_t cert_info;
  svn_auth_cred_ssl_server_trust_t *server_creds = NULL;
  svn_auth_iterstate_t *state;
  const char *realmstring;
  apr_uint32_t svn_failures;
  apr_hash_t *issuer, *subject, *serf_cert;
  void *creds;

  /* Implicitly approve any non-server certs. */
  if (serf_ssl_cert_depth(cert) > 0)
    {
      return APR_SUCCESS;
    }

  /* Extract the info from the certificate */
  subject = serf_ssl_cert_subject(cert, scratch_pool);
  issuer = serf_ssl_cert_issuer(cert, scratch_pool);
  serf_cert = serf_ssl_cert_certificate(cert, scratch_pool);

  cert_info.hostname = apr_hash_get(subject, "CN", APR_HASH_KEY_STRING);
  cert_info.fingerprint = apr_hash_get(serf_cert, "sha1", APR_HASH_KEY_STRING);
  if (! cert_info.fingerprint)
    cert_info.fingerprint = apr_pstrdup(scratch_pool, "<unknown>");
  cert_info.valid_from = apr_hash_get(serf_cert, "notBefore",
                         APR_HASH_KEY_STRING);
  if (! cert_info.valid_from)
    cert_info.valid_from = apr_pstrdup(scratch_pool, "[invalid date]");
  cert_info.valid_until = apr_hash_get(serf_cert, "notAfter",
                          APR_HASH_KEY_STRING);
  if (! cert_info.valid_until)
    cert_info.valid_until = apr_pstrdup(scratch_pool, "[invalid date]");
  cert_info.issuer_dname = convert_organisation_to_str(issuer, scratch_pool);
  cert_info.ascii_cert = serf_ssl_cert_export(cert, scratch_pool);

  svn_failures = ssl_convert_serf_failures(failures);

  /* Match server certificate CN with the hostname of the server */
  if (cert_info.hostname)
    {
      if (apr_fnmatch(cert_info.hostname, conn->hostinfo,
                      APR_FNM_PERIOD) == APR_FNM_NOMATCH)
        {
          svn_failures |= SVN_AUTH_SSL_CNMISMATCH;
        }
    }

  svn_auth_set_parameter(conn->session->wc_callbacks->auth_baton,
                         SVN_AUTH_PARAM_SSL_SERVER_FAILURES,
                         &svn_failures);

  svn_auth_set_parameter(conn->session->wc_callbacks->auth_baton,
                         SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO,
                         &cert_info);

  realmstring = construct_realm(conn->session, conn->session->pool);

  SVN_ERR(svn_auth_first_credentials(&creds, &state,
                                     SVN_AUTH_CRED_SSL_SERVER_TRUST,
                                     realmstring,
                                     conn->session->wc_callbacks->auth_baton,
                                     scratch_pool));
  if (creds)
    {
      server_creds = creds;
      SVN_ERR(svn_auth_save_credentials(state, scratch_pool));
    }

  svn_auth_set_parameter(conn->session->wc_callbacks->auth_baton,
                         SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO, NULL);

  if (!server_creds)
    return svn_error_create(SVN_ERR_RA_SERF_SSL_CERT_UNTRUSTED, NULL, NULL);

  return SVN_NO_ERROR;
}

/* Implements serf_ssl_need_server_cert_t for ssl_server_cert */
static apr_status_t
ssl_server_cert_cb(void *baton, int failures,
                const serf_ssl_certificate_t *cert)
{
  svn_ra_serf__connection_t *conn = baton;
  svn_ra_serf__session_t *session = conn->session;
  apr_pool_t *subpool;
  svn_error_t *err;

  subpool = svn_pool_create(session->pool);
  err = ssl_server_cert(baton, failures, cert, subpool);

  svn_pool_destroy(subpool);

  if (err || session->pending_error)
    {
      session->pending_error = svn_error_compose_create(
                                                    session->pending_error,
                                                    err);

      return session->pending_error->apr_err;
    }

  return APR_SUCCESS;

}

static svn_error_t *
load_authorities(svn_ra_serf__connection_t *conn, const char *authorities,
                 apr_pool_t *pool)
{
  char *files, *file, *last;
  files = apr_pstrdup(pool, authorities);

  while ((file = apr_strtok(files, ";", &last)) != NULL)
    {
      serf_ssl_certificate_t *ca_cert;
      apr_status_t status = serf_ssl_load_cert_file(&ca_cert, file, pool);
      if (status == APR_SUCCESS)
        status = serf_ssl_trust_cert(conn->ssl_context, ca_cert);

      if (status != APR_SUCCESS)
        {
          return svn_error_createf
            (SVN_ERR_BAD_CONFIG_VALUE, NULL,
             _("Invalid config: unable to load certificate file '%s'"),
             svn_dirent_local_style(file, pool));
        }
      files = NULL;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
conn_setup(apr_socket_t *sock,
           serf_bucket_t **read_bkt,
           serf_bucket_t **write_bkt,
           void *baton,
           apr_pool_t *pool)
{
  svn_ra_serf__connection_t *conn = baton;

  /* While serf < 0.4.0 is supported we should set read_bkt even when
     we have an error. See svn_ra_serf__conn_setup() */
  *read_bkt = serf_context_bucket_socket_create(conn->session->context,
                                               sock, conn->bkt_alloc);

  if (conn->using_ssl)
    {
      /* input stream */
      *read_bkt = serf_bucket_ssl_decrypt_create(*read_bkt, conn->ssl_context,
                                                 conn->bkt_alloc);
      if (!conn->ssl_context)
        {
          conn->ssl_context = serf_bucket_ssl_encrypt_context_get(*read_bkt);

          serf_ssl_client_cert_provider_set(conn->ssl_context,
                                            svn_ra_serf__handle_client_cert,
                                            conn, conn->session->pool);
          serf_ssl_client_cert_password_set(conn->ssl_context,
                                            svn_ra_serf__handle_client_cert_pw,
                                            conn, conn->session->pool);
          serf_ssl_server_cert_callback_set(conn->ssl_context,
                                            ssl_server_cert_cb,
                                            conn);

          /* See if the user wants us to trust "default" openssl CAs. */
          if (conn->session->trust_default_ca)
            {
              serf_ssl_use_default_certificates(conn->ssl_context);
            }
          /* Are there custom CAs to load? */
          if (conn->session->ssl_authorities)
            {
              SVN_ERR(load_authorities(conn, conn->session->ssl_authorities,
                                       conn->session->pool));
            }
        }

    if (write_bkt) /* = Serf >= 0.4.0, see svn_ra_serf__conn_setup() */
      /* output stream */
      *write_bkt = serf_bucket_ssl_encrypt_create(*write_bkt, conn->ssl_context,
                                                  conn->bkt_alloc);
    }

  return SVN_NO_ERROR;
}

#if SERF_VERSION_AT_LEAST(0, 4, 0)
/* This ugly ifdef construction can be cleaned up as soon as serf >= 0.4
   gets the minimum supported serf version! */

/* svn_ra_serf__conn_setup is a callback for serf. This function
   creates a read bucket and will wrap the write bucket if SSL
   is needed. */
apr_status_t
svn_ra_serf__conn_setup(apr_socket_t *sock,
                        serf_bucket_t **read_bkt,
                        serf_bucket_t **write_bkt,
                        void *baton,
                        apr_pool_t *pool)
{
#else
/* This is the old API, for compatibility with serf
   versions <= 0.3. */
serf_bucket_t *
svn_ra_serf__conn_setup(apr_socket_t *sock,
                        void *baton,
                        apr_pool_t *pool)
{
  serf_bucket_t **write_bkt = NULL;
  serf_bucket_t *rb = NULL;
  serf_bucket_t **read_bkt = &rb;
#endif
  svn_ra_serf__connection_t *conn = baton;
  svn_ra_serf__session_t *session = conn->session;
  apr_status_t status = SVN_NO_ERROR;

  svn_error_t *err = conn_setup(sock,
                                read_bkt,
                                write_bkt,
                                baton,
                                pool);

  if (err || session->pending_error)
    {
      session->pending_error = svn_error_compose_create(
                                          session->pending_error,
                                          err);

      status = session->pending_error->apr_err;
    }

#if ! SERF_VERSION_AT_LEAST(0, 4, 0)
  SVN_ERR_ASSERT_NO_RETURN(rb != NULL);
  return rb;
#else
  return status;
#endif
}

serf_bucket_t*
svn_ra_serf__accept_response(serf_request_t *request,
                             serf_bucket_t *stream,
                             void *acceptor_baton,
                             apr_pool_t *pool)
{
  serf_bucket_t *c;
  serf_bucket_alloc_t *bkt_alloc;

  bkt_alloc = serf_request_get_alloc(request);
  c = serf_bucket_barrier_create(stream, bkt_alloc);

  return serf_bucket_response_create(c, bkt_alloc);
}

static serf_bucket_t*
accept_head(serf_request_t *request,
            serf_bucket_t *stream,
            void *acceptor_baton,
            apr_pool_t *pool)
{
  serf_bucket_t *response;

  response = svn_ra_serf__accept_response(request, stream, acceptor_baton,
                                          pool);

  /* We know we shouldn't get a response body. */
  serf_bucket_response_set_head(response);

  return response;
}

static svn_error_t *
connection_closed(serf_connection_t *conn,
                  svn_ra_serf__connection_t *sc,
                  apr_status_t why,
                  apr_pool_t *pool)
{
  if (why)
    {
      SVN_ERR_MALFUNCTION();
    }

  if (sc->using_ssl)
      sc->ssl_context = NULL;

  /* Restart the authentication phase on this new connection. */
  if (sc->session->auth_protocol)
    SVN_ERR(sc->session->auth_protocol->init_conn_func(sc->session,
                                                       sc,
                                                       sc->session->pool));

  return SVN_NO_ERROR;
}

void
svn_ra_serf__conn_closed(serf_connection_t *conn,
                         void *closed_baton,
                         apr_status_t why,
                         apr_pool_t *pool)
{
  svn_ra_serf__connection_t *sc = closed_baton;
  svn_error_t *err;

  err = connection_closed(conn, sc, why, pool);

  if (err)
    sc->session->pending_error = svn_error_compose_create(
                                            sc->session->pending_error,
                                            err);
}

apr_status_t
svn_ra_serf__cleanup_serf_session(void *data)
{
  /* svn_ra_serf__session_t *serf_sess = data; */

  /* Nothing to do. */

  return APR_SUCCESS;
}

/* Implementation of svn_ra_serf__handle_client_cert */
static svn_error_t *
handle_client_cert(void *data,
                   const char **cert_path,
                   apr_pool_t *pool)
{
    svn_ra_serf__connection_t *conn = data;
    svn_ra_serf__session_t *session = conn->session;
    const char *realm;
    void *creds;

    *cert_path = NULL;

    realm = construct_realm(session, session->pool);

    if (!conn->ssl_client_auth_state)
      {
        SVN_ERR(svn_auth_first_credentials(&creds,
                                           &conn->ssl_client_auth_state,
                                           SVN_AUTH_CRED_SSL_CLIENT_CERT,
                                           realm,
                                           session->wc_callbacks->auth_baton,
                                           pool));
      }
    else
      {
        SVN_ERR(svn_auth_next_credentials(&creds,
                                          conn->ssl_client_auth_state,
                                          session->pool));
      }

    if (creds)
      {
        svn_auth_cred_ssl_client_cert_t *client_creds;
        client_creds = creds;
        *cert_path = client_creds->cert_file;
      }

    return SVN_NO_ERROR;
}

/* Implements serf_ssl_need_client_cert_t for handle_client_cert */
apr_status_t svn_ra_serf__handle_client_cert(void *data,
                                             const char **cert_path)
{
  svn_ra_serf__connection_t *conn = data;
  svn_ra_serf__session_t *session = conn->session;
  svn_error_t *err = handle_client_cert(data,
                                        cert_path,
                                        session->pool);

  if (err || session->pending_error)
    {
      session->pending_error = svn_error_compose_create(
                                                    session->pending_error,
                                                    err);

      return session->pending_error->apr_err;
    }

  return APR_SUCCESS;
}

/* Implementation for svn_ra_serf__handle_client_cert_pw */
static svn_error_t *
handle_client_cert_pw(void *data,
                      const char *cert_path,
                      const char **password,
                      apr_pool_t *pool)
{
    svn_ra_serf__connection_t *conn = data;
    svn_ra_serf__session_t *session = conn->session;
    void *creds;

    *password = NULL;

    if (!conn->ssl_client_pw_auth_state)
      {
        SVN_ERR(svn_auth_first_credentials(&creds,
                                           &conn->ssl_client_pw_auth_state,
                                           SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
                                           cert_path,
                                           session->wc_callbacks->auth_baton,
                                           pool));
      }
    else
      {
        SVN_ERR(svn_auth_next_credentials(&creds,
                                          conn->ssl_client_pw_auth_state,
                                          pool));
      }

    if (creds)
      {
        svn_auth_cred_ssl_client_cert_pw_t *pw_creds;
        pw_creds = creds;
        *password = pw_creds->password;
      }

    return APR_SUCCESS;
}

/* Implements serf_ssl_need_client_cert_pw_t for handle_client_cert_pw */
apr_status_t svn_ra_serf__handle_client_cert_pw(void *data,
                                                const char *cert_path,
                                                const char **password)
{
  svn_ra_serf__connection_t *conn = data;
  svn_ra_serf__session_t *session = conn->session;
  svn_error_t *err = handle_client_cert_pw(data,
                                           cert_path,
                                           password,
                                           session->pool);

  if (err || session->pending_error)
    {
      session->pending_error = svn_error_compose_create(
                                          session->pending_error,
                                          err);

      return session->pending_error->apr_err;
    }

  return APR_SUCCESS;
}


svn_error_t *
svn_ra_serf__setup_serf_req(serf_request_t *request,
                            serf_bucket_t **req_bkt,
                            serf_bucket_t **ret_hdrs_bkt,
                            svn_ra_serf__connection_t *conn,
                            const char *method, const char *url,
                            serf_bucket_t *body_bkt, const char *content_type)
{
  serf_bucket_t *hdrs_bkt;

  /* Create a request bucket.  Note that this sucker is kind enough to
     add a "Host" header for us.  */
  *req_bkt =
    serf_request_bucket_request_create(request, method, url, body_bkt,
                                       serf_request_get_alloc(request));

  hdrs_bkt = serf_bucket_request_get_headers(*req_bkt);
  serf_bucket_headers_setn(hdrs_bkt, "User-Agent", conn->useragent);

  if (content_type)
    {
      serf_bucket_headers_setn(hdrs_bkt, "Content-Type", content_type);
    }

  /* These headers need to be sent with every request; see issue #3255
     ("mod_dav_svn does not pass client capabilities to start-commit
     hooks") for why. */
  serf_bucket_headers_set(hdrs_bkt, "DAV", SVN_DAV_NS_DAV_SVN_DEPTH);
  serf_bucket_headers_set(hdrs_bkt, "DAV", SVN_DAV_NS_DAV_SVN_MERGEINFO);
  serf_bucket_headers_set(hdrs_bkt, "DAV", SVN_DAV_NS_DAV_SVN_LOG_REVPROPS);

  /* Setup server authorization headers */
  if (conn->session->auth_protocol)
    SVN_ERR(conn->session->auth_protocol->setup_request_func(conn, method, url,
                                                             hdrs_bkt));

  /* Setup proxy authorization headers */
  if (conn->session->proxy_auth_protocol)
    SVN_ERR(conn->session->proxy_auth_protocol->setup_request_func(conn,
                                                                   method,
                                                                   url,
                                                                   hdrs_bkt));

#if ! SERF_VERSION_AT_LEAST(0, 4, 0)
  /* Set up SSL if we need to */
  if (conn->using_ssl)
    {
      *req_bkt = serf_bucket_ssl_encrypt_create(*req_bkt, conn->ssl_context,
                                            serf_request_get_alloc(request));
      if (!conn->ssl_context)
        {
          conn->ssl_context = serf_bucket_ssl_encrypt_context_get(*req_bkt);

          serf_ssl_client_cert_provider_set(conn->ssl_context,
                                            svn_ra_serf__handle_client_cert,
                                            conn, conn->session->pool);
          serf_ssl_client_cert_password_set(conn->ssl_context,
                                            svn_ra_serf__handle_client_cert_pw,
                                            conn, conn->session->pool);
        }
    }
#endif

  if (ret_hdrs_bkt)
    {
      *ret_hdrs_bkt = hdrs_bkt;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__context_run_wait(svn_boolean_t *done,
                              svn_ra_serf__session_t *sess,
                              apr_pool_t *pool)
{
  apr_status_t status;

  assert(sess->pending_error == SVN_NO_ERROR);

  while (!*done)
    {
      svn_error_t *err;
      int i;

      if (sess->wc_callbacks &&
          sess->wc_callbacks->cancel_func)
        SVN_ERR((sess->wc_callbacks->cancel_func)(sess->wc_callback_baton));

      status = serf_context_run(sess->context, sess->timeout, pool);

      err = sess->pending_error;
      sess->pending_error = SVN_NO_ERROR;

      if (APR_STATUS_IS_TIMEUP(status))
        {
          svn_error_clear(err);
          return svn_error_create(SVN_ERR_RA_DAV_CONN_TIMEOUT,
                                  NULL,
                                  _("Connection timed out"));
        }

      SVN_ERR(err);
      if (status)
        {
          if (status >= SVN_ERR_BAD_CATEGORY_START && status < SVN_ERR_LAST)
            {
              /* apr can't translate subversion errors to text */
              SVN_ERR_W(svn_error_create(status, NULL, NULL),
                        _("Error running context"));
            }

          return svn_error_wrap_apr(status, _("Error running context"));
        }
      /* Debugging purposes only! */
      serf_debug__closed_conn(sess->bkt_alloc);
      for (i = 0; i < sess->num_conns; i++)
        {
         serf_debug__closed_conn(sess->conns[i]->bkt_alloc);
        }
    }

  return SVN_NO_ERROR;
}


/*
 * Expat callback invoked on a start element tag for an error response.
 */
static svn_error_t *
start_error(svn_ra_serf__xml_parser_t *parser,
            void *userData,
            svn_ra_serf__dav_props_t name,
            const char **attrs)
{
  svn_ra_serf__server_error_t *ctx = userData;

  if (!ctx->in_error &&
      strcmp(name.namespace, "DAV:") == 0 &&
      strcmp(name.name, "error") == 0)
    {
      ctx->in_error = TRUE;
    }
  else if (ctx->in_error && strcmp(name.name, "human-readable") == 0)
    {
      const char *err_code;

      err_code = svn_xml_get_attr_value("errcode", attrs);
      if (err_code)
        {
          apr_int64_t val;

          SVN_ERR(svn_cstring_atoi64(&val, err_code));
          ctx->error->apr_err = (apr_status_t)val;
        }
      else
        {
          ctx->error->apr_err = APR_EGENERAL;
        }

      /* Start collecting cdata. */
      svn_stringbuf_setempty(ctx->cdata);
      ctx->collect_cdata = TRUE;
    }

  return SVN_NO_ERROR;
}

/*
 * Expat callback invoked on an end element tag for a PROPFIND response.
 */
static svn_error_t *
end_error(svn_ra_serf__xml_parser_t *parser,
          void *userData,
          svn_ra_serf__dav_props_t name)
{
  svn_ra_serf__server_error_t *ctx = userData;

  if (ctx->in_error &&
      strcmp(name.namespace, "DAV:") == 0 &&
      strcmp(name.name, "error") == 0)
    {
      ctx->in_error = FALSE;
    }
  if (ctx->in_error && strcmp(name.name, "human-readable") == 0)
    {
      /* On the server dav_error_response_tag() will add a leading
         and trailing newline if DEBUG_CR is defined in mod_dav.h,
         so remove any such characters here. */
      apr_size_t len;
      const char *cd = ctx->cdata->data;
      if (*cd == '\n')
        ++cd;
      len = strlen(cd);
      if (len > 0 && cd[len-1] == '\n')
        --len;

      ctx->error->message = apr_pstrmemdup(ctx->error->pool, cd, len);
      ctx->collect_cdata = FALSE;
    }

  return SVN_NO_ERROR;
}

/*
 * Expat callback invoked on CDATA elements in an error response.
 *
 * This callback can be called multiple times.
 */
static svn_error_t *
cdata_error(svn_ra_serf__xml_parser_t *parser,
            void *userData,
            const char *data,
            apr_size_t len)
{
  svn_ra_serf__server_error_t *ctx = userData;

  if (ctx->collect_cdata)
    {
      svn_stringbuf_appendbytes(ctx->cdata, data, len);
    }

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__response_handler_t */
svn_error_t *
svn_ra_serf__handle_discard_body(serf_request_t *request,
                                 serf_bucket_t *response,
                                 void *baton,
                                 apr_pool_t *pool)
{
  apr_status_t status;
  svn_ra_serf__server_error_t *server_err = baton;

  if (server_err)
    {
      if (!server_err->init)
        {
          serf_bucket_t *hdrs;
          const char *val;

          server_err->init = TRUE;
          hdrs = serf_bucket_response_get_headers(response);
          val = serf_bucket_headers_get(hdrs, "Content-Type");
          if (val && strncasecmp(val, "text/xml", sizeof("text/xml") - 1) == 0)
            {
              server_err->error = svn_error_create(APR_SUCCESS, NULL, NULL);
              server_err->has_xml_response = TRUE;
              server_err->contains_precondition_error = FALSE;
              server_err->cdata = svn_stringbuf_create("", pool);
              server_err->collect_cdata = FALSE;
              server_err->parser.pool = server_err->error->pool;
              server_err->parser.user_data = server_err;
              server_err->parser.start = start_error;
              server_err->parser.end = end_error;
              server_err->parser.cdata = cdata_error;
              server_err->parser.done = &server_err->done;
              server_err->parser.ignore_errors = TRUE;
            }
          else
            {
              server_err->error = SVN_NO_ERROR;
            }
        }

      if (server_err->has_xml_response)
        {
          svn_error_t *err = svn_ra_serf__handle_xml_parser(
                                                        request,
                                                        response,
                                                        &server_err->parser,
                                                        pool);

          if (server_err->done && server_err->error->apr_err == APR_SUCCESS)
            {
              svn_error_clear(server_err->error);
              server_err->error = SVN_NO_ERROR;
            }

          return svn_error_return(err);
        }

    }

  status = svn_ra_serf__response_discard_handler(request, response,
                                                 NULL, pool);

  if (status)
    return svn_error_wrap_apr(status, NULL);

  return SVN_NO_ERROR;
}

apr_status_t
svn_ra_serf__response_discard_handler(serf_request_t *request,
                                      serf_bucket_t *response,
                                      void *baton,
                                      apr_pool_t *pool)
{
  /* Just loop through and discard the body. */
  while (1)
    {
      apr_status_t status;
      const char *data;
      apr_size_t len;

      status = serf_bucket_read(response, SERF_READ_ALL_AVAIL, &data, &len);

      if (status)
        {
          return status;
        }

      /* feed me */
    }
}

const char *
svn_ra_serf__response_get_location(serf_bucket_t *response,
                                   apr_pool_t *pool)
{
  serf_bucket_t *headers;
  const char *val;

  headers = serf_bucket_response_get_headers(response);
  val = serf_bucket_headers_get(headers, "Location");
  return val ? svn_urlpath__canonicalize(val, pool) : NULL;
}

/* Implements svn_ra_serf__response_handler_t */
svn_error_t *
svn_ra_serf__handle_status_only(serf_request_t *request,
                                serf_bucket_t *response,
                                void *baton,
                                apr_pool_t *pool)
{
  svn_error_t *err;
  svn_ra_serf__simple_request_context_t *ctx = baton;

  err = svn_ra_serf__handle_discard_body(request, response,
                                         &ctx->server_error, pool);

  if (err && APR_STATUS_IS_EOF(err->apr_err))
    {
      serf_status_line sl;
      apr_status_t rv;

      rv = serf_bucket_response_status(response, &sl);

      ctx->status = sl.code;
      ctx->reason = sl.reason;
      ctx->location = svn_ra_serf__response_get_location(response, pool);
      ctx->done = TRUE;
    }

  return svn_error_return(err);
}

/* Given a string like "HTTP/1.1 500 (status)" in BUF, parse out the numeric
   status code into *STATUS_CODE_OUT.  Ignores leading whitespace. */
static svn_error_t *
parse_dav_status(int *status_code_out, svn_stringbuf_t *buf,
                 apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  const char *token;
  char *tok_status;
  svn_stringbuf_t *temp_buf = svn_stringbuf_dup(buf, scratch_pool);

  svn_stringbuf_strip_whitespace(temp_buf);
  token = apr_strtok(temp_buf->data, " \t\r\n", &tok_status);
  if (token)
    token = apr_strtok(NULL, " \t\r\n", &tok_status);
  if (!token)
    return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                             _("Malformed DAV:status CDATA '%s'"),
                             buf->data);
  err = svn_cstring_atoi(status_code_out, token);
  if (err)
    return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, err,
                             _("Malformed DAV:status CDATA '%s'"),
                             buf->data);

  return SVN_NO_ERROR;
}

/*
 * Expat callback invoked on a start element tag for a 207 response.
 */
static svn_error_t *
start_207(svn_ra_serf__xml_parser_t *parser,
          void *userData,
          svn_ra_serf__dav_props_t name,
          const char **attrs)
{
  svn_ra_serf__server_error_t *ctx = userData;

  if (!ctx->in_error &&
      strcmp(name.namespace, "DAV:") == 0 &&
      strcmp(name.name, "multistatus") == 0)
    {
      ctx->in_error = TRUE;
    }
  else if (ctx->in_error && strcmp(name.name, "responsedescription") == 0)
    {
      /* Start collecting cdata. */
      svn_stringbuf_setempty(ctx->cdata);
      ctx->collect_cdata = TRUE;
    }
  else if (ctx->in_error &&
           strcmp(name.namespace, "DAV:") == 0 &&
           strcmp(name.name, "status") == 0)
    {
      /* Start collecting cdata. */
      svn_stringbuf_setempty(ctx->cdata);
      ctx->collect_cdata = TRUE;
    }

  return SVN_NO_ERROR;
}

/*
 * Expat callback invoked on an end element tag for a 207 response.
 */
static svn_error_t *
end_207(svn_ra_serf__xml_parser_t *parser,
        void *userData,
        svn_ra_serf__dav_props_t name)
{
  svn_ra_serf__server_error_t *ctx = userData;

  if (ctx->in_error &&
      strcmp(name.namespace, "DAV:") == 0 &&
      strcmp(name.name, "multistatus") == 0)
    {
      ctx->in_error = FALSE;
    }
  if (ctx->in_error && strcmp(name.name, "responsedescription") == 0)
    {
      ctx->collect_cdata = FALSE;
      ctx->error->message = apr_pstrmemdup(ctx->error->pool, ctx->cdata->data,
                                           ctx->cdata->len);
      if (ctx->contains_precondition_error)
        ctx->error->apr_err = SVN_ERR_FS_PROP_BASEVALUE_MISMATCH;
      else
        ctx->error->apr_err = SVN_ERR_RA_DAV_REQUEST_FAILED;
    }
  else if (ctx->in_error &&
           strcmp(name.namespace, "DAV:") == 0 &&
           strcmp(name.name, "status") == 0)
    {
      int status_code;

      ctx->collect_cdata = FALSE;

      SVN_ERR(parse_dav_status(&status_code, ctx->cdata, parser->pool));
      if (status_code == 412)
        ctx->contains_precondition_error = TRUE;
    }

  return SVN_NO_ERROR;
}

/*
 * Expat callback invoked on CDATA elements in a 207 response.
 *
 * This callback can be called multiple times.
 */
static svn_error_t *
cdata_207(svn_ra_serf__xml_parser_t *parser,
          void *userData,
          const char *data,
          apr_size_t len)
{
  svn_ra_serf__server_error_t *ctx = userData;

  if (ctx->collect_cdata)
    {
      svn_stringbuf_appendbytes(ctx->cdata, data, len);
    }

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__response_handler_t */
svn_error_t *
svn_ra_serf__handle_multistatus_only(serf_request_t *request,
                                     serf_bucket_t *response,
                                     void *baton,
                                     apr_pool_t *pool)
{
  svn_error_t *err;
  svn_ra_serf__simple_request_context_t *ctx = baton;
  svn_ra_serf__server_error_t *server_err = &ctx->server_error;

  /* If necessary, initialize our XML parser. */
  if (server_err && !server_err->init)
    {
      serf_bucket_t *hdrs;
      const char *val;

      server_err->init = TRUE;
      hdrs = serf_bucket_response_get_headers(response);
      val = serf_bucket_headers_get(hdrs, "Content-Type");
      if (val && strncasecmp(val, "text/xml", sizeof("text/xml") - 1) == 0)
        {
          server_err->error = svn_error_create(APR_SUCCESS, NULL, NULL);
          server_err->has_xml_response = TRUE;
          server_err->contains_precondition_error = FALSE;
          server_err->cdata = svn_stringbuf_create("", pool);
          server_err->collect_cdata = FALSE;
          server_err->parser.pool = server_err->error->pool;
          server_err->parser.user_data = server_err;
          server_err->parser.start = start_207;
          server_err->parser.end = end_207;
          server_err->parser.cdata = cdata_207;
          server_err->parser.done = &ctx->done;
          server_err->parser.ignore_errors = TRUE;
        }
      else
        {
          ctx->done = TRUE;
          server_err->error = SVN_NO_ERROR;
        }
    }

  /* If server_err->error still contains APR_SUCCESS, it means that we
     have not successfully parsed the XML yet. */
  if (server_err && server_err->error
      && server_err->error->apr_err == APR_SUCCESS)
    {
      err = svn_ra_serf__handle_xml_parser(request, response,
                                           &server_err->parser, pool);

      /* APR_EOF will be returned when parsing is complete.  If we see
         any other error, return it immediately.  In practice the only
         other error we expect to see is APR_EAGAIN, which indicates that
         we could not parse the XML because the contents are not yet
         available to be read. */
      if (!err || !APR_STATUS_IS_EOF(err->apr_err))
        {
          return svn_error_return(err);
        }
      else if (ctx->done && server_err->error->apr_err == APR_SUCCESS)
        {
          svn_error_clear(server_err->error);
          server_err->error = SVN_NO_ERROR;
        }

      svn_error_clear(err);
    }

  err = svn_ra_serf__handle_discard_body(request, response, NULL, pool);

  if (err && APR_STATUS_IS_EOF(err->apr_err))
    {
      serf_status_line sl;
      apr_status_t rv;

      rv = serf_bucket_response_status(response, &sl);

      ctx->status = sl.code;
      ctx->reason = sl.reason;
      ctx->location = svn_ra_serf__response_get_location(response, pool);
    }

  return svn_error_return(err);
}

static void
start_xml(void *userData, const char *raw_name, const char **attrs)
{
  svn_ra_serf__xml_parser_t *parser = userData;
  svn_ra_serf__dav_props_t name;

  if (parser->error)
    return;

  if (!parser->state)
    svn_ra_serf__xml_push_state(parser, 0);

  svn_ra_serf__define_ns(&parser->state->ns_list, attrs, parser->state->pool);

  svn_ra_serf__expand_ns(&name, parser->state->ns_list, raw_name);

  parser->error = parser->start(parser, parser->user_data, name, attrs);
}

static void
end_xml(void *userData, const char *raw_name)
{
  svn_ra_serf__xml_parser_t *parser = userData;
  svn_ra_serf__dav_props_t name;

  if (parser->error)
    return;

  svn_ra_serf__expand_ns(&name, parser->state->ns_list, raw_name);

  parser->error = parser->end(parser, parser->user_data, name);
}

static void
cdata_xml(void *userData, const char *data, int len)
{
  svn_ra_serf__xml_parser_t *parser = userData;

  if (parser->error)
    return;

  if (!parser->state)
    svn_ra_serf__xml_push_state(parser, 0);

  parser->error = parser->cdata(parser, parser->user_data, data, len);
}

/* Implements svn_ra_serf__response_handler_t */
svn_error_t *
svn_ra_serf__handle_xml_parser(serf_request_t *request,
                               serf_bucket_t *response,
                               void *baton,
                               apr_pool_t *pool)
{
  const char *data;
  apr_size_t len;
  serf_status_line sl;
  apr_status_t status;
  int xml_status;
  svn_ra_serf__xml_parser_t *ctx = baton;
  svn_error_t *err;

  serf_bucket_response_status(response, &sl);

  if (ctx->status_code)
    {
      *ctx->status_code = sl.code;
    }

  if (sl.code == 301 || sl.code == 302 || sl.code == 307)
    {
      ctx->location = svn_ra_serf__response_get_location(response, pool);
    }

  /* Woo-hoo.  Nothing here to see.  */
  if (sl.code == 404 && ctx->ignore_errors == FALSE)
    {
      /* If our caller won't know about the 404, abort() for now. */
      SVN_ERR_ASSERT(ctx->status_code);

      if (*ctx->done == FALSE)
        {
          *ctx->done = TRUE;
          if (ctx->done_list)
            {
              ctx->done_item->data = ctx->user_data;
              ctx->done_item->next = *ctx->done_list;
              *ctx->done_list = ctx->done_item;
            }
        }

      err = svn_ra_serf__handle_server_error(request, response, pool);

      SVN_ERR(svn_error_compose_create(
        svn_ra_serf__handle_discard_body(request, response, NULL, pool),
        err));
      return SVN_NO_ERROR;
    }

  if (!ctx->xmlp)
    {
      ctx->xmlp = XML_ParserCreate(NULL);
      XML_SetUserData(ctx->xmlp, ctx);
      XML_SetElementHandler(ctx->xmlp, start_xml, end_xml);
      if (ctx->cdata)
        {
          XML_SetCharacterDataHandler(ctx->xmlp, cdata_xml);
        }
    }

  while (1)
    {
      status = serf_bucket_read(response, 8000, &data, &len);

      if (SERF_BUCKET_READ_ERROR(status))
        {
          return svn_error_wrap_apr(status, NULL);
        }

      xml_status = XML_Parse(ctx->xmlp, data, len, 0);
      if (xml_status == XML_STATUS_ERROR && ctx->ignore_errors == FALSE)
        {
          XML_ParserFree(ctx->xmlp);

          SVN_ERR_ASSERT(ctx->status_code);

          if (*ctx->done == FALSE)
            {
              *ctx->done = TRUE;
              if (ctx->done_list)
                {
                  ctx->done_item->data = ctx->user_data;
                  ctx->done_item->next = *ctx->done_list;
                  *ctx->done_list = ctx->done_item;
                }
            }
          SVN_ERR(svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                    _("XML parsing failed: (%d %s)"),
                                    sl.code, sl.reason));
        }

      if (ctx->error && ctx->ignore_errors == FALSE)
        {
          XML_ParserFree(ctx->xmlp);
          SVN_ERR(ctx->error);
        }

      if (APR_STATUS_IS_EAGAIN(status))
        {
          return svn_error_wrap_apr(status, NULL);
        }

      if (APR_STATUS_IS_EOF(status))
        {
          xml_status = XML_Parse(ctx->xmlp, NULL, 0, 1);
          XML_ParserFree(ctx->xmlp);

          *ctx->done = TRUE;
          if (ctx->done_list)
            {
              ctx->done_item->data = ctx->user_data;
              ctx->done_item->next = *ctx->done_list;
              *ctx->done_list = ctx->done_item;
            }
          return svn_error_wrap_apr(status, NULL);
        }

      /* feed me! */
    }
  /* not reached */
}

/* Implements svn_ra_serf__response_handler_t */
svn_error_t *
svn_ra_serf__handle_server_error(serf_request_t *request,
                                 serf_bucket_t *response,
                                 apr_pool_t *pool)
{
  svn_ra_serf__server_error_t server_err = { 0 };

  svn_error_clear(svn_ra_serf__handle_discard_body(request, response,
                                                   &server_err, pool));

  return server_err.error;
}

apr_status_t
svn_ra_serf__credentials_callback(char **username, char **password,
                                  serf_request_t *request, void *baton,
                                  int code, const char *authn_type,
                                  const char *realm,
                                  apr_pool_t *pool)
{
  svn_ra_serf__handler_t *ctx = baton;
  svn_ra_serf__session_t *session = ctx->session;
  void *creds;
  svn_auth_cred_simple_t *simple_creds;
  svn_error_t *err;

  if (code == 401)
    {
      /* Use svn_auth_first_credentials if this is the first time we ask for
         credentials during this session OR if the last time we asked
         session->auth_state wasn't set (eg. if the credentials provider was
         cancelled by the user). */
      if (!session->auth_state)
        {
          err = svn_auth_first_credentials(&creds,
                                           &session->auth_state,
                                           SVN_AUTH_CRED_SIMPLE,
                                           realm,
                                           session->wc_callbacks->auth_baton,
                                           session->pool);
        }
      else
        {
          err = svn_auth_next_credentials(&creds,
                                          session->auth_state,
                                          session->pool);
        }

      if (err)
        {
          session->pending_error
              = svn_error_compose_create(session->pending_error, err);
          return err->apr_err;
        }

      session->auth_attempts++;

      if (!creds || session->auth_attempts > 4)
        {
          /* No more credentials. */
          session->pending_error
              = svn_error_compose_create(
                    session->pending_error,
                    svn_error_create(
                          SVN_ERR_AUTHN_FAILED, NULL,
                          _("No more credentials or we tried too many times.\n"
                            "Authentication failed")));
          return SVN_ERR_AUTHN_FAILED;
        }

      simple_creds = creds;
      *username = apr_pstrdup(pool, simple_creds->username);
      *password = apr_pstrdup(pool, simple_creds->password);
    }
  else
    {
      *username = apr_pstrdup(pool, session->proxy_username);
      *password = apr_pstrdup(pool, session->proxy_password);

      session->proxy_auth_attempts++;

      if (!session->proxy_username || session->proxy_auth_attempts > 4)
        {
          /* No more credentials. */
          session->pending_error
              = svn_error_compose_create(
                      ctx->session->pending_error,
                      svn_error_create(SVN_ERR_AUTHN_FAILED, NULL,
                                       _("Proxy authentication failed")));
          return SVN_ERR_AUTHN_FAILED;
        }
    }

  ctx->conn->last_status_code = code;

  return APR_SUCCESS;
}

/* Wait for HTTP response status and headers, and invoke CTX->
   response_handler() to carry out operation-specific processing.
   Afterwards, check for connection close.

   SERF_STATUS allows returning errors to serf without creating a
   subversion error object.
   */
static svn_error_t *
handle_response(serf_request_t *request,
                serf_bucket_t *response,
                svn_ra_serf__handler_t *ctx,
                apr_status_t *serf_status,
                apr_pool_t *pool)
{
  serf_status_line sl;
  apr_status_t status;

  if (!response)
    {
      /* Uh-oh.  Our connection died.  Requeue. */
      if (ctx->response_error)
        SVN_ERR(ctx->response_error(request, response, 0,
                                    ctx->response_error_baton));

      svn_ra_serf__request_create(ctx);

      return APR_SUCCESS;
    }

  status = serf_bucket_response_status(response, &sl);
  if (SERF_BUCKET_READ_ERROR(status))
    {
      *serf_status = status;
      return SVN_NO_ERROR; /* Handled by serf */
    }
  if (!sl.version && (APR_STATUS_IS_EOF(status) ||
                      APR_STATUS_IS_EAGAIN(status)))
    {
      *serf_status = status;
      return SVN_NO_ERROR; /* Handled by serf */
    }

  status = serf_bucket_response_wait_for_headers(response);
  if (status)
    {
      if (!APR_STATUS_IS_EOF(status))
        {
          *serf_status = status;
          return SVN_NO_ERROR;
        }

      /* Cases where a lack of a response body (via EOF) is okay:
       *  - A HEAD request
       *  - 204/304 response
       *
       * Otherwise, if we get an EOF here, something went really wrong: either
       * the server closed on us early or we're reading too much.  Either way,
       * scream loudly.
       */
      if (strcmp(ctx->method, "HEAD") != 0 && sl.code != 204 && sl.code != 304)
        {
          svn_error_t *err =
              svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA,
                                svn_error_wrap_apr(status, NULL),
                                _("Premature EOF seen from server "
                                  "(http status=%d)"), sl.code);
          /* This discard may be no-op, but let's preserve the algorithm
             used elsewhere in this function for clarity's sake. */
          svn_ra_serf__response_discard_handler(request, response, NULL, pool);
          return err;
        }
    }

  if (ctx->conn->last_status_code == 401 && sl.code < 400)
    {
      SVN_ERR(svn_auth_save_credentials(ctx->session->auth_state,
                                        ctx->session->pool));
      ctx->session->auth_attempts = 0;
      ctx->session->auth_state = NULL;
      ctx->session->realm = NULL;
    }

  ctx->conn->last_status_code = sl.code;

  if (sl.code == 401 || sl.code == 407)
    {
      /* 401 Authorization or 407 Proxy-Authentication required */
      status = svn_ra_serf__response_discard_handler(request, response, NULL, pool);

      /* Don't bother handling the authentication request if the response
         wasn't received completely yet. Serf will call handle_response
         again when more data is received. */
      if (APR_STATUS_IS_EAGAIN(status))
        {
          *serf_status = status;
          return SVN_NO_ERROR;
        }

      SVN_ERR(svn_ra_serf__handle_auth(sl.code, ctx,
                                       request, response, pool));

      svn_ra_serf__priority_request_create(ctx);

      *serf_status = status;
      return SVN_NO_ERROR;
    }
  else if (sl.code == 409 || sl.code >= 500)
    {
      /* 409 Conflict: can indicate a hook error.
         5xx (Internal) Server error. */
      SVN_ERR(svn_ra_serf__handle_server_error(request, response, pool));

      if (!ctx->session->pending_error)
        {
          return
              svn_error_createf(APR_EGENERAL, NULL,
              _("Unspecified error message: %d %s"), sl.code, sl.reason);
        }

      return SVN_NO_ERROR; /* Error is set in caller */
    }
  else
    {
      svn_error_t *err;

      /* Validate this response message. */
      if (ctx->session->auth_protocol ||
          ctx->session->proxy_auth_protocol)
        {
          const svn_ra_serf__auth_protocol_t *prot;

          if (ctx->session->auth_protocol)
            prot = ctx->session->auth_protocol;
          else
            prot = ctx->session->proxy_auth_protocol;

          err = prot->validate_response_func(ctx, request, response, pool);
          if (err)
            {
              svn_ra_serf__response_discard_handler(request, response, NULL,
                                                    pool);
              /* Ignore serf status code, just return the real error */

              return svn_error_return(err);
            }
        }

      err = ctx->response_handler(request,response, ctx->response_baton, pool);

      if (err
          && (!SERF_BUCKET_READ_ERROR(err->apr_err)
               || APR_STATUS_IS_ECONNRESET(err->apr_err)))
        {
          /* These errors are special cased in serf
             ### We hope no handler returns these by accident. */
          *serf_status = err->apr_err;
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }

      return svn_error_return(err);
    }
}


/* Implements serf_response_handler_t for handle_response. Storing
   errors in ctx->session->pending_error if appropriate. */
static apr_status_t
handle_response_cb(serf_request_t *request,
                   serf_bucket_t *response,
                   void *baton,
                   apr_pool_t *pool)
{
  svn_ra_serf__handler_t *ctx = baton;
  svn_ra_serf__session_t *session = ctx->session;
  svn_error_t *err;
  apr_status_t serf_status = APR_SUCCESS;

  err = svn_error_return(
          handle_response(request, response, ctx, &serf_status, pool));

  if (err || session->pending_error)
    {
      session->pending_error = svn_error_compose_create(session->pending_error,
                                                        err);

      serf_status = session->pending_error->apr_err;
    }

  return serf_status;
}

/* If the CTX->setup() callback is non-NULL, invoke it to carry out the
   majority of the serf_request_setup_t implementation.  Otherwise, perform
   default setup, with special handling for HEAD requests, and finer-grained
   callbacks invoked (if non-NULL) to produce the request headers and
   body. */
static svn_error_t *
setup_request(serf_request_t *request,
                 svn_ra_serf__handler_t *ctx,
                 serf_bucket_t **req_bkt,
                 serf_response_acceptor_t *acceptor,
                 void **acceptor_baton,
                 serf_response_handler_t *handler,
                 void **handler_baton,
                 apr_pool_t *pool)
{
  serf_bucket_t *headers_bkt;

  *acceptor = svn_ra_serf__accept_response;
  *acceptor_baton = ctx->session;

  if (ctx->setup)
    {
      svn_ra_serf__response_handler_t response_handler;
      void *response_baton;

      SVN_ERR(ctx->setup(request, ctx->setup_baton, req_bkt,
                         acceptor, acceptor_baton,
                         &response_handler, &response_baton,
                         pool));

      ctx->response_handler = response_handler;
      ctx->response_baton = response_baton;
    }
  else
    {
      serf_bucket_t *body_bkt;
      serf_bucket_alloc_t *bkt_alloc = serf_request_get_alloc(request);

      if (strcmp(ctx->method, "HEAD") == 0)
        {
          *acceptor = accept_head;
        }

      if (ctx->body_delegate)
        {
          SVN_ERR(ctx->body_delegate(&body_bkt, ctx->body_delegate_baton,
                                     bkt_alloc, pool));
        }
      else
        {
          body_bkt = NULL;
        }

      SVN_ERR(svn_ra_serf__setup_serf_req(request, req_bkt, &headers_bkt,
                                          ctx->conn, ctx->method, ctx->path,
                                          body_bkt, ctx->body_type));

      if (ctx->header_delegate)
        {
          SVN_ERR(ctx->header_delegate(headers_bkt, ctx->header_delegate_baton,
                                       pool));
        }
    }

  *handler = handle_response_cb;
  *handler_baton = ctx;

  return APR_SUCCESS;
}

/* Implements the serf_request_setup_t interface (which sets up both a
   request and its response handler callback). Handles errors for
   setup_request_cb */
static apr_status_t
setup_request_cb(serf_request_t *request,
              void *setup_baton,
              serf_bucket_t **req_bkt,
              serf_response_acceptor_t *acceptor,
              void **acceptor_baton,
              serf_response_handler_t *handler,
              void **handler_baton,
              apr_pool_t *pool)
{
  svn_ra_serf__handler_t *ctx = setup_baton;
  svn_error_t *err;

  err = setup_request(request, ctx,
                      req_bkt,
                      acceptor, acceptor_baton,
                      handler, handler_baton,
                      pool);

  if (err)
    {
      ctx->session->pending_error
                = svn_error_compose_create(ctx->session->pending_error,
                                           err);

      return err->apr_err;
    }

  return APR_SUCCESS;
}

serf_request_t *
svn_ra_serf__request_create(svn_ra_serf__handler_t *handler)
{
  return serf_connection_request_create(handler->conn->conn,
                                        setup_request_cb, handler);
}

serf_request_t *
svn_ra_serf__priority_request_create(svn_ra_serf__handler_t *handler)
{
  return serf_connection_priority_request_create(handler->conn->conn,
                                                 setup_request_cb, handler);
}

svn_error_t *
svn_ra_serf__discover_vcc(const char **vcc_url,
                          svn_ra_serf__session_t *session,
                          svn_ra_serf__connection_t *conn,
                          apr_pool_t *pool)
{
  apr_hash_t *props;
  const char *path, *relative_path, *uuid;

  /* If we've already got the information our caller seeks, just return it.  */
  if (session->vcc_url && session->repos_root_str)
    {
      *vcc_url = session->vcc_url;
      return SVN_NO_ERROR;
    }

  /* If no connection is provided, use the default one. */
  if (! conn)
    {
      conn = session->conns[0];
    }

  props = apr_hash_make(pool);
  path = session->repos_url.path;
  *vcc_url = NULL;
  uuid = NULL;

  do
    {
      svn_error_t *err = svn_ra_serf__retrieve_props(props, session, conn,
                                                     path, SVN_INVALID_REVNUM,
                                                     "0", base_props, pool);
      if (! err)
        {
          *vcc_url =
              svn_ra_serf__get_ver_prop(props, path,
                                        SVN_INVALID_REVNUM,
                                        "DAV:",
                                        "version-controlled-configuration");

          relative_path = svn_ra_serf__get_ver_prop(props, path,
                                                    SVN_INVALID_REVNUM,
                                                    SVN_DAV_PROP_NS_DAV,
                                                    "baseline-relative-path");

          uuid = svn_ra_serf__get_ver_prop(props, path,
                                           SVN_INVALID_REVNUM,
                                           SVN_DAV_PROP_NS_DAV,
                                           "repository-uuid");
          break;
        }
      else
        {
          if (err->apr_err != SVN_ERR_FS_NOT_FOUND)
            {
              return err;  /* found a _real_ error */
            }
          else
            {
              /* This happens when the file is missing in HEAD. */
              svn_error_clear(err);

              /* Okay, strip off a component from PATH. */
              path = svn_urlpath__dirname(path, pool);
            }
        }
    }
  while ((path[0] != '\0')
         && (! (path[0] == '/') && (path[1] == '\0')));

  if (!*vcc_url)
    {
      return svn_error_create(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
                              _("The OPTIONS response did not include the "
                                "requested version-controlled-configuration "
                                "value"));
    }

  /* Store our VCC in our cache. */
  if (!session->vcc_url)
    {
      session->vcc_url = apr_pstrdup(session->pool, *vcc_url);
    }

  /* Update our cached repository root URL. */
  if (!session->repos_root_str)
    {
      svn_stringbuf_t *url_buf;

      url_buf = svn_stringbuf_create(path, pool);

      svn_path_remove_components(url_buf,
                                 svn_path_component_count(relative_path));

      /* Now recreate the root_url. */
      session->repos_root = session->repos_url;
      session->repos_root.path = apr_pstrdup(session->pool, url_buf->data);
      session->repos_root_str =
        svn_urlpath__canonicalize(apr_uri_unparse(session->pool,
                                                  &session->repos_root, 0),
                                  session->pool);
    }

  /* Store the repository UUID in the cache. */
  if (!session->uuid)
    {
      session->uuid = apr_pstrdup(session->pool, uuid);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_relative_path(const char **rel_path,
                               const char *orig_path,
                               svn_ra_serf__session_t *session,
                               svn_ra_serf__connection_t *conn,
                               apr_pool_t *pool)
{
  const char *decoded_root, *decoded_orig;

  if (! session->repos_root.path)
    {
      const char *vcc_url;

      /* This should only happen if we haven't detected HTTP v2
         support from the server.  */
      assert(! SVN_RA_SERF__HAVE_HTTPV2_SUPPORT(session));

      /* We don't actually care about the VCC_URL, but this API
         promises to populate the session's root-url cache, and that's
         what we really want. */
      SVN_ERR(svn_ra_serf__discover_vcc(&vcc_url, session,
                                        conn ? conn : session->conns[0],
                                        pool));
    }

  decoded_root = svn_path_uri_decode(session->repos_root.path, pool);
  decoded_orig = svn_path_uri_decode(orig_path, pool);
  if (strcmp(decoded_root, decoded_orig) == 0)
    {
      *rel_path = "";
    }
  else
    {
      *rel_path = svn_urlpath__is_child(decoded_root, decoded_orig, pool);
      SVN_ERR_ASSERT(*rel_path != NULL);
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__report_resource(const char **report_target,
                             svn_ra_serf__session_t *session,
                             svn_ra_serf__connection_t *conn,
                             apr_pool_t *pool)
{
  /* If we have HTTP v2 support, we want to report against the 'me'
     resource. */
  if (SVN_RA_SERF__HAVE_HTTPV2_SUPPORT(session))
    *report_target = apr_pstrdup(pool, session->me_resource);

  /* Otherwise, we'll use the default VCC. */
  else
    SVN_ERR(svn_ra_serf__discover_vcc(report_target, session, conn, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__error_on_status(int status_code,
                             const char *path,
                             const char *location)
{
  switch(status_code)
    {
      case 301:
      case 302:
      case 307:
        return svn_error_createf(SVN_ERR_RA_DAV_RELOCATED, NULL,
                                 (status_code == 301)
                                 ? _("Repository moved permanently to '%s';"
                                     " please relocate")
                                 : _("Repository moved temporarily to '%s';"
                                     " please relocate"), location);
      case 404:
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("'%s' path not found"), path);
      case 423:
        return svn_error_createf(SVN_ERR_FS_NO_LOCK_TOKEN, NULL,
                                 _("'%s': no lock token available"), path);
    }

  return SVN_NO_ERROR;
}
