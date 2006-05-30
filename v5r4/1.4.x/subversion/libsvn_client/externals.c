/*
 * externals.c:  handle the svn:externals property
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include "svn_client.h"
#include "svn_hash.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"



/* Closure for handle_external_item_change. */
struct handle_external_item_change_baton
{
  /* As returned by svn_wc_parse_externals_description(). */
  apr_hash_t *new_desc;
  apr_hash_t *old_desc;

  /* The directory that has this externals property. */
  const char *parent_dir;

  /* Passed through to svn_client_* functions. */
  svn_client_ctx_t *ctx;

  /* If set, then run update on items that didn't change. */
  svn_boolean_t update_unchanged;
  svn_boolean_t *timestamp_sleep;
  svn_boolean_t is_export;

  /* A scratchwork pool -- do not put anything in here that needs to
     outlive the hash diffing callback! */
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
compare_external_items(svn_wc_external_item_t *new_item,
                       svn_wc_external_item_t *old_item)
{
  if ((strcmp(new_item->target_dir, old_item->target_dir) != 0)
      || (strcmp(new_item->url, old_item->url) != 0)
      || (! svn_client__compare_revisions(&(new_item->revision),
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
 * Pass CANCEL_FUNC, CANCEL_BATON to svn_wc_remove_from_revision_control.
 *
 * Use POOL for all temporary allocation.
 */
static svn_error_t *
relegate_external(const char *path,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *pool)
{
  svn_error_t *err;
  svn_wc_adm_access_t *adm_access;

  SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, path, TRUE, -1, cancel_func,
                           cancel_baton, pool));
  err = svn_wc_remove_from_revision_control(adm_access,
                                            SVN_WC_ENTRY_THIS_DIR,
                                            TRUE, FALSE,
                                            cancel_func,
                                            cancel_baton,
                                            pool);

  /* ### Ugly. Unlock only if not going to return an error. Revisit */
  if (!err || err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD)
    SVN_ERR(svn_wc_adm_close(adm_access));

  if (err && (err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD))
    {
      const char *new_path;

      svn_error_clear(err);

      /* Reserve the new dir name. */
      SVN_ERR(svn_io_open_unique_file2
              (NULL, &new_path, path, ".OLD", svn_io_file_del_none, pool));

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
      err = svn_io_remove_file(new_path, pool);
      if (err)
        svn_error_clear(err);  /* It's not clear why this is ignored, is
                                   it because the rename will catch it? */

      /* Rename. */
      SVN_ERR(svn_io_file_rename(path, new_path, pool));
    }
  else if (err)
    return err;

  return SVN_NO_ERROR;
}

/* Try to update an external PATH to URL at REVISION.
   Use POOL for temporary allocations, and use the client context CTX. */
static svn_error_t *
switch_external(const char *path,
                const char *url,
                const svn_opt_revision_t *revision,
                svn_boolean_t *timestamp_sleep,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_error_t *err;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* First notify that we're about to handle an external. */
  if (ctx->notify_func2)
    ctx->notify_func2(ctx->notify_baton2,
                      svn_wc_create_notify(path, svn_wc_notify_update_external,
                                           pool), pool);
  
  /* If path is a directory, try to update/switch to the correct URL
     and revison. */
  SVN_ERR(svn_io_check_path(path, &kind, pool));
  if (kind == svn_node_dir)
    {
      svn_wc_adm_access_t *adm_access;
      const svn_wc_entry_t *entry;

      SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, path, TRUE, 0,
                               ctx->cancel_func, ctx->cancel_baton, subpool));
      SVN_ERR(svn_wc_entry(&entry, path, adm_access, 
                           FALSE, subpool));
      SVN_ERR(svn_wc_adm_close(adm_access));

      if (entry && entry->url)
        {
          /* If we have what appears to be a version controlled
             subdir, and its top-level URL matches that of our
             externals definition, perform an update. */
          if (strcmp(entry->url, url) == 0)
            {
              SVN_ERR(svn_client__update_internal(NULL, path, revision,
                                                  TRUE, FALSE, timestamp_sleep,
                                                  ctx, pool));
              return SVN_NO_ERROR;
            }
          else if (entry->repos)
            {
              /* URLs don't match.  Try to relocate (if necessary) and then
                 switch. */
              if (! svn_path_is_ancestor(entry->repos, url))
                {
                  const char *repos_root;
                  svn_ra_session_t *ra_session;

                  /* Get the repos root of the new URL. */
                  SVN_ERR(svn_client__open_ra_session_internal
                          (&ra_session, url, NULL, NULL, NULL, FALSE, TRUE,
                           ctx, subpool));
                  SVN_ERR(svn_ra_get_repos_root(ra_session, &repos_root,
                                                subpool));

                  err = svn_client_relocate(path, entry->repos, repos_root,
                                            TRUE, ctx, subpool);
                  /* If the relocation failed because the new URL points
                     to another repository, then we need to relegate and
                     check out a new WC. */
                  if (err
                      && (err->apr_err == SVN_ERR_WC_INVALID_RELOCATION
                          || (err->apr_err
                              == SVN_ERR_CLIENT_INVALID_RELOCATION)))
                    {
                      svn_error_clear(err);
                      goto relegate;
                    }
                  else if (err)
                    return err;
                }

              SVN_ERR(svn_client__switch_internal(NULL, path, url, revision,
                                                  TRUE, timestamp_sleep, ctx,
                                                  subpool));

              return SVN_NO_ERROR;
            }
        }
    }

 relegate:
      
  /* Fall back on removing the WC and checking out a new one. */

  /* Ensure that we don't have any RA sessions or WC locks from failed
     operations above. */
  svn_pool_destroy(subpool);

  if (kind == svn_node_dir)
    /* Buh-bye, old and busted ... */
    SVN_ERR(relegate_external(path,
                              ctx->cancel_func,
                              ctx->cancel_baton,
                              pool));
  else
    {
      /* The target dir might have multiple components.  Guarantee
         the path leading down to the last component. */
      const char *parent = svn_path_dirname(path, pool);
      SVN_ERR(svn_io_make_dir_recursively(parent, pool));
    }

  /* ... Hello, new hotness. */
  SVN_ERR(svn_client__checkout_internal(NULL, url, path, revision, revision,
                                        TRUE, FALSE, timestamp_sleep,
                                        ctx, pool));

  return SVN_NO_ERROR;
}

/* This implements the 'svn_hash_diff_func_t' interface.
   BATON is of type 'struct handle_external_item_change_baton *'.  */
static svn_error_t *
handle_external_item_change(const void *key, apr_ssize_t klen,
                            enum svn_hash_diff_key_status status,
                            void *baton)
{
  struct handle_external_item_change_baton *ib = baton;
  svn_wc_external_item_t *old_item, *new_item;
  const char *parent;
  const char *path = svn_path_join(ib->parent_dir,
                                   (const char *) key, ib->pool);

  /* Don't bother to check status, since we'll get that for free by
     attempting to retrieve the hash values anyway.  */

  if ((ib->old_desc) && (! ib->is_export))
    old_item = apr_hash_get(ib->old_desc, key, klen);
  else
    old_item = NULL;

  if (ib->new_desc)
    new_item = apr_hash_get(ib->new_desc, key, klen);
  else
    new_item = NULL;

  /* We couldn't possibly be here if both values were null, right? */
  assert(old_item || new_item);

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
      svn_path_split(path, &parent, NULL, ib->pool);
      SVN_ERR(svn_io_make_dir_recursively(parent, ib->pool));

      /* If we were handling renames the fancy way, then  before
         checking out a new subdir here, we would somehow learn if
         it's really just a rename of an old one.  That would work in
         tandem with the next case -- this case would do nothing,
         knowing that the next case either already has, or soon will,
         rename the external subdirectory. */

      /* First notify that we're about to handle an external. */
      if (ib->ctx->notify_func2)
        (*ib->ctx->notify_func2)
          (ib->ctx->notify_baton2,
           svn_wc_create_notify(path, svn_wc_notify_update_external,
                                ib->pool), ib->pool);

      if (ib->is_export)
        /* ### It should be okay to "force" this export.  Externals
           only get created in subdirectories of versioned
           directories, so an external directory couldn't already
           exist before the parent export process unless a versioned
           directory above it did, which means the user would have
           already had to force these creations to occur. */
        SVN_ERR(svn_client_export3(NULL, new_item->url, path,
                                   &(new_item->revision),
                                   &(new_item->revision),
                                   TRUE, FALSE, TRUE, NULL,
                                   ib->ctx, ib->pool));
      else
        SVN_ERR(svn_client__checkout_internal(NULL, new_item->url, path,
                                              &(new_item->revision),
                                              &(new_item->revision),
                                              TRUE, FALSE,
                                              ib->timestamp_sleep,
                                              ib->ctx, ib->pool));
    }
  else if (! new_item)
    {
      /* See comment in above case about fancy rename handling.  Here,
         before removing an old subdir, we would see if it wants to
         just be renamed to a new one. */ 

      svn_error_t *err, *err2;
      svn_wc_adm_access_t *adm_access;

      SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, path, TRUE, -1,
                               ib->ctx->cancel_func, ib->ctx->cancel_baton,
                               ib->pool));

      /* We don't use relegate_external() here, because we know that
         nothing else in this externals description (at least) is
         going to need this directory, and therefore it's better to
         leave stuff where the user expects it. */
      err = svn_wc_remove_from_revision_control
        (adm_access, SVN_WC_ENTRY_THIS_DIR, TRUE, FALSE,
         ib->ctx->cancel_func, ib->ctx->cancel_baton, ib->pool);

      /* ### Ugly. Unlock only if not going to return an error. Revisit */
      if (!err || err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD)
        if ((err2 = svn_wc_adm_close(adm_access)))
          {
            if (!err)
              err = err2;
            else
              svn_error_clear(err2);
          }

      if (err && (err->apr_err != SVN_ERR_WC_LEFT_LOCAL_MOD))
        return err;
      svn_error_clear(err);

      /* ### If there were multiple path components leading down to
         that wc, we could try to remove them too. */
    }
  else if (! compare_external_items(new_item, old_item)
           || ib->update_unchanged)
    {
      /* Either the URL changed, or the exact same item is present in
         both hashes, and caller wants to update such unchanged items.
         In the latter case, the call below will try to make sure that
         the external really is a WC pointing to the correct
         URL/revision. */
      SVN_ERR(switch_external(path, new_item->url, &(new_item->revision),
                              ib->timestamp_sleep, ib->ctx, ib->pool));
    }

  /* Clear IB->pool -- we only use it for scratchwork (and this will
     close any RA sessions still open in this pool). */
  svn_pool_clear(ib->pool);

  return SVN_NO_ERROR;
}


