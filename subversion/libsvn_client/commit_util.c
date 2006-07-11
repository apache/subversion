/*
 * commit_util.c:  Driver for the WC commit process.
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


#include <string.h>

#include <apr_pools.h>
#include <apr_hash.h>

#include "client.h"
#include "svn_path.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_props.h"
#include "svn_md5.h"

#include <assert.h>
#include <stdlib.h>  /* for qsort() */

#include "svn_private_config.h"

/*** Uncomment this to turn on commit driver debugging. ***/
/*
#define SVN_CLIENT_COMMIT_DEBUG
*/



/*** Harvesting Commit Candidates ***/


/* Add a new commit candidate (described by all parameters except
   `COMMITTABLES') to the COMMITABLES hash. */
static void
add_committable(apr_hash_t *committables,
                const char *path,
                svn_node_kind_t kind,
                const char *url,
                svn_revnum_t revision,
                const char *copyfrom_url,
                svn_revnum_t copyfrom_rev,
                apr_byte_t state_flags)
{
  apr_pool_t *pool = apr_hash_pool_get(committables);
  const char *repos_name = SVN_CLIENT__SINGLE_REPOS_NAME;
  apr_array_header_t *array;
  svn_client_commit_item2_t *new_item;

  /* Sanity checks. */
  assert(path && url);

  /* ### todo: Get the canonical repository for this item, which will
     be the real key for the COMMITTABLES hash, instead of the above
     bogosity. */
  array = apr_hash_get(committables, repos_name, APR_HASH_KEY_STRING);

  /* E-gads!  There is no array for this repository yet!  Oh, no
     problem, we'll just create (and add to the hash) one. */
  if (array == NULL)
    {
      array = apr_array_make(pool, 1, sizeof(new_item));
      apr_hash_set(committables, repos_name, APR_HASH_KEY_STRING, array);
    }

  /* Now update pointer values, ensuring that their allocations live
     in POOL. */
  new_item = apr_pcalloc(pool, sizeof(*new_item));
  new_item->path           = apr_pstrdup(pool, path);
  new_item->kind           = kind;
  new_item->url            = apr_pstrdup(pool, url);
  new_item->revision       = revision;
  new_item->copyfrom_url   = copyfrom_url 
    ? apr_pstrdup(pool, copyfrom_url) : NULL;
  new_item->copyfrom_rev   = copyfrom_rev;
  new_item->state_flags    = state_flags;
  new_item->wcprop_changes = apr_array_make(pool, 1, sizeof(svn_prop_t *));
   
  /* Now, add the commit item to the array. */
  APR_ARRAY_PUSH(array, svn_client_commit_item2_t *) = new_item;
}


static svn_error_t *
check_prop_mods(svn_boolean_t *props_changed,
                svn_boolean_t *eol_prop_changed,
                const char *path,
                svn_wc_adm_access_t *adm_access,
                apr_pool_t *pool)
{
  apr_array_header_t *prop_mods;
  int i;

  *eol_prop_changed = *props_changed = FALSE;
  SVN_ERR(svn_wc_props_modified_p(props_changed, path, adm_access, pool));
  if (! *props_changed)
    return SVN_NO_ERROR;
  SVN_ERR(svn_wc_get_prop_diffs(&prop_mods, NULL, path, adm_access, pool));
  for (i = 0; i < prop_mods->nelts; i++)
    {
      svn_prop_t *prop_mod = &APR_ARRAY_IDX(prop_mods, i, svn_prop_t);
      if (strcmp(prop_mod->name, SVN_PROP_EOL_STYLE) == 0)
        *eol_prop_changed = TRUE;
    }
  return SVN_NO_ERROR;
}


/* If there is a commit item for PATH in COMMITTABLES, return it, else
   return NULL.  Use POOL for temporary allocation only. */
static svn_client_commit_item2_t *
look_up_committable(apr_hash_t *committables,
                    const char *path,
                    apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, committables); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      apr_array_header_t *these_committables;
      int i;
      
      apr_hash_this(hi, &key, NULL, &val);
      these_committables = val;
      
      for (i = 0; i < these_committables->nelts; i++)
        {
          svn_client_commit_item2_t *this_committable
            = APR_ARRAY_IDX(these_committables, i,
                            svn_client_commit_item2_t *);
          
          if (strcmp(this_committable->path, path) == 0)
            return this_committable;
        }
    }

  return NULL;
}

/* This implements the svn_wc_entry_callbacks_t->found_entry interface. */
static svn_error_t *
add_lock_token(const char *path, const svn_wc_entry_t *entry,
               void *walk_baton, apr_pool_t *pool)
{
  apr_hash_t *lock_tokens = walk_baton;
  apr_pool_t *token_pool = apr_hash_pool_get(lock_tokens);

  /* I want every lock-token I can get my dirty hands on!
     If this entry is switched, so what.  We will send an irrelevant lock
     token. */
  if (entry->url && entry->lock_token)
    apr_hash_set(lock_tokens, apr_pstrdup(token_pool, entry->url),
                 APR_HASH_KEY_STRING,
                 apr_pstrdup(token_pool, entry->lock_token));

  return SVN_NO_ERROR;
}

/* Entry walker callback table to add lock tokens in an hierarchy. */
static svn_wc_entry_callbacks_t add_tokens_callbacks = {
  add_lock_token
};

/* Recursively search for commit candidates in (and under) PATH (with
   entry ENTRY and ancestry URL), and add those candidates to
   COMMITTABLES.  If in ADDS_ONLY modes, only new additions are
   recognized.  COPYFROM_URL is the default copyfrom-url for children
   of copied directories.  NONRECURSIVE indicates that this function
   will not recurse into subdirectories of PATH when PATH is itself a
   directory.  Lock tokens of candidates will be added to LOCK_TOKENS, if
   non-NULL.  JUST_LOCKED indicates whether to treat non-modified items with
   lock tokens as commit candidates.

   If in COPY_MODE, treat the entry as if it is destined to be added
   with history as URL, and add 'deleted' entries to COMMITTABLES as
   items to delete in the copy destination.

   If CTX->CANCEL_FUNC is non-null, call it with CTX->CANCEL_BATON to see 
   if the user has cancelled the operation.  */
static svn_error_t *
harvest_committables(apr_hash_t *committables,
                     apr_hash_t *lock_tokens,
                     const char *path,
                     svn_wc_adm_access_t *adm_access,
                     const char *url,
                     const char *copyfrom_url,
                     const svn_wc_entry_t *entry,
                     const svn_wc_entry_t *parent_entry,
                     svn_boolean_t adds_only,
                     svn_boolean_t copy_mode,
                     svn_boolean_t nonrecursive,
                     svn_boolean_t just_locked,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  apr_hash_t *entries = NULL;
  svn_boolean_t text_mod = FALSE, prop_mod = FALSE;
  apr_byte_t state_flags = 0;
  svn_node_kind_t kind;
  const char *p_path;
  svn_boolean_t tc, pc;
  const char *cf_url = NULL;
  svn_revnum_t cf_rev = entry->copyfrom_rev;
  const svn_string_t *propval;
  svn_boolean_t is_special;
  apr_pool_t *token_pool = (lock_tokens ? apr_hash_pool_get(lock_tokens)
                            : NULL);

  /* Early out if the item is already marked as committable. */
  if (look_up_committable(committables, path, pool))
    return SVN_NO_ERROR;

  assert(entry);
  assert(url);

  if (ctx->cancel_func)
    SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

  /* Make P_PATH the parent dir. */
  p_path = svn_path_dirname(path, pool);

  /* Return error on unknown path kinds.  We check both the entry and
     the node itself, since a path might have changed kind since its
     entry was written. */
  if ((entry->kind != svn_node_file) && (entry->kind != svn_node_dir))
    return svn_error_createf
      (SVN_ERR_NODE_UNKNOWN_KIND, NULL, _("Unknown entry kind for '%s'"),
       svn_path_local_style(path, pool));

  SVN_ERR(svn_io_check_special_path(path, &kind, &is_special, pool));

  if ((kind != svn_node_file)
      && (kind != svn_node_dir)
      && (kind != svn_node_none))
    {
      return svn_error_createf
        (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
         _("Unknown entry kind for '%s'"),
         svn_path_local_style(path, pool));
    }

  /* Verify that the node's type has not changed before attempting to
     commit. */
  SVN_ERR(svn_wc_prop_get(&propval, SVN_PROP_SPECIAL, path, adm_access,
                          pool));

  if ((((! propval) && (is_special))
#ifdef HAVE_SYMLINK  
       || ((propval) && (! is_special))
#endif /* HAVE_SYMLINK */
       ) && (kind != svn_node_none))
    {
      return svn_error_createf
        (SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
         _("Entry '%s' has unexpectedly changed special status"),
         svn_path_local_style(path, pool));
    }

  /* Get a fully populated entry for PATH if we can, and check for
     conflicts. If this is a directory ... */
  if (entry->kind == svn_node_dir)
    { 
      /* ... then try to read its own entries file so we have a full
         entry for it (we were going to have to do this eventually to
         recurse anyway, so... ) */
      svn_error_t *err;
      const svn_wc_entry_t *e = NULL;
      err = svn_wc_entries_read(&entries, adm_access, copy_mode, pool);

      /* If we failed to get an entries hash for the directory, no
         sweat.  Cleanup and move along.  */
      if (err)
        {
          svn_error_clear(err);
          entries = NULL;
        }
      
      /* If we got an entries hash, and the "this dir" entry is
         present, override our current ENTRY with it, and check for
         conflicts. */
      if ((entries) && ((e = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR, 
                                          APR_HASH_KEY_STRING))))
        {
          entry = e;
          SVN_ERR(svn_wc_conflicted_p(&tc, &pc, path, entry, pool));
        }

      /* No new entry?  Just check the parent's pointer for
         conflicts. */
      else
        {
          SVN_ERR(svn_wc_conflicted_p(&tc, &pc, p_path, entry, pool));
        }
    }

  /* If this is not a directory, check for conflicts using the
     parent's path. */
  else
    {
      SVN_ERR(svn_wc_conflicted_p(&tc, &pc, p_path, entry, pool));
    }

  /* Bail now if any conflicts exist for the ENTRY. */
  if (tc || pc)
    return svn_error_createf(SVN_ERR_WC_FOUND_CONFLICT, NULL,
                             _("Aborting commit: '%s' remains in conflict"),
                             svn_path_local_style(path, pool));

  /* If we have our own URL, and we're NOT in COPY_MODE, it wins over
     the telescoping one(s).  In COPY_MODE, URL will always be the
     URL-to-be of the copied item.  */
  if ((entry->url) && (! copy_mode))
    url = entry->url;

  /* Check for the deletion case.  Deletes occur only when not in
     "adds-only mode".  We use the SVN_CLIENT_COMMIT_ITEM_DELETE flag
     to represent two slightly different conditions:

     - The entry is marked as 'deleted'.  When copying a mixed-rev wc,
       we still need to send a delete for that entry, otherwise the
       object will wrongly exist in the repository copy.

     - The entry is scheduled for deletion or replacement, which case
       we need to send a delete either way.
  */
  if ((! adds_only)
      && ((entry->deleted && entry->schedule == svn_wc_schedule_normal)
          || (entry->schedule == svn_wc_schedule_delete)
          || (entry->schedule == svn_wc_schedule_replace)))
    {
      state_flags |= SVN_CLIENT_COMMIT_ITEM_DELETE;
    }

  /* Check for the trivial addition case.  Adds can be explicit
     (schedule == add) or implicit (schedule == replace ::= delete+add).  
     We also note whether or not this is an add with history here.  */
  if ((entry->schedule == svn_wc_schedule_add)
      || (entry->schedule == svn_wc_schedule_replace))
    {
      state_flags |= SVN_CLIENT_COMMIT_ITEM_ADD;
      if (entry->copyfrom_url)
        {
          state_flags |= SVN_CLIENT_COMMIT_ITEM_IS_COPY;
          cf_url = entry->copyfrom_url;
          adds_only = FALSE;
        }
      else
        {
          adds_only = TRUE;
        }
    }

  /* Check for the copied-subtree addition case.  */
  if ((entry->copied || copy_mode) 
      && (! entry->deleted)
      && (entry->schedule == svn_wc_schedule_normal))
    {
      svn_revnum_t p_rev = entry->revision - 1; /* arbitrary non-equal value */
      svn_boolean_t wc_root = FALSE;

      /* If this is not a WC root then its parent's revision is
         admissible for comparitive purposes. */
      SVN_ERR(svn_wc_is_wc_root(&wc_root, path, adm_access, pool));
      if (! wc_root)
        {
          if (parent_entry)
            p_rev = parent_entry->revision;
        }
      else if (! copy_mode)
        return svn_error_createf 
          (SVN_ERR_WC_CORRUPT, NULL,
           _("Did not expect '%s' to be a working copy root"),
           svn_path_local_style(path, pool));

      /* If the ENTRY's revision differs from that of its parent, we
         have to explicitly commit ENTRY as a copy. */
      if (entry->revision != p_rev)
        {
          state_flags |= SVN_CLIENT_COMMIT_ITEM_ADD;
          state_flags |= SVN_CLIENT_COMMIT_ITEM_IS_COPY;
          adds_only = FALSE;
          cf_rev = entry->revision;
          if (copy_mode)
            cf_url = entry->url;
          else if (copyfrom_url)
            cf_url = copyfrom_url;
          else /* ### See issue #830 */
            return svn_error_createf 
              (SVN_ERR_BAD_URL, NULL,
               _("Commit item '%s' has copy flag but no copyfrom URL"),
               svn_path_local_style(path, pool));
        }
    }

  /* If an add is scheduled to occur, dig around for some more
     information about it. */
  if (state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
    {
      svn_boolean_t eol_prop_changed;

      /* See if there are property modifications to send. */
      SVN_ERR(check_prop_mods(&prop_mod, &eol_prop_changed, path, 
                              adm_access, pool));

      /* Regular adds of files have text mods, but for copies we have
         to test for textual mods.  Directories simply don't have text! */
      if (entry->kind == svn_node_file)
        {
          /* Check for text mods.  If EOL_PROP_CHANGED is TRUE, then
             we need to force a translated byte-for-byte comparison
             against the text-base so that a timestamp comparison
             won't bail out early.  Depending on how the svn:eol-style
             prop was changed, we might have to send new text to the
             server to match the new newline style.  */
          if (state_flags & SVN_CLIENT_COMMIT_ITEM_IS_COPY)
            SVN_ERR(svn_wc_text_modified_p(&text_mod, path,
                                           eol_prop_changed, adm_access,
                                           pool));
          else
            text_mod = TRUE;
        }
    }

  /* Else, if we aren't deleting this item, we'll have to look for
     local text or property mods to determine if the path might be
     committable. */
  else if (! (state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE))
    {
      svn_boolean_t eol_prop_changed;

      /* See if there are property modifications to send. */
      SVN_ERR(check_prop_mods(&prop_mod, &eol_prop_changed, path, 
                              adm_access, pool));

      /* Check for text mods on files.  If EOL_PROP_CHANGED is TRUE,
         then we need to force a translated byte-for-byte comparison
         against the text-base so that a timestamp comparison won't
         bail out early.  Depending on how the svn:eol-style prop was
         changed, we might have to send new text to the server to
         match the new newline style.  */
      if (entry->kind == svn_node_file)
        SVN_ERR(svn_wc_text_modified_p(&text_mod, path, eol_prop_changed,
                                       adm_access, pool));
    }

  /* Set text/prop modification flags accordingly. */
  if (text_mod)
    state_flags |= SVN_CLIENT_COMMIT_ITEM_TEXT_MODS;
  if (prop_mod)
    state_flags |= SVN_CLIENT_COMMIT_ITEM_PROP_MODS;

  /* If the entry has a lock token and it is already a commit candidate,
     or the caller wants unmodified locked items to be treated as
     such, note this fact. */
  if (entry->lock_token
      && (state_flags || just_locked))
    state_flags |= SVN_CLIENT_COMMIT_ITEM_LOCK_TOKEN;

  /* Now, if this is something to commit, add it to our list. */
  if (state_flags)
    {
      /* Finally, add the committable item. */
      add_committable(committables, path, entry->kind, url,
                      entry->revision,
                      cf_url,
                      cf_rev,
                      state_flags);
      if (lock_tokens && entry->lock_token)
        apr_hash_set(lock_tokens, apr_pstrdup(token_pool, url),
                     APR_HASH_KEY_STRING,
                     apr_pstrdup(token_pool, entry->lock_token));
    }

  /* For directories, recursively handle each of their entries (except
     when the directory is being deleted, unless the deletion is part
     of a replacement ... how confusing).  Oh, and don't recurse at
     all if this is a nonrecursive commit.  ### We'll probably make
     the whole 'nonrecursive' concept go away soon and be replaced
     with the more sophisticated Depth0|Depth1|DepthInfinity. */
  if (entries && (! nonrecursive)
      && ((! (state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE))
          || (state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)))
    {
      apr_hash_index_t *hi;
      const svn_wc_entry_t *this_entry;
      apr_pool_t *loop_pool = svn_pool_create(pool);

      /* Loop over all other entries in this directory, skipping the
         "this dir" entry. */
      for (hi = apr_hash_first(pool, entries);
           hi;
           hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          const char *name;
          const char *full_path;
          const char *used_url = NULL;
          const char *name_uri = NULL;
          const char *this_cf_url = cf_url ? cf_url : copyfrom_url;
          svn_wc_adm_access_t *dir_access = adm_access;

          svn_pool_clear(loop_pool);

          /* Get the next entry.  Name is an entry name; value is an
             entry structure. */
          apr_hash_this(hi, &key, NULL, &val);
          name = key;
          
          /* Skip "this dir" */
          if (! strcmp(name, SVN_WC_ENTRY_THIS_DIR))
            continue;

          this_entry = val;
          name_uri = svn_path_uri_encode(name, loop_pool);

          full_path = svn_path_join(path, name, loop_pool);
          if (this_cf_url)
            this_cf_url = svn_path_join(this_cf_url, name_uri, loop_pool);

          /* We'll use the entry's URL if it has one and if we aren't
             in copy_mode, else, we'll just extend the parent's URL
             with the entry's basename.  */
          if ((! this_entry->url) || (copy_mode))
            used_url = svn_path_join(url, name_uri, loop_pool);

          /* Recurse. */
          if (this_entry->kind == svn_node_dir)
            {
              svn_error_t *lockerr;
              lockerr = svn_wc_adm_retrieve(&dir_access, adm_access,
                                            full_path, loop_pool);

              if (lockerr)
                {
                  if (lockerr->apr_err == SVN_ERR_WC_NOT_LOCKED)
                    {
                      /* A missing, schedule-delete child dir is
                         allowable.  Just don't try to recurse. */
                      svn_node_kind_t childkind;
                      svn_error_t *err = svn_io_check_path(full_path,
                                                           &childkind,
                                                           loop_pool);
                      if (! err
                          && childkind == svn_node_none
                          && this_entry->schedule == svn_wc_schedule_delete)
                        {
                          add_committable(committables, full_path,
                                          this_entry->kind, used_url,
                                          SVN_INVALID_REVNUM, 
                                          NULL,
                                          SVN_INVALID_REVNUM,
                                          SVN_CLIENT_COMMIT_ITEM_DELETE);
                          svn_error_clear(lockerr);
                          continue; /* don't recurse! */
                        }
                      else
                        {
                          svn_error_clear(err);
                          return lockerr;
                        }
                    }
                  else
                    return lockerr;
                }
            }
          else
            dir_access = adm_access;

          SVN_ERR(harvest_committables 
                  (committables, lock_tokens, full_path, dir_access,
                   used_url ? used_url : this_entry->url,
                   this_cf_url,
                   this_entry,
                   entry,
                   adds_only,
                   copy_mode,
                   FALSE, just_locked,
                   ctx,
                   loop_pool));
        }

      svn_pool_destroy(loop_pool);
    }

  /* Fetch lock tokens for descendants of deleted directories. */
  if (lock_tokens && entry->kind == svn_node_dir
      && (state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE))
    {
      SVN_ERR(svn_wc_walk_entries2(path, adm_access, &add_tokens_callbacks,
                                   lock_tokens, FALSE, ctx->cancel_func,
                                   ctx->cancel_baton, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__harvest_committables(apr_hash_t **committables,
                                 apr_hash_t **lock_tokens,
                                 svn_wc_adm_access_t *parent_dir,
                                 apr_array_header_t *targets,
                                 svn_boolean_t nonrecursive,
                                 svn_boolean_t just_locked,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *pool)
{
  int i = 0;
  svn_wc_adm_access_t *dir_access;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* It's possible that one of the named targets has a parent that is
   * itself scheduled for addition or replacement -- that is, the
   * parent is not yet versioned in the repository.  This is okay, as
   * long as the parent itself is part of this same commit, either
   * directly, or by virtue of a grandparent, great-grandparent, etc,
   * being part of the commit.
   *
   * Since we don't know what's included in the commit until we've
   * harvested all the targets, we can't reliably check this as we
   * go.  So in `danglers', we record named targets whose parents
   * are unversioned, then after harvesting the total commit group, we
   * check to make sure those parents are included.
   *
   * Each key of danglers is an unversioned parent.  The (const char *) 
   * value is one of that parent's children which is named as part of
   * the commit; the child is included only to make a better error
   * message.
   *
   * (The reason we don't bother to check unnamed -- i.e, implicit --
   * targets is that they can only join the commit if their parents
   * did too, so this situation can't arise for them.)
   */
  apr_hash_t *danglers = apr_hash_make(pool);

  /* Create the COMMITTABLES hash. */
  *committables = apr_hash_make(pool);

  /* And the LOCK_TOKENS dito. */
  *lock_tokens = apr_hash_make(pool);

  do
    {
      svn_wc_adm_access_t *adm_access;
      const svn_wc_entry_t *entry;
      const char *target;

      svn_pool_clear(subpool);
      /* Add the relative portion of our full path (if there are no
         relative paths, TARGET will just be PARENT_DIR for a single
         iteration. */
      target = svn_path_join_many(subpool, 
                                  svn_wc_adm_access_path(parent_dir),  
                                  targets->nelts 
                                  ? (((const char **) targets->elts)[i]) 
                                  : NULL,
                                  NULL);

      /* No entry?  This TARGET isn't even under version control! */
      SVN_ERR(svn_wc_adm_probe_retrieve(&adm_access, parent_dir,
                                        target, subpool));
      SVN_ERR(svn_wc_entry(&entry, target, adm_access, FALSE, subpool));
      if (! entry)
        return svn_error_createf
          (SVN_ERR_ENTRY_NOT_FOUND, NULL,
           _("'%s' is not under version control"), target);
      if (! entry->url)
        return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL, 
                                 _("Entry for '%s' has no URL"),
                                 svn_path_local_style(target, pool));

      /* We have to be especially careful around entries scheduled for
         addition or replacement. */
      if ((entry->schedule == svn_wc_schedule_add)
          || (entry->schedule == svn_wc_schedule_replace))
        {
          const char *parent, *base_name;
          svn_wc_adm_access_t *parent_access;
          const svn_wc_entry_t *p_entry = NULL;
          svn_error_t *err;

          svn_path_split(target, &parent, &base_name, subpool);
          err = svn_wc_adm_retrieve(&parent_access, parent_dir, 
                                    parent, subpool);
          if (err && err->apr_err == SVN_ERR_WC_NOT_LOCKED)
            {
              svn_error_clear(err);
              SVN_ERR(svn_wc_adm_open3(&parent_access, NULL, parent,
                                       FALSE, 0, ctx->cancel_func,
                                       ctx->cancel_baton, subpool));
            }
          else if (err)
            {
              return err;
            }

          SVN_ERR(svn_wc_entry(&p_entry, parent, parent_access, 
                               FALSE, subpool));
          if (! p_entry)
            return svn_error_createf 
              (SVN_ERR_WC_CORRUPT, NULL, 
               _("'%s' is scheduled for addition within unversioned parent"),
               svn_path_local_style(target, pool));
          if ((p_entry->schedule == svn_wc_schedule_add)
              || (p_entry->schedule == svn_wc_schedule_replace))
            {
              /* Copy the parent and target into pool; subpool
                 lasts only for this loop iteration, and we check
                 danglers after the loop is over. */
              apr_hash_set(danglers, apr_pstrdup(pool, parent),
                           APR_HASH_KEY_STRING, 
                           apr_pstrdup(pool, target));
            }
        }
      
      /* If this entry is marked as 'copied' but scheduled normally, then
         it should be the child of something else marked for addition with
         history. */
      if ((entry->copied) && (entry->schedule == svn_wc_schedule_normal))
        return svn_error_createf 
          (SVN_ERR_ILLEGAL_TARGET, NULL, 
           _("Entry for '%s' is marked as 'copied' but is not itself scheduled"
             "\nfor addition.  Perhaps you're committing a target that is\n"
             "inside an unversioned (or not-yet-versioned) directory?"),
           svn_path_local_style(target, pool));

      /* Handle our TARGET. */
      SVN_ERR(svn_wc_adm_retrieve(&dir_access, parent_dir,
                                  (entry->kind == svn_node_dir
                                   ? target
                                   : svn_path_dirname(target, subpool)),
                                  subpool));
      SVN_ERR(harvest_committables(*committables, *lock_tokens, target, dir_access,
                                   entry->url, NULL, entry, NULL, FALSE, 
                                   FALSE, nonrecursive, just_locked, ctx,
                                   subpool));

      i++;
    }
  while (i < targets->nelts);

  /* Make sure that every path in danglers is part of the commit. */
  {
    apr_hash_index_t *hi;

    for (hi = apr_hash_first(pool, danglers); hi; hi = apr_hash_next(hi))
      {
        const void *key;
        void *val;
        const char *dangling_parent, *dangling_child;

        /* Get the next entry.  Name is an entry name; value is an
           entry structure. */
        apr_hash_this(hi, &key, NULL, &val);
        dangling_parent = key;
        dangling_child = val;

        if (! look_up_committable(*committables, dangling_parent, pool))
          {
            return svn_error_createf 
              (SVN_ERR_ILLEGAL_TARGET, NULL, 
               _("'%s' is not under version control "
                 "and is not part of the commit, "
                 "yet its child '%s' is part of the commit"),
               /* Probably one or both of these is an entry, but
                  safest to local_stylize just in case. */
               svn_path_local_style(dangling_parent, pool),
               svn_path_local_style(dangling_child, pool));
          }
      }
  }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__get_copy_committables(apr_hash_t **committables,
                                  const char *new_url,
                                  const char *target,
                                  svn_wc_adm_access_t *adm_access,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;

  /* Create the COMMITTABLES hash. */
  *committables = apr_hash_make(pool);

  /* Read the entry for TARGET. */
  SVN_ERR(svn_wc_entry(&entry, target, adm_access, FALSE, pool));
  if (! entry)
    return svn_error_createf
      (SVN_ERR_ENTRY_NOT_FOUND, NULL, _("'%s' is not under version control"),
       svn_path_local_style(target, pool));
      
  /* Handle our TARGET. */
  SVN_ERR(harvest_committables(*committables, NULL, target,
                               adm_access, new_url, entry->url, entry, NULL,
                               FALSE, TRUE, FALSE, FALSE, ctx, pool));

  return SVN_NO_ERROR;
}


int svn_client__sort_commit_item_urls(const void *a, const void *b)
{
  const svn_client_commit_item2_t *item1
    = *((const svn_client_commit_item2_t * const *) a);
  const svn_client_commit_item2_t *item2
    = *((const svn_client_commit_item2_t * const *) b);
  return svn_path_compare_paths(item1->url, item2->url);
}



svn_error_t *
svn_client__condense_commit_items(const char **base_url,
                                  apr_array_header_t *commit_items,
                                  apr_pool_t *pool)
{
  apr_array_header_t *ci = commit_items; /* convenience */
  const char *url;
  svn_client_commit_item2_t *item, *last_item = NULL;
  int i;
  
  assert(ci && ci->nelts);

  /* Sort our commit items by their URLs. */
  qsort(ci->elts, ci->nelts, 
        ci->elt_size, svn_client__sort_commit_item_urls);

  /* Loop through the URLs, finding the longest usable ancestor common
     to all of them, and making sure there are no duplicate URLs.  */
  for (i = 0; i < ci->nelts; i++)
    {
      item = APR_ARRAY_IDX(ci, i, svn_client_commit_item2_t *);
      url = item->url;

      if ((last_item) && (strcmp(last_item->url, url) == 0))
        return svn_error_createf 
          (SVN_ERR_CLIENT_DUPLICATE_COMMIT_URL, NULL,
           _("Cannot commit both '%s' and '%s' as they refer to the same URL"),
           svn_path_local_style(item->path, pool),
           svn_path_local_style(last_item->path, pool));

      /* In the first iteration, our BASE_URL is just our only
         encountered commit URL to date.  After that, we find the
         longest ancestor between the current BASE_URL and the current
         commit URL.  */
      if (i == 0)
        *base_url = apr_pstrdup(pool, url);
      else
        *base_url = svn_path_get_longest_ancestor(*base_url, url, pool); 

      /* If our BASE_URL is itself a to-be-committed item, and it is
         anything other than an already-versioned directory with
         property mods, we'll call its parent directory URL the
         BASE_URL.  Why?  Because we can't have a file URL as our base
         -- period -- and all other directory operations (removal,
         addition, etc.) require that we open that directory's parent
         dir first.  */
      /* ### I don't understand the strlen()s here, hmmm.  -kff */
      if ((strlen(*base_url) == strlen(url))
          && (! ((item->kind == svn_node_dir)
                 && item->state_flags == SVN_CLIENT_COMMIT_ITEM_PROP_MODS)))
        *base_url = svn_path_dirname(*base_url, pool);

      /* Stash our item here for the next iteration. */
      last_item = item;
    }
  
  /* Now that we've settled on a *BASE_URL, go hack that base off
     of all of our URLs. */
  for (i = 0; i < ci->nelts; i++)
    {
      svn_client_commit_item2_t *this_item
        = APR_ARRAY_IDX(ci, i, svn_client_commit_item2_t *);
      int url_len = strlen(this_item->url);
      int base_url_len = strlen(*base_url);

      if (url_len > base_url_len)
        this_item->url = apr_pstrdup(pool, this_item->url + base_url_len + 1);
      else
        this_item->url = "";
    }

#ifdef SVN_CLIENT_COMMIT_DEBUG
  /* ### TEMPORARY CODE ### */
  fprintf(stderr, "COMMITTABLES: (base URL=%s)\n", *base_url);
  fprintf(stderr, "   FLAGS     REV  REL-URL (COPY-URL)\n");
  for (i = 0; i < ci->nelts; i++)
    {
      svn_client_commit_item2_t *this_item
        = APR_ARRAY_IDX(ci, i, svn_client_commit_item2_t *);
      char flags[6];
      flags[0] = (this_item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
                   ? 'a' : '-';
      flags[1] = (this_item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
                   ? 'd' : '-';
      flags[2] = (this_item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS)
                   ? 't' : '-';
      flags[3] = (this_item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
                   ? 'p' : '-';
      flags[4] = (this_item->state_flags & SVN_CLIENT_COMMIT_ITEM_IS_COPY)
                   ? 'c' : '-';
      flags[5] = '\0';
      fprintf(stderr, "   %s  %6ld  '%s' (%s)\n", 
              flags,
              this_item->revision,
              this_item->url ? this_item->url : "",
              this_item->copyfrom_url ? this_item->copyfrom_url : "none");
    }  
#endif /* SVN_CLIENT_COMMIT_DEBUG */

  return SVN_NO_ERROR;
}


struct file_mod_t
{
  svn_client_commit_item2_t *item;
  void *file_baton;
};


/* A baton for use with the path-based editor driver */
struct path_driver_cb_baton
{
  svn_wc_adm_access_t *adm_access;     /* top-level access baton */
  const svn_delta_editor_t *editor;    /* commit editor */
  void *edit_baton;                    /* commit editor's baton */
  apr_hash_t *file_mods;               /* hash: path->file_mod_t */
  apr_hash_t *tempfiles;               /* hash of tempfiles created */
  const char *notify_path_prefix;      /* notification path prefix */
  svn_client_ctx_t *ctx;               /* client context baton */
  apr_hash_t *commit_items;            /* the committables */
};


/* This implements svn_delta_path_driver_cb_func_t */
static svn_error_t *
do_item_commit(void **dir_baton,
               void *parent_baton,
               void *callback_baton,
               const char *path,
               apr_pool_t *pool)
{
  struct path_driver_cb_baton *cb_baton = callback_baton;
  svn_client_commit_item2_t *item = apr_hash_get(cb_baton->commit_items,
                                                 path, APR_HASH_KEY_STRING);
  svn_node_kind_t kind = item->kind;
  void *file_baton = NULL;
  const char *copyfrom_url = NULL;
  apr_pool_t *file_pool = NULL;
  svn_wc_adm_access_t *adm_access = cb_baton->adm_access;
  const svn_delta_editor_t *editor = cb_baton->editor;
  apr_hash_t *file_mods = cb_baton->file_mods;
  apr_hash_t *tempfiles = cb_baton->tempfiles;
  const char *notify_path_prefix = cb_baton->notify_path_prefix;
  svn_client_ctx_t *ctx = cb_baton->ctx;

  /* Do some initializations. */
  *dir_baton = NULL;
  if (item->copyfrom_url)
    copyfrom_url = item->copyfrom_url;

  /* If this is a file with textual mods, we'll be keeping its baton
     around until the end of the commit.  So just lump its memory into
     a single, big, all-the-file-batons-in-here pool.  Otherwise, we
     can just use POOL, and trust our caller to clean that mess up. */
  if ((kind == svn_node_file) 
      && (item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS))
    file_pool = apr_hash_pool_get(file_mods);
  else
    file_pool = pool;

  /* Call the cancellation function. */
  if (ctx->cancel_func)
    SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

  /* Validation. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_IS_COPY)
    {
      if (! copyfrom_url)
        return svn_error_createf 
          (SVN_ERR_BAD_URL, NULL,
           _("Commit item '%s' has copy flag but no copyfrom URL"),
           svn_path_local_style(path, pool));
      if (! SVN_IS_VALID_REVNUM(item->copyfrom_rev))
        return svn_error_createf 
          (SVN_ERR_CLIENT_BAD_REVISION, NULL,
           _("Commit item '%s' has copy flag but an invalid revision"),
           svn_path_local_style(path, pool));
    }

  /* If a feedback table was supplied by the application layer,
     describe what we're about to do to this item.  */
  if (ctx->notify_func2)
    {
      /* Convert an absolute path into a relative one (if possible.) */
      const char *npath = NULL;
      svn_wc_notify_t *notify;

      if (notify_path_prefix)
        {
          if (strcmp(notify_path_prefix, item->path))
            npath = svn_path_is_child(notify_path_prefix, item->path, pool);
          else
            npath = ".";
        }
      if (! npath)
        npath = item->path; /* Otherwise just use full path */

      if ((item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
          && (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD))
        {
          /* We don't print the "(bin)" notice for binary files when
             replacing, only when adding.  So we don't bother to get
             the mime-type here. */
          notify = svn_wc_create_notify(npath, svn_wc_notify_commit_replaced,
                                        pool);
        }
      else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
        {
          notify = svn_wc_create_notify(npath, svn_wc_notify_commit_deleted,
                                        pool);
        }
      else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
        {
          notify = svn_wc_create_notify(npath, svn_wc_notify_commit_added,
                                        pool);
          if (item->kind == svn_node_file)
            {
              const svn_string_t *propval;
              SVN_ERR(svn_wc_prop_get
                      (&propval, SVN_PROP_MIME_TYPE, item->path, adm_access,
                       pool));
              if (propval)
                notify->mime_type = propval->data;
            }
        }
      else if ((item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS)
               || (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS))
        {
          notify = svn_wc_create_notify(npath, svn_wc_notify_commit_modified,
                                        pool);
          if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS)
            notify->content_state = svn_wc_notify_state_changed;
          else
            notify->content_state = svn_wc_notify_state_unchanged;
          if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
            notify->prop_state = svn_wc_notify_state_changed;
          else
            notify->prop_state = svn_wc_notify_state_unchanged;
        }
      else
        notify = NULL;

      if (notify)
        {
          notify->kind = item->kind;
          (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
        }
    }

  /* If this item is supposed to be deleted, do so. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
    {
      assert(parent_baton);
      SVN_ERR(editor->delete_entry(path, item->revision, 
                                   parent_baton, pool));
    }

  /* If this item is supposed to be added, do so. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
    {
      if (kind == svn_node_file)
        {
          assert(parent_baton);
          SVN_ERR(editor->add_file 
                  (path, parent_baton, copyfrom_url, 
                   copyfrom_url ? item->copyfrom_rev : SVN_INVALID_REVNUM,
                   file_pool, &file_baton));
        }
      else
        {
          assert(parent_baton);
          SVN_ERR(editor->add_directory
                  (path, parent_baton, copyfrom_url,
                   copyfrom_url ? item->copyfrom_rev : SVN_INVALID_REVNUM,
                   pool, dir_baton));
        }
    }
    
  /* Now handle property mods. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
    {
      const char *tempfile;
      const svn_wc_entry_t *tmp_entry;

      if (kind == svn_node_file)
        {
          if (! file_baton)
            {
              assert(parent_baton);
              SVN_ERR(editor->open_file(path, parent_baton, 
                                        item->revision, 
                                        file_pool, &file_baton));
            }
        }
      else
        {
          if (! *dir_baton)
            {
              if (! parent_baton)
                {
                  SVN_ERR(editor->open_root
                          (cb_baton->edit_baton, item->revision,
                           pool, dir_baton));
                }
              else
                {
                  SVN_ERR(editor->open_directory
                          (path, parent_baton, item->revision, 
                           pool, dir_baton));
                }
            }
        }

      SVN_ERR(svn_wc_entry(&tmp_entry, item->path, adm_access, TRUE, pool));
      SVN_ERR(svn_wc_transmit_prop_deltas 
              (item->path, adm_access, tmp_entry, editor,
               (kind == svn_node_dir) ? *dir_baton : file_baton, 
               &tempfile, pool));
      if (tempfile && tempfiles)
        {
          tempfile = apr_pstrdup(apr_hash_pool_get(tempfiles), tempfile);
          apr_hash_set(tempfiles, tempfile, APR_HASH_KEY_STRING, (void *)1);
        }
    }

  /* Finally, handle text mods (in that we need to open a file if it
     hasn't already been opened, and we need to put the file baton in
     our FILES hash). */
  if ((kind == svn_node_file) 
      && (item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS))
    {
      struct file_mod_t *mod = apr_palloc(file_pool, sizeof(*mod));

      if (! file_baton)
        {
          assert(parent_baton);
          SVN_ERR(editor->open_file(path, parent_baton,
                                    item->revision,
                                    file_pool, &file_baton));
        }

      /* Add this file mod to the FILE_MODS hash. */
      mod->item = item;
      mod->file_baton = file_baton;
      apr_hash_set(file_mods, item->url, APR_HASH_KEY_STRING, mod);
    }
  else if (file_baton)
    {
      /* Close any outstanding file batons that didn't get caught by
         the "has local mods" conditional above. */
      SVN_ERR(editor->close_file(file_baton, NULL, file_pool));
    }

  return SVN_NO_ERROR;
}


#ifdef SVN_CLIENT_COMMIT_DEBUG
/* Prototype for function below */
static svn_error_t *get_test_editor(const svn_delta_editor_t **editor,
                                    void **edit_baton,
                                    const svn_delta_editor_t *real_editor,
                                    void *real_eb,
                                    const char *base_url,
                                    apr_pool_t *pool);
#endif /* SVN_CLIENT_COMMIT_DEBUG */

svn_error_t *
svn_client__do_commit(const char *base_url,
                      apr_array_header_t *commit_items,
                      svn_wc_adm_access_t *adm_access,
                      const svn_delta_editor_t *editor,
                      void *edit_baton,
                      const char *notify_path_prefix,
                      apr_hash_t **tempfiles,
                      apr_hash_t **digests,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  apr_hash_t *file_mods = apr_hash_make(pool);
  apr_hash_t *items_hash = apr_hash_make(pool);
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_index_t *hi;
  int i;
  struct path_driver_cb_baton cb_baton;
  apr_array_header_t *paths = 
    apr_array_make(pool, commit_items->nelts, sizeof(const char *));

#ifdef SVN_CLIENT_COMMIT_DEBUG
  {
    SVN_ERR(get_test_editor(&editor, &edit_baton, 
                            editor, edit_baton,
                            base_url, pool));
  }
#endif /* SVN_CLIENT_COMMIT_DEBUG */

  /* If the caller wants us to track temporary file creation, create a
     hash to store those paths in. */
  if (tempfiles)
    *tempfiles = apr_hash_make(pool);

  /* Ditto for the md5 digests. */
  if (digests)
    *digests = apr_hash_make(pool);

  /* Build a hash from our COMMIT_ITEMS array, keyed on the
     URI-decoded relative paths (which come from the item URLs).  And
     keep an array of those decoded paths, too.  */
  for (i = 0; i < commit_items->nelts; i++)
    {
      svn_client_commit_item2_t *item = 
        APR_ARRAY_IDX(commit_items, i, svn_client_commit_item2_t *);
      const char *path = svn_path_uri_decode(item->url, pool);
      apr_hash_set(items_hash, path, APR_HASH_KEY_STRING, item);
      APR_ARRAY_PUSH(paths, const char *) = path;
    }

  /* Setup the callback baton. */
  cb_baton.adm_access = adm_access;
  cb_baton.editor = editor;
  cb_baton.edit_baton = edit_baton;
  cb_baton.file_mods = file_mods;
  cb_baton.tempfiles = tempfiles ? *tempfiles : NULL;
  cb_baton.notify_path_prefix = notify_path_prefix;
  cb_baton.ctx = ctx;
  cb_baton.commit_items = items_hash;

  /* Drive the commit editor! */
  SVN_ERR(svn_delta_path_driver(editor, edit_baton, SVN_INVALID_REVNUM,
                                paths, do_item_commit, &cb_baton, pool));

  /* Transmit outstanding text deltas. */
  for (hi = apr_hash_first(pool, file_mods); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      struct file_mod_t *mod;
      svn_client_commit_item2_t *item;
      void *val;
      void *file_baton;
      const char *tempfile, *dir_path;
      unsigned char digest[APR_MD5_DIGESTSIZE];
      svn_boolean_t fulltext = FALSE;
      svn_wc_adm_access_t *item_access;
      
      svn_pool_clear(subpool);
      /* Get the next entry. */
      apr_hash_this(hi, &key, &klen, &val);
      mod = val;

      /* Transmit the entry. */
      item = mod->item;
      file_baton = mod->file_baton;

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      if (ctx->notify_func2)
        {
          svn_wc_notify_t *notify;
          const char *npath = NULL;

          if (notify_path_prefix)
            {
              if (strcmp(notify_path_prefix, item->path) != 0)
                npath = svn_path_is_child(notify_path_prefix, item->path,
                                          subpool);
              else
                npath = ".";
            }
          if (! npath)
            npath = item->path;
          notify = svn_wc_create_notify(npath,
                                        svn_wc_notify_commit_postfix_txdelta,
                                        subpool);
          notify->kind = svn_node_file;
          (*ctx->notify_func2)(ctx->notify_baton2, notify, subpool);
        }

      if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
        fulltext = TRUE;

      dir_path = svn_path_dirname(item->path, subpool);
      SVN_ERR(svn_wc_adm_retrieve(&item_access, adm_access, dir_path,
                                  subpool));
      SVN_ERR(svn_wc_transmit_text_deltas2(&tempfile, digest, item->path,
                                           item_access, fulltext, editor,
                                           file_baton, subpool));
      if (tempfile && *tempfiles)
        {
          tempfile = apr_pstrdup(apr_hash_pool_get(*tempfiles), tempfile);
          apr_hash_set(*tempfiles, tempfile, APR_HASH_KEY_STRING, (void *)1);
        }
      if (digests)
        {
          unsigned char *new_digest = apr_pmemdup(apr_hash_pool_get(*digests),
                                                  digest, APR_MD5_DIGESTSIZE);
          apr_hash_set(*digests, item->path, APR_HASH_KEY_STRING, new_digest);
        }
    }

  svn_pool_destroy(subpool);

  /* Close the edit. */
  SVN_ERR(editor->close_edit(edit_baton, pool));
  return SVN_NO_ERROR;
}

/* Commit callback baton */

struct commit_baton {
  svn_commit_info_t **info;
  apr_pool_t *pool;
};

svn_error_t *svn_client__commit_get_baton(void **baton,
                                          svn_commit_info_t **info,
                                          apr_pool_t *pool)
{
  struct commit_baton *cb = apr_pcalloc(pool, sizeof(*cb));
  cb->info = info;
  cb->pool = pool;
  *baton = cb;

  return SVN_NO_ERROR;
}

svn_error_t *svn_client__commit_callback(const svn_commit_info_t *commit_info,
                                         void *baton,
                                         apr_pool_t *pool)
{
  struct commit_baton *cb = baton;

  *(cb->info) = svn_commit_info_dup(commit_info, cb->pool);

  return SVN_NO_ERROR;
}


#ifdef SVN_CLIENT_COMMIT_DEBUG

/*** Temporary test editor ***/

struct edit_baton
{
  const char *path;

  const svn_delta_editor_t *real_editor;
  void *real_eb;
};

struct item_baton
{
  struct edit_baton *eb;
  void *real_baton;

  const char *path;
};

static struct item_baton *
make_baton(struct edit_baton *eb,
           void *real_baton,
           const char *path,
           apr_pool_t *pool)
{
  struct item_baton *new_baton = apr_pcalloc(pool, sizeof(*new_baton));
  new_baton->eb = eb;
  new_baton->real_baton = real_baton;
  new_baton->path = apr_pstrdup(pool, path);
  return new_baton;
}

static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  return (*eb->real_editor->set_target_revision)(eb->real_eb,
                                                 target_revision,
                                                 pool);
}

static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *dir_pool,
          void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct item_baton *new_baton = make_baton(eb, NULL, eb->path, dir_pool);
  fprintf(stderr, "TEST EDIT STARTED (base URL=%s)\n", eb->path);
  *root_baton = new_baton;
  return (*eb->real_editor->open_root)(eb->real_eb,
                                       base_revision,
                                       dir_pool,
                                       &new_baton->real_baton);
}

static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *pool,
         void **baton)
{
  struct item_baton *db = parent_baton;
  struct item_baton *new_baton = make_baton(db->eb, NULL, path, pool);
  const char *copystuffs = "";
  if (copyfrom_path && SVN_IS_VALID_REVNUM(copyfrom_revision))
    copystuffs = apr_psprintf(pool, 
                              " (copied from %s:%ld)",
                              copyfrom_path,
                              copyfrom_revision);
  fprintf(stderr, "   Adding  : %s%s\n", path, copystuffs);
  *baton = new_baton;
  return (*db->eb->real_editor->add_file)(path, db->real_baton,
                                          copyfrom_path, copyfrom_revision,
                                          pool, &new_baton->real_baton);
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct item_baton *db = parent_baton;
  fprintf(stderr, "   Deleting: %s\n", path);
  return (*db->eb->real_editor->delete_entry)(path, revision,
                                              db->real_baton, pool);
}

static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **baton)
{
  struct item_baton *db = parent_baton;
  struct item_baton *new_baton = make_baton(db->eb, NULL, path, pool);
  fprintf(stderr, "   Opening : %s\n", path);
  *baton = new_baton;
  return (*db->eb->real_editor->open_file)(path, db->real_baton,
                                           base_revision, pool,
                                           &new_baton->real_baton);
}

static svn_error_t *
close_file(void *baton, const char *text_checksum, apr_pool_t *pool)
{
  struct item_baton *fb = baton;
  fprintf(stderr, "   Closing : %s\n", fb->path);
  return (*fb->eb->real_editor->close_file)(fb->real_baton,
                                            text_checksum, pool);
}


static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct item_baton *fb = file_baton;
  fprintf(stderr, "      PropSet (%s=%s)\n", name, value ? value->data : "");
  return (*fb->eb->real_editor->change_file_prop)(fb->real_baton,
                                                  name, value, pool);
}

static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct item_baton *fb = file_baton;
  fprintf(stderr, "      Transmitting text...\n");
  return (*fb->eb->real_editor->apply_textdelta)(fb->real_baton,
                                                 base_checksum, pool,
                                                 handler, handler_baton);
}

static svn_error_t *
close_edit(void *edit_baton, apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  fprintf(stderr, "TEST EDIT COMPLETED\n");
  return (*eb->real_editor->close_edit)(eb->real_eb, pool);
}

static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *pool,
              void **baton)
{
  struct item_baton *db = parent_baton;
  struct item_baton *new_baton = make_baton(db->eb, NULL, path, pool);
  const char *copystuffs = "";
  if (copyfrom_path && SVN_IS_VALID_REVNUM(copyfrom_revision))
    copystuffs = apr_psprintf(pool, 
                              " (copied from %s:%ld)",
                              copyfrom_path,
                              copyfrom_revision);
  fprintf(stderr, "   Adding  : %s%s\n", path, copystuffs);
  *baton = new_baton;
  return (*db->eb->real_editor->add_directory)(path,
                                               db->real_baton,
                                               copyfrom_path,
                                               copyfrom_revision,
                                               pool,
                                               &new_baton->real_baton);
}

static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **baton)
{
  struct item_baton *db = parent_baton;
  struct item_baton *new_baton = make_baton(db->eb, NULL, path, pool);
  fprintf(stderr, "   Opening : %s\n", path);
  *baton = new_baton;
  return (*db->eb->real_editor->open_directory)(path, db->real_baton,
                                                base_revision, pool,
                                                &new_baton->real_baton);
}

static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct item_baton *db = dir_baton;
  fprintf(stderr, "      PropSet (%s=%s)\n", name, value ? value->data : "");
  return (*db->eb->real_editor->change_dir_prop)(db->real_baton,
                                                 name, value, pool);
}

static svn_error_t *
close_directory(void *baton, apr_pool_t *pool)
{
  struct item_baton *db = baton;
  fprintf(stderr, "   Closing : %s\n", db->path);
  return (*db->eb->real_editor->close_directory)(db->real_baton, pool);
}

static svn_error_t *
abort_edit(void *edit_baton, apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  fprintf(stderr, "TEST EDIT ABORTED\n");
  return (*eb->real_editor->abort_edit)(eb->real_eb, pool);
}

static svn_error_t *
get_test_editor(const svn_delta_editor_t **editor,
                void **edit_baton,
                const svn_delta_editor_t *real_editor,
                void *real_eb,
                const char *base_url,
                apr_pool_t *pool)
{
  svn_delta_editor_t *ed = svn_delta_default_editor(pool);
  struct edit_baton *eb = apr_pcalloc(pool, sizeof(*eb));

  eb->path = apr_pstrdup(pool, base_url);
  eb->real_editor = real_editor;
  eb->real_eb = real_eb;

  /* We don't implement absent_file() or absent_directory() in this
     editor, because presumably commit would never send that. */
  ed->set_target_revision = set_target_revision;
  ed->open_root = open_root;
  ed->add_directory = add_directory;
  ed->open_directory = open_directory;
  ed->close_directory = close_directory;
  ed->add_file = add_file;
  ed->open_file = open_file;
  ed->close_file = close_file;
  ed->delete_entry = delete_entry;
  ed->apply_textdelta = apply_textdelta;
  ed->change_dir_prop = change_dir_prop;
  ed->change_file_prop = change_file_prop;
  ed->close_edit = close_edit;
  ed->abort_edit = abort_edit;

  *editor = ed;
  *edit_baton = eb;
  return SVN_NO_ERROR;
}
#endif /* SVN_CLIENT_COMMIT_DEBUG */

svn_error_t * svn_client__get_log_msg(const char **log_msg,
                                      const char **tmp_file,
                                      const apr_array_header_t *commit_items,
                                      svn_client_ctx_t *ctx,
                                      apr_pool_t *pool)
{
    /* client provided new callback function. simply forward call to him */
    if (ctx->log_msg_func2)
      return (*ctx->log_msg_func2)(log_msg, tmp_file, commit_items,
                                   ctx->log_msg_baton2, pool);

    /* client want use old (pre 1.3) API, therefore build
     * svn_client_commit_item_t array */

    if (ctx->log_msg_func)
      {
        int i;
        svn_error_t * err;
        svn_client_commit_item_t *old_item;
        apr_pool_t * subpool = svn_pool_create(pool);
        apr_array_header_t *old_commit_items
          = apr_array_make(subpool, commit_items->nelts, sizeof(old_item));

        for (i = 0; i < commit_items->nelts; i++)
          {
            svn_client_commit_item2_t *item =
              APR_ARRAY_IDX(commit_items, i, svn_client_commit_item2_t *);

            old_item = apr_pcalloc(subpool, sizeof(*old_item));
            old_item->path = item->path;
            old_item->kind = item->kind;
            old_item->url = item->url;
            /* pre 1.3 API use revision field for copyfrom_rev and revision
             * depeding of copyfrom_url */
            old_item->revision = item->copyfrom_url ?
              item->copyfrom_rev : item->revision;
            old_item->copyfrom_url = item->copyfrom_url;
            old_item->state_flags = item->state_flags;
            old_item->wcprop_changes = item->wcprop_changes;

            APR_ARRAY_PUSH(old_commit_items, svn_client_commit_item_t *)
              = old_item;
          }

        err = (*ctx->log_msg_func)(log_msg, tmp_file, old_commit_items,
          ctx->log_msg_baton, pool);

        svn_pool_destroy(subpool);
        return err;
      }

    *log_msg = "";
    *tmp_file = NULL;

    return SVN_NO_ERROR;
}
