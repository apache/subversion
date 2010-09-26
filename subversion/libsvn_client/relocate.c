/*
 * relocate.c:  wrapper around wc relocation functionality.
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

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "client.h"

#include "svn_private_config.h"


/*** Code. ***/

/* Repository root and UUID for a repository. */
struct url_uuid_t
{
  const char *root;
  const char *uuid;
};

struct validator_baton_t
{
  svn_client_ctx_t *ctx;
  const char *path;
  apr_array_header_t *url_uuids;
  apr_pool_t *pool;

};


static svn_error_t *
validator_func(void *baton,
               const char *uuid,
               const char *url,
               const char *root_url,
               apr_pool_t *pool)
{
  struct validator_baton_t *b = baton;
  struct url_uuid_t *url_uuid = NULL;
  const char *disable_checks;

  apr_array_header_t *uuids = b->url_uuids;
  int i;

  for (i = 0; i < uuids->nelts; ++i)
    {
      struct url_uuid_t *uu = &APR_ARRAY_IDX(uuids, i,
                                             struct url_uuid_t);
      if (svn_uri_is_ancestor(uu->root, url))
        {
          url_uuid = uu;
          break;
        }
    }

  disable_checks = getenv("SVN_I_LOVE_CORRUPTED_WORKING_COPIES_SO_DISABLE_RELOCATE_VALIDATION");
  if (disable_checks && (strcmp(disable_checks, "yes") == 0))
    {
      /* Lie about URL_UUID's components, claiming they match the
         expectations of the validation code below.  */
      url_uuid = apr_pcalloc(pool, sizeof(*url_uuid));
      url_uuid->root = apr_pstrdup(pool, root_url);
      url_uuid->uuid = apr_pstrdup(pool, uuid);
    }

  /* We use an RA session in a subpool to get the UUID of the
     repository at the new URL so we can force the RA session to close
     by destroying the subpool. */
  if (! url_uuid)
    {
      apr_pool_t *sesspool = svn_pool_create(pool);
      svn_ra_session_t *ra_session;
      SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL, url, NULL,
                                                   NULL, FALSE, TRUE,
                                                   b->ctx, sesspool));
      url_uuid = &APR_ARRAY_PUSH(uuids, struct url_uuid_t);
      SVN_ERR(svn_ra_get_uuid2(ra_session, &(url_uuid->uuid), pool));
      SVN_ERR(svn_ra_get_repos_root2(ra_session, &(url_uuid->root), pool));
      svn_pool_destroy(sesspool);
    }

  /* Make sure the url is a repository root if desired. */
  if (root_url
      && strcmp(root_url, url_uuid->root) != 0)
    return svn_error_createf(SVN_ERR_CLIENT_INVALID_RELOCATION, NULL,
                             _("'%s' is not the root of the repository"),
                             url);

  /* Make sure the UUIDs match. */
  if (uuid && strcmp(uuid, url_uuid->uuid) != 0)
    return svn_error_createf
      (SVN_ERR_CLIENT_INVALID_RELOCATION, NULL,
       _("The repository at '%s' has uuid '%s', but the WC has '%s'"),
       url, url_uuid->uuid, uuid);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_relocate2(const char *wcroot_dir,
                     const char *from,
                     const char *to,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  struct validator_baton_t vb;
  const char *local_abspath;

  /* Populate our validator callback baton, and call the relocate code. */
  vb.ctx = ctx;
  vb.path = wcroot_dir;
  vb.url_uuids = apr_array_make(pool, 1, sizeof(struct url_uuid_t));
  vb.pool = pool;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, wcroot_dir, pool));
  SVN_ERR(svn_wc_relocate4(ctx->wc_ctx, local_abspath, from, to,
                           validator_func, &vb, pool));

  return SVN_NO_ERROR;
}
