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


/*** Code. ***/

svn_error_t *
svn_client_set_changelist(const char *path,
                          const char *changelist_name,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *pool)
{
  SVN_ERR(svn_wc_set_changelist(path, changelist_name, pool));

  /* ### TODO(sussman): create new notification type, and send
         notification feedback.  See locking-commands.c. */
  if (changelist_name)
    printf("Path '%s' is now part of changelist '%s'.\n",
           path, changelist_name);
  else
    printf("Path '%s' is no longer associated with a changelist'.\n", path);

  return SVN_NO_ERROR;
}


/* ### Make this a public func:  should this be in libsvn_wc instead?? */

/* Entry-walker callback routine (& baton) for
   svn_client_retrieve_changelist. */

struct fe_baton
{
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
          APR_ARRAY_PUSH(b->path_list, const char *) = apr_pstrdup(b->pool,
                                                                   path);
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_retrieve_changelist(apr_array_header_t **paths,
                               const char *changelist_name,
                               const char *root_path,
                               svn_cancel_func_t cancel_func,
                               void *cancel_baton,
                               apr_pool_t *pool)
{
  svn_wc_entry_callbacks_t entry_callbacks;
  struct fe_baton feb;
  svn_wc_adm_access_t *adm_access;

  entry_callbacks.found_entry = found_an_entry;
  feb.pool = pool;
  feb.changelist_name = changelist_name;
  feb.path_list = apr_array_make(pool, 1, sizeof(const char *));

  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, root_path,
                                 FALSE, /* no write lock */
                                 -1, /* infinite depth */
                                 cancel_func, cancel_baton, pool));

  SVN_ERR(svn_wc_walk_entries2(root_path, adm_access,
                               &entry_callbacks, &feb,
                               FALSE, /* don't show hidden entries */
                               cancel_func, cancel_baton,
                               pool));

  SVN_ERR(svn_wc_adm_close(adm_access));

  *paths = feb.path_list;
  return SVN_NO_ERROR;
}
