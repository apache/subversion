/*
 * get-location-segments.c: mod_dav_svn versioning provider functions
 *                          for Subversion's get-location-segments RA API.
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

#include <apr_tables.h>
#include <apr_uuid.h>

#include <httpd.h>
#include <http_log.h>
#include <mod_dav.h>

#include "svn_fs.h"
#include "svn_xml.h"
#include "svn_repos.h"
#include "svn_dav.h"
#include "svn_time.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_dav.h"
#include "svn_base64.h"

#include "../dav_svn.h"


struct location_segment_baton
{
  svn_boolean_t sent_opener;
  ap_filter_t *output;
  apr_bucket_brigade *bb;
  dav_svn__authz_read_baton arb;
};


/* Send the get-location-segments-report XML open tag if it hasn't
   been sent already.  */
static svn_error_t *
maybe_send_opener(struct location_segment_baton *b)
{
  if (! b->sent_opener)
    {
      SVN_ERR(dav_svn__send_xml(b->bb, b->output, DAV_XML_HEADER DEBUG_CR
                                "<S:get-location-segments-report xmlns:S=\""
                                SVN_XML_NAMESPACE "\" xmlns:D=\"DAV:\">"
                                DEBUG_CR));
      b->sent_opener = TRUE;
    }
  return SVN_NO_ERROR;
}


/* Implements `svn_location_segment_receiver_t'; helper for
   dav_svn__get_location_segments_report(). */
static svn_error_t *
location_segment_receiver(svn_location_segment_t *segment,
                          void *baton,
                          apr_pool_t *pool)
{
  struct location_segment_baton *b = baton;
  apr_status_t apr_err;

  SVN_ERR(maybe_send_opener(b));

  if (segment->path)
    {
      const char *path_quoted = apr_xml_quote_string(pool, segment->path, 1);
      apr_err = ap_fprintf(b->output, b->bb,
                           "<S:location-segment path=\"%s\" "
                           "range-start=\"%ld\" range-end=\"%ld\"/>" DEBUG_CR,
                           path_quoted,
                           segment->range_start, segment->range_end);
    }
  else
    {
      apr_err = ap_fprintf(b->output, b->bb,
                           "<S:location-segment "
                           "range-start=\"%ld\" range-end=\"%ld\"/>" DEBUG_CR,
                           segment->range_start, segment->range_end);
    }
  if (apr_err)
    return svn_error_create(apr_err, 0, NULL);
  return SVN_NO_ERROR;
}


dav_error *
dav_svn__get_location_segments_report(const dav_resource *resource,
                                      const apr_xml_doc *doc,
                                      ap_filter_t *output)
{
  svn_error_t *serr;
  dav_error *derr = NULL;
  apr_bucket_brigade *bb;
  int ns;
  apr_xml_elem *child;
  const char *path = NULL;
  svn_revnum_t peg_revision = SVN_INVALID_REVNUM;
  svn_revnum_t start_rev = SVN_INVALID_REVNUM;
  svn_revnum_t end_rev = SVN_INVALID_REVNUM;
  dav_svn__authz_read_baton arb;
  struct location_segment_baton location_segment_baton;

  /* Sanity check. */
  ns = dav_svn__find_ns(doc->namespaces, SVN_XML_NAMESPACE);
  if (ns == -1)
    {
      return dav_svn__new_error_tag(resource->pool, HTTP_BAD_REQUEST, 0,
                                    "The request does not contain the 'svn:' "
                                    "namespace, so it is not going to have "
                                    "certain required elements.",
                                    SVN_DAV_ERROR_NAMESPACE,
                                    SVN_DAV_ERROR_TAG);
    }

  /* Gather the parameters. */
  for (child = doc->root->first_child; child != NULL; child = child->next)
    {
      /* If this element isn't one of ours, then skip it. */
      if (child->ns != ns)
        continue;

      if (strcmp(child->name, "peg-revision") == 0)
        {
          peg_revision = SVN_STR_TO_REV(dav_xml_get_cdata(child,
                                                          resource->pool, 1));
        }
      else if (strcmp(child->name, "start-revision") == 0)
        {
          start_rev = SVN_STR_TO_REV(dav_xml_get_cdata(child,
                                                       resource->pool, 1));
        }
      else if (strcmp(child->name, "end-revision") == 0)
        {
          end_rev = SVN_STR_TO_REV(dav_xml_get_cdata(child,
                                                     resource->pool, 1));
        }
      else if (strcmp(child->name, "path") == 0)
        {
          path = dav_xml_get_cdata(child, resource->pool, 0);
          if ((derr = dav_svn__test_canonical(path, resource->pool)))
            return derr;
          path = svn_path_join(resource->info->repos_path, path,
                               resource->pool);
        }
    }

  /* Check our inputs. */
  if (! path)
    return dav_svn__new_error_tag(resource->pool, HTTP_BAD_REQUEST, 0,
                                  "Not all parameters passed.",
                                  SVN_DAV_ERROR_NAMESPACE,
                                  SVN_DAV_ERROR_TAG);
  if (SVN_IS_VALID_REVNUM(start_rev)
      && SVN_IS_VALID_REVNUM(end_rev)
      && (end_rev > start_rev))
    return dav_svn__new_error_tag(resource->pool, HTTP_BAD_REQUEST, 0,
                                  "End revision must not be younger than "
                                  "start revision",
                                  SVN_DAV_ERROR_NAMESPACE,
                                  SVN_DAV_ERROR_TAG);
  if (SVN_IS_VALID_REVNUM(peg_revision)
      && SVN_IS_VALID_REVNUM(start_rev)
      && (start_rev > peg_revision))
    return dav_svn__new_error_tag(resource->pool, HTTP_BAD_REQUEST, 0,
                                  "Start revision must not be younger than "
                                  "peg revision",
                                  SVN_DAV_ERROR_NAMESPACE,
                                  SVN_DAV_ERROR_TAG);

  /* Build an authz read baton. */
  arb.r = resource->info->r;
  arb.repos = resource->info->repos;

  /* Build the bucket brigade we'll use for output. */
  bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);

  /* Do what we came here for. */
  location_segment_baton.sent_opener = FALSE;
  location_segment_baton.output = output;
  location_segment_baton.bb = bb;
  if ((serr = svn_repos_node_location_segments(resource->info->repos->repos,
                                               path, peg_revision,
                                               start_rev, end_rev,
                                               location_segment_receiver,
                                               &location_segment_baton,
                                               dav_svn__authz_read_func(&arb),
                                               &arb, resource->pool)))
    {
      derr = dav_svn__convert_err(serr, HTTP_BAD_REQUEST, serr->message,
                                  resource->pool);
      goto cleanup;
    }

  if ((serr = maybe_send_opener(&location_segment_baton)))
    {
      derr = dav_svn__convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                  "Error beginning REPORT response.",
                                  resource->pool);
      goto cleanup;
    }

  if ((serr = dav_svn__send_xml(bb, output,
                                "</S:get-location-segments-report>" DEBUG_CR)))
    {
      derr = dav_svn__convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                  "Error ending REPORT response.",
                                  resource->pool);
      goto cleanup;
    }

 cleanup:
  return dav_svn__final_flush_or_error(resource->info->r, bb, output,
                                       derr, resource->pool);
}
