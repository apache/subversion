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
#include <ne_compress.h>

#include "svn_string.h"
#include "svn_xml.h"
#include "svn_path.h"
#include "svn_config.h"

#include "ra_dav.h"





void svn_ra_dav__copy_href(svn_stringbuf_t *dst, const char *src)
{
  ne_uri parsed_url;

  /* parse the PATH element out of the URL and store it.

     ### do we want to verify the rest matches the current session?

     Note: mod_dav does not (currently) use an absolute URL, but simply a
     server-relative path (i.e. this uri_parse is effectively a no-op).
  */
  (void) ne_uri_parse(src, &parsed_url);
  svn_stringbuf_set(dst, parsed_url.path);
  ne_uri_free(&parsed_url);
}

svn_error_t *svn_ra_dav__convert_error(ne_session *sess,
                                       const char *context,
                                       int retcode)
{
  int errcode = SVN_ERR_RA_DAV_REQUEST_FAILED;
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

  return svn_error_createf (errcode, 0, NULL, "%s: %s", context, msg);
  
}


/** Error parsing **/


/* Custom function of type ne_accept_response. */
static int ra_dav_error_accepter(void *userdata,
                                 ne_request *req,
                                 const ne_status *st)
{
  /* Only accept the body-response if the HTTP status code is *not* 2XX. */
  return (st->klass != 2);
}


static const struct ne_xml_elm error_elements[] =
{
  { "DAV:", "error", ELEM_error, 0 },
  { "svn:", "error", ELEM_svn_error, 0 },
  { "http://apache.org/dav/xmlns", "human-readable", 
    ELEM_human_readable, NE_XML_CDATA },

  /* ### our validator doesn't yet recognize the rich, specific
         <D:some-condition-failed/> objects as defined by DeltaV.*/

  { NULL }
};


static int validate_error_elements(void *userdata,
                                   ne_xml_elmid parent,
                                   ne_xml_elmid child)
{
  switch (parent)
    {
    case NE_ELM_root:
      if (child == ELEM_error)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    case ELEM_error:
      if (child == ELEM_svn_error
          || child == ELEM_human_readable)
        return NE_XML_VALID;
      else
        return NE_XML_DECLINE;  /* ignore if something else was in there */

    default:
      return NE_XML_DECLINE;
    }

  /* NOTREACHED */
}


static int start_err_element(void *userdata, const struct ne_xml_elm *elm,
                             const char **atts)
{
  svn_error_t **err = userdata;

  switch (elm->id)
    {
    case ELEM_svn_error:
      {
        /* allocate the svn_error_t.  Hopefully the value will be
           overwritten by the <human-readable> tag, or even someday by
           a <D:failed-precondition/> tag. */
        *err = svn_error_create(APR_EGENERAL, 0, NULL,
                                "General svn error from server");
        break;
      }
    case ELEM_human_readable:
      {
        /* get the errorcode attribute if present */
        const char *errcode_str = 
          svn_xml_get_attr_value("errcode", /* ### make constant in
                                               some mod_dav header? */
                                 atts);

        if (errcode_str && *err) 
          (*err)->apr_err = atoi(errcode_str);

        break;
      }

    default:
      break;
    }

  return 0;
}

