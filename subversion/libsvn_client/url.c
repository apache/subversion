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

#include "private/svn_client_private.h"
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

      return svn_error_trace(
                 svn_wc__node_get_url(url, ctx->wc_ctx, path_or_url,
                                      result_pool, scratch_pool));
    }
  else
    *url = svn_uri_canonicalize(path_or_url, result_pool);

  return SVN_NO_ERROR;
}


svn_client_peg_t *
svn_client_peg_dup(const svn_client_peg_t *peg,
                   apr_pool_t *pool)
{
  svn_client_peg_t *peg2 = apr_pmemdup(pool, peg, sizeof(*peg2));

  peg2->path_or_url = apr_pstrdup(pool, peg->path_or_url);
  return peg2;
}

svn_client_peg_t *
svn_client_peg_create(const char *path_or_url,
                      const svn_opt_revision_t *peg_revision,
                      apr_pool_t *pool)
{
  svn_client_peg_t *peg = apr_palloc(pool, sizeof(*peg));

  peg->path_or_url = apr_pstrdup(pool, path_or_url);
  peg->peg_revision = *peg_revision;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__peg_resolve(svn_client_target_t **target_p,
                        svn_ra_session_t **session_p,
                        const svn_client_peg_t *peg,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_client__target(target_p, peg->path_or_url, &peg->peg_revision,
                             result_pool));
  *session_p = NULL;
  SVN_ERR(svn_client__resolve_target_location(*target_p, session_p,
                                              ctx, result_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__target(svn_client_target_t **target_p,
                   const char *path_or_url,
                   const svn_opt_revision_t *peg_revision,
                   apr_pool_t *pool)
{
  svn_opt_revision_t unspecified_rev = { svn_opt_revision_unspecified, { 0 } };
  *target_p = apr_pcalloc(pool, sizeof(**target_p));

  (*target_p)->pool = pool;
  (*target_p)->path_or_url = path_or_url;
  if (svn_path_is_url(path_or_url))
    (*target_p)->abspath_or_url = path_or_url;
  else
    svn_error_clear(svn_dirent_get_absolute(&(*target_p)->abspath_or_url,
                                    path_or_url, pool));
  (*target_p)->peg_revision = peg_revision ? *peg_revision : unspecified_rev;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__parse_target(svn_client_target_t **target,
                         const char *target_string,
                         apr_pool_t *pool)
{
  svn_opt_revision_t peg_revision;
  const char *path_or_url;

  SVN_ERR(svn_opt_parse_path(&peg_revision, &path_or_url, target_string,
                             pool));
  SVN_ERR(svn_client__target(target, path_or_url, &peg_revision, pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__resolve_location(const char **repo_root_url_p,
                             const char **repo_uuid_p,
                             svn_revnum_t *repo_revnum_p,
                             const char **repo_relpath_p,
                             svn_ra_session_t **ra_session_p,
                             const char *path_or_url,
                             const svn_opt_revision_t *peg_revision,
                             const svn_opt_revision_t *revision,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  const char *abspath_or_url;
  const char *repos_root_url;

  if (svn_path_is_url(path_or_url))
    abspath_or_url = path_or_url;
  else
    SVN_ERR(svn_dirent_get_absolute(&abspath_or_url, path_or_url,
                                    scratch_pool));

  SVN_ERR(svn_client_get_repos_root(&repos_root_url, repo_uuid_p,
                                    abspath_or_url,
                                    ctx, result_pool, scratch_pool));
  if (repo_root_url_p)
    *repo_root_url_p = repos_root_url;

  if (repo_relpath_p || repo_revnum_p)
    {
      svn_ra_session_t *ra_session;
      const char *url;

      SVN_ERR(svn_client__ra_session_from_path(&ra_session,
                                               repo_revnum_p,
                                               &url,
                                               abspath_or_url,
                                               NULL /* base_dir_abspath */,
                                               peg_revision, revision,
                                               ctx, result_pool));
      if (! url)
        return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                                 _("Path '%s' has no URL in the repository"),
                                 path_or_url);
      if (repo_relpath_p)
        *repo_relpath_p = svn_uri_skip_ancestor(repos_root_url, url, result_pool);
      if (ra_session_p != NULL && *ra_session_p == NULL)
        *ra_session_p = ra_session;
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__resolve_target_location(svn_client_target_t *target,
                                    svn_ra_session_t **ra_session_p,
                                    svn_client_ctx_t *ctx,
                                    apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_client__resolve_location(&target->repos_root_url,
                                       &target->repos_uuid,
                                       &target->repos_revnum,
                                       &target->repos_relpath,
                                       ra_session_p,
                                       target->path_or_url,
                                       &target->peg_revision,
                                       &target->revision,
                                       ctx, target->pool, scratch_pool));
  return SVN_NO_ERROR;
}

/* Set *MARKER to the value of the branch root marker property of TARGET. */
static svn_error_t *
get_branch_root_marker(const char **marker,
                       svn_client_target_t *target,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *pool)
{
  apr_hash_t *props;
  const char *propname = SVN_PROP_BRANCH_ROOT;
  svn_string_t *propval;

  SVN_ERR(svn_client_propget5(&props, propname, target, svn_depth_empty,
                              NULL, ctx, pool, pool));
  propval = apr_hash_get(props, target->abspath_or_url, APR_HASH_KEY_STRING);
  *marker = propval ? propval->data : NULL;

  /* ### if SVN_PROP_BRANCH_ROOT is "svn:ignore", for testing, just
   * look at the first 10 characters otherwise we'll see differences that
   * we don't care about and error messages will be unreadably long. */
  *marker = apr_pstrndup(pool, *marker, 10);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__check_branch_root_marker(const char **marker,
                                     svn_client_target_t *source,
                                     svn_client_target_t *target,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *pool)
{
  const char *source_marker, *target_marker;

  /* Check the source's and target's branch root marker properties. */
  SVN_ERR(get_branch_root_marker(&target_marker, target, ctx, pool));
  SVN_ERR(get_branch_root_marker(&source_marker, source, ctx, pool));
  if (! source_marker && ! target_marker)
    {
      /* Old-style branches, not marked as such. Marker will be NULL. */
    }
  else if (! source_marker || ! target_marker)
    {
      if (source_marker)
        return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                                 _("Source branch marker is '%s' but "
                                   "target has no branch marker"),
                                 source_marker);
      else
        return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                                 _("Target branch marker is '%s' but "
                                   "source has no branch marker"),
                                 target_marker);
    }
  else if (strcmp(source_marker, target_marker) != 0)
    {
      return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                               _("Source branch marker is '%s' but "
                                 "target branch marker is '%s'"),
                               source_marker, target_marker);
    }
  *marker = source_marker;
  return SVN_NO_ERROR;
}
