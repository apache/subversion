/*
 * relocate.c:  wrapper around wc relocation functionality.
 *
 * ====================================================================
 * Copyright (c) 2002-2004 CollabNet.  All rights reserved.
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
               svn_boolean_t root,
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
      apr_pool_t *subpool = svn_pool_create(pool); 
      svn_ra_session_t *ra_session;
      const char *ra_uuid, *ra_root;
      SVN_ERR(svn_client__open_ra_session_internal(&ra_session, url, NULL,
                                                   NULL, NULL, FALSE, TRUE,
                                                   b->ctx, subpool));
      SVN_ERR(svn_ra_get_uuid(ra_session, &ra_uuid, subpool));
      SVN_ERR(svn_ra_get_repos_root(ra_session, &ra_root, subpool));
      url_uuid = &APR_ARRAY_PUSH(uuids, struct url_uuid_t);
      url_uuid->root = apr_pstrdup(b->pool, ra_root);
      url_uuid->uuid = apr_pstrdup(b->pool, ra_uuid);
      svn_pool_destroy(subpool);
    }

  /* Make sure the url is a repository root if desired. */
  if (root
      && strcmp(url, url_uuid->root) != 0)
    return svn_error_createf(SVN_ERR_CLIENT_INVALID_RELOCATION, NULL,
                             _("'%s' is not the root of the repository"),
                             url);

  /* Make sure the UUIDs match. */
  if (uuid && strcmp(uuid, url_uuid->uuid) != 0)
    return svn_error_createf
      (SVN_ERR_CLIENT_INVALID_RELOCATION, NULL,
       _("The repository at '%s' has uuid '%s', but the WC has '%s'"),
       url, uuid, url_uuid->uuid);

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
  svn_wc_adm_access_t *adm_access;
  struct validator_baton_t vb;

  /* Get an access baton for PATH. */
  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path,
                                 TRUE, recurse ? -1 : 0,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));

  /* Now, populate our validator callback baton, and call the relocate code. */
  vb.ctx = ctx;
  vb.path = path;
  vb.url_uuids = apr_array_make(pool, 1, sizeof(struct url_uuid_t));
  vb.pool = pool;
  SVN_ERR(svn_wc_relocate2(path, adm_access, from, to,
                           recurse, validator_func, &vb, pool));

  /* All done.  Clean up, and move on out. */
  SVN_ERR(svn_wc_adm_close(adm_access));
  return SVN_NO_ERROR;
}
