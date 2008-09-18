/*
 * diff.c -- The diff editor for comparing the working copy against the
 *           repository.
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
#include <apr_md5.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "svn_md5.h"
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

  /* The stream attached to an @c svnpatch_file.  This is to avoid
   * allocating <tt>svn_stream_t *</tt> every now and then. */
  svn_stream_t *svnpatch_stream;

  /* The list of diffable files of interest to svnpatch. */
  apr_array_header_t *diff_targets;

  /* Diff editor baton */
  svn_delta_editor_t *diff_editor;

  /* A token holder, helps to build the svnpatch. */
  int next_token;

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

  /* Set when dealing with svnpatch diff. */
  const char *token;

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
  const char *temp_file_path;

  /* The list of incoming BASE->repos propchanges. */
  apr_array_header_t *propchanges;

  /* APPLY_HANDLER/APPLY_BATON represent the delta applcation baton. */
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;

  /* The overall crawler editor baton. */
  struct edit_baton *edit_baton;

  /* The directory that contains the file. */
  struct dir_baton *dir_baton;

  /* Set when dealing with svnpatch diff. */
  const char *token;

  apr_pool_t *pool;
};

/* Used to wrap svn_wc_diff_callbacks_t. */
struct callbacks_wrapper_baton {
  const svn_wc_diff_callbacks_t *callbacks;
  void *baton;
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
  eb->diff_targets = apr_array_make(pool, 1, sizeof(const char *));
  eb->next_token = 0;
  eb->svnpatch_stream = NULL;
  eb->changelist_hash = changelist_hash;
  eb->pool = pool;

  *edit_baton = eb;
  return SVN_NO_ERROR;
}

static const char *
make_token(char type,
           struct edit_baton *eb,
           apr_pool_t *pool)
{
  return apr_psprintf(pool, "%c%d", type, eb->next_token++);
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
               const char *token,
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
  dir_baton->token = token;

  return dir_baton;
}

/* Create a new file baton.  PATH is the file path, including
 * anchor_path.  ADDED is set if this file is being added rather than
 * replaced.  PARENT_BATON is the baton of the parent directory.  TOKEN
 * is used when dealing with svnpatch.  The directory and its parent
 * may or may not exist in the working copy.
 */
static struct file_baton *
make_file_baton(const char *path,
                svn_boolean_t added,
                struct dir_baton *parent_baton,
                const char *token,
                apr_pool_t *pool)
{
  struct file_baton *file_baton = apr_pcalloc(pool, sizeof(*file_baton));
  struct edit_baton *edit_baton = parent_baton->edit_baton;

  file_baton->edit_baton = edit_baton;
  file_baton->added = added;
  file_baton->pool = pool;
  file_baton->propchanges  = apr_array_make(pool, 1, sizeof(svn_prop_t));
  file_baton->path = path;
  file_baton->token = token;
  file_baton->dir_baton = parent_baton;

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
      const char *temp_dir;

      SVN_ERR(svn_io_temp_dir(&temp_dir, b->pool));
      SVN_ERR(svn_io_open_unique_file2
              (NULL, &(b->empty_file),
               svn_path_join(temp_dir, "tmp", b->pool),
               "", svn_io_file_del_on_pool_cleanup,
               b->pool));
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
  svn_boolean_t file_to_diff = FALSE; /* whether or not to push to diffables */

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
    {
      const svn_wc_entry_t *parent_entry;

      schedule = svn_wc_schedule_normal;

      /* Regarding svnpatch, when this file is only copied we only want
       * to consider it when its parent was *not* copied as well.  This
       * allows loose fuzzing and prevent adding the file to diffables
       * when it doesn't need to.  This is also true for move ops. */
      SVN_ERR(svn_wc_entry(&parent_entry, dir_baton->path,
                           adm_access, TRUE, dir_baton->pool));

      if (! parent_entry->copied)
        file_to_diff = TRUE;
    }

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
      textbase = svn_wc__text_revert_path(path, FALSE, pool);
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
              (NULL, NULL, path,
               textbase,
               empty_file,
               base_mimetype,
               NULL,
               baseprops,
               dir_baton->edit_baton->callback_baton));

      file_to_diff = TRUE;

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
              (NULL, NULL, NULL, path,
               empty_file,
               translated,
               0, entry->revision,
               NULL,
               working_mimetype,
               NULL, SVN_INVALID_REVNUM, /* XXX make use of new 1.6 API */
               propchanges, baseprops,
               dir_baton->edit_baton->callback_baton));

      file_to_diff = TRUE;

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
          svn_boolean_t mt_binary, mt1_binary, mt2_binary;

          /* Get svn:mime-type for both base and working file. */
          SVN_ERR(get_base_mimetype(&base_mimetype, &baseprops,
                                    adm_access, path, pool));
          SVN_ERR(get_working_mimetype(&working_mimetype, NULL,
                                       adm_access, path, pool));

          SVN_ERR(dir_baton->edit_baton->callbacks->file_changed
                  (NULL, NULL, NULL,
                   path,
                   modified ? textbase : NULL,
                   translated,
                   entry->revision,
                   SVN_INVALID_REVNUM,
                   base_mimetype,
                   working_mimetype,
                   propchanges, baseprops,
                   dir_baton->edit_baton->callback_baton));

          /* As far as svnpatch is concerned, we only grab binary stuff
           * and prop changes here.  In other words, no grab if this is
           * a non-binary file that carries text changes only as this
           * is left to the unidiff part. */
          mt1_binary = mt2_binary = FALSE;
          if (base_mimetype)
            mt1_binary = svn_mime_type_is_binary(base_mimetype);
          if (working_mimetype)
            mt1_binary = svn_mime_type_is_binary(working_mimetype);

          mt_binary = (mt1_binary || mt2_binary);
          if (mt_binary || (propchanges->nelts > 0 && ! mt_binary))
            file_to_diff = TRUE;
        }
    }

  if (file_to_diff)
    APR_ARRAY_PUSH(eb->diff_targets, const char *)
      = apr_pstrdup(eb->pool, path);

  return SVN_NO_ERROR;
}

/* Called from directory_elements_diff to trigger callbacks when
 * svnpatch format is enabled. */
