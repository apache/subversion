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
     svn_opt_revision_number, svn_opt_revision_date, and
     svn_opt_revision_head. */
  svn_opt_revision_t revision;
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
      struct external_item *item;

      if ((! line) || (line[0] == '#'))
        continue;

      /* else proceed */

      line_parts = svn_cstring_split (line, " \t", TRUE, pool);

      item = apr_palloc (pool, sizeof (*item));

      if (line_parts->nelts < 2)
        goto parse_error;

      else if (line_parts->nelts == 2)
        {
          /* No "-r REV" given. */
          item->target_dir = APR_ARRAY_IDX (line_parts, 0, const char *);
          item->url = APR_ARRAY_IDX (line_parts, 1, const char *);
          item->revision.kind = svn_opt_revision_head;
        }
      else if ((line_parts->nelts == 3) || (line_parts->nelts == 4))
        {
          /* We're dealing with one of these two forms:
           * 
           *    TARGET_DIR  -rN  URL
           *    TARGET_DIR  -r N  URL
           * 
           * Handle either way.
           */

          const char *r_part_1 = NULL, *r_part_2 = NULL;

          item->target_dir = APR_ARRAY_IDX (line_parts, 0, const char *);
          item->revision.kind = svn_opt_revision_number;

          if (line_parts->nelts == 3)
            {
              r_part_1 = APR_ARRAY_IDX (line_parts, 1, const char *);
              item->url = APR_ARRAY_IDX (line_parts, 2, const char *);
            }
          else  /* nelts == 4 */
            {
              r_part_1 = APR_ARRAY_IDX (line_parts, 1, const char *);
              r_part_2 = APR_ARRAY_IDX (line_parts, 2, const char *);
              item->url = APR_ARRAY_IDX (line_parts, 3, const char *);
            }

          if ((! r_part_1) || (r_part_1[0] != '-') || (r_part_1[1] != 'r'))
            goto parse_error;

          if (! r_part_2)  /* "-rN" */
            {
              if (strlen (r_part_1) < 3)
                goto parse_error;
              else
                item->revision.value.number = SVN_STR_TO_REV (r_part_1 + 2);
            }
          else             /* "-r N" */
            {
              if (strlen (r_part_2) < 1)
                goto parse_error;
              else
                item->revision.value.number = SVN_STR_TO_REV (r_part_2);
            }
        }
      else    /* too many items on line */
        goto parse_error;

      if (0)
        {
        parse_error:
          return svn_error_createf
            (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, 0, NULL, pool,
             "error parsing " SVN_PROP_EXTERNALS " property on '%s':\n"
             "Invalid line: '%s'", parent_directory, line);
        }

      apr_hash_set (externals, item->target_dir, APR_HASH_KEY_STRING, item);
    }

  *externals_p = externals;

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
  svn_wc_notify_func_t notify_func;
  void *notify_baton;
  svn_client_auth_baton_t *auth_baton;

  /* If set, then run update on items that didn't change. */
  svn_boolean_t update_unchanged;

  apr_pool_t *pool;
};


/* Return true if NEW_ITEM and OLD_ITEM represent the same external
   item at the same revision checked out into the same target subdir,
   else return false.

   ### If this returned the nature of the difference, we could use it
   to update externals more efficiently.  For example, if we know
   that only the revision number changed, but the target URL did not,
   we could get away with an "update -r" on the external, instead of
   a re-checkout. */
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


/* Remove PATH from revision control, and do the same to any revision
 * controlled directories underneath PATH (including directories not
 * referred to by parent svn administrative areas); then if PATH is
 * empty afterwards, remove it, else rename it to a unique name in the
 * same parent directory.
 *
 * Use POOL for all temporary allocation.
 */
