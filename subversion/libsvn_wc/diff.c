/*
 * diff.c -- The diff editor for comparing the working copy against the
 *           repository.
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

/*
 * This code uses an svn_delta_editor_t editor driven by
 * svn_wc_crawl_revisions (like the update command) to retrieve the
 * differences between the working copy and the requested repository
 * version. Rather than updating the working copy, this new editor creates
 * temporary files that contain the pristine repository versions. When the
 * crawler closes the files the editor calls back to a client layer
 * function to compare the working copy and the temporary file. There is
 * only ever one temporary file in existence at any time.
 *
 * When the crawler closes a directory, the editor then calls back to the
 * client layer to compare any remaining files that may have been modified
 * locally. Added directories do not have corresponding temporary
 * directories created, as they are not needed.
 *
 * ### TODO: It might be better if the temporary files were not created in
 * the admin's temp area, but in a more general area (/tmp, $TMPDIR) as
 * then diff could be run on a read-only working copy.
 *
 * ### TODO: Replacements where the node kind changes needs support. It
 * mostly works when the change is in the repository, but not when it is
 * in the working copy.
 *
 * ### TODO: Do we need to support copyfrom?
 *
 */

#include <apr_hash.h>
#include "svn_pools.h"
#include <assert.h>

#include "wc.h"
#include "adm_files.h"


/*-------------------------------------------------------------------------*/
/* A little helper function.

   You see, when we ask the server to update us to a certain revision,
   we construct the new fulltext, and then run 

         'diff <repos_fulltext> <working_fulltext>'

   which is, of course, actually backwards from the repository's point
   of view.  It thinks we want to move from working->repos.

   So when the server sends property changes, they're effectively
   backwards from what we want.  We don't want working->repos, but
   repos->working.  So this little helper "reverses" the value in
   BASEPROPS and PROPCHANGES before we pass them off to the
   prop_changed() diff-callback.  */
static void
reverse_propchanges (apr_hash_t *baseprops,
                     apr_array_header_t *propchanges,
                     apr_pool_t *pool)
{
  int i;

  /* ### todo: research lifetimes for property values below */

  for (i = 0; i < propchanges->nelts; i++)
    {
      svn_prop_t *propchange
        = &APR_ARRAY_IDX (propchanges, i, svn_prop_t);
      
      const svn_string_t *original_value =
        apr_hash_get (baseprops, propchange->name, APR_HASH_KEY_STRING);
     
      if ((original_value == NULL) && (propchange->value != NULL)) 
        {
          /* found an addition.  make it look like a deletion. */
          apr_hash_set (baseprops, propchange->name, APR_HASH_KEY_STRING,
                        svn_string_dup (propchange->value, pool));
          propchange->value = NULL;
        }

      else if ((original_value != NULL) && (propchange->value == NULL)) 
        {
          /* found a deletion.  make it look like an addition. */
          propchange->value = svn_string_dup (original_value, pool);
          apr_hash_set (baseprops, propchange->name, APR_HASH_KEY_STRING,
                        NULL);
        }

      else if ((original_value != NULL) && (propchange->value != NULL)) 
        {
          /* found a change.  just swap the values.  */
          const svn_string_t *str = svn_string_dup (propchange->value, pool);
          propchange->value = svn_string_dup (original_value, pool);
          apr_hash_set (baseprops, propchange->name, APR_HASH_KEY_STRING, str);
        }
    }
}


/*-------------------------------------------------------------------------*/

/* Overall crawler editor baton.
 */
struct edit_baton {
  /* ANCHOR/TARGET represent the base of the hierarchy to be compared. */
  svn_wc_adm_access_t *anchor;
  const char *anchor_path;
  const char *target;

  /* Target revision */
  svn_revnum_t revnum;

  /* The callbacks and callback argument that implement the file comparison
     functions */
  const svn_wc_diff_callbacks_t *callbacks;
  void *callback_baton;

  /* Flags whether to diff recursively or not. If set the diff is
     recursive. */
  svn_boolean_t recurse;

  apr_pool_t *pool;
};

/* Directory level baton.
 */
struct dir_baton {
  /* Gets set if the directory is added rather than replaced/unchanged. */
  svn_boolean_t added;

  /* The "correct" path of the directory, but it may not exist in the
     working copy. */
  const char *path;

