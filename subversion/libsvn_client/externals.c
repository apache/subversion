/*
 * externals.c:  handle the svn:externals property
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

#include <assert.h>
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"



svn_error_t *
svn_client__parse_externals_description (apr_hash_t **externals_p,
                                         const char *desc,
                                         apr_pool_t *pool)
{
  apr_hash_t *externals = apr_hash_make (pool);
  apr_array_header_t *lines = svn_cstring_split (desc, "\n\r", TRUE, pool);
  int i;
  
  for (i = 0; i < lines->nelts; i++)
    {
      const char *line = APR_ARRAY_IDX (lines, i, const char *);
      apr_array_header_t *line_parts;
      const char *target_dir;
      const char *url;
      svn_client__external_item_t *item;
      svn_client_revision_t *revision;

      if ((! line) || (line[0] == '#'))
        continue;

      /* else proceed */

      line_parts = svn_cstring_split (line, " \t", TRUE, pool);
      target_dir = APR_ARRAY_IDX (line_parts, 0, const char *);
      url = APR_ARRAY_IDX (line_parts, 1, const char *);
      item = apr_palloc (pool, sizeof (*item));
      revision = apr_palloc (pool, sizeof (*revision));
      
      if (! url)
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, 0, NULL, pool,
           "invalid line: '%s'", line);

      /* ### Eventually, parse revision numbers and even dates from
         the description file. */
      revision->kind = svn_client_revision_head;
      item->revision = revision;
      item->target_dir = target_dir;
      item->url = url;

      apr_hash_set (externals, target_dir, APR_HASH_KEY_STRING, item);
    }

  return SVN_NO_ERROR;
}


/* Check out the external items described by DESCRIPTION into PATH.
 * Use POOL for any temporary allocation.
 *
 * The format of DESCRIPTION is the same as for values of the directory
 * property SVN_PROP_EXTERNALS, which see.
 *
 * BEFORE_EDITOR/BEFORE_EDIT_BATON and AFTER_EDITOR/AFTER_EDIT_BATON,
 * along with AUTH_BATON, are passed along to svn_client_checkout() to
 * check out the external item.
 *
 * If the format of DESCRIPTION is invalid, return
 * SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION and don't touch
 * *EXTERNALS_P.
 */
static svn_error_t *
checkout_externals_description (const char *description,
                                const char *path,
                                const svn_delta_editor_t *before_editor,
                                void *before_edit_baton,
                                const svn_delta_editor_t *after_editor,
                                void *after_edit_baton,
                                svn_client_auth_baton_t *auth_baton,
                                apr_pool_t *pool)
{
  apr_hash_t *items;
  apr_hash_index_t *hi;
  svn_error_t *err;

  err = svn_client__parse_externals_description (&items, description, pool);
  if (err)
    return svn_error_createf
      (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, 0, err, pool,
       "error parsing value of " SVN_PROP_EXTERNALS " property for %s",
       path);

  for (hi = apr_hash_first (pool, items); hi; hi = apr_hash_next (hi))
    {
      svn_client__external_item_t *item;
      void *val;
          
      /* We can ignore the hash name, it's in the item anyway. */
      apr_hash_this (hi, NULL, NULL, &val);
      item = val;

      SVN_ERR (svn_client_checkout
               (before_editor,
                before_edit_baton,
                after_editor,
                after_edit_baton,
                auth_baton,
                item->url,
                svn_path_join (path, item->target_dir, pool),
                item->revision,
                TRUE, /* recurse */
                NULL,
                pool));
    }

  return SVN_NO_ERROR;
}


/* Walk newly checked-out tree PATH looking for directories that have
   the "svn:externals" property set.  For each one, read the external
   items from in the property value, and check them out as subdirs
   of the directory that had the property.

   BEFORE_EDITOR/BEFORE_EDIT_BATON and AFTER_EDITOR/AFTER_EDIT_BATON,
   along with AUTH_BATON, are passed along to svn_client_checkout() to
   check out the external items.   ### This is a lousy notification
   system, soon it will be notification callbacks instead, that will
   be nice! ###

   ### todo: AUTH_BATON may not be so useful.  It's almost like we
       need access to the original auth-obtaining callbacks that
       produced auth baton in the first place.  Hmmm. ###

   Use POOL for temporary allocation.
   
   Notes: This is done _after_ the entire initial checkout is complete
   so that fetching external items (and any errors therefrom) won't
   delay the primary checkout.  */
svn_error_t *
svn_client__checkout_externals (const char *path, 
                                const svn_delta_editor_t *before_editor,
                                void *before_edit_baton,
                                const svn_delta_editor_t *after_editor,
                                void *after_edit_baton,
                                svn_client_auth_baton_t *auth_baton,
                                apr_pool_t *pool)
{
  const svn_string_t *description;

  SVN_ERR (svn_wc_prop_get (&description, SVN_PROP_EXTERNALS, path, pool));

  if (description)
    SVN_ERR (checkout_externals_description (description->data,
                                             path,
                                             before_editor,
                                             before_edit_baton,
                                             after_editor,
                                             after_edit_baton,
                                             auth_baton,
                                             pool));
  
  /* Recurse. */
  {
    apr_hash_t *entries;
    apr_hash_index_t *hi;
    apr_pool_t *subpool = svn_pool_create (pool);
    
    SVN_ERR (svn_wc_entries_read (&entries, path, FALSE, pool));
    for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
      {
        void *val;
        svn_wc_entry_t *ent;
        
        apr_hash_this (hi, NULL, NULL, &val);
        ent = val;

        if ((ent->kind == svn_node_dir)
            && (strcmp (ent->name, SVN_WC_ENTRY_THIS_DIR) != 0))
          {
            const char *path2 = svn_path_join (path, ent->name, subpool);
            SVN_ERR (svn_client__checkout_externals (path2, 
                                                     before_editor,
                                                     before_edit_baton,
                                                     after_editor,
                                                     after_edit_baton,
                                                     auth_baton,
                                                     subpool));
          }

        svn_pool_clear (subpool);
      }

    svn_pool_destroy (subpool);
  }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: 
 */
