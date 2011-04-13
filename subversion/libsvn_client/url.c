/*
 * url.c:  converting paths to urls
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
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
svn_client_url_from_path2(const char **url,
                          const char *path_or_url,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  if (!svn_path_is_url(path_or_url))
    {
      SVN_ERR(svn_dirent_get_absolute(&path_or_url, path_or_url,
                                      scratch_pool));

      return svn_error_return(
                 svn_wc__node_get_url(url, ctx->wc_ctx, path_or_url,
                                      result_pool, scratch_pool));
    }
  else
    *url = apr_pstrdup(result_pool, path_or_url);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_root_url_from_path(const char **url,
                              const char *path_or_url,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *pool)
{
  if (!svn_path_is_url(path_or_url))
    SVN_ERR(svn_dirent_get_absolute(&path_or_url, path_or_url, pool));

  return svn_error_return(
           svn_client__get_repos_root(url, path_or_url,
                                      ctx, pool, pool));
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
      /* If we need to contact the repository for *PEG_REVNUM just get
         the *URL now.  Otherwise the working copy has all the information
         we need. */
      if (peg_revision->kind == svn_opt_revision_date
          || peg_revision->kind == svn_opt_revision_head)
        SVN_ERR(svn_wc__node_get_url(url, ctx->wc_ctx, abspath_or_url,
                                     result_pool, scratch_pool));
      else
        SVN_ERR(svn_client__entry_location(url, peg_revnum, ctx->wc_ctx,
                                           abspath_or_url, peg_revision->kind,
                                           result_pool, scratch_pool));
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
          SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL,
                                                       *url, NULL, NULL,
                                                       FALSE, TRUE,
                                                       ctx, scratch_pool));
        }
      SVN_ERR(svn_client__get_revision_number(peg_revnum, NULL, ctx->wc_ctx,
                                              NULL, ra_session, peg_revision,
                                              scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__entry_location(const char **url,
                           svn_revnum_t *revnum,
                           svn_wc_context_t *wc_ctx,
                           const char *local_abspath,
                           enum svn_opt_revision_kind peg_rev_kind,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  const char *copyfrom_url;
  svn_revnum_t copyfrom_rev;

  /* This function doesn't contact the repository, so error out if
     asked to do so. */
  if (peg_rev_kind == svn_opt_revision_date
      || peg_rev_kind == svn_opt_revision_head)
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL, NULL);

  SVN_ERR(svn_wc__node_get_copyfrom_info(NULL, NULL,
                                         &copyfrom_url, &copyfrom_rev,
                                         NULL, wc_ctx, local_abspath,
                                         result_pool, scratch_pool));

  if (copyfrom_url && peg_rev_kind == svn_opt_revision_working)
    {
      *url = copyfrom_url;
      if (revnum)
        *revnum = copyfrom_rev;
    }
  else
    {
      const char *node_url;

      SVN_ERR(svn_wc__node_get_url(&node_url, wc_ctx, local_abspath,
                                   result_pool, scratch_pool));
      if (node_url)
        {
          *url = node_url;
          if (revnum)
            {
              if ((peg_rev_kind == svn_opt_revision_committed) ||
                  (peg_rev_kind == svn_opt_revision_previous))
                {
                  SVN_ERR(svn_wc__node_get_changed_info(revnum, NULL, NULL,
                                                        wc_ctx,
                                                        local_abspath,
                                                        result_pool,
                                                        scratch_pool));
                  if (peg_rev_kind == svn_opt_revision_previous)
                    *revnum = *revnum - 1;
                }
              else
                {
                  /* Local modifications are not relevant here, so consider
                     svn_opt_revision_unspecified, svn_opt_revision_number,
                     svn_opt_revision_base, and svn_opt_revision_working
                     as the same. */
                  SVN_ERR(svn_wc__node_get_base_rev(revnum,
                                                    wc_ctx, local_abspath,
                                                    scratch_pool));
                }
            }
        }
      else
        {
          return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                                   _("Entry for '%s' has no URL"),
                                   svn_dirent_local_style(local_abspath,
                                                          scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}