 /* Identifies those directory elements that get compared while running the
    crawler. These elements should not be compared again when recursively
    looking for local only diffs. */
  apr_hash_t *compared;

  /* The baton for the parent directory, or null if this is the root of the
     hierarchy to be compared. */
  struct dir_baton *dir_baton;

  /* The original property hash, and the list of incoming propchanges. */
  apr_hash_t *baseprops;
  apr_array_header_t *propchanges;
  svn_boolean_t fetched_baseprops; /* did we get the working props yet? */

  /* The overall crawler editor baton. */
  struct edit_baton *edit_baton;

  apr_pool_t *pool;
};

/* File level baton.
 */
struct file_baton {
  /* Gets set if the file is added rather than replaced. */
  svn_boolean_t added;

  /* PATH is the "correct" path of the file, but it may not exist in the
     working copy.  WC_PATH is a path we can use to make temporary files
     or open empty files; it doesn't necessarily exist either, but the
     directory part of it does. */
  const char *path;
  const char *wc_path;

 /* When constructing the requested repository version of the file,
    ORIGINAL_FILE is version of the file in the working copy. TEMP_FILE is
    the pristine repository file obtained by applying the repository diffs
    to ORIGINAL_FILE. */
  apr_file_t *original_file;
  apr_file_t *temp_file;

  /* The original property hash, and the list of incoming propchanges. */
  apr_hash_t *baseprops;
  apr_array_header_t *propchanges;
  svn_boolean_t fetched_baseprops; /* did we get the working props yet? */

  /* APPLY_HANDLER/APPLY_BATON represent the delta applcation baton. */
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;

  /* The overall crawler editor baton. */
  struct edit_baton *edit_baton;

  apr_pool_t *pool;
};


/* Create a new edit baton. TARGET/ANCHOR are working copy paths that
 * describe the root of the comparison. CALLBACKS/CALLBACK_BATON
 * define the callbacks to compare files. RECURSE defines whether to
 * descend into subdirectories.
 */
static struct edit_baton *
make_editor_baton (svn_wc_adm_access_t *anchor,
                   const char *target,
                   const svn_wc_diff_callbacks_t *callbacks,
                   void *callback_baton,
                   svn_boolean_t recurse,
                   apr_pool_t *pool)
{
  struct edit_baton *eb = apr_palloc (pool, sizeof (*eb));

  eb->anchor = anchor;
  eb->anchor_path = svn_wc_adm_access_path (anchor);
  eb->target = apr_pstrdup (pool, target);
  eb->callbacks = callbacks;
  eb->callback_baton = callback_baton;
  eb->recurse = recurse;
  eb->pool = pool;

  return eb;
}


/* Create a new directory baton.  PATH is the directory path,
 * including anchor_path.  ADDED is set if this directory is being
 * added rather than replaced.  PARENT_BATON is the baton of the
 * parent directory, it will be null if this is the root of the
 * comparison hierarchy.  The directory and its parent may or may not
 * exist in the working copy.  EDIT_BATON is the overall crawler
 * editor baton.
 */
static struct dir_baton *
make_dir_baton (const char *path,
                struct dir_baton *parent_baton,
                struct edit_baton *edit_baton,
                svn_boolean_t added,
                apr_pool_t *pool)
{
  struct dir_baton *dir_baton = apr_pcalloc (pool, sizeof (*dir_baton));

  dir_baton->dir_baton = parent_baton;
  dir_baton->edit_baton = edit_baton;
  dir_baton->added = added;
  dir_baton->pool = pool;
  dir_baton->baseprops = apr_hash_make (dir_baton->pool);
  dir_baton->propchanges = apr_array_make (pool, 1, sizeof (svn_prop_t));
  dir_baton->compared = apr_hash_make (dir_baton->pool);
  dir_baton->path = path;

  return dir_baton;
}

/* Create a new file baton.  PATH is the file path, including
 * anchor_path.  ADDED is set if this file is being added rather than
 * replaced.  PARENT_BATON is the baton of the parent directory.  The
 * directory and its parent may or may not exist in the working copy.
 */
static struct file_baton *
make_file_baton (const char *path,
                 svn_boolean_t added,
                 struct dir_baton *parent_baton,
                 apr_pool_t *pool)
{
  struct file_baton *file_baton = apr_pcalloc (pool, sizeof (*file_baton));
  struct edit_baton *edit_baton = parent_baton->edit_baton;

  file_baton->edit_baton = edit_baton;
  file_baton->added = added;
  file_baton->pool = pool;
  file_baton->baseprops = apr_hash_make (file_baton->pool);
  file_baton->propchanges  = apr_array_make (pool, 1, sizeof (svn_prop_t));
  file_baton->path = path;

  /* If the parent directory is added rather than replaced it does not
     exist in the working copy.  Determine a working copy path whose
     directory part does exist; we can use that to create temporary
     files.  It doesn't matter whether the file part exists in the
     directory. */
  if (parent_baton->added)
    {
      struct dir_baton *wc_dir_baton = parent_baton;

      /* Ascend until a directory is not being added, this will be a
         directory that does exist. This must terminate since the root of
         the comparison cannot be added. */
      while (wc_dir_baton->added)
        wc_dir_baton = wc_dir_baton->dir_baton;

      file_baton->wc_path = svn_path_join (wc_dir_baton->path, "unimportant",
                                           file_baton->pool);
    }
  else
    {
      file_baton->wc_path = file_baton->path;
    }

  return file_baton;
}


/* Called by directory_elements_diff when a file is to be compared. At this
 * stage we are dealing with a file that does exist in the working copy.
 *
 * DIR_BATON is the parent directory baton, PATH is the path to the file to
 * be compared. ENTRY is the working copy entry for the file. ADDED forces
 * the file to be treated as added.
 *
 * Do all allocation in POOL.
 *
 * ### TODO: Need to work on replace if the new filename used to be a
 * directory.
 */
static svn_error_t *
file_diff (struct dir_baton *dir_baton,
           const char *path,
           const svn_wc_entry_t *entry,
           svn_boolean_t added,
           apr_pool_t *pool)
{
  const char *pristine_copy, *empty_file;
  svn_boolean_t modified;
  enum svn_wc_schedule_t schedule = entry->schedule;
  svn_wc_adm_access_t *adm_access;

  /* If the directory is being added, then this file will need to be
     added. */
  if (added)
    schedule = svn_wc_schedule_add;

  switch (schedule)
    {
      /* Replace is treated like a delete plus an add: two
         comparisons are generated, first one for the delete and
         then one for the add. */
    case svn_wc_schedule_replace:
    case svn_wc_schedule_delete:
      /* Delete compares text-base against empty file, modifications to the
         working-copy version of the deleted file are not wanted. */
      pristine_copy = svn_wc__text_base_path (path, FALSE, pool);
      empty_file = svn_wc__empty_file_path (path, pool);

      SVN_ERR (dir_baton->edit_baton->callbacks->file_deleted
               (NULL, path, 
                pristine_copy, 
                empty_file,
                dir_baton->edit_baton->callback_baton));

      /* Replace will fallthrough! */
      if (schedule == svn_wc_schedule_delete)
        break;

    case svn_wc_schedule_add:
      empty_file = svn_wc__empty_file_path (path, pool);

      SVN_ERR (dir_baton->edit_baton->callbacks->file_added
               (NULL, path,
                empty_file,
                path,
                dir_baton->edit_baton->callback_baton));

      SVN_ERR (svn_wc_adm_retrieve (&adm_access, dir_baton->edit_baton->anchor,
                                    dir_baton->path, pool));
      SVN_ERR (svn_wc_props_modified_p (&modified, path, adm_access, pool));
      if (modified)
        {
          apr_array_header_t *propchanges;
          apr_hash_t *baseprops;

          SVN_ERR (svn_wc_get_prop_diffs (&propchanges, &baseprops, path,
                                          pool));

          SVN_ERR (dir_baton->edit_baton->callbacks->props_changed
                   (NULL, NULL,
                    path,
                    propchanges, baseprops,
                    dir_baton->edit_baton->callback_baton));
        }
      break;

    default:
      SVN_ERR (svn_wc_adm_retrieve (&adm_access, dir_baton->edit_baton->anchor,
                                    dir_baton->path, pool));
      SVN_ERR (svn_wc_text_modified_p (&modified, path, adm_access, pool));
      if (modified)
        {
          const char *translated;
          svn_error_t *err;

          pristine_copy = svn_wc__text_base_path (path, FALSE, pool);   

          /* Note that this might be the _second_ time we translate
             the file, as svn_wc_text_modified_p() might have used a
             tmp translated copy too.  But what the heck, diff is
             already expensive, translating twice for the sake of code
             modularity is liveable. */
          SVN_ERR (svn_wc_translated_file (&translated, path, adm_access,
                                           TRUE, pool));
          
          err = dir_baton->edit_baton->callbacks->file_changed
            (NULL, NULL,
             path,
             pristine_copy, 
             translated,
             entry->revision,
             SVN_INVALID_REVNUM,
             dir_baton->edit_baton->callback_baton);
          
          if (translated != path)
            SVN_ERR (svn_io_remove_file (translated, pool));

          if (err)
            return err;
        }

      SVN_ERR (svn_wc_props_modified_p (&modified, path, adm_access, pool));
      if (modified)
        {
          apr_array_header_t *propchanges;
          apr_hash_t *baseprops;

          SVN_ERR (svn_wc_get_prop_diffs (&propchanges, &baseprops, path,
                                          pool));

          SVN_ERR (dir_baton->edit_baton->callbacks->props_changed
                   (NULL, NULL,
                    path,
                    propchanges, baseprops,
                    dir_baton->edit_baton->callback_baton));
        }
    }

  return SVN_NO_ERROR;
}

