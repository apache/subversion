/*
 * options.c :  routines for performing OPTIONS server requests
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#include <apr_pools.h>

#include <hip_xml.h>
#include <http_request.h>

#include "svn_error.h"
#include "svn_ra.h"

#include "ra_dav.h"


enum {
  ELEM_activity_coll_set = DAV_ELM_207_UNUSED,
  ELEM_options_response
};

static const struct hip_xml_elm options_elements[] =
{
  { "DAV:", "activity-collection-set", ELEM_activity_coll_set, 0 },
  { "DAV:", "href", DAV_ELM_href, HIP_XML_CDATA },
  { "DAV:", "options-response", ELEM_options_response, 0 },

  { NULL }
};

typedef struct {
  svn_string_t *activity_url;
  apr_pool_t *pool;

} options_ctx_t;



static int validate_element(hip_xml_elmid parent, hip_xml_elmid child)
{
  switch (parent)
    {
    case HIP_ELM_root:
      if (child == ELEM_options_response)
        return HIP_XML_VALID;
      else
        return HIP_XML_INVALID;

    case ELEM_options_response:
      if (child == ELEM_activity_coll_set)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* not concerned with other response */

    case ELEM_activity_coll_set:
      if (child == DAV_ELM_href)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* not concerned with unknown crud */

    default:
      return HIP_XML_DECLINE;
    }

  /* NOTREACHED */
}

static int start_element(void *userdata, const struct hip_xml_elm *elm,
                         const char **atts)
{
  /* nothing to do here */
  return 0;
}

static int end_element(void *userdata, const struct hip_xml_elm *elm,
                       const char *cdata)
{
  options_ctx_t *oc = userdata;

  if (elm->id == DAV_ELM_href)
    {
      oc->activity_url = svn_string_create(cdata, oc->pool);
    }

  return 0;
}

svn_error_t * svn_ra_dav__get_activity_url(svn_string_t **activity_url,
                                           svn_ra_session_t *ras,
                                           const char *url,
                                           apr_pool_t *pool)
{
  options_ctx_t oc = { 0 };
  http_req *req;
  hip_xml_parser *parser;
  int rv;
  int code;

  /* create/prep the request */
  req = http_request_create(ras->sess, "OPTIONS", url);
  if (req == NULL)
    {
      return svn_error_createf(SVN_ERR_RA_CREATING_REQUEST, 0, NULL, pool,
                               "Could not create an OPTIONS request (%s)",
                               url);
    }

  http_set_request_body_buffer(req,
                               "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                               "<D:options xmlns:D=\"DAV:\">"
                               "<D:activity-collection-set/>"
                               "</D:options>");

#if 0
  http_add_response_header_handler(req, "dav",
                                   http_duplicate_header, &dav_header);
#endif

  oc.pool = pool;

  /* create a parser to read the OPTIONS response body */
  parser = hip_xml_create();
  hip_xml_push_handler(parser, options_elements,
                       validate_element, start_element, end_element, &oc);
  http_add_response_body_reader(req, http_accept_2xx, hip_xml_parse_v, parser);

  /* run the request and get the resulting status code. */
  rv = http_request_dispatch(req);
  if (rv != HTTP_OK)
    {
      /* ### need to be more sophisticated with reporting the failure */

      switch (rv)
        {
        case HTTP_CONNECT:
          /* ### need an SVN_ERR here */
          return svn_error_createf(APR_EGENERAL, 0, NULL, pool,
                                   "Could not connect to server "
                                   "(%s, port %d).",
                                   ras->root.host, ras->root.port);

        case HTTP_AUTH:
          return svn_error_create(SVN_ERR_NOT_AUTHORIZED, 0, NULL, pool,
                                  "Authentication failed on server.");

        default:
          return svn_error_createf(SVN_ERR_RA_REQUEST_FAILED, 0, NULL, pool,
                                   "The OPTIONS request failed (#%d) (%s)",
                                   rv, url);
        }
    }

  code = http_get_status(req)->code;

  http_request_destroy(req);

  if (code != 200)
    {
      /* ### need an SVN_ERR here */
      return svn_error_createf(APR_EGENERAL, 0, NULL, pool,
                               "The OPTIONS status was %d, but expected 200.",
                               code);
    }

  if (oc.activity_url == NULL)
    {
      /* ### error */
      return svn_error_create(APR_EGENERAL, 0, NULL, pool,
                              "The OPTIONS response did not include the "
                              "requested activity-collection-set.");
    }

  *activity_url = oc.activity_url;

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
