/*
 * util.c :  utility functions for the RA/DAV library
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <uri.h>

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
  svn_string_set(dst, parsed_url.path);
  uri_free(&parsed_url);
}


svn_error_t *svn_ra_dav__parsed_request(svn_ra_session_t *ras,
                                        const char *method,
                                        const char *url,
                                        const char *body,
                                        FILE *fp,
                                        const struct hip_xml_elm *elements, 
                                        hip_xml_validate_cb validate_cb,
                                        hip_xml_startelm_cb startelm_cb, 
                                        hip_xml_endelm_cb endelm_cb,
                                        void *baton,
                                        apr_pool_t *pool)
{
  http_req *req;
  hip_xml_parser *parser;
  int rv;
  int code;
  const char *msg;
  svn_error_t *err;

  /* create/prep the request */
  req = http_request_create(ras->sess, method, url);
  if (req == NULL)
    {
      return svn_error_createf(SVN_ERR_RA_CREATING_REQUEST, 0, NULL, pool,
                               "Could not create a %s request (%s)",
                               method, url);
    }

  if (body != NULL)
    http_set_request_body_buffer(req, body);
  else
    http_set_request_body_stream(req, fp);

  /* ### use a symbolic name somewhere for this MIME type? */
  http_add_request_header(req, "Content-Type", "text/xml");

  /* create a parser to read the MERGE response body */
  parser = hip_xml_create();
  hip_xml_push_handler(parser, elements,
                       validate_cb, startelm_cb, endelm_cb, baton);
  http_add_response_body_reader(req, http_accept_2xx, hip_xml_parse_v, parser);

  /* run the request and get the resulting status code. */
  rv = http_request_dispatch(req);
  code = http_get_status(req)->code;
  http_request_destroy(req);

  if (rv != HTTP_OK)
    {
      /* ### need to be more sophisticated with reporting the failure */

      switch (rv)
        {
        case HTTP_CONNECT:
          /* ### need an SVN_ERR here */
          err = svn_error_createf(APR_EGENERAL, 0, NULL, pool,
                                  "Could not connect to server "
                                  "(%s, port %d).",
                                  ras->root.host, ras->root.port);
          goto error;

        case HTTP_AUTH:
          err = svn_error_create(SVN_ERR_NOT_AUTHORIZED, 0, NULL, pool,
                                 "Authentication failed on server.");
          goto error;

        default:
          err = svn_error_createf(SVN_ERR_RA_REQUEST_FAILED, 0, NULL, pool,
                                  "The %s request failed (#%d) (%s)",
                                  method, rv, url);
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
  msg = hip_xml_get_error(parser);
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
  hip_xml_destroy(parser);

  return NULL;

 error:
  hip_xml_destroy(parser);
  return err;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
