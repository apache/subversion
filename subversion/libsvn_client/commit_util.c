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


/* Add a new commit candidate to the COMMITABLES hash.  PATH, ENTRY,
   TEXT_MODS and PROP_MODS all refer to the new commit candidate. */
static void
add_committable (apr_hash_t *committables,
                 svn_stringbuf_t *path,
                 svn_stringbuf_t *url,
                 svn_wc_entry_t *entry,
                 apr_byte_t state_flags)
{
  apr_pool_t *pool = apr_hash_pool_get (committables);
  const char *repos_name = SVN_CLIENT__SINGLE_REPOS_NAME;
  apr_array_header_t *array;
  svn_client_commit_item_t *new_item;

  /* Sanity checks. */
  assert (path && entry && url);

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
  new_item->path        = svn_stringbuf_dup (path, pool);
  new_item->url         = svn_stringbuf_dup (url, pool);
  new_item->entry       = svn_wc_entry_dup (entry, pool);
  new_item->state_flags = state_flags;
   
  /* Now, add the commit item to the array. */
  (*((svn_client_commit_item_t **) apr_array_push (array))) = new_item;
}



/* Recursively search for commit candidates in (and under) PATH (with
   entry ENTRY and ancestry URL), and add those candidates to
   COMMITTABLES.  If in ADDS_ONLY modes, only new additions are
   recognized.  
*/
static svn_error_t *
harvest_committables (apr_hash_t *committables,
                      apr_hash_t *locked_dirs,
                      svn_stringbuf_t *path,
                      svn_stringbuf_t *url,
                      svn_wc_entry_t *entry,
                      svn_boolean_t adds_only,
                      apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *entries = NULL;
  svn_boolean_t text_mod = FALSE, prop_mod = FALSE;
  apr_byte_t state_flags = 0;
  svn_stringbuf_t *p_path = NULL;

  assert (entry);
  assert (url);

  /* Return error on unknown path kinds. */
  if ((entry->kind != svn_node_file) && (entry->kind != svn_node_dir))
    return svn_error_create 
      (SVN_ERR_UNKNOWN_NODE_KIND, 0, NULL, pool, path->data);

  /* If this is a directory ... */
  if (entry->kind == svn_node_dir)
    { 
      /* ... then try to read its own entries file so we have a full
         entry for it (we were going to have to do this eventually to
         recurse, so... ) */
      svn_wc_entry_t *e;

      if (SVN_NO_ERROR == svn_wc_entries_read (&entries, path, subpool))
        e = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);
      else
        entries = NULL; /* paranoia */

      if (e) 
        entry = e;
    }

  /* If we have our own URL, it wins over the telescoping one. */
  if (entry->url)
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
     (schedule==add) or import (schedule==replace==delete+add).  We
     also note whether or not this is an add with history here.  */
  if ((entry->schedule == svn_wc_schedule_add)
      || (entry->schedule == svn_wc_schedule_replace))
    {
      state_flags |= SVN_CLIENT_COMMIT_ITEM_ADD;
      if (entry->copyfrom_url)
        {
          state_flags |= SVN_CLIENT_COMMIT_ITEM_IS_COPY;
          adds_only = TRUE;
        }
    }

  /* Check for the copied-subtree addition case.  */
  if (entry->copied && (entry->schedule == svn_wc_schedule_normal))
    {
      svn_boolean_t wc_root = FALSE;
      svn_wc_entry_t *p_entry;

      /* If this is a WC root ... well, something is probably wrong. */
      SVN_ERR (svn_wc_is_wc_root (&wc_root, path, subpool));
      if (wc_root)
        return svn_error_createf 
          (SVN_ERR_WC_CORRUPT, 0, NULL, subpool,
           "Did not expect `%s' to be a working copy root", path->data);

      /* If this is NOT a WC root, check out its parent's revision. */
      p_path = svn_stringbuf_dup (path, subpool);
      svn_path_remove_component (p_path);
      SVN_ERR (svn_wc_entry (&p_entry, p_path, subpool));
      if (entry->revision != p_entry->revision)
        {
          state_flags |= SVN_CLIENT_COMMIT_ITEM_ADD;
          state_flags |= SVN_CLIENT_COMMIT_ITEM_IS_COPY;
          adds_only = TRUE;
        }
    }