static svn_error_t *
relegate_external (const char *path, apr_pool_t *pool)
{
  svn_error_t *err;
  svn_wc_adm_access_t *adm_access;

  SVN_ERR (svn_wc_adm_open (&adm_access, NULL, path, TRUE, FALSE, pool));
  err = svn_wc_remove_from_revision_control (adm_access,
                                             SVN_WC_ENTRY_THIS_DIR,
                                             TRUE,
                                             pool);

  /* ### Ugly. Unlock only if not going to return an error. Revisit */
  if (!err || err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD)
    SVN_ERR (svn_wc_adm_close (adm_access));

  if (err && (err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD))
    {
      apr_file_t *f;
      const char *new_path;

      /* Reserve the new dir name. */
      SVN_ERR (svn_io_open_unique_file
               (&f, &new_path, path, ".OLD", FALSE, pool));
      apr_file_close (f);  /* toss error */

      /* Sigh...  We must fall ever so slightly from grace.

         Ideally, there would be no window, however brief, when we
         don't have a reservation on the new name.  Unfortunately,
         at least in the Unix (Linux?) version of apr_file_rename(),
         you can't rename a directory over a file, because it's just
         calling stdio rename(), which says:

            ENOTDIR
              A  component used as a directory in oldpath or newpath
              path is not, in fact, a directory.  Or, oldpath  is
              a directory, and newpath exists but is not a directory

         So instead, we get the name, then remove the file (ugh), then
         rename the directory, hoping that nobody has gotten that name
         in the meantime -- which would never happen in real life, so
         no big deal.
      */
      svn_io_remove_file (new_path, pool);  /* toss error */

      /* Rename. */
      SVN_ERR (svn_io_file_rename (path, new_path, pool));
    }
  else if (err)
    return err;

  return SVN_NO_ERROR;
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
  const char *path = svn_path_join (ib->parent_dir,
                                    (const char *) key, ib->pool);

  /* Don't bother to check status, since we'll get that for free by
     attempting to retrieve the hash values anyway.  */

  if (ib->old_desc)
    old_item = apr_hash_get (ib->old_desc, key, klen);
  else
    old_item = NULL;

  if (ib->new_desc)
    new_item = apr_hash_get (ib->new_desc, key, klen);
  else
    new_item = NULL;

  /* We couldn't possibly be here if both values were null, right? */
  assert (old_item || new_item);

  /* There's one potential ugliness.  If a target subdir changed, but
     its URL did not, then ideally we'd just rename the subdir, rather
     than remove the old subdir only to do a new checkout into the new
     subdir.

     We could solve this by "sneaking around the back" and looking in
     ib->new_desc, ib->old_desc to check if anything else in this
     parent_dir has the same URL.  Of course, if an external gets
     moved into some other directory, then we'd lose anyway.  The only
     way to fully handle this would be to harvest a global list based
     on urls/revs, and consult the list every time we're about to
     delete an external subdir: whenever a deletion is really part of
     a rename, then we'd do the rename on the spot.

     IMHO, renames aren't going to be frequent enough to make the
     extra bookkeeping worthwhile.
  */

  /* Not protecting against recursive externals.  Detecting them in
     the global case is hard, and it should be pretty obvious to a
     user when it happens.  Worst case: your disk fills up :-). */

  if (! old_item)
    {
      /* The target dir might have multiple components.  Guarantee
         the path leading down to the last component. */
      {
        const char *checkout_parent;
        svn_path_split_nts (path, &checkout_parent, NULL, ib->pool);
        SVN_ERR (svn_io_make_dir_recursively (checkout_parent, ib->pool));
      }

      /* If we were handling renames the fancy way, then  before
         checking out a new subdir here, we would somehow learn if
         it's really just a rename of an old one.  That would work in
         tandem with the next case -- this case would do nothing,
         knowing that the next case either already has, or soon will,
         rename the external subdirectory. */

      /* First notify that we're about to handle an external. */
      (*ib->notify_func) (ib->notify_baton,
                          path,
                          svn_wc_notify_update_external,
                          svn_node_unknown,
                          NULL,
                          svn_wc_notify_state_unknown,
                          svn_wc_notify_state_unknown,
                          SVN_INVALID_REVNUM);

      SVN_ERR (svn_client_checkout
               (ib->notify_func, ib->notify_baton,
                ib->auth_baton,
                new_item->url,
                path,
                &(new_item->revision),
                TRUE, /* recurse */
                NULL,
                ib->pool));
    }
  else if (! new_item)
    {
      /* See comment in above case about fancy rename handling.  Here,
         before removing an old subdir, we would see if it wants to
         just be renamed to a new one. */ 

      svn_error_t *err;
      svn_wc_adm_access_t *adm_access;

      SVN_ERR (svn_wc_adm_open (&adm_access, NULL, path, TRUE, TRUE,
                                ib->pool));

      /* We don't use relegate_external() here, because we know that
         nothing else in this externals description (at least) is
         going to need this directory, and therefore it's better to
         leave stuff where the user expects it. */
      err = svn_wc_remove_from_revision_control
        (adm_access, SVN_WC_ENTRY_THIS_DIR, TRUE, ib->pool);

      /* ### Ugly. Unlock only if not going to return an error. Revisit */
      if (!err || err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD)
        SVN_ERR (svn_wc_adm_close (adm_access));

      if (err && (err->apr_err != SVN_ERR_WC_LEFT_LOCAL_MOD))
        return err;

      /* ### If there were multiple path components leading down to
         that wc, we could try to remove them too. */
    }
  else if (! compare_external_items (new_item, old_item))
    {
      /* ### Better yet, compare_external_items should report the
         nature of the difference.  That way, when it's just a change
         in the "-r REV" portion, for example, we could do an update
         here instead of a relegation followed by full checkout. */

      SVN_ERR (relegate_external (path, ib->pool));
      
      /* First notify that we're about to handle an external. */
      (*ib->notify_func) (ib->notify_baton,
                          path,
                          svn_wc_notify_update_external,
                          svn_node_unknown,
                          NULL,
                          svn_wc_notify_state_unknown,
                          svn_wc_notify_state_unknown,
                          SVN_INVALID_REVNUM);

      SVN_ERR (svn_client_checkout
               (ib->notify_func, ib->notify_baton,
                ib->auth_baton,
                new_item->url,
                path,
                &(new_item->revision),
                TRUE, /* recurse */
                NULL,
                ib->pool));
    }
  else if (ib->update_unchanged)
    {
      /* Exact same item is present in both hashes, and caller wants
         to update such unchanged items. */

      svn_error_t *err;

      /* First notify that we're about to handle an external. */
      (*ib->notify_func) (ib->notify_baton,
                          path,
                          svn_wc_notify_update_external,
                          svn_node_unknown,
                          NULL,
                          svn_wc_notify_state_unknown,
                          svn_wc_notify_state_unknown,
                          SVN_INVALID_REVNUM);

      /* Try an update, but if no such dir, then check out instead. */
      err = svn_client_update (ib->auth_baton,
                               path,
                               NULL,
                               &(new_item->revision),
                               TRUE, /* recurse */
                               ib->notify_func, ib->notify_baton,
                               ib->pool);

      if (err && (err->apr_err == SVN_ERR_ENTRY_NOT_FOUND))
        {
          /* No problem.  Probably user added this external item, but
             hasn't updated since then, so they don't actually have a
             working copy of it yet.  Just check it out. */
          
          /* The target dir might have multiple components.  Guarantee
             the path leading down to the last component. */
          {
            const char *parent;
            svn_path_split_nts (path, &parent, NULL, ib->pool);
            SVN_ERR (svn_io_make_dir_recursively (parent, ib->pool));
          }
          
          SVN_ERR (svn_client_checkout
                   (ib->notify_func, ib->notify_baton,
                    ib->auth_baton,
                    new_item->url,
                    path,
                    &(new_item->revision),
                    TRUE, /* recurse */
                    NULL,
                    ib->pool));
        }
      else if (err)
        return err;
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
  svn_wc_notify_func_t notify_func;
  void *notify_baton;
  svn_client_auth_baton_t *auth_baton;
  svn_boolean_t update_unchanged;

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
  ib.notify_func       = cb->notify_func;
  ib.notify_baton      = cb->notify_baton;
  ib.auth_baton        = cb->auth_baton;
  ib.update_unchanged  = cb->update_unchanged;
  ib.pool              = cb->pool;

  SVN_ERR (svn_hash_diff (old_desc, new_desc,
                          handle_external_item_change, &ib, cb->pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__handle_externals (svn_wc_traversal_info_t *traversal_info,
                              svn_wc_notify_func_t notify_func,
                              void *notify_baton,
                              svn_client_auth_baton_t *auth_baton,
                              svn_boolean_t update_unchanged,
                              apr_pool_t *pool)
{
  apr_hash_t *externals_old, *externals_new;
  struct handle_externals_desc_change_baton cb;

  svn_wc_edited_externals (&externals_old, &externals_new, traversal_info);

  cb.externals_new     = externals_new;
  cb.externals_old     = externals_old;
  cb.notify_func       = notify_func;
  cb.notify_baton      = notify_baton;
  cb.auth_baton        = auth_baton;
  cb.update_unchanged  = update_unchanged;
  cb.pool              = pool;

  SVN_ERR (svn_hash_diff (externals_old, externals_new,
                          handle_externals_desc_change, &cb, pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: 
 */
