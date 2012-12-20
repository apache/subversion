/*
 * repos_diff.c -- The diff editor for comparing two repository versions
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
 */

/* This code uses an editor driven by a tree delta between two
 * repository revisions (REV1 and REV2). For each file encountered in
 * the delta the editor constructs two temporary files, one for each
 * revision. This necessitates a separate request for the REV1 version
 * of the file when the delta shows the file being modified or
 * deleted. Files that are added by the delta do not require a
 * separate request, the REV1 version is empty and the delta is
 * sufficient to construct the REV2 version. When both versions of
 * each file have been created the diff callback is invoked to display
 * the difference between the two files.  */

#include <apr_uri.h>
#include <apr_md5.h>

#include "svn_checksum.h"
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_props.h"
#include "svn_private_config.h"

#include "client.h"

#include "private/svn_subr_private.h"
#include "private/svn_wc_private.h"

/* Overall crawler editor baton.  */
struct edit_baton {
  /* The passed depth */
  svn_depth_t depth;

  /* The callback and calback argument that implement the file comparison
     function */
  const svn_wc_diff_callbacks4_t *diff_callbacks;
  void *diff_cmd_baton;

  /* RA_SESSION is the open session for making requests to the RA layer */
  svn_ra_session_t *ra_session;

  /* The rev1 from the '-r Rev1:Rev2' command line option */
  svn_revnum_t revision;

  /* The rev2 from the '-r Rev1:Rev2' option, specifically set by
     set_target_revision(). */
  svn_revnum_t target_revision;

  /* The path to a temporary empty file used for add/delete
     differences.  The path is cached here so that it can be reused,
     since all empty files are the same. */
  const char *empty_file;

  /* Empty hash used for adds. */
  apr_hash_t *empty_hash;

  /* Hash used to check replaced paths. Key is path relative CWD,
   * Value is *deleted_path_notify_t.
   * All allocations are from edit_baton's pool. */
  apr_hash_t *deleted_paths;

  /* If the func is non-null, send notifications of actions. */
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;

  /* TRUE if the operation needs to walk deleted dirs on the "old" side.
     FALSE otherwise. */
  svn_boolean_t walk_deleted_repos_dirs;

  /* Whether to report text deltas */
  svn_boolean_t text_deltas;

  /* A callback used to see if the client wishes to cancel the running
     operation. */
  svn_cancel_func_t cancel_func;

  /* A baton to pass to the cancellation callback. */
  void *cancel_baton;

  apr_pool_t *pool;
};

typedef struct deleted_path_notify_t
{
  svn_node_kind_t kind;
  svn_wc_notify_action_t action;
  svn_wc_notify_state_t state;
  svn_boolean_t tree_conflicted;
} deleted_path_notify_t;

/* Directory level baton.
 */
struct dir_baton {
  /* Gets set if the directory is added rather than replaced/unchanged. */
  svn_boolean_t added;

  /* Gets set if this operation caused a tree-conflict on this directory
   * (does not show tree-conflicts persisting from before this operation). */
  svn_boolean_t tree_conflicted;

  /* If TRUE, this node is skipped entirely.
   * This is used to skip all children of a tree-conflicted
   * directory without setting TREE_CONFLICTED to TRUE everywhere. */
  svn_boolean_t skip;

  /* If TRUE, all children of this directory are skipped. */
  svn_boolean_t skip_children;

  /* The path of the directory within the repository */
  const char *path;

  /* The baton for the parent directory, or null if this is the root of the
     hierarchy to be compared. */
  struct dir_baton *dir_baton;

  /* The overall crawler editor baton. */
  struct edit_baton *edit_baton;

  /* A cache of any property changes (svn_prop_t) received for this dir. */
  apr_array_header_t *propchanges;

  /* Boolean indicating whether a node property was changed */
  svn_boolean_t has_propchange;

  /* The pool passed in by add_dir, open_dir, or open_root.
     Also, the pool this dir baton is allocated in. */
  apr_pool_t *pool;

  /* Base revision of directory. */
  svn_revnum_t base_revision;
};

/* File level baton.
 */
struct file_baton {
  /* Gets set if the file is added rather than replaced. */
  svn_boolean_t added;

  /* Gets set if this operation caused a tree-conflict on this file
   * (does not show tree-conflicts persisting from before this operation). */
  svn_boolean_t tree_conflicted;

  /* If TRUE, this node is skipped entirely.
   * This is currently used to skip all children of a tree-conflicted
   * directory. */
  svn_boolean_t skip;

  /* The path of the file within the repository */
  const char *path;

  /* The path and APR file handle to the temporary file that contains the
     first repository version.  Also, the pristine-property list of
     this file. */
  const char *path_start_revision;
  apr_hash_t *pristine_props;
  svn_revnum_t base_revision;

  /* The path and APR file handle to the temporary file that contains the
     second repository version.  These fields are set when processing
     textdelta and file deletion, and will be NULL if there's no
     textual difference between the two revisions. */
  const char *path_end_revision;

  /* APPLY_HANDLER/APPLY_BATON represent the delta application baton. */
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;

  /* The overall crawler editor baton. */
  struct edit_baton *edit_baton;

  /* Holds the checksum of the start revision file */
  svn_checksum_t *start_md5_checksum;

  /* Holds the resulting md5 digest of a textdelta transform */
  unsigned char result_digest[APR_MD5_DIGESTSIZE];
  svn_checksum_t *result_md5_checksum;

