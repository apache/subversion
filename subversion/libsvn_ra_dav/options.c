/*
 * options.c :  routines for performing OPTIONS server requests
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



#include <apr_pools.h>

#include <ne_request.h>
#include <ne_xml.h>

#include "svn_error.h"
#include "svn_ra.h"

#include "ra_dav.h"


static const struct ne_xml_elm options_elements[] =
{
  { "DAV:", "activity-collection-set", ELEM_activity_coll_set, 0 },
  { "DAV:", "href", NE_ELM_href, NE_XML_CDATA },
  { "DAV:", "options-response", ELEM_options_response, 0 },

  { NULL }
};

typedef struct {
  const svn_string_t *activity_coll;
  apr_pool_t *pool;

} options_ctx_t;



static int validate_element(void *userdata, ne_xml_elmid parent, ne_xml_elmid child)
{
  switch (parent)
    {
    case NE_ELM_root:
      if (child == ELEM_options_response)
        return NE_XML_VALID;
      else
        return NE_XML_INVALID;

    case ELEM_options_response:
      if (child == ELEM_activity_coll_set)
        return NE_XML_VALID;
      else
        return NE_XML_DECLINE; /* not concerned with other response */

    case ELEM_activity_coll_set:
      if (child == NE_ELM_href)
        return NE_XML_VALID;
      else
        return NE_XML_DECLINE; /* not concerned with unknown crud */

    default:
      return NE_XML_DECLINE;
    }

  /* NOTREACHED */
}

static int start_element(void *userdata, const struct ne_xml_elm *elm,
                         const char **atts)
{
  /* nothing to do here */
  return 0;
}

static int end_element(void *userdata, const struct ne_xml_elm *elm,
                       const char *cdata)
{
  options_ctx_t *oc = userdata;

  if (elm->id == NE_ELM_href)
    {
      oc->activity_coll = svn_string_create(cdata, oc->pool);
    }

  return 0;
}

svn_error_t * svn_ra_dav__get_activity_collection(
  const svn_string_t **activity_coll,
  svn_ra_session_t *ras,
  const char *url,
  apr_pool_t *pool)
{
  options_ctx_t oc = { 0 };

#if 0
  ne_add_response_header_handler(req, "dav",
                                 ne_duplicate_header, &dav_header);
#endif

  oc.pool = pool;

  SVN_ERR( svn_ra_dav__parsed_request(ras, "OPTIONS", url,
                                      "<?xml version=\"1.0\" "
                                      "encoding=\"utf-8\"?>"
                                      "<D:options xmlns:D=\"DAV:\">"
                                      "<D:activity-collection-set/>"
                                      "</D:options>", 0,
                                      options_elements, validate_element,
                                      start_element, end_element, &oc,
                                      NULL, pool) );

  if (oc.activity_coll == NULL)
    {
      /* ### error */
      return svn_error_create(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED,
                              0, NULL,
                              "The OPTIONS response did not include the "
                              "requested activity-collection-set.\n"
                              "(Check the URL again;  this often means that "
                              "the URL is not WebDAV-enabled.)");
    }

  *activity_coll = oc.activity_coll;

  return SVN_NO_ERROR;
}
