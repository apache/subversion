/*
 * url.c:  converting paths to urls
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_types.h"
#include "svn_opt.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_path.h"

#include "private/svn_wc_private.h"
#include "client.h"
#include "svn_private_config.h"



svn_error_t *
svn_client_url_from_path(const char **url,
                         const char *path_or_url,
                         apr_pool_t *pool)
{
  svn_opt_revision_t revision;
  revision.kind = svn_opt_revision_unspecified;
  return svn_client__derive_location(url, NULL, path_or_url, &revision,
                                     NULL, NULL, NULL, pool);
}


svn_error_t *
svn_client_root_url_from_path(const char **url,
                              const char *path_or_url,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *pool)
{
  svn_opt_revision_t peg_revision;
  peg_revision.kind = svn_path_is_url(path_or_url) ? svn_opt_revision_head
                                                   : svn_opt_revision_base;
  return svn_client__get_repos_root(url, path_or_url, &peg_revision,
                                    NULL, ctx, pool);
}

svn_error_t *
svn_client__derive_location(const char **url,
                            svn_revnum_t *peg_revnum,
                            const char *path_or_url,
                            const svn_opt_revision_t *peg_revision,
                            svn_ra_session_t *ra_session,
                            svn_wc_adm_access_t *adm_access,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  /* If PATH_OR_URL is a local path (not a URL), we need to transform
     it into a URL. */
  if (! svn_path_is_url(path_or_url))
    {
      const svn_wc_entry_t *entry;

      if (adm_access)
        {
          SVN_ERR(svn_wc__entry_versioned(&entry, path_or_url, adm_access,
                                          FALSE, pool));
        }
      else
        {
          svn_cancel_func_t cancel_func = NULL;
          void *cancel_baton = NULL;

          if (ctx)
            {
              cancel_func = ctx->cancel_func;
              cancel_baton = ctx->cancel_baton;
            }

          SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path_or_url,
                                         FALSE, 0, cancel_func, cancel_baton,
                                         pool));
          SVN_ERR(svn_wc__entry_versioned(&entry, path_or_url, adm_access,
                                          FALSE, pool));
          SVN_ERR(svn_wc_adm_close2(adm_access, pool));
        }

      SVN_ERR(svn_client__entry_location(url, peg_revnum, path_or_url,
                                         peg_revision->kind, entry, pool));
    }
  else
    {
      *url = path_or_url;
      /* peg_revnum (if provided) will be set below. */
    }

  /* If we haven't resolved for ourselves a numeric peg revision, do so. */
  if (peg_revnum && !SVN_IS_VALID_REVNUM(*peg_revnum))
    {
      /* Use sesspool to assure that if we opened an RA session, we
         close it. */
      apr_pool_t *sesspool = NULL;

      if (ra_session == NULL)
        {
          sesspool = svn_pool_create(pool);
          SVN_ERR(svn_client__open_ra_session_internal(&ra_session, *url, NULL,
                                                       NULL, NULL, FALSE,
                                                       TRUE, ctx, sesspool));
        }
      SVN_ERR(svn_client__get_revision_number(peg_revnum, NULL, ra_session,
                                              peg_revision, NULL, pool));
      if (sesspool)
        svn_pool_destroy(sesspool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__entry_location(const char **url, svn_revnum_t *revnum,
                           const char *wc_path,
                           enum svn_opt_revision_kind peg_rev_kind,
                           const svn_wc_entry_t *entry, apr_pool_t *pool)
{
  if (entry->copyfrom_url && peg_rev_kind == svn_opt_revision_working)
    {
      *url = entry->copyfrom_url;
      if (revnum)
        *revnum = entry->copyfrom_rev;
    }
  else if (entry->url)
    {
      *url = entry->url;
      if (revnum)
        *revnum = entry->revision;
    }
  else
    {
      return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                               _("Entry for '%s' has no URL"),
                               svn_path_local_style(wc_path, pool));
    }

  return SVN_NO_ERROR;
}
