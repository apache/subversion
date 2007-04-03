/*
 * changelist.c:  implementation of the 'changelist' command
 *
 * ====================================================================
 * Copyright (c) 2006-2007 CollabNet.  All rights reserved.
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

#include "svn_client.h"
#include "svn_wc.h"
#include "svn_pools.h"


/*** Code. ***/

svn_error_t *
svn_client_add_to_changelist(const apr_array_header_t *paths,
                             const char *changelist_name,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool)
{
  /* Someday this routine might be use a different underlying API to
     to make the associations in a centralized database. */

  return svn_wc_set_changelist(paths, changelist_name, NULL,
                               ctx->cancel_func, ctx->cancel_baton,
                               ctx->notify_func2, ctx->notify_baton2, pool);
}


svn_error_t *
svn_client_remove_from_changelist(const apr_array_header_t *paths,
                                  const char *changelist_name,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *pool)
{
  /* Someday this routine might be use a different underlying API to
     remove the associations from a centralized database.

     To that end, we should keep our semantics consistent.  If
     CHANGELIST_NAME is defined, then it's not enough to just "blank
     out" any changelist name attached to each path's entry_t; we need
     to verify that each incoming path is already a member of the
     named changelist first... if not, then skip over it or show a
     warning.  This is what a centralized database would do.

     If CHANGELIST_NAME is undefined, then we can be more lax and
     remove the path from whatever changelist it's already a part of.
 */

  return svn_wc_set_changelist(paths, NULL, changelist_name,
                               ctx->cancel_func, ctx->cancel_baton,
                               ctx->notify_func2, ctx->notify_baton2, pool);
}


/* Entry-walker callback for svn_client_get_changelist*() below. */

struct fe_baton
{
  svn_boolean_t store_paths;
  svn_changelist_receiver_t callback_func;
  void *callback_baton;
  apr_array_header_t *path_list;
  const char *changelist_name;
  apr_pool_t *pool;
};


static svn_error_t *
found_an_entry(const char *path,
               const svn_wc_entry_t *entry,
               void *baton,
               apr_pool_t *pool)
{
  struct fe_baton *b = (struct fe_baton *)baton;

  if (entry->changelist
      && (strcmp(entry->changelist, b->changelist_name) == 0))
    {
      if ((entry->kind == svn_node_file)
          || ((entry->kind == svn_node_dir)
              && (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) == 0)))
        {
          if (b->store_paths)
            APR_ARRAY_PUSH(b->path_list, const char *) = apr_pstrdup(b->pool,
                                                                     path);
          else
            SVN_ERR(b->callback_func(b->callback_baton, path));
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_get_changelist(apr_array_header_t **paths,
                          const char *changelist_name,
                          const char *root_path,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *pool)
{
  svn_wc_entry_callbacks_t entry_callbacks;
  struct fe_baton feb;
  svn_wc_adm_access_t *adm_access;

  entry_callbacks.found_entry = found_an_entry;
  feb.store_paths = TRUE;
  feb.pool = pool;
  feb.changelist_name = changelist_name;
  feb.path_list = apr_array_make(pool, 1, sizeof(const char *));

  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, root_path,
                                 FALSE, /* no write lock */
                                 -1, /* infinite depth */
                                 ctx->cancel_func, ctx->cancel_baton, pool));

  SVN_ERR(svn_wc_walk_entries2(root_path, adm_access,
                               &entry_callbacks, &feb,
                               FALSE, /* don't show hidden entries */
                               ctx->cancel_func, ctx->cancel_baton,
                               pool));

  SVN_ERR(svn_wc_adm_close(adm_access));

  *paths = feb.path_list;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_get_changelist_streamy(svn_changelist_receiver_t callback_func,
                                  void *callback_baton,
                                  const char *changelist_name,
                                  const char *root_path,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *pool)
{
  svn_wc_entry_callbacks_t entry_callbacks;
  struct fe_baton feb;
  svn_wc_adm_access_t *adm_access;

  entry_callbacks.found_entry = found_an_entry;
  feb.store_paths = FALSE;
  feb.callback_func = callback_func;
  feb.callback_baton = callback_baton;
  feb.pool = pool;
  feb.changelist_name = changelist_name;
  feb.path_list = apr_array_make(pool, 1, sizeof(const char *));

  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, root_path,
                                 FALSE, /* no write lock */
                                 -1, /* infinite depth */
                                 ctx->cancel_func, ctx->cancel_baton, pool));

  SVN_ERR(svn_wc_walk_entries2(root_path, adm_access,
                               &entry_callbacks, &feb,
                               FALSE, /* don't show hidden entries */
                               ctx->cancel_func, ctx->cancel_baton,
                               pool));

  SVN_ERR(svn_wc_adm_close(adm_access));

  return SVN_NO_ERROR;
}
