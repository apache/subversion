/*
 * commit-drive.c:  Driver for the WC commit process.
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


#include <string.h>

#include "apr_pools.h"
#include "apr_hash.h"

#include "client.h"
#include "svn_path.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_sorts.h"

#include <assert.h>
#include <stdlib.h>  /* for qsort() */



/*** Uncomment this to turn on commit driver debugging. ***/
/*
#define SVN_CLIENT_COMMIT_DEBUG
*/




/*** Harvesting Commit Candidates ***/


/* If DIR isn't already in the LOCKED_DIRS hash, attempt to lock it.
   If the lock is successful, add DIR to the LOCKED_DIRS hash.  Use
   the hash's pool for adding new items, and POOL for any other
   allocations. */
static svn_error_t *
lock_dir (apr_hash_t *locked_dirs,
          svn_stringbuf_t *dir,
          apr_pool_t *pool)
{
  apr_pool_t *hash_pool = apr_hash_pool_get (locked_dirs);

  if (! apr_hash_get (locked_dirs, dir->data, dir->len))
    {
      SVN_ERR (svn_wc_lock (dir, 0, pool));
      apr_hash_set (locked_dirs, apr_pstrdup (hash_pool, dir->data), 
                    dir->len, (void *)1);
    }
  return SVN_NO_ERROR;
}


/* Add a new commit candidate (described by all parameters except
   `COMMITTABLES') to the COMMITABLES hash. */
static void
add_committable (apr_hash_t *committables,
                 svn_stringbuf_t *path,
                 svn_node_kind_t kind,
                 svn_stringbuf_t *url,
                 svn_revnum_t revision,
                 svn_stringbuf_t *copyfrom_url,
                 apr_byte_t state_flags)
{
  apr_pool_t *pool = apr_hash_pool_get (committables);
  const char *repos_name = SVN_CLIENT__SINGLE_REPOS_NAME;
  apr_array_header_t *array;
  svn_client_commit_item_t *new_item;

  /* Sanity checks. */
  assert (path && url);

  /* ### todo: Get the canonical repository for this item, which will
     be the real key for the COMMITTABLES hash, instead of the above
     bogosity. */
  array = apr_hash_get (committables, repos_name, APR_HASH_KEY_STRING);

  /* E-gads!  There is no array for this repository yet!  Oh, no
     problem, we'll just create (and add to the hash) one. */
  if (array == NULL)
    {
      array = apr_array_make (pool, 1, sizeof (new_item));
      apr_hash_set (committables, repos_name, APR_HASH_KEY_STRING, array);
    }

  /* Now update pointer values, ensuring that their allocations live
     in POOL. */
  new_item = apr_pcalloc (pool, sizeof (*new_item));
  new_item->path         = svn_stringbuf_dup (path, pool);
  new_item->kind         = kind;
  new_item->url          = svn_stringbuf_dup (url, pool);
  new_item->revision     = revision;
  new_item->copyfrom_url = copyfrom_url 
                           ? svn_stringbuf_dup (copyfrom_url, pool)
                           : NULL;
  new_item->state_flags  = state_flags;
   
  /* Now, add the commit item to the array. */
  (*((svn_client_commit_item_t **) apr_array_push (array))) = new_item;
}



/* Recursively search for commit candidates in (and under) PATH (with
   entry ENTRY and ancestry URL), and add those candidates to
   COMMITTABLES.  If in ADDS_ONLY modes, only new additions are
   recognized.  COPYFROM_URL is the default copyfrom-url for children
   of copied directories.

   If in COPY_MODE, the entry is treated as if it is destined to be
   added with history as URL.  */
