/*
 * relocate.c:  wrapper around wc relocation functionality.
 *
 * ====================================================================
 * Copyright (c) 2002-2004, 2009 CollabNet.  All rights reserved.
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

  apr_array_header_t *uuids = b->url_uuids;
  int i;

  for (i = 0; i < uuids->nelts; ++i)
    {
      struct url_uuid_t *uu = &APR_ARRAY_IDX(uuids, i,
                                             struct url_uuid_t);
      if (svn_path_is_ancestor(uu->root, url))
        {
          url_uuid = uu;
          break;
        }
    }

  /* We use an RA session in a subpool to get the UUID of the
     repository at the new URL so we can force the RA session to close
     by destroying the subpool. */
  if (! url_uuid)
    {
      apr_pool_t *sesspool = svn_pool_create(pool);
      svn_ra_session_t *ra_session;
      SVN_ERR(svn_client__open_ra_session_internal(&ra_session, url, NULL,
                                                   NULL, NULL, FALSE, TRUE,
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
svn_client_relocate(const char *path,
                    const char *from,
                    const char *to,
                    svn_boolean_t recurse,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  struct validator_baton_t vb;
  svn_wc_context_t *wc_ctx;
  const char *local_abspath;

  if (!ctx->wc_ctx)
    SVN_ERR(svn_wc_context_create(&wc_ctx, NULL /* config */, pool, pool));
  else
    wc_ctx = ctx->wc_ctx;

  /* Now, populate our validator callback baton, and call the relocate code. */
  vb.ctx = ctx;
  vb.path = path;
  vb.url_uuids = apr_array_make(pool, 1, sizeof(struct url_uuid_t));
  vb.pool = pool;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc_relocate4(wc_ctx, local_abspath, from, to, recurse,
                           validator_func, &vb, pool));

  if (!ctx->wc_ctx)
    SVN_ERR(svn_wc_context_destroy(wc_ctx));

  return SVN_NO_ERROR;
}
