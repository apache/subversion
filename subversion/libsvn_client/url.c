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


svn_error_t *
svn_client_root_url_from_path(const char **url,
                              const char *path_or_url,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *pool)
{
  if (!svn_path_is_url(path_or_url))
    SVN_ERR(svn_dirent_get_absolute(&path_or_url, path_or_url, pool));

  return svn_error_trace(
           svn_client__get_repos_root(url, NULL, path_or_url,
                                      ctx, pool, pool));
}

svn_error_t *
svn_client__target(svn_client_target_t **target_p,
                   const char *path_or_url,
                   const svn_opt_revision_t *peg_revision,
                   apr_pool_t *pool)
{
  *target_p = apr_pcalloc(pool, sizeof(**target_p));

  (*target_p)->pool = pool;
  (*target_p)->path_or_url = path_or_url;
  if (svn_path_is_url(path_or_url))
    (*target_p)->abspath_or_url = path_or_url;
  else
    svn_error_clear(svn_dirent_get_absolute(&(*target_p)->abspath_or_url,
                                    path_or_url, pool));
  (*target_p)->peg_revision = *peg_revision;
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
                             const char *path_or_url,
                             const svn_opt_revision_t *peg_revision,
                             const svn_opt_revision_t *revision,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool)
{
  const char *abspath_or_url;
  const char *repos_root_url;

  if (svn_path_is_url(path_or_url))
    abspath_or_url = path_or_url;
  else
    SVN_ERR(svn_dirent_get_absolute(&abspath_or_url, path_or_url, pool));

  SVN_ERR(svn_client__get_repos_root(&repos_root_url, repo_uuid_p,
                                     abspath_or_url,
                                     ctx, pool, pool));
  if (repo_root_url_p)
    *repo_root_url_p = repos_root_url;

  if (repo_relpath_p)
    {
      const char *url;

      SVN_ERR(svn_client_url_from_path2(&url, path_or_url, ctx, pool, pool));
      if (! url)
        return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                                 _("Path '%s' has no URL in the repository"),
                                 path_or_url);
      *repo_relpath_p = svn_uri_skip_ancestor(repos_root_url, url, pool);
    }

  if (repo_revnum_p)
    {
      svn_ra_session_t *ra_session = NULL;

      SVN_ERR(svn_client__get_revision_number(repo_revnum_p,
                                              NULL /* youngest */,
                                              ctx->wc_ctx,
                                              abspath_or_url,
                                              ra_session,
                                              peg_revision,
                                              pool));
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__resolve_target_location(svn_client_target_t *target,
                                    svn_client_ctx_t *ctx,
                                    apr_pool_t *pool)
{
  SVN_ERR(svn_client__resolve_location(&target->repos_root_url,
                                       &target->repos_uuid,
                                       &target->repos_revnum,
                                       &target->repos_relpath,
                                       target->path_or_url,
                                       &target->peg_revision,
                                       &target->revision,
                                       ctx, pool));
  return SVN_NO_ERROR;
}

/* Set *MARKER to the value of the branch-root-identifier of TARGET. */
static svn_error_t *
get_branch_root_marker(const char **marker,
                       svn_client_target_t *target,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *pool)
{
  apr_hash_t *props;
  const char *propname = SVN_PROP_BRANCHING_ROOT;
  svn_string_t *propval;

  SVN_ERR(svn_client_propget5(&props, propname, target, svn_depth_empty,
                              NULL, ctx, pool, pool));
  propval = apr_hash_get(props, target->abspath_or_url, APR_HASH_KEY_STRING);
  *marker = propval ? propval->data : NULL;
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

  /* Check the source's and target's branch-marker properties. */
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
                                 _("Source is marked as a branch of "
                                   "project '%s', but target is not marked"),
                                 source_marker);
      else
        return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                                 _("Target is marked as a branch of "
                                   "project '%s', but source is not marked"),
                                 target_marker);
    }
  else if (strcmp(source_marker, target_marker) != 0)
    {
      /* ### The '.99' is just for display tidiness when I'm messing about
       * with using 'svn:ignore' as the branch marker property. */
      return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                               _("error: Source is marked as branch of project '%.99s' "
                                 "but target is marked as branch of project '%.99s'"),
                               source_marker, target_marker);
    }
  *marker = source_marker;
  return SVN_NO_ERROR;
}
