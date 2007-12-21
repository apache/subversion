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
#include "svn_hash.h"

#include "client.h"


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
  svn_changelist_receiver_t callback_func;
  void *callback_baton;
  apr_hash_t *changelists;
  apr_pool_t *pool;
};


static svn_error_t *
found_an_entry(const char *path,
               const svn_wc_entry_t *entry,
               void *baton,
               apr_pool_t *pool)
{
  struct fe_baton *b = (struct fe_baton *)baton;

  /* If the entry has a changelist, and is a file or is the "this-dir"
     entry for directory, and the changelist matches one that we're
     looking for (or we aren't looking for any in particular)... */
  if (entry->changelist
      && ((! b->changelists)
          || apr_hash_get(b->changelists, entry->changelist, 
                          APR_HASH_KEY_STRING))
      && ((entry->kind == svn_node_file)
          || ((entry->kind == svn_node_dir)
              && (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) == 0))))
    {
      /* ...then call the callback function. */
      SVN_ERR(b->callback_func(b->callback_baton, path, 
                               entry->changelist, pool));
    }

  return SVN_NO_ERROR;
}

static const svn_wc_entry_callbacks2_t entry_callbacks =
  { found_an_entry, svn_client__default_walker_error_handler };

svn_error_t *
svn_client_get_changelists(const char *path,
                           const apr_array_header_t *changelists,
                           svn_depth_t depth,
                           svn_changelist_receiver_t callback_func,
                           void *callback_baton,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  struct fe_baton feb;
  svn_wc_adm_access_t *adm_access;

  feb.callback_func = callback_func;
  feb.callback_baton = callback_baton;
  feb.pool = pool;
  if (changelists)
    SVN_ERR(svn_hash_from_cstring_keys(&(feb.changelists), changelists, pool));
  else
    feb.changelists = NULL;
  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path,
                                 FALSE, /* no write lock */
                                 -1, /* levels to lock == infinity */
                                 ctx->cancel_func, ctx->cancel_baton, pool));
  SVN_ERR(svn_wc_walk_entries3(path, adm_access, &entry_callbacks, &feb,
                               depth, FALSE, /* don't show hidden entries */
                               ctx->cancel_func, ctx->cancel_baton, pool));
  SVN_ERR(svn_wc_adm_close(adm_access));

  return SVN_NO_ERROR;
}
