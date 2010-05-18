/*
 * util.c : serf utility routines for ra_serf
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



#define APR_WANT_STRFUNC
#include <apr.h>
#include <apr_want.h>
#include <apr_fnmatch.h>

#include <serf.h>
#include <serf_bucket_types.h>

#include "svn_path.h"
#include "svn_private_config.h"
#include "svn_xml.h"
#include "private/svn_dep_compat.h"

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

/* Callback that implements serf_ssl_need_server_cert_t. This function is
   called on receiving a ssl certificate of a server when opening a https
   connection. It allows Subversion to override the initial validation done
   by serf.
   Serf provides us the @a baton as provided in the call to
   serf_ssl_server_cert_callback_set. The result of serf's initial validation
   of the certificate @a CERT is returned as a bitmask in FAILURES. */
static apr_status_t
ssl_server_cert(void *baton, int failures,
                const serf_ssl_certificate_t *cert)
{
  svn_ra_serf__connection_t *conn = baton;
  apr_pool_t *subpool;
  svn_auth_ssl_server_cert_info_t cert_info;
  svn_auth_cred_ssl_server_trust_t *server_creds = NULL;
  svn_auth_iterstate_t *state;
  const char *realmstring;
  apr_uint32_t svn_failures;
  svn_error_t *err;
  apr_hash_t *issuer, *subject, *serf_cert;
  void *creds;

  /* Implicitly approve any non-server certs. */
  if (serf_ssl_cert_depth(cert) > 0)
    {
      return APR_SUCCESS;
    }

  apr_pool_create(&subpool, conn->session->pool);

  /* Extract the info from the certificate */
  subject = serf_ssl_cert_subject(cert, subpool);
  issuer = serf_ssl_cert_issuer(cert, subpool);
  serf_cert = serf_ssl_cert_certificate(cert, subpool);

  cert_info.hostname = apr_hash_get(subject, "CN", APR_HASH_KEY_STRING);
  cert_info.fingerprint = apr_hash_get(serf_cert, "sha1", APR_HASH_KEY_STRING);
  if (! cert_info.fingerprint)
    cert_info.fingerprint = apr_pstrdup(subpool, "<unknown>");
  cert_info.valid_from = apr_hash_get(serf_cert, "notBefore",
                         APR_HASH_KEY_STRING);
  if (! cert_info.valid_from)
    cert_info.valid_from = apr_pstrdup(subpool, "[invalid date]");
  cert_info.valid_until = apr_hash_get(serf_cert, "notAfter",
                          APR_HASH_KEY_STRING);
  if (! cert_info.valid_until)
    cert_info.valid_until = apr_pstrdup(subpool, "[invalid date]");
  cert_info.issuer_dname = convert_organisation_to_str(issuer, subpool);
  cert_info.ascii_cert = serf_ssl_cert_export(cert, subpool);

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

  /* Construct the realmstring, e.g. https://svn.collab.net:443 */
  realmstring = apr_uri_unparse(subpool, &conn->session->repos_url,
                                APR_URI_UNP_OMITPATHINFO);

  err = svn_auth_first_credentials(&creds, &state,
                                   SVN_AUTH_CRED_SSL_SERVER_TRUST,
                                   realmstring,
                                   conn->session->wc_callbacks->auth_baton,
                                   subpool);
  if (err || ! creds)
    {
      svn_error_clear(err);
    }
  else
    {
      server_creds = creds;
      err = svn_auth_save_credentials(state, subpool);
      if (err)
        {
          /* It would be nice to show the error to the user somehow... */
          svn_error_clear(err);
        }
    }

  svn_auth_set_parameter(conn->session->wc_callbacks->auth_baton,
                         SVN_AUTH_PARAM_SSL_SERVER_CERT_INFO, NULL);

  svn_pool_destroy(subpool);

  return server_creds ? APR_SUCCESS : SVN_ERR_RA_SERF_SSL_CERT_UNTRUSTED;
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
             svn_path_local_style(file, pool));
        }
      files = NULL;
    }

  return SVN_NO_ERROR;
}

