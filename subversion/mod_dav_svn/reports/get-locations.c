/*
 * get-locations.c:  generate the 'get locations' report response.
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


static apr_status_t
send_get_locations_report(ap_filter_t *output,
                          apr_bucket_brigade *bb,
                          const dav_resource *resource,
                          apr_hash_t *fs_locations)
{
  apr_hash_index_t *hi;
  apr_pool_t *pool;
  apr_status_t apr_err;

  pool = resource->pool;

  apr_err = ap_fprintf(output, bb, DAV_XML_HEADER DEBUG_CR
                       "<S:get-locations-report xmlns:S=\"" SVN_XML_NAMESPACE
                       "\" xmlns:D=\"DAV:\">" DEBUG_CR);
  if (apr_err)
    return apr_err;

  for (hi = apr_hash_first(pool, fs_locations); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *value;
      const char *path_quoted;

      apr_hash_this(hi, &key, NULL, &value);
      path_quoted = apr_xml_quote_string(pool, value, 1);
      apr_err = ap_fprintf(output, bb, "<S:location "
                           "rev=\"%ld\" path=\"%s\"/>" DEBUG_CR,
                           *(const svn_revnum_t *)key, path_quoted);
      if (apr_err)
        return apr_err;
    }
  return ap_fprintf(output, bb, "</S:get-locations-report>" DEBUG_CR);
}


dav_error *
dav_svn__get_locations_report(const dav_resource *resource,
                              const apr_xml_doc *doc,
                              ap_filter_t *output)
{
  svn_error_t *serr;
  dav_error *derr = NULL;
  apr_status_t apr_err;
  apr_bucket_brigade *bb;
  dav_svn__authz_read_baton arb;

  /* The parameters to do the operation on. */
  const char *relative_path = NULL;
  const char *abs_path;
  svn_revnum_t peg_revision = SVN_INVALID_REVNUM;
  apr_array_header_t *location_revisions;

  /* XML Parsing Variables */
  int ns;
  apr_xml_elem *child;

  apr_hash_t *fs_locations;

  location_revisions = apr_array_make(resource->pool, 0,
                                      sizeof(svn_revnum_t));

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
        peg_revision = SVN_STR_TO_REV(dav_xml_get_cdata(child,
                                                        resource->pool, 1));
      else if (strcmp(child->name, "location-revision") == 0)
        {
          svn_revnum_t revision
            = SVN_STR_TO_REV(dav_xml_get_cdata(child, resource->pool, 1));
          APR_ARRAY_PUSH(location_revisions, svn_revnum_t) = revision;
        }
      else if (strcmp(child->name, "path") == 0)
        {
          relative_path = dav_xml_get_cdata(child, resource->pool, 0);
          if ((derr = dav_svn__test_canonical(relative_path, resource->pool)))
            return derr;
        }
    }

  /* Now we should have the parameters ready - let's
     check if they are all present. */
  if (! (relative_path && SVN_IS_VALID_REVNUM(peg_revision)))
    {
      return dav_svn__new_error_tag(resource->pool, HTTP_BAD_REQUEST, 0,
                                    "Not all parameters passed.",
                                    SVN_DAV_ERROR_NAMESPACE,
                                    SVN_DAV_ERROR_TAG);
    }

  /* Append the relative path to the base FS path to get an absolute
     repository path. */
  abs_path = svn_path_join(resource->info->repos_path, relative_path,
                           resource->pool);

  /* Build an authz read baton */
  arb.r = resource->info->r;
  arb.repos = resource->info->repos;

  serr = svn_repos_trace_node_locations(resource->info->repos->fs,
                                        &fs_locations, abs_path, peg_revision,
                                        location_revisions,
                                        dav_svn__authz_read_func(&arb), &arb,
                                        resource->pool);

  if (serr)
    {
      return dav_svn__convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                  serr->message, resource->pool);
    }

  bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);

  apr_err = send_get_locations_report(output, bb, resource, fs_locations);

  if (apr_err)
    derr = dav_svn__convert_err(svn_error_create(apr_err, 0, NULL),
                                HTTP_INTERNAL_SERVER_ERROR,
                                "Error writing REPORT response.",
                                resource->pool);

  return dav_svn__final_flush_or_error(resource->info->r, bb, output,
                                       derr, resource->pool);
}