static int end_err_element(void *userdata, const struct ne_xml_elm *elm,
                           const char *cdata)
{
  svn_error_t **err = userdata;

  switch (elm->id)
    {
    case ELEM_human_readable:
      {
        if (cdata && *err)
          (*err)->message = apr_pstrdup((*err)->pool, cdata);
        break;
      }

    default:
      break;
    }

  return 0;
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
  ne_decompress *decompress_main;
  ne_decompress *decompress_err;
  ne_xml_parser *success_parser;
  ne_xml_parser *error_parser;
  int rv;
  int decompress_rv;
  int decompress_on;
  int code;
  const char *msg;
  const char *do_compression;
  svn_config_t *cfg;
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR( svn_config_read_config(&cfg, pool) );

  svn_config_get(cfg, &do_compression, "miscellany", "do_compression", "yes");
  if (strcasecmp(do_compression, "yes") == 0) {
    decompress_on = 1;
  }                              
  else {
    decompress_on = 0;
  }

  /* create/prep the request */
  req = ne_request_create(ras->sess, method, url);

  if (body != NULL)
    ne_set_request_body_buffer(req, body, strlen(body));
  else
    ne_set_request_body_fd(req, fd);

  /* ### use a symbolic name somewhere for this MIME type? */
  ne_add_request_header(req, "Content-Type", "text/xml");

  /* create a parser to read the normal response body */
  success_parser = ne_xml_create();
  ne_xml_push_handler(success_parser, elements,
                       validate_cb, startelm_cb, endelm_cb, baton);

  /* create a parser to read the <D:error> response body */
  error_parser = ne_xml_create();
  ne_xml_push_handler(error_parser, error_elements, validate_error_elements,
                      start_err_element, end_err_element, &err); 

  /* Register the "main" accepter and body-reader with the request --
     the one to use when the HTTP status is 2XX */
  if (decompress_on)
    {
      decompress_main = ne_decompress_reader(req, ne_accept_2xx,
                                             ne_xml_parse_v, success_parser);
    }
  else
    {
      decompress_main = NULL;
      ne_add_response_body_reader(req, ne_accept_2xx, ne_xml_parse_v,
                                  success_parser);
    }

  /* Register the "error" accepter and body-reader with the request --
     the one to use when HTTP status is *not* 2XX */
  if (decompress_on)
    {
      decompress_err = ne_decompress_reader(req, ra_dav_error_accepter,
                                            ne_xml_parse_v, error_parser);
    }
  else
    {
      decompress_err = NULL;
      ne_add_response_body_reader(req, ra_dav_error_accepter, ne_xml_parse_v,
                                  error_parser);
    }

  /* run the request and get the resulting status code. */
  rv = ne_request_dispatch(req);

  if (decompress_main)
    {
      decompress_rv = ne_decompress_destroy(decompress_main);
      if (decompress_rv != 0)
        {
          rv = decompress_rv;
        }
    }

  if (decompress_err)
    {
      decompress_rv = ne_decompress_destroy(decompress_err);
      if (decompress_rv != 0)
        {
          rv = decompress_rv;
        }
    }
  
  code = ne_get_status(req)->code;
  ne_request_destroy(req);

  if (rv != NE_OK)
    {
      msg = apr_psprintf(pool, "%s of %s", method, url);
      err = svn_ra_dav__convert_error(ras->sess, msg, rv);
    }

  if (err) /* Either from error-parser or from just above */
    goto error;

  if (code != 200)
    {
      /* Bad status, but error-parser didn't build an error.  Return a
         generic error instead.*/
      err = svn_error_createf(APR_EGENERAL, 0, NULL,
                              "The %s status was %d, but expected 200.",
                              method, code);
      goto error;
    }

  /* was there an XML parse error somewhere? */
  msg = ne_xml_get_error(success_parser);
  if (msg != NULL && *msg != '\0')
    {
      err = svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, 0, NULL,
                              "The %s request returned invalid XML "
                              "in the response: %s. (%s)",
                              method, msg, url);
      goto error;
    }
  /* ### maybe hook this to a pool? */
  ne_xml_destroy(success_parser);
  ne_xml_destroy(error_parser);

  return NULL;

 error:
  ne_xml_destroy(success_parser);
  ne_xml_destroy(error_parser);
  return svn_error_createf(err->apr_err, err->src_err, err,
                           "%s request failed on %s", method, url );
}



svn_error_t *
svn_ra_dav__maybe_store_auth_info(svn_ra_session_t *ras)
{
  void *a, *auth_baton;
  svn_ra_simple_password_authenticator_t *authenticator;
  
  SVN_ERR (ras->callbacks->get_authenticator (&a, &auth_baton, 
                                              svn_ra_auth_simple_password, 
                                              ras->callback_baton,
                                              ras->pool));
  authenticator = (svn_ra_simple_password_authenticator_t *) a;      
  
  /* If we have a auth-info storage callback, use it. */
  if (authenticator->store_user_and_pass)
    /* Storage will only happen if AUTH_BATON is already caching auth info. */
    SVN_ERR (authenticator->store_user_and_pass (auth_baton));
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_ra_dav__request_dispatch(int *code,
                             ne_request *request,
                             ne_session *session,
                             const char *method,
                             const char *url,
                             int okay_1,
                             int okay_2,
                             apr_pool_t *pool)
{
  ne_xml_parser *error_parser;
  int rv;
  const ne_status *statstruct;
  const char *code_desc;
  svn_error_t *err = SVN_NO_ERROR;

  /* attach a standard <D:error> body parser to the request */
  error_parser = ne_xml_create();
  ne_xml_push_handler(error_parser, error_elements, validate_error_elements,
                      start_err_element, end_err_element, &err);
  ne_add_response_body_reader(request, ra_dav_error_accepter,
                              ne_xml_parse_v, error_parser);

  /* run the request, see what comes back. */
  rv = ne_request_dispatch(request);

  statstruct = ne_get_status(request);
  *code = statstruct->code;
  code_desc = apr_pstrdup(pool, statstruct->reason_phrase);

  ne_request_destroy(request);
  ne_xml_destroy(error_parser);

  /* first, check to see if neon itself got an error */
  if (rv != NE_OK)
    {
      const char *msg = apr_psprintf(pool, "%s of %s", method, url);
      return svn_ra_dav__convert_error(session, msg, rv);
    }

  /* If the status code was one of the two that we expected, then go
     ahead and return now. IGNORE any marshalled error. */
  if (*code == okay_1 || *code == okay_2)
    return SVN_NO_ERROR;

  /* next, check to see if a <D:error> was discovered */
  if (err)
    return err;

  /* Bad http status, but error-parser didn't build an svn_error_t
     for some reason.  Return a generic error instead. */
  return svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, 0, NULL,
                           "%s of %s returned status code %d (%s)",
                           method, url, *code, code_desc);
}