static svn_error_t *
harvest_committables (apr_hash_t *committables,
                      apr_hash_t *locked_dirs,
                      svn_stringbuf_t *path,
                      svn_stringbuf_t *url,
                      svn_stringbuf_t *copyfrom_url,
                      svn_wc_entry_t *entry,
                      svn_boolean_t adds_only,
                      svn_boolean_t copy_mode,
                      apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *entries = NULL;
  svn_boolean_t text_mod = FALSE, prop_mod = FALSE;
  apr_byte_t state_flags = 0;
  svn_stringbuf_t *p_path = svn_stringbuf_dup (path, pool);
  svn_boolean_t tconflict, pconflict;
  svn_stringbuf_t *cf_url = NULL;

  assert (entry);
  assert (url);

  /* Make P_PATH the parent dir. */
  svn_path_remove_component (p_path);

  /* Return error on unknown path kinds. */
  if ((entry->kind != svn_node_file) && (entry->kind != svn_node_dir))
    return svn_error_create 
      (SVN_ERR_UNKNOWN_NODE_KIND, 0, NULL, pool, path->data);

  /* If this is a directory ... */
  if (entry->kind == svn_node_dir)
    { 
      /* ... then try to read its own entries file so we have a full
         entry for it (we were going to have to do this eventually to
         recurse anyway, so... ) */
      svn_wc_entry_t *e = NULL;
      if (svn_wc_entries_read (&entries, path, subpool))
        entries = NULL;

      if ((entries) 
          && ((e = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, 
                                 APR_HASH_KEY_STRING))))
        entry = e;
    }

  /* Test for a state of conflict, returning an error if an unresolved
     conflict exists for this item. */
  SVN_ERR (svn_wc_conflicted_p (&tconflict, &pconflict, p_path, 
                                entry, subpool));
  if (tconflict || pconflict)
    return svn_error_createf (SVN_ERR_WC_FOUND_CONFLICT, 0, NULL, pool,
                              "Aborting commit: '%s' remains in conflict.",
                              path->data);

  /* If we have our own URL, and we're NOT in COPY_MODE, it wins over
     the telescoping one(s).  In COPY_MODE, URL will always be the
     URL-to-be of the copied item.  */
  if ((entry->url) && (! copy_mode))
    url = entry->url;

  /* Check for the deletion case.  Deletes can occur only when we are
     not in "adds-only mode".  They can be either explicit
     (schedule==delete) or implicit (schedule==replace==delete+add).  */
  if ((! adds_only)
      && ((entry->schedule == svn_wc_schedule_delete)
          || (entry->schedule == svn_wc_schedule_replace)))
    {
      state_flags |= SVN_CLIENT_COMMIT_ITEM_DELETE;
    }

  /* Check for the trivial addition case.  Adds can be explicit
     (schedule==add) or implicit (schedule==replace==delete+add).  We
     also note whether or not this is an add with history here.  */
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
      && (entry->schedule == svn_wc_schedule_normal))
    {
#if 0 /* ### todo: Find a better way to do this that doesn't require
         reading the parent's entry. */
      svn_revnum_t p_rev = entry->revision - 1; /* arbitrary non-equal value */
      svn_boolean_t wc_root = FALSE;

      /* If this is not a WC root then its parent's revision is
         admissible for comparitive purposes. */
      SVN_ERR (svn_wc_is_wc_root (&wc_root, path, subpool));
      if (! wc_root)
        {
          svn_wc_entry_t *p_entry;
          SVN_ERR (svn_wc_entry (&p_entry, p_path, subpool));
          p_rev = p_entry->revision;
        }
      else if (! copy_mode)
        return svn_error_createf 
          (SVN_ERR_WC_CORRUPT, 0, NULL, subpool,
           "Did not expect `%s' to be a working copy root", path->data);

      if (entry->revision != p_rev)
#endif /* 0 */
        {
          state_flags |= SVN_CLIENT_COMMIT_ITEM_ADD;
          state_flags |= SVN_CLIENT_COMMIT_ITEM_IS_COPY;
          adds_only = TRUE;
          if (copy_mode)
            cf_url = entry->url;
          else
            cf_url = copyfrom_url;
        }
    }

  /* If an add is scheduled to occur, dig around for some more
     information about it. */
  if (state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
    {
      /* See if there are property modifications to send. */
      SVN_ERR (svn_wc_props_modified_p (&prop_mod, path, subpool));

      /* Regular adds of files have text mods, but for copies we have
         to test for textual mods.  Directories simply don't have text! */
      if (entry->kind == svn_node_file)
        {
          if (state_flags & SVN_CLIENT_COMMIT_ITEM_IS_COPY)
            SVN_ERR (svn_wc_text_modified_p (&text_mod, path, subpool));
          else
            text_mod = TRUE;
        }
    }

  /* Else, if we aren't deleting this item, we'll have to look for
     local text or property mods to determine if the path might be
     committable. */
  else if (! (state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE))
    {
      /* Check for local mods: text+props for files, props alone for dirs. */
      if (entry->kind == svn_node_file)
        SVN_ERR (svn_wc_text_modified_p (&text_mod, path, subpool));
      SVN_ERR (svn_wc_props_modified_p (&prop_mod, path, subpool));
    }

  /* Set text/prop modification flags accordingly. */
  if (text_mod)
    state_flags |= SVN_CLIENT_COMMIT_ITEM_TEXT_MODS;
  if (prop_mod)
    state_flags |= SVN_CLIENT_COMMIT_ITEM_PROP_MODS;

  /* Now, if this is something to commit, add it to our list. */
  if (state_flags)
    {
      /* If the commit item is a directory, lock it, else lock its parent. */
      if (entry->kind == svn_node_dir)
        lock_dir (locked_dirs, path, pool);
      else
        lock_dir (locked_dirs, p_path, pool);

      /* Finally, add the committable item. */
      add_committable (committables, path, entry->kind, url,
                       cf_url ? entry->copyfrom_rev : entry->revision, 
                       cf_url, state_flags);
    }

  /* For directories, recursively handle each of their entries (except
     when the directory is being deleted, unless the deletion is part
     of a replacement ... how confusing). */
  if ((entries) 
      && ((! (state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE))
          || (state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)))
    {
      apr_hash_index_t *hi;
      svn_wc_entry_t *this_entry;
      svn_stringbuf_t *full_path = svn_stringbuf_dup (path, subpool);
      svn_stringbuf_t *this_url = svn_stringbuf_dup (url, subpool);
      svn_stringbuf_t *this_cf_url
        = cf_url ? svn_stringbuf_dup (cf_url, subpool) : NULL;

      /* Loop over all other entries in this directory, skipping the
         "this dir" entry. */
      for (hi = apr_hash_first (subpool, entries); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          apr_ssize_t klen;
          void *val;
          const char *name;
          svn_stringbuf_t *used_url = NULL;

          /* Get the next entry */
          apr_hash_this (hi, &key, &klen, &val);
          name = (const char *) key;
          
          /* Skip "this dir" */
          if (! strcmp (name, SVN_WC_ENTRY_THIS_DIR))
            continue;

          /* Name is an entry name; value is an entry structure. */
          this_entry = (svn_wc_entry_t *) val;
          svn_path_add_component_nts (full_path, name);
          if (this_cf_url)
            svn_path_add_component_nts (this_cf_url, name);

          /* We'll use the entry's URL if it has one and if we aren't
             in copy_mode, else, we'll just extend the parent's URL
             with the entry's basename.  */
          if ((! this_entry->url) || (copy_mode))
            {
              svn_path_add_component_nts (this_url, name);
              used_url = this_url;
            }

          /* Recurse. */
          SVN_ERR (harvest_committables 
                   (committables, locked_dirs, full_path,
                    used_url ? used_url : this_entry->url,
                    this_cf_url,
                    (svn_wc_entry_t *)val, 
                    adds_only,
                    copy_mode,
                    subpool));

          /* Truncate paths back to their pre-loop state. */
          svn_stringbuf_chop (full_path, klen + 1);
          if (this_cf_url)
            svn_stringbuf_chop (this_cf_url, klen + 1);
          if (used_url)
            svn_stringbuf_chop (this_url, klen + 1);
        }
    }

  /* Destroy the subpool. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__harvest_committables (apr_hash_t **committables,
                                  apr_hash_t **locked_dirs,
                                  svn_stringbuf_t *parent_dir,
                                  apr_array_header_t *targets,
                                  apr_pool_t *pool)
{
  int i = 0;
  svn_stringbuf_t *target = svn_stringbuf_dup (parent_dir, pool);
  
  /* Create the COMMITTABLES hash. */
  *committables = apr_hash_make (pool);

  /* Create the LOCKED_DIRS hash. */
  *locked_dirs = apr_hash_make (pool);

  do
    {
      svn_wc_entry_t *entry;
      svn_stringbuf_t *url;

      /* Add the relative portion of our full path (if there are no
         relative paths, TARGET will just be PARENT_DIR for a single
         iteration. */
      if (targets->nelts)
        svn_path_add_component (target, 
                                (((svn_stringbuf_t **) targets->elts)[i]));

      /* No entry?  This TARGET isn't even under version control! */
      SVN_ERR (svn_wc_entry (&entry, target, pool));
      if (! entry)
        return svn_error_create 
          (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool, target->data);
      
      if (! entry->url)
        {
          svn_stringbuf_t *parent, *basename;
          svn_wc_entry_t *p_entry;
          svn_boolean_t wc_root;

          /* An entry with no URL should only come about when it is
             scheduled for addition or replacement. */
          if (! ((entry->schedule == svn_wc_schedule_add)
                 || (entry->schedule == svn_wc_schedule_replace)))
            return svn_error_createf 
              (SVN_ERR_WC_CORRUPT, 0, NULL, pool, 
               "Entry for `%s' has no URL, yet is not scheduled for addition",
               target->data);

          /* Check for WC-root-ness. */
          SVN_ERR (svn_wc_is_wc_root (&wc_root, target, pool));
          if (wc_root)
            return svn_error_createf 
              (SVN_ERR_ILLEGAL_TARGET, 0, NULL, pool, 
               "Entry for `%s' has no URL, and none can be derived for it",
               target->data);
          
          /* See if the parent is under version control (corruption if it
             isn't) and possibly scheduled for addition (illegal target if
             it is). */
          svn_path_split (target, &parent, &basename, pool);
          if (svn_path_is_empty (parent))
            parent = svn_stringbuf_create (".", pool);
          SVN_ERR (svn_wc_entry (&p_entry, parent, pool));
          if (! p_entry)
            return svn_error_createf 
              (SVN_ERR_WC_CORRUPT, 0, NULL, pool, 
               "Entry for `%s' has no URL, and its parent directory does"
               "not appear to be under version control", target->data);
          if ((p_entry->schedule == svn_wc_schedule_add)
              || (p_entry->schedule == svn_wc_schedule_replace))
            return svn_error_createf 
              (SVN_ERR_ILLEGAL_TARGET, 0, NULL, pool, 
               "`%s' is the child an unversioned (or not-yet-versioned)"
               "directory.  Try committing the directory itself",
               target->data);
          
          /* Manufacture a URL for this TARGET. */
          url = svn_stringbuf_dup (p_entry->url, pool);
          svn_path_add_component (url, basename);
        }
      else
        url = entry->url;
      
      /* If this entry is marked as 'copied' but scheduled normally, then
         it should be the child of something else marked for addition with
         history. */
      if ((entry->copied) && (entry->schedule == svn_wc_schedule_normal))
        return svn_error_createf 
          (SVN_ERR_ILLEGAL_TARGET, 0, NULL, pool, 
           "Entry for `%s' is marked as `copied' but is not itself scheduled "
           "for addition.  Perhaps you're committing a target that this "
           "inside of an unversioned (or not-yet-versioned) directory?",
           target->data);

      /* Handle our TARGET. */
      SVN_ERR (harvest_committables (*committables, *locked_dirs, target, 
                                     url, NULL, entry, FALSE, FALSE, pool));

      /* Reset our base path for the next iteration, and increment our
         counter. */
      svn_stringbuf_chop (target, target->len - parent_dir->len);
      i++;
    }
  while (i < targets->nelts);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__get_copy_committables (apr_hash_t **committables,
                                   apr_hash_t **locked_dirs,
                                   svn_stringbuf_t *new_url,
                                   svn_stringbuf_t *target,
                                   apr_pool_t *pool)
{
  svn_wc_entry_t *entry;

  /* Create the COMMITTABLES hash. */
  *committables = apr_hash_make (pool);

  /* Create the LOCKED_DIRS hash. */
  *locked_dirs = apr_hash_make (pool);

  /* Read the entry for TARGET. */
  SVN_ERR (svn_wc_entry (&entry, target, pool));
  if (! entry)
    return svn_error_create 
      (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool, target->data);
      
  /* Handle our TARGET. */
  SVN_ERR (harvest_committables (*committables, *locked_dirs, target, 
                                 new_url, entry->url, entry, 
                                 FALSE, TRUE, pool));

  return SVN_NO_ERROR;
}


int svn_client__sort_commit_item_urls (const void *a, const void *b)
{
  svn_client_commit_item_t *item1 = *((svn_client_commit_item_t **) a);
  svn_client_commit_item_t *item2 = *((svn_client_commit_item_t **) b);
  return svn_path_compare_paths (item1->url, item2->url);
}



svn_error_t *
svn_client__condense_commit_items (svn_stringbuf_t **base_url,
                                   apr_array_header_t *commit_items,
                                   apr_pool_t *pool)
{
  apr_array_header_t *ci = commit_items; /* convenience */
  svn_stringbuf_t *url;
  svn_client_commit_item_t *item, *last_item = NULL;
  int i;
  
  assert (ci && ci->nelts);

  /* Sort our commit items by their URLs. */
  qsort (ci->elts, ci->nelts, 
         ci->elt_size, svn_client__sort_commit_item_urls);

  /* Loop through the URLs, finding the longest usable ancestor common
     to all of them, and making sure there are no duplicate URLs.  */
  for (i = 0; i < ci->nelts; i++)
    {
      item = (((svn_client_commit_item_t **) ci->elts)[i]);
      url = item->url;

      if ((last_item) && (svn_stringbuf_compare (last_item->url, url)))
        return svn_error_createf 
          (SVN_ERR_CLIENT_DUPLICATE_COMMIT_URL, 0, NULL, pool,
           "Cannot commit both `%s' and `%s' as they refer to the same URL.",
           item->path->data, last_item->path->data);

      /* In the first iteration, our BASE_URL is just are only
         encountered commit URL to date.  After that, we find the
         longest ancestor between the current BASE_URL and the current
         commit URL.  */
      if (i == 0)
        *base_url = svn_stringbuf_dup (url, pool);
      else
        *base_url = svn_path_get_longest_ancestor (*base_url, url, pool); 

      /* If our BASE_URL is itself a to-be-committed item, and it is
         anything other than an already-versioned directory with
         property mods, we'll call its parent directory URL the
         BASE_URL.  Why?  Because we can't have a file URL as our base
         -- period -- and all other directory operations (removal,
         addition, etc.) require that we open that directory's parent
         dir first.  */
      if (((*base_url)->len == url->len)
          && (! ((item->kind == svn_node_dir)
                 && item->state_flags == SVN_CLIENT_COMMIT_ITEM_PROP_MODS)))
        svn_path_remove_component (*base_url);

      /* Stash our item here for the next iteration. */
      last_item = item;
    }
  
  /* Now that we've settled on a *BASE_URL, go hack that base off
     of all of our URLs. */
  for (i = 0; i < ci->nelts; i++)
    {
      url = (((svn_client_commit_item_t **) ci->elts)[i])->url;
      if (url->len > (*base_url)->len)
        {
          memmove (url->data, 
                   url->data + (*base_url)->len + 1,
                   url->len - (*base_url)->len - 1);
          url->len = url->len - (*base_url)->len - 1;
          url->data[url->len] = 0;
        }
      else
        {
          url->data[0] = 0;
          url->len = 0;
        }
    }

#ifdef SVN_CLIENT_COMMIT_DEBUG
  /* ### TEMPORARY CODE ### */
  printf ("COMMITTABLES: (base url=%s)\n", (*base_url)->data);
  for (i = 0; i < ci->nelts; i++)
    {
      url = (((svn_client_commit_item_t **) ci->elts)[i])->url;
      printf ("   %s\n", url->data ? url->data : "");
    }  
#endif /* SVN_CLIENT_COMMIT_DEBUG */

  return SVN_NO_ERROR;
}


static svn_error_t *
init_stack (apr_array_header_t **db_stack,
            int *stack_ptr,
            const svn_delta_editor_t *editor,
            void *edit_baton,
            apr_pool_t *pool)
{
  void *db;

  /* Call the EDITOR's open_root function to get our first directory
     baton. */
  SVN_ERR (editor->open_root (edit_baton, SVN_INVALID_REVNUM, pool, &db));

  /* Now allocate an array for dir_batons and push our first one
     there, incrementing our stack pointer. */
  *db_stack = apr_array_make (pool, 4, sizeof (void *));
  (*((void **) apr_array_push (*db_stack))) = db;
  *stack_ptr = 1;

  return SVN_NO_ERROR;
}


static svn_error_t *
push_stack (const char *rel_url, /* relative to base url of commit */
            apr_array_header_t *db_stack,
            int *stack_ptr,
            const svn_delta_editor_t *editor,
            const char *copyfrom_path,
            svn_revnum_t revision,
            svn_boolean_t is_add,
            apr_pool_t *pool)
{
  void *parent_db, *db;
  
  assert (db_stack && db_stack->nelts && *stack_ptr);

  /* Call the EDITOR's open_directory function to get a new directory
     baton. */
  parent_db = ((void **) db_stack->elts)[*stack_ptr - 1];
  if (is_add)
    SVN_ERR (editor->add_directory (rel_url, parent_db, copyfrom_path,
                                    revision, pool, &db));
  else
    SVN_ERR (editor->open_directory (rel_url, parent_db, revision, pool, &db));

  /* If all our current stack space is in use, push the DB onto the
     end of the array (which will allocate more space).  Else, we will
     just re-use a previously allocated slot.  */
  if (*stack_ptr == db_stack->nelts)
    (*((void **) apr_array_push (db_stack))) = db;
  else
    ((void **) db_stack->elts)[*stack_ptr] = db;

  /* Increment our stack pointer and get outta here. */
  (*stack_ptr)++;
  return SVN_NO_ERROR;
}


static svn_error_t *
pop_stack (apr_array_header_t *db_stack,
           int *stack_ptr,
           const svn_delta_editor_t *editor)
{
  void *db;

  /* Decrement our stack pointer. */
  (*stack_ptr)--;

  /* Close the most recent directory pushed to the stack. */
  db = ((void **) db_stack->elts)[*stack_ptr];
  return editor->close_directory (db);
}


static int
count_components (const char *path)
{
  int count = 1;
  const char *instance = path;

  if ((strlen (path) == 1) && (path[0] == '/'))
    return 0;

  do
    {
      instance++;
      instance = strchr (instance, '/');
      if (instance)
        count++;
    }
  while (instance);

  return count;
}


struct file_mod_t
{
  apr_pool_t *subpool;
  svn_client_commit_item_t *item;
  void *file_baton;
};


static svn_error_t *
do_item_commit (const char *url,
                svn_client_commit_item_t *item,
                const svn_delta_editor_t *editor,
                apr_array_header_t *db_stack,
                int *stack_ptr,
                apr_hash_t *file_mods,
                apr_hash_t *tempfiles,
                svn_wc_notify_func_t notify_func,
                void *notify_baton,
                svn_stringbuf_t *display_dir,
                apr_pool_t *pool)
{
  svn_node_kind_t kind = item->kind;
  void *file_baton = NULL, *parent_baton = NULL, *dir_baton = NULL;
  const char *copyfrom_url = item->copyfrom_url 
                             ? item->copyfrom_url->data
                             : NULL;
  apr_pool_t *file_pool = ((kind == svn_node_file)
                           ? svn_pool_create (apr_hash_pool_get (file_mods))
                           : NULL);


  /* Get the parent dir_baton. */
  parent_baton = ((void **) db_stack->elts)[*stack_ptr - 1];

  /* If a feedback table was supplied by the application layer,
     describe what we're about to do to this item.  */
  if (notify_func)
    {
      /* Convert an absolute path into a relative one (for feedback.) */
      const char *path = item->path->data + (display_dir->len + 1);

      if ((item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
          && (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD))
        (*notify_func) (notify_baton, svn_wc_notify_commit_replaced, path);

      else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
        (*notify_func) (notify_baton, svn_wc_notify_commit_deleted, path);

      else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
        (*notify_func) (notify_baton, svn_wc_notify_commit_added, path);

      else if ((item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS)
               || (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS))
        (*notify_func) (notify_baton, svn_wc_notify_commit_modified, path);
    }

  /* If this item is supposed to be deleted, do so. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
    SVN_ERR (editor->delete_entry (url, item->revision, 
                                   parent_baton, pool));

  /* If this item is supposed to be added, do so. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
    {
      if (kind == svn_node_file)
        {
          SVN_ERR (editor->add_file 
                   (url, parent_baton, copyfrom_url, 
                    item->revision,
                    file_pool, &file_baton));
        }
      else
        {
          SVN_ERR (push_stack 
                   (url, db_stack, stack_ptr, editor, copyfrom_url, 
                    item->revision,
                    TRUE, pool));
          dir_baton = ((void **) db_stack->elts)[*stack_ptr - 1];
        }
    }
    
  /* Now handle property mods. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
    {
      svn_stringbuf_t *tempfile;

      if (kind == svn_node_file)
        {
          if (! file_baton)
            SVN_ERR (editor->open_file (url, parent_baton, item->revision,
                                        file_pool, &file_baton));
        }
      else
        {
          if (! dir_baton)
            {
              SVN_ERR (push_stack (url, db_stack, stack_ptr, editor, NULL,
                                   item->revision, FALSE, pool));
              dir_baton = ((void **) db_stack->elts)[*stack_ptr - 1];
            }
        }

      SVN_ERR (svn_wc_transmit_prop_deltas 
               (item->path, kind, editor,
                (kind == svn_node_dir) ? dir_baton : file_baton, 
                &tempfile, pool));
      if (tempfile && tempfiles)
        apr_hash_set (tempfiles, tempfile->data, tempfile->len, (void *)1);
    }

  /* Finally, handle text mods (in that we need to open a file if it
     hasn't already been opened, and we need to put the file baton in
     our FILES hash). */
  if ((kind == svn_node_file) 
      && (item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS))
    {
      struct file_mod_t *mod = apr_palloc (apr_hash_pool_get (file_mods),
                                           sizeof (*mod));

      if (! file_baton)
        SVN_ERR (editor->open_file (url, parent_baton,
                                    item->revision,
                                    file_pool, &file_baton));

      /* Copy in the contents of the mod structure to the array.  Note
         that this is NOT a copy of a pointer reference, but a copy of
         the structure's contents!! */
      mod->subpool = file_pool;
      mod->item = item;
      mod->file_baton = file_baton;
      apr_hash_set (file_mods, item->url->data, item->url->len, (void *)mod);
    }

  /* Close any outstanding file batons that didn't get caught by the
     "has local mods" conditional above. */
  else if (file_baton)
    {
      SVN_ERR (editor->close_file (file_baton));
      svn_pool_destroy (file_pool);
    }

  return SVN_NO_ERROR;
}


#ifdef SVN_CLIENT_COMMIT_DEBUG
/* Prototype for function below */
static svn_error_t *get_test_editor (const svn_delta_editor_t **editor,
                                     void **edit_baton,
                                     const char *base_url,
                                     apr_pool_t *pool);
#endif /* SVN_CLIENT_COMMIT_DEBUG */

svn_error_t *
svn_client__do_commit (svn_stringbuf_t *base_url,
                       apr_array_header_t *commit_items,
                       const svn_delta_editor_t *editor,
                       void *edit_baton,
                       svn_wc_notify_func_t notify_func,
                       void *notify_baton,
                       svn_stringbuf_t *display_dir,
                       apr_hash_t **tempfiles,
                       apr_pool_t *pool)
{
  apr_array_header_t *db_stack;
  apr_hash_t *file_mods = apr_hash_make (pool);
  apr_hash_index_t *hi;
  int i, stack_ptr = 0;

#ifdef SVN_CLIENT_COMMIT_DEBUG
  {
    const svn_delta_editor_t *test_editor;
    void *test_edit_baton;
    SVN_ERR (get_test_editor (&test_editor, &test_edit_baton, 
                              base_url->data, pool));
    svn_delta_compose_editors (&editor, &edit_baton,
                               editor, edit_baton,
                               test_editor, test_edit_baton, pool);
  }
#endif /* SVN_CLIENT_COMMIT_DEBUG */

  /* If the caller wants us to track temporary file creation, create a
     hash to store those paths in. */
  if (tempfiles)
    *tempfiles = apr_hash_make (pool);

  /* We start by opening the root. */
  SVN_ERR (init_stack (&db_stack, &stack_ptr, editor, edit_baton, pool));

  /* Now, loop over the commit items, traversing the URL tree and
     driving the editor. */
  for (i = 0; i < commit_items->nelts; i++)
    {
      svn_stringbuf_t *last_url, *item_url, *item_dir, *item_name;
      svn_stringbuf_t *common = NULL;
      svn_client_commit_item_t *item
        = ((svn_client_commit_item_t **) commit_items->elts)[i];
      
      /* Get the next commit item URL. */
      item_url = item->url;

      /*** Step A - Find the common ancestor of the last commit item
           and the current one.  For the first iteration, this is just
           the empty string.  ***/
      if (i > 0)
        common = svn_path_get_longest_ancestor (last_url, item_url, pool);
      if (! common)
        common = svn_stringbuf_create ("", pool);

      /*** Step B - Close any directories between the last commit item
           and the new common ancestor, if any need to be closed.
           Sometimes there is nothing to do here (like, for the first
           iteration, or when the last commit item was an ancestor of
           the current item).  ***/
      if ((i > 0) && (last_url->len > common->len))
        {
          char *rel = last_url->data + (common->len ? (common->len + 1) : 0);
          int count = count_components (rel);
          while (count--)
            {
              SVN_ERR (pop_stack (db_stack, &stack_ptr, editor));
            }
        }

      /*** Step C - Open any directories between the common ancestor
           and the parent of the commit item. ***/
      svn_path_split (item_url, &item_dir, &item_name, pool);
      if (item_dir->len > common->len)
        {
          char *rel = apr_pstrdup (pool, item_dir->data);
          char *piece = rel + common->len + 1;

          while (1)
            {
              /* Find the first separator. */
              piece = strchr (piece, '/');

              /* Temporarily replace it with a NULL terminator. */
              if (piece)
                *piece = 0;

              /* Open the subdirectory. */
              SVN_ERR (push_stack (rel, db_stack, &stack_ptr, 
                                   editor, NULL, SVN_INVALID_REVNUM,
                                   FALSE, pool));
              
              /* If we temporarily replaced a '/' with a NULL,
                 un-replace it and move our piece pointer to the
                 character after the '/' we found.  If there was no
                 piece found, though, we're done.  */
              if (piece)
                {
                  *piece = '/';
                  piece++;    
                }
              else
                break;
            }
        }

      /*** Step D - Commit the item.  ***/
      SVN_ERR (do_item_commit (item_url->data, item, editor,
                               db_stack, &stack_ptr, file_mods, *tempfiles,
                               notify_func, notify_baton, display_dir, pool));

      /* Save our state for the next iteration. */
      if ((item->kind == svn_node_dir)
          && ((! (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE))
              || (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)))
        last_url = item_url;
      else
        last_url = item_dir;
    }

  /* Close down any remaining open directory batons. */
  while (stack_ptr)
    {
      SVN_ERR (pop_stack (db_stack, &stack_ptr, editor));
    }

  /* Transmit outstanding text deltas. */
  for (hi = apr_hash_first (pool, file_mods); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      struct file_mod_t *mod;
      svn_client_commit_item_t *item;
      void *file_baton;
      svn_stringbuf_t *tempfile;
      svn_boolean_t fulltext = FALSE;
      
      /* Get the next entry. */
      apr_hash_this (hi, &key, &klen, (void **) &mod);

      /* Transmit the entry. */
      item = mod->item;
      file_baton = mod->file_baton;

      if (notify_func)
        (*notify_func) (notify_baton, svn_wc_notify_commit_postfix_txdelta, 
                        item->path->data);

      if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
        fulltext = TRUE;

      SVN_ERR (svn_wc_transmit_text_deltas (item->path, fulltext,
                                            editor, file_baton, 
                                            &tempfile, mod->subpool));
      if (tempfile && *tempfiles)
        {
          tempfile = svn_stringbuf_dup (tempfile, 
                                        apr_hash_pool_get (*tempfiles));
          apr_hash_set (*tempfiles, tempfile->data, tempfile->len, (void *)1);
        }
      
      SVN_ERR (editor->close_file (file_baton));
      svn_pool_destroy (mod->subpool);
    }

  /* Close the edit. */
  SVN_ERR (editor->close_edit (edit_baton));
  return SVN_NO_ERROR;
}


svn_client_commit_info_t *
svn_client__make_commit_info (svn_revnum_t revision,
                              const char *author,
                              const char *date,
                              apr_pool_t *pool)
{
  svn_client_commit_info_t *info;

  if (date || author || SVN_IS_VALID_REVNUM (revision))
    {
      info = apr_palloc (pool, sizeof (*info));
      info->date = date ? apr_pstrdup (pool, date) : NULL;
      info->author = author ? apr_pstrdup (pool, author) : NULL;
      info->revision = revision;
      return info;
    }
  return NULL;
}


#ifdef SVN_CLIENT_COMMIT_DEBUG

/*** Temporary test editor ***/

struct edit_baton
{
  const char *path;
};


static void *
make_baton (const char *path, apr_pool_t *pool)
{
  struct edit_baton *new_baton 
    = apr_pcalloc (pool, sizeof (struct edit_baton *));
  new_baton->path = apr_pstrdup (pool, path);
  return ((void *) new_baton);
}


static svn_error_t *
open_root (void *edit_baton,
           svn_revnum_t base_revision,
           apr_pool_t *dir_pool,
           void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  printf ("TEST EDIT STARTED (base url=%s)\n", eb->path);
  *root_baton = make_baton (eb->path, dir_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
add_item (const char *path,
          void *parent_baton,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          apr_pool_t *pool,
          void **baton)
{
  printf ("   Adding  : %s\n", path);
  *baton = make_baton (path, pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry (const char *path,
              svn_revnum_t revision,
              void *parent_baton,
              apr_pool_t *pool)
{
  printf ("   Deleting: %s\n", path);
  return SVN_NO_ERROR;
}

static svn_error_t *
open_item (const char *path,
           void *parent_baton,
           svn_revnum_t base_revision,
           apr_pool_t *pool,
           void **baton)
{
  printf ("   Opening : %s\n", path);
  *baton = make_baton (path, pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
close_item (void *baton)
{
  struct edit_baton *this = baton;
  printf ("   Closing : %s\n", this->path);
  return SVN_NO_ERROR;
}


static svn_error_t *
change_prop (void *file_baton,
             const char *name,
             const svn_string_t *value,
             apr_pool_t *pool)
{
  printf ("      PropSet (%s=%s)\n", name, value ? value->data : "");
  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  printf ("      Transmitting text...\n");
  *handler = *handler_baton = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
close_edit (void *edit_baton)
{
  printf ("TEST EDIT COMPLETED\n");
  return SVN_NO_ERROR;
}

static svn_error_t *
get_test_editor (const svn_delta_editor_t **editor,
                 void **edit_baton,
                 const char *base_url,
                 apr_pool_t *pool)
{
  svn_delta_editor_t *ed = svn_delta_default_editor (pool);
  struct edit_baton *eb = apr_pcalloc (pool, sizeof (*eb));

  eb->path = apr_pstrdup (pool, base_url);

  ed->open_root = open_root;
  ed->add_directory = add_item;
  ed->open_directory = open_item;
  ed->close_directory = close_item;
  ed->add_file = add_item;
  ed->open_file = open_item;
  ed->close_file = close_item;
  ed->delete_entry = delete_entry;
  ed->apply_textdelta = apply_textdelta;
  ed->change_dir_prop = change_prop;
  ed->change_file_prop = change_prop;
  ed->close_edit = close_edit;

  *editor = ed;
  *edit_baton = eb;
  return SVN_NO_ERROR;
}
#endif /* SVN_CLIENT_COMMIT_DEBUG */
  

/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */

