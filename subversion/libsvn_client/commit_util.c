/*
 * commit_util.c:  Driver for the WC commit process.
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


/* Add a new commit candidate (described by all parameters except
   `COMMITTABLES') to the COMMITABLES hash. */
static void
add_committable (apr_hash_t *committables,
                 const char *path,
                 svn_node_kind_t kind,
                 const char *url,
                 svn_revnum_t revision,
                 const char *copyfrom_url,
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
  new_item->path         = apr_pstrdup (pool, path);
  new_item->kind         = kind;
  new_item->url          = apr_pstrdup (pool, url);
  new_item->revision     = revision;
  new_item->copyfrom_url = copyfrom_url
                           ? apr_pstrdup (pool, copyfrom_url) : NULL;
  new_item->state_flags  = state_flags;
   
  /* Now, add the commit item to the array. */
  (*((svn_client_commit_item_t **) apr_array_push (array))) = new_item;
}



/* Recursively search for commit candidates in (and under) PATH (with
   entry ENTRY and ancestry URL), and add those candidates to
   COMMITTABLES.  If in ADDS_ONLY modes, only new additions are
   recognized.  COPYFROM_URL is the default copyfrom-url for children
   of copied directories.  NONRECURSIVE indicates that this function
   will not recurse into subdirectories of PATH when PATH is itself a
   directory.

   If in COPY_MODE, the entry is treated as if it is destined to be
   added with history as URL.  */