  /* A cache of any property changes (svn_prop_t) received for this file. */
  apr_array_header_t *propchanges;

  /* Boolean indicating whether a node property was changed */
  svn_boolean_t has_propchange;

  /* The pool passed in by add_file or open_file.
     Also, the pool this file_baton is allocated in. */
  apr_pool_t *pool;
};

/* Create a new directory baton for PATH in POOL.  ADDED is set if
 * this directory is being added rather than replaced. PARENT_BATON is
 * the baton of the parent directory (or NULL if this is the root of
 * the comparison hierarchy). The directory and its parent may or may
 * not exist in the working copy.  EDIT_BATON is the overall crawler
 * editor baton.
 */
static struct dir_baton *
make_dir_baton(const char *path,
               struct dir_baton *parent_baton,
               struct edit_baton *edit_baton,
               svn_boolean_t added,
               svn_revnum_t base_revision,
               apr_pool_t *pool)
{
  apr_pool_t *dir_pool = svn_pool_create(pool);
  struct dir_baton *dir_baton = apr_pcalloc(dir_pool, sizeof(*dir_baton));

  dir_baton->dir_baton = parent_baton;
  dir_baton->edit_baton = edit_baton;
  dir_baton->added = added;
  dir_baton->tree_conflicted = FALSE;
  dir_baton->skip = FALSE;
  dir_baton->skip_children = FALSE;
  dir_baton->pool = dir_pool;
  dir_baton->path = apr_pstrdup(dir_pool, path);
  dir_baton->propchanges  = apr_array_make(pool, 8, sizeof(svn_prop_t));
  dir_baton->base_revision = base_revision;

  return dir_baton;
}

/* Create a new file baton for PATH in POOL, which is a child of
 * directory PARENT_PATH. ADDED is set if this file is being added
 * rather than replaced.  EDIT_BATON is a pointer to the global edit
 * baton.
 */
static struct file_baton *
make_file_baton(const char *path,
                svn_boolean_t added,
                struct edit_baton *edit_baton,
                apr_pool_t *pool)
{
  apr_pool_t *file_pool = svn_pool_create(pool);
  struct file_baton *file_baton = apr_pcalloc(file_pool, sizeof(*file_baton));

  file_baton->edit_baton = edit_baton;
  file_baton->added = added;
  file_baton->tree_conflicted = FALSE;
  file_baton->skip = FALSE;
  file_baton->pool = file_pool;
  file_baton->path = apr_pstrdup(file_pool, path);
  file_baton->propchanges  = apr_array_make(pool, 8, sizeof(svn_prop_t));
  file_baton->base_revision = edit_baton->revision;

  return file_baton;
}

/* Helper function: return up to two svn:mime-type values buried
 * within a file baton.  Set *MIMETYPE1 to the value within the file's
 * pristine properties, or NULL if not available.  Set *MIMETYPE2 to
 * the value within the "new" file's propchanges, or NULL if not
 * available.
 */
static void
get_file_mime_types(const char **mimetype1,
                    const char **mimetype2,
                    struct file_baton *fb)
{
  /* Defaults */
  *mimetype1 = NULL;
  *mimetype2 = NULL;

  if (fb->pristine_props)
    {
      svn_string_t *pristine_val;
      pristine_val = apr_hash_get(fb->pristine_props, SVN_PROP_MIME_TYPE,
                                  strlen(SVN_PROP_MIME_TYPE));
      if (pristine_val)
        *mimetype2 = *mimetype1 = pristine_val->data;
    }

  if (fb->propchanges)
    {
      int i;
      svn_prop_t *propchange;

      for (i = 0; i < fb->propchanges->nelts; i++)
        {
          propchange = &APR_ARRAY_IDX(fb->propchanges, i, svn_prop_t);
          if (strcmp(propchange->name, SVN_PROP_MIME_TYPE) == 0)
            {
              if (propchange->value)
                *mimetype2 = propchange->value->data;
              break;
            }
        }
    }
}


/* Get revision FB->base_revision of the file described by FB from the
 * repository, through FB->edit_baton->ra_session.
 *
 * Unless PROPS_ONLY is true:
 *   Set FB->path_start_revision to the path of a new temporary file containing
 *   the file's text.
 *   Set FB->start_md5_checksum to that file's MD-5 checksum.
 *   Install a pool cleanup handler on FB->pool to delete the file.
 *
 * Always:
 *   Set FB->pristine_props to a new hash containing the file's properties.
 *
 * Allocate all results in FB->pool.
 */
static svn_error_t *
get_file_from_ra(struct file_baton *fb,
                 svn_boolean_t props_only,
                 apr_pool_t *scratch_pool)
{
  if (! props_only)
    {
      svn_stream_t *fstream;

      SVN_ERR(svn_stream_open_unique(&fstream, &(fb->path_start_revision),
                                     NULL, svn_io_file_del_on_pool_cleanup,
                                     fb->pool, scratch_pool));

      fstream = svn_stream_checksummed2(fstream, NULL, &fb->start_md5_checksum,
                                        svn_checksum_md5, TRUE, scratch_pool);

      /* Retrieve the file and its properties */
      SVN_ERR(svn_ra_get_file(fb->edit_baton->ra_session,
                              fb->path,
                              fb->base_revision,
                              fstream, NULL,
                              &(fb->pristine_props),
                              fb->pool));
      SVN_ERR(svn_stream_close(fstream));
    }
  else
    {
      SVN_ERR(svn_ra_get_file(fb->edit_baton->ra_session,
                              fb->path,
                              fb->base_revision,
                              NULL, NULL,
                              &(fb->pristine_props),
                              fb->pool));
    }

  return SVN_NO_ERROR;
}

