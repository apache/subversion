/*
 * mergeinfo.c :  routines for requesting and parsing mergeinfo reports
 *
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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
#include <apr_tables.h>
#include <apr_strings.h>
#include <apr_xml.h>

#include <ne_socket.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_mergeinfo.h"
#include "private/svn_dav_protocol.h"
#include "../libsvn_ra/ra_loader.h"

#include "ra_neon.h"

/* Baton for accumulating mergeinfo.  CATALOG stores the final
   mergeinfo catalog result we are going to hand back to the caller of
   get_mergeinfo.  curr_path and curr_info contain the value of the
   CDATA from the mergeinfo items as we get them from the server.  */

struct mergeinfo_baton
{
  apr_pool_t *pool;
  svn_stringbuf_t *curr_path;
  svn_stringbuf_t *curr_info;
  svn_mergeinfo_catalog_t catalog;
  svn_error_t *err;
};

static const svn_ra_neon__xml_elm_t mergeinfo_report_elements[] =
  {
    { SVN_XML_NAMESPACE, SVN_DAV__MERGEINFO_REPORT, ELEM_mergeinfo_report, 0 },
    { SVN_XML_NAMESPACE, SVN_DAV__MERGEINFO_ITEM, ELEM_mergeinfo_item, 0 },
    { SVN_XML_NAMESPACE, SVN_DAV__MERGEINFO_PATH, ELEM_mergeinfo_path,
      SVN_RA_NEON__XML_CDATA },
    { SVN_XML_NAMESPACE, SVN_DAV__MERGEINFO_INFO, ELEM_mergeinfo_info,
      SVN_RA_NEON__XML_CDATA },
    { NULL }
  };

static svn_error_t *
start_element(int *elem, void *baton, int parent_state, const char *nspace,
              const char *elt_name, const char **atts)
{
  struct mergeinfo_baton *mb = baton;

  const svn_ra_neon__xml_elm_t *elm
    = svn_ra_neon__lookup_xml_elem(mergeinfo_report_elements, nspace,
                                   elt_name);
  if (! elm)
    {
      *elem = NE_XML_DECLINE;
      return SVN_NO_ERROR;
    }

  if (parent_state == ELEM_root)
    {
      /* If we're at the root of the tree, the element has to be the editor
       * report itself. */
      if (elm->id != ELEM_mergeinfo_report)
        return UNEXPECTED_ELEMENT(nspace, elt_name);
    }

  if (elm->id == ELEM_mergeinfo_item)
    {
      svn_stringbuf_setempty(mb->curr_info);
      svn_stringbuf_setempty(mb->curr_path);
    }

  SVN_ERR(mb->err);

  *elem = elm->id;
  return SVN_NO_ERROR;
}