serf_bucket_t *
svn_ra_serf__conn_setup(apr_socket_t *sock,
                        void *baton,
                        apr_pool_t *pool)
{
  serf_bucket_t *bucket;
  svn_ra_serf__connection_t *conn = baton;

  bucket = serf_context_bucket_socket_create(conn->session->context,
                                             sock, conn->bkt_alloc);

  if (conn->using_ssl)
    {
      bucket = serf_bucket_ssl_decrypt_create(bucket, conn->ssl_context,
                                              conn->bkt_alloc);
      if (!conn->ssl_context)
        {
          conn->ssl_context = serf_bucket_ssl_decrypt_context_get(bucket);

          serf_ssl_client_cert_provider_set(conn->ssl_context,
                                            svn_ra_serf__handle_client_cert,
                                            conn, conn->session->pool);
          serf_ssl_client_cert_password_set(conn->ssl_context,
                                            svn_ra_serf__handle_client_cert_pw,
                                            conn, conn->session->pool);

          serf_ssl_server_cert_callback_set(conn->ssl_context,
                                            ssl_server_cert,
                                            conn);
          /* See if the user wants us to trust "default" openssl CAs. */
          if (conn->session->trust_default_ca)
            {
              serf_ssl_use_default_certificates(conn->ssl_context);
            }
          /* Are there custom CAs to load? */
          if (conn->session->ssl_authorities)
            {
              svn_error_t *err;
              err = load_authorities(conn, conn->session->ssl_authorities,
                                     conn->session->pool);
              if (err)
                {
                  /* TODO: we need a way to pass this error back to the
                     caller */
                  svn_error_clear(err);
                }
            }
        }
    }

  return bucket;
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

void
svn_ra_serf__conn_closed(serf_connection_t *conn,
                         void *closed_baton,
                         apr_status_t why,
                         apr_pool_t *pool)
{
  svn_ra_serf__connection_t *our_conn = closed_baton;

  if (why)
    {
      SVN_ERR_MALFUNCTION_NO_RETURN();
    }

  if (our_conn->using_ssl)
    {
      our_conn->ssl_context = NULL;
    }
  /* Restart the authentication phase on this new connection. */
  if (our_conn->session->auth_protocol)
    {
      our_conn->session->auth_protocol->init_conn_func(our_conn->session,
                                                       our_conn,
                                                       our_conn->session->pool);
    }
}

apr_status_t
svn_ra_serf__cleanup_serf_session(void *data)
{
  /* svn_ra_serf__session_t *serf_sess = data; */

  /* Nothing to do. */

  return APR_SUCCESS;
}

apr_status_t svn_ra_serf__handle_client_cert(void *data,
                                             const char **cert_path)
{
    svn_ra_serf__connection_t *conn = data;
    svn_ra_serf__session_t *session = conn->session;
    const char *realm;
    apr_port_t port;
    svn_error_t *err;
    void *creds;

    *cert_path = NULL;

    if (session->repos_url.port_str)
      {
        port = session->repos_url.port;
      }
    else
      {
        port = apr_uri_port_of_scheme(session->repos_url.scheme);
      }

    realm = apr_psprintf(session->pool, "%s://%s:%d",
                         session->repos_url.scheme,
                         session->repos_url.hostname,
                         port);

    if (!conn->ssl_client_auth_state)
      {
        err = svn_auth_first_credentials(&creds,
                                         &conn->ssl_client_auth_state,
                                         SVN_AUTH_CRED_SSL_CLIENT_CERT,
                                         realm,
                                         session->wc_callbacks->auth_baton,
                                         session->pool);
      }
    else
      {
        err = svn_auth_next_credentials(&creds,
                                        conn->ssl_client_auth_state,
                                        session->pool);
      }

    if (err)
      {
        session->pending_error = err;
        return err->apr_err;
      }

    if (creds)
      {
        svn_auth_cred_ssl_client_cert_t *client_creds;
        client_creds = creds;
        *cert_path = client_creds->cert_file;
      }

    return APR_SUCCESS;
}

apr_status_t svn_ra_serf__handle_client_cert_pw(void *data,
                                                const char *cert_path,
                                                const char **password)
{
    svn_ra_serf__connection_t *conn = data;
    svn_ra_serf__session_t *session = conn->session;
    svn_error_t *err;
    void *creds;

    *password = NULL;

    if (!conn->ssl_client_pw_auth_state)
      {
        err = svn_auth_first_credentials(&creds,
                                         &conn->ssl_client_pw_auth_state,
                                         SVN_AUTH_CRED_SSL_CLIENT_CERT_PW,
                                         cert_path,
                                         session->wc_callbacks->auth_baton,
                                         session->pool);
      }
    else
      {
        err = svn_auth_next_credentials(&creds,
                                        conn->ssl_client_pw_auth_state,
                                        session->pool);
      }

    if (err)
      {
        session->pending_error = err;
        return err->apr_err;
      }

    if (creds)
      {
        svn_auth_cred_ssl_client_cert_pw_t *pw_creds;
        pw_creds = creds;
        *password = pw_creds->password;
      }

    return APR_SUCCESS;
}

void
svn_ra_serf__setup_serf_req(serf_request_t *request,
                            serf_bucket_t **req_bkt,
                            serf_bucket_t **ret_hdrs_bkt,
                            svn_ra_serf__connection_t *conn,
                            const char *method, const char *url,
                            serf_bucket_t *body_bkt, const char *content_type)
{
  serf_bucket_t *hdrs_bkt;

  *req_bkt = serf_bucket_request_create(method, url, body_bkt,
                                        serf_request_get_alloc(request));

  hdrs_bkt = serf_bucket_request_get_headers(*req_bkt);
  serf_bucket_headers_setn(hdrs_bkt, "Host", conn->hostinfo);
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
    conn->session->auth_protocol->setup_request_func(conn, hdrs_bkt);

  /* Setup proxy authorization headers */
  if (conn->session->proxy_auth_protocol)
    conn->session->proxy_auth_protocol->setup_request_func(conn, hdrs_bkt);

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

  /* Set up Proxy settings */
  if (conn->session->using_proxy)
    {
      char *root = apr_uri_unparse(conn->session->pool,
                                   &conn->session->repos_url,
                                   APR_URI_UNP_OMITPATHINFO);
      serf_bucket_request_set_root(*req_bkt, root);
    }

  if (ret_hdrs_bkt)
    {
      *ret_hdrs_bkt = hdrs_bkt;
    }
}

svn_error_t *
svn_ra_serf__context_run_wait(svn_boolean_t *done,
                              svn_ra_serf__session_t *sess,
                              apr_pool_t *pool)
{
  apr_status_t status;

  sess->pending_error = SVN_NO_ERROR;

  while (!*done)
    {
      int i;

      if (sess->wc_callbacks &&
          sess->wc_callbacks->cancel_func)
        SVN_ERR((sess->wc_callbacks->cancel_func)(sess->wc_callback_baton));

      status = serf_context_run(sess->context, SERF_DURATION_FOREVER, pool);
      if (APR_STATUS_IS_TIMEUP(status))
        {
          continue;
        }
      if (sess->pending_error)
        {
          return sess->pending_error;
        }
      if (status)
        {
          return svn_error_wrap_apr(status, "Error running context");
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
          ctx->error->apr_err = apr_atoi64(err_code);
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

apr_status_t
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
          status = svn_ra_serf__handle_xml_parser(request, response,
                                                  &server_err->parser, pool);

          if (server_err->done && server_err->error->apr_err == APR_SUCCESS)
            {
              svn_error_clear(server_err->error);
              server_err->error = SVN_NO_ERROR;
            }

          return status;
        }

    }

  /* Just loop through and discard the body. */
  while (1)
    {
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

apr_status_t
svn_ra_serf__handle_status_only(serf_request_t *request,
                                serf_bucket_t *response,
                                void *baton,
                                apr_pool_t *pool)
{
  apr_status_t status;
  svn_ra_serf__simple_request_context_t *ctx = baton;

  status = svn_ra_serf__handle_discard_body(request, response,
                                            &ctx->server_error, pool);

  if (APR_STATUS_IS_EOF(status))
    {
      serf_status_line sl;
      apr_status_t rv;

      rv = serf_bucket_response_status(response, &sl);

      ctx->status = sl.code;
      ctx->reason = sl.reason;

      ctx->done = TRUE;
    }

  return status;
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
      ctx->error->apr_err = SVN_ERR_RA_DAV_REQUEST_FAILED;
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

apr_status_t
svn_ra_serf__handle_multistatus_only(serf_request_t *request,
                                     serf_bucket_t *response,
                                     void *baton,
                                     apr_pool_t *pool)
{
  apr_status_t status;
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
      status = svn_ra_serf__handle_xml_parser(request, response,
					      &server_err->parser, pool);

      /* APR_EOF will be returned when parsing is complete.  If we see
	 any other error, return it immediately.  In practice the only
	 other error we expect to see is APR_EAGAIN, which indicates that
	 we could not parse the XML because the contents are not yet
	 available to be read. */
      if (!APR_STATUS_IS_EOF(status))
	{
	  return status;
	}
      else if (ctx->done && server_err->error->apr_err == APR_SUCCESS)
	{
	  svn_error_clear(server_err->error);
	  server_err->error = SVN_NO_ERROR;
	}
    }

  status = svn_ra_serf__handle_discard_body(request, response,
                                            NULL, pool);

  if (APR_STATUS_IS_EOF(status))
    {
      serf_status_line sl;
      apr_status_t rv;

      rv = serf_bucket_response_status(response, &sl);

      ctx->status = sl.code;
      ctx->reason = sl.reason;
    }

  return status;
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

  name = svn_ra_serf__expand_ns(parser->state->ns_list, raw_name);

  parser->error = parser->start(parser, parser->user_data, name, attrs);
}

static void
end_xml(void *userData, const char *raw_name)
{
  svn_ra_serf__xml_parser_t *parser = userData;
  svn_ra_serf__dav_props_t name;

  if (parser->error)
    return;

  name = svn_ra_serf__expand_ns(parser->state->ns_list, raw_name);

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

apr_status_t
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

  serf_bucket_response_status(response, &sl);

  if (ctx->status_code)
    {
      *ctx->status_code = sl.code;
    }

  /* Woo-hoo.  Nothing here to see.  */
  if (sl.code == 404 && ctx->ignore_errors == FALSE)
    {
      /* If our caller won't know about the 404, abort() for now. */
      SVN_ERR_ASSERT_NO_RETURN(ctx->status_code);

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
      ctx->error = svn_ra_serf__handle_server_error(request, response, pool);
      return svn_ra_serf__handle_discard_body(request, response, NULL, pool);
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
          return status;
        }

      xml_status = XML_Parse(ctx->xmlp, data, len, 0);
      if (xml_status == XML_STATUS_ERROR && ctx->ignore_errors == FALSE)
        {
          XML_ParserFree(ctx->xmlp);

          SVN_ERR_ASSERT_NO_RETURN(ctx->status_code);

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
          ctx->error = svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                         "XML parsing failed: (%d %s)",
                                         sl.code, sl.reason);
          return ctx->error->apr_err;
        }

      if (ctx->error && ctx->ignore_errors == FALSE)
        {
          XML_ParserFree(ctx->xmlp);
          return ctx->error->apr_err;
        }

      if (APR_STATUS_IS_EAGAIN(status))
        {
          return status;
        }

      if (APR_STATUS_IS_EOF(status))
        {
          xml_status = XML_Parse(ctx->xmlp, NULL, 0, 1);
          XML_ParserFree(ctx->xmlp);
          if (xml_status == XML_STATUS_ERROR && ctx->ignore_errors == FALSE)
            {
              svn_error_clear(ctx->error);
            }

          *ctx->done = TRUE;
          if (ctx->done_list)
            {
              ctx->done_item->data = ctx->user_data;
              ctx->done_item->next = *ctx->done_list;
              *ctx->done_list = ctx->done_item;
            }
          return status;
        }

      /* feed me! */
    }
  /* not reached */
}

svn_error_t *
svn_ra_serf__handle_server_error(serf_request_t *request,
                                 serf_bucket_t *response,
                                 apr_pool_t *pool)
{
  svn_ra_serf__server_error_t server_err = { 0 };
  apr_status_t status;

  status = svn_ra_serf__handle_discard_body(request, response,
                                            &server_err, pool);

  return server_err.error;
}

/* Implements the serf_response_handler_t interface.  Wait for HTTP
   response status and headers, and invoke CTX->response_handler() to
   carry out operation-specific processing.  Afterwards, check for
   connection close.

   If during the setup of the request we set a snapshot on the body buckets,
   handle_response has to make sure these buckets get destroyed iff the
   request doesn't have to be resent.
   */
static apr_status_t
handle_response(serf_request_t *request,
                serf_bucket_t *response,
                void *baton,
                apr_pool_t *pool)
{
  svn_ra_serf__handler_t *ctx = baton;
  serf_status_line sl;
  apr_status_t status;

  if (!response)
    {
      /* Uh-oh.  Our connection died.  Requeue. */
      if (ctx->response_error)
        {
          status = ctx->response_error(request, response, 0,
                                       ctx->response_error_baton);
          if (status)
            {
              goto cleanup;
            }
        }

      svn_ra_serf__request_create(ctx);

      return APR_SUCCESS;
    }

  status = serf_bucket_response_status(response, &sl);
  if (SERF_BUCKET_READ_ERROR(status))
    {
      return status;
    }
  if (!sl.version && (APR_STATUS_IS_EOF(status) ||
                      APR_STATUS_IS_EAGAIN(status)))
    {
      goto cleanup;
    }

  status = serf_bucket_response_wait_for_headers(response);
  if (status)
    {
      if (!APR_STATUS_IS_EOF(status))
        {
          goto cleanup;
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
          ctx->session->pending_error =
              svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                               _("Premature EOF seen from server"));
          goto cleanup;
        }
    }

  if (ctx->conn->last_status_code == 401 && sl.code < 400)
    {
      svn_auth_save_credentials(ctx->session->auth_state, ctx->session->pool);
      ctx->session->auth_attempts = 0;
      ctx->session->auth_state = NULL;
      ctx->session->realm = NULL;
    }

  ctx->conn->last_status_code = sl.code;

  if (sl.code == 401 || sl.code == 407)
    {
      /* 401 Authorization or 407 Proxy-Authentication required */
      svn_error_t *err;

      err = svn_ra_serf__handle_auth(sl.code, ctx->session, ctx->conn,
                                     request, response, pool);
      if (err)
        {
          ctx->session->pending_error = err;
          svn_ra_serf__handle_discard_body(request, response, NULL, pool);
          status = ctx->session->pending_error->apr_err;
          goto cleanup;
        }
      else
        {
          status = svn_ra_serf__handle_discard_body(request, response, NULL,
                                                    pool);
          /* At this time we might not have received the whole response from
             the server. If that's the case, don't setup a new request now
             but wait till we retry the request later. */
          if (! APR_STATUS_IS_EAGAIN(status))
            {
              svn_ra_serf__priority_request_create(ctx);
              return status;
            }
        }
    }
  else if (sl.code == 409 || sl.code >= 500)
    {
      /* 409 Conflict: can indicate a hook error.
         5xx (Internal) Server error. */
      ctx->session->pending_error =
          svn_ra_serf__handle_server_error(request, response, pool);
      if (!ctx->session->pending_error)
        {
          ctx->session->pending_error =
              svn_error_create(APR_EGENERAL, NULL,
                               _("Unspecified error message"));
        }
      status = APR_EGENERAL;
      goto cleanup;
    }
  else
    {
      status = ctx->response_handler(request, response, ctx->response_baton,
                                     pool);
    }

cleanup:
  /* If a snapshot was set on the body bucket, it wasn't destroyed when the
     request was sent, we have to destroy it now upon successful handling of
     the response. */
  if (ctx->body_snapshot_set && ctx->body_buckets)
    {
      serf_bucket_destroy(ctx->body_buckets);
      ctx->body_buckets = NULL;
      ctx->body_snapshot_set = FALSE;
    }

  return status;
}

