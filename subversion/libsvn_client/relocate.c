/*
 * relocate.c:  wrapper around wc relocation functionality.
 *
 * ====================================================================
 * Copyright (c) 2002 CollabNet.  All rights reserved.
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
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"



/*** Code. ***/

struct validator_baton_t
{
  void *ra_baton;
  svn_client_ctx_t *ctx;
  const char *path;
  apr_hash_t *url_uuids;
  apr_pool_t *pool;

};


static svn_error_t *
relocate_error (const char *url,
                const char *actual_uuid,
                const char *expected_uuid)
{
  return svn_error_createf 
    (SVN_ERR_CLIENT_INVALID_RELOCATION, NULL,
     "The repository at %s has uuid '%s', but the WC has '%s'",
     url, actual_uuid, expected_uuid);
}


static svn_error_t *
validator_func (void *baton, 
                const char *uuid, 
                const char *url)
{
  svn_ra_plugin_t *ra_lib;
  void *sess;
  struct validator_baton_t *b = baton;
  const char *ra_uuid;
  const char *auth_dir;

  apr_hash_t *uuids = b->url_uuids;
  apr_pool_t *pool = b->pool;
  apr_pool_t *subpool;

  if (apr_hash_count (uuids) != 0)
    {
      apr_hash_index_t *hi;
      for (hi = apr_hash_first (pool, uuids); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          const char *item_url;
          const char *item_uuid;

          apr_hash_this (hi, &key, NULL, &val);
          item_url = key;
          item_uuid = val;

          if (strncmp (item_url, url, strlen (item_url)) != 0)
            continue;

          if (strcmp (uuid, item_uuid) == 0)
            return SVN_NO_ERROR;

          return relocate_error (item_url, item_uuid, uuid);
        }
    }


  /* We use an RA session in a subpool to get the UUID of the
     repository at the new URL so we can force the RA session to close
     by destroying the subpool. */
  subpool = svn_pool_create (pool); 
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, b->ra_baton, url, subpool));
  SVN_ERR (svn_client__default_auth_dir (&auth_dir, b->path, subpool));
  SVN_ERR (svn_client__open_ra_session (&sess, ra_lib, url, auth_dir,
                                        NULL, NULL, FALSE, TRUE,
                                        b->ctx, subpool));
  SVN_ERR (ra_lib->get_uuid (sess, &ra_uuid, subpool));
  ra_uuid = apr_pstrdup (pool, ra_uuid);
  svn_pool_destroy (subpool);

  /* Make sure the UUIDs match. */
  if (strcmp (uuid, ra_uuid))
    return relocate_error (url, ra_uuid, uuid);

  apr_hash_set (uuids, url, APR_HASH_KEY_STRING, ra_uuid);

  return SVN_NO_ERROR;
}
              
svn_error_t *
svn_client_relocate (const char *path,
                     const char *from,
                     const char *to,
                     svn_boolean_t recurse,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  struct validator_baton_t vb;

  /* Get an access baton for PATH. */
  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, path,
                                  TRUE, recurse, pool));

  /* Now, populate our validator callback baton, and call the relocate code. */
  SVN_ERR (svn_ra_init_ra_libs (&(vb.ra_baton), pool));
  vb.ctx = ctx;
  vb.path = path;
  vb.url_uuids = apr_hash_make (pool);
  vb.pool = pool;
  SVN_ERR (svn_wc_relocate (path, adm_access, from, to,
                            recurse, validator_func, &vb, pool));

  /* All done.  Clean up, and move on out. */
  SVN_ERR (svn_wc_adm_close (adm_access));
  return SVN_NO_ERROR;
}