/* Closure for handle_externals_change. */
struct handle_externals_desc_change_baton
{
  /* As returned by svn_wc_edited_externals(). */
  apr_hash_t *externals_new;
  apr_hash_t *externals_old;

  /* Passed through to handle_external_item_change_baton. */
  svn_client_ctx_t *ctx;
  svn_boolean_t update_unchanged;
  svn_boolean_t *timestamp_sleep;
  svn_boolean_t is_export;

  apr_pool_t *pool;
};


/* This implements the 'svn_hash_diff_func_t' interface.
   BATON is of type 'struct handle_externals_desc_change_baton *'.  
*/
static svn_error_t *
handle_externals_desc_change(const void *key, apr_ssize_t klen,
                             enum svn_hash_diff_key_status status,
                             void *baton)
{
  struct handle_externals_desc_change_baton *cb = baton;
  struct handle_external_item_change_baton ib;
  const char *old_desc_text, *new_desc_text;
  apr_array_header_t *old_desc, *new_desc;
  apr_hash_t *old_desc_hash, *new_desc_hash;
  int i;
  svn_wc_external_item_t *item;

  if ((old_desc_text = apr_hash_get(cb->externals_old, key, klen)))
    SVN_ERR(svn_wc_parse_externals_description2(&old_desc, key,
                                                old_desc_text, cb->pool));
  else
    old_desc = NULL;

  if ((new_desc_text = apr_hash_get(cb->externals_new, key, klen)))
    SVN_ERR(svn_wc_parse_externals_description2(&new_desc, key,
                                                new_desc_text, cb->pool));
  else
    new_desc = NULL;

  old_desc_hash = apr_hash_make(cb->pool);
  new_desc_hash = apr_hash_make(cb->pool);

  /* Create hashes of our two externals arrays so that we can
     efficiently generate a diff for them. */
  for (i = 0; old_desc && (i < old_desc->nelts); i++)
    {
      item = APR_ARRAY_IDX(old_desc, i, svn_wc_external_item_t *);

      apr_hash_set(old_desc_hash, item->target_dir,
                   APR_HASH_KEY_STRING, item);
    }
  
  for (i = 0; new_desc && (i < new_desc->nelts); i++)
    {
      item = APR_ARRAY_IDX(new_desc, i, svn_wc_external_item_t *);

      apr_hash_set(new_desc_hash, item->target_dir,
                   APR_HASH_KEY_STRING, item);
    }

  ib.old_desc          = old_desc_hash;
  ib.new_desc          = new_desc_hash;
  ib.parent_dir        = (const char *) key;
  ib.ctx               = cb->ctx;
  ib.update_unchanged  = cb->update_unchanged;
  ib.is_export         = cb->is_export;
  ib.timestamp_sleep   = cb->timestamp_sleep;
  ib.pool              = svn_pool_create(cb->pool);

  /* We must use a custom version of svn_hash_diff so that the diff
     entries are processed in the order they were originally specified
     in the svn:externals properties. */

  for (i = 0; old_desc && (i < old_desc->nelts); i++)
    {
      item = APR_ARRAY_IDX(old_desc, i, svn_wc_external_item_t *);

      if (apr_hash_get(new_desc_hash, item->target_dir, APR_HASH_KEY_STRING))
        SVN_ERR(handle_external_item_change(item->target_dir,
                                            APR_HASH_KEY_STRING,
                                            svn_hash_diff_key_both, &ib));
      else
        SVN_ERR(handle_external_item_change(item->target_dir,
                                            APR_HASH_KEY_STRING,
                                            svn_hash_diff_key_a, &ib));
    }
  for (i = 0; new_desc && (i < new_desc->nelts); i++)
    {
      item = APR_ARRAY_IDX(new_desc, i, svn_wc_external_item_t *);
      if (! apr_hash_get(old_desc_hash, item->target_dir, APR_HASH_KEY_STRING))
        SVN_ERR(handle_external_item_change(item->target_dir,
                                            APR_HASH_KEY_STRING,
                                            svn_hash_diff_key_b, &ib));
    }
  
  /* Now destroy the subpool we pass to the hash differ.  This will
     close any remaining RA sessions used by the hash diff callback. */
  svn_pool_destroy(ib.pool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__handle_externals(svn_wc_traversal_info_t *traversal_info,
                             svn_boolean_t update_unchanged,
                             svn_boolean_t *timestamp_sleep,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool)
{
  apr_hash_t *externals_old, *externals_new;
  struct handle_externals_desc_change_baton cb;

  svn_wc_edited_externals(&externals_old, &externals_new, traversal_info);

  cb.externals_new     = externals_new;
  cb.externals_old     = externals_old;
  cb.ctx               = ctx;
  cb.update_unchanged  = update_unchanged;
  cb.timestamp_sleep   = timestamp_sleep;
  cb.is_export         = FALSE;
  cb.pool              = pool;

  SVN_ERR(svn_hash_diff(cb.externals_old, cb.externals_new,
                        handle_externals_desc_change, &cb, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__fetch_externals(apr_hash_t *externals,
                            svn_boolean_t is_export,
                            svn_boolean_t *timestamp_sleep,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  struct handle_externals_desc_change_baton cb;

  cb.externals_new     = externals;
  cb.externals_old     = apr_hash_make(pool);
  cb.ctx               = ctx;
  cb.update_unchanged  = TRUE;
  cb.timestamp_sleep   = timestamp_sleep;
  cb.is_export         = is_export;
  cb.pool              = pool;

  SVN_ERR(svn_hash_diff(cb.externals_old, cb.externals_new,
                        handle_externals_desc_change, &cb, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__do_external_status(svn_wc_traversal_info_t *traversal_info,
                               svn_wc_status_func2_t status_func,
                               void *status_baton,
                               svn_boolean_t get_all,
                               svn_boolean_t update,
                               svn_boolean_t no_ignore,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *pool)
{
  apr_hash_t *externals_old, *externals_new;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Get the values of the svn:externals properties. */
  svn_wc_edited_externals(&externals_old, &externals_new, traversal_info);

  /* Loop over the hash of new values (we don't care about the old
     ones).  This is a mapping of versioned directories to property
     values. */
  for (hi = apr_hash_first(pool, externals_new); 
       hi; 
       hi = apr_hash_next(hi))
    {
      apr_array_header_t *exts;
      const void *key;
      void *val;
      const char *path;
      const char *propval;
      apr_pool_t *iterpool;
      int i;

      /* Clear the subpool. */
      svn_pool_clear(subpool);

      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      propval = val;

      /* Parse the svn:externals property value.  This results in a
         hash mapping subdirectories to externals structures. */
      SVN_ERR(svn_wc_parse_externals_description2(&exts, path, 
                                                  propval, subpool));

      /* Make a sub-pool of SUBPOOL. */
      iterpool = svn_pool_create(subpool);

      /* Loop over the subdir array. */
      for (i = 0; exts && (i < exts->nelts); i++)
        {
          const char *fullpath;
          svn_wc_external_item_t *external;
          svn_node_kind_t kind;

          svn_pool_clear(iterpool);

          external = APR_ARRAY_IDX(exts, i, svn_wc_external_item_t *);
          fullpath = svn_path_join(path, external->target_dir, iterpool);

          /* If the external target directory doesn't exist on disk,
             just skip it. */
          SVN_ERR(svn_io_check_path(fullpath, &kind, iterpool));
          if (kind != svn_node_dir)
            continue;

          /* Tell the client we're staring an external status set. */
          if (ctx->notify_func2)
            (ctx->notify_func2)
              (ctx->notify_baton2,
               svn_wc_create_notify(fullpath, svn_wc_notify_status_external,
                                    iterpool), iterpool);

          /* And then do the status. */
          SVN_ERR(svn_client_status2(NULL, fullpath, 
                                     &(external->revision),
                                     status_func, status_baton, 
                                     TRUE, get_all, update, no_ignore, FALSE,
                                     ctx, iterpool));
        }
    } 
  
  /* Destroy SUBPOOL and (implicitly) ITERPOOL. */
  apr_pool_destroy(subpool);

  return SVN_NO_ERROR;
}
