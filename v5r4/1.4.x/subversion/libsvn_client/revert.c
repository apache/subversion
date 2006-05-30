/*
 * revert.c:  wrapper around wc revert functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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
#include "svn_time.h"
#include "svn_config.h"
#include "client.h"



/*** Code. ***/

/* Attempt to revert PATH, recursively if RECURSIVE is true and PATH
   is a directory, else non-recursively.  Consult CTX to determine
   whether or not to revert timestamp to the time of last commit
   ('use-commit-times = yes').  Use POOL for temporary allocation.

   If PATH is unversioned, return SVN_ERR_UNVERSIONED_RESOURCE. */
static svn_error_t *
revert(const char *path,
       svn_boolean_t recursive,
       svn_boolean_t use_commit_times,
       svn_client_ctx_t *ctx,
       apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access, *target_access;
  const char *target;
  svn_error_t *err;

  SVN_ERR(svn_wc_adm_open_anchor(&adm_access, &target_access, &target, path,
                                 TRUE, recursive ? -1 : 0,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));

  err = svn_wc_revert2(path, adm_access, recursive, use_commit_times,
                       ctx->cancel_func, ctx->cancel_baton,
                       ctx->notify_func2, ctx->notify_baton2,
                       pool);

  if (err)
    {
      /* If target isn't versioned, just send a 'skip'
         notification and move on. */
      if (err->apr_err == SVN_ERR_ENTRY_NOT_FOUND
          || err->apr_err == SVN_ERR_UNVERSIONED_RESOURCE)
        {
          if (ctx->notify_func2)
            (*ctx->notify_func2)
              (ctx->notify_baton2,
               svn_wc_create_notify(path, svn_wc_notify_skip, pool),
               pool);
          svn_error_clear(err);
        }
      else
        return err;
    }

  SVN_ERR(svn_wc_adm_close(adm_access));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_revert(const apr_array_header_t *paths,
                  svn_boolean_t recursive,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  apr_pool_t *subpool;
  svn_error_t *err = SVN_NO_ERROR;
  int i;
  svn_config_t *cfg;
  svn_boolean_t use_commit_times;

  cfg = ctx->config ? apr_hash_get(ctx->config, SVN_CONFIG_CATEGORY_CONFIG,  
                                   APR_HASH_KEY_STRING) : NULL;

  SVN_ERR(svn_config_get_bool(cfg, &use_commit_times,
                              SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_USE_COMMIT_TIMES,
                              FALSE));

  subpool = svn_pool_create(pool);

  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      svn_pool_clear(subpool);

      /* See if we've been asked to cancel this operation. */
      if ((ctx->cancel_func) 
          && ((err = ctx->cancel_func(ctx->cancel_baton))))
        goto errorful;

      err = revert(path, recursive, use_commit_times, ctx, subpool);
      if (err)
        goto errorful;
    }
  
 errorful:

  svn_pool_destroy(subpool);
  
  /* Sleep to ensure timestamp integrity. */
  svn_sleep_for_timestamps();

  return err;
}


