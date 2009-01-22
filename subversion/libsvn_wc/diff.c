/*
 * diff.c -- The diff editor for comparing the working copy against the
 *           repository.
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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
 * ### TODO: Replacements where the node kind changes needs support. It
 * mostly works when the change is in the repository, but not when it is
 * in the working copy.
 *
 * ### TODO: Do we need to support copyfrom?
 *
 */

#include <apr_hash.h>

#include "svn_pools.h"
#include "svn_path.h"
#include "svn_hash.h"

#include "private/svn_wc_private.h"

#include "wc.h"
#include "props.h"
#include "adm_files.h"

#include "svn_private_config.h"


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
reverse_propchanges(apr_hash_t *baseprops,
                    apr_array_header_t *propchanges,
                    apr_pool_t *pool)
{
  int i;

  /* ### todo: research lifetimes for property values below */

  for (i = 0; i < propchanges->nelts; i++)
    {
      svn_prop_t *propchange
        = &APR_ARRAY_IDX(propchanges, i, svn_prop_t);

      const svn_string_t *original_value =
        apr_hash_get(baseprops, propchange->name, APR_HASH_KEY_STRING);

      if ((original_value == NULL) && (propchange->value != NULL))
        {
          /* found an addition.  make it look like a deletion. */
          apr_hash_set(baseprops, propchange->name, APR_HASH_KEY_STRING,
                       svn_string_dup(propchange->value, pool));
          propchange->value = NULL;
        }

      else if ((original_value != NULL) && (propchange->value == NULL))
        {
          /* found a deletion.  make it look like an addition. */
          propchange->value = svn_string_dup(original_value, pool);
          apr_hash_set(baseprops, propchange->name, APR_HASH_KEY_STRING,
                       NULL);
        }

      else if ((original_value != NULL) && (propchange->value != NULL))
        {
          /* found a change.  just swap the values.  */
          const svn_string_t *str = svn_string_dup(propchange->value, pool);
          propchange->value = svn_string_dup(original_value, pool);
          apr_hash_set(baseprops, propchange->name, APR_HASH_KEY_STRING, str);
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

  /* Was the root opened? */
  svn_boolean_t root_opened;

  /* The callbacks and callback argument that implement the file comparison
     functions */
  const svn_wc_diff_callbacks3_t *callbacks;
  void *callback_baton;

  /* How does this diff descend? */
  svn_depth_t depth;

  /* Should this diff ignore node ancestry. */
  svn_boolean_t ignore_ancestry;

  /* Possibly diff repos against text-bases instead of working files. */
  svn_boolean_t use_text_base;

  /* Possibly show the diffs backwards. */
  svn_boolean_t reverse_order;

  /* Empty file used to diff adds / deletes */
  const char *empty_file;

  /* Hash whose keys are const char * changelist names. */
  apr_hash_t *changelist_hash;

  apr_pool_t *pool;
};

/* Directory level baton.
 */
struct dir_baton {
  /* Gets set if the directory is added rather than replaced/unchanged. */
  svn_boolean_t added;

  /* The depth at which this directory should be diffed. */
  svn_depth_t depth;

  /* The "correct" path of the directory, but it may not exist in the
     working copy. */
  const char *path;

  /* Identifies those directory elements that get compared while running
     the crawler.  These elements should not be compared again when
     recursively looking for local modifications.

     This hash maps the full path of the entry to an unimportant value
     (presence in the hash is the important factor here, not the value
     itself).

     If the directory's properties have been compared, an item with hash
     key of "" (an empty string) will be present in the hash. */
  apr_hash_t *compared;

  /* The baton for the parent directory, or null if this is the root of the
     hierarchy to be compared. */
  struct dir_baton *dir_baton;

  /* The list of incoming BASE->repos propchanges. */
  apr_array_header_t *propchanges;

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

 /* When constructing the requested repository version of the file, we
    drop the result into a file at TEMP_FILE_PATH. */
  const char *temp_file_path;

  /* The list of incoming BASE->repos propchanges. */
  apr_array_header_t *propchanges;

  /* APPLY_HANDLER/APPLY_BATON represent the delta applcation baton. */
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;

  /* The overall crawler editor baton. */
  struct edit_baton *edit_baton;

  apr_pool_t *pool;
};

/* Create a new edit baton. TARGET/ANCHOR are working copy paths that
 * describe the root of the comparison. CALLBACKS/CALLBACK_BATON
 * define the callbacks to compare files. DEPTH defines if and how to
 * descend into subdirectories; see public doc string for exactly how.
 * IGNORE_ANCESTRY defines whether to utilize node ancestry when
 * calculating diffs.  USE_TEXT_BASE defines whether to compare
 * against working files or text-bases.  REVERSE_ORDER defines which
 * direction to perform the diff.
 *
 * CHANGELISTS is a list of const char * changelist names, used to
 * filter diff output responses to only those items in one of the
 * specified changelists, empty (or NULL altogether) if no changelist
 * filtering is requested.
 */
static svn_error_t *
make_editor_baton(struct edit_baton **edit_baton,
                  svn_wc_adm_access_t *anchor,
                  const char *target,
                  const svn_wc_diff_callbacks3_t *callbacks,
                  void *callback_baton,
                  svn_depth_t depth,
                  svn_boolean_t ignore_ancestry,
                  svn_boolean_t use_text_base,
                  svn_boolean_t reverse_order,
                  const apr_array_header_t *changelists,
                  apr_pool_t *pool)
{
  apr_hash_t *changelist_hash = NULL;
  struct edit_baton *eb;

  if (changelists && changelists->nelts)
    SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelists, pool));

  eb = apr_pcalloc(pool, sizeof(*eb));
  eb->anchor = anchor;
  eb->anchor_path = svn_wc_adm_access_path(anchor);
  eb->target = apr_pstrdup(pool, target);
  eb->callbacks = callbacks;
  eb->callback_baton = callback_baton;
  eb->depth = depth;
  eb->ignore_ancestry = ignore_ancestry;
  eb->use_text_base = use_text_base;
  eb->reverse_order = reverse_order;
  eb->changelist_hash = changelist_hash;
  eb->pool = pool;

  *edit_baton = eb;
  return SVN_NO_ERROR;
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
make_dir_baton(const char *path,
               struct dir_baton *parent_baton,
               struct edit_baton *edit_baton,
               svn_boolean_t added,
               svn_depth_t depth,
               apr_pool_t *pool)
{
  struct dir_baton *dir_baton = apr_pcalloc(pool, sizeof(*dir_baton));

  dir_baton->dir_baton = parent_baton;
  dir_baton->edit_baton = edit_baton;
  dir_baton->added = added;
  dir_baton->depth = depth;
  dir_baton->pool = pool;
  dir_baton->propchanges = apr_array_make(pool, 1, sizeof(svn_prop_t));
  dir_baton->compared = apr_hash_make(dir_baton->pool);
  dir_baton->path = path;

  return dir_baton;
}

/* Create a new file baton.  PATH is the file path, including
 * anchor_path.  ADDED is set if this file is being added rather than
 * replaced.  PARENT_BATON is the baton of the parent directory.  The
 * directory and its parent may or may not exist in the working copy.
 */
static struct file_baton *
make_file_baton(const char *path,
                svn_boolean_t added,
                struct dir_baton *parent_baton,
                apr_pool_t *pool)
{
  struct file_baton *file_baton = apr_pcalloc(pool, sizeof(*file_baton));
  struct edit_baton *edit_baton = parent_baton->edit_baton;

  file_baton->edit_baton = edit_baton;
  file_baton->added = added;
  file_baton->pool = pool;
  file_baton->propchanges  = apr_array_make(pool, 1, sizeof(svn_prop_t));
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

      file_baton->wc_path = svn_path_join(wc_dir_baton->path, "unimportant",
                                          file_baton->pool);
    }
  else
    {
      file_baton->wc_path = file_baton->path;
    }

  return file_baton;
}

