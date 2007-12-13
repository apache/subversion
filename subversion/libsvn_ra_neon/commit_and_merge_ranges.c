/*
 * commit_and_merge_ranges.c :  routines for requesting and parsing 
 * commit-and-merge-ranges-report.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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
#include "private/svn_mergeinfo_private.h"

#include "ra_neon.h"

/* Baton for accumulating commit and merge_ranges.
*/

struct mergeinfo_baton
{
  apr_pool_t *pool;
  apr_array_header_t *merge_ranges_list;
  apr_array_header_t *commit_rangelist;
  svn_error_t *err;
};

static const svn_ra_neon__xml_elm_t commit_and_merge_ranges_report_elements[] =
  {
    { SVN_XML_NAMESPACE, SVN_DAV__COMMIT_AND_MERGE_RANGES_REPORT,
      ELEM_commit_and_merge_ranges_report, 0 },
    { SVN_XML_NAMESPACE, SVN_DAV__COMMIT_MERGE_INFO, ELEM_commit_mergeinfo, 0},
    { SVN_XML_NAMESPACE, SVN_DAV__MERGE_RANGES, ELEM_merge_ranges,
      SVN_RA_NEON__XML_CDATA},
    { SVN_XML_NAMESPACE, SVN_DAV__COMMIT_REV, ELEM_commit_rev,
      SVN_RA_NEON__XML_CDATA},
    { NULL }
  };

static svn_error_t *
start_element(int *elem, void *baton, int parent_state, const char *nspace,
              const char *elt_name, const char **atts)
{
  struct mergeinfo_baton *mb = baton;

  const svn_ra_neon__xml_elm_t *elm
    = svn_ra_neon__lookup_xml_elem(commit_and_merge_ranges_report_elements,
                                   nspace, elt_name);
  if (! elm)
    {
      *elem = NE_XML_DECLINE;
      return SVN_NO_ERROR;
    }

  if (parent_state == ELEM_root)
    {
      /* If we're at the root of the tree, the element has to be the editor
       * report itself. */
      if (elm->id != ELEM_commit_and_merge_ranges_report)
        return UNEXPECTED_ELEMENT(nspace, elt_name);
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
    = svn_ra_neon__lookup_xml_elem(commit_and_merge_ranges_report_elements,
                                   nspace, elt_name);
  if (! elm)
    return UNEXPECTED_ELEMENT(nspace, elt_name);

  return SVN_NO_ERROR;
}

static svn_error_t *
cdata_handler(void *baton, int state, const char *cdata, size_t len)
{
  struct mergeinfo_baton *mb = baton;
  apr_size_t nlen = len;
  char *endstr;
  svn_revnum_t commit_rev;
  const char *cdata_local = apr_pstrndup(mb->pool, cdata, nlen);
  svn_merge_range_t *range;
  apr_array_header_t *merge_rangelist;

  switch (state)
    {
    case ELEM_merge_ranges:
      SVN_ERR(svn_rangelist__parse(&merge_rangelist, cdata_local,
                                   FALSE, FALSE, mb->pool));
      APR_ARRAY_PUSH(mb->merge_ranges_list, 
                     apr_array_header_t *) = merge_rangelist;
      break;

    case ELEM_commit_rev:
      commit_rev = strtol(cdata_local, &endstr, 10);
      range = apr_pcalloc(mb->pool, sizeof(*range));
      range->start = commit_rev - 1;
      range->end = commit_rev;
      range->inheritable = TRUE;
      APR_ARRAY_PUSH(mb->commit_rangelist, svn_merge_range_t *) = range;
      break;

    default:
      break;
    }
  SVN_ERR(mb->err);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_neon__get_commit_and_merge_ranges(svn_ra_session_t *session,
                                        apr_array_header_t **merge_ranges_list,
                                        apr_array_header_t **commit_rangelist,
                                        const char* merge_target,
                                        const char* merge_source,
                                        svn_revnum_t min_commit_rev,
                                        svn_revnum_t max_commit_rev,
                                        svn_mergeinfo_inheritance_t inherit,
                                        apr_pool_t *pool)
{
  svn_error_t *err;
  int status_code;
  svn_ra_neon__session_t *ras = session->priv;
  svn_stringbuf_t *request_body = svn_stringbuf_create("", pool);
  struct mergeinfo_baton mb;
  svn_string_t bc_url, bc_relative;
  const char *final_bc_url;

  static const char minfo_report_head[] =
    "<S:" SVN_DAV__COMMIT_AND_MERGE_RANGES_REPORT
     " xmlns:S=\"" SVN_XML_NAMESPACE "\">"
    DEBUG_CR;

  static const char minfo_report_tail[] =
    "</S:" SVN_DAV__COMMIT_AND_MERGE_RANGES_REPORT ">" DEBUG_CR;

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
  svn_stringbuf_appendcstr(request_body,
                           apr_psprintf(pool,
                                        "<S:inherit>%s"
                                        "</S:inherit>",
                                        svn_inheritance_to_word(inherit)));

  svn_stringbuf_appendcstr(request_body, minfo_report_tail);

  *commit_rangelist = apr_array_make(pool, 0, sizeof(svn_merge_range_t *));
  *merge_ranges_list = apr_array_make(pool, 0, sizeof(apr_array_header_t *));

  mb.pool = pool;
  mb.merge_ranges_list = *merge_ranges_list;
  mb.commit_rangelist = *commit_rangelist;
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
     ### capabilities *before* sending our REPORT.

     ### We can do that, with svn_ra_has_capability()...  -Karl */
  if (status_code == 501)
    {
      *commit_rangelist = apr_array_make(pool, 0, sizeof(svn_merge_range_t *));
      *merge_ranges_list = apr_array_make(pool, 0, sizeof(svn_merge_range_t *));
      svn_error_clear(err);
    }
  else if (err)
    return err;
  return mb.err;
}