static svn_error_t *
dir_diff(struct dir_baton *dir_baton,
         const char *path,
         const svn_wc_entry_t *entry,
         apr_pool_t *pool)
{
  struct edit_baton *eb = dir_baton->edit_baton;
  svn_boolean_t dir_to_diff = FALSE; /* whether or not to push to diffables */

  switch (entry->schedule)
    {
      case svn_wc_schedule_replace:
      case svn_wc_schedule_delete:
        SVN_ERR(eb->callbacks->dir_deleted
                (NULL,
                 NULL,
                 path,
                 eb->callback_baton));

        dir_to_diff = TRUE;

        /* So that 'replace' falls through */
        if (entry->schedule == svn_wc_schedule_delete)
          break;

      case svn_wc_schedule_add:
        SVN_ERR(eb->callbacks->dir_added
                (NULL,
                 NULL,
                 path,
                 entry->revision,
                 NULL, SVN_INVALID_REVNUM, /* XXX make use of new 1.6 API */
                 eb->callback_baton));

        dir_to_diff = TRUE;

        break;

      default:
        break;
    }

  if (dir_to_diff)
    APR_ARRAY_PUSH(eb->diff_targets, const char *)
      = apr_pstrdup(eb->pool, path);

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
  struct edit_baton *eb = dir_baton->edit_baton;

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
                  (adm_access, NULL,
                   dir_baton->path,
                   propchanges, baseprops,
                   dir_baton->edit_baton->callback_baton));

          if (eb->svnpatch_stream)
            {
              const svn_wc_entry_t *this_entry;
              SVN_ERR(svn_wc_entry(&this_entry, dir_baton->path,
                                   adm_access, TRUE, dir_baton->pool));

              /* If scheduled for addition, this dir has already
               * been pushed to our array, see dir_diff. */
              if (this_entry->schedule != svn_wc_schedule_add)
                APR_ARRAY_PUSH(eb->diff_targets, const char *)
                  = apr_pstrdup(eb->pool, dir_baton->path);
            }
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
                                            FALSE,
                                            NULL,
                                            depth_below_here,
                                            subpool);

              /* Before we recurse, let's trigger directory's callbacks */
              SVN_ERR(dir_diff(subdir_baton, path, entry, subpool));

              /* And if this is a schedule-delete directory, no need to
               * recurse any deeper down the rabbit hole. */
              if (entry->schedule == svn_wc_schedule_delete)
                break;

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

/* Drive @a editor against @a path's content modifications. */
static svn_error_t *
transmit_svndiff(const char *path,
                 svn_wc_adm_access_t *adm_access,
                 const svn_delta_editor_t *editor,
                 void *file_baton,
                 apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;
  svn_txdelta_window_handler_t handler;
  svn_txdelta_stream_t *txdelta_stream;
  svn_stream_t *base_stream;
  svn_stream_t *local_stream;
  void *wh_baton;

  /* Initialize window_handler/baton to produce svndiff from txdelta
   * windows. */
  SVN_ERR(eb->diff_editor->apply_textdelta
          (fb, NULL, pool, &handler, &wh_baton));

  base_stream = svn_stream_empty(pool);

  SVN_ERR(svn_wc_translated_stream(&local_stream, path, path,
                                   adm_access, SVN_WC_TRANSLATE_TO_NF,
                                   pool));

  svn_txdelta(&txdelta_stream, base_stream, local_stream, pool);
  SVN_ERR(svn_txdelta_send_txstream(txdelta_stream, handler,
                                    wh_baton, pool));
  return SVN_NO_ERROR;
}

/* Call @a change_prop_fn against the list of property changes @a
 * propchanges.  This can be used for files and directories as their
 * change_prop callbacks have the same prototype. */
static svn_error_t *
transmit_prop_deltas(apr_array_header_t *propchanges,
                     apr_hash_t *originalprops,
                     void *baton,
                     struct edit_baton *eb,
                     svn_error_t *(*change_prop_fn)
                                   (void *baton,
                                    const char *name,
                                    const svn_string_t *value,
                                    apr_pool_t *pool),
                     apr_pool_t *pool)
{
  int i;
  apr_array_header_t *props;

  SVN_ERR(svn_categorize_props(propchanges, NULL, NULL, &props, pool));
  for (i = 0; i < props->nelts; ++i)
    {
      const svn_string_t *original_value;
      const svn_prop_t *propchange
        = &APR_ARRAY_IDX(props, i, svn_prop_t);

      if (originalprops)
        original_value = apr_hash_get(originalprops, 
            propchange->name, APR_HASH_KEY_STRING);
      else
        original_value = NULL;

      /* The property was removed. */
      if (original_value != NULL)
        SVN_ERR(change_prop_fn(baton, propchange->name, NULL, pool));

      if (propchange->value != NULL)
        SVN_ERR(change_prop_fn(baton, propchange->name,
                               propchange->value, pool));
    }

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
  void *fb = NULL; /* file baton */
  apr_array_header_t *propchanges;
  const char *empty_file;
  const char *source_file;
  const char *translated_file;
  svn_boolean_t file_need_close = TRUE;

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

  SVN_ERR(eb->callbacks->file_added
          (adm_access, NULL, NULL,
           path,
           empty_file, translated_file,
           0, entry->revision,
           NULL, mimetype,
           NULL, SVN_INVALID_REVNUM, /* XXX make use of new 1.6 API */
           propchanges, emptyprops,
           eb->callback_baton));

  if (eb->svnpatch_stream)
    {
      /* There can't be any copy-path here since we're actually dealing with
       * a file that's already deleted in the future (yes Subversion is also
       * known as The Oracle).  This was reversed here into a file-addition
       * as we're ... returning from the future. */ 
      SVN_ERR(eb->diff_editor->add_file(path, dir_baton, NULL,
                                        SVN_INVALID_REVNUM, pool, &fb));
      if (propchanges->nelts > 0)
        SVN_ERR(svn_wc_transmit_prop_deltas
                (path, adm_access, entry, eb->diff_editor,
                 fb, NULL, pool));

      if (mimetype && svn_mime_type_is_binary(mimetype))
        {
          SVN_ERR(svn_wc_transmit_text_deltas2
                  (NULL,
                   NULL,/* TODO:digest bin stuff */
                   path, adm_access, TRUE,
                   eb->diff_editor, fb, pool));
          /* svn_wc_transmit_text_deltas2() does the close itself. */
          file_need_close = FALSE; 
        }

      if (file_need_close)
        SVN_ERR(eb->diff_editor->close_file(fb, NULL, pool));
    }

  return SVN_NO_ERROR;
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
                             const svn_wc_entry_t *dir_entry,
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

      if (eb->svnpatch_stream)
        {
          /* No copy-path, see report_wc_file_as_added() inner-docstrings. */
          SVN_ERR(eb->diff_editor->add_directory
                  (dir_baton->path, dir_baton->dir_baton, NULL,
                   SVN_INVALID_REVNUM, pool, (void **)&dir_baton));

          if (propchanges->nelts > 0)
            SVN_ERR(svn_wc_transmit_prop_deltas
                    (dir_baton->path, adm_access, dir_entry, eb->diff_editor,
                     dir_baton, NULL, pool));
        }

      if (propchanges->nelts > 0)
        SVN_ERR(eb->callbacks->dir_props_changed
                (adm_access, NULL,
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
                                            NULL, depth_below_here,
                                            subpool);

              SVN_ERR(report_wc_directory_as_added(subdir_baton, entry,
                                                   subpool));
            }
          break;

        default:
          break;
        }
    }
  
  svn_pool_destroy(subpool);

  if (eb->svnpatch_stream)
    SVN_ERR(eb->diff_editor->close_directory
            (dir_baton, pool));

  return SVN_NO_ERROR;
}


