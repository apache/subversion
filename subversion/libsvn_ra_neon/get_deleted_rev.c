/*
 * get_deleted_rev.c :  ra_neon get_deleted_rev API implementation.
 *
 * ====================================================================
 * Copyright (c) 2008-2009 CollabNet.  All rights reserved.
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
#include <apr_strings.h>
#include <apr_xml.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_ra.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_path.h"
#include "svn_xml.h"

#include "private/svn_dav_protocol.h"
#include "svn_private_config.h"

#include "ra_neon.h"

/* -------------------------------------------------------------------------
 *
 * DELETED REV REPORT HANDLING
 *
 * The get-deleted-rev-report XML request body is quite straightforward:
 *
 *   <S:get-deleted-rev-report xmlns:S="svn:" xmlns:D="DAV:">
 *     <S:path>...</S:path>
 *     <S:peg-revision>...</S:peg-revision>
 *     <S:end-revision>...</S:end-revision>
 *   </S:get-deleted-rev-report>
 *
 * The response is simply a DAV:version-name element giving the revision
 * path@peg-revision was first deleted up to end-revision or SVN_INVALID_REVNUM
 * if it was never deleted:
 *
 *  <S:get-deleted-rev-report xmlns:S="svn:" xmlns:D="DAV:">
 *    <D:version-name>...</D:version-name>
 *  </S:get-deleted-rev-report>
 */

/* Elements used in a get-deleted-rev-report response. */
static const svn_ra_neon__xml_elm_t drev_report_elements[] =
{
  { SVN_XML_NAMESPACE, "get-deleted-rev-report", ELEM_deleted_rev_report, 0 },
  { "DAV:", "version-name", ELEM_version_name, SVN_RA_NEON__XML_CDATA },
  { NULL }
};

/* Context for parsing server's response. */
typedef struct
{
  svn_stringbuf_t *cdata;
  svn_revnum_t revision;
  apr_pool_t *pool;
} drev_baton_t;


/* This implements the 'svn_ra_neon__startelm_cb_t' prototype. */
static svn_error_t *
drev_start_element(int *elem, void *baton, int parent,
                   const char *nspace, const char *name, const char **atts)
{
  const svn_ra_neon__xml_elm_t *elm =
    svn_ra_neon__lookup_xml_elem(drev_report_elements, nspace, name);
  drev_baton_t *b = baton;

  *elem = elm ? elm->id : SVN_RA_NEON__XML_DECLINE;
  if (!elm)
    return SVN_NO_ERROR;

  if (elm->id == ELEM_version_name)
    b->cdata = svn_stringbuf_create("", b->pool);

  return SVN_NO_ERROR;
}

/* This implements the 'svn_ra_neon__endelm_cb_t' prototype. */
static svn_error_t *
drev_end_element(void *baton, int state,
                 const char *nspace, const char *name)
{
  drev_baton_t *b = baton;

  if (state == ELEM_version_name && b->cdata)
    {
      b->revision = SVN_STR_TO_REV(b->cdata->data);
      b->cdata = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_neon__get_deleted_rev(svn_ra_session_t *session,
                             const char *path,
                             svn_revnum_t peg_revision,
                             svn_revnum_t end_revision,
                             svn_revnum_t *revision_deleted,
                             apr_pool_t *pool)
{
  svn_ra_neon__session_t *ras = session->priv;
  const char *body, *final_bc_url;
  svn_string_t bc_url, bc_relative;
  int status_code;
  svn_error_t *err;
  drev_baton_t *b = apr_palloc(pool, sizeof(*b));

  b->pool = pool;
  b->cdata = NULL;
  b->revision = SVN_INVALID_REVNUM;

  /* ras's URL may not exist in HEAD, and thus it's not safe to send
     it as the main argument to the REPORT request; it might cause
     dav_get_resource() to choke on the server.  So instead, we pass a
     baseline-collection URL, which we get from the peg revision.  */
  SVN_ERR(svn_ra_neon__get_baseline_info(NULL, &bc_url, &bc_relative, NULL,
                                         ras, ras->url->data,
                                         peg_revision,
                                         pool));
  final_bc_url = svn_path_url_add_component(bc_url.data, bc_relative.data,
                                            pool);

  body = apr_psprintf(pool,
                      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                      "<S:get-deleted-rev-report xmlns:S=\""
                      SVN_XML_NAMESPACE "\" ""xmlns:D=\"DAV:\">"
                      "<S:path>%s</S:path>"
                      "<S:peg-revision>%s</S:peg-revision>"
                      "<S:end-revision>%s</S:end-revision>"
                      "</S:get-deleted-rev-report>",
                      apr_xml_quote_string(pool, path, FALSE),
                      apr_psprintf(pool, "%ld", peg_revision),
                      apr_psprintf(pool, "%ld", end_revision));

  /* Send the get-deleted-rev-report report request.  There is no guarantee
     that svn_ra_neon__parsed_request() will set status_code, so initialize
     it. */
  status_code = 0;
  err = svn_ra_neon__parsed_request(ras, "REPORT", final_bc_url, body,
                                    NULL, NULL,
                                    drev_start_element,
                                    svn_ra_neon__xml_collect_cdata,
                                    drev_end_element,
                                    b, NULL, &status_code, FALSE, pool);

  /* Map status 501: Method Not Implemented to our not implemented error.
     1.5.x servers and older don't support this report. */
  if (status_code == 501)
    return svn_error_createf(SVN_ERR_RA_NOT_IMPLEMENTED, err,
                             _("'%s' REPORT not implemented"), "get-deleted-rev");

  SVN_ERR(err);
  *revision_deleted = b->revision;
  return SVN_NO_ERROR;
}
