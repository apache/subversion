/*
 * util.c: some handy utilities functions
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <mod_dav.h>

#include "svn_error.h"
#include "dav_svn.h"


dav_error * dav_svn_convert_err(const svn_error_t *serr, int status,
                                const char *message)
{
    dav_error *derr;

    derr = dav_new_error(serr->pool, status, serr->apr_err, serr->message);
    if (message != NULL)
        derr = dav_push_error(serr->pool, status, serr->apr_err,
                              message, derr);
    return derr;
}

const char *dav_svn_build_uri(const dav_resource *resource,
                              enum dav_svn_build_what what,
                              svn_revnum_t revision,
                              const char *path,
                              int add_href,
                              apr_pool_t *pool)
{
  const char *root_path = resource->info->repos->root_path;
  const char *special_uri = resource->info->repos->special_uri;
  const char *href1 = add_href ? "<D:href>" : "";
  const char *href2 = add_href ? "</D:href>" : "";

  switch (what)
    {
    case DAV_SVN_BUILD_URI_ACT_COLLECTION:
      return apr_psprintf(pool, "%s%s/%s/act/%s",
                          href1, root_path, special_uri, href2);

    case DAV_SVN_BUILD_URI_BC:
      return apr_psprintf(pool, "%s%s/%s/bc/%ld/%s",
                          href1, root_path, special_uri, revision, href2);

    case DAV_SVN_BUILD_URI_VERSION:
      /* path is the STABLE_ID */
      return apr_psprintf(pool, "%s%s/%s/ver/%s%s",
                          href1, root_path, special_uri, path, href2);

    case DAV_SVN_BUILD_URI_BASELINE:
      return apr_psprintf(pool, "%s%s/%s/bln/%ld%s",
                          href1, root_path, special_uri, revision, href2);

    case DAV_SVN_BUILD_URI_VCC:
      return apr_psprintf(pool, "%s%s/%s/vcc/" DAV_SVN_DEFAULT_VCC_NAME "%s",
                          href1, root_path, special_uri, href2);

    default:
      /* programmer error somewhere */
      abort();
      return NULL;
    }

  /* NOTREACHED */
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
