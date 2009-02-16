/*
 * commit_util.c:  Driver for the WC commit process.
 *
 * ====================================================================
 * Copyright (c) 2000-2008 CollabNet.  All rights reserved.
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
#include <apr_md5.h>

#include "client.h"
#include "svn_path.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_iter.h"
#include "svn_hash.h"

#include <assert.h>
#include <stdlib.h>  /* for qsort() */

#include "svn_private_config.h"
#include "private/svn_wc_private.h"

/*** Uncomment this to turn on commit driver debugging. ***/
/*
#define SVN_CLIENT_COMMIT_DEBUG
*/

/* Wrap an RA error in an out-of-date error if warranted. */
static svn_error_t *
fixup_out_of_date_error(const char *path,
                        svn_node_kind_t kind,
                        svn_error_t *err)
{
  if (err->apr_err == SVN_ERR_FS_NOT_FOUND
      || err->apr_err == SVN_ERR_RA_DAV_PATH_NOT_FOUND)
    return  svn_error_createf(SVN_ERR_WC_NOT_UP_TO_DATE, err,
                              (kind == svn_node_dir
                               ? _("Directory '%s' is out of date")
                               : _("File '%s' is out of date")),
                              path);
  else
    return err;
}


/*** Harvesting Commit Candidates ***/


/* Add a new commit candidate (described by all parameters except
   `COMMITTABLES') to the COMMITTABLES hash.  All of the commit item's
   members are allocated out of the COMMITTABLES hash pool. */
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
  svn_client_commit_item3_t *new_item;

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
  new_item = svn_client_commit_item3_create(pool);
  new_item->path           = apr_pstrdup(pool, path);
  new_item->kind           = kind;
  new_item->url            = apr_pstrdup(pool, url);
  new_item->revision       = revision;
  new_item->copyfrom_url   = copyfrom_url
    ? apr_pstrdup(pool, copyfrom_url) : NULL;
  new_item->copyfrom_rev   = copyfrom_rev;
  new_item->state_flags    = state_flags;
  new_item->incoming_prop_changes = apr_array_make(pool, 1,
                                                   sizeof(svn_prop_t *));

  /* Now, add the commit item to the array. */
  APR_ARRAY_PUSH(array, svn_client_commit_item3_t *) = new_item;
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
static svn_client_commit_item3_t *
look_up_committable(apr_hash_t *committables,
                    const char *path,
                    apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, committables); hi; hi = apr_hash_next(hi))
    {
      void *val;
      apr_array_header_t *these_committables;
      int i;

      apr_hash_this(hi, NULL, NULL, &val);
      these_committables = val;

      for (i = 0; i < these_committables->nelts; i++)
        {
          svn_client_commit_item3_t *this_committable
            = APR_ARRAY_IDX(these_committables, i,
                            svn_client_commit_item3_t *);

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
static svn_wc_entry_callbacks2_t add_tokens_callbacks = {
  add_lock_token,
  svn_client__default_walker_error_handler
};


/* Helper for harvest_committables().
 * If ENTRY is a dir, return an SVN_ERR_WC_FOUND_CONFLICT error when
 * encountering a tree-conflicted immediate child node. However, do
 * not consider immediate children that are outside the bounds of DEPTH.
 *
 * PATH, ENTRY, ADM_ACCESS, DEPTH, CHANGELISTS and POOL are the same ones
 * originally received by harvest_committables().
 *
 * Tree-conflicts information is stored in the victim's immediate parent.
 * In some cases of an absent tree-conflicted victim, the tree-conflict
 * information in its parent dir is the only indication that the node
 * is under version control. This function is necessary for this
 * particular case. In all other cases, this simply bails out a little
 * bit earlier.
 *
 * Note: Tree-conflicts info can only be found in "THIS_DIR" entries. */
static svn_error_t *
bail_on_tree_conflicted_children(const char *path,
                                 const svn_wc_entry_t *entry,
                                 svn_wc_adm_access_t *adm_access,
                                 svn_depth_t depth,
                                 apr_hash_t *changelists,
                                 apr_pool_t *pool)
{
  apr_array_header_t *conflicts;
  int i;

  if ((depth == svn_depth_empty)
      || (entry->kind != svn_node_dir)
      || (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) != 0))
    /* There can't possibly be tree-conflicts information here. */
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__read_tree_conflicts(&conflicts, entry->tree_conflict_data,
                                      path, pool));

  for (i = 0; i < conflicts->nelts; i++)
    {
      const svn_wc_conflict_description_t *conflict;
      svn_wc_adm_access_t *child_adm_access;
      const svn_wc_entry_t *child_entry = NULL;

      conflict = APR_ARRAY_IDX(conflicts, i,
                               svn_wc_conflict_description_t *);

      if ((conflict->node_kind == svn_node_dir) &&
          (depth == svn_depth_files))
        continue;

      /* So we've encountered a conflict that is included in DEPTH.
       * Bail out. */

      /* Get the child's entry, if it has one, else NULL. */
      if (conflict->node_kind == svn_node_dir)
        {
          svn_error_t *err = svn_wc_adm_retrieve(
                               &child_adm_access, adm_access,
                               conflict->path, pool);
          if (err
              && (svn_error_root_cause(err)->apr_err
                  == SVN_ERR_WC_PATH_NOT_FOUND))
            {
              svn_error_clear(err);
              err = 0;
              child_adm_access = NULL;
            }
          SVN_ERR(err);
        }
      else
        child_adm_access = adm_access;

      if (child_adm_access != NULL)
        SVN_ERR(svn_wc_entry(&child_entry, conflict->path,
                             child_adm_access, TRUE, pool));

      /* If changelists are used but there is no entry, no changelist
       * can possibly match. */
      /* TODO: Currently, there is no way to set a changelist name on
       * a tree-conflict victim that has no entry (e.g. locally
       * deleted). */
      if ((changelists == NULL)
          || ((child_entry != NULL)
              && SVN_WC__CL_MATCH(changelists, child_entry)))
        return svn_error_createf(
                 SVN_ERR_WC_FOUND_CONFLICT, NULL,
                 _("Aborting commit: '%s' remains in conflict"),
                 svn_path_local_style(conflict->path, pool));
    }

  return SVN_NO_ERROR;
}