/* Called when the directory is closed to compare any elements that have
 * not yet been compared. This identifies local, working copy only
 * changes. At this stage we are dealing with files/directories that do
 * exist in the working copy.
 *
 * DIR_BATON is the baton for the directory. ADDED forces the directory
 * to be treated as added.
 */
static svn_error_t *
directory_elements_diff (struct dir_baton *dir_baton,
                         svn_boolean_t added)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_boolean_t in_anchor_not_target;
  apr_pool_t *subpool;
  svn_wc_adm_access_t *adm_access;

  /* This directory should have been unchanged or replaced, not added,
     since an added directory can only contain added files and these will
     already have been compared. (Note: the ADDED flag is used to simulate
     added directories, these are *not* scheduled to be added in the
     working copy.) */
  assert (!dir_baton->added);

  /* Determine if this is the anchor directory if the anchor is different
     to the target. When the target is a file, the anchor is the parent
     directory and if this is that directory the non-target entries must be
     skipped. */
  in_anchor_not_target =
    (dir_baton->edit_baton->target
     && (! svn_path_compare_paths_nts
         (dir_baton->path,
          svn_wc_adm_access_path (dir_baton->edit_baton->anchor))));

  SVN_ERR (svn_wc_adm_retrieve (&adm_access, dir_baton->edit_baton->anchor,
                                dir_baton->path, dir_baton->pool));

  /* Check for property mods on this directory. */
  if (!in_anchor_not_target)
    {
      svn_boolean_t modified;

      SVN_ERR (svn_wc_props_modified_p (&modified, dir_baton->path, adm_access,
                                        dir_baton->pool));
      if (modified)
        {
          apr_array_header_t *propchanges;
          apr_hash_t *baseprops;

          SVN_ERR (svn_wc_get_prop_diffs (&propchanges, &baseprops,
                                          dir_baton->path,
                                          dir_baton->pool));
              
          SVN_ERR (dir_baton->edit_baton->callbacks->props_changed
                   (NULL, NULL,
                    dir_baton->path,
                    propchanges, baseprops,
                    dir_baton->edit_baton->callback_baton));
        }
    }

  SVN_ERR (svn_wc_entries_read (&entries, adm_access, FALSE, dir_baton->pool));

  subpool = svn_pool_create (dir_baton->pool);

  for (hi = apr_hash_first (dir_baton->pool, entries); hi;
       hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      const svn_wc_entry_t *entry;
      struct dir_baton *subdir_baton;
      const char *name, *path;

      apr_hash_this (hi, &key, NULL, &val);
      name = key;
      entry = val;
      
      /* Skip entry for the directory itself. */
      if (strcmp (key, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      /* In the anchor directory, if the anchor is not the target then all
         entries other than the target should not be diff'd. Running diff
         on one file in a directory should not diff other files in that
         directory. */
      if (in_anchor_not_target
          && strcmp (dir_baton->edit_baton->target, name))
        continue;

      path = svn_path_join (dir_baton->path, name, subpool);

      /* Skip entry if it is in the list of entries already diff'd. */
      if (apr_hash_get (dir_baton->compared, path, APR_HASH_KEY_STRING))
        continue;

      switch (entry->kind)
        {
        case svn_node_file:
          SVN_ERR (file_diff (dir_baton, path, entry, added, subpool));
          break;

        case svn_node_dir:
          if (entry->schedule == svn_wc_schedule_replace)
            {
              /* ### TODO: Don't know how to do this bit. How do I get
                 information about what is being replaced? If it was a
                 directory then the directory elements are also going to be
                 deleted. We need to show deletion diffs for these
                 files. If it was a file we need to show a deletion diff
                 for that file. */
            }

          /* Check the subdir if in the anchor (the subdir is the target), or
             if recursive */
          if (in_anchor_not_target || dir_baton->edit_baton->recurse)
            {
              subdir_baton = make_dir_baton (path, dir_baton,
                                             dir_baton->edit_baton,
                                             FALSE,
                                             subpool);

              SVN_ERR (directory_elements_diff (subdir_baton, added));
            }
          break;

        default:
          break;
        }

      svn_pool_clear (subpool);
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}

/* An editor function. */
static svn_error_t *
set_target_revision (void *edit_baton, 
                     svn_revnum_t target_revision,
                     apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  eb->revnum = target_revision;

  return SVN_NO_ERROR;
}

/* An editor function. The root of the comparison hierarchy */
static svn_error_t *
open_root (void *edit_baton,
           svn_revnum_t base_revision,
           apr_pool_t *dir_pool,
           void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *b;

  b = make_dir_baton (eb->anchor_path, NULL, eb, FALSE, dir_pool);
  *root_baton = b;

  return SVN_NO_ERROR;
}

/* An editor function. */
static svn_error_t *
delete_entry (const char *path,
              svn_revnum_t base_revision,
              void *parent_baton,
              apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  const svn_wc_entry_t *entry;
  struct dir_baton *b;
  const char *full_path = svn_path_join (pb->edit_baton->anchor_path, path,
                                         pb->pool);
  svn_wc_adm_access_t *adm_access;

  SVN_ERR (svn_wc_adm_probe_retrieve (&adm_access, pb->edit_baton->anchor,
                                      full_path, pool));
  SVN_ERR (svn_wc_entry (&entry, full_path, adm_access, FALSE, pool));
  switch (entry->kind)
    {
    case svn_node_file:
      /* A delete is required to change working-copy into requested
         revision, so diff should show this as and add. Thus compare the
         empty file against the current working copy. */
      SVN_ERR (pb->edit_baton->callbacks->file_added
               (NULL, full_path,
                svn_wc__empty_file_path (full_path, pool),
                full_path,
                pb->edit_baton->callback_baton));
      apr_hash_set (pb->compared, full_path, APR_HASH_KEY_STRING, "");
      break;

    case svn_node_dir:
      b = make_dir_baton (full_path, pb, pb->edit_baton, FALSE, pool);
      /* A delete is required to change working-copy into requested
         revision, so diff should show this as and add. Thus force the
         directory diff to treat this as added. */
      SVN_ERR (directory_elements_diff (b, TRUE));
      break;

    default:
      break;
    }

  return SVN_NO_ERROR;
}

/* An editor function. */
static svn_error_t *
add_directory (const char *path,
               void *parent_baton,
               const char *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               apr_pool_t *dir_pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct dir_baton *b;
  const char *full_path;

  /* ### TODO: support copyfrom? */

  full_path = svn_path_join (pb->edit_baton->anchor_path, path, dir_pool);
  b = make_dir_baton (full_path, pb, pb->edit_baton, TRUE, dir_pool);
  *child_baton = b;

  return SVN_NO_ERROR;
}

/* An editor function. */
static svn_error_t *
open_directory (const char *path,
                void *parent_baton,
                svn_revnum_t base_revision,
                apr_pool_t *dir_pool,
                void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct dir_baton *b;
  const char *full_path;

  full_path = svn_path_join (pb->edit_baton->anchor_path, path, dir_pool);
  b = make_dir_baton (full_path, pb, pb->edit_baton, FALSE, dir_pool);
  *child_baton = b;

  return SVN_NO_ERROR;
}

/* An editor function.  When a directory is closed, all the directory
 * elements that have been added or replaced will already have been
 * diff'd. However there may be other elements in the working copy
 * that have not yet been considered.  */
static svn_error_t *
close_directory (void *dir_baton,
                 apr_pool_t *pool)
{
  struct dir_baton *b = dir_baton;

  /* Skip added directories, they can only contain added elements all of
     which have already been diff'd. */
  if (!b->added)
    SVN_ERR (directory_elements_diff (dir_baton, FALSE));

  /* Mark this directory as compared in the parent directory's baton. */
  if (b->dir_baton)
    {
      apr_hash_set (b->dir_baton->compared, b->path, APR_HASH_KEY_STRING,
                    "");
    }

  if (b->propchanges->nelts > 0)
    {
      reverse_propchanges (b->baseprops, b->propchanges, b->pool);
      SVN_ERR (b->edit_baton->callbacks->props_changed
               (NULL, NULL,
                b->path,
                b->propchanges,
                b->baseprops,
                b->edit_baton->callback_baton));
    }

  return SVN_NO_ERROR;
}

/* An editor function. */
static svn_error_t *
add_file (const char *path,
          void *parent_baton,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          apr_pool_t *file_pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *b;
  const char *full_path;

  /* ### TODO: support copyfrom? */

  full_path = svn_path_join (pb->edit_baton->anchor_path, path, file_pool);
  b = make_file_baton (full_path, TRUE, pb, file_pool);
  *file_baton = b;

  /* Add this filename to the parent directory's list of elements that
     have been compared. */
  apr_hash_set (pb->compared, apr_pstrdup(pb->pool, full_path),
                APR_HASH_KEY_STRING, "");

  return SVN_NO_ERROR;
}

/* An editor function. */
static svn_error_t *
open_file (const char *path,
           void *parent_baton,
           svn_revnum_t base_revision,
           apr_pool_t *file_pool,
           void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *b;
  const char *full_path;

  full_path = svn_path_join (pb->edit_baton->anchor_path, path, file_pool);
  b = make_file_baton (full_path, FALSE, pb, file_pool);
  *file_baton = b;

  /* Add this filename to the parent directory's list of elements that
     have been compared. */
  apr_hash_set (pb->compared, apr_pstrdup(pb->pool, full_path),
                APR_HASH_KEY_STRING, "");

  return SVN_NO_ERROR;
}

/* This is an apr cleanup handler. It is called whenever the associated
 * pool is cleared or destroyed. It is installed when the temporary file is
 * created, and removes the file when the file pool is deleted, whether in
 * the normal course of events, or if an error occurs.
 *
 * ARG is the file baton for the working copy file associated with the
 * temporary file.
 */
static apr_status_t
temp_file_cleanup_handler (void *arg)
{
  struct file_baton *b = arg;
  svn_error_t *err;

  /* The path to the temporary copy of the pristine repository version. */
  const char *temp_file_path
    = svn_wc__text_base_path (b->wc_path, TRUE, b->pool);

  err = svn_io_remove_file (temp_file_path, b->pool);

  return (err? err->apr_err : APR_SUCCESS);
}

/* This removes the temp_file_cleanup_handler in the child process before
 * exec'ing diff.
 */
static apr_status_t
temp_file_cleanup_handler_remover (void *arg)
{
  struct file_baton *b = arg;
  apr_pool_cleanup_kill (b->pool, b, temp_file_cleanup_handler);
  return APR_SUCCESS;
}

/* An editor function.  Do the work of applying the text delta. */
static svn_error_t *
window_handler (svn_txdelta_window_t *window,
                void *window_baton)
{
  struct file_baton *b = window_baton;

  SVN_ERR (b->apply_handler (window, b->apply_baton));

  if (!window)
    {
      SVN_ERR (svn_wc__close_text_base (b->temp_file, b->wc_path, 0, b->pool));

      if (b->added)
        {
          SVN_ERR (svn_wc__close_empty_file (b->original_file, b->wc_path,
                                             b->pool));
        }
      else
        {
          SVN_ERR (svn_wc__close_text_base (b->original_file, b->wc_path, 0,
                                            b->pool));
        }
    }

  return SVN_NO_ERROR;
}

/* An editor function. */
static svn_error_t *
apply_textdelta (void *file_baton,
                 apr_pool_t *pool,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct file_baton *b = file_baton;

  if (b->added)
    {
      /* An empty file is the starting point if the file is being added */
      SVN_ERR (svn_wc__open_empty_file (&b->original_file, b->wc_path,
                                        b->pool));
    }
  else
    {
      /* The current text-base is the starting point if replacing */
      SVN_ERR (svn_wc__open_text_base (&b->original_file, b->wc_path,
                                       APR_READ, b->pool));
    }

  /* This is the file that will contain the pristine repository version. It
     is created in the admin temporary area. This file continues to exists
     until after the diff callback is run, at which point it is deleted. */ 
  SVN_ERR (svn_wc__open_text_base (&b->temp_file, b->wc_path,
                                   (APR_WRITE | APR_TRUNCATE | APR_CREATE),
                                   b->pool));

  /* Need to ensure that the above file gets removed if the program aborts
     with some error. So register a pool cleanup handler to delete the
     file. This handler is removed just before deleting the file. */
  apr_pool_cleanup_register (b->pool, file_baton, temp_file_cleanup_handler,
                             temp_file_cleanup_handler_remover);

  svn_txdelta_apply (svn_stream_from_aprfile (b->original_file, b->pool),
                     svn_stream_from_aprfile (b->temp_file, b->pool),
                     b->pool,
                     &b->apply_handler, &b->apply_baton);

  *handler = window_handler;
  *handler_baton = file_baton;
  return SVN_NO_ERROR;
}

/* An editor function.  When the file is closed we have a temporary
 * file containing a pristine version of the repository file. This can
 * be compared against the working copy.  */
static svn_error_t *
close_file (void *file_baton,
            apr_pool_t *pool)
{
  struct file_baton *b = file_baton;
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;

  /* The path to the temporary copy of the pristine repository version. */
  const char *temp_file_path
    = svn_wc__text_base_path (b->wc_path, TRUE, b->pool);
  SVN_ERR (svn_wc_adm_probe_retrieve (&adm_access, b->edit_baton->anchor,
                                      b->wc_path, b->pool));
  SVN_ERR (svn_wc_entry (&entry, b->wc_path, adm_access, FALSE, b->pool));

  if (b->added)
    {
      /* Add is required to change working-copy into requested revision, so
         diff should show this as and delete. Thus compare the current
         working copy against the empty file. */
      SVN_ERR (b->edit_baton->callbacks->file_deleted
               (NULL, b->path,
                temp_file_path,
                svn_wc__empty_file_path (b->wc_path, b->pool),
                b->edit_baton->callback_baton));
    }
  else
    {
      /* Be careful with errors to ensure that the temporary translated
         file is deleted. */
      svn_error_t *err1, *err2 = SVN_NO_ERROR;
      const char *translated;
      
      SVN_ERR (svn_wc_translated_file (&translated, b->path, adm_access,
                                       TRUE, b->pool));

      err1 = b->edit_baton->callbacks->file_changed
        (NULL, NULL,
         b->path,
         temp_file_path,
         translated,
         b->edit_baton->revnum,
         SVN_INVALID_REVNUM,
         b->edit_baton->callback_baton);
      
      if (translated != b->path)
        err2 = svn_io_remove_file (translated, b->pool);

      if (err1 || err2)
        return err1 ? err1 : err2;
      
      if (b->propchanges->nelts > 0)
        {
          reverse_propchanges (b->baseprops, b->propchanges, b->pool);
          SVN_ERR (b->edit_baton->callbacks->props_changed
                   (NULL, NULL,
                    b->path,
                    b->propchanges,
                    b->baseprops,
                    b->edit_baton->callback_baton));
        }
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  const char *name,
                  const svn_string_t *value,
                  apr_pool_t *pool)
{
  struct file_baton *b = file_baton;
  svn_prop_t *propchange;

  propchange = apr_array_push (b->propchanges);
  propchange->name = apr_pstrdup (b->pool, name);
  propchange->value = value ? svn_string_dup (value, b->pool) : NULL;
  
  /* Read the baseprops if you haven't already. */
  if (! b->fetched_baseprops)
    {
      /* the 'base' props to compare against, in this case, are
         actually the working props.  that's what we do with texts,
         anyway, in the 'svn diff -rN foo' case.  */

      /* also notice we're ignoring error here;  there's a chance that
         this path might not exist in the working copy, in which case
         the baseprops remains an empty hash. */
      svn_wc_prop_list (&(b->baseprops), b->path, b->pool);
      b->fetched_baseprops = TRUE;
    }

  return SVN_NO_ERROR;
}


/* An editor function. */
static svn_error_t *
change_dir_prop (void *dir_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  svn_prop_t *propchange;

  propchange = apr_array_push (db->propchanges);
  propchange->name = apr_pstrdup (db->pool, name);
  propchange->value = value ? svn_string_dup (value, db->pool) : NULL;

  /* Read the baseprops if you haven't already. */
  if (! db->fetched_baseprops)
    {
      /* the 'base' props to compare against, in this case, are
         actually the working props.  that's what we do with texts,
         anyway, in the 'svn diff -rN foo' case.  */

      /* also notice we're ignoring error here;  there's a chance that
         this path might not exist in the working copy, in which case
         the baseprops remains an empty hash. */
      svn_wc_prop_list (&(db->baseprops), db->path, db->pool);
      db->fetched_baseprops = TRUE;
    }

  return SVN_NO_ERROR;
}


/* An editor function. */
static svn_error_t *
close_edit (void *edit_baton,
            apr_pool_t *pool)
{
  /* ### TODO: If the root has not been replaced, then we need to do this
     to pick up local changes. Can this happen? Well, at the moment, it
     does if I replace a file with a directory, but I don't know if that is
     supposed to be supported yet. I would do something like this...
  */
# if 0
  struct edit_baton *eb = edit_baton;

  if (!eb->root_replaced)
    {
      struct dir_baton *b;

      b = make_dir_baton (eb->anchor_path, NULL, eb, FALSE, eb->pool);
      SVN_ERR (directory_elements_diff (b, FALSE));
    }
#endif

  return SVN_NO_ERROR;
}

/* Public Interface */


/* Create a diff editor and baton. */
svn_error_t *
svn_wc_get_diff_editor (svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks_t *callbacks,
                        void *callback_baton,
                        svn_boolean_t recurse,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        apr_pool_t *pool)
{
  struct edit_baton *eb;
  svn_delta_editor_t *tree_editor;

  eb = make_editor_baton (anchor, target, callbacks, callback_baton,
                          recurse, pool);
  tree_editor = svn_delta_default_editor (eb->pool);

  tree_editor->set_target_revision = set_target_revision;
  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->close_directory = close_directory;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->change_dir_prop = change_dir_prop;
  tree_editor->close_file = close_file;
  tree_editor->close_edit = close_edit;

  *edit_baton = eb;
  *editor = tree_editor;

  return SVN_NO_ERROR;
}

/* Compare working copy against the text-base. */
svn_error_t *
svn_wc_diff (svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks_t *callbacks,
             void *callback_baton,
             svn_boolean_t recurse,
             apr_pool_t *pool)
{
  struct edit_baton *eb;
  struct dir_baton *b;
  const svn_wc_entry_t *entry;
  const char *target_path;
  svn_wc_adm_access_t *adm_access;

  eb = make_editor_baton (anchor, target, callbacks, callback_baton,
                          recurse, pool);

  if (target)
    target_path = svn_path_join (svn_wc_adm_access_path (anchor), target,
                                 eb->pool);
  else
    target_path = apr_pstrdup (eb->pool, svn_wc_adm_access_path (anchor));

  SVN_ERR (svn_wc_adm_probe_retrieve (&adm_access, anchor, target_path,
                                      eb->pool));
  SVN_ERR (svn_wc_entry (&entry, target_path, adm_access, FALSE, eb->pool));

  if (entry->kind == svn_node_dir)
    b = make_dir_baton (target_path, NULL, eb, FALSE, eb->pool);
  else
    b = make_dir_baton (eb->anchor_path, NULL, eb, FALSE, eb->pool);

  SVN_ERR (directory_elements_diff (b, FALSE));

  return SVN_NO_ERROR;
}


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