#if 0 /* unfinished */
  /* Test for a state of conflict, returning an error if an unresolved
     conflict exists for this item. */
  {
    svn_boolean_t *text_conflict_p, *prop_conflict_p;
    SVN_ERR (svn_wc_conflicted_p (&text_conflict_p, &prop_conflict_p,
                                  parent_dir, entry, pool));
    if (text_conflict_p || prop_conflict_p)
      return svn_error_createf (SVN_ERR_WC_FOUND_CONFLICT, 0, NULL, pool,
                                "Aborting commit: '%s' remains in conflict.",
                                full_path->data);
  }
#endif

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

  /* Else, we'll have to look for local text or property mods to
     determine if the path might be committable. */
  else
    {
      /* Check for local mods: text+props for files, props alone for dirs. */
      if (entry->kind == svn_node_file)
        SVN_ERR (svn_wc_text_modified_p (&text_mod, path, subpool));
      SVN_ERR (svn_wc_props_modified_p (&prop_mod, path, subpool));
    }

  if (text_mod)
    state_flags |= SVN_CLIENT_COMMIT_ITEM_TEXT_MODS;
  if (prop_mod)
    state_flags |= SVN_CLIENT_COMMIT_ITEM_PROP_MODS;

  /* Now, if this is something to commit, add it to our list. */
  if (state_flags)
    {
      /* If the commit item is a directory, lock it. */
      if (entry->kind == svn_node_dir)
        lock_dir (locked_dirs, path, pool);

      /* Else, try to lock its parent directory (calculating it if we
         haven't done so already. */
      else
        {
          if (! p_path)
            {
              p_path = svn_stringbuf_dup (path, pool);
              svn_path_remove_component (p_path);
            }
          lock_dir (locked_dirs, p_path, pool);
        }

      /* Finally, add the committable item. */
      add_committable (committables, path, url, entry, state_flags);
    }

  /* For directories, recursively handle each of their entries.  
     ### todo: if do_delete==TRUE, we could skip this.  deleted
     sub-things would not be reported as committables, but it would
     save us the recursion step. */
  if ((entries) && (! (state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)))
    {
      apr_hash_index_t *hi;
      svn_wc_entry_t *this_entry;
      svn_stringbuf_t *full_path = svn_stringbuf_dup (path, subpool);
      svn_stringbuf_t *this_url = svn_stringbuf_dup (url, subpool);

      /* Loop over all other entries in this directory, skipping the
         "this dir" entry. */
      for (hi = apr_hash_first (subpool, entries); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          apr_ssize_t klen;
          void *val;
          const char *name;

          /* Get the next entry */
          apr_hash_this (hi, &key, &klen, &val);
          name = (const char *) key;
          
          /* Skip "this dir" */
          if (! strcmp (name, SVN_WC_ENTRY_THIS_DIR))
            continue;

          /* Name is an entry name; value is an entry structure. */
          this_entry = (svn_wc_entry_t *) val;
          svn_path_add_component_nts (full_path, name);

          /* We'll use the entry's URL if it has one, else, we'll just
             extend the parent's URL with the entry's basename.  */
          if (! this_entry->url)
            svn_path_add_component_nts (this_url, name);

          /* Recurse. */
          SVN_ERR (harvest_committables 
                   (committables, locked_dirs, full_path,
                    this_entry->url ? this_entry->url : this_url, 
                    (svn_wc_entry_t *)val, 
                    adds_only,
                    subpool));

          /* Truncate paths back to their pre-loop state. */
          svn_stringbuf_chop (full_path, klen + 1);
          if (! this_entry->url)
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

      /* Add the relative portion of our full path (if there are no
         relative paths, TARGET will just be PARENT_DIR for a single
         iteration. */
      if (targets->nelts)
        svn_path_add_component (target, 
                                (((svn_stringbuf_t **) targets->elts)[i]));

      /* Read the entry for PATH.  We require it, and require it to
         have a URL. */
      SVN_ERR (svn_wc_entry (&entry, target, pool));
      if (! entry)
        return svn_error_create 
          (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool, target->data);
      if (! entry->url)
        return svn_error_createf 
          (SVN_ERR_ENTRY_MISSING_URL, 0, NULL, pool, 
           "Entry for `%s' has no URL.  Perhaps you're committing "
           "inside of an unversioned (or not-yet-versioned) directory?",
           target->data);

      /* Handle our TARGET. */
      SVN_ERR (harvest_committables (*committables, *locked_dirs, target, 
                                     entry->url, entry, FALSE, pool));

      /* Reset our base path for the next iteration, and increment our
         counter. */
      svn_stringbuf_chop (target, target->len - parent_dir->len);
      i++;
    }
  while (i < targets->nelts);

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
  int i;
  
  assert (ci && ci->nelts);

  /* Sort our commit items by their URLs. */
  qsort (ci->elts, ci->nelts, 
         ci->elt_size, svn_client__sort_commit_item_urls);

  /* Find a common BASE_URL that these ci share. */
  url = (((svn_client_commit_item_t **) ci->elts)[0])->url;
  *base_url = svn_stringbuf_dup (url, pool);

  /* ### BIG OL' TODO:  

     We seriously need to know the canonical repository URL, methinks.
     For one, we could get rid of that stupid hack COMMITTABLES key
     name (see client.h), but more importantly, for a commit of a
     single item, we sometimes need to anchor our commit editor on the
     parent of that thing.  For example, to commit a single file, we
     must call ed->open_root() on its parent, then open_file() on the
     file itself.  To commit, say, a single directory deletion, we
     need to call ed->open_root() on the parent of the dir, then
     delete_entry() on the dir itself.  While we don't have to do this
     for, say, the commit of a single directory's property changes, it
     doesn't hurt to just do this all the time.

     However, this is one exception to the "doesn't hurt to just do
     this all the time" rule, and that is when we're talking about the
     root URL of a repository.  We can't open the parent of the root
     URL (!), and in general there shouldn't be any working copy
     operation that would necessitate this anyway.  If we knew the
     canonical repos URL though, a single comparison of that versus
     the single thing we're committing would tell us whether or not it
     was okay to open the parent directory of the thing. */

  /* If there is only one commit candidate, we'll call its parent
     directory URL the BASE_URL. */
  if (ci->nelts == 1)
    svn_path_remove_component (*base_url);
  else
    {
      /* Loop through the remainder of the URLs (this only happens if
         there was more than one commit candidate), finding the common
         ancestor between it and our current best pick for a
         *BASE_URL.  That will be our our new best pick. */
      for (i = 1; i < ci->nelts; i++)
        {
          url = (((svn_client_commit_item_t **) ci->elts)[i])->url;
          *base_url = svn_path_get_longest_ancestor (*base_url, url, pool); 
        }
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

  /* ### TEMPORARY CODE ### */
  printf ("COMMITTABLES: (base url=%s)\n", (*base_url)->data);
  for (i = 0; i < ci->nelts; i++)
    {
      url = (((svn_client_commit_item_t **) ci->elts)[i])->url;
      printf ("   %s\n", url->data ? url->data : "");
    }  

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
  svn_client_commit_item_t *item;
  void *file_baton;
};


static svn_error_t *
do_item_commit (const char *url,
                svn_client_commit_item_t *item,
                const svn_delta_editor_t *editor,
                apr_array_header_t *db_stack,
                int *stack_ptr,
                apr_array_header_t *file_mods,
                apr_pool_t *pool)
{
  svn_wc_entry_t *entry = item->entry;
  svn_node_kind_t kind = entry->kind;
  void *file_baton = NULL, *parent_baton = NULL, *dir_baton = NULL;
  const char *copyfrom_url = entry->copyfrom_url 
                             ? entry->copyfrom_url->data
                             : NULL;
  svn_revnum_t copyfrom_rev = copyfrom_url 
                              ? entry->copyfrom_rev 
                              : SVN_INVALID_REVNUM;

  /* Get the parent dir_baton. */
  parent_baton = ((void **) db_stack->elts)[*stack_ptr - 1];

  /* If this item is supposed to be deleted, do so. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
    SVN_ERR (editor->delete_entry (url, entry->revision, 
                                   parent_baton, pool));

  /* If this item is supposed to be added, do so. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
    {
      if (kind == svn_node_file)
        {
          SVN_ERR (editor->add_file 
                   (url, parent_baton, copyfrom_url, 
                    copyfrom_url ? copyfrom_rev : entry->revision,
                    pool, &file_baton));
        }
      else
        {
          SVN_ERR (push_stack 
                   (url, db_stack, stack_ptr, editor, copyfrom_url, 
                    copyfrom_url ? copyfrom_rev : entry->revision,
                    TRUE, pool));
          dir_baton = ((void **) db_stack->elts)[*stack_ptr - 1];
        }
    }
    
  /* Now handle property mods. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
    {
      if (kind == svn_node_file)
        {
          if (! file_baton)
            SVN_ERR (editor->open_file (url, parent_baton, entry->revision,
                                        pool, &file_baton));
        }
      else
        {
          if (! dir_baton)
            {
              SVN_ERR (push_stack (url, db_stack, stack_ptr, editor, NULL,
                                   entry->revision, FALSE, pool));
              dir_baton = ((void **) db_stack->elts)[*stack_ptr - 1];
            }
        }

      SVN_ERR (svn_wc_transmit_prop_deltas 
               (item->path, kind, editor,
                (kind == svn_node_dir) ? dir_baton : file_baton, pool));
    }

  /* Finally, handle text mods (in that we need to open a file if it
     hasn't already been opened, and we need to put the file baton in
     our FILES hash). */
  if ((kind == svn_node_file) &&
      (item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS))
    {
      struct file_mod_t mod;

      if (! file_baton)
        SVN_ERR (editor->open_file (url, parent_baton,
                                    item->entry->revision,
                                    pool, &file_baton));

      /* Copy in the contents of the mod structure to the array.  Note
         that this is NOT a copy of a pointer reference, but a copy of
         the structure's contents!! */
      mod.item = item;
      mod.file_baton = file_baton;
      (*((struct file_mod_t *) apr_array_push (file_mods))) = mod;
      return SVN_NO_ERROR;
    }

  /* Close any outstanding file batons that didn't get caught by the
     "has local mods" conditional above. */
  if (file_baton)
    SVN_ERR (editor->close_file (file_baton));
  
  return SVN_NO_ERROR;
}



/* Prototype for function below */
static svn_error_t *get_test_editor (const svn_delta_editor_t **editor,
                                     void **edit_baton,
                                     const char *base_url,
                                     apr_pool_t *pool);


svn_error_t *
svn_client__do_commit (apr_array_header_t *commit_items,
                       const svn_delta_editor_t *editor,
                       void *edit_baton,
                       svn_boolean_t wc_commit,
                       const svn_ra_get_latest_revnum_func_t *revnum_fn,
                       void *rev_baton,
                       apr_pool_t *pool)
{
  svn_stringbuf_t *base_url;
  apr_array_header_t *db_stack;
  apr_array_header_t *file_mods 
    = apr_array_make (pool, 1, sizeof (struct file_mod_t));
  int i, stack_ptr = 0;

  /* Sort and condense our COMMIT_ITEMS. */
  SVN_ERR (svn_client__condense_commit_items (&base_url, commit_items, pool));

  /* ### TEMPORARY ### */
  /* Editor?  Editor who?  We'll use our own testing editor for now. */
  SVN_ERR (get_test_editor (&editor, &edit_baton, base_url->data, pool));

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
          int count = count_components (last_url->data + common->len + 1);
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
                                   editor, NULL, item->entry->revision, 
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
                               db_stack, &stack_ptr, file_mods, pool));

      /* Save our state for the next iteration. */
      last_url = (item->entry->kind == svn_node_dir) ? item_url : item_dir;
    }

  /* Close down any remaining open directory batons. */
  while (stack_ptr--)
    {
      SVN_ERR (pop_stack (db_stack, &stack_ptr, editor));
    }

  /* Transmit outstanding text deltas. */
  for (i = 0; i < file_mods->nelts; i++)
    {
      struct file_mod_t *mod
        = ((struct file_mod_t *) file_mods->elts) + i;
      svn_client_commit_item_t *item = mod->item;
      void *file_baton = mod->file_baton;

      printf ("   Sending : %s\n", item->url->data);
      SVN_ERR (svn_wc_transmit_text_deltas 
               (item->path, item->entry, editor, file_baton, pool));
    }

  /* Close the edit. */
  SVN_ERR (editor->close_edit (edit_baton));
  return SVN_NO_ERROR;
}




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
  

/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */

