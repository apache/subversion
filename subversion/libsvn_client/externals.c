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



/* One external item.  This usually represents one line from an
   svn:externals description. */
struct external_item_t
{
  /* The name of the subdirectory into which this external should be
     checked out.  (But note that these structs are often stored in
     hash tables with the target dirs as keys, so this field will
     often be redundant.) */
  const char *target_dir;

  /* Where to check out from. */
  const char *url;

  /* What revision to check out.  Only svn_client_revision_number,
     svn_client_revision_date, and svn_client_revision_head are
     valid.  ### Any reason to change this to inline, instead of
     pointer? */
  svn_client_revision_t *revision;

};


#if 0 /* ### temporarily commented out, while unused */

/* Set *EXTERNALS_P to a hash table whose keys are target subdir
 * names, and values are `struct external_item_t *' objects,
 * based on DESC.
 *
 * The format of EXTERNALS is the same as for values of the directory
 * property SVN_PROP_EXTERNALS, which see.
 *
 * Allocate the table, keys, and values in POOL.
 *
 * If the format of DESC is invalid, don't touch *EXTERNALS_P and
 * return SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION.
 */
static svn_error_t *
parse_externals_description (apr_hash_t **externals_p,
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
      struct external_item_t *item;
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

  err = parse_externals_description (&items, description, pool);
  if (err)
    return svn_error_createf
      (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, 0, err, pool,
       "error parsing value of " SVN_PROP_EXTERNALS " property for %s",
       path);

  for (hi = apr_hash_first (pool, items); hi; hi = apr_hash_next (hi))
    {
      struct external_item_t *item;
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
#endif /* 0, temporarily commented out, while unused */


svn_error_t *
svn_client__handle_externals_changes (void *traversal_info,
                                      const svn_delta_editor_t *before_editor,
                                      void *before_edit_baton,
                                      const svn_delta_editor_t *after_editor,
                                      void *after_edit_baton,
                                      svn_client_auth_baton_t *auth_baton,
                                      apr_pool_t *pool)
{
  apr_hash_t *externals_old, *externals_new;
  
  svn_wc_edited_externals (&externals_old, &externals_new, traversal_info);

  /* ### in progress */

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: 
 */
