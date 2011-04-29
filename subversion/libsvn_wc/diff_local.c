/*
 * diff_pristine.c -- A simple diff walker which compares local files against
 *                    their pristine versions.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 *
 * This is the simple working copy diff algorithm which is used when you
 * just use 'svn diff PATH'. It shows what is modified in your working copy
 * since a node was checked out or copied but doesn't show most kinds of
 * restructuring operations.
 *
 * You can look at this as another form of the status walker.
 *
 * ### TODO: Simply use svn_wc_walk_status() to provide this diff.
 */

#include <apr_hash.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"

#include "private/svn_wc_private.h"

#include "wc.h"
#include "props.h"
#include "adm_files.h"
#include "translate.h"

#include "svn_private_config.h"





/* Set *RESULT_ABSPATH to the absolute path to a readable file containing
   the pristine text of LOCAL_ABSPATH in DB, or to NULL if it does not have
   any pristine text.

   If USE_BASE is FALSE it gets the pristine text of what is currently in the
   working copy. (So it returns the pristine file of a copy).

   If USE_BASE is TRUE, it looks in the lowest layer of the working copy and
   shows exactly what was originally checked out (or updated to).

   Rationale:

   Which text-base do we want to use for the diff?  If the node is replaced
   by a new file, then the base of the replaced file is called (in WC-1) the
   "revert base".  If the replacement is a copy or move, then there is also
   the base of the copied file to consider.

   One could argue that we should never diff against the revert
   base, and instead diff against the empty-file for both types of
   replacement.  After all, there is no ancestry relationship
   between the working file and the base file.  But my guess is that
   in practice, users want to see the diff between their working
   file and "the nearest versioned thing", whatever that is.  I'm
   not 100% sure this is the right decision, but it at least seems
   to match our test suite's expectations. */
static svn_error_t *
get_pristine_file(const char **result_abspath,
                  svn_wc__db_t *db,
                  const char *local_abspath,
                  svn_boolean_t use_base,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  const svn_checksum_t *checksum;

  if (!use_base)
    {
      SVN_ERR(svn_wc__db_read_pristine_info(NULL, NULL, NULL, NULL, NULL, NULL,
                                            &checksum, NULL, NULL,
                                            db, local_abspath,
                                            scratch_pool, scratch_pool));
    }
  else
    {
      SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, &checksum,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL,
                                       db, local_abspath,
                                       scratch_pool, scratch_pool));
    }

  if (checksum != NULL)
    {
      SVN_ERR(svn_wc__db_pristine_get_path(result_abspath, db, local_abspath,
                                           checksum,
                                           result_pool, scratch_pool));
      return SVN_NO_ERROR;
    }

  *result_abspath = NULL;
  return SVN_NO_ERROR;
}


/*-------------------------------------------------------------------------*/


/* Overall crawler editor baton.
 */
struct edit_baton {
  /* A wc db. */
  svn_wc__db_t *db;

  /* ANCHOR/TARGET represent the base of the hierarchy to be compared. */
  const char *target;

  /* The absolute path of the anchor directory */
  const char *anchor_abspath;

  /* Target revision */
  svn_revnum_t revnum;

  /* Was the root opened? */
  svn_boolean_t root_opened;

  /* The callbacks and callback argument that implement the file comparison
     functions */
  const svn_wc_diff_callbacks4_t *callbacks;
  void *callback_baton;

  /* How does this diff descend? */
  svn_depth_t depth;

  /* Should this diff ignore node ancestry? */
  svn_boolean_t ignore_ancestry;

  /* Should this diff not compare copied files with their source? */
  svn_boolean_t show_copies_as_adds;

  /* Are we producing a git-style diff? */
  svn_boolean_t use_git_diff_format;

  /* Possibly diff repos against text-bases instead of working files. */
  svn_boolean_t use_text_base;

  /* Possibly show the diffs backwards. */
  svn_boolean_t reverse_order;

  /* Empty file used to diff adds / deletes */
  const char *empty_file;

  /* Hash whose keys are const char * changelist names. */
  apr_hash_t *changelist_hash;

  /* Cancel function/baton */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  apr_pool_t *pool;
};

