/*
 * merge.c :  routines for performing a MERGE server requests
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


static const struct hip_xml_elm merge_elements[] =
{
  { "DAV:", "updated-set", ELEM_updated_set, 0 },
  { "DAV:", "merged-set", ELEM_merged_set, 0 },
  { "DAV:", "ignored-set", ELEM_ignored_set, 0 },
  { "DAV:", "href", DAV_ELM_href, HIP_XML_CDATA },
  { "DAV:", "merge-response", ELEM_merge_response, 0 },
  { "DAV:", "checked-in", ELEM_checked_in, 0 },

  { NULL }
};

typedef struct {
  apr_pool_t *pool;

} merge_ctx_t;


static int validate_element(hip_xml_elmid parent, hip_xml_elmid child)
{
  switch (parent)
    {
    case HIP_ELM_root:
      if (child == ELEM_merge_response)
        return HIP_XML_VALID;
      else
        return HIP_XML_INVALID;

    case ELEM_merge_response:
      if (child == ELEM_updated_set
          || child == ELEM_merged_set
          || child == ELEM_ignored_set)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* any child is allowed */

    case ELEM_updated_set:
      if (child == DAV_ELM_response)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* ignore if something else was in there */

    case ELEM_merged_set:
      if (child == DAV_ELM_response)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* ignore if something else was in there */

    case ELEM_ignored_set:
      if (child == DAV_ELM_href)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* ignore if something else was in there */

    case DAV_ELM_response:
      if (child == DAV_ELM_href
          || child == DAV_ELM_status
          || child == DAV_ELM_propstat
          || child == DAV_ELM_responsedescription)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* ignore if something else was in there */

    case DAV_ELM_propstat:
      if (child == DAV_ELM_prop
          || child == DAV_ELM_status
          || child == DAV_ELM_responsedescription)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* ignore if something else was in there */

    case DAV_ELM_prop:
      if (child == ELEM_checked_in)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* ignore other props */

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
#if 0
  merge_ctx_t *mc = userdata;
#endif

  /* ### do some work */

  return 0;
}

svn_error_t * svn_ra_dav__merge_activity(svn_ra_session_t *ras,
                                         const char *repos_url,
                                         const char *activity_url,
                                         apr_pool_t *pool)
{
  merge_ctx_t mc = { 0 };
  http_req *req;
  hip_xml_parser *parser;
  int rv;
  int code;
  const char *body;

  /* create/prep the request */
  req = http_request_create(ras->sess, "MERGE", repos_url);
  if (req == NULL)
    {
      return svn_error_createf(SVN_ERR_RA_CREATING_REQUEST, 0, NULL, pool,
                               "Could not create a MERGE request (%s)",
                               repos_url);
    }

  body = apr_psprintf(pool,
                      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                      "<D:merge xmlns:D=\"DAV:\">"
                      "<D:source><D:href>%s</D:href></D:source>"
                      "<D:no-auto-merge/><D:no-checkout>"
                      "<D:prop><D:checked-in></D:prop>"
                      "</D:merge>", activity_url);
  http_set_request_body_buffer(req, body);

  mc.pool = pool;

  /* create a parser to read the MERGE response body */
  parser = hip_xml_create();
  hip_xml_push_handler(parser, merge_elements,
                       validate_element, start_element, end_element, &mc);
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
          return svn_error_createf(APR_EGENERAL, 0, NULL, pool,
                                   "Could not connect to server "
                                   "(%s, port %d).",
                                   ras->root.host, ras->root.port);

        case HTTP_AUTH:
          return svn_error_create(SVN_ERR_NOT_AUTHORIZED, 0, NULL, pool,
                                  "Authentication failed on server.");

        default:
          return svn_error_createf(SVN_ERR_RA_REQUEST_FAILED, 0, NULL, pool,
                                   "The MERGE request failed (#%d) (%s)",
                                   rv, repos_url);
        }
    }

  if (code != 200)
    {
      /* ### need an SVN_ERR here */
      return svn_error_createf(APR_EGENERAL, 0, NULL, pool,
                               "The MERGE status was %d, but expected 200.",
                               code);
    }

  /* ### do some work */

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
