/*
 * merge.c: handle the MERGE response processing
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <apr_pools.h>
#include <apr_buckets.h>
#include <apr_xml.h>

#include <httpd.h>
#include <util_filter.h>

#include "svn_fs.h"

#include "dav_svn.h"

dav_error * dav_svn__merge_response(ap_filter_t *output,
                                    svn_fs_t *repos,
                                    svn_revnum_t new_rev,
                                    apr_xml_elem *prop_elem,
                                    apr_pool_t *pool)
{
  apr_bucket_brigade *bb;
  svn_fs_root_t *committed_root;
  svn_fs_root_t *previous_root;
  svn_error_t *serr;

  serr = svn_fs_revision_root(&committed_root, repos, new_rev, pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not open the FS root for the "
                                 "revision just committed.");
    }
  serr = svn_fs_revision_root(&previous_root, repos, new_rev - 1, pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not open the FS root for the "
                                 "previous revision.");
    }

  bb = apr_brigade_create(pool);
  (void) ap_fputs(output, bb,
                  DAV_XML_HEADER DEBUG_CR
                  "<D:merge-response xmlns:D=\"DAV:\">" DEBUG_CR
                  "<D:merged-set>" DEBUG_CR);

  /* ### more work here... */

  (void) ap_fputs(output, bb,
                  "</D:merged-set>" DEBUG_CR
                  "</D:merge-response>" DEBUG_CR);

  /* send whatever is left in the brigade */
  (void) ap_pass_brigade(output, bb);
  
  return NULL;
}