/* svnpatch-specific editor functions follow */

static svn_error_t *
svnpatch_open_root(void *edit_baton,
                   svn_revnum_t base_revision,
                   apr_pool_t *dir_pool,
                   void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  const char *token = make_token('d', eb, dir_pool);

  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "open-root", "c", token));

  eb->root_opened = TRUE;
  *root_baton = make_dir_baton(eb->anchor_path, NULL, eb,
                               FALSE, token, eb->depth, dir_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
svnpatch_open_directory(const char *path,
                        void *parent_baton,
                        svn_revnum_t base_revision,
                        apr_pool_t *dir_pool,
                        void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  const char *token = make_token('d', eb, dir_pool);
  svn_depth_t subdir_depth = (pb->depth == svn_depth_immediates)
                              ? svn_depth_empty : pb->depth;

  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "open-dir", "ccc", path, pb->token, token));
  *child_baton = make_dir_baton(path, pb, eb, FALSE, token,
                                subdir_depth, dir_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
svnpatch_close_directory(void *dir_baton,
                         apr_pool_t *pool)
{
  struct dir_baton *b = dir_baton;
  struct edit_baton *eb = b->edit_baton;

  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "close-dir", "c", b->token));
  return SVN_NO_ERROR;
}

static svn_error_t *
svnpatch_add_directory(const char *path,
                       void *parent_baton,
                       const char *copyfrom_path,
                       svn_revnum_t copyfrom_revision,
                       apr_pool_t *dir_pool,
                       void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  const char *token = make_token('d', eb, dir_pool);
  svn_depth_t subdir_depth = (pb->depth == svn_depth_immediates)
                              ? svn_depth_empty : pb->depth;

  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "add-dir", "ccc(?c)", path, pb->token,
                           token, copyfrom_path));
  *child_baton = make_dir_baton(path, pb, eb, TRUE, token,
                                subdir_depth, dir_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
svnpatch_change_dir_prop(void *dir_baton,
                         const char *name,
                         const svn_string_t *value,
                         apr_pool_t *pool)
{
  struct dir_baton *pb = dir_baton;
  struct edit_baton *eb = pb->edit_baton;

  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "change-dir-prop", "cc(?s)", pb->token, name,
                           value));
  return SVN_NO_ERROR;
}

static svn_error_t *
svnpatch_open_file(const char *path,
                   void *parent_baton,
                   svn_revnum_t base_revision,
                   apr_pool_t *file_pool,
                   void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  const char *token = make_token('c', eb, file_pool);

  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "open-file", "ccc", path, pb->token,
                           token));
  *file_baton = make_file_baton(path, FALSE, pb, token, file_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
svnpatch_add_file(const char *path,
                  void *parent_baton,
                  const char *copyfrom_path,
                  svn_revnum_t copyfrom_revision,
                  apr_pool_t *file_pool,
                  void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  const char *token = make_token('c', eb, file_pool);

  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "add-file", "ccc(?c)", path, pb->token,
                           token, copyfrom_path));
  *file_baton = make_file_baton(path, TRUE, pb, token, file_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
svnpatch_close_file(void *file_baton,
                    const char *text_checksum,
                    apr_pool_t *pool)
{
  struct file_baton *b = file_baton;
  struct edit_baton *eb = b->edit_baton;
  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "close-file", "c(?c)",
                           b->token, text_checksum));
  return SVN_NO_ERROR;
}

static svn_error_t *
svnpatch_change_file_prop(void *file_baton,
                          const char *name,
                          const svn_string_t *value,
                          apr_pool_t *pool)
{
  struct file_baton *b = file_baton;
  struct edit_baton *eb = b->edit_baton;

  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "change-file-prop", "cc(?s)",
                           b->token, name, value));
  return SVN_NO_ERROR;
}

static svn_error_t *
svnpatch_delete_entry(const char *path,
                      svn_revnum_t base_revision,
                      void *parent_baton,
                      apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  if (eb->reverse_order)
    SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                             "delete-entry", "cc",
                             path, pb->token));
  return SVN_NO_ERROR;
}

/* Used in svnpatch_apply_textdelta() to set up the diff_stream. */
static svn_error_t *
svndiff_write_handler(void *baton,
                      const char *data,
                      apr_size_t *len)
{
  struct file_baton *f = baton;
  struct edit_baton *eb = f->edit_baton;
  svn_string_t str;

  str.data = data;
  str.len = *len;
  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "textdelta-chunk", "cs", f->token, &str));
  return SVN_NO_ERROR;
}

/* Used in svnpatch_apply_textdelta() to set up the diff_stream. */
static svn_error_t *
svndiff_close_handler(void *baton)
{
  struct file_baton *f = baton;
  struct edit_baton *eb = f->edit_baton;

  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "textdelta-end", "c", f->token));
  return SVN_NO_ERROR;
  
}
static svn_error_t *
svnpatch_apply_textdelta(void *file_baton,
                         const char *base_checksum,
                         apr_pool_t *pool,
                         svn_txdelta_window_handler_t *handler,
                         void **handler_baton)
{
  struct file_baton *f = file_baton;
  struct edit_baton *eb = f->edit_baton;
  svn_stream_t *diff_stream;

  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "apply-textdelta", "c(?c)", f->token,
                           base_checksum));
  diff_stream = svn_stream_create(f, pool);
  svn_stream_set_write(diff_stream, svndiff_write_handler);
  svn_stream_set_close(diff_stream, svndiff_close_handler);
  svn_txdelta_to_svndiff2(handler, handler_baton, diff_stream, 1, pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
svnpatch_close_edit(void *edit_baton,
                    apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  
  SVN_ERR_ASSERT(eb->root_opened);
  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "close-edit", ""));
  return SVN_NO_ERROR;
}

/* Create an editor, close to what svn_wc_get_diff_editor4() does for
 * rep/wc diff.  As svnpatch is revisionless, no need for @c
 * set_target_revision callback.  No cancel facility here as this is
 * wc/wc diff. */
