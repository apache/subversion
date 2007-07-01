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

/*** Code. ***/

svn_error_t *
handle_auth(svn_ra_serf__session_t *session,
            svn_ra_serf__connection_t *conn,
            serf_request_t *request,
            serf_bucket_t *response,
            apr_pool_t *pool)
{
  serf_bucket_t *hdrs;
  char *cur, *auth_attr, *auth_hdr;

  hdrs = serf_bucket_response_get_headers(response);
  auth_hdr = (char*)serf_bucket_headers_get(hdrs, "WWW-Authenticate");

  if (!auth_hdr)
    {
      abort();
    }

  cur = apr_strtok(auth_hdr, " ", &auth_attr);

  if (strcmp(cur, "Basic") == 0)
    {
      SVN_ERR(handle_basic_auth(session, conn, request, 
                                response, auth_hdr, auth_attr,
                                pool));
    }
  else
    {
      return svn_error_createf(SVN_ERR_AUTHN_FAILED, NULL,
                               "%s authentication not supported.\n"
                               "Authentication failed", cur);
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
  apr_size_t tmp_len, encoded_len;
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

  encoded_len = apr_base64_encode_len(tmp_len);

  session->auth_value = apr_palloc(session->pool, encoded_len + 6);

  apr_cpystrn(session->auth_value, "Basic ", 7);

  apr_base64_encode(&session->auth_value[6], tmp, tmp_len);

  session->auth_header = "Authorization";

  /* FIXME Come up with a cleaner way of changing the connection auth. */
  for (i = 0; i < session->num_conns; i++)
    {
      session->conns[i]->auth_header = session->auth_header;
      session->conns[i]->auth_value = session->auth_value;
    }

  return SVN_NO_ERROR;
}