/* Helper function for svn_client__harvest_committables().
 * Determine whether we are within a tree-conflicted subtree of the
 * working copy and return an SVN_ERR_WC_FOUND_CONFLICT error if so.
 * Step outward through the parent directories up to the working copy
 * root, obtaining read locks temporarily. */
static svn_error_t *
bail_on_tree_conflicted_ancestor(svn_wc_adm_access_t *first_ancestor,
                                 apr_pool_t *scratch_pool)
{
  const char *path;
  const char *parent_path;
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t wc_root;
  svn_boolean_t tree_conflicted;

  path = svn_wc_adm_access_path(first_ancestor);
  adm_access = first_ancestor;

  while(1)
    {
      /* Here, ADM_ACCESS refers to PATH. */
      svn_wc__strictly_is_wc_root(&wc_root,
                                  path,
                                  adm_access,
                                  scratch_pool);

      if (adm_access != first_ancestor)
        svn_wc_adm_close2(adm_access, scratch_pool);

      if (wc_root)
        break;

      /* Check the parent directory's entry for tree-conflicts
       * on PATH. */
      parent_path = svn_path_dirname(path, scratch_pool);
      SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, parent_path,
                               FALSE,  /* Write lock */
                               0, /* lock levels */
                               NULL, NULL,
                               scratch_pool));
      /* Now, ADM_ACCESS refers to PARENT_PATH. */

      svn_wc_conflicted_p2(NULL, NULL, &tree_conflicted,
                           path, adm_access, scratch_pool);

      if (tree_conflicted)
        return svn_error_createf(
                 SVN_ERR_WC_FOUND_CONFLICT, NULL,
                 _("Aborting commit: '%s' remains in tree-conflict"),
                 svn_path_local_style(path, scratch_pool));

      /* Step outwards */
      path = parent_path;
      /* And again, ADM_ACCESS refers to PATH. */
    }

  return SVN_NO_ERROR;
}


/* Recursively search for commit candidates in (and under) PATH (with
   entry ENTRY and ancestry URL), and add those candidates to
   COMMITTABLES.  If in ADDS_ONLY modes, only new additions are
   recognized.  COPYFROM_URL is the default copyfrom-url for children
   of copied directories.

   DEPTH indicates how to treat files and subdirectories of PATH when
   PATH is itself a directory; see svn_client__harvest_committables()
   for its behavior.

   Lock tokens of candidates will be added to LOCK_TOKENS, if
   non-NULL.  JUST_LOCKED indicates whether to treat non-modified items with
   lock tokens as commit candidates.

   If in COPY_MODE, treat the entry as if it is destined to be added
   with history as URL, and add 'deleted' entries to COMMITTABLES as
   items to delete in the copy destination.

   If CHANGELISTS is non-NULL, it is a hash whose keys are const char *
   changelist names used as a restrictive filter
   when harvesting committables; that is, don't add a path to
   COMMITTABLES unless it's a member of one of those changelists.

   If CTX->CANCEL_FUNC is non-null, call it with CTX->CANCEL_BATON to see
   if the user has cancelled the operation.

   Any items added to COMMITTABLES are allocated from the COMITTABLES
   hash pool, not POOL.  SCRATCH_POOL is used for temporary allocations. */
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
                     svn_depth_t depth,
                     svn_boolean_t just_locked,
                     apr_hash_t *changelists,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *scratch_pool)
{
  apr_hash_t *entries = NULL;
  svn_boolean_t text_mod = FALSE, prop_mod = FALSE;
  apr_byte_t state_flags = 0;
  svn_node_kind_t kind;
  const char *p_path;
  svn_boolean_t tc, pc, treec;
  const char *cf_url = NULL;
  svn_revnum_t cf_rev = entry->copyfrom_rev;
  const svn_string_t *propval;
  svn_boolean_t is_special;
  apr_pool_t *token_pool = (lock_tokens ? apr_hash_pool_get(lock_tokens)
                            : NULL);

  /* Early out if the item is already marked as committable. */
  if (look_up_committable(committables, path, scratch_pool))
    return SVN_NO_ERROR;

  SVN_ERR_ASSERT(entry);
  SVN_ERR_ASSERT(url);

  if (ctx->cancel_func)
    SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

  /* Make P_PATH the parent dir. */
  p_path = svn_path_dirname(path, scratch_pool);

  /* Return error on unknown path kinds.  We check both the entry and
     the node itself, since a path might have changed kind since its
     entry was written. */
  if ((entry->kind != svn_node_file) && (entry->kind != svn_node_dir))
    return svn_error_createf
      (SVN_ERR_NODE_UNKNOWN_KIND, NULL, _("Unknown entry kind for '%s'"),
       svn_path_local_style(path, scratch_pool));

  SVN_ERR(svn_io_check_special_path(path, &kind, &is_special, scratch_pool));

  if ((kind != svn_node_file)
      && (kind != svn_node_dir)
      && (kind != svn_node_none))
    {
      return svn_error_createf
        (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
         _("Unknown entry kind for '%s'"),
         svn_path_local_style(path, scratch_pool));
    }

  /* Verify that the node's type has not changed before attempting to
     commit. */
  SVN_ERR(svn_wc_prop_get(&propval, SVN_PROP_SPECIAL, path, adm_access,
                          scratch_pool));

  if ((((! propval) && (is_special))
#ifdef HAVE_SYMLINK
       || ((propval) && (! is_special))
#endif /* HAVE_SYMLINK */
       ) && (kind != svn_node_none))
    {
      return svn_error_createf
        (SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
         _("Entry '%s' has unexpectedly changed special status"),
         svn_path_local_style(path, scratch_pool));
    }

  if (entry->kind == svn_node_dir)
    {
      /* Read the dir's own entries for use when recursing. */
      svn_error_t *err;
      const svn_wc_entry_t *e = NULL;
      err = svn_wc_entries_read(&entries, adm_access, copy_mode, scratch_pool);

      /* If we failed to get an entries hash for the directory, no
         sweat.  Cleanup and move along.  */
      if (err)
        {
          svn_error_clear(err);
          entries = NULL;
        }

      /* If we got an entries hash, and the "this dir" entry is
         present, try to override our current ENTRY with it. */
      if ((entries) && ((e = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR,
                                          APR_HASH_KEY_STRING))))
          entry = e;
    }

  SVN_ERR(svn_wc_conflicted_p2(&tc, &pc, &treec, path, adm_access,
                               scratch_pool));

  /* Bail now if any conflicts exist for the ENTRY. */
  if (tc || pc || treec)
    {
      /* Paths in conflict which are not part of our changelist should
         be ignored. */
      if (SVN_WC__CL_MATCH(changelists, entry))
        return svn_error_createf(SVN_ERR_WC_FOUND_CONFLICT, NULL,
                                 _("Aborting commit: '%s' remains in conflict"),
                                 svn_path_local_style(path, scratch_pool));
    }

  SVN_ERR(bail_on_tree_conflicted_children(path, entry, adm_access, depth,
                                           changelists, scratch_pool));

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
         admissible for comparative purposes. */
      SVN_ERR(svn_wc_is_wc_root(&wc_root, path, adm_access, scratch_pool));
      if (! wc_root)
        {
          if (parent_entry)
            p_rev = parent_entry->revision;
        }
      else if (! copy_mode)
        return svn_error_createf
          (SVN_ERR_WC_CORRUPT, NULL,
           _("Did not expect '%s' to be a working copy root"),
           svn_path_local_style(path, scratch_pool));

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
               svn_path_local_style(path, scratch_pool));
        }
    }

  /* If an add is scheduled to occur, dig around for some more
     information about it. */
  if (state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
    {
      svn_node_kind_t working_kind;
      svn_boolean_t eol_prop_changed;

      /* First of all, the working file or directory must exist.
         See issue #3198. */
      SVN_ERR(svn_io_check_path(path, &working_kind, scratch_pool));
      if (working_kind == svn_node_none)
        {
          return svn_error_createf
            (SVN_ERR_WC_PATH_NOT_FOUND, NULL,
             _("'%s' is scheduled for addition, but is missing"),
             svn_path_local_style(path, scratch_pool));
        }

      /* See if there are property modifications to send. */
      SVN_ERR(check_prop_mods(&prop_mod, &eol_prop_changed, path,
                              adm_access, scratch_pool));

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
                                           eol_prop_changed,
                                           adm_access, scratch_pool));
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
                              adm_access, scratch_pool));

      /* Check for text mods on files.  If EOL_PROP_CHANGED is TRUE,
         then we need to force a translated byte-for-byte comparison
         against the text-base so that a timestamp comparison won't
         bail out early.  Depending on how the svn:eol-style prop was
         changed, we might have to send new text to the server to
         match the new newline style.  */
      if (entry->kind == svn_node_file)
        SVN_ERR(svn_wc_text_modified_p(&text_mod, path, eol_prop_changed,
                                       adm_access, scratch_pool));
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
      if (SVN_WC__CL_MATCH(changelists, entry))
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
    }

  /* For directories, recursively handle each entry according to depth
     (except when the directory is being deleted, unless the deletion
     is part of a replacement ... how confusing).  */
  if (entries && (depth > svn_depth_empty)
      && ((! (state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE))
          || (state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)))
    {
      apr_hash_index_t *hi;
      const svn_wc_entry_t *this_entry;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);

      /* Loop over all other entries in this directory, skipping the
         "this dir" entry. */
      for (hi = apr_hash_first(scratch_pool, entries);
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

          svn_pool_clear(iterpool);

          /* Get the next entry.  Name is an entry name; value is an
             entry structure. */
          apr_hash_this(hi, &key, NULL, &val);
          name = key;

          /* Skip "this dir" */
          if (! strcmp(name, SVN_WC_ENTRY_THIS_DIR))
            continue;

          this_entry = val;

          /* Skip the excluded item. */
          if (this_entry->depth == svn_depth_exclude)
            continue;

          name_uri = svn_path_uri_encode(name, iterpool);

          full_path = svn_path_join(path, name, iterpool);
          if (this_cf_url)
            this_cf_url = svn_path_join(this_cf_url, name_uri, iterpool);

          /* We'll use the entry's URL if it has one and if we aren't
             in copy_mode, else, we'll just extend the parent's URL
             with the entry's basename.  */
          if ((! this_entry->url) || (copy_mode))
            used_url = svn_path_join(url, name_uri, iterpool);

          /* Recurse. */
          if (this_entry->kind == svn_node_dir)
            {
              if (depth <= svn_depth_files)
                {
                  /* Don't bother with any of this if it's a directory
                     and depth says not to go there. */
                  continue;
                }
              else
                {
                  svn_error_t *lockerr;
                  lockerr = svn_wc_adm_retrieve(&dir_access, adm_access,
                                                full_path, iterpool);

                  if (lockerr)
                    {
                      if (lockerr->apr_err == SVN_ERR_WC_NOT_LOCKED)
                        {
                          /* A missing, schedule-delete child dir is
                             allowable.  Just don't try to recurse. */
                          svn_node_kind_t childkind;
                          svn_error_t *err = svn_io_check_path(full_path,
                                                               &childkind,
                                                               iterpool);
                          if (! err
                              && (childkind == svn_node_none)
                              && (this_entry->schedule
                                  == svn_wc_schedule_delete))
                            {
                              if (SVN_WC__CL_MATCH(changelists, entry))
                                {
                                  add_committable(
                                    committables, full_path,
                                    this_entry->kind, used_url,
                                    SVN_INVALID_REVNUM,
                                    NULL,
                                    SVN_INVALID_REVNUM,
                                    SVN_CLIENT_COMMIT_ITEM_DELETE);
                                  svn_error_clear(lockerr);
                                  continue; /* don't recurse! */
                                }
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
            }
          else
            {
              dir_access = adm_access;
            }

          {
            svn_depth_t depth_below_here = depth;

            /* If depth is svn_depth_files, then we must be recursing
               into a file, or else we wouldn't be here -- either way,
               svn_depth_empty is the right depth to use.  On the
               other hand, if depth is svn_depth_immediates, then we
               could be recursing into a directory or a file -- in
               which case svn_depth_empty is *still* the right depth
               to use.  I know that sounds bizarre (one normally
               expects to just do depth - 1) but it's correct. */
            if (depth == svn_depth_immediates
                || depth == svn_depth_files)
              depth_below_here = svn_depth_empty;

            SVN_ERR(harvest_committables
                    (committables, lock_tokens, full_path, dir_access,
                     used_url ? used_url : this_entry->url,
                     this_cf_url,
                     this_entry,
                     entry,
                     adds_only,
                     copy_mode,
                     depth_below_here,
                     just_locked,
                     changelists,
                     ctx,
                     iterpool));
          }
        }

      svn_pool_destroy(iterpool);
    }

  /* Fetch lock tokens for descendants of deleted directories. */
  if (lock_tokens && entry->kind == svn_node_dir
      && (state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE))
    {
      SVN_ERR(svn_wc_walk_entries3(path, adm_access, &add_tokens_callbacks,
                                   lock_tokens,
                                   /* If a directory was deleted, everything
                                      under it would better be deleted too,
                                      so pass svn_depth_infinity not depth. */
                                   svn_depth_infinity, FALSE,
                                   ctx->cancel_func, ctx->cancel_baton,
                                   scratch_pool));
    }

  return SVN_NO_ERROR;
}


/* BATON is an apr_hash_t * of harvested committables. */
static svn_error_t *
validate_dangler(void *baton,
                 const void *key, apr_ssize_t klen, void *val,
                 apr_pool_t *pool)
{
  const char *dangling_parent = key;
  const char *dangling_child = val;

  /* The baton points to the committables hash */
  if (! look_up_committable(baton, dangling_parent, pool))
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

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__harvest_committables(apr_hash_t **committables,
                                 apr_hash_t **lock_tokens,
                                 svn_wc_adm_access_t *parent_dir,
                                 apr_array_header_t *targets,
                                 svn_depth_t depth,
                                 svn_boolean_t just_locked,
                                 const apr_array_header_t *changelists,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *pool)
{
  int i = 0;
  svn_wc_adm_access_t *dir_access;
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *changelist_hash = NULL;

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

  /* If we have a list of changelists, convert that into a hash with
     changelist keys. */
  if (changelists && changelists->nelts)
    SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelists, pool));

  do
    {
      svn_wc_adm_access_t *adm_access;
      const svn_wc_entry_t *entry;
      const char *target;
      svn_error_t *err;

      svn_pool_clear(subpool);
      /* Add the relative portion of our full path (if there are no
         relative paths, TARGET will just be PARENT_DIR for a single
         iteration. */
      target = svn_path_join_many(subpool,
                                  svn_wc_adm_access_path(parent_dir),
                                  targets->nelts
                                  ? APR_ARRAY_IDX(targets, i, const char *)
                                  : NULL,
                                  NULL);

      /* No entry?  This TARGET isn't even under version control! */
      SVN_ERR(svn_wc_adm_probe_retrieve(&adm_access, parent_dir,
                                        target, subpool));

      err = svn_wc__entry_versioned(&entry, target, adm_access, FALSE,
                                    subpool);
      /* If a target of the commit is a tree-conflicted node that
       * has no entry (e.g. locally deleted), issue a proper tree-
       * conflicts error instead of a "not under version control". */
      if (err && (err->apr_err == SVN_ERR_ENTRY_NOT_FOUND))
        {
          svn_wc_conflict_description_t *conflict = NULL;
          svn_wc__get_tree_conflict(&conflict, target, adm_access, pool);
          if (conflict != NULL)
            {
              svn_error_clear(err);
              return svn_error_createf(
                       SVN_ERR_WC_FOUND_CONFLICT, NULL,
                       _("Aborting commit: '%s' remains in conflict"),
                       svn_path_local_style(conflict->path, pool));
            }
        }
      SVN_ERR(err);

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

      /* Make sure this isn't inside a working copy subtree that is
       * marked as tree-conflicted. */
      SVN_ERR(bail_on_tree_conflicted_ancestor(dir_access, subpool));

      SVN_ERR(harvest_committables(*committables, *lock_tokens, target,
                                   dir_access, entry->url, NULL,
                                   entry, NULL, FALSE, FALSE, depth,
                                   just_locked, changelist_hash,
                                   ctx, subpool));

      i++;
    }
  while (i < targets->nelts);

  /* Make sure that every path in danglers is part of the commit. */
  SVN_ERR(svn_iter_apr_hash(NULL,
                            danglers, validate_dangler, *committables, pool));

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

struct copy_committables_baton
{
  svn_wc_adm_access_t *adm_access;
  svn_client_ctx_t *ctx;
  apr_hash_t *committables;
};

static svn_error_t *
harvest_copy_committables(void *baton, void *item, apr_pool_t *pool)
{
  struct copy_committables_baton *btn = baton;

  const svn_wc_entry_t *entry;
  svn_client__copy_pair_t *pair =
    *(svn_client__copy_pair_t **)item;
  svn_wc_adm_access_t *dir_access;

  /* Read the entry for this SRC. */
  SVN_ERR(svn_wc__entry_versioned(&entry, pair->src, btn->adm_access, FALSE,
                                  pool));

  /* Get the right access baton for this SRC. */
  if (entry->kind == svn_node_dir)
    SVN_ERR(svn_wc_adm_retrieve(&dir_access, btn->adm_access, pair->src, pool));
  else
    SVN_ERR(svn_wc_adm_retrieve(&dir_access, btn->adm_access,
                                svn_path_dirname(pair->src, pool),
                                pool));

  /* Handle this SRC.  Because add_committable() uses the hash pool to
     allocate the new commit_item, we can safely use the iterpool here. */
  return harvest_committables(btn->committables, NULL, pair->src,
                              dir_access, pair->dst, entry->url, entry,
                              NULL, FALSE, TRUE, svn_depth_infinity,
                              FALSE, NULL, btn->ctx, pool);
}



svn_error_t *
svn_client__get_copy_committables(apr_hash_t **committables,
                                  const apr_array_header_t *copy_pairs,
                                  svn_wc_adm_access_t *adm_access,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *pool)
{
  struct copy_committables_baton btn;

  *committables = apr_hash_make(pool);

  btn.adm_access = adm_access;
  btn.ctx = ctx;
  btn.committables = *committables;

  /* For each copy pair, harvest the committables for that pair into the
     committables hash. */
  return svn_iter_apr_array(NULL, copy_pairs,
                            harvest_copy_committables, &btn, pool);
}


int svn_client__sort_commit_item_urls(const void *a, const void *b)
{
  const svn_client_commit_item3_t *item1
    = *((const svn_client_commit_item3_t * const *) a);
  const svn_client_commit_item3_t *item2
    = *((const svn_client_commit_item3_t * const *) b);
  return svn_path_compare_paths(item1->url, item2->url);
}



svn_error_t *
svn_client__condense_commit_items(const char **base_url,
                                  apr_array_header_t *commit_items,
                                  apr_pool_t *pool)
{
  apr_array_header_t *ci = commit_items; /* convenience */
  const char *url;
  svn_client_commit_item3_t *item, *last_item = NULL;
  int i;

  SVN_ERR_ASSERT(ci && ci->nelts);

  /* Sort our commit items by their URLs. */
  qsort(ci->elts, ci->nelts,
        ci->elt_size, svn_client__sort_commit_item_urls);

  /* Loop through the URLs, finding the longest usable ancestor common
     to all of them, and making sure there are no duplicate URLs.  */
  for (i = 0; i < ci->nelts; i++)
    {
      item = APR_ARRAY_IDX(ci, i, svn_client_commit_item3_t *);
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
      svn_client_commit_item3_t *this_item
        = APR_ARRAY_IDX(ci, i, svn_client_commit_item3_t *);
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
      svn_client_commit_item3_t *this_item
        = APR_ARRAY_IDX(ci, i, svn_client_commit_item3_t *);
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
  svn_client_commit_item3_t *item;
  void *file_baton;
};


/* A baton for use with the path-based editor driver */
struct path_driver_cb_baton
{
  svn_wc_adm_access_t *adm_access;     /* top-level access baton */
  const svn_delta_editor_t *editor;    /* commit editor */
  void *edit_baton;                    /* commit editor's baton */
  apr_hash_t *file_mods;               /* hash: path->file_mod_t */
  const char *notify_path_prefix;      /* notification path prefix
                                          (NULL is okay, else abs path) */
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
  svn_client_commit_item3_t *item = apr_hash_get(cb_baton->commit_items,
                                                 path, APR_HASH_KEY_STRING);
  svn_node_kind_t kind = item->kind;
  void *file_baton = NULL;
  const char *copyfrom_url = NULL;
  apr_pool_t *file_pool = NULL;
  svn_wc_adm_access_t *adm_access = cb_baton->adm_access;
  const svn_delta_editor_t *editor = cb_baton->editor;
  apr_hash_t *file_mods = cb_baton->file_mods;
  svn_client_ctx_t *ctx = cb_baton->ctx;
  svn_error_t *err = SVN_NO_ERROR;

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
      const char *npath = item->path;
      svn_wc_notify_t *notify;

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
          notify->path_prefix = cb_baton->notify_path_prefix;
          (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
        }
    }

  /* If this item is supposed to be deleted, do so. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
    {
      SVN_ERR_ASSERT(parent_baton);
      err = editor->delete_entry(path, item->revision,
                                 parent_baton, pool);

      if (err)
        return fixup_out_of_date_error(path, item->kind, err);
    }

  /* If this item is supposed to be added, do so. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
    {
      if (kind == svn_node_file)
        {
          SVN_ERR_ASSERT(parent_baton);
          SVN_ERR(editor->add_file
                  (path, parent_baton, copyfrom_url,
                   copyfrom_url ? item->copyfrom_rev : SVN_INVALID_REVNUM,
                   file_pool, &file_baton));
        }
      else
        {
          SVN_ERR_ASSERT(parent_baton);
          SVN_ERR(editor->add_directory
                  (path, parent_baton, copyfrom_url,
                   copyfrom_url ? item->copyfrom_rev : SVN_INVALID_REVNUM,
                   pool, dir_baton));
        }

      /* Set other prop-changes, if available in the baton */
      if (item->outgoing_prop_changes)
        {
          svn_prop_t *prop;
          apr_array_header_t *prop_changes = item->outgoing_prop_changes;
          int ctr;
          for (ctr = 0; ctr < prop_changes->nelts; ctr++)
            {
              prop = APR_ARRAY_IDX(prop_changes, ctr, svn_prop_t *);
              if (kind == svn_node_file)
                {
                  editor->change_file_prop(file_baton, prop->name,
                                           prop->value, pool);
                }
              else
                {
                  editor->change_dir_prop(*dir_baton, prop->name,
                                          prop->value, pool);
                }
            }
        }
    }

  /* Now handle property mods. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
    {
      const svn_wc_entry_t *tmp_entry;

      if (kind == svn_node_file)
        {
          if (! file_baton)
            {
              SVN_ERR_ASSERT(parent_baton);
              err = editor->open_file(path, parent_baton,
                                      item->revision,
                                      file_pool, &file_baton);

              if (err)
                return fixup_out_of_date_error(path, kind, err);
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

      /* Ensured by harvest_committables(), item->path will never be an
         excluded path. However, will it be deleted/absent items?  I think
         committing an modification on a deleted/absent item does not make
         sense. So it's probably safe to turn off the show_hidden flag here.*/
      SVN_ERR(svn_wc_entry(&tmp_entry, item->path, adm_access, FALSE, pool));

      /* When committing a directory that no longer exists in the
         repository, a "not found" error does not occur immediately
         upon opening the directory.  It appears here during the delta
         transmisssion. */
      err = svn_wc_transmit_prop_deltas
        (item->path, adm_access, tmp_entry, editor,
         (kind == svn_node_dir) ? *dir_baton : file_baton, NULL, pool);

      if (err)
        return fixup_out_of_date_error(path, kind, err);

      SVN_ERR(svn_wc_transmit_prop_deltas
              (item->path, adm_access, tmp_entry, editor,
               (kind == svn_node_dir) ? *dir_baton : file_baton, NULL, pool));

      /* Make any additional client -> repository prop changes. */
      if (item->outgoing_prop_changes)
        {
          svn_prop_t *prop;
          int i;

          for (i = 0; i < item->outgoing_prop_changes->nelts; i++)
            {
              prop = APR_ARRAY_IDX(item->outgoing_prop_changes, i,
                                   svn_prop_t *);
              if (kind == svn_node_file)
                {
                  editor->change_file_prop(file_baton, prop->name,
                                           prop->value, pool);
                }
              else
                {
                  editor->change_dir_prop(*dir_baton, prop->name,
                                          prop->value, pool);
                }
            }
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
          SVN_ERR_ASSERT(parent_baton);
          err = editor->open_file(path, parent_baton,
                                    item->revision,
                                    file_pool, &file_baton);

          if (err)
            return fixup_out_of_date_error(path, item->kind, err);
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
                      apr_hash_t **checksums,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  apr_hash_t *file_mods = apr_hash_make(pool);
  apr_hash_t *items_hash = apr_hash_make(pool);
  apr_pool_t *iterpool = svn_pool_create(pool);
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

  /* Ditto for the md5 checksums. */
  if (checksums)
    *checksums = apr_hash_make(pool);

  /* Build a hash from our COMMIT_ITEMS array, keyed on the
     URI-decoded relative paths (which come from the item URLs).  And
     keep an array of those decoded paths, too.  */
  for (i = 0; i < commit_items->nelts; i++)
    {
      svn_client_commit_item3_t *item =
        APR_ARRAY_IDX(commit_items, i, svn_client_commit_item3_t *);
      const char *path = svn_path_uri_decode(item->url, pool);
      apr_hash_set(items_hash, path, APR_HASH_KEY_STRING, item);
      APR_ARRAY_PUSH(paths, const char *) = path;
    }

  /* Setup the callback baton. */
  cb_baton.adm_access = adm_access;
  cb_baton.editor = editor;
  cb_baton.edit_baton = edit_baton;
  cb_baton.file_mods = file_mods;
  cb_baton.notify_path_prefix = notify_path_prefix;
  cb_baton.ctx = ctx;
  cb_baton.commit_items = items_hash;

  /* Drive the commit editor! */
  SVN_ERR(svn_delta_path_driver(editor, edit_baton, SVN_INVALID_REVNUM,
                                paths, do_item_commit, &cb_baton, pool));

  /* Transmit outstanding text deltas. */
  for (hi = apr_hash_first(pool, file_mods); hi; hi = apr_hash_next(hi))
    {
      struct file_mod_t *mod;
      svn_client_commit_item3_t *item;
      void *val;
      void *file_baton;
      const char *tempfile, *dir_path;
      unsigned char digest[APR_MD5_DIGESTSIZE];
      svn_boolean_t fulltext = FALSE;
      svn_wc_adm_access_t *item_access;

      svn_pool_clear(iterpool);
      /* Get the next entry. */
      apr_hash_this(hi, NULL, NULL, &val);
      mod = val;

      /* Transmit the entry. */
      item = mod->item;
      file_baton = mod->file_baton;

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      if (ctx->notify_func2)
        {
          svn_wc_notify_t *notify;
          notify = svn_wc_create_notify(item->path,
                                        svn_wc_notify_commit_postfix_txdelta,
                                        iterpool);
          notify->kind = svn_node_file;
          notify->path_prefix = notify_path_prefix;
          (*ctx->notify_func2)(ctx->notify_baton2, notify, iterpool);
        }

      if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
        fulltext = TRUE;

      dir_path = svn_path_dirname(item->path, iterpool);
      SVN_ERR(svn_wc_adm_retrieve(&item_access, adm_access, dir_path,
                                  iterpool));
      SVN_ERR(svn_wc_transmit_text_deltas2(tempfiles ? &tempfile : NULL,
                                           digest, item->path,
                                           item_access, fulltext, editor,
                                           file_baton, iterpool));
      if (tempfiles && tempfile)
        {
          tempfile = apr_pstrdup(pool, tempfile);
          apr_hash_set(*tempfiles, tempfile, APR_HASH_KEY_STRING, (void *)1);
        }
      if (checksums)
        apr_hash_set(*checksums, item->path, APR_HASH_KEY_STRING,
                     svn_checksum__from_digest(digest, svn_checksum_md5,
                                               apr_hash_pool_get(*checksums)));
    }

  svn_pool_destroy(iterpool);

  /* Close the edit. */
  return editor->close_edit(edit_baton, pool);
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

svn_error_t *
svn_client__get_log_msg(const char **log_msg,
                        const char **tmp_file,
                        const apr_array_header_t *commit_items,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  if (ctx->log_msg_func3)
    {
      /* The client provided a callback function for the current API.
         Forward the call to it directly. */
      return (*ctx->log_msg_func3)(log_msg, tmp_file, commit_items,
                                   ctx->log_msg_baton3, pool);
    }
  else if (ctx->log_msg_func2 || ctx->log_msg_func)
    {
      /* The client provided a pre-1.5 (or pre-1.3) API callback
         function.  Convert the commit_items list to the appropriate
         type, and forward call to it. */
      svn_error_t *err;
      apr_pool_t *subpool = svn_pool_create(pool);
      apr_array_header_t *old_commit_items =
        apr_array_make(subpool, commit_items->nelts, sizeof(void*));

      int i;
      for (i = 0; i < commit_items->nelts; i++)
        {
          svn_client_commit_item3_t *item =
            APR_ARRAY_IDX(commit_items, i, svn_client_commit_item3_t *);

          if (ctx->log_msg_func2)
            {
              svn_client_commit_item2_t *old_item =
                apr_pcalloc(subpool, sizeof(*old_item));

              old_item->path = item->path;
              old_item->kind = item->kind;
              old_item->url = item->url;
              old_item->revision = item->revision;
              old_item->copyfrom_url = item->copyfrom_url;
              old_item->copyfrom_rev = item->copyfrom_rev;
              old_item->state_flags = item->state_flags;
              old_item->wcprop_changes = item->incoming_prop_changes;

              APR_ARRAY_PUSH(old_commit_items, svn_client_commit_item2_t *) =
                old_item;
            }
          else /* ctx->log_msg_func */
            {
              svn_client_commit_item_t *old_item =
                apr_pcalloc(subpool, sizeof(*old_item));

              old_item->path = item->path;
              old_item->kind = item->kind;
              old_item->url = item->url;
              /* The pre-1.3 API used the revision field for copyfrom_rev
                 and revision depeding of copyfrom_url. */
              old_item->revision = item->copyfrom_url ?
                item->copyfrom_rev : item->revision;
              old_item->copyfrom_url = item->copyfrom_url;
              old_item->state_flags = item->state_flags;
              old_item->wcprop_changes = item->incoming_prop_changes;

              APR_ARRAY_PUSH(old_commit_items, svn_client_commit_item_t *) =
                old_item;
            }
        }

      if (ctx->log_msg_func2)
        err = (*ctx->log_msg_func2)(log_msg, tmp_file, old_commit_items,
                                    ctx->log_msg_baton2, pool);
      else
        err = (*ctx->log_msg_func)(log_msg, tmp_file, old_commit_items,
                                   ctx->log_msg_baton, pool);
      svn_pool_destroy(subpool);
      return err;
    }
  else
    {
      /* No log message callback was provided by the client. */
      *log_msg = "";
      *tmp_file = NULL;
      return SVN_NO_ERROR;
    }
}

svn_error_t *
svn_client__ensure_revprop_table(apr_hash_t **revprop_table_out,
                                 const apr_hash_t *revprop_table_in,
                                 const char *log_msg,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *pool)
{
  apr_hash_t *new_revprop_table;
  if (revprop_table_in)
    {
      if (svn_prop_has_svn_prop(revprop_table_in, pool))
        return svn_error_create(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                                _("Standard properties can't be set "
                                  "explicitly as revision properties"));
      new_revprop_table = apr_hash_copy(pool, revprop_table_in);
    }
  else
    {
      new_revprop_table = apr_hash_make(pool);
    }
  apr_hash_set(new_revprop_table, SVN_PROP_REVISION_LOG, APR_HASH_KEY_STRING,
               svn_string_create(log_msg, pool));
  *revprop_table_out = new_revprop_table;
  return SVN_NO_ERROR;
}
