/*
 * deleted-rev :  routine for getting the revision a path was deleted.
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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

#include <apr_xml.h>

#include <httpd.h>
#include <mod_dav.h>

#include "svn_xml.h"
#include "svn_repos.h"
#include "svn_dav.h"
#include "svn_pools.h"

#include "private/svn_dav_protocol.h"

#include "../dav_svn.h"

/* Respond to a S:deleted-rev-report request. */
dav_error *
dav_svn__get_deleted_rev_report(const dav_resource *resource,
                                const apr_xml_doc *doc,
                                ap_filter_t *output)
{
  apr_xml_elem *child;
  int ns;
  const char *rel_path = NULL, *abs_path;
  svn_revnum_t peg_rev = SVN_INVALID_REVNUM, end_rev = SVN_INVALID_REVNUM,
      deleted_rev;
  apr_bucket_brigade *bb;
  svn_error_t *err;
  apr_status_t apr_err;
  dav_error *derr = NULL;

  /* Sanity check. */
  ns = dav_svn__find_ns(doc->namespaces, SVN_XML_NAMESPACE);
  if (ns == -1)
    return dav_svn__new_error_tag(resource->pool, HTTP_BAD_REQUEST, 0,
                                  "The request does not contain the 'svn:' "
                                  "namespace, so it is not going to have "
                                  "certain required elements.",
                                  SVN_DAV_ERROR_NAMESPACE,
                                  SVN_DAV_ERROR_TAG);

  for (child = doc->root->first_child; child != NULL; child = child->next)
    {
      /* If this element isn't one of ours, then skip it. */
      if (child->ns != ns )
        continue;

      if (strcmp(child->name, "peg-revision") == 0)
        {
          peg_rev = SVN_STR_TO_REV(dav_xml_get_cdata(child,
                                                     resource->pool, 1));
        }
      else if (strcmp(child->name, "end-revision") == 0)
        {
          end_rev = SVN_STR_TO_REV(dav_xml_get_cdata(child,
                                                     resource->pool, 1));
        }
      else if (strcmp(child->name, "path") == 0)
        {
          rel_path = dav_xml_get_cdata(child, resource->pool, 0);
          if ((derr = dav_svn__test_canonical(rel_path, resource->pool)))
            return derr;
        }
    }

    /* Check that all parameters are present. */
  if (! (rel_path
         && SVN_IS_VALID_REVNUM(peg_rev)
         && SVN_IS_VALID_REVNUM(end_rev)))
    {
      return dav_svn__new_error_tag(resource->pool, HTTP_BAD_REQUEST, 0,
                                    "Not all parameters passed.",
                                    SVN_DAV_ERROR_NAMESPACE,
                                    SVN_DAV_ERROR_TAG);
    }

  /* Append the relative path to the base FS path to get an absolute
     repository path. */
  abs_path = svn_path_join(resource->info->repos_path, rel_path,
                           resource->pool);

  /* Do what we actually came here for: Find the rev abs_path was deleted. */
  err = svn_repos_deleted_rev(resource->info->repos->fs,
                              abs_path, peg_rev, end_rev,
                              &deleted_rev, resource->pool);
  if (err)
    {
      svn_error_clear(err);
      return dav_new_error(resource->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                           "Could not find revision path was deleted.");
    }

  bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);
  apr_err = ap_fprintf(output, bb,
                       DAV_XML_HEADER DEBUG_CR
                       "<S:get-deleted-rev-report xmlns:S=\""
                       SVN_XML_NAMESPACE "\" xmlns:D=\"DAV:\">" DEBUG_CR
                       "<D:" SVN_DAV__VERSION_NAME ">%ld</D:"
                       SVN_DAV__VERSION_NAME ">""</S:get-deleted-rev-report>",
                       deleted_rev);
  if (apr_err)
    derr = dav_svn__convert_err(svn_error_create(apr_err, 0, NULL),
                                HTTP_INTERNAL_SERVER_ERROR,
                                "Error writing REPORT response.",
                                resource->pool);

  return dav_svn__final_flush_or_error(resource->info->r, bb, output,
                                       derr, resource->pool);
}
