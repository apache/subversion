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
#include "svn_hash.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"



/* One external item.  This usually represents one line from an
   svn:externals description. */
struct external_item
{
  /* The name of the subdirectory into which this external should be
     checked out.  This is relative to the parent directory that holds
     this external item.  (Note that these structs are often stored in
     hash tables with the target dirs as keys, so this field will
     often be redundant.) */
  const char *target_dir;

  /* Where to check out from. */
  const char *url;

  /* What revision to check out.  The only valid kinds for this are
     svn_client_revision_number, svn_client_revision_date, and
     svn_client_revision_head. */
  svn_client_revision_t revision;
};


/* Set *EXTERNALS_P to a hash table whose keys are target subdir
 * names, and values are `struct external_item *' objects,
 * based on DESC.
 *
 * The format of EXTERNALS is the same as for values of the directory
 * property SVN_PROP_EXTERNALS, which see.
 *
 * Allocate the table, keys, and values in POOL.
 *
 * If the format of DESC is invalid, don't touch *EXTERNALS_P and
 * return SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION.
 *
 * Use PARENT_DIRECTORY only in constructing error strings.
 */
static svn_error_t *
parse_externals_description (apr_hash_t **externals_p,
                             const char *parent_directory,
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
      struct external_item *item;

      if ((! line) || (line[0] == '#'))
        continue;

      /* else proceed */

      line_parts = svn_cstring_split (line, " \t", TRUE, pool);
      target_dir = APR_ARRAY_IDX (line_parts, 0, const char *);
      url = APR_ARRAY_IDX (line_parts, 1, const char *);
      item = apr_palloc (pool, sizeof (*item));
      
      if (! url)
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, 0, NULL, pool,
           "error parsing " SVN_PROP_EXTERNALS " property on '%s':\n",
           "Invalid line: '%s'", parent_directory, line);

      /* ### Eventually, parse revision numbers and even dates from
         the description file. */
      item->revision.kind = svn_client_revision_head;
      item->target_dir = target_dir;
      item->url = url;

      apr_hash_set (externals, target_dir, APR_HASH_KEY_STRING, item);
    }

  return SVN_NO_ERROR;
}


/* Closure for handle_external_item_change. */
struct handle_external_item_change_baton
{
  /* As returned by parse_externals_description(). */
  apr_hash_t *new_desc;
  apr_hash_t *old_desc;

  /* The directory that has this externals property. */
  const char *parent_dir;

  /* Passed through to svn_client_checkout(). */
  const svn_delta_editor_t *before_editor;
  void *before_edit_baton;
  const svn_delta_editor_t *after_editor;
  void *after_edit_baton;
  svn_client_auth_baton_t *auth_baton;

  apr_pool_t *pool;
};


/* Return true if NEW_ITEM and OLD_ITEM represent the same external
   item at the same revision checked out into the same target subdir,
   else return false. */
static svn_boolean_t
compare_external_items (struct external_item *new_item,
                        struct external_item *old_item)
{
  if ((strcmp (new_item->target_dir, old_item->target_dir) != 0)
      || (strcmp (new_item->url, old_item->url) != 0)
      || (! svn_client__compare_revisions (&(new_item->revision),
                                           &(old_item->revision))))
    return FALSE;
    
  /* Else. */
  return TRUE;
}


/* This implements the `svn_hash_diff_func_t' interface.
   BATON is of type `struct handle_external_item_change_baton *'.  */