static svn_error_t *
harvest_committables (apr_hash_t *committables,
                      const char *path,
                      const char *url,
                      const char *copyfrom_url,
                      svn_wc_entry_t *entry,
                      svn_wc_entry_t *parent_entry,
                      svn_boolean_t adds_only,
                      svn_boolean_t copy_mode,
                      svn_boolean_t nonrecursive,
                      apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);  /* ### why? */
  apr_hash_t *entries = NULL;
  svn_boolean_t text_mod = FALSE, prop_mod = FALSE;
  apr_byte_t state_flags = 0;
  const char *p_path;
  svn_boolean_t tconflict, pconflict;
  const char *cf_url = NULL;

  assert (entry);
  assert (url);

  /* Make P_PATH the parent dir. */
  p_path = svn_path_remove_component_nts (path, pool);

  /* Return error on unknown path kinds. */
  if ((entry->kind != svn_node_file) && (entry->kind != svn_node_dir))
    return svn_error_create 
      (SVN_ERR_UNKNOWN_NODE_KIND, 0, NULL, pool, path);

  /* Get a fully populated entry for PATH if we can, and check for
     conflicts. If this is a directory ... */
  if (entry->kind == svn_node_dir)
    { 
      /* ... then try to read its own entries file so we have a full
         entry for it (we were going to have to do this eventually to
         recurse anyway, so... ) */
      svn_wc_entry_t *e = NULL;
      if (svn_wc_entries_read (&entries, path, FALSE, subpool))
        entries = NULL;

      if ((entries) 
          && ((e = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, 
                                 APR_HASH_KEY_STRING))))
        {
          entry = e;
          SVN_ERR (svn_wc_conflicted_p (&tconflict, &pconflict, path, 
                                        entry, subpool));
        }
      else
        {
          SVN_ERR (svn_wc_conflicted_p (&tconflict, &pconflict, p_path, 
                                        entry, subpool));
        }
    }
  else
    {
      /* If not a directory, use the parent path. */
      SVN_ERR (svn_wc_conflicted_p (&tconflict, &pconflict, p_path, 
                                    entry, subpool));
    }

  if (tconflict || pconflict)
    return svn_error_createf (SVN_ERR_WC_FOUND_CONFLICT, 0, NULL, pool,
                              "Aborting commit: '%s' remains in conflict.",
                              path);

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
      svn_revnum_t p_rev = entry->revision - 1; /* arbitrary non-equal value */
      svn_boolean_t wc_root = FALSE;

      /* If this is not a WC root then its parent's revision is
         admissible for comparitive purposes. */
      SVN_ERR (svn_wc_is_wc_root (&wc_root, path, subpool));
      if (! wc_root)
        {
          if (parent_entry)
            p_rev = parent_entry->revision;
        }
      else if (! copy_mode)
        return svn_error_createf 
          (SVN_ERR_WC_CORRUPT, 0, NULL, subpool,
           "Did not expect `%s' to be a working copy root", path);

      if (entry->revision != p_rev)
        {
          state_flags |= SVN_CLIENT_COMMIT_ITEM_ADD;
          state_flags |= SVN_CLIENT_COMMIT_ITEM_IS_COPY;
          adds_only = TRUE;
          entry->copyfrom_rev = entry->revision;
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
      apr_pool_t *loop_pool = svn_pool_create (subpool);

      /* Loop over all other entries in this directory, skipping the
         "this dir" entry. */
      for (hi = apr_hash_first (subpool, entries);
           hi;
           hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          const char *name;
          const char *used_url = NULL;
          const char *name_uri = NULL;

          /* ### Why do we need to alloc these? */
          const char *full_path = apr_pstrdup (loop_pool, path);
          const char *this_url = apr_pstrdup (loop_pool, url);
          const char *this_cf_url
            = cf_url ? apr_pstrdup (loop_pool, cf_url) : NULL;

          /* Get the next entry.  Name is an entry name; value is an
             entry structure. */
          apr_hash_this (hi, &key, NULL, &val);
          name = key;
          
          /* Skip "this dir" */
          if (! strcmp (name, SVN_WC_ENTRY_THIS_DIR))
            continue;

          this_entry = val;
          name_uri = svn_path_uri_encode (name, loop_pool);

          /* Skip subdirectory entries when we're not recursing.

             ### it occurs to me that if someone specified two
             targets, `some/dir' and `some/dir/subdir' for the commit,
             *and* specified that they wanted a non-recursive commit,
             that these would be "compressed" to a single target of
             `some/dir', which would (because of the non-recursive
             feature) result in `some/dir/subdir' not getting
             committed.  we probably ought to do something about that.  */
          if ((this_entry->kind == svn_node_dir) && nonrecursive)
            continue;

          full_path = svn_path_join (full_path, name, loop_pool);
          if (this_cf_url)
            this_cf_url = svn_path_join (this_cf_url, name_uri, loop_pool);

          /* We'll use the entry's URL if it has one and if we aren't
             in copy_mode, else, we'll just extend the parent's URL
             with the entry's basename.  */
          if ((! this_entry->url) || (copy_mode))
            {
              this_url = svn_path_join (this_url, name_uri, loop_pool);
              used_url = this_url;
            }

          /* Recurse. */
          SVN_ERR (harvest_committables 
                   (committables, full_path,
                    used_url ? used_url : this_entry->url,
                    this_cf_url,
                    (svn_wc_entry_t *)val, 
                    entry,
                    adds_only,
                    copy_mode,
                    FALSE,
                    subpool));

          svn_pool_clear (loop_pool);
        }
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__harvest_committables (apr_hash_t **committables,
                                  const char *parent_dir,
                                  apr_array_header_t *targets,
                                  svn_boolean_t nonrecursive,
                                  apr_pool_t *pool)
{
  int i = 0;

  /* Create the COMMITTABLES hash. */
  *committables = apr_hash_make (pool);

  /* ### Would be nice to use an iteration pool here, but need to
     first look into lifetime issues w/ anything passed to
     harvest_committables() and possibly stored by it. */ 
  do
    {
      svn_wc_entry_t *entry;
      const char *url;
      const char *target = apr_pstrdup (pool, parent_dir);

      /* Add the relative portion of our full path (if there are no
         relative paths, TARGET will just be PARENT_DIR for a single
         iteration. */
      if (targets->nelts)
        {
          target = svn_path_join (parent_dir,  
                                  (((const char **) targets->elts)[i]),
                                  pool);
        }

      /* No entry?  This TARGET isn't even under version control! */
      SVN_ERR (svn_wc_entry (&entry, target, FALSE, pool));
      if (! entry)
        return svn_error_create 
          (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool, target);
      
      if (! entry->url)
        {
          const char *parent, *base_name;
          svn_wc_entry_t *p_entry;
          svn_boolean_t wc_root;

          /* An entry with no URL should only come about when it is
             scheduled for addition or replacement. */
          if (! ((entry->schedule == svn_wc_schedule_add)
                 || (entry->schedule == svn_wc_schedule_replace)))
            return svn_error_createf 
              (SVN_ERR_WC_CORRUPT, 0, NULL, pool, 
               "Entry for `%s' has no URL, yet is not scheduled for addition",
               target);

          /* Check for WC-root-ness. */
          SVN_ERR (svn_wc_is_wc_root (&wc_root, target, pool));
          if (wc_root)
            return svn_error_createf 
              (SVN_ERR_ILLEGAL_TARGET, 0, NULL, pool, 
               "Entry for `%s' has no URL, and none can be derived for it",
               target);
          
          /* See if the parent is under version control (corruption if it
             isn't) and possibly scheduled for addition (illegal target if
             it is). */
          svn_path_split_nts (target, &parent, &base_name, pool);
          if (svn_path_is_empty_nts (parent))
            parent = ".";
          SVN_ERR (svn_wc_entry (&p_entry, parent, FALSE, pool));
          if (! p_entry)
            return svn_error_createf 
              (SVN_ERR_WC_CORRUPT, 0, NULL, pool, 
               "Entry for `%s' has no URL, and its parent directory\n"
               "does not appear to be under version control.", target);
          if ((p_entry->schedule == svn_wc_schedule_add)
              || (p_entry->schedule == svn_wc_schedule_replace))
            return svn_error_createf 
              (SVN_ERR_ILLEGAL_TARGET, 0, NULL, pool, 
               "`%s' is the child of an unversioned (or not-yet-versioned) "
               "directory.\nTry committing the directory itself.",
               target);
          
          /* Manufacture a URL for this TARGET. */
          url = svn_path_url_add_component (p_entry->url, base_name, pool);
        }
      else
        url = entry->url;
      
      /* If this entry is marked as 'copied' but scheduled normally, then
         it should be the child of something else marked for addition with
         history. */
      if ((entry->copied) && (entry->schedule == svn_wc_schedule_normal))
        return svn_error_createf 
          (SVN_ERR_ILLEGAL_TARGET, 0, NULL, pool, 
           "Entry for `%s' is marked as `copied' but is not itself scheduled\n"
           "for addition.  Perhaps you're committing a target that this\n"
           "inside of an unversioned (or not-yet-versioned) directory?",
           target);

      /* Handle our TARGET. */
      SVN_ERR (harvest_committables (*committables, target, 
                                     url, NULL, entry, NULL, FALSE, FALSE, 
                                     nonrecursive, pool));

      i++;
    }
  while (i < targets->nelts);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__get_copy_committables (apr_hash_t **committables,
                                   const char *new_url,
                                   const char *target,
                                   apr_pool_t *pool)
{
  svn_wc_entry_t *entry;

  /* Create the COMMITTABLES hash. */
  *committables = apr_hash_make (pool);

  /* Read the entry for TARGET. */
  SVN_ERR (svn_wc_entry (&entry, target, FALSE, pool));
  if (! entry)
    return svn_error_create 
      (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool, target);
      
  /* Handle our TARGET. */
  SVN_ERR (harvest_committables (*committables, target, 
                                 new_url, entry->url, entry, NULL,
                                 FALSE, TRUE, FALSE, pool));

  return SVN_NO_ERROR;
}


int svn_client__sort_commit_item_urls (const void *a, const void *b)
{
  const svn_client_commit_item_t *item1
    = *((const svn_client_commit_item_t * const *) a);
  const svn_client_commit_item_t *item2
    = *((const svn_client_commit_item_t * const *) b);
  return svn_path_compare_paths_nts (item1->url, item2->url);
}



svn_error_t *
svn_client__condense_commit_items (const char **base_url,
                                   apr_array_header_t *commit_items,
                                   apr_pool_t *pool)
{
  apr_array_header_t *ci = commit_items; /* convenience */
  const char *url;
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

      if ((last_item) && (strcmp (last_item->url, url) == 0))
        return svn_error_createf 
          (SVN_ERR_CLIENT_DUPLICATE_COMMIT_URL, 0, NULL, pool,
           "Cannot commit both `%s' and `%s' as they refer to the same URL.",
           item->path, last_item->path);

      /* In the first iteration, our BASE_URL is just our only
         encountered commit URL to date.  After that, we find the
         longest ancestor between the current BASE_URL and the current
         commit URL.  */
      if (i == 0)
        *base_url = apr_pstrdup (pool, url);
      else
        *base_url = svn_path_get_longest_ancestor (*base_url, url, pool); 

      /* If our BASE_URL is itself a to-be-committed item, and it is
         anything other than an already-versioned directory with
         property mods, we'll call its parent directory URL the
         BASE_URL.  Why?  Because we can't have a file URL as our base
         -- period -- and all other directory operations (removal,
         addition, etc.) require that we open that directory's parent
         dir first.  */
      /* ### I don't understand the strlen()s here, hmmm.  -kff */
      if ((strlen (*base_url) == strlen (url))
          && (! ((item->kind == svn_node_dir)
                 && item->state_flags == SVN_CLIENT_COMMIT_ITEM_PROP_MODS)))
        *base_url = svn_path_remove_component_nts (*base_url, pool);

      /* Stash our item here for the next iteration. */
      last_item = item;
    }
  
  /* Now that we've settled on a *BASE_URL, go hack that base off
     of all of our URLs. */
  for (i = 0; i < ci->nelts; i++)
    {
      svn_client_commit_item_t *this_item
        = ((svn_client_commit_item_t **) ci->elts)[i];
      int url_len = strlen (this_item->url);
      int base_url_len = strlen (*base_url);

      if (url_len > base_url_len)
        this_item->url = apr_pstrdup (pool, this_item->url + base_url_len + 1);
      else
        this_item->url = "";
    }

#ifdef SVN_CLIENT_COMMIT_DEBUG
  /* ### TEMPORARY CODE ### */
  printf ("COMMITTABLES: (base url=%s)\n", *base_url);
  for (i = 0; i < ci->nelts; i++)
    {
      url = (((svn_client_commit_item_t **) ci->elts)[i])->url;
      printf ("   %s\n", url ? url : "");
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
push_stack (const char *rel_decoded_url, /* relative to commit base url */
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
    SVN_ERR (editor->add_directory (rel_decoded_url, parent_db, copyfrom_path,
                                    revision, pool, &db));
  else
    SVN_ERR (editor->open_directory (rel_decoded_url, parent_db, revision, 
                                     pool, &db));

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
                int notify_path_offset,
                apr_pool_t *pool)
{
  svn_node_kind_t kind = item->kind;
  void *file_baton = NULL, *parent_baton = NULL, *dir_baton = NULL;
  const char *copyfrom_url = item->copyfrom_url 
                             ? item->copyfrom_url
                             : NULL;
  apr_pool_t *file_pool = ((kind == svn_node_file)
                           ? svn_pool_create (apr_hash_pool_get (file_mods))
                           : NULL);
  const char *url_decoded = svn_path_uri_decode (url, pool);

  /* Get the parent dir_baton. */
  parent_baton = ((void **) db_stack->elts)[*stack_ptr - 1];

  /* If a feedback table was supplied by the application layer,
     describe what we're about to do to this item.  */
  if (notify_func)
    {
      /* Convert an absolute path into a relative one (for feedback.) */
      const char *path = item->path + notify_path_offset;

      if ((item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
          && (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD))
        (*notify_func) (notify_baton, path, svn_wc_notify_commit_replaced,
                        item->kind,
                        NULL,
                        svn_wc_notify_state_unknown,
                        svn_wc_notify_state_unknown,
                        SVN_INVALID_REVNUM);

      else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
        (*notify_func) (notify_baton, path, svn_wc_notify_commit_deleted,
                        item->kind,
                        NULL,
                        svn_wc_notify_state_unknown,
                        svn_wc_notify_state_unknown,
                        SVN_INVALID_REVNUM);

      else if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
        (*notify_func) (notify_baton, path, svn_wc_notify_commit_added,
                        item->kind,
                        NULL,  /* ### Where can we get mime type? */
                        svn_wc_notify_state_unknown,
                        svn_wc_notify_state_unknown,
                        SVN_INVALID_REVNUM);

      else if ((item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS)
               || (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS))
        {
          svn_boolean_t tmod
            = (item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS);
          svn_boolean_t pmod
            = (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS);

          (*notify_func) (notify_baton, path, svn_wc_notify_commit_modified,
                          item->kind,
                          NULL,
                          (tmod ? svn_wc_notify_state_modified
                                : svn_wc_notify_state_unchanged),
                          (pmod ? svn_wc_notify_state_modified
                                : svn_wc_notify_state_unchanged),
                          SVN_INVALID_REVNUM);
        }
    }

  /* If this item is supposed to be deleted, do so. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
    SVN_ERR (editor->delete_entry (url_decoded, item->revision, 
                                   parent_baton, pool));

  /* If this item is supposed to be added, do so. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
    {
      if (kind == svn_node_file)
        {
          SVN_ERR (editor->add_file 
                   (url_decoded, parent_baton, copyfrom_url, 
                    item->revision,
                    file_pool, &file_baton));
        }
      else
        {
          SVN_ERR (push_stack 
                   (url_decoded, db_stack, stack_ptr, editor, copyfrom_url, 
                    item->revision,
                    TRUE, pool));
          dir_baton = ((void **) db_stack->elts)[*stack_ptr - 1];
        }
    }
    
  /* Now handle property mods. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
    {
      const char *tempfile;
      svn_wc_entry_t *tmp_entry;

      if (kind == svn_node_file)
        {
          if (! file_baton)
            SVN_ERR (editor->open_file (url_decoded, parent_baton, 
                                        item->revision, 
                                        file_pool, &file_baton));
        }
      else
        {
          if (! dir_baton)
            {
              SVN_ERR (push_stack (url_decoded, db_stack, 
                                   stack_ptr, editor, NULL,
                                   item->revision, FALSE, pool));
              dir_baton = ((void **) db_stack->elts)[*stack_ptr - 1];
            }
        }

      SVN_ERR (svn_wc_entry (&tmp_entry, item->path, TRUE, pool));
      SVN_ERR (svn_wc_transmit_prop_deltas 
               (item->path, tmp_entry, editor,
                (kind == svn_node_dir) ? dir_baton : file_baton, 
                &tempfile, pool));
      if (tempfile && tempfiles)
        apr_hash_set (tempfiles, tempfile, APR_HASH_KEY_STRING, (void *)1);
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
        SVN_ERR (editor->open_file (url_decoded, parent_baton,
                                    item->revision,
                                    file_pool, &file_baton));

      /* Add this file mod to the FILE_MODS hash. */
      mod->subpool = file_pool;
      mod->item = item;
      mod->file_baton = file_baton;
      apr_hash_set (file_mods, item->url, APR_HASH_KEY_STRING, (void *)mod);
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
svn_client__do_commit (const char *base_url,
                       apr_array_header_t *commit_items,
                       const svn_delta_editor_t *editor,
                       void *edit_baton,
                       svn_wc_notify_func_t notify_func,
                       void *notify_baton,
                       int notify_path_offset,
                       apr_hash_t **tempfiles,
                       apr_pool_t *pool)
{
  apr_array_header_t *db_stack;
  apr_hash_t *file_mods = apr_hash_make (pool);
  apr_hash_index_t *hi;
  const char *last_url = NULL; /* Initialise to remove gcc 'may be used
                                  unitialised' warning */
  int i, stack_ptr = 0;

#ifdef SVN_CLIENT_COMMIT_DEBUG
  {
    const svn_delta_editor_t *test_editor;
    void *test_edit_baton;
    SVN_ERR (get_test_editor (&test_editor, &test_edit_baton, 
                              base_url, pool));
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
      const char *item_url, *item_dir, *item_name;
      const char *common = NULL;
      size_t common_len;
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
        common = "";

      common_len = strlen (common);

      /*** Step B - Close any directories between the last commit item
           and the new common ancestor, if any need to be closed.
           Sometimes there is nothing to do here (like, for the first
           iteration, or when the last commit item was an ancestor of
           the current item).  ***/
      if ((i > 0) && (strlen (last_url) > common_len))
        {
          const char *rel = last_url + (common_len ? (common_len + 1) : 0);
          int count = count_components (rel);
          while (count--)
            {
              SVN_ERR (pop_stack (db_stack, &stack_ptr, editor));
            }
        }

      /*** Step C - Open any directories between the common ancestor
           and the parent of the commit item. ***/
      svn_path_split_nts (item_url, &item_dir, &item_name, pool);
      if (strlen (item_dir) > common_len)
        {
          char *rel = apr_pstrdup (pool, item_dir);
          char *piece = rel + common_len + 1;

          while (1)
            {
              /* Find the first separator. */
              piece = strchr (piece, '/');

              /* Temporarily replace it with a NULL terminator. */
              if (piece)
                *piece = 0;

              /* Open the subdirectory. */
              SVN_ERR (push_stack (svn_path_uri_decode (rel, pool),
                                   db_stack, &stack_ptr, 
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
      SVN_ERR (do_item_commit (item_url, item, editor,
                               db_stack, &stack_ptr, file_mods, *tempfiles,
                               notify_func, notify_baton, notify_path_offset,
                               pool));

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
      void *val;
      void *file_baton;
      const char *tempfile;
      svn_boolean_t fulltext = FALSE;
      
      /* Get the next entry. */
      apr_hash_this (hi, &key, &klen, &val);
      mod = val;

      /* Transmit the entry. */
      item = mod->item;
      file_baton = mod->file_baton;

      if (notify_func)
        (*notify_func) (notify_baton, item->path,
                        svn_wc_notify_commit_postfix_txdelta, 
                        svn_node_file,
                        NULL,
                        svn_wc_notify_state_unknown,
                        svn_wc_notify_state_unknown,
                        SVN_INVALID_REVNUM);

      if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
        fulltext = TRUE;

      SVN_ERR (svn_wc_transmit_text_deltas (item->path, fulltext,
                                            editor, file_baton, 
                                            &tempfile, mod->subpool));
      if (tempfile && *tempfiles)
        {
          tempfile = apr_pstrdup (apr_hash_pool_get (*tempfiles), tempfile);
          apr_hash_set (*tempfiles, tempfile, APR_HASH_KEY_STRING, (void *)1);
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