/* Get the empty file associated with the edit baton. This is cached so
 * that it can be reused, all empty files are the same.
 */
static svn_error_t *
get_empty_file(struct edit_baton *b,
               const char **empty_file)
{
  /* Create the file if it does not exist */
  /* Note that we tried to use /dev/null in r17220, but
     that won't work on Windows: it's impossible to stat NUL */
  if (!b->empty_file)
    {
      SVN_ERR(svn_io_open_unique_file3(NULL, &b->empty_file, NULL,
                                       svn_io_file_del_on_pool_cleanup,
                                       b->pool, b->pool));
    }

  *empty_file = b->empty_file;

  return SVN_NO_ERROR;
}


/* Return the value of the svn:mime-type property held in PROPS, or NULL
   if no such property exists. */
static const char *
get_prop_mimetype(apr_hash_t *props)
{
  const svn_string_t *mimetype_val;

  mimetype_val = apr_hash_get(props,
                              SVN_PROP_MIME_TYPE,
                              strlen(SVN_PROP_MIME_TYPE));
  return (mimetype_val) ? mimetype_val->data : NULL;
}


/* Set *MIMETYPE to the BASE version of the svn:mime-type property of
   file PATH, using ADM_ACCESS, or to NULL if no such property exists.
   BASEPROPS is optional: if present, use it to cache the BASE properties
   of the file.

   Return the property value and property hash allocated in POOL.
*/
static svn_error_t *
get_base_mimetype(const char **mimetype,
                  apr_hash_t **baseprops,
                  svn_wc_adm_access_t *adm_access,
                  const char *path,
                  apr_pool_t *pool)
{
  apr_hash_t *props = NULL;

  if (baseprops == NULL)
    baseprops = &props;

  if (*baseprops == NULL)
    SVN_ERR(svn_wc_get_prop_diffs(NULL, baseprops, path, adm_access, pool));

  *mimetype = get_prop_mimetype(*baseprops);

  return SVN_NO_ERROR;
}


/* Set *MIMETYPE to the WORKING version of the svn:mime-type property
   of file PATH, using ADM_ACCESS, or to NULL if no such property exists.
   WORKINGPROPS is optional: if present, use it to cache the WORKING
   properties of the file.

   Return the property value and property hash allocated in POOL.
*/
static svn_error_t *
get_working_mimetype(const char **mimetype,
                     apr_hash_t **workingprops,
                     svn_wc_adm_access_t *adm_access,
                     const char *path,
                     apr_pool_t *pool)
{
  apr_hash_t *props = NULL;

  if (workingprops == NULL)
    workingprops = &props;

  if (*workingprops == NULL)
    SVN_ERR(svn_wc_prop_list(workingprops, path, adm_access, pool));

  *mimetype = get_prop_mimetype(*workingprops);

  return SVN_NO_ERROR;
}

/* Return the property hash resulting from combining PROPS and PROPCHANGES.
 *
 * A note on pool usage: The returned hash and hash keys are allocated in
 * the same pool as PROPS, but the hash values will be taken directly from
 * either PROPS or PROPCHANGES, as appropriate.  Caller must therefore
 * ensure that the returned hash is only used for as long as PROPS and
 * PROPCHANGES remain valid.
 */
static apr_hash_t *
apply_propchanges(apr_hash_t *props,
                  apr_array_header_t *propchanges)
{
  apr_hash_t *newprops = apr_hash_copy(apr_hash_pool_get(props), props);
  int i;

  for (i = 0; i < propchanges->nelts; ++i)
    {
      const svn_prop_t *prop = &APR_ARRAY_IDX(propchanges, i, svn_prop_t);
      apr_hash_set(newprops, prop->name, APR_HASH_KEY_STRING, prop->value);
    }

  return newprops;
}


/* Called by directory_elements_diff when a file is to be compared. At this
 * stage we are dealing with a file that does exist in the working copy.
 *
 * DIR_BATON is the parent directory baton, PATH is the path to the file to
 * be compared. ENTRY is the working copy entry for the file.
 *
 * Do all allocation in POOL.
 *
 * ### TODO: Need to work on replace if the new filename used to be a
 * directory.
 */
static svn_error_t *
file_diff(struct dir_baton *dir_baton,
          const char *path,
          const svn_wc_entry_t *entry,
          apr_pool_t *pool)
{
  struct edit_baton *eb = dir_baton->edit_baton;
  const char *textbase, *empty_file;
  svn_boolean_t modified;
  enum svn_wc_schedule_t schedule = entry->schedule;
  svn_boolean_t copied = entry->copied;
  svn_wc_adm_access_t *adm_access;
  const char *base_mimetype, *working_mimetype;
  const char *translated = NULL;
  apr_array_header_t *propchanges = NULL;
  apr_hash_t *baseprops = NULL;

  SVN_ERR_ASSERT(! eb->use_text_base);

  SVN_ERR(svn_wc_adm_retrieve(&adm_access, dir_baton->edit_baton->anchor,
                              dir_baton->path, pool));

  /* If the item is not a member of a specified changelist (and there are
     some specified changelists), skip it. */
  if (! SVN_WC__CL_MATCH(dir_baton->edit_baton->changelist_hash, entry))
    return SVN_NO_ERROR;

  /* If the item is schedule-add *with history*, then we don't want to
     see a comparison to the empty file;  we want the usual working
     vs. text-base comparision. */
  if (copied)
    schedule = svn_wc_schedule_normal;

  /* If this was scheduled replace and we are ignoring ancestry,
     report it as a normal file modification. */
  if (eb->ignore_ancestry && (schedule == svn_wc_schedule_replace))
    schedule = svn_wc_schedule_normal;

  /* Prep these two paths early. */
  textbase = svn_wc__text_base_path(path, FALSE, pool);

  /* If the regular text base is not there, we fall back to the revert
     text base (if that's not present either, we'll error later).  But
     the logic here is subtler than one might at first expect.

     When the file has some non-replacement scheduling, then it can be
     expected to still have its regular text base.  But what about
     when it's replaced or replaced-with-history?  In both cases, a
     revert text-base will be present; in the latter case only, a
     regular text-base be present as well.  So which text-base do we
     want to use for the diff?

     One could argue that we should never diff against the revert
     base, and instead diff against the empty-file for both types of
     replacement.  After all, there is no ancestry relationship
     between the working file and the base file.  But my guess is that
     in practice, users want to see the diff between their working
     file and "the nearest versioned thing", whatever that is.  I'm
     not 100% sure this is the right decision, but it at least seems
     to match our test suite's expectations. */
  {
    svn_node_kind_t kind;
    SVN_ERR(svn_io_check_path(textbase, &kind, pool));
    if (kind == svn_node_none)
      textbase = svn_wc__text_revert_path(path, pool);
  }

  SVN_ERR(get_empty_file(eb, &empty_file));

  /* Get property diffs if this is not schedule delete. */
  if (schedule != svn_wc_schedule_delete)
    {
      SVN_ERR(svn_wc_props_modified_p(&modified, path, adm_access, pool));
      if (modified)
        SVN_ERR(svn_wc_get_prop_diffs(&propchanges, &baseprops, path,
                                      adm_access, pool));
      else
        propchanges = apr_array_make(pool, 1, sizeof(svn_prop_t));
    }
  else
    {
      SVN_ERR(svn_wc_get_prop_diffs(NULL, &baseprops, path,
                                    adm_access, pool));
    }

  switch (schedule)
    {
      /* Replace is treated like a delete plus an add: two
         comparisons are generated, first one for the delete and
         then one for the add. */
    case svn_wc_schedule_replace:
    case svn_wc_schedule_delete:
      /* Delete compares text-base against empty file, modifications to the
         working-copy version of the deleted file are not wanted. */

      /* Get svn:mime-type from BASE props of PATH. */
      SVN_ERR(get_base_mimetype(&base_mimetype, &baseprops,
                                adm_access, path, pool));

      SVN_ERR(dir_baton->edit_baton->callbacks->file_deleted
              (NULL, NULL, NULL, path,
               textbase,
               empty_file,
               base_mimetype,
               NULL,
               baseprops,
               dir_baton->edit_baton->callback_baton));

      /* Replace will fallthrough! */
      if (schedule == svn_wc_schedule_delete)
        break;

    case svn_wc_schedule_add:
      /* Get svn:mime-type from working props of PATH. */
      SVN_ERR(get_working_mimetype(&working_mimetype, NULL,
                                   adm_access, path, pool));

      SVN_ERR(svn_wc_translated_file2
              (&translated, path, path, adm_access,
               SVN_WC_TRANSLATE_TO_NF
               | SVN_WC_TRANSLATE_USE_GLOBAL_TMP,
               pool));

      SVN_ERR(dir_baton->edit_baton->callbacks->file_added
              (NULL, NULL, NULL, NULL, path,
               empty_file,
               translated,
               0, entry->revision,
               NULL,
               working_mimetype,
               propchanges, baseprops,
               dir_baton->edit_baton->callback_baton));

      break;

    default:
      SVN_ERR(svn_wc_text_modified_p(&modified, path, FALSE,
                                     adm_access, pool));
      if (modified)
        {
          /* Note that this might be the _second_ time we translate
             the file, as svn_wc_text_modified_p() might have used a
             tmp translated copy too.  But what the heck, diff is
             already expensive, translating twice for the sake of code
             modularity is liveable. */
          SVN_ERR(svn_wc_translated_file2
                  (&translated, path,
                   path, adm_access,
                   SVN_WC_TRANSLATE_TO_NF
                   | SVN_WC_TRANSLATE_USE_GLOBAL_TMP,
                   pool));
        }

      if (modified || propchanges->nelts > 0)
        {
          /* Get svn:mime-type for both base and working file. */
          SVN_ERR(get_base_mimetype(&base_mimetype, &baseprops,
                                    adm_access, path, pool));
          SVN_ERR(get_working_mimetype(&working_mimetype, NULL,
                                       adm_access, path, pool));

          SVN_ERR(dir_baton->edit_baton->callbacks->file_changed
                  (NULL, NULL, NULL, NULL,
                   path,
                   modified ? textbase : NULL,
                   translated,
                   entry->revision,
                   SVN_INVALID_REVNUM,
                   base_mimetype,
                   working_mimetype,
                   propchanges, baseprops,
                   dir_baton->edit_baton->callback_baton));
        }
    }
  return SVN_NO_ERROR;
}

/* Called when the directory is closed to compare any elements that have
 * not yet been compared.  This identifies local, working copy only
 * changes.  At this stage we are dealing with files/directories that do
 * exist in the working copy.
 *
 * DIR_BATON is the baton for the directory.
 */
static svn_error_t *
directory_elements_diff(struct dir_baton *dir_baton)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  const svn_wc_entry_t *this_dir_entry;
  svn_boolean_t in_anchor_not_target;
  apr_pool_t *subpool;
  svn_wc_adm_access_t *adm_access;

  /* This directory should have been unchanged or replaced, not added,
     since an added directory can only contain added files and these will
     already have been compared. */
  SVN_ERR_ASSERT(!dir_baton->added);

  /* Everything we do below is useless if we are comparing to BASE. */
  if (dir_baton->edit_baton->use_text_base)
    return SVN_NO_ERROR;

  /* Determine if this is the anchor directory if the anchor is different
     to the target. When the target is a file, the anchor is the parent
     directory and if this is that directory the non-target entries must be
     skipped. */
  in_anchor_not_target =
    (*dir_baton->edit_baton->target
     && (! svn_path_compare_paths
         (dir_baton->path,
          svn_wc_adm_access_path(dir_baton->edit_baton->anchor))));

  SVN_ERR(svn_wc_adm_retrieve(&adm_access, dir_baton->edit_baton->anchor,
                              dir_baton->path, dir_baton->pool));

  SVN_ERR(svn_wc_entries_read(&entries, adm_access, FALSE, dir_baton->pool));
  this_dir_entry = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR,
                                APR_HASH_KEY_STRING);

  /* Check for local property mods on this directory, if we haven't
     already reported them and we aren't changelist-filted. */
  if (SVN_WC__CL_MATCH(dir_baton->edit_baton->changelist_hash, this_dir_entry)
      && (! in_anchor_not_target)
      && (! apr_hash_get(dir_baton->compared, "", 0)))
    {
      svn_boolean_t modified;

      SVN_ERR(svn_wc_props_modified_p(&modified,
                                      dir_baton->path, adm_access,
                                      dir_baton->pool));
      if (modified)
        {
          apr_array_header_t *propchanges;
          apr_hash_t *baseprops;

          SVN_ERR(svn_wc_get_prop_diffs(&propchanges, &baseprops,
                                        dir_baton->path, adm_access,
                                        dir_baton->pool));

          SVN_ERR(dir_baton->edit_baton->callbacks->dir_props_changed
                  (adm_access, NULL, NULL,
                   dir_baton->path,
                   propchanges, baseprops,
                   dir_baton->edit_baton->callback_baton));
        }
    }

  if (dir_baton->depth == svn_depth_empty && !in_anchor_not_target)
    return SVN_NO_ERROR;

  subpool = svn_pool_create(dir_baton->pool);

  for (hi = apr_hash_first(dir_baton->pool, entries); hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const svn_wc_entry_t *entry;
      struct dir_baton *subdir_baton;
      const char *name, *path;

      svn_pool_clear(subpool);

      apr_hash_this(hi, &key, NULL, &val);
      name = key;
      entry = val;

      /* Skip entry for the directory itself. */
      if (strcmp(key, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      /* In the anchor directory, if the anchor is not the target then all
         entries other than the target should not be diff'd. Running diff
         on one file in a directory should not diff other files in that
         directory. */
      if (in_anchor_not_target
          && strcmp(dir_baton->edit_baton->target, name))
        continue;

      path = svn_path_join(dir_baton->path, name, subpool);

      /* Skip entry if it is in the list of entries already diff'd. */
      if (apr_hash_get(dir_baton->compared, path, APR_HASH_KEY_STRING))
        continue;

      switch (entry->kind)
        {
        case svn_node_file:
          SVN_ERR(file_diff(dir_baton, path, entry, subpool));
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
          if (in_anchor_not_target
              || (dir_baton->depth > svn_depth_files)
              || (dir_baton->depth == svn_depth_unknown))
            {
              svn_depth_t depth_below_here = dir_baton->depth;

              if (depth_below_here == svn_depth_immediates)
                depth_below_here = svn_depth_empty;

              subdir_baton = make_dir_baton(path, dir_baton,
                                            dir_baton->edit_baton,
                                            FALSE, depth_below_here,
                                            subpool);

              SVN_ERR(directory_elements_diff(subdir_baton));
            }
          break;

        default:
          break;
        }
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Report an existing file in the working copy (either in BASE or WORKING)
 * as having been added.
 *
 * DIR_BATON is the parent directory baton, ADM_ACCESS/PATH is the path
 * to the file to be compared. ENTRY is the working copy entry for
 * the file.
 *
 * Do all allocation in POOL.
 */
static svn_error_t *
report_wc_file_as_added(struct dir_baton *dir_baton,
                        svn_wc_adm_access_t *adm_access,
                        const char *path,
                        const svn_wc_entry_t *entry,
                        apr_pool_t *pool)
{
  struct edit_baton *eb = dir_baton->edit_baton;
  apr_hash_t *emptyprops;
  const char *mimetype;
  apr_hash_t *wcprops = NULL;
  apr_array_header_t *propchanges;
  const char *empty_file;
  const char *source_file;
  const char *translated_file;

  /* If this entry is filtered by changelist specification, do nothing. */
  if (! SVN_WC__CL_MATCH(dir_baton->edit_baton->changelist_hash, entry))
    return SVN_NO_ERROR;

  SVN_ERR(get_empty_file(eb, &empty_file));

  /* We can't show additions for files that don't exist. */
  SVN_ERR_ASSERT(!(entry->schedule == svn_wc_schedule_delete && !eb->use_text_base));

  /* If the file was added *with history*, then we don't want to
     see a comparison to the empty file;  we want the usual working
     vs. text-base comparision. */
  if (entry->copied)
    {
      /* Don't show anything if we're comparing to BASE, since by
         definition there can't be any local modifications. */
      if (eb->use_text_base)
        return SVN_NO_ERROR;

      /* Otherwise show just the local modifications. */
      return file_diff(dir_baton, path, entry, pool);
    }

  emptyprops = apr_hash_make(pool);

  if (eb->use_text_base)
    SVN_ERR(get_base_mimetype(&mimetype, &wcprops,
                              adm_access, path, pool));
  else
    SVN_ERR(get_working_mimetype(&mimetype, &wcprops,
                                 adm_access, path, pool));

  SVN_ERR(svn_prop_diffs(&propchanges,
                         wcprops, emptyprops, pool));


  if (eb->use_text_base)
    source_file = svn_wc__text_base_path(path, FALSE, pool);
  else
    source_file = path;

  SVN_ERR(svn_wc_translated_file2
          (&translated_file,
           source_file, path, adm_access,
           SVN_WC_TRANSLATE_TO_NF
           | SVN_WC_TRANSLATE_USE_GLOBAL_TMP,
           pool));

  return eb->callbacks->file_added
          (adm_access, NULL, NULL, NULL,
           path,
           empty_file, translated_file,
           0, entry->revision,
           NULL, mimetype,
           propchanges, emptyprops,
           eb->callback_baton);
}

/* Report an existing directory in the working copy (either in BASE
 * or WORKING) as having been added.  If recursing, also report any
 * subdirectories as added.
 *
 * DIR_BATON is the baton for the directory.
 *
 * Do all allocation in POOL.
 */
static svn_error_t *
report_wc_directory_as_added(struct dir_baton *dir_baton,
                             apr_pool_t *pool)
{
  struct edit_baton *eb = dir_baton->edit_baton;
  svn_wc_adm_access_t *adm_access;
  apr_hash_t *emptyprops = apr_hash_make(pool), *wcprops = NULL;
  const svn_wc_entry_t *this_dir_entry;
  apr_array_header_t *propchanges;
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  apr_pool_t *subpool;

  SVN_ERR(svn_wc_adm_retrieve(&adm_access, eb->anchor,
                              dir_baton->path, pool));

  SVN_ERR(svn_wc_entries_read(&entries, adm_access, FALSE, pool));
  this_dir_entry = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR,
                                APR_HASH_KEY_STRING);

  /* If this directory passes changelist filtering, get its BASE or
     WORKING properties, as appropriate, and simulate their
     addition. */
  if (SVN_WC__CL_MATCH(dir_baton->edit_baton->changelist_hash, this_dir_entry))
    {
      if (eb->use_text_base)
        SVN_ERR(svn_wc_get_prop_diffs(NULL, &wcprops,
                                      dir_baton->path, adm_access, pool));
      else
        SVN_ERR(svn_wc_prop_list(&wcprops,
                                 dir_baton->path, adm_access, pool));

      SVN_ERR(svn_prop_diffs(&propchanges,
                             wcprops, emptyprops, pool));

      if (propchanges->nelts > 0)
        SVN_ERR(eb->callbacks->dir_props_changed
                (adm_access, NULL, NULL,
                 dir_baton->path,
                 propchanges, emptyprops,
                 eb->callback_baton));
    }

  /* Report the addition of the directory's contents. */
  subpool = svn_pool_create(pool);

  for (hi = apr_hash_first(pool, entries); hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *name, *path;
      const svn_wc_entry_t *entry;

      svn_pool_clear(subpool);

      apr_hash_this(hi, &key, NULL, &val);
      name = key;
      entry = val;

      /* Skip entry for the directory itself. */
      if (strcmp(key, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      /* If comparing against WORKING, skip entries that are
         schedule-deleted - they don't really exist. */
      if (!eb->use_text_base && entry->schedule == svn_wc_schedule_delete)
        continue;

      path = svn_path_join(dir_baton->path, name, subpool);

      switch (entry->kind)
        {
        case svn_node_file:
          SVN_ERR(report_wc_file_as_added(dir_baton,
                                          adm_access, path, entry, subpool));
          break;

        case svn_node_dir:
          if (dir_baton->depth > svn_depth_files
              || dir_baton->depth == svn_depth_unknown)
            {
              svn_depth_t depth_below_here = dir_baton->depth;
              struct dir_baton *subdir_baton;

              if (depth_below_here == svn_depth_immediates)
                depth_below_here = svn_depth_empty;

              subdir_baton = make_dir_baton(path, dir_baton, eb, FALSE,
                                            depth_below_here, subpool);

              SVN_ERR(report_wc_directory_as_added(subdir_baton, subpool));
            }
          break;

        default:
          break;
        }
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* An editor function. */
static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  eb->revnum = target_revision;

  return SVN_NO_ERROR;
}

/* An editor function. The root of the comparison hierarchy */
static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *dir_pool,
          void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *b;

  eb->root_opened = TRUE;
  b = make_dir_baton(eb->anchor_path, NULL, eb, FALSE, eb->depth, dir_pool);
  *root_baton = b;

  return SVN_NO_ERROR;
}

/* An editor function. */
static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t base_revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  const svn_wc_entry_t *entry;
  struct dir_baton *b;
  const char *empty_file;
  const char *full_path = svn_path_join(pb->edit_baton->anchor_path, path,
                                        pb->pool);
  svn_wc_adm_access_t *adm_access;

  SVN_ERR(svn_wc_adm_probe_retrieve(&adm_access, pb->edit_baton->anchor,
                                    full_path, pool));
  SVN_ERR(svn_wc_entry(&entry, full_path, adm_access, FALSE, pool));

  /* So, it turns out that this can be NULL in at least one actual case,
     if you do a nonrecursive checkout and the diff involves the addition
     of one of the directories that is not present due to the fact that
     your checkout is nonrecursive.  There isn't really a good way to be
     sure though, since nonrecursive checkouts suck, and don't leave any
     indication in .svn/entries that the directories in question are just
     missing. */
  if (! entry)
    return SVN_NO_ERROR;

  /* Mark this entry as compared in the parent directory's baton. */
  apr_hash_set(pb->compared, full_path, APR_HASH_KEY_STRING, "");

  /* If comparing against WORKING, skip entries that are schedule-deleted
     - they don't really exist. */
  if (!eb->use_text_base && entry->schedule == svn_wc_schedule_delete)
    return SVN_NO_ERROR;

  SVN_ERR(get_empty_file(pb->edit_baton, &empty_file));
  switch (entry->kind)
    {
    case svn_node_file:
      /* A delete is required to change working-copy into requested
         revision, so diff should show this as an add. Thus compare
         the empty file against the current working copy.  If
         'reverse_order' is set, then show a deletion. */

      if (eb->reverse_order)
        {
          /* Whenever showing a deletion, we show the text-base vanishing. */
          /* ### This is wrong if we're diffing WORKING->repos. */
          const char *textbase = svn_wc__text_base_path(full_path, FALSE,
                                                        pool);
          apr_hash_t *baseprops = NULL;
          const char *base_mimetype;

          SVN_ERR(get_base_mimetype(&base_mimetype, &baseprops,
                                    adm_access, full_path, pool));

          SVN_ERR(pb->edit_baton->callbacks->file_deleted
                  (NULL, NULL, NULL, full_path,
                   textbase,
                   empty_file,
                   base_mimetype,
                   NULL,
                   baseprops,
                   pb->edit_baton->callback_baton));
        }
      else
        {
          /* Or normally, show the working file being added. */
          SVN_ERR(report_wc_file_as_added(pb, adm_access, full_path, entry,
                                          pool));
        }
      break;

    case svn_node_dir:
      b = make_dir_baton(full_path, pb, pb->edit_baton, FALSE,
                         svn_depth_infinity, pool);
      /* A delete is required to change working-copy into requested
         revision, so diff should show this as an add. */
      SVN_ERR(report_wc_directory_as_added(b, pool));
      break;

    default:
      break;
    }

  return SVN_NO_ERROR;
}

/* An editor function. */
static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *dir_pool,
              void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct dir_baton *b;
  const char *full_path;
  svn_depth_t subdir_depth = (pb->depth == svn_depth_immediates)
                              ? svn_depth_empty : pb->depth;

  /* ### TODO: support copyfrom? */

  full_path = svn_path_join(pb->edit_baton->anchor_path, path, dir_pool);
  b = make_dir_baton(full_path, pb, pb->edit_baton, TRUE,
                     subdir_depth, dir_pool);
  *child_baton = b;

  return SVN_NO_ERROR;
}

/* An editor function. */
static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *dir_pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct dir_baton *b;
  const char *full_path;
  svn_depth_t subdir_depth = (pb->depth == svn_depth_immediates)
                              ? svn_depth_empty : pb->depth;

  /* Allocate path from the parent pool since the memory is used in the
     parent's compared hash */
  full_path = svn_path_join(pb->edit_baton->anchor_path, path, pb->pool);
  b = make_dir_baton(full_path, pb, pb->edit_baton, FALSE,
                     subdir_depth, dir_pool);
  *child_baton = b;

  return SVN_NO_ERROR;
}

/* An editor function.  When a directory is closed, all the directory
 * elements that have been added or replaced will already have been
 * diff'd. However there may be other elements in the working copy
 * that have not yet been considered.  */
static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton *b = dir_baton;
  struct dir_baton *pb = b->dir_baton;

  /* Report the property changes on the directory itself, if necessary. */
  if (b->propchanges->nelts > 0)
    {
      /* The working copy properties at the base of the wc->repos comparison:
         either BASE or WORKING. */
      apr_hash_t *originalprops;

      if (b->added)
        {
          originalprops = apr_hash_make(b->pool);
        }
      else
        {
          svn_wc_adm_access_t *adm_access;

          SVN_ERR(svn_wc_adm_retrieve(&adm_access,
                                      b->edit_baton->anchor, b->path,
                                      b->pool));

          if (b->edit_baton->use_text_base)
            {
              SVN_ERR(svn_wc_get_prop_diffs(NULL, &originalprops,
                                            b->path, adm_access, pool));
            }
          else
            {
              apr_hash_t *base_props, *repos_props;

              SVN_ERR(svn_wc_prop_list(&originalprops, b->path,
                                       b->edit_baton->anchor, pool));

              /* Load the BASE and repository directory properties. */
              SVN_ERR(svn_wc_get_prop_diffs(NULL, &base_props,
                                            b->path, adm_access, pool));

              repos_props = apply_propchanges(base_props, b->propchanges);

              /* Recalculate b->propchanges as the change between WORKING
                 and repos. */
              SVN_ERR(svn_prop_diffs(&b->propchanges,
                                     repos_props, originalprops, b->pool));
            }
        }

      if (! b->edit_baton->reverse_order)
        reverse_propchanges(originalprops, b->propchanges, b->pool);

      SVN_ERR(b->edit_baton->callbacks->dir_props_changed
              (NULL, NULL, NULL,
               b->path,
               b->propchanges,
               originalprops,
               b->edit_baton->callback_baton));

      /* Mark the properties of this directory as having already been
         compared so that we know not to show any local modifications
         later on. */
      apr_hash_set(b->compared, "", 0, "");
    }

  /* Report local modifications for this directory.  Skip added
     directories since they can only contain added elements, all of
     which have already been diff'd. */
  if (!b->added)
    SVN_ERR(directory_elements_diff(dir_baton));

  /* Mark this directory as compared in the parent directory's baton,
     unless this is the root of the comparison. */
  if (pb)
    apr_hash_set(pb->compared, b->path, APR_HASH_KEY_STRING, "");

  return SVN_NO_ERROR;
}