/* Remove every no-op property change from CHANGES: that is, remove every
   entry in which the target value is the same as the value of the
   corresponding property in PRISTINE_PROPS.

     Issue #3657 'dav update report handler in skelta mode can cause
     spurious conflicts'.  When communicating with the repository via ra_serf,
     the change_dir_prop and change_file_prop svn_delta_editor_t
     callbacks are called (obviously) when a directory or file property has
     changed between the start and end of the edit.  Less obvious however,
     is that these callbacks may be made describing *all* of the properties
     on FILE_BATON->PATH when using the DAV providers, not just the change(s).
     (Specifically ra_serf does it for diff/merge/update/switch).

     This means that the change_[file|dir]_prop svn_delta_editor_t callbacks
     may be made where there are no property changes (i.e. a noop change of
     NAME from VALUE to VALUE).  Normally this is harmless, but during a
     merge it can result in spurious conflicts if the WC's pristine property
     NAME has a value other than VALUE.  In an ideal world the mod_dav_svn
     update report handler, when in 'skelta' mode and describing changes to
     a path on which a property has changed, wouldn't ask the client to later
     fetch all properties and figure out what has changed itself.  The server
     already knows which properties have changed!

     Regardless, such a change is not yet implemented, and even when it is,
     the client should DTRT with regard to older servers which behave this
     way.  Hence this little hack:  We populate FILE_BATON->PROPCHANGES only
     with *actual* property changes.

     See http://subversion.tigris.org/issues/show_bug.cgi?id=3657#desc9 and
     http://svn.haxx.se/dev/archive-2010-08/0351.shtml for more details.
 */
static void
remove_non_prop_changes(apr_hash_t *pristine_props,
                        apr_array_header_t *changes)
{
  int i;

  for (i = 0; i < changes->nelts; i++)
    {
      svn_prop_t *change = &APR_ARRAY_IDX(changes, i, svn_prop_t);

      if (change->value)
        {
          const svn_string_t *old_val = apr_hash_get(pristine_props,
                                                     change->name,
                                                     APR_HASH_KEY_STRING);

          if (old_val && svn_string_compare(old_val, change->value))
            {
              int j;

              /* Remove the matching change by shifting the rest */
              for (j = i; j < changes->nelts - 1; j++)
                {
                  APR_ARRAY_IDX(changes, j, svn_prop_t)
                       = APR_ARRAY_IDX(changes, j+1, svn_prop_t);
                }
              changes->nelts--;
            }
        }
    }
}


/* Get the empty file associated with the edit baton. This is cached so
 * that it can be reused, all empty files are the same.
 */
static svn_error_t *
get_empty_file(struct edit_baton *eb,
               const char **empty_file_path)
{
  /* Create the file if it does not exist */
  /* Note that we tried to use /dev/null in r857294, but
     that won't work on Windows: it's impossible to stat NUL */
  if (!eb->empty_file)
    SVN_ERR(svn_io_open_unique_file3(NULL, &(eb->empty_file), NULL,
                                     svn_io_file_del_on_pool_cleanup,
                                     eb->pool, eb->pool));

  *empty_file_path = eb->empty_file;

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function.  */
static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  eb->target_revision = target_revision;
  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. The root of the comparison hierarchy */
static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *db = make_dir_baton("", NULL, eb, FALSE, base_revision,
                                        pool);

  *root_baton = db;
  return SVN_NO_ERROR;
}

/* Compare a file being deleted against an empty file.
 */
static svn_error_t *
diff_deleted_file(svn_wc_notify_state_t *state_p,
                  svn_boolean_t *tree_conflicted_p,
                  const char *path,
                  struct edit_baton *eb,
                  apr_pool_t *scratch_pool)
{
  struct file_baton *fb = make_file_baton(path, FALSE, eb, scratch_pool);
/*  struct edit_baton *eb = fb->edit_baton;*/
  const char *mimetype1, *mimetype2;

  if (eb->cancel_func)
    SVN_ERR(eb->cancel_func(eb->cancel_baton));

  if (eb->text_deltas)
    SVN_ERR(get_file_from_ra(fb, FALSE, scratch_pool));
  else
    SVN_ERR(get_empty_file(eb, &fb->path_start_revision));
  SVN_ERR(get_empty_file(eb, &fb->path_end_revision));
  get_file_mime_types(&mimetype1, &mimetype2, fb);

  SVN_ERR(eb->diff_callbacks->file_deleted(state_p, tree_conflicted_p,
                                           fb->path,
                                           fb->path_start_revision,
                                           fb->path_end_revision,
                                           mimetype1, mimetype2,
                                           fb->pristine_props,
                                           eb->diff_cmd_baton,
                                           scratch_pool));
  return SVN_NO_ERROR;
}

/* Recursively walk tree rooted at DIR (at EB->revision) in the repository,
 * reporting all children as deleted.  Part of a workaround for issue 2333.
 *
 * DIR is a repository path relative to the URL in EB->ra_session.  EB is
 * the overall crawler editor baton.  EB->revision must be a valid revision
 * number, not SVN_INVALID_REVNUM.  Use EB->cancel_func (if not null) with
 * EB->cancel_baton for cancellation.
 */