static svn_error_t *
end_element(void *baton, int state, const char *nspace, const char *elt_name)
{
  struct mergeinfo_baton *mb = baton;

  const svn_ra_neon__xml_elm_t *elm
    = svn_ra_neon__lookup_xml_elem(mergeinfo_report_elements, nspace,
                                   elt_name);
  if (! elm)
    return UNEXPECTED_ELEMENT(nspace, elt_name);

  if (elm->id == ELEM_mergeinfo_item)
    {
      if (mb->curr_info && mb->curr_path)
        {
          svn_mergeinfo_t path_mergeinfo;

          SVN_ERR_ASSERT(mb->curr_path->data);
          SVN_ERR((mb->err = svn_mergeinfo_parse(&path_mergeinfo,
                                                 mb->curr_info->data,
                                                 mb->pool)));
          apr_hash_set(mb->catalog, apr_pstrdup(mb->pool, mb->curr_path->data),
                       APR_HASH_KEY_STRING, path_mergeinfo);
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
cdata_handler(void *baton, int state, const char *cdata, size_t len)
{
  struct mergeinfo_baton *mb = baton;
  apr_size_t nlen = len;

  switch (state)
    {
    case ELEM_mergeinfo_path:
      svn_stringbuf_appendbytes(mb->curr_path, cdata, nlen);
      break;

    case ELEM_mergeinfo_info:
      svn_stringbuf_appendbytes(mb->curr_info, cdata, nlen);
      break;

    default:
      break;
    }
  return mb->err;
}

/* Request a mergeinfo-report from the URL attached to SESSION,
   and fill in the CATALOG with the results.  */
svn_error_t *
svn_ra_neon__get_mergeinfo(svn_ra_session_t *session,
                           svn_mergeinfo_catalog_t *catalog,
                           const apr_array_header_t *paths,
                           svn_revnum_t revision,
                           svn_mergeinfo_inheritance_t inherit,
                           svn_boolean_t include_descendants,
                           apr_pool_t *pool)
{
  int i, status_code;
  svn_ra_neon__session_t *ras = session->priv;
  svn_stringbuf_t *request_body = svn_stringbuf_create("", pool);
  struct mergeinfo_baton mb;
  svn_string_t bc_url, bc_relative;
  const char *final_bc_url;

  static const char minfo_report_head[] =
    "<S:" SVN_DAV__MERGEINFO_REPORT " xmlns:S=\"" SVN_XML_NAMESPACE "\">"
    DEBUG_CR;

  static const char minfo_report_tail[] =
    "</S:" SVN_DAV__MERGEINFO_REPORT ">" DEBUG_CR;

  *catalog = NULL;

  /* Construct the request body. */
  svn_stringbuf_appendcstr(request_body, minfo_report_head);
  svn_stringbuf_appendcstr(request_body,
                           apr_psprintf(pool,
                                        "<S:revision>%ld"
                                        "</S:revision>", revision));
  svn_stringbuf_appendcstr(request_body,
                           apr_psprintf(pool,
                                        "<S:inherit>%s"
                                        "</S:inherit>",
                                        svn_inheritance_to_word(inherit)));

  if (include_descendants)
    {
      /* Send it only if true; server will default to "no". */
      svn_stringbuf_appendcstr(request_body,
                               "<S:include-descendants>yes"
                               "</S:include-descendants>");
    }

  if (paths)
    {
      for (i = 0; i < paths->nelts; i++)
        {
          const char *this_path =
            apr_xml_quote_string(pool,
                                 ((const char **)paths->elts)[i],
                                 0);
          svn_stringbuf_appendcstr(request_body, "<S:path>");
          svn_stringbuf_appendcstr(request_body, this_path);
          svn_stringbuf_appendcstr(request_body, "</S:path>");
        }
    }

  svn_stringbuf_appendcstr(request_body, minfo_report_tail);

  mb.pool = pool;
  mb.curr_path = svn_stringbuf_create("", pool);
  mb.curr_info = svn_stringbuf_create("", pool);
  mb.catalog = apr_hash_make(pool);
  mb.err = SVN_NO_ERROR;

  /* ras's URL may not exist in HEAD, and thus it's not safe to send
     it as the main argument to the REPORT request; it might cause
     dav_get_resource() to choke on the server.  So instead, we pass a
     baseline-collection URL, which we get from END. */
  SVN_ERR(svn_ra_neon__get_baseline_info(NULL, &bc_url, &bc_relative, NULL,
                                         ras, ras->url->data, revision,
                                         pool));
  final_bc_url = svn_path_url_add_component(bc_url.data, bc_relative.data,
                                            pool);

  SVN_ERR(svn_ra_neon__parsed_request(ras,
                                      "REPORT",
                                      final_bc_url,
                                      request_body->data,
                                      NULL, NULL,
                                      start_element,
                                      cdata_handler,
                                      end_element,
                                      &mb,
                                      NULL,
                                      &status_code,
                                      FALSE,
                                      pool));

  if (mb.err == SVN_NO_ERROR && apr_hash_count(mb.catalog))
    *catalog = mb.catalog;

  return mb.err;
}