/* An editor function. */
static svn_error_t *
add_file(const char *path,
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

  full_path = svn_path_join(pb->edit_baton->anchor_path, path, file_pool);
  b = make_file_baton(full_path, TRUE, pb, file_pool);
  *file_baton = b;

  /* Add this filename to the parent directory's list of elements that
     have been compared. */
  apr_hash_set(pb->compared, apr_pstrdup(pb->pool, full_path),
               APR_HASH_KEY_STRING, "");

  return SVN_NO_ERROR;
}

/* An editor function. */
static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *file_pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *b;
  const char *full_path;

  full_path = svn_path_join(pb->edit_baton->anchor_path, path, file_pool);
  b = make_file_baton(full_path, FALSE, pb, file_pool);
  *file_baton = b;

  /* Add this filename to the parent directory's list of elements that
     have been compared. */
  apr_hash_set(pb->compared, apr_pstrdup(pb->pool, full_path),
               APR_HASH_KEY_STRING, "");

  return SVN_NO_ERROR;
}

/* Do the work of applying the text delta. */
static svn_error_t *
window_handler(svn_txdelta_window_t *window,
               void *window_baton)
{
  struct file_baton *b = window_baton;

  return b->apply_handler(window, b->apply_baton);
}

/* An editor function. */
static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct file_baton *b = file_baton;
  struct edit_baton *eb = b->edit_baton;
  const svn_wc_entry_t *entry;
  const char *parent, *base_name;
  svn_stream_t *source;
  apr_file_t *temp_file;

  SVN_ERR(svn_wc_entry(&entry, b->wc_path, eb->anchor, FALSE, b->pool));

  svn_path_split(b->wc_path, &parent, &base_name, b->pool);

  /* Check to see if there is a schedule-add with history entry in
     the current working copy.  If so, then this is not actually
     an add, but instead a modification.*/
  if (entry && entry->copyfrom_url)
    b->added = FALSE;

  if (b->added)
    {
      source = svn_stream_empty(pool);
    }
  else
    {
      /* The current text-base is the starting point if replacing */
      SVN_ERR(svn_wc_get_pristine_contents(&source, b->path, b->pool, b->pool));
    }

  /* This is the file that will contain the pristine repository version. It
     is created in the admin temporary area. This file continues to exists
     until after the diff callback is run, at which point it is deleted. */
  SVN_ERR(svn_wc_create_tmp_file2(&temp_file, &b->temp_file_path,
                                  parent, svn_io_file_del_on_pool_cleanup,
                                  b->pool));

  svn_txdelta_apply(source,
                    svn_stream_from_aprfile2(temp_file, FALSE, b->pool),
                    NULL,
                    b->temp_file_path /* error_info */,
                    b->pool,
                    &b->apply_handler, &b->apply_baton);

  *handler = window_handler;
  *handler_baton = file_baton;
  return SVN_NO_ERROR;
}

