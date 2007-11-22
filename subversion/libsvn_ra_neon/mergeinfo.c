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

/* Baton for accumulating mergeinfo.  RESULT stores the final
   mergeinfo hash result we are going to hand back to the caller of
   get_mergeinfo.  curr_path and curr_info contain the value of the
   CDATA from the mergeinfo items as we get them from the server.  */

struct mergeinfo_baton
{
  apr_pool_t *pool;
  const char *curr_path;
  svn_stringbuf_t *curr_info;
  apr_hash_t *result;
  svn_error_t *err;
};

static const svn_ra_neon__xml_elm_t mergeinfo_report_elements[] =
  {
    { SVN_XML_NAMESPACE, SVN_DAV__MERGEINFO_REPORT, ELEM_mergeinfo_report, 0 },
    { SVN_XML_NAMESPACE, SVN_DAV__COMMIT_REVS_FOR_MERGE_RANGES_REPORT,
      ELEM_mergeinfo_report, 0 },
    { SVN_XML_NAMESPACE, "mergeinfo-item", ELEM_mergeinfo_item, 0 },
    { SVN_XML_NAMESPACE, "mergeinfo-path", ELEM_mergeinfo_path,
      SVN_RA_NEON__XML_CDATA },
    { SVN_XML_NAMESPACE, "mergeinfo-info", ELEM_mergeinfo_info,
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

  const svn_ra_neon__xml_elm_t *elm
    = svn_ra_neon__lookup_xml_elem(mergeinfo_report_elements, nspace,
                                   elt_name);
  if (! elm)
    return UNEXPECTED_ELEMENT(nspace, elt_name);

  if (elm->id == ELEM_mergeinfo_item)
    {
      if (mb->curr_info && mb->curr_path)
        {
          apr_hash_t *path_mergeinfo;

          mb->err = svn_mergeinfo_parse(&path_mergeinfo, mb->curr_info->data,
                                        mb->pool);
          SVN_ERR(mb->err);

          apr_hash_set(mb->result, mb->curr_path,  APR_HASH_KEY_STRING,
                       path_mergeinfo);
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
      mb->curr_path = apr_pstrndup(mb->pool, cdata, nlen);
      break;

    case ELEM_mergeinfo_info:
      if (mb->curr_info)
        svn_stringbuf_appendbytes(mb->curr_info, cdata, nlen);
      break;

    default:
      break;
    }
  SVN_ERR(mb->err);

  return SVN_NO_ERROR;
}

/* Request a mergeinfo-report from the URL attached to SESSION,
   and fill in the MERGEINFO hash with the results.  */
svn_error_t *
svn_ra_neon__get_mergeinfo(svn_ra_session_t *session,
                           apr_hash_t **mergeinfo,
                           const apr_array_header_t *paths,
                           svn_revnum_t revision,
                           svn_mergeinfo_inheritance_t inherit,
                           apr_pool_t *pool)
{
  svn_error_t *err;
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
  mb.curr_path = NULL;
  mb.curr_info = svn_stringbuf_create("", pool);
  mb.result = apr_hash_make(pool);
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

  err = svn_ra_neon__parsed_request(ras,
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
                                    pool);
  /* If the server responds with HTTP_NOT_IMPLEMENTED, assume its
     mod_dav_svn is too old to understand the "mergeinfo-report" REPORT.

     ### It would be less expensive if we knew the server's
     ### capabilities *before* sending our REPORT. */
  if (status_code == 501)
    {
      *mergeinfo = NULL;
      svn_error_clear(err);
    }
  else if (err)
    return err;

  if (mb.err == SVN_NO_ERROR)
    *mergeinfo = mb.result;

  return mb.err;
}

/* Request a commit-revs-for-merge-ranges-report from the URL attached to
   SESSION, and fill in the commit_rev_rangelist array with the results.*/
svn_error_t *
svn_ra_neon__get_commit_revs_for_merge_ranges(
                                     svn_ra_session_t *session,
                                     apr_array_header_t **commit_rev_rangelist,
                                     const char* merge_target,
                                     const char* merge_source,
                                     svn_revnum_t min_commit_rev,
                                     svn_revnum_t max_commit_rev,
                                     const apr_array_header_t *merge_rangelist,
                                     svn_mergeinfo_inheritance_t inherit,
                                     apr_pool_t *pool)
{
  svn_error_t *err;
  int status_code;
  svn_ra_neon__session_t *ras = session->priv;
  svn_stringbuf_t *request_body = svn_stringbuf_create("", pool);
  struct mergeinfo_baton mb;
  svn_string_t bc_url, bc_relative;
  svn_stringbuf_t *merge_rangelist_str;
  const char *final_bc_url;

  static const char minfo_report_head[] =
    "<S:" SVN_DAV__COMMIT_REVS_FOR_MERGE_RANGES_REPORT
     " xmlns:S=\"" SVN_XML_NAMESPACE "\">"
    DEBUG_CR;

  static const char minfo_report_tail[] =
    "</S:" SVN_DAV__COMMIT_REVS_FOR_MERGE_RANGES_REPORT ">" DEBUG_CR;

  /* Construct the request body. */
  svn_stringbuf_appendcstr(request_body, minfo_report_head);
  svn_stringbuf_appendcstr(request_body, "<S:merge-target>");
  svn_stringbuf_appendcstr(request_body,
                           apr_xml_quote_string(pool, merge_target, 0));
  svn_stringbuf_appendcstr(request_body, "</S:merge-target>");
  svn_stringbuf_appendcstr(request_body, "<S:merge-source>");
  svn_stringbuf_appendcstr(request_body,
                           apr_xml_quote_string(pool, merge_source, 0));
  svn_stringbuf_appendcstr(request_body, "</S:merge-source>");
  svn_stringbuf_appendcstr(request_body,
                           apr_psprintf(pool,
                                        "<S:min-commit-revision>%ld"
                                        "</S:min-commit-revision>",
                                        min_commit_rev));
  svn_stringbuf_appendcstr(request_body,
                           apr_psprintf(pool,
                                        "<S:max-commit-revision>%ld"
                                        "</S:max-commit-revision>",
                                        max_commit_rev));
  SVN_ERR(svn_rangelist_to_stringbuf(&merge_rangelist_str, merge_rangelist,
                                     pool));
  svn_stringbuf_appendcstr(request_body, "<S:merge-ranges>");
  svn_stringbuf_appendcstr(request_body, merge_rangelist_str->data);
  svn_stringbuf_appendcstr(request_body, "</S:merge-ranges>");
  svn_stringbuf_appendcstr(request_body,
                           apr_psprintf(pool,
                                        "<S:inherit>%s"
                                        "</S:inherit>",
                                        svn_inheritance_to_word(inherit)));

  svn_stringbuf_appendcstr(request_body, minfo_report_tail);

  mb.pool = pool;
  mb.curr_path = NULL;
  mb.curr_info = svn_stringbuf_create("", pool);
  mb.result = apr_hash_make(pool);
  mb.err = SVN_NO_ERROR;

  /* ras's URL may not exist in HEAD, and thus it's not safe to send
     it as the main argument to the REPORT request; it might cause
     dav_get_resource() to choke on the server.  So instead, we pass a
     baseline-collection URL, which we get from END. */
  SVN_ERR(svn_ra_neon__get_baseline_info(NULL, &bc_url, &bc_relative, NULL,
                                         ras, ras->url->data, max_commit_rev,
                                         pool));
  final_bc_url = svn_path_url_add_component(bc_url.data, bc_relative.data,
                                            pool);

  err = svn_ra_neon__parsed_request(ras,
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
                                    pool);
  /* If the server responds with HTTP_NOT_IMPLEMENTED, assume its
     mod_dav_svn is too old to understand the "mergeinfo-report" REPORT.

     ### It would be less expensive if we knew the server's
     ### capabilities *before* sending our REPORT. */
  if (status_code == 501)
    {
      *commit_rev_rangelist = apr_array_make(pool, 0,
                                             sizeof(svn_merge_range_t *));
      svn_error_clear(err);
    }
  else if (err)
    return err;
  else if (mb.err == SVN_NO_ERROR)
    {
      apr_hash_t *target_mergeinfo = apr_hash_get(mb.result, merge_target,
                                                  APR_HASH_KEY_STRING);
      if (target_mergeinfo)
        *commit_rev_rangelist = apr_hash_get(target_mergeinfo, merge_source,
                                             APR_HASH_KEY_STRING);
    }

  return mb.err;
}