/* Create a new edit baton. TARGET_PATH/ANCHOR are working copy paths
 * that describe the root of the comparison. CALLBACKS/CALLBACK_BATON
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
make_edit_baton(struct edit_baton **edit_baton,
                svn_wc__db_t *db,
                const char *anchor_abspath,
                const char *target,
                const svn_wc_diff_callbacks4_t *callbacks,
                void *callback_baton,
                svn_depth_t depth,
                svn_boolean_t ignore_ancestry,
                svn_boolean_t show_copies_as_adds,
                svn_boolean_t use_git_diff_format,
                svn_boolean_t use_text_base,
                svn_boolean_t reverse_order,
                const apr_array_header_t *changelists,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *pool)
{
  apr_hash_t *changelist_hash = NULL;
  struct edit_baton *eb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(anchor_abspath));

  if (changelists && changelists->nelts)
    SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelists, pool));

  eb = apr_pcalloc(pool, sizeof(*eb));
  eb->db = db;
  eb->anchor_abspath = apr_pstrdup(pool, anchor_abspath);
  eb->target = apr_pstrdup(pool, target);
  eb->callbacks = callbacks;
  eb->callback_baton = callback_baton;
  eb->depth = depth;
  eb->ignore_ancestry = ignore_ancestry;
  eb->show_copies_as_adds = show_copies_as_adds;
  eb->use_git_diff_format = use_git_diff_format;
  eb->use_text_base = use_text_base;
  eb->reverse_order = reverse_order;
  eb->changelist_hash = changelist_hash;
  eb->cancel_func = cancel_func;
  eb->cancel_baton = cancel_baton;
  eb->pool = pool;

  *edit_baton = eb;
  return SVN_NO_ERROR;
}

/* Get the empty file associated with the edit baton. This is cached so
 * that it can be reused, all empty files are the same.
 */
