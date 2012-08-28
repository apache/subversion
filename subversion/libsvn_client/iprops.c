/*
 * iprops.c:  wrappers around wc inherited property functionality
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_ra.h"

#include "client.h"
#include "svn_private_config.h"

#include "private/svn_wc_private.h"


/*** Code. ***/

/* Determine if ABSPATH needs an inherited property cache (i.e. it is a WC
   root that is not also the repository root or it is switched).  If it does,
   then set *NEEDS_CACHE to true, set it to false otherwise. */
static svn_error_t *
need_to_cache_iprops(svn_boolean_t *needs_cache,
                     const char *abspath,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *scratch_pool)
{
  svn_boolean_t is_wc_root;
  svn_error_t *err;

  /* Our starting assumption. */
  *needs_cache = FALSE;

  err = svn_wc_is_wc_root2(&is_wc_root, ctx->wc_ctx, abspath,
                           scratch_pool);

  /* ABSPATH can't need a cache if it doesn't exist. */
  if (err)
    {
      if (err->apr_err == SVN_ERR_ENTRY_NOT_FOUND)
        {
          svn_error_clear(err);
          is_wc_root = FALSE;
        }
      else
        {
          return svn_error_trace(err);
        }
    }

  if (is_wc_root)
    {
      const char *child_repos_relpath;

      /* We want to cache the inherited properties for WC roots, unless that
         root points to the root of the repository, then there in nowhere to
         inherit properties from. */
      SVN_ERR(svn_wc__node_get_repos_relpath(&child_repos_relpath,
                                             ctx->wc_ctx, abspath,
                                             scratch_pool, scratch_pool));
      if (child_repos_relpath[0] != '\0')
        *needs_cache = TRUE;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__get_inheritable_props(apr_hash_t **wcroot_iprops,
                                  const char *local_abspath,
                                  svn_revnum_t revision,
                                  svn_depth_t depth,
                                  svn_ra_session_t *ra_session,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  *wcroot_iprops = apr_hash_make(result_pool);

  /* If we don't have a base revision for LOCAL_ABSPATH then it can't
     possibly be a working copy root, nor can it contain any WC roots
     in the form of switched subtrees.  So there is nothing to cache. */
  if (SVN_IS_VALID_REVNUM(revision))
    {
      apr_hash_t *iprop_paths;
      apr_hash_index_t *hi;
      const char *old_session_url = NULL;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);

      SVN_ERR(svn_wc__get_cached_iprop_children(&iprop_paths, depth,
                                                ctx->wc_ctx, local_abspath,
                                                scratch_pool, iterpool));

      /* Make sure LOCAL_ABSPATH is present. */
      if (!apr_hash_get(iprop_paths, local_abspath, APR_HASH_KEY_STRING))
        {
          const char *target_abspath = apr_pstrdup(scratch_pool,
                                                   local_abspath);
          apr_hash_set(iprop_paths, target_abspath, APR_HASH_KEY_STRING,
                       target_abspath);
        }

      for (hi = apr_hash_first(scratch_pool, iprop_paths);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *child_abspath = svn__apr_hash_index_key(hi);
          svn_boolean_t needs_cached_iprops;

          svn_pool_clear(iterpool);

          SVN_ERR(need_to_cache_iprops(&needs_cached_iprops, child_abspath,
                                       ctx, iterpool));

          if (needs_cached_iprops)
            {
              const char *url;
              apr_array_header_t *inherited_props;

              SVN_ERR(svn_wc__node_get_url(&url, ctx->wc_ctx, child_abspath,
                                           iterpool, iterpool));
              if (ra_session)
                {
                  SVN_ERR(svn_client__ensure_ra_session_url(&old_session_url,
                                                            ra_session,
                                                            url,
                                                            scratch_pool));
                }
              else
                {
                  SVN_ERR(svn_client__open_ra_session_internal(&ra_session,
                                                               NULL, url,
                                                               NULL, NULL,
                                                               FALSE, TRUE,
                                                               ctx,
                                                               scratch_pool));
                }

              SVN_ERR(svn_ra_get_inherited_props(ra_session, &inherited_props,
                                                 "", revision, result_pool,
                                                 scratch_pool));

              if (old_session_url)
                SVN_ERR(svn_ra_reparent(ra_session, old_session_url,
                                        iterpool));

              apr_hash_set(*wcroot_iprops,
                           apr_pstrdup(result_pool, child_abspath),
                           APR_HASH_KEY_STRING,
                           inherited_props);
            }
        }

      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}
