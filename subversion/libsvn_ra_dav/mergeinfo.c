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
#include "../libsvn_ra/ra_loader.h"

#include "ra_dav.h"

/* Baton for accumulating mergeinfo.  RESULT stores the final
   mergeinfo hash result we are going to hand back to the caller of
   get_mergeinfo.  curr_path and curr_info contain the value of the
   CDATA from the merge info items as we get them from the server.  */

struct mergeinfo_baton
{
  apr_pool_t *pool;
  const char *curr_path;
  const char *curr_info;
  apr_hash_t *result;
  svn_error_t *err;
};

static const svn_ra_dav__xml_elm_t mergeinfo_report_elements[] =
  {
    { SVN_XML_NAMESPACE, "merge-info-report", ELEM_merge_info_report, 0 },
    { SVN_XML_NAMESPACE, "merge-info-item", ELEM_merge_info_item, 0 },
    { SVN_XML_NAMESPACE, "merge-info-path", ELEM_merge_info_path,
      SVN_RA_DAV__XML_CDATA },
    { SVN_XML_NAMESPACE, "merge-info-info", ELEM_merge_info_info,
      SVN_RA_DAV__XML_CDATA },
    { NULL }
  };

static svn_error_t *
start_element(int *elem, void *baton, int parent_state, const char *nspace,
              const char *elt_name, const char **atts)
{
  struct mergeinfo_baton *mb = baton;

  const svn_ra_dav__xml_elm_t *elm
    = svn_ra_dav__lookup_xml_elem(mergeinfo_report_elements, nspace,
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
      if (elm->id != ELEM_merge_info_report)
        return UNEXPECTED_ELEMENT(nspace, elt_name);
    }

  if (elm->id == ELEM_merge_info_item)
    {
      mb->curr_info = NULL;
      mb->curr_path = NULL;
    }

  SVN_ERR(mb->err);

  *elem = elm->id;
  return SVN_NO_ERROR;
}

static svn_error_t *
end_element(void *baton, int state, const char *nspace, const char *elt_name)
{
  struct mergeinfo_baton *mb = baton;

  const svn_ra_dav__xml_elm_t *elm
    = svn_ra_dav__lookup_xml_elem(mergeinfo_report_elements, nspace,
                                  elt_name);
  if (! elm)
    return UNEXPECTED_ELEMENT(nspace, elt_name);

  if (elm->id == ELEM_merge_info_item)
    {
      if (mb->curr_info && mb->curr_path)
        {
          apr_hash_t *temp;

          mb->err = svn_mergeinfo_parse(mb->curr_info, &temp, mb->pool);
          SVN_ERR(mb->err);

          apr_hash_set(mb->result, mb->curr_path,  APR_HASH_KEY_STRING,
                       temp);
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
    case ELEM_merge_info_path:
      mb->curr_path = apr_pstrndup(mb->pool, cdata, nlen);
      break;

    case ELEM_merge_info_info:
      if (mb->curr_info)
        {
          char *to_append = apr_pstrmemdup(mb->pool, cdata, len);
          mb->curr_info = apr_pstrcat(mb->pool, mb->curr_info, to_append,
                                      NULL);
        }
      else
        {
          mb->curr_info = apr_pstrndup(mb->pool, cdata, nlen);
        }
      break;

    default:
      break;
    }
  SVN_ERR(mb->err);

  return SVN_NO_ERROR;
}

/* Request a merge-info-report from the URL attached to SESSION,
   and fill in the MERGEINFO hash with the results.  */
svn_error_t * svn_ra_dav__get_merge_info(svn_ra_session_t *session,
                                         apr_hash_t **mergeinfo,
                                         const apr_array_header_t *paths,
                                         svn_revnum_t revision,
                                         svn_boolean_t include_parents,
                                         apr_pool_t *pool)
{
  /* The Plan: Send a request to the server for a merge-info report.
   */

  int i;
  svn_ra_dav__session_t *ras = session->priv;
  svn_stringbuf_t *request_body = svn_stringbuf_create("", pool);
  struct mergeinfo_baton mb = { NULL, NULL, NULL, NULL, NULL };

  /* ### todo: I don't understand why the static, file-global
     variables shared by update and status are called `report_head'
     and `report_tail', instead of `request_head' and `request_tail'.
     Maybe Greg can explain?  Meanwhile, I'm tentatively using
     "request_*" for my local vars below. */

  static const char minfo_request_head[]
    = "<S:merge-info-report xmlns:S=\"" SVN_XML_NAMESPACE "\">" DEBUG_CR;

  static const char minfo_request_tail[] = "</S:merge-info-report>" DEBUG_CR;

  /* Construct the request body. */
  svn_stringbuf_appendcstr(request_body, minfo_request_head);
  svn_stringbuf_appendcstr(request_body,
                           apr_psprintf(pool,
                                        "<S:revision>%ld"
                                        "</S:revision>", revision));
  if (include_parents)
    {
      svn_stringbuf_appendcstr(request_body,
                               apr_psprintf(pool,
                                            "<S:include-parents/>"));
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

  svn_stringbuf_appendcstr(request_body, minfo_request_tail);

  mb.pool = pool;
  mb.err = SVN_NO_ERROR;
  mb.curr_path = NULL;
  mb.curr_info = NULL;
  mb.result = apr_hash_make(pool);

  SVN_ERR (svn_ra_dav__parsed_request(ras->sess,
                                      "REPORT",
                                      ras->url->data,
                                      request_body->data,
                                      NULL, NULL,
                                      start_element,
                                      cdata_handler,
                                      end_element,
                                      &mb,
                                      NULL,
                                      NULL,
                                      FALSE,
                                      pool));
  if (mb.err == SVN_NO_ERROR)
    *mergeinfo = mb.result;

  return mb.err;
}
