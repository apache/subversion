/*
 * url.c:  converting paths to urls
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */



#include <apr_pools.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_types.h"
#include "svn_opt.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_dirent_uri.h"
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
  svn_client_ctx_t *ctx;
  
  SVN_ERR(svn_client_create_context(&ctx, pool));

  if (!svn_path_is_url(path_or_url))
    SVN_ERR(svn_dirent_get_absolute(&path_or_url, path_or_url, pool));

  revision.kind = svn_opt_revision_unspecified;
  return svn_client__derive_location(url, NULL, path_or_url, &revision,
                                     NULL, ctx, pool, pool);
}


svn_error_t *
svn_client_root_url_from_path(const char **url,
                              const char *path_or_url,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *pool)
{
  svn_opt_revision_t peg_revision;

  if (svn_path_is_url(path_or_url))
    {
      peg_revision.kind = svn_opt_revision_head;
    }
  else
    {
      peg_revision.kind = svn_opt_revision_base;
      SVN_ERR(svn_dirent_get_absolute(&path_or_url, path_or_url, pool));
    }
  return svn_client__get_repos_root(url, path_or_url, &peg_revision,
                                    ctx, pool, pool);
}

svn_error_t *
svn_client__derive_location(const char **url,
                            svn_revnum_t *peg_revnum,
                            const char *abspath_or_url,
                            const svn_opt_revision_t *peg_revision,
                            svn_ra_session_t *ra_session,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  /* If PATH_OR_URL is a local path (not a URL), we need to transform
     it into a URL. */
  if (! svn_path_is_url(abspath_or_url))
    {
      const svn_wc_entry_t *entry;

      SVN_ERR(svn_wc__get_entry_versioned(&entry, ctx->wc_ctx, abspath_or_url,
                                          svn_node_unknown, FALSE, FALSE,
                                          scratch_pool, scratch_pool));

      SVN_ERR(svn_client__entry_location(url, peg_revnum, abspath_or_url,
                                         peg_revision->kind, entry,
                                         result_pool));
    }
  else
    {
      *url = apr_pstrdup(result_pool, abspath_or_url);
      /* peg_revnum (if provided) will be set below. */
    }

  /* If we haven't resolved for ourselves a numeric peg revision, do so. */
  if (peg_revnum && !SVN_IS_VALID_REVNUM(*peg_revnum))
    {
      if (ra_session == NULL)
        {
          SVN_ERR(svn_client__open_ra_session_internal(&ra_session, *url, NULL,
                                                       NULL, NULL, FALSE,
                                                       TRUE, ctx,
                                                       scratch_pool));
        }
      SVN_ERR(svn_client__get_revision_number(peg_revnum, NULL, ctx->wc_ctx,
                                              NULL, ra_session, peg_revision,
                                              scratch_pool));
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
                               svn_dirent_local_style(wc_path, pool));
    }

  return SVN_NO_ERROR;
}