static svn_error_t *
get_empty_file(struct edit_baton *b,
               const char **empty_file)
{
  /* Create the file if it does not exist */
  /* Note that we tried to use /dev/null in r857294, but
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


/* Diff the file PATH against its text base.  At this
 * stage we are dealing with a file that does exist in the working copy.
 *
 * DIR_BATON is the parent directory baton, PATH is the path to the file to
 * be compared.
 *
 * Do all allocation in POOL.
 *
 * ### TODO: Need to work on replace if the new filename used to be a
 * directory.
 */
static svn_error_t *
file_diff(struct edit_baton *eb,
          const char *local_abspath,
          const char *path,
          apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = eb->db;
  const char *textbase;
  const char *empty_file;
  svn_boolean_t replaced;
  svn_wc__db_status_t status;
  const char *original_repos_relpath;
  svn_revnum_t revision;
  svn_revnum_t revert_base_revnum;
  svn_boolean_t have_base;
  svn_wc__db_status_t base_status;
  svn_boolean_t use_base = FALSE;

  SVN_ERR_ASSERT(! eb->use_text_base);

  /* If the item is not a member of a specified changelist (and there are
     some specified changelists), skip it. */
  if (! svn_wc__internal_changelist_match(db, local_abspath,
                                          eb->changelist_hash, scratch_pool))
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__db_read_info(&status, NULL, &revision, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL,
                               &have_base, NULL, NULL,
                               db, local_abspath, scratch_pool, scratch_pool));
  if (have_base)
    SVN_ERR(svn_wc__db_base_get_info(&base_status, NULL, &revert_base_revnum,
                                     NULL, NULL, NULL, NULL, NULL, NULL,
                                     NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                     NULL, NULL,
                                     db, local_abspath,
                                     scratch_pool, scratch_pool));

  replaced = ((status == svn_wc__db_status_added)
              && have_base
              && base_status != svn_wc__db_status_not_present);

  /* Now refine ADDED to one of: ADDED, COPIED, MOVED_HERE. Note that only
     the latter two have corresponding pristine info to diff against.  */
  if (status == svn_wc__db_status_added)
    SVN_ERR(svn_wc__db_scan_addition(&status, NULL, NULL, NULL, NULL,
                                     &original_repos_relpath, NULL, NULL,
                                     NULL, db, local_abspath,
                                     scratch_pool, scratch_pool));

  /* A wc-wc diff of replaced files actually shows a diff against the
   * revert-base, showing all previous lines as removed and adding all new
   * lines. This does not happen for copied/moved-here files, not even with
   * show_copies_as_adds == TRUE (in which case copy/move is really shown as
   * an add, diffing against the empty file).
   * So show the revert-base revision for plain replaces. */
  if (replaced
      && ! (status == svn_wc__db_status_copied
            || status == svn_wc__db_status_moved_here))
    {
      use_base = TRUE;
      revision = revert_base_revnum;
    }

  /* Set TEXTBASE to the path to the text-base file that we want to diff
     against.

     ### There shouldn't be cases where the result is NULL, but at present
     there might be - see get_nearest_pristine_text_as_file(). */
  SVN_ERR(get_pristine_file(&textbase, db, local_abspath,
                            use_base, scratch_pool, scratch_pool));

  SVN_ERR(get_empty_file(eb, &empty_file));

  /* Delete compares text-base against empty file, modifications to the
   * working-copy version of the deleted file are not wanted.
   * Replace is treated like a delete plus an add: two comparisons are
   * generated, first one for the delete and then one for the add.
   * However, if this file was replaced and we are ignoring ancestry,
   * report it as a normal file modification instead. */
  if ((! replaced && status == svn_wc__db_status_deleted) ||
      (replaced && ! eb->ignore_ancestry))
    {
      const char *base_mimetype;
      apr_hash_t *baseprops;

      /* Get svn:mime-type from pristine props (in BASE or WORKING) of PATH. */
      SVN_ERR(svn_wc__get_pristine_props(&baseprops, db, local_abspath,
                                         scratch_pool, scratch_pool));
      if (baseprops)
        base_mimetype = get_prop_mimetype(baseprops);
      else
        base_mimetype = NULL;

      SVN_ERR(eb->callbacks->file_deleted(NULL, NULL, path,
                                          textbase,
                                          empty_file,
                                          base_mimetype,
                                          NULL,
                                          baseprops,
                                          eb->callback_baton,
                                          scratch_pool));

      if (! (replaced && ! eb->ignore_ancestry))
        {
          /* We're here only for showing a delete, so we're done. */
          return SVN_NO_ERROR;
        }
    }

 /* Now deal with showing additions, or the add-half of replacements.
  * If the item is schedule-add *with history*, then we usually want
  * to see the usual working vs. text-base comparison, which will show changes
  * made since the file was copied.  But in case we're showing copies as adds,
  * we need to compare the copied file to the empty file. If we're doing a git
  * diff, and the file was copied, we need to report the file as added and
  * diff it against the text base, so that a "copied" git diff header, and
  * possibly a diff against the copy source, will be generated for it. */
  if ((! replaced && status == svn_wc__db_status_added) ||
     (replaced && ! eb->ignore_ancestry) ||
     ((status == svn_wc__db_status_copied ||
       status == svn_wc__db_status_moved_here) &&
         (eb->show_copies_as_adds || eb->use_git_diff_format)))
    {
      const char *translated = NULL;
      const char *working_mimetype;
      apr_hash_t *baseprops;
      apr_hash_t *workingprops;
      apr_array_header_t *propchanges;

      /* Get svn:mime-type from ACTUAL props of PATH. */
      SVN_ERR(svn_wc__get_actual_props(&workingprops, db, local_abspath,
                                       scratch_pool, scratch_pool));
      working_mimetype = get_prop_mimetype(workingprops);

      /* Set the original properties to empty, then compute "changes" from
         that. Essentially, all ACTUAL props will be "added".  */
      baseprops = apr_hash_make(scratch_pool);
      SVN_ERR(svn_prop_diffs(&propchanges, workingprops, baseprops,
                             scratch_pool));

      SVN_ERR(svn_wc__internal_translated_file(
              &translated, local_abspath, db, local_abspath,
              SVN_WC_TRANSLATE_TO_NF | SVN_WC_TRANSLATE_USE_GLOBAL_TMP,
              eb->cancel_func, eb->cancel_baton,
              scratch_pool, scratch_pool));

      SVN_ERR(eb->callbacks->file_added(NULL, NULL, NULL, path,
                                        (! eb->show_copies_as_adds &&
                                         eb->use_git_diff_format &&
                                         status != svn_wc__db_status_added) ?
                                          textbase : empty_file,
                                        translated,
                                        0, revision,
                                        NULL,
                                        working_mimetype,
                                        original_repos_relpath,
                                        SVN_INVALID_REVNUM, propchanges,
                                        baseprops, eb->callback_baton,
                                        scratch_pool));
    }
  else
    {
      const char *translated = NULL;
      apr_hash_t *baseprops;
      const char *base_mimetype;
      const char *working_mimetype;
      apr_hash_t *workingprops;
      apr_array_header_t *propchanges;
      svn_boolean_t modified;

      /* Here we deal with showing pure modifications. */
      SVN_ERR(svn_wc__internal_file_modified_p(&modified, NULL, NULL, db,
                                               local_abspath, FALSE, TRUE,
                                               scratch_pool));
      if (modified)
        {
          /* Note that this might be the _second_ time we translate
             the file, as svn_wc__text_modified_internal_p() might have used a
             tmp translated copy too.  But what the heck, diff is
             already expensive, translating twice for the sake of code
             modularity is liveable. */
          SVN_ERR(svn_wc__internal_translated_file(
                    &translated, local_abspath, db, local_abspath,
                    SVN_WC_TRANSLATE_TO_NF | SVN_WC_TRANSLATE_USE_GLOBAL_TMP,
                    eb->cancel_func, eb->cancel_baton,
                    scratch_pool, scratch_pool));
        }

      /* Get the properties, the svn:mime-type values, and compute the
         differences between the two.  */
      if (replaced
          && eb->ignore_ancestry)
        {
          /* We don't want the normal pristine properties (which are
             from the WORKING tree). We want the pristines associated
             with the BASE tree, which are saved as "revert" props.  */
          SVN_ERR(svn_wc__db_base_get_props(&baseprops,
                                            db, local_abspath,
                                            scratch_pool, scratch_pool));
        }
      else
        {
          /* We can only fetch the pristine props (from BASE or WORKING) if
             the node has not been replaced, or it was copied/moved here.  */
          SVN_ERR_ASSERT(!replaced
                         || status == svn_wc__db_status_copied
                         || status == svn_wc__db_status_moved_here);

          SVN_ERR(svn_wc__get_pristine_props(&baseprops, db, local_abspath,
                                             scratch_pool, scratch_pool));

          /* baseprops will be NULL for added nodes */
          if (!baseprops)
            baseprops = apr_hash_make(scratch_pool);
        }
      base_mimetype = get_prop_mimetype(baseprops);

      SVN_ERR(svn_wc__get_actual_props(&workingprops, db, local_abspath,
                                       scratch_pool, scratch_pool));
      working_mimetype = get_prop_mimetype(workingprops);

      SVN_ERR(svn_prop_diffs(&propchanges, workingprops, baseprops, scratch_pool));

      if (modified || propchanges->nelts > 0)
        {
          SVN_ERR(eb->callbacks->file_changed(NULL, NULL, NULL,
                                              path,
                                              modified ? textbase : NULL,
                                              translated,
                                              revision,
                                              SVN_INVALID_REVNUM,
                                              base_mimetype,
                                              working_mimetype,
                                              propchanges, baseprops,
                                              eb->callback_baton,
                                              scratch_pool));
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
walk_local_nodes_diff(struct edit_baton *eb,
                      const char *local_abspath,
                      const char *path,
                      svn_depth_t depth,
                      apr_hash_t *compared,
                      apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = eb->db;
  const apr_array_header_t *children;
  int i;
  svn_boolean_t in_anchor_not_target;
  apr_pool_t *iterpool;

  /* Everything we do below is useless if we are comparing to BASE. */
  if (eb->use_text_base)
    return SVN_NO_ERROR;

  /* Determine if this is the anchor directory if the anchor is different
     to the target. When the target is a file, the anchor is the parent
     directory and if this is that directory the non-target entries must be
     skipped. */
  in_anchor_not_target = ((*path == '\0') && (*eb->target != '\0'));

  /* Check for local property mods on this directory, if we haven't
     already reported them and we aren't changelist-filted.
     ### it should be noted that we do not currently allow directories
     ### to be part of changelists, so if a changelist is provided, the
     ### changelist check will always fail. */
  if (svn_wc__internal_changelist_match(db, local_abspath,
                                        eb->changelist_hash, scratch_pool)
      && (! in_anchor_not_target)
      && (!compared || ! apr_hash_get(compared, path, 0)))
    {
      svn_boolean_t modified;

      SVN_ERR(svn_wc__props_modified(&modified, db, local_abspath,
                                     scratch_pool));
      if (modified)
        {
          apr_array_header_t *propchanges;
          apr_hash_t *baseprops;

          SVN_ERR(svn_wc__internal_propdiff(&propchanges, &baseprops,
                                            db, local_abspath,
                                            scratch_pool, scratch_pool));

          SVN_ERR(eb->callbacks->dir_props_changed(NULL, NULL,
                                                   path, FALSE /* ### ? */,
                                                   propchanges, baseprops,
                                                   eb->callback_baton,
                                                   scratch_pool));
        }
    }

  if (depth == svn_depth_empty && !in_anchor_not_target)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_wc__db_read_children(&children, db, local_abspath,
                                   scratch_pool, iterpool));

  for (i = 0; i < children->nelts; i++)
    {
      const char *name = APR_ARRAY_IDX(children, i, const char*);
      const char *child_abspath, *child_path;
      svn_wc__db_status_t status;
      svn_wc__db_kind_t kind;

      svn_pool_clear(iterpool);

      if (eb->cancel_func)
        SVN_ERR(eb->cancel_func(eb->cancel_baton));

      /* In the anchor directory, if the anchor is not the target then all
         entries other than the target should not be diff'd. Running diff
         on one file in a directory should not diff other files in that
         directory. */
      if (in_anchor_not_target && strcmp(eb->target, name))
        continue;

      child_abspath = svn_dirent_join(local_abspath, name, iterpool);

      SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   db, child_abspath,
                                   iterpool, iterpool));

      if (status == svn_wc__db_status_not_present
          || status == svn_wc__db_status_excluded
          || status == svn_wc__db_status_absent)
        continue;

      child_path = svn_relpath_join(path, name, iterpool);

      /* Skip this node if it is in the list of nodes already diff'd. */
      if (compared && apr_hash_get(compared, child_path, APR_HASH_KEY_STRING))
        continue;

      switch (kind)
        {
        case svn_wc__db_kind_file:
        case svn_wc__db_kind_symlink:
          SVN_ERR(file_diff(eb, child_abspath, child_path, iterpool));
          break;

        case svn_wc__db_kind_dir:
          /* ### TODO: Don't know how to do replaced dirs. How do I get
             information about what is being replaced? If it was a
             directory then the directory elements are also going to be
             deleted. We need to show deletion diffs for these
             files. If it was a file we need to show a deletion diff
             for that file. */

          /* Check the subdir if in the anchor (the subdir is the target), or
             if recursive */
          if (in_anchor_not_target
              || (depth > svn_depth_files)
              || (depth == svn_depth_unknown))
            {
              svn_depth_t depth_below_here = depth;

              if (depth_below_here == svn_depth_immediates)
                depth_below_here = svn_depth_empty;

              SVN_ERR(walk_local_nodes_diff(eb,
                                            child_abspath,
                                            child_path,
                                            depth_below_here,
                                            NULL,
                                            iterpool));
            }
          break;

        default:
          break;
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Public Interface */
svn_error_t *
svn_wc_diff6(svn_wc_context_t *wc_ctx,
             const char *target_abspath,
             const svn_wc_diff_callbacks4_t *callbacks,
             void *callback_baton,
             svn_depth_t depth,
             svn_boolean_t ignore_ancestry,
             svn_boolean_t show_copies_as_adds,
             svn_boolean_t use_git_diff_format,
             const apr_array_header_t *changelists,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             apr_pool_t *pool)
{
  struct edit_baton *eb;
  const char *target;
  const char *anchor_abspath;
  svn_wc__db_kind_t kind;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(target_abspath));
  SVN_ERR(svn_wc__db_read_kind(&kind, wc_ctx->db, target_abspath, FALSE,
                               pool));

  if (kind == svn_wc__db_kind_dir)
    {
      anchor_abspath = target_abspath;
      target = "";
    }
  else
    svn_dirent_split(&anchor_abspath, &target, target_abspath, pool);

  SVN_ERR(make_edit_baton(&eb,
                          wc_ctx->db,
                          anchor_abspath,
                          target,
                          callbacks, callback_baton,
                          depth, ignore_ancestry, show_copies_as_adds,
                          use_git_diff_format,
                          FALSE, FALSE, changelists,
                          cancel_func, cancel_baton,
                          pool));

  SVN_ERR(walk_local_nodes_diff(eb,
                                eb->anchor_abspath,
                                "",
                                depth,
                                NULL,
                                eb->pool));

  return SVN_NO_ERROR;
}