static void /* Only invoked in this same file, yet. */
get_svnpatch_diff_editor(svn_delta_editor_t **editor,
                         apr_pool_t *pool)
{
  svn_delta_editor_t *diff_editor;

  diff_editor = svn_delta_default_editor(pool);

  diff_editor->open_root = svnpatch_open_root;
  diff_editor->delete_entry = svnpatch_delete_entry;
  diff_editor->add_directory = svnpatch_add_directory;
  diff_editor->open_directory = svnpatch_open_directory;
  diff_editor->close_directory = svnpatch_close_directory;
  diff_editor->add_file = svnpatch_add_file;
  diff_editor->open_file = svnpatch_open_file;
  diff_editor->apply_textdelta = svnpatch_apply_textdelta;
  diff_editor->change_file_prop = svnpatch_change_file_prop;
  diff_editor->change_dir_prop = svnpatch_change_dir_prop;
  diff_editor->close_file = svnpatch_close_file;
  diff_editor->close_edit = svnpatch_close_edit;

  *editor = diff_editor;
}

struct path_driver_cb_baton
{
  svn_wc_adm_access_t *adm_access;     /* top-level access baton */
  const svn_delta_editor_t *editor;    /* diff editor */
  void *edit_baton;                    /* diff editor's baton */
  apr_hash_t *diffable_entries;        /* diff targets (svnpatch) */

  /* This is a workaround to mislead svn_delta_path_driver() when
   * performing a relative drive, as opposed to an absolute drive when
   * starting from WC's Root.  The point is to be able to start an
   * editor drive from within a sub-directory, different from the usual
   * WC's Root, and avoid open-root and close-edit calls.  When set,
   * this baton will cause @c path_driver_cb_func to:
   *  - set its @a *dir_baton argument to @c join_dir_baton in order to
   *  avoid an open-root call.  @c join_dir_baton is acting as the root
   *  of the drive.
   *  - join relative @a path argument with @c join_dir_baton->path. */
  struct dir_baton *join_dir_baton;
};

/* This function is directly related to svnpatch and implements @c
 * svn_delta_path_driver_cb_func_t callback needed by
 * svn_delta_path_driver() to achieve an editor drive.  It is in many
 * ways similar to do_item_commit() in a sense that it looks up
 * <tt>svn_wc_entry_t *</tt> objects from a hashmap of diffables (as in
 * 'commitables') keyed on their path and do what needs to be done by
 * invoking editor functions against them, which in turn write ra_svn
 * protocol bytes to the pipe.  Except the pipe is a temporary file we
 * dump to stdout when done, rather than a network connection.  */