/* Implements the serf_request_setup_t interface (which sets up both a
   request and its response handler callback).  If the CTX->delegate()
   callback is non-NULL, invoke it to carry out the majority of the
   serf_request_setup_t implementation.  Otherwise, perform default
   setup, with special handling for HEAD requests, and finer-grained
   callbacks invoked (if non-NULL) to produce the request headers and
   body. */
static apr_status_t
setup_request(serf_request_t *request,
              void *setup_baton,
              serf_bucket_t **req_bkt,
              serf_response_acceptor_t *acceptor,
              void **acceptor_baton,
              serf_response_handler_t *handler,
              void **handler_baton,
              apr_pool_t *pool)
{
  svn_ra_serf__handler_t *ctx = setup_baton;
  serf_bucket_t *headers_bkt;

  *acceptor = svn_ra_serf__accept_response;
  *acceptor_baton = ctx->session;

  if (ctx->delegate)
    {
      apr_status_t status;

      status = ctx->delegate(request, ctx->delegate_baton, req_bkt,
                             acceptor, acceptor_baton, handler, handler_baton,
                             pool);
      if (status)
        {
          return status;
        }

      ctx->response_handler = *handler;
      ctx->response_baton = *handler_baton;
    }
  else
    {
      serf_bucket_t *body_bkt = ctx->body_buckets;

      if (strcmp(ctx->method, "HEAD") == 0)
        {
          *acceptor = accept_head;
        }

      if (ctx->body_delegate)
        {
          body_bkt = ctx->body_buckets =
              ctx->body_delegate(ctx->body_delegate_baton,
                                 serf_request_get_alloc(request),
                                 pool);
        }
      /* If this is a request that has to be retried, we might be able to reuse
         the existing body buckets if a snapshot was set. */
      else if (ctx->body_buckets)
          {
            /* Wrap the body bucket in a barrier bucket if a snapshot was set.
               After the request is sent serf will destroy the request bucket
               (req_bkt) including this barrier bucket, but this way our
               body_buckets bucket will not be destroyed and we can reuse it
               later.
               This does put ownership of body_buckets in our own hands though,
               so we have to make sure it gets destroyed when handling the
               response. */
            /* TODO: for now we assume restoring a snapshot on a bucket that
               hasn't been read yet is a cheap operation. We need a way to find
               out if we really need to restore a snapshot, or if we still are
               in the initial state. */
            apr_status_t status;
            if (ctx->body_snapshot_set)
              {
                /* If restoring a snapshot doesn't work, we have to fall back
                   on current behavior (ie. retrying a request fails). */
                status = serf_bucket_restore_snapshot(ctx->body_buckets);
              }
            status = serf_bucket_snapshot(ctx->body_buckets);
            if (status == APR_SUCCESS)
              {
                ctx->body_snapshot_set = TRUE;
                body_bkt = serf_bucket_barrier_create(ctx->body_buckets,
                             serf_request_get_alloc(request));
              }
            else
              {
                /* If the snapshot wasn't successful (maybe because the caller
                   used a bucket that doesn't support the snapshot feature),
                   fall back to non-snapshot behavior and hope that the request
                   is handled the first time. */
              }
          }

      svn_ra_serf__setup_serf_req(request, req_bkt, &headers_bkt, ctx->conn,
                                  ctx->method, ctx->path,
                                  body_bkt, ctx->body_type);

      if (ctx->header_delegate)
        {
          ctx->header_delegate(headers_bkt, ctx->header_delegate_baton, pool);
        }
    }

  *handler = handle_response;
  *handler_baton = ctx;

  return APR_SUCCESS;
}