/* ### TODO: Handle depth. */
static svn_error_t *
diff_deleted_dir(svn_wc_notify_state_t *state_p,
                 svn_boolean_t *tree_conflicted_p,
                 const char *dir,
                 struct edit_baton *eb,
                 apr_pool_t *pool)
{
  apr_hash_t *dirents;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_hash_index_t *hi;

  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(eb->revision));

  if (eb->cancel_func)
    SVN_ERR(eb->cancel_func(eb->cancel_baton));

  SVN_ERR(eb->diff_callbacks->dir_deleted(
                        state_p, tree_conflicted_p, dir,
                        eb->diff_cmd_baton, pool));

  /* The "old" dir will be skipped by the repository report.  If required,
   * crawl it recursively, diffing each file against the empty file.  This
   * is a workaround for issue 2333 "'svn diff URL1 URL2' not reverse of
   * 'svn diff URL2 URL1'". */
  if (! eb->walk_deleted_repos_dirs)
    {
      svn_pool_destroy(iterpool);
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_ra_get_dir2(eb->ra_session,
                          &dirents,
                          NULL, NULL,
                          dir,
                          eb->revision,
                          SVN_DIRENT_KIND,
                          pool));

  for (hi = apr_hash_first(pool, dirents); hi;
       hi = apr_hash_next(hi))
    {
      const char *path;
      const char *name = svn__apr_hash_index_key(hi);
      svn_dirent_t *dirent = svn__apr_hash_index_val(hi);

      svn_pool_clear(iterpool);

      path = svn_relpath_join(dir, name, iterpool);

      if (dirent->kind == svn_node_file)
        {
          SVN_ERR(diff_deleted_file(NULL, NULL, path, eb, iterpool));
        }

      if (dirent->kind == svn_node_dir)
        {
          SVN_ERR(diff_deleted_dir(NULL, NULL, path, eb, iterpool));
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function.  */
static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t base_revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  svn_node_kind_t kind;
  svn_wc_notify_state_t state = svn_wc_notify_state_inapplicable;
  svn_wc_notify_action_t action = svn_wc_notify_skip;
  svn_boolean_t tree_conflicted = FALSE;
  apr_pool_t *scratch_pool;

  /* Skip *everything* within a newly tree-conflicted directory,
   * and directories the children of which should be skipped. */
  if (pb->skip || pb->tree_conflicted || pb->skip_children)
    return SVN_NO_ERROR;

  scratch_pool = svn_pool_create(eb->pool);

  /* We need to know if this is a directory or a file */
  SVN_ERR(svn_ra_check_path(eb->ra_session, path, eb->revision, &kind,
                            scratch_pool));

  switch (kind)
    {
    case svn_node_file:
      {
        SVN_ERR(diff_deleted_file(&state, &tree_conflicted, path, eb,
                                  scratch_pool));
        break;
      }
    case svn_node_dir:
      {
        SVN_ERR(diff_deleted_dir(&state, &tree_conflicted, path, eb,
                                 scratch_pool));
        break;
      }
    default:
      break;
    }

  if ((state != svn_wc_notify_state_missing)
      && (state != svn_wc_notify_state_obstructed)
      && !tree_conflicted)
    {
      action = svn_wc_notify_update_delete;
    }

  if (eb->notify_func)
    {
      const char *deleted_path = apr_pstrdup(eb->pool, path);
      deleted_path_notify_t *dpn = apr_pcalloc(eb->pool, sizeof(*dpn));

      dpn->kind = kind;
      dpn->action = tree_conflicted ? svn_wc_notify_tree_conflict : action;
      dpn->state = state;
      dpn->tree_conflicted = tree_conflicted;
      apr_hash_set(eb->deleted_paths, deleted_path, APR_HASH_KEY_STRING, dpn);
    }

  svn_pool_destroy(scratch_pool);

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function.  */
static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *pool,
              void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *db;
  svn_wc_notify_state_t state;

  /* ### TODO: support copyfrom? */

  db = make_dir_baton(path, pb, eb, TRUE, SVN_INVALID_REVNUM, pool);
  *child_baton = db;

  /* Skip *everything* within a newly tree-conflicted directory,
   * and directories the children of which should be skipped. */
  if (pb->skip || pb->tree_conflicted || pb->skip_children)
    {
      db->skip = TRUE;
      return SVN_NO_ERROR;
    }


  SVN_ERR(eb->diff_callbacks->dir_added(
                &state, &db->tree_conflicted,
                &db->skip, &db->skip_children, db->path,
                eb->target_revision, copyfrom_path, copyfrom_revision,
                eb->diff_cmd_baton, pool));

  /* Notifications for directories are done at close_directory time.
   * But for paths at which the editor drive adds directories, we make an
   * exception to this rule, so that the path appears in the output before
   * any children of the newly added directory. Since a deletion at this path
   * must have happened before this addition, we can safely notify about
   * replaced directories here, too. */
  if (eb->notify_func)
    {
      deleted_path_notify_t *dpn;
      svn_wc_notify_t *notify;
      svn_wc_notify_action_t action;
      svn_node_kind_t kind = svn_node_dir;

      /* Find out if a pending delete notification for this path is
       * still around. */
      dpn = apr_hash_get(eb->deleted_paths, db->path, APR_HASH_KEY_STRING);
      if (dpn)
        {
          /* If any was found, we will handle the pending 'deleted path
           * notification' (DPN) here. Remove it from the list. */
          apr_hash_set(eb->deleted_paths, db->path,
                       APR_HASH_KEY_STRING, NULL);

          /* the pending delete might be on a different node kind. */
          kind = dpn->kind;
          state = dpn->state;
        }

      /* Determine what the notification (ACTION) should be.
       * In case of a pending 'delete', this might become a 'replace'. */
      if (db->tree_conflicted)
        action = svn_wc_notify_tree_conflict;
      else if (dpn)
        {
          if (dpn->action == svn_wc_notify_update_delete)
            action = svn_wc_notify_update_replace;
          else
            /* Note: dpn->action might be svn_wc_notify_tree_conflict */
            action = dpn->action;
        }
      else if (state == svn_wc_notify_state_missing ||
               state == svn_wc_notify_state_obstructed)
        action = svn_wc_notify_skip;
      else
        action = svn_wc_notify_update_add;

      notify = svn_wc_create_notify(db->path, action, pool);
      notify->kind = kind;
      notify->content_state = notify->prop_state = state;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function.  */
static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *db;

  db = make_dir_baton(path, pb, eb, FALSE, base_revision, pool);

  *child_baton = db;

  /* Skip *everything* within a newly tree-conflicted directory
   * and directories the children of which should be skipped. */
  if (pb->skip || pb->tree_conflicted || pb->skip_children)
    {
      db->skip = TRUE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(eb->diff_callbacks->dir_opened(
                &db->tree_conflicted, &db->skip,
                &db->skip_children, db->path, base_revision,
                eb->diff_cmd_baton, pool));

  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function.  */
static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *pool,
         void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *fb;

  /* ### TODO: support copyfrom? */

  fb = make_file_baton(path, TRUE, pb->edit_baton, pool);
  *file_baton = fb;

  /* Skip *everything* within a newly tree-conflicted directory.
   * and directories the children of which should be skipped. */
  if (pb->skip || pb->tree_conflicted || pb->skip_children)
    {
      fb->skip = TRUE;
      return SVN_NO_ERROR;
    }

  fb->pristine_props = pb->edit_baton->empty_hash;

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function.  */
static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *fb;
  struct edit_baton *eb = pb->edit_baton;
  fb = make_file_baton(path, FALSE, pb->edit_baton, pool);
  *file_baton = fb;

  /* Skip *everything* within a newly tree-conflicted directory
   * and directories the children of which should be skipped. */
  if (pb->skip || pb->tree_conflicted || pb->skip_children)
    {
      fb->skip = TRUE;
      return SVN_NO_ERROR;
    }

  fb->base_revision = base_revision;

  SVN_ERR(eb->diff_callbacks->file_opened(
                   &fb->tree_conflicted, &fb->skip,
                   fb->path, base_revision, eb->diff_cmd_baton, pool));

  return SVN_NO_ERROR;
}

/* Do the work of applying the text delta.  */
static svn_error_t *
window_handler(svn_txdelta_window_t *window,
               void *window_baton)
{
  struct file_baton *fb = window_baton;

  SVN_ERR(fb->apply_handler(window, fb->apply_baton));

  if (!window)
    {
      fb->result_md5_checksum = svn_checksum__from_digest_md5(
                                        fb->result_digest,
                                        fb->pool);
    }

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function.  */
static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_md5_digest,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct file_baton *fb = file_baton;
  svn_stream_t *src_stream;
  svn_stream_t *result_stream;
  apr_pool_t *scratch_pool = fb->pool;

  /* Skip *everything* within a newly tree-conflicted directory. */
  if (fb->skip)
    {
      *handler = svn_delta_noop_window_handler;
      *handler_baton = NULL;
      return SVN_NO_ERROR;
    }

  /* If we're not sending file text, then ignore any that we receive. */
  if (! fb->edit_baton->text_deltas)
    {
      /* Supply valid paths to indicate there is a text change. */
      SVN_ERR(get_empty_file(fb->edit_baton, &fb->path_start_revision));
      SVN_ERR(get_empty_file(fb->edit_baton, &fb->path_end_revision));

      *handler = svn_delta_noop_window_handler;
      *handler_baton = NULL;

      return SVN_NO_ERROR;
    }

  /* We need the expected pristine file, so go get it */
  if (!fb->added)
    SVN_ERR(get_file_from_ra(fb, FALSE, scratch_pool));
  else
    SVN_ERR(get_empty_file(fb->edit_baton, &(fb->path_start_revision)));

  SVN_ERR_ASSERT(fb->path_start_revision != NULL);

  if (base_md5_digest != NULL)
    {
      svn_checksum_t *base_md5_checksum;

      SVN_ERR(svn_checksum_parse_hex(&base_md5_checksum, svn_checksum_md5,
                                     base_md5_digest, scratch_pool));

      if (!svn_checksum_match(base_md5_checksum, fb->start_md5_checksum))
        return svn_error_trace(svn_checksum_mismatch_err(
                                      base_md5_checksum,
                                      fb->start_md5_checksum,
                                      scratch_pool,
                                      _("Base checksum mismatch for '%s'"),
                                      fb->path));
    }

  /* Open the file to be used as the base for second revision */
  SVN_ERR(svn_stream_open_readonly(&src_stream, fb->path_start_revision,
                                   scratch_pool, scratch_pool));

  /* Open the file that will become the second revision after applying the
     text delta, it starts empty */
  SVN_ERR(svn_stream_open_unique(&result_stream, &fb->path_end_revision, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 scratch_pool, scratch_pool));

  svn_txdelta_apply(src_stream,
                    result_stream,
                    fb->result_digest,
                    fb->path, fb->pool,
                    &(fb->apply_handler), &(fb->apply_baton));

  *handler = window_handler;
  *handler_baton = file_baton;

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function.  When the file is closed we have a temporary
 * file containing a pristine version of the repository file. This can
 * be compared against the working copy.
 *
 * ### Ignore TEXT_CHECKSUM for now.  Someday we can use it to verify
 * ### the integrity of the file being diffed.  Done efficiently, this
 * ### would probably involve calculating the checksum as the data is
 * ### received, storing the final checksum in the file_baton, and
 * ### comparing against it here.
 */
static svn_error_t *
close_file(void *file_baton,
           const char *expected_md5_digest,
           apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;
  svn_wc_notify_state_t content_state = svn_wc_notify_state_unknown;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unknown;
  apr_pool_t *scratch_pool;

  /* Skip *everything* within a newly tree-conflicted directory. */
  if (fb->skip)
    {
      svn_pool_destroy(fb->pool);
      return SVN_NO_ERROR;
    }

  scratch_pool = fb->pool;

  if (expected_md5_digest && eb->text_deltas)
    {
      svn_checksum_t *expected_md5_checksum;

      SVN_ERR(svn_checksum_parse_hex(&expected_md5_checksum, svn_checksum_md5,
                                     expected_md5_digest, scratch_pool));

      if (!svn_checksum_match(expected_md5_checksum, fb->result_md5_checksum))
        return svn_error_trace(svn_checksum_mismatch_err(
                                      expected_md5_checksum,
                                      fb->result_md5_checksum,
                                      pool,
                                      _("Checksum mismatch for '%s'"),
                                      fb->path));
    }

  if (fb->path_end_revision || fb->has_propchange)
    {
      const char *mimetype1, *mimetype2;

      if (!fb->added && !fb->pristine_props)
        {
          /* We didn't receive a text change, so we have no pristine props.
             Retrieve just the props now. */
          SVN_ERR(get_file_from_ra(fb, TRUE, scratch_pool));
        }

      if (! fb->pristine_props)
        remove_non_prop_changes(fb->pristine_props, fb->propchanges);

      get_file_mime_types(&mimetype1, &mimetype2, fb);


      if (fb->added)
        SVN_ERR(eb->diff_callbacks->file_added(
                 &content_state, &prop_state, &fb->tree_conflicted,
                 fb->path,
                 fb->path_end_revision ? fb->path_start_revision : NULL,
                 fb->path_end_revision,
                 0,
                 eb->target_revision,
                 mimetype1, mimetype2,
                 NULL, SVN_INVALID_REVNUM,
                 fb->propchanges, fb->pristine_props,
                 eb->diff_cmd_baton,
                 scratch_pool));
      else
        SVN_ERR(eb->diff_callbacks->file_changed(
                 &content_state, &prop_state,
                 &fb->tree_conflicted, fb->path,
                 fb->path_end_revision ? fb->path_start_revision : NULL,
                 fb->path_end_revision,
                 eb->revision,
                 eb->target_revision,
                 mimetype1, mimetype2,
                 fb->propchanges, fb->pristine_props,
                 eb->diff_cmd_baton,
                 scratch_pool));
    }


  if (eb->notify_func)
    {
      deleted_path_notify_t *dpn;
      svn_wc_notify_t *notify;
      svn_wc_notify_action_t action;
      svn_node_kind_t kind = svn_node_file;

      /* Find out if a pending delete notification for this path is
       * still around. */
      dpn = apr_hash_get(eb->deleted_paths, fb->path, APR_HASH_KEY_STRING);
      if (dpn)
        {
          /* If any was found, we will handle the pending 'deleted path
           * notification' (DPN) here. Remove it from the list. */
          apr_hash_set(eb->deleted_paths, fb->path,
                       APR_HASH_KEY_STRING, NULL);

          /* the pending delete might be on a different node kind. */
          kind = dpn->kind;
          content_state = prop_state = dpn->state;
        }

      /* Determine what the notification (ACTION) should be.
       * In case of a pending 'delete', this might become a 'replace'. */
      if (fb->tree_conflicted)
        action = svn_wc_notify_tree_conflict;
      else if (dpn)
        {
          if (dpn->action == svn_wc_notify_update_delete
              && fb->added)
            action = svn_wc_notify_update_replace;
          else
            /* Note: dpn->action might be svn_wc_notify_tree_conflict */
            action = dpn->action;
        }
      else if ((content_state == svn_wc_notify_state_missing)
                || (content_state == svn_wc_notify_state_obstructed))
        action = svn_wc_notify_skip;
      else if (fb->added)
        action = svn_wc_notify_update_add;
      else
        action = svn_wc_notify_update_update;

      notify = svn_wc_create_notify(fb->path, action, scratch_pool);
      notify->kind = kind;
      notify->content_state = content_state;
      notify->prop_state = prop_state;
      (*eb->notify_func)(eb->notify_baton, notify, scratch_pool);
    }

  svn_pool_destroy(fb->pool); /* Destroy file and scratch pool */

  return SVN_NO_ERROR;
}

/* Report any accumulated prop changes via the 'dir_props_changed' callback,
 * and then call the 'dir_closed' callback.  Notify about any deleted paths
 * within this directory that have not already been notified, and then about
 * this directory itself (unless it was added, in which case the notification
 * was done at that time).
 *
 * An svn_delta_editor_t function.  */
static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;
  svn_wc_notify_state_t content_state = svn_wc_notify_state_unknown;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unknown;
  svn_boolean_t skipped = FALSE;
  apr_pool_t *scratch_pool;
  apr_hash_t *pristine_props;

  /* Skip *everything* within a newly tree-conflicted directory. */
  if (db->skip)
    {
      svn_pool_destroy(db->pool);
      return SVN_NO_ERROR;
    }

  scratch_pool = db->pool;

  if (db->has_propchange)
    {
      if (db->added)
        {
          pristine_props = eb->empty_hash;
        }
      else
        {
          SVN_ERR(svn_ra_get_dir2(eb->ra_session, NULL, NULL, &pristine_props,
                                  db->path, db->base_revision, 0, scratch_pool));
        }

      if (db->propchanges->nelts > 0)
        {
          remove_non_prop_changes(pristine_props, db->propchanges);
        }

      if (db->propchanges->nelts > 0)
        {
          svn_boolean_t tree_conflicted = FALSE;
          SVN_ERR(eb->diff_callbacks->dir_props_changed(
                   &prop_state, &tree_conflicted,
                   db->path, db->added,
                   db->propchanges, pristine_props,
                   eb->diff_cmd_baton, scratch_pool));
          if (tree_conflicted)
            db->tree_conflicted = TRUE;

          if (prop_state == svn_wc_notify_state_obstructed
              || prop_state == svn_wc_notify_state_missing)
            {
              content_state = prop_state;
              skipped = TRUE;
            }
        }
    }

  SVN_ERR(eb->diff_callbacks->dir_closed(NULL, NULL, NULL,
                                         db->path, db->added,
                                         eb->diff_cmd_baton,
                                         scratch_pool));

  /* Notify about any deleted paths within this directory that have not
   * already been notified. */
  if (!skipped && !db->added && eb->notify_func)
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(pool, eb->deleted_paths); hi;
           hi = apr_hash_next(hi))
        {
          svn_wc_notify_t *notify;
          const char *deleted_path = svn__apr_hash_index_key(hi);
          deleted_path_notify_t *dpn = svn__apr_hash_index_val(hi);

          /* Ignore paths which are not children of bb->path.  (There
             should be none due to editor ordering constraints, but
             ra_serf drops the ball here -- see issue #3802 for
             details.) */
          if (! svn_relpath_skip_ancestor(db->path, deleted_path))
            continue;

          notify = svn_wc_create_notify(deleted_path, dpn->action, pool);
          notify->kind = dpn->kind;
          notify->content_state = notify->prop_state = dpn->state;
          notify->lock_state = svn_wc_notify_lock_state_inapplicable;
          (*eb->notify_func)(eb->notify_baton, notify, pool);
          apr_hash_set(eb->deleted_paths, deleted_path,
                       APR_HASH_KEY_STRING, NULL);
        }
    }

  /* Notify about this directory itself (unless it was added, in which
   * case the notification was done at that time). */
  if (!db->added && eb->notify_func)
    {
      svn_wc_notify_t *notify;
      svn_wc_notify_action_t action;

      if (db->tree_conflicted)
        action = svn_wc_notify_tree_conflict;
      else if (skipped)
        action = svn_wc_notify_skip;
      else
        action = svn_wc_notify_update_update;

      notify = svn_wc_create_notify(db->path, action, pool);
      notify->kind = svn_node_dir;

      /* In case of a tree conflict during merge, the diff callback
       * sets content_state appropriately. So copy the state into the
       * notify_t to make sure conflicts get displayed. */
      notify->content_state = content_state;

      notify->prop_state = prop_state;
      notify->lock_state = svn_wc_notify_lock_state_inapplicable;
      (*eb->notify_func)(eb->notify_baton, notify, scratch_pool);
    }

  svn_pool_destroy(db->pool); /* Destroy baton and scratch_pool */

  return SVN_NO_ERROR;
}


/* Record a prop change, which we will report later in close_file().
 *
 * An svn_delta_editor_t function.  */
static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  svn_prop_t *propchange;

  /* Skip *everything* within a newly tree-conflicted directory. */
  if (fb->skip)
    return SVN_NO_ERROR;

  if (!fb->has_propchange && svn_property_kind2(name) == svn_prop_regular_kind)
    fb->has_propchange = TRUE;

  propchange = apr_array_push(fb->propchanges);
  propchange->name = apr_pstrdup(fb->pool, name);
  propchange->value = value ? svn_string_dup(value, fb->pool) : NULL;

  return SVN_NO_ERROR;
}