/* An editor function.  When the file is closed we have a temporary
 * file containing a pristine version of the repository file. This can
 * be compared against the working copy.
 *
 * Ignore TEXT_CHECKSUM.
 */
static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  struct file_baton *b = file_baton;
  struct edit_baton *eb = b->edit_baton;
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  const char *repos_mimetype;
  const char *empty_file;

  /* The BASE and repository properties of the file. */
  apr_hash_t *base_props;
  apr_hash_t *repos_props;

  /* The path to the wc file: either BASE or WORKING. */
  const char *localfile;
  /* The path to the temporary copy of the pristine repository version. */
  const char *temp_file_path;
  svn_boolean_t modified;
  /* The working copy properties at the base of the wc->repos
     comparison: either BASE or WORKING. */
  apr_hash_t *originalprops;


  SVN_ERR(svn_wc_adm_probe_retrieve(&adm_access, b->edit_baton->anchor,
                                    b->wc_path, b->pool));
  SVN_ERR(svn_wc_entry(&entry, b->wc_path, adm_access, FALSE, b->pool));

  SVN_ERR(get_empty_file(b->edit_baton, &empty_file));


  /* Load the BASE and repository file properties. */
  if (b->added)
    base_props = apr_hash_make(pool);
  else
    SVN_ERR(svn_wc_get_prop_diffs(NULL, &base_props,
                                  b->path, adm_access, pool));

  repos_props = apply_propchanges(base_props, b->propchanges);

  repos_mimetype = get_prop_mimetype(repos_props);


  /* The repository version of the file is in the temp file we applied
     the BASE->repos delta to.  If we haven't seen any changes, it's
     the same as BASE. */
  temp_file_path = b->temp_file_path;
  if (!temp_file_path)
    temp_file_path = svn_wc__text_base_path(b->path, FALSE, b->pool);


  /* If the file isn't in the working copy (either because it was added
     in the BASE->repos diff or because we're diffing against WORKING
     and it was marked as schedule-deleted), we show either an addition
     or a deletion of the complete contents of the repository file,
     depending upon the direction of the diff. */
  if (b->added ||
      (!eb->use_text_base && entry->schedule == svn_wc_schedule_delete))
    {
      if (eb->reverse_order)
        return b->edit_baton->callbacks->file_added
                (NULL, NULL, NULL, NULL, b->path,
                 empty_file,
                 temp_file_path,
                 0,
                 eb->revnum,
                 NULL,
                 repos_mimetype,
                 b->propchanges,
                 apr_hash_make(pool),
                 b->edit_baton->callback_baton);
      else
          return b->edit_baton->callbacks->file_deleted
                  (NULL, NULL, NULL, b->path,
                   temp_file_path,
                   empty_file,
                   repos_mimetype,
                   NULL,
                   repos_props,
                   b->edit_baton->callback_baton);
    }

  /* If we didn't see any content changes between the BASE and repository
     versions (i.e. we only saw property changes), then, if we're diffing
     against WORKING, we also need to check whether there are any local
     (BASE:WORKING) modifications. */
  modified = (b->temp_file_path != NULL);
  if (!modified && !eb->use_text_base)
    SVN_ERR(svn_wc_text_modified_p(&modified, b->path, FALSE,
                                   adm_access, pool));

  if (modified)
    {
      if (eb->use_text_base)
        localfile = svn_wc__text_base_path(b->path, FALSE, b->pool);
      else
        /* a detranslated version of the working file */
        SVN_ERR(svn_wc_translated_file2
                (&localfile, b->path,
                 b->path, adm_access,
                 SVN_WC_TRANSLATE_TO_NF
                 | SVN_WC_TRANSLATE_USE_GLOBAL_TMP,
                 pool));
    }
  else
    localfile = temp_file_path = NULL;

  if (eb->use_text_base)
    {
      originalprops = base_props;
    }
  else
    {
      SVN_ERR(svn_wc_prop_list(&originalprops,
                               b->path, adm_access, pool));

      /* We have the repository properties in repos_props, and the
         WORKING properties in originalprops.  Recalculate
         b->propchanges as the change between WORKING and repos. */
      SVN_ERR(svn_prop_diffs(&b->propchanges,
                             repos_props, originalprops, b->pool));
    }

  if (localfile || b->propchanges->nelts > 0)
    {
      const char *original_mimetype = get_prop_mimetype(originalprops);

      if (b->propchanges->nelts > 0
          && ! eb->reverse_order)
        reverse_propchanges(originalprops, b->propchanges, b->pool);

      SVN_ERR(b->edit_baton->callbacks->file_changed
              (NULL, NULL, NULL, NULL,
               b->path,
               eb->reverse_order ? localfile : temp_file_path,
               eb->reverse_order ? temp_file_path : localfile,
               eb->reverse_order ? SVN_INVALID_REVNUM : b->edit_baton->revnum,
               eb->reverse_order ? b->edit_baton->revnum : SVN_INVALID_REVNUM,
               eb->reverse_order ? original_mimetype : repos_mimetype,
               eb->reverse_order ? repos_mimetype : original_mimetype,
               b->propchanges, originalprops,
               b->edit_baton->callback_baton));
    }

  return SVN_NO_ERROR;
}