static svn_error_t *
path_driver_cb_func(void **dir_baton,
                    void *parent_baton,
                    void *callback_baton,
                    const char *path,
                    apr_pool_t *pool)
{
  struct path_driver_cb_baton *cb_baton = callback_baton;
  const svn_delta_editor_t *editor = cb_baton->editor;
  svn_wc_adm_access_t *adm_access = cb_baton->adm_access;
  svn_wc_entry_t *entry = apr_hash_get(cb_baton->diffable_entries,
                                       path, APR_HASH_KEY_STRING);
  struct edit_baton *eb = cb_baton->edit_baton;
  struct dir_baton *pb = parent_baton;
  void *fb = NULL; /* file baton */
  const char *copyfrom_url = NULL;
  svn_boolean_t file_modified; /* text modifications */
  svn_boolean_t file_is_binary;
  svn_boolean_t file_need_close = FALSE;

  *dir_baton = NULL;
  if (entry->copyfrom_url)
    copyfrom_url = svn_path_is_child(entry->repos, entry->copyfrom_url, NULL);

  /* If the join baton is set we're performing a drive relative to the
   * directory @c join_dir_baton holds. */
  if (cb_baton->join_dir_baton)
    path = svn_path_join(cb_baton->join_dir_baton->path, path, pool);

  switch (entry->schedule)
    {
      case svn_wc_schedule_replace: /* fallthrough del + add */
      case svn_wc_schedule_delete:
        SVN_ERR_ASSERT(pb);
        eb->reverse_order = 1; /* TODO: fix this crappy workaround */
        SVN_ERR(editor->delete_entry
                (path, SVN_INVALID_REVNUM, pb, pool));
        eb->reverse_order = 0;

        if (svn_wc_schedule_delete) /* we're done */
          break;

      case svn_wc_schedule_add:
        if (entry->kind == svn_node_file)
          {
            SVN_ERR_ASSERT(pb);
            SVN_ERR(editor->add_file
                    (path, pb, copyfrom_url, SVN_INVALID_REVNUM,
                     pool, &fb));
            file_need_close = TRUE;
          }
        else /* dir */
          {
            SVN_ERR_ASSERT(pb);
            SVN_ERR(editor->add_directory
                    (path, pb, copyfrom_url, SVN_INVALID_REVNUM,
                     pool, dir_baton));
          }

      /* No break here so that the current entry scheduled for addition
       * also benefits the code below dealing with prop changes and binary
       * changes processing. */

      case svn_wc_schedule_normal:

        /* Open the current entry -- root, dir or file -- if it needs to. */
        if (entry->kind == svn_node_file)
          {
            if (! fb)
              {
                SVN_ERR_ASSERT(pb);
                SVN_ERR(editor->open_file
                        (path, pb, SVN_INVALID_REVNUM, pool, &fb));
                file_need_close = TRUE;
              }
          }
        else
          {
            if (! *dir_baton)
              {
                if (! pb)
                  {
                    /* This block is reached when opening the root of a
                     * relative drive. */
                    if (cb_baton->join_dir_baton)
                      *dir_baton = cb_baton->join_dir_baton;
                    else
                      SVN_ERR(editor->open_root
                              (eb, SVN_INVALID_REVNUM, pool, dir_baton));
                  }
                else
                  {
                    SVN_ERR(editor->open_directory
                            (path, pb, SVN_INVALID_REVNUM, pool, dir_baton));
                  }
              }
          }

        /* Process property changes. */
        if (entry->has_prop_mods)
          {
            SVN_ERR(svn_wc_transmit_prop_deltas
                    (path, adm_access, entry, editor,
                     (entry->kind == svn_node_file) ? fb : *dir_baton,
                     NULL, pool));
            if (entry->kind == svn_node_file)
              file_need_close = TRUE;
          }

        /* Process binary changes. */
        SVN_ERR(svn_wc_has_binary_prop(&file_is_binary, path,
                                       adm_access, pool));
        if (file_is_binary)
          {
            SVN_ERR(svn_wc_text_modified_p(&file_modified, path,
                                           TRUE, adm_access, pool));
            if (file_modified)
              SVN_ERR(svn_wc_transmit_text_deltas2
                      (NULL,
                       NULL,/* TODO:digest bin stuff */
                       path, adm_access, TRUE,
                       editor, fb, pool));
            /* svn_wc_transmit_text_deltas2() does the close itself. */
            file_need_close = FALSE; 
          }

        /* A non-binary file may need to be closed. */
        if (file_need_close)
          SVN_ERR(editor->close_file(fb, NULL, pool));
        break;

      default:
        break;
    }
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
  const char *token = make_token('d', eb, dir_pool);

  if (eb->svnpatch_stream)
    {
      get_svnpatch_diff_editor(&eb->diff_editor, eb->pool);
      SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                               "open-root", "c", token));
    }

  eb->root_opened = TRUE;
  b = make_dir_baton(eb->anchor_path, NULL, eb, FALSE, token, eb->depth, dir_pool);
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
          const char *textbase = svn_wc__text_base_path(full_path,
                                                        FALSE, pool);
          apr_hash_t *baseprops = NULL;
          const char *base_mimetype;

          SVN_ERR(get_base_mimetype(&base_mimetype, &baseprops,
                                    adm_access, full_path, pool));

          SVN_ERR(pb->edit_baton->callbacks->file_deleted
                  (NULL, NULL, full_path,
                   textbase,
                   empty_file,
                   base_mimetype,
                   NULL,
                   baseprops,
                   pb->edit_baton->callback_baton));

          if (eb->svnpatch_stream)
            SVN_ERR(eb->diff_editor->delete_entry
                    (path, SVN_INVALID_REVNUM, pb, pool));
        }
      else
        {
          /* Or normally, show the working file being added. */
          SVN_ERR(report_wc_file_as_added(pb, adm_access, full_path, entry,
                                          pool));
        }
      break;

    case svn_node_dir:
      if (eb->reverse_order)
        {
          if (eb->svnpatch_stream)
            SVN_ERR(eb->diff_editor->delete_entry
                    (path, SVN_INVALID_REVNUM, pb, pool));
        }
      else
        {
          b = make_dir_baton(full_path, pb, pb->edit_baton,
                             FALSE, NULL, svn_depth_infinity, pool);
          /* A delete is required to change working-copy into requested
             revision, so diff should show this as an add. */
          SVN_ERR(report_wc_directory_as_added(b, entry, pool));
        }
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
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *b;
  const char *full_path;
  const char *token = NULL;
  svn_depth_t subdir_depth = (pb->depth == svn_depth_immediates)
                              ? svn_depth_empty : pb->depth;

  if (eb->reverse_order)
    token = make_token('d', eb, dir_pool);


  if (eb->svnpatch_stream)
    {
      /* reverse_order is half-assed: we're actually dealing with a file
       * addition when reverse_order is true. */
      if (eb->reverse_order)
        SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                                 "add-dir", "ccc(?c)", path, pb->token,
                                 token, copyfrom_path));
      else
        SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                                 "delete-entry", "cc",
                                 path, pb->token));
    }

  /* ### TODO: support copyfrom? */

  full_path = svn_path_join(pb->edit_baton->anchor_path, path, dir_pool);
  b = make_dir_baton(full_path, pb, pb->edit_baton, TRUE,
                     token, subdir_depth, dir_pool);
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
  struct edit_baton *eb = pb->edit_baton;
  const char *full_path;
  const char *token = make_token('d', eb, dir_pool);
  svn_depth_t subdir_depth = (pb->depth == svn_depth_immediates)
                              ? svn_depth_empty : pb->depth;

  if (eb->svnpatch_stream)
    {
      SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                               "open-dir", "ccc", path, pb->token, token));
    }

  /* Allocate path from the parent pool since the memory is used in the
     parent's compared hash */
  full_path = svn_path_join(pb->edit_baton->anchor_path, path, pb->pool);
  b = make_dir_baton(full_path, pb, pb->edit_baton, FALSE,
                     token, subdir_depth, dir_pool);
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
  struct edit_baton *eb = b->edit_baton;

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
              (NULL, NULL,
               b->path,
               b->propchanges,
               originalprops,
               b->edit_baton->callback_baton));

      if (b->edit_baton->svnpatch_stream)
        SVN_ERR(transmit_prop_deltas
                (b->propchanges, originalprops, b, eb,
                 eb->diff_editor->change_dir_prop, b->pool));

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

  if (b->edit_baton->svnpatch_stream)
    {
      /* As we're in the middle of a depth-first traversal here, and in
       * the ultimate block before closing the current directory, we'd
       * better process the current-dir wc local-changes before
       * backtracking.  That is, we have to append serialiazed Editor
       * Commands related to child components into our streamy-buffer
       * before we write the close-dir command.  The @c diff_targets
       * array contains local-change paths, over which we loop and empty
       * out when done.  We're about to use svn_delta_path_driver() to
       * perform the traversal of any target-child-components.  Except
       * this traversal starts in the middle of nowhere, i.e. is
       * relative to the current-path, so we're using chunks of glue
       * below -- @c join_dir_baton -- to join with previous commands
       * and mislead svn_delta_path_driver() -- no open-root/dir and a
       * set of relative paths.
       * TODO: factorize with svn_wc_diff4's similar section. */
      if (eb->diff_targets->nelts > 0)
        {
          int i;
          struct path_driver_cb_baton cb_baton;
          svn_wc_adm_access_t *adm_access;
          apr_hash_t *diffable_entries = apr_hash_make(pool);

          SVN_ERR(svn_wc_adm_retrieve(&adm_access,
                                      b->edit_baton->anchor, b->path,
                                      b->pool));

          /* Push empty path to avoid editor->open-root call from
           * svn_delta_path_driver(), and handle the call from @c
           * path_driver_cb_func instead. */
          APR_ARRAY_PUSH(eb->diff_targets, const char *) = "";

          for (i = 0; i < eb->diff_targets->nelts; ++i)
            {
              const char *name = APR_ARRAY_IDX(eb->diff_targets, i,
                                               const char *);
              const svn_wc_entry_t *new_entry;
              const char *relative_path = NULL;

              /* No need for this when at the root. */
              if (pb && strlen(name) > 0)
                relative_path = name + strlen(b->path) + 1;

              SVN_ERR(svn_wc_entry(&new_entry, name, adm_access, TRUE, eb->pool));
              apr_hash_set(diffable_entries,
                           relative_path ? relative_path : name,
                           APR_HASH_KEY_STRING, new_entry);

              /* Replace with relative path to mislead
               * svn_delta_path_driver(). */
              APR_ARRAY_IDX(eb->diff_targets, i, const char *)
                = relative_path ? relative_path : name;
            }

          cb_baton.editor = eb->diff_editor;
          cb_baton.adm_access = adm_access;
          cb_baton.edit_baton = eb;
          cb_baton.diffable_entries = diffable_entries;
          cb_baton.join_dir_baton = b; /* Start the drive with this dir. */

          SVN_ERR(svn_delta_path_driver(eb->diff_editor, eb,
                                        SVN_INVALID_REVNUM, eb->diff_targets,
                                        path_driver_cb_func, (void *)&cb_baton,
                                        pool));

          /* Make it empty, we're done with those targets. */
          while(apr_array_pop(eb->diff_targets))
            ;

        }
      else
        {
          if (eb->reverse_order || ! b->added)
            SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                                     "close-dir", "c", b->token));
        }

    }

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
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *b;
  const char *full_path;
  const char *token = make_token('c', eb, file_pool);

  /* ### TODO: support copyfrom? */

  full_path = svn_path_join(pb->edit_baton->anchor_path, path, file_pool);
  b = make_file_baton(full_path, TRUE, pb, token, file_pool);
  *file_baton = b;

  if (eb->svnpatch_stream && eb->reverse_order)
    SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                             "add-file", "ccc(?c)", path, pb->token,
                             token, copyfrom_path));

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
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *b;
  const char *full_path;
  const char *token = make_token('c', eb, file_pool);

  full_path = svn_path_join(pb->edit_baton->anchor_path, path, file_pool);
  b = make_file_baton(full_path, FALSE, pb, token, file_pool);
  *file_baton = b;

  if (eb->svnpatch_stream)
    SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                             "open-file", "ccc", path, pb->token,
                             token));

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

  SVN_ERR(b->apply_handler(window, b->apply_baton));

  if (!window)
    {
      SVN_ERR(svn_io_file_close(b->temp_file, b->pool));

      if (b->added)
        SVN_ERR(svn_io_file_close(b->original_file, b->pool));
      else
        {
          SVN_ERR(svn_wc__close_text_base(b->original_file, b->path, 0,
                                          b->pool));
        }
    }

  return SVN_NO_ERROR;
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

  SVN_ERR(svn_wc_entry(&entry, b->wc_path, eb->anchor, FALSE, b->pool));

  svn_path_split(b->wc_path, &parent, &base_name, b->pool);

  /* Check to see if there is a schedule-add with history entry in
     the current working copy.  If so, then this is not actually
     an add, but instead a modification.*/
  if (entry && entry->copyfrom_url)
    b->added = FALSE;

  if (b->added)
    {
      /* An empty file is the starting point if the file is being added */
      const char *empty_file;

      SVN_ERR(get_empty_file(eb, &empty_file));
      SVN_ERR(svn_io_file_open(&b->original_file, empty_file,
                               APR_READ, APR_OS_DEFAULT, pool));
    }
  else
    {
      /* The current text-base is the starting point if replacing */
      SVN_ERR(svn_wc__open_text_base(&b->original_file, b->path,
                                     APR_READ, b->pool));
    }

  /* This is the file that will contain the pristine repository version. It
     is created in the admin temporary area. This file continues to exists
     until after the diff callback is run, at which point it is deleted. */
  SVN_ERR(svn_wc_create_tmp_file2(&b->temp_file, &b->temp_file_path,
                                  parent, svn_io_file_del_on_pool_cleanup,
                                  b->pool));

  svn_txdelta_apply(svn_stream_from_aprfile2(b->original_file, TRUE, b->pool),
                    svn_stream_from_aprfile2(b->temp_file, TRUE, b->pool),
                    NULL,
                    b->temp_file_path,
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
  svn_boolean_t binary_file;

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
  binary_file = repos_mimetype ?
    svn_mime_type_is_binary(repos_mimetype) : FALSE;

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
        {
          /* svnpatch-related */
          if (eb->svnpatch_stream)
            {
              SVN_ERR(transmit_prop_deltas
                      (b->propchanges, NULL, b, eb,
                       eb->diff_editor->change_file_prop, b->pool));

              /* If this new repos-incoming file holds a binary
               * mime-type, we want our svnpatch to convey the file's
               * content. */
              if (binary_file)
                  SVN_ERR(transmit_svndiff(temp_file_path, adm_access,
                                           eb->diff_editor, b, pool));

              /* Last chance to write a close-file command as a return
               * statement follows. */
              SVN_ERR(eb->diff_editor->close_file(b, binary_file ?
                                                  text_checksum : NULL, pool));
            }

          /* Unidiff-related through libsvn_client. */
          return b->edit_baton->callbacks->file_added
                  (NULL, NULL, NULL, b->path,
                   empty_file,
                   temp_file_path,
                   0,
                   eb->revnum,
                   NULL,
                   repos_mimetype,
                   NULL, SVN_INVALID_REVNUM, /* XXX make use of new 1.6 API */
                   b->propchanges,
                   apr_hash_make(pool),
                   b->edit_baton->callback_baton);
        }
      else
        {
          if (eb->svnpatch_stream)
            SVN_ERR(eb->diff_editor->delete_entry
                    (b->path, SVN_INVALID_REVNUM, b->dir_baton, pool));

          return b->edit_baton->callbacks->file_deleted
                  (NULL, NULL, b->path,
                   temp_file_path,
                   empty_file,
                   repos_mimetype,
                   NULL,
                   repos_props,
                   b->edit_baton->callback_baton);
        }
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

      binary_file = binary_file ? TRUE :
        (original_mimetype ? svn_mime_type_is_binary(original_mimetype)
          : FALSE);

      if (b->propchanges->nelts > 0
          && ! eb->reverse_order)
        reverse_propchanges(originalprops, b->propchanges, b->pool);

      if (eb->svnpatch_stream)
        {
          SVN_ERR(transmit_prop_deltas
                  (b->propchanges, originalprops, b, eb,
                   eb->diff_editor->change_file_prop, b->pool));

          if (binary_file)
            {
              unsigned char tmp_digest[APR_MD5_DIGESTSIZE];
              const char *the_right_path = eb->reverse_order ?
                                           temp_file_path : localfile;

              SVN_ERR(transmit_svndiff
                      (the_right_path, adm_access,
                       eb->diff_editor, b, pool));

              /* Calculate the file's checksum since the one above might
               * be wrong. */
              SVN_ERR (svn_io_file_checksum (tmp_digest, the_right_path, pool));
              text_checksum = (const char*)svn_md5_digest_to_cstring_display
                                            (tmp_digest, pool);
            }
        }

      SVN_ERR(b->edit_baton->callbacks->file_changed
              (NULL, NULL, NULL,
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

  if (eb->svnpatch_stream)
    SVN_ERR(eb->diff_editor->close_file
            (b, binary_file ? text_checksum : NULL, b->pool));

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
                         NULL, eb->depth, eb->pool);
      SVN_ERR(directory_elements_diff(b));
    }

  if (eb->svnpatch_stream)
    {
      /* No more target left to diff. */
      SVN_ERR_ASSERT(eb->diff_targets->nelts < 1);
      SVN_ERR(eb->diff_editor->close_edit
              (eb, pool));
    }
  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
file_changed(svn_wc_adm_access_t *adm_access,
             svn_wc_notify_state_t *contentstate,
             svn_wc_notify_state_t *propstate,
             const char *path,
             const char *tmpfile1,
             const char *tmpfile2,
             svn_revnum_t rev1,
             svn_revnum_t rev2,
             const char *mimetype1,
             const char *mimetype2,
             const apr_array_header_t *propchanges,
             apr_hash_t *originalprops,
             void *diff_baton)
{
  struct callbacks_wrapper_baton *b = diff_baton;
  if (tmpfile2 != NULL)
    SVN_ERR(b->callbacks->file_changed(adm_access, contentstate, path,
                                       tmpfile1, tmpfile2,
                                       rev1, rev2, mimetype1, mimetype2,
                                       b->baton));
  if (propchanges->nelts > 0)
    SVN_ERR(b->callbacks->props_changed(adm_access, propstate, path,
                                        propchanges, originalprops,
                                        b->baton));

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
file_added(svn_wc_adm_access_t *adm_access,
           svn_wc_notify_state_t *contentstate,
           svn_wc_notify_state_t *propstate,
           const char *path,
           const char *tmpfile1,
           const char *tmpfile2,
           svn_revnum_t rev1,
           svn_revnum_t rev2,
           const char *mimetype1,
           const char *mimetype2,
           const char *copyfrom_path,
           svn_revnum_t copyfrom_revision,
           const apr_array_header_t *propchanges,
           apr_hash_t *originalprops,
           void *diff_baton)
{
  struct callbacks_wrapper_baton *b = diff_baton;
  SVN_ERR(b->callbacks->file_added(adm_access, contentstate, path,
                                   tmpfile1, tmpfile2, rev1, rev2,
                                   mimetype1, mimetype2, b->baton));
  if (propchanges->nelts > 0)
    SVN_ERR(b->callbacks->props_changed(adm_access, propstate, path,
                                        propchanges, originalprops,
                                        b->baton));

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
file_deleted(svn_wc_adm_access_t *adm_access,
             svn_wc_notify_state_t *state,
             const char *path,
             const char *tmpfile1,
             const char *tmpfile2,
             const char *mimetype1,
             const char *mimetype2,
             apr_hash_t *originalprops,
             void *diff_baton)
{
  struct callbacks_wrapper_baton *b = diff_baton;

  SVN_ERR_ASSERT(originalprops);

  return b->callbacks->file_deleted(adm_access, state, path,
                                    tmpfile1, tmpfile2, mimetype1, mimetype2,
                                    b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
dir_added(svn_wc_adm_access_t *adm_access,
          svn_wc_notify_state_t *state,
          const char *path,
          svn_revnum_t rev,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          void *diff_baton)
{
  struct callbacks_wrapper_baton *b = diff_baton;

  return b->callbacks->dir_added(adm_access, state, path, rev, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
dir_deleted(svn_wc_adm_access_t *adm_access,
            svn_wc_notify_state_t *state,
            const char *path,
            void *diff_baton)
{
  struct callbacks_wrapper_baton *b = diff_baton;

  return b->callbacks->dir_deleted(adm_access, state, path, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
dir_props_changed(svn_wc_adm_access_t *adm_access,
                  svn_wc_notify_state_t *state,
                  const char *path,
                  const apr_array_header_t *propchanges,
                  apr_hash_t *originalprops,
                  void *diff_baton)
{
  struct callbacks_wrapper_baton *b = diff_baton;
  return b->callbacks->props_changed(adm_access, state, path, propchanges,
                                     originalprops, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t
   and svn_wc_diff_callbacks2_t. */
static svn_error_t *
dir_opened(svn_wc_adm_access_t *adm_access,
           const char *path,
           svn_revnum_t rev,
           void *diff_baton)
{
  /* Do nothing. */
  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t
   and svn_wc_diff_callbacks2_t. */
static svn_error_t *
dir_closed(svn_wc_adm_access_t *adm_access,
           svn_wc_notify_state_t *state,
           const char *path,
           void *diff_baton)
{
  /* Do nothing. */
  return SVN_NO_ERROR;
}

/* Used to wrap svn_diff_callbacks_t as an svn_wc_diff_callbacks3_t. */
static struct svn_wc_diff_callbacks3_t callbacks_wrapper = {
  file_changed,
  file_added,
  file_deleted,
  dir_added,
  dir_deleted,
  dir_props_changed,
  dir_opened,
  dir_closed
};

/* Used to wrap svn_diff_callbacks2_t as an svn_wc_diff_callbacks3_t. */
static svn_wc_diff_callbacks3_t *
callbacks2_wrap(const svn_wc_diff_callbacks2_t *callbacks2, apr_pool_t *pool)
{
  svn_wc_diff_callbacks3_t *callbacks3 = apr_palloc(pool, sizeof(*callbacks3));
  callbacks3->file_changed      = callbacks2->file_changed;
  callbacks3->file_added        = callbacks2->file_added;
  callbacks3->file_deleted      = callbacks2->file_deleted;
  callbacks3->dir_added         = callbacks2->dir_added;
  callbacks3->dir_deleted       = callbacks2->dir_deleted;
  callbacks3->dir_props_changed = callbacks2->dir_props_changed;
  callbacks3->dir_opened = dir_opened;
  callbacks3->dir_closed = dir_closed;
  return callbacks3;
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
                        apr_file_t *svnpatch_file,
                        apr_pool_t *pool)
{
  struct edit_baton *eb;
  void *inner_baton;
  svn_delta_editor_t *tree_editor;
  const svn_delta_editor_t *inner_editor;

  SVN_ERR(make_editor_baton(&eb, anchor, target, callbacks, callback_baton,
                            depth, ignore_ancestry, use_text_base,
                            reverse_order, changelists, pool));

  if (svnpatch_file)
      eb->svnpatch_stream =
        svn_stream_from_aprfile2(svnpatch_file,
                                 FALSE, eb->pool);

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

svn_error_t *
svn_wc_get_diff_editor4(svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks2_t *callbacks,
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
  return svn_wc_get_diff_editor5(anchor,
                                 target,
                                 callbacks2_wrap(callbacks, pool),
                                 callback_baton,
                                 depth,
                                 ignore_ancestry,
                                 use_text_base,
                                 reverse_order,
                                 cancel_func,
                                 cancel_baton,
                                 changelists,
                                 editor,
                                 edit_baton,
                                 NULL,
                                 pool);
}

svn_error_t *
svn_wc_get_diff_editor3(svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks2_t *callbacks,
                        void *callback_baton,
                        svn_boolean_t recurse,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        apr_pool_t *pool)
{
  return svn_wc_get_diff_editor4(anchor,
                                 target,
                                 callbacks,
                                 callback_baton,
                                 SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                 ignore_ancestry,
                                 use_text_base,
                                 reverse_order,
                                 cancel_func,
                                 cancel_baton,
                                 NULL,
                                 editor,
                                 edit_baton,
                                 pool);
}

svn_error_t *
svn_wc_get_diff_editor2(svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks_t *callbacks,
                        void *callback_baton,
                        svn_boolean_t recurse,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        apr_pool_t *pool)
{
  struct callbacks_wrapper_baton *b = apr_palloc(pool, sizeof(*b));
  b->callbacks = callbacks;
  b->baton = callback_baton;
  return svn_wc_get_diff_editor5(anchor, target, &callbacks_wrapper, b,
                                 SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                 ignore_ancestry, use_text_base,
                                 reverse_order, cancel_func, cancel_baton,
                                 NULL, editor, edit_baton, NULL, pool);
}

svn_error_t *
svn_wc_get_diff_editor(svn_wc_adm_access_t *anchor,
                       const char *target,
                       const svn_wc_diff_callbacks_t *callbacks,
                       void *callback_baton,
                       svn_boolean_t recurse,
                       svn_boolean_t use_text_base,
                       svn_boolean_t reverse_order,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       const svn_delta_editor_t **editor,
                       void **edit_baton,
                       apr_pool_t *pool)
{
  return svn_wc_get_diff_editor2(anchor, target, callbacks, callback_baton,
                                 recurse, FALSE, use_text_base, reverse_order,
                                 cancel_func, cancel_baton,
                                 editor, edit_baton, pool);
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
             apr_file_t *svnpatch_file,
             apr_pool_t *pool)
{
  struct edit_baton *eb;
  struct dir_baton *b;
  const svn_wc_entry_t *entry;
  const char *target_path;
  svn_wc_adm_access_t *adm_access;
  svn_delta_editor_t *diff_editor;

  SVN_ERR(make_editor_baton(&eb, anchor, target, callbacks, callback_baton,
                            depth, ignore_ancestry, FALSE, FALSE,
                            changelists, pool));

  /* Get ready with svnpatch work: initiate a stream once and for all to
   * manipulate the svnpatch file.  As much of the functions called
   * below down through the stack all have access to the @c edit_baton,
   * we'll be testing whether or not we want svnpatch diff against the
   * nullity of @c edit_baton->svnpatch_stream. */
  if (svnpatch_file)
      eb->svnpatch_stream =
        svn_stream_from_aprfile2(svnpatch_file,
                                 FALSE, eb->pool);

  target_path = svn_path_join(svn_wc_adm_access_path(anchor), target,
                              eb->pool);

  SVN_ERR(svn_wc_adm_probe_retrieve(&adm_access, anchor, target_path,
                                    eb->pool));
  SVN_ERR(svn_wc__entry_versioned(&entry, target_path, adm_access, FALSE,
                                  eb->pool));

  if (entry->kind == svn_node_dir)
    b = make_dir_baton(target_path, NULL, eb, FALSE, NULL, depth, eb->pool);
  else
    b = make_dir_baton(eb->anchor_path, NULL, eb, FALSE, NULL, depth, eb->pool);

  SVN_ERR(directory_elements_diff(b));

  /* Time to dump some serialiazed Editor Commands. */
  if (svnpatch_file && eb->diff_targets->nelts > 0)
    {
      struct path_driver_cb_baton cb_baton;
      apr_hash_t *diffable_entries;
      int i = 0;
      eb->next_token = 0;

      /* Set up @c diff_editor with the set of svnpatch editor
       * functions defined in this same file. */
      get_svnpatch_diff_editor(&diff_editor, eb->pool);

      /* Create a hashmap of @c svn_wc_entry_t * objects from the array
       * of diff_targets, i.e. the list of *diffable* entries, keyed on
       * their path.  This hash is looked up from path_driver_cb_func().
       */
      diffable_entries = apr_hash_make(pool);

      for (i = 0; i < eb->diff_targets->nelts; ++i)
        {
          const char *name = APR_ARRAY_IDX(eb->diff_targets, i, const char *);
          const svn_wc_entry_t *new_entry;

          SVN_ERR(svn_wc_entry(&new_entry, name, adm_access, TRUE, eb->pool));
          apr_hash_set(diffable_entries, name, APR_HASH_KEY_STRING, new_entry);
        }

      cb_baton.editor = diff_editor;
      cb_baton.adm_access = adm_access;
      cb_baton.edit_baton = eb;
      cb_baton.diffable_entries = diffable_entries;
      cb_baton.join_dir_baton = NULL; /* No need for this feature here. */

      /* Drive the editor to dump serialized editor commands to the
       * svnpatch tempfile. */
      SVN_ERR(svn_delta_path_driver(diff_editor, eb,
                                    SVN_INVALID_REVNUM, eb->diff_targets,
                                    path_driver_cb_func, (void *)&cb_baton,
                                    pool));
      SVN_ERR(diff_editor->close_edit(eb, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_diff4(svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks2_t *callbacks,
             void *callback_baton,
             svn_depth_t depth,
             svn_boolean_t ignore_ancestry,
             const apr_array_header_t *changelists,
             apr_file_t *svnpatch_file,
             apr_pool_t *pool)
{


  return svn_wc_diff5(anchor, target, callbacks2_wrap(callbacks, pool),
                      callback_baton, depth, ignore_ancestry, changelists,
                      svnpatch_file, pool);
}

svn_error_t *
svn_wc_diff3(svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks2_t *callbacks,
             void *callback_baton,
             svn_boolean_t recurse,
             svn_boolean_t ignore_ancestry,
             apr_pool_t *pool)
{
  return svn_wc_diff4(anchor, target, callbacks, callback_baton,
                      SVN_DEPTH_INFINITY_OR_FILES(recurse), ignore_ancestry,
                      NULL, NULL, pool);
}

svn_error_t *
svn_wc_diff2(svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks_t *callbacks,
             void *callback_baton,
             svn_boolean_t recurse,
             svn_boolean_t ignore_ancestry,
             apr_pool_t *pool)
{
  struct callbacks_wrapper_baton *b = apr_pcalloc(pool, sizeof(*b));
  b->callbacks = callbacks;
  b->baton = callback_baton;
  return svn_wc_diff5(anchor, target, &callbacks_wrapper, b,
                      SVN_DEPTH_INFINITY_OR_FILES(recurse), ignore_ancestry,
                      NULL, NULL, pool);
}

svn_error_t *
svn_wc_diff(svn_wc_adm_access_t *anchor,
            const char *target,
            const svn_wc_diff_callbacks_t *callbacks,
            void *callback_baton,
            svn_boolean_t recurse,
            apr_pool_t *pool)
{
  return svn_wc_diff2(anchor, target, callbacks, callback_baton,
                      recurse, FALSE, pool);
}