/* Make a note of this prop change, to be reported when the dir is closed.
 *
 * An svn_delta_editor_t function.  */
static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  svn_prop_t *propchange;

  /* Skip *everything* within a newly tree-conflicted directory. */
  if (db->skip)
    return SVN_NO_ERROR;

  if (!db->has_propchange && svn_property_kind2(name) == svn_prop_regular_kind)
    db->has_propchange = TRUE;

  propchange = apr_array_push(db->propchanges);
  propchange->name = apr_pstrdup(db->pool, name);
  propchange->value = value ? svn_string_dup(value, db->pool) : NULL;

  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function.  */
static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  svn_pool_destroy(eb->pool);

  return SVN_NO_ERROR;
}

/* Notify that the node at PATH is 'missing'.
 * An svn_delta_editor_t function.  */
static svn_error_t *
absent_directory(const char *path,
                 void *parent_baton,
                 apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  /* ### TODO: Raise a tree-conflict?? I sure hope not.*/

  if (eb->notify_func)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(path, svn_wc_notify_skip, pool);

      notify->kind = svn_node_dir;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_missing;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}


/* Notify that the node at PATH is 'missing'.
 * An svn_delta_editor_t function.  */
static svn_error_t *
absent_file(const char *path,
            void *parent_baton,
            apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  /* ### TODO: Raise a tree-conflict?? I sure hope not.*/

  if (eb->notify_func)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(path, svn_wc_notify_skip, pool);

      notify->kind = svn_node_file;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_missing;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_kind_func(svn_kind_t *kind,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  svn_node_kind_t node_kind;

  if (!SVN_IS_VALID_REVNUM(base_revision))
    base_revision = eb->revision;

  SVN_ERR(svn_ra_check_path(eb->ra_session, path, base_revision, &node_kind,
                            scratch_pool));

  *kind = svn__kind_from_node_kind(node_kind, FALSE);
  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_props_func(apr_hash_t **props,
                 void *baton,
                 const char *path,
                 svn_revnum_t base_revision,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  svn_node_kind_t node_kind;

  if (!SVN_IS_VALID_REVNUM(base_revision))
    base_revision = eb->revision;

  SVN_ERR(svn_ra_check_path(eb->ra_session, path, base_revision, &node_kind,
                            scratch_pool));

  if (node_kind == svn_node_file)
    {
      SVN_ERR(svn_ra_get_file(eb->ra_session, path, base_revision,
                              NULL, NULL, props, result_pool));
    }
  else if (node_kind == svn_node_dir)
    {
      apr_array_header_t *tmp_props;

      SVN_ERR(svn_ra_get_dir2(eb->ra_session, NULL, NULL, props, path,
                              base_revision, 0 /* Dirent fields */,
                              result_pool));
      tmp_props = svn_prop_hash_to_array(*props, result_pool);
      SVN_ERR(svn_categorize_props(tmp_props, NULL, NULL, &tmp_props,
                                   result_pool));
      *props = svn_prop_array_to_hash(tmp_props, result_pool);
    }
  else
    {
      *props = apr_hash_make(result_pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_base_func(const char **filename,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  svn_stream_t *fstream;
  svn_error_t *err;

  if (!SVN_IS_VALID_REVNUM(base_revision))
    base_revision = eb->revision;

  SVN_ERR(svn_stream_open_unique(&fstream, filename, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 result_pool, scratch_pool));

  err = svn_ra_get_file(eb->ra_session, path, base_revision,
                        fstream, NULL, NULL, scratch_pool);
  if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
    {
      svn_error_clear(err);
      SVN_ERR(svn_stream_close(fstream));

      *filename = NULL;
      return SVN_NO_ERROR;
    }
  else if (err)
    return svn_error_trace(err);

  SVN_ERR(svn_stream_close(fstream));

  return SVN_NO_ERROR;
}

/* Create a repository diff editor and baton.  */
svn_error_t *
svn_client__get_diff_editor(const svn_delta_editor_t **editor,
                            void **edit_baton,
                            svn_depth_t depth,
                            svn_ra_session_t *ra_session,
                            svn_revnum_t revision,
                            svn_boolean_t walk_deleted_dirs,
                            svn_boolean_t text_deltas,
                            const svn_wc_diff_callbacks4_t *diff_callbacks,
                            void *diff_cmd_baton,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            svn_wc_notify_func2_t notify_func,
                            void *notify_baton,
                            apr_pool_t *result_pool)
{
  apr_pool_t *editor_pool = svn_pool_create(result_pool);
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(editor_pool);
  struct edit_baton *eb = apr_pcalloc(editor_pool, sizeof(*eb));
  svn_delta_shim_callbacks_t *shim_callbacks =
                                svn_delta_shim_callbacks_default(editor_pool);

  eb->pool = editor_pool;
  eb->depth = depth;
  eb->diff_callbacks = diff_callbacks;
  eb->diff_cmd_baton = diff_cmd_baton;
  eb->ra_session = ra_session;

  eb->revision = revision;
  eb->empty_file = NULL;
  eb->empty_hash = apr_hash_make(eb->pool);
  eb->deleted_paths = apr_hash_make(eb->pool);
  eb->notify_func = notify_func;
  eb->notify_baton = notify_baton;
  eb->walk_deleted_repos_dirs = walk_deleted_dirs;
  eb->text_deltas = text_deltas;
  eb->cancel_func = cancel_func;
  eb->cancel_baton = cancel_baton;

  tree_editor->set_target_revision = set_target_revision;
  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->close_file = close_file;
  tree_editor->close_directory = close_directory;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->change_dir_prop = change_dir_prop;
  tree_editor->close_edit = close_edit;
  tree_editor->absent_directory = absent_directory;
  tree_editor->absent_file = absent_file;

  SVN_ERR(svn_delta_get_cancellation_editor(cancel_func, cancel_baton,
                                            tree_editor, eb,
                                            editor, edit_baton,
                                            eb->pool));

  shim_callbacks->fetch_kind_func = fetch_kind_func;
  shim_callbacks->fetch_props_func = fetch_props_func;
  shim_callbacks->fetch_base_func = fetch_base_func;
  shim_callbacks->fetch_baton = eb;

  SVN_ERR(svn_editor__insert_shims(editor, edit_baton, *editor, *edit_baton,
                                   NULL, NULL, shim_callbacks,
                                   result_pool, result_pool));

  return SVN_NO_ERROR;
}