/* An editor function. */
static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton *b = file_baton;
  svn_prop_t *propchange;

  propchange = apr_array_push(b->propchanges);
  propchange->name = apr_pstrdup(b->pool, name);
  propchange->value = value ? svn_string_dup(value, b->pool) : NULL;

  return SVN_NO_ERROR;
}


/* An editor function. */
static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  svn_prop_t *propchange;

  propchange = apr_array_push(db->propchanges);
  propchange->name = apr_pstrdup(db->pool, name);
  propchange->value = value ? svn_string_dup(value, db->pool) : NULL;

  return SVN_NO_ERROR;
}


/* An editor function. */
static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  if (!eb->root_opened)
    {
      struct dir_baton *b;

      b = make_dir_baton(eb->anchor_path, NULL, eb, FALSE,
                         eb->depth, eb->pool);
      SVN_ERR(directory_elements_diff(b));
    }

  return SVN_NO_ERROR;
}

/* Public Interface */


/* Create a diff editor and baton. */
svn_error_t *
svn_wc_get_diff_editor5(svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks3_t *callbacks,
                        void *callback_baton,
                        svn_depth_t depth,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        const apr_array_header_t *changelists,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        apr_pool_t *pool)
{
  struct edit_baton *eb;
  void *inner_baton;
  svn_delta_editor_t *tree_editor;
  const svn_delta_editor_t *inner_editor;

  SVN_ERR(make_editor_baton(&eb, anchor, target, callbacks, callback_baton,
                            depth, ignore_ancestry, use_text_base,
                            reverse_order, changelists, pool));
  tree_editor = svn_delta_default_editor(eb->pool);

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

  inner_editor = tree_editor;
  inner_baton = eb;

  if (depth == svn_depth_unknown)
    SVN_ERR(svn_wc__ambient_depth_filter_editor(&inner_editor,
                                                &inner_baton,
                                                inner_editor,
                                                inner_baton,
                                                svn_wc_adm_access_path(anchor),
                                                target,
                                                anchor,
                                                pool));

  return svn_delta_get_cancellation_editor(cancel_func,
                                           cancel_baton,
                                           inner_editor,
                                           inner_baton,
                                           editor,
                                           edit_baton,
                                           pool);
}


