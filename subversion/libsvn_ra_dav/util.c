/*
 * util.c :  utility functions for the RA/DAV library
 *
      * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include <apr_want.h>

#include <ne_uri.h>

#include "svn_string.h"

#include "ra_dav.h"



void svn_ra_dav__copy_href(svn_stringbuf_t *dst, const char *src)
{
  struct uri parsed_url;

  /* parse the PATH element out of the URL and store it.

     ### do we want to verify the rest matches the current session?

     Note: mod_dav does not (currently) use an absolute URL, but simply a
     server-relative path (i.e. this uri_parse is effectively a no-op).
  */
  (void) uri_parse(src, &parsed_url, NULL);
  svn_stringbuf_set(dst, parsed_url.path);
  uri_free(&parsed_url);
}

svn_error_t *svn_ra_dav__convert_error(ne_session *sess,
                                       const char *context,
                                       int retcode,
                                       apr_pool_t *pool)
{
  int errcode = SVN_ERR_RA_REQUEST_FAILED;
  const char *msg;

  /* Convert the return codes. */
  switch (retcode) 
    {
    case NE_AUTH:
      errcode = SVN_ERR_RA_NOT_AUTHORIZED;
      msg = "authorization failed";
      break;
      
    case NE_CONNECT:
      msg = "could not connect to server";
      break;

    case NE_TIMEOUT:
      msg = "timed out waiting for server";
      break;

    default:
      /* Get the error string from neon. */
      msg = ne_get_error (sess);
      break;
    }

  return svn_error_createf (errcode, 0, NULL, pool,
                            "%s: %s", context, msg);
  
}

svn_error_t *svn_ra_dav__parsed_request(svn_ra_session_t *ras,
                                        const char *method,
                                        const char *url,
                                        const char *body,
                                        int fd,
                                        const struct ne_xml_elm *elements, 
                                        ne_xml_validate_cb validate_cb,
                                        ne_xml_startelm_cb startelm_cb, 
                                        ne_xml_endelm_cb endelm_cb,
                                        void *baton,
                                        apr_pool_t *pool)
{
  ne_request *req;
  ne_xml_parser *parser;
  int rv;
  int code;
  const char *msg;
  svn_error_t *err;

  /* create/prep the request */
  req = ne_request_create(ras->sess, method, url);

  if (body != NULL)
    ne_set_request_body_buffer(req, body, strlen(body));
  else
    ne_set_request_body_fd(req, fd);

  /* ### use a symbolic name somewhere for this MIME type? */
  ne_add_request_header(req, "Content-Type", "text/xml");

  /* create a parser to read the MERGE response body */
  parser = ne_xml_create();
  ne_xml_push_handler(parser, elements,
                       validate_cb, startelm_cb, endelm_cb, baton);
  ne_add_response_body_reader(req, ne_accept_2xx, ne_xml_parse_v, parser);

  /* run the request and get the resulting status code. */
  rv = ne_request_dispatch(req);
  code = ne_get_status(req)->code;
  ne_request_destroy(req);

  if (rv != NE_OK)
    {
      svn_error_t *err2;

      /* ### need to be more sophisticated with reporting the failure */
      err2 = svn_error_createf (SVN_ERR_RA_REQUEST_FAILED, 0, NULL, pool,
                                "neon: %s", ne_get_error (ras->sess));
                               
      switch (rv)
        {
        case NE_CONNECT:
          /* ### need an SVN_ERR here */
          err = svn_error_createf(APR_EGENERAL, 0, err2, pool,
                                  "Could not connect to server "
                                  "(%s, port %d).",
                                  ras->root.host, ras->root.port);
          goto error;

        case NE_AUTH:
          err = svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, 0, err2, pool,
                                 "Authentication failed on server.");
          goto error;

        default:
          err = svn_error_createf(SVN_ERR_RA_REQUEST_FAILED, 0, err2, pool,
                                  "The %s request failed (%s)",
                                  method, url);
          goto error;
        }
    }

  if (code != 200)
    {
      /* ### need an SVN_ERR here */
      err = svn_error_createf(APR_EGENERAL, 0, NULL, pool,
                              "The %s status was %d, but expected 200.",
                              method, code);
      goto error;
    }

  /* was there a parse error somewhere in the response? */
  msg = ne_xml_get_error(parser);
  if (msg != NULL && *msg != '\0')
    {
      err = svn_error_createf(SVN_ERR_RA_REQUEST_FAILED, 0, NULL,
                              pool,
                              "The %s request returned invalid XML "
                              "in the response: %s. (%s)",
                              method, msg, url);
      goto error;
    }
  /* ### maybe hook this to a pool? */
  ne_xml_destroy(parser);

  return NULL;

 error:
  ne_xml_destroy(parser);
  return err;
}



svn_error_t *
svn_ra_dav__maybe_store_auth_info (svn_ra_session_t *ras)
{
  void *a, *auth_baton;
  svn_ra_simple_password_authenticator_t *authenticator;
  
  SVN_ERR (ras->callbacks->get_authenticator (&a, &auth_baton, 
                                              SVN_RA_AUTH_SIMPLE_PASSWORD, 
                                              ras->callback_baton,
                                              ras->pool));
  authenticator = (svn_ra_simple_password_authenticator_t *) a;      
  
  /* If we have a auth-info storage callback, use it. */
  if (authenticator->store_user_and_pass)
    /* Storage will only happen if AUTH_BATON is already caching auth info. */
    SVN_ERR (authenticator->store_user_and_pass (auth_baton));
  
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