static svn_error_t *
handle_external_item_change (const void *key, apr_ssize_t klen,
                             enum svn_hash_diff_key_status status,
                             void *baton)
{
  struct handle_external_item_change_baton *ib = baton;
  struct external_item *old_item, *new_item;

  /* Don't bother to check status, since we'll get that for free by
     attempting to retrieve the hash values anyway.  */

  old_item = apr_hash_get (ib->old_desc, key, klen);
  new_item = apr_hash_get (ib->new_desc, key, klen);

  /* We couldn't possibly be here if both values were null, right? */
  assert (old_item || new_item);

  /* There's one potential ugliness.  If a target subdir changed, but
     its URL did not, then we only want to rename the subdir, and not
     check out the URL again.  Thus, for subdir changes, we "sneak
     around the back" and look in ib->new_desc, ib->old_desc to check
     if anything else in this parent_dir has the same URL.

     Of course, if an external gets moved into another parent
     directory entirely, then we lose -- we'll have to check it all
     out again.  The only way to prevent this is to harvest a global
     list based on urls/revs, and check the list every time we're
     about to delete an external subdir; and when a deletion is really
     part of a rename, then we'd do the rename right then.  This is
     not worth the bookkeeping complexity, IMHO. */

  if (! old_item)
    {
      /* ### todo: before checking out a new subdir, see if this is
         really just a rename of an old one.  This can work in tandem
         with the next case -- this case would do nothing, knowing
         that the next case either already has, or soon will, rename
         the external subdirectory. */

      SVN_ERR (svn_client_checkout
               (ib->before_editor,
                ib->before_edit_baton,
                ib->after_editor,
                ib->after_edit_baton,
                ib->auth_baton,
                new_item->url,
                svn_path_join (ib->parent_dir, new_item->target_dir, ib->pool),
                &(new_item->revision),
                TRUE, /* recurse */
                NULL,
                ib->pool));
    }
  if (! new_item)
    {
      /* ### todo: before removing an old subdir, see if it wants to
         just be renamed to a new one.  See above case. */
      svn_error_t *err;

      err = svn_wc_remove_from_revision_control (ib->parent_dir,
                                                 old_item->target_dir,
                                                 TRUE,  /* destroy wc */
                                                 ib->pool);

      if (err && (err->apr_err != SVN_ERR_WC_LEFT_LOCAL_MOD))
        return err;
    }
  else if (! compare_external_items (new_item, old_item))
    {
      SVN_ERR (svn_io_remove_dir (svn_path_join (ib->parent_dir,
                                                 old_item->target_dir,
                                                 ib->pool), ib->pool));
      
      SVN_ERR (svn_client_checkout
               (ib->before_editor,
                ib->before_edit_baton,
                ib->after_editor,
                ib->after_edit_baton,
                ib->auth_baton,
                new_item->url,
                svn_path_join (ib->parent_dir, new_item->target_dir, ib->pool),
                &(new_item->revision),
                TRUE, /* recurse */
                NULL,
                ib->pool));
    }

  return SVN_NO_ERROR;
}


/* Closure for handle_externals_change. */
struct handle_externals_desc_change_baton
{
  /* As returned by svn_wc_edited_externals(). */
  apr_hash_t *externals_new;
  apr_hash_t *externals_old;

  /* Passed through to handle_external_item_change_baton. */
  const svn_delta_editor_t *before_editor;
  void *before_edit_baton;
  const svn_delta_editor_t *after_editor;
  void *after_edit_baton;
  svn_client_auth_baton_t *auth_baton;

  apr_pool_t *pool;
};


/* This implements the `svn_hash_diff_func_t' interface.
   BATON is of type `struct handle_externals_desc_change_baton *'.  
*/
static svn_error_t *
handle_externals_desc_change (const void *key, apr_ssize_t klen,
                              enum svn_hash_diff_key_status status,
                              void *baton)
{
  struct handle_externals_desc_change_baton *cb = baton;
  struct handle_external_item_change_baton ib;
  const char *old_desc_text, *new_desc_text;
  apr_hash_t *old_desc, *new_desc;

  if ((old_desc_text = apr_hash_get (cb->externals_old, key, klen)))
    SVN_ERR (parse_externals_description (&old_desc, (const char *) key,
                                          old_desc_text, cb->pool));
  else
    old_desc = NULL;

  if ((new_desc_text = apr_hash_get (cb->externals_new, key, klen)))
    SVN_ERR (parse_externals_description (&new_desc, (const char *) key,
                                          new_desc_text, cb->pool));
  else
    new_desc = NULL;

  ib.old_desc          = old_desc;
  ib.new_desc          = new_desc;
  ib.parent_dir        = (const char *) key;
  ib.before_editor     = cb->before_editor;
  ib.before_edit_baton = cb->before_edit_baton;
  ib.after_editor      = cb->after_editor;
  ib.after_edit_baton  = cb->after_edit_baton;
  ib.auth_baton        = cb->auth_baton;
  ib.pool              = cb->pool;

  SVN_ERR (svn_hash_diff (old_desc, new_desc,
                          handle_external_item_change, &ib, cb->pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__handle_externals_changes (svn_wc_traversal_info_t *traversal_info,
                                      const svn_delta_editor_t *before_editor,
                                      void *before_edit_baton,
                                      const svn_delta_editor_t *after_editor,
                                      void *after_edit_baton,
                                      svn_client_auth_baton_t *auth_baton,
                                      apr_pool_t *pool)
{
  apr_hash_t *externals_old, *externals_new;
  struct handle_externals_desc_change_baton cb;

  svn_wc_edited_externals (&externals_old, &externals_new, traversal_info);

  cb.externals_new     = externals_new;
  cb.externals_old     = externals_old;
  cb.before_editor     = before_editor;
  cb.before_edit_baton = before_edit_baton;
  cb.after_editor      = after_editor;
  cb.after_edit_baton  = after_edit_baton;
  cb.auth_baton        = auth_baton;

  SVN_ERR (svn_hash_diff (externals_old, externals_new,
                          handle_externals_desc_change, &cb, pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: 
 */