serf_request_t *
svn_ra_serf__request_create(svn_ra_serf__handler_t *handler)
{
  return serf_connection_request_create(handler->conn->conn,
                                        setup_request, handler);
}

serf_request_t *
svn_ra_serf__priority_request_create(svn_ra_serf__handler_t *handler)
{
  return serf_connection_priority_request_create(handler->conn->conn,
                                                 setup_request, handler);
}

svn_error_t *
svn_ra_serf__discover_root(const char **vcc_url,
                           const char **rel_path,
                           svn_ra_serf__session_t *session,
                           svn_ra_serf__connection_t *conn,
                           const char *orig_path,
                           apr_pool_t *pool)
{
  apr_hash_t *props;
  const char *path, *relative_path, *present_path = "", *uuid;

  /* If we're only interested in our VCC, just return it. */
  if (session->vcc_url && !rel_path)
    {
      *vcc_url = session->vcc_url;
      return SVN_NO_ERROR;
    }

  props = apr_hash_make(pool);
  path = orig_path;
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

              /* Okay, strip off. */
              present_path = svn_path_join(svn_path_basename(path, pool),
                                           present_path, pool);
              path = svn_path_dirname(path, pool);
            }
        }
    }
  while (!svn_path_is_empty(path));

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
        svn_path_canonicalize(apr_uri_unparse(session->pool,
                                              &session->repos_root, 0),
                              session->pool);
    }

  /* Store the repository UUID in the cache. */
  if (!session->uuid)
    {
      session->uuid = apr_pstrdup(session->pool, uuid);
    }

  if (rel_path)
    {
      if (present_path[0] != '\0')
        {
          /* The relative path is supposed to be URI decoded, so decode
             present_path before joining both together. */
          *rel_path = svn_path_join(relative_path,
                                    svn_path_uri_decode(present_path, pool),
                                    pool);
        }
      else
        {
          *rel_path = relative_path;
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__error_on_status(int status_code, const char *path)
{
  switch(status_code)
    {
      case 301:
      case 302:
        return svn_error_createf(SVN_ERR_RA_DAV_RELOCATED, NULL,
                        (status_code == 301)
                        ? _("Repository moved permanently to '%s';"
                            " please relocate")
                        : _("Repository moved temporarily to '%s';"
                            " please relocate"), path);
      case 404:
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("'%s' path not found"), path);
    }

  return SVN_NO_ERROR;
}
