/*
 * options.c :  routines for performing OPTIONS server requests
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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

#include <ne_socket.h>
#include <ne_request.h>

#include "svn_error.h"

#include "svn_private_config.h"

#include "ra_dav.h"


static const svn_ra_dav__xml_elm_t options_elements[] =
{
  { "DAV:", "activity-collection-set", ELEM_activity_coll_set, 0 },
  { "DAV:", "href", ELEM_href, SVN_RA_DAV__XML_CDATA },
  { "DAV:", "options-response", ELEM_options_response, 0 },

  { NULL }
};

typedef struct {
  /*WARNING: WANT_CDATA should stay the first element in the baton:
    svn_ra_dav__xml_collect_cdata() assumes the baton starts with a stringbuf.
  */
  svn_stringbuf_t *want_cdata;
  svn_stringbuf_t *cdata;
  apr_pool_t *pool;
  svn_string_t *activity_coll;
} options_ctx_t;



static int
validate_element(svn_ra_dav__xml_elmid parent, svn_ra_dav__xml_elmid child)
{
  switch (parent)
    {
    case ELEM_root:
      if (child == ELEM_options_response)
        return SVN_RA_DAV__XML_VALID;
      else
        return SVN_RA_DAV__XML_INVALID;

    case ELEM_options_response:
      if (child == ELEM_activity_coll_set)
        return SVN_RA_DAV__XML_VALID;
      else
        return SVN_RA_DAV__XML_DECLINE; /* not concerned with other response */

    case ELEM_activity_coll_set:
      if (child == ELEM_href)
        return SVN_RA_DAV__XML_VALID;
      else
        return SVN_RA_DAV__XML_DECLINE; /* not concerned with unknown crud */

    default:
      return SVN_RA_DAV__XML_DECLINE;
    }

  /* NOTREACHED */
}

static svn_error_t *
start_element(int *elem, void *baton, int parent,
              const char *nspace, const char *name, const char **atts)
{
  options_ctx_t *oc = baton;
  const svn_ra_dav__xml_elm_t *elm
    = svn_ra_dav__lookup_xml_elem(options_elements, nspace, name);
  int acc = elm ? validate_element(parent, elm->id) : SVN_RA_DAV__XML_DECLINE;

  if (acc != SVN_RA_DAV__XML_VALID)
    {
      *elem = acc;
      return (acc == SVN_RA_DAV__XML_DECLINE) ?
        SVN_NO_ERROR : svn_error_create(SVN_ERR_XML_MALFORMED, NULL, NULL);
    }
  else
    *elem = elm->id;

  if (elm->id == ELEM_href)
    oc->want_cdata = oc->cdata;
  else
    oc->want_cdata = NULL;

  return SVN_NO_ERROR;
}

static svn_error_t *
end_element(void *baton, int state,
            const char *nspace, const char *name)
{
  options_ctx_t *oc = baton;

  if (state == ELEM_href)
    oc->activity_coll = svn_string_create_from_buf(oc->cdata, oc->pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_dav__get_activity_collection(const svn_string_t **activity_coll,
                                    svn_ra_dav__session_t *ras,
                                    const char *url,
                                    apr_pool_t *pool)
{
  options_ctx_t oc = { 0 };

#if 0
  ne_add_response_header_handler(req, "dav",
                                 ne_duplicate_header, &dav_header);
#endif

  oc.pool = pool;
  oc.cdata = svn_stringbuf_create("", pool);

  SVN_ERR(svn_ra_dav__parsed_request(ras, "OPTIONS", url,
                                     "<?xml version=\"1.0\" "
                                     "encoding=\"utf-8\"?>"
                                     "<D:options xmlns:D=\"DAV:\">"
                                     "<D:activity-collection-set/>"
                                     "</D:options>", 0, NULL,
                                     start_element,
                                     svn_ra_dav__xml_collect_cdata,
                                     end_element, &oc,
                                     NULL, NULL, FALSE, pool));

  if (oc.activity_coll == NULL)
    {
      /* ### error */
      return svn_error_create(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
                              _("The OPTIONS response did not include the "
                                "requested activity-collection-set; "
                                "this often means that "
                                "the URL is not WebDAV-enabled"));
    }

  *activity_coll = oc.activity_coll;

  return SVN_NO_ERROR;
}