/* Compare working copy against the text-base. */
svn_error_t *
svn_wc_diff5(svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks3_t *callbacks,
             void *callback_baton,
             svn_depth_t depth,
             svn_boolean_t ignore_ancestry,
             const apr_array_header_t *changelists,
             apr_pool_t *pool)
{
  struct edit_baton *eb;
  struct dir_baton *b;
  const svn_wc_entry_t *entry;
  const char *target_path;
  svn_wc_adm_access_t *adm_access;

  SVN_ERR(make_editor_baton(&eb, anchor, target, callbacks, callback_baton,
                            depth, ignore_ancestry, FALSE, FALSE,
                            changelists, pool));

  target_path = svn_path_join(svn_wc_adm_access_path(anchor), target,
                              eb->pool);

  SVN_ERR(svn_wc_adm_probe_retrieve(&adm_access, anchor, target_path,
                                    eb->pool));
  SVN_ERR(svn_wc__entry_versioned(&entry, target_path, adm_access, FALSE,
                                  eb->pool));

  if (entry->kind == svn_node_dir)
    b = make_dir_baton(target_path, NULL, eb, FALSE, depth, eb->pool);
  else
    b = make_dir_baton(eb->anchor_path, NULL, eb, FALSE, depth, eb->pool);

  return directory_elements_diff(b);
}
