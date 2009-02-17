/*
 * resolved.c:  wrapper around wc resolved functionality.
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

#include "svn_types.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"
#include "private/svn_wc_private.h"


/*** Code. ***/

svn_error_t *
svn_client_resolve(const char *path,
                   svn_depth_t depth,
                   svn_wc_conflict_choice_t conflict_choice,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  int adm_lock_level = SVN_WC__LEVELS_TO_LOCK_FROM_DEPTH(depth);
  svn_boolean_t wc_root;

  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL,
                                 path,
                                 TRUE,
                                 adm_lock_level,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));

  /* Make sure we do not end up looking for tree conflict info
   * above the working copy root. It's OK to check for tree conflict
   * info in the parent of a *switched* subtree, because the
   * subtree itself might be a tree conflict victim. */
  SVN_ERR(svn_wc__strictly_is_wc_root(&wc_root, path, adm_access, pool));

  if (! wc_root) /* but possibly a switched subdir */
    {
      /* In order to resolve tree-conflicts on the target PATH, we need an
       * adm_access on its parent directory. The lock level then needs to
       * extend at least onto the immediate children. */
      SVN_ERR(svn_wc_adm_close2(adm_access, pool));
      if (adm_lock_level >= 0)
        adm_lock_level++;
      SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL,
                                     svn_path_dirname(path, pool),
                                     TRUE, adm_lock_level,
                                     ctx->cancel_func, ctx->cancel_baton,
                                     pool));
    }

  SVN_ERR(svn_wc_resolved_conflict4(path, adm_access, TRUE, TRUE, TRUE,
                                    depth, conflict_choice,
                                    ctx->notify_func2, ctx->notify_baton2,
                                    ctx->cancel_func, ctx->cancel_baton,
                                    pool));

  return svn_wc_adm_close2(adm_access, pool);
}
