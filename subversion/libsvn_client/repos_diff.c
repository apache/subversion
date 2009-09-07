/*
 * repos_diff.c -- The diff editor for comparing two repository versions
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

#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_props.h"

#include "client.h"

/* Overall crawler editor baton.  */
struct edit_baton {
  /* TARGET is a working-copy directory which corresponds to the base
     URL open in RA_SESSION below. */
  const char *target;

  /* ADM_ACCESS is an access baton that includes the TARGET directory */
  svn_wc_adm_access_t *adm_access;

  /* The callback and calback argument that implement the file comparison
     function */
  const svn_wc_diff_callbacks3_t *diff_callbacks;
  void *diff_cmd_baton;

  /* DRY_RUN is TRUE if this is a dry-run diff, false otherwise. */
  svn_boolean_t dry_run;

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
   * This is currently used to skip all children of a tree-conflicted
   * directory without setting TREE_CONFLICTED to TRUE everywhere. */
  svn_boolean_t skip;

  /* The path of the directory within the repository */
  const char *path;

  /* The path of the directory in the wc, relative to cwd */
  const char *wcpath;

  /* The baton for the parent directory, or null if this is the root of the
     hierarchy to be compared. */
  struct dir_baton *dir_baton;

  /* The overall crawler editor baton. */
  struct edit_baton *edit_baton;

  /* A cache of any property changes (svn_prop_t) received for this dir. */
  apr_array_header_t *propchanges;

  /* The pristine-property list attached to this directory. */
  apr_hash_t *pristine_props;


  /* The pool passed in by add_dir, open_dir, or open_root.
     Also, the pool this dir baton is allocated in. */
  apr_pool_t *pool;
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

  /* The path of the file in the wc, relative to cwd */
  const char *wcpath;

  /* The path and APR file handle to the temporary file that contains the
     first repository version.  Also, the pristine-property list of
     this file. */
  const char *path_start_revision;
  apr_file_t *file_start_revision;
  apr_hash_t *pristine_props;

  /* The path and APR file handle to the temporary file that contains the
     second repository version.  These fields are set when processing
     textdelta and file deletion, and will be NULL if there's no
     textual difference between the two revisions. */
  const char *path_end_revision;
  apr_file_t *file_end_revision;

  /* APPLY_HANDLER/APPLY_BATON represent the delta application baton. */
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;

  /* The overall crawler editor baton. */
  struct edit_baton *edit_baton;

  /* A cache of any property changes (svn_prop_t) received for this file. */
  apr_array_header_t *propchanges;

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
               apr_pool_t *pool)
{
  struct dir_baton *dir_baton = apr_pcalloc(pool, sizeof(*dir_baton));

  dir_baton->dir_baton = parent_baton;
  dir_baton->edit_baton = edit_baton;
  dir_baton->added = added;
  dir_baton->tree_conflicted = FALSE;
  dir_baton->skip = FALSE;
  dir_baton->pool = pool;
  dir_baton->path = apr_pstrdup(pool, path);
  dir_baton->wcpath = svn_path_join(edit_baton->target, path, pool);
  dir_baton->propchanges  = apr_array_make(pool, 1, sizeof(svn_prop_t));

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
                void *edit_baton,
                apr_pool_t *pool)
{
  struct file_baton *file_baton = apr_pcalloc(pool, sizeof(*file_baton));
  struct edit_baton *eb = edit_baton;

  file_baton->edit_baton = edit_baton;
  file_baton->added = added;
  file_baton->tree_conflicted = FALSE;
  file_baton->skip = FALSE;
  file_baton->pool = pool;
  file_baton->path = apr_pstrdup(pool, path);
  file_baton->wcpath = svn_path_join(eb->target, path, pool);
  file_baton->propchanges  = apr_array_make(pool, 1, sizeof(svn_prop_t));

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
                    struct file_baton *b)
{
  /* Defaults */
  *mimetype1 = NULL;
  *mimetype2 = NULL;

  if (b->pristine_props)
    {
      svn_string_t *pristine_val;
      pristine_val = apr_hash_get(b->pristine_props, SVN_PROP_MIME_TYPE,
                                  strlen(SVN_PROP_MIME_TYPE));
      if (pristine_val)
        *mimetype1 = pristine_val->data;
    }

  if (b->propchanges)
    {
      int i;
      svn_prop_t *propchange;

      for (i = 0; i < b->propchanges->nelts; i++)
        {
          propchange = &APR_ARRAY_IDX(b->propchanges, i, svn_prop_t);
          if (strcmp(propchange->name, SVN_PROP_MIME_TYPE) == 0)
            {
              if (propchange->value)
                *mimetype2 = propchange->value->data;
              break;
            }
        }
    }
}


/* Get the repository version of a file. This makes an RA request to
 * retrieve the file contents. A pool cleanup handler is installed to
 * delete this file.
 */
static svn_error_t *
get_file_from_ra(struct file_baton *b, svn_revnum_t revision)
{
  svn_stream_t *fstream;

  SVN_ERR(svn_stream_open_unique(&fstream, &(b->path_start_revision), NULL,
                                 svn_io_file_del_on_pool_cleanup, b->pool,
                                 b->pool));

  SVN_ERR(svn_ra_get_file(b->edit_baton->ra_session,
                          b->path,
                          revision,
                          fstream, NULL,
                          &(b->pristine_props),
                          b->pool));
  return svn_stream_close(fstream);
}

/* Get the props attached to a directory in the repository at BASE_REVISION. */
static svn_error_t *
get_dirprops_from_ra(struct dir_baton *b, svn_revnum_t base_revision)
{
  return svn_ra_get_dir2(b->edit_baton->ra_session,
                         NULL, NULL, &(b->pristine_props),
                         b->path,
                         base_revision,
                         0,
                         b->pool);
}


/* Create an empty file, the path to the file is returned in
   EMPTY_FILE_PATH.  If ADM_ACCESS is not NULL and a lock is held,
   create the file in the adm tmp/ area, otherwise use a system temp
   directory.

   If FILE is non-NULL, an open file is returned in *FILE. */
static svn_error_t *
create_empty_file(apr_file_t **file,
                  const char **empty_file_path,
                  svn_wc_adm_access_t *adm_access,
                  svn_io_file_del_t delete_when,
                  apr_pool_t *pool)
{
  if (adm_access && svn_wc_adm_locked(adm_access))
    return svn_wc_create_tmp_file2(file, empty_file_path,
                                   svn_wc_adm_access_path(adm_access),
                                   delete_when, pool);

  return svn_io_open_unique_file3(file, empty_file_path, NULL,
                                  delete_when, pool, pool);
}

/* Return in *PATH_ACCESS the access baton for the directory PATH by
   searching the access baton set of ADM_ACCESS.  If ADM_ACCESS is NULL
   then *PATH_ACCESS will be NULL.  If LENIENT is TRUE then failure to find
   an access baton will not return an error but will set *PATH_ACCESS to
   NULL instead. */
static svn_error_t *
get_path_access(svn_wc_adm_access_t **path_access,
                svn_wc_adm_access_t *adm_access,
                const char *path,
                svn_boolean_t lenient,
                apr_pool_t *pool)
{
  if (! adm_access)
    *path_access = NULL;
  else
    {
      svn_error_t *err = svn_wc_adm_retrieve(path_access, adm_access, path,
                                             pool);
      if (err)
        {
          if (! lenient)
            return err;
          svn_error_clear(err);
          *path_access = NULL;
        }
    }

  return SVN_NO_ERROR;
}

/* Like get_path_access except the returned access baton, in
   *PARENT_ACCESS, is for the parent of PATH rather than for PATH
   itself. */
static svn_error_t *
get_parent_access(svn_wc_adm_access_t **parent_access,
                  svn_wc_adm_access_t *adm_access,
                  const char *path,
                  svn_boolean_t lenient,
                  apr_pool_t *pool)
{
  if (! adm_access)
    *parent_access = NULL;  /* Avoid messing around with paths */
  else
    {
      const char *parent_path = svn_path_dirname(path, pool);
      SVN_ERR(get_path_access(parent_access, adm_access, parent_path,
                              lenient, pool));
    }
  return SVN_NO_ERROR;
}

/* Get the empty file associated with the edit baton. This is cached so
 * that it can be reused, all empty files are the same.
 */
static svn_error_t *
get_empty_file(struct edit_baton *eb,
               const char **empty_file_path)
{
  /* Create the file if it does not exist */
  /* Note that we tried to use /dev/null in r17220, but
     that won't work on Windows: it's impossible to stat NUL */
  if (!eb->empty_file)
    SVN_ERR(create_empty_file(NULL, &(eb->empty_file), eb->adm_access,
                              svn_io_file_del_on_pool_cleanup, eb->pool));


  *empty_file_path = eb->empty_file;

  return SVN_NO_ERROR;
}

/* An editor function. The root of the comparison hierarchy */
static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  eb->target_revision = target_revision;
  return SVN_NO_ERROR;
}

/* An editor function. The root of the comparison hierarchy */
static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *b = make_dir_baton("", NULL, eb, FALSE, pool);

  /* Override the wcpath in our baton. */
  b->wcpath = apr_pstrdup(pool, eb->target);

  SVN_ERR(get_dirprops_from_ra(b, base_revision));

  *root_baton = b;
  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t base_revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  svn_node_kind_t kind;
  svn_wc_adm_access_t *adm_access;
  svn_wc_notify_state_t state = svn_wc_notify_state_inapplicable;
  svn_wc_notify_action_t action = svn_wc_notify_skip;
  svn_boolean_t tree_conflicted = FALSE;

  /* Skip *everything* within a newly tree-conflicted directory. */
  if (pb->skip || pb->tree_conflicted)
    return SVN_NO_ERROR;

  /* We need to know if this is a directory or a file */
  SVN_ERR(svn_ra_check_path(eb->ra_session, path, eb->revision, &kind, pool));
  SVN_ERR(get_path_access(&adm_access, eb->adm_access, pb->wcpath,
                          TRUE, pool));
  if ((! eb->adm_access) || adm_access)
    {
      switch (kind)
        {
        case svn_node_file:
          {
            const char *mimetype1, *mimetype2;
            struct file_baton *b;

            /* Compare a file being deleted against an empty file */
            b = make_file_baton(path, FALSE, eb, pool);
            SVN_ERR(get_file_from_ra(b, eb->revision));
            SVN_ERR(get_empty_file(b->edit_baton, &(b->path_end_revision)));

            get_file_mime_types(&mimetype1, &mimetype2, b);

            SVN_ERR(eb->diff_callbacks->file_deleted
                    (adm_access, &state, &tree_conflicted, b->wcpath,
                     b->path_start_revision,
                     b->path_end_revision,
                     mimetype1, mimetype2,
                     b->pristine_props,
                     b->edit_baton->diff_cmd_baton));

            break;
          }
        case svn_node_dir:
          {
            SVN_ERR(eb->diff_callbacks->dir_deleted
                    (adm_access, &state, &tree_conflicted,
                     svn_path_join(eb->target, path, pool),
                     eb->diff_cmd_baton));
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
          if (eb->dry_run)
            {
              /* Remember what we _would've_ deleted (issue #2584). */
              const char *wcpath = svn_path_join(eb->target, path, pb->pool);
              apr_hash_set(svn_client__dry_run_deletions(eb->diff_cmd_baton),
                           wcpath, APR_HASH_KEY_STRING, wcpath);

              /* ### TODO: if (kind == svn_node_dir), record all
                 ### children as deleted to avoid collisions from
                 ### subsequent edits. */
            }
        }
    }

  if (eb->notify_func)
    {
      const char* deleted_path;
      deleted_path_notify_t *dpn = apr_palloc(eb->pool, sizeof(*dpn));
      deleted_path = svn_path_join(eb->target, path, eb->pool);
      dpn->kind = kind;
      dpn->action = tree_conflicted ? svn_wc_notify_tree_conflict : action;
      dpn->state = state;
      dpn->tree_conflicted = tree_conflicted;
      apr_hash_set(eb->deleted_paths, deleted_path, APR_HASH_KEY_STRING, dpn);
    }
  return SVN_NO_ERROR;
}

/* An editor function.  */
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
  struct dir_baton *b;
  svn_wc_adm_access_t *adm_access;
  svn_wc_notify_state_t state;

  /* ### TODO: support copyfrom? */

  b = make_dir_baton(path, pb, eb, TRUE, pool);
  b->pristine_props = eb->empty_hash;
  *child_baton = b;

  /* Skip *everything* within a newly tree-conflicted directory. */
  if (pb->skip || pb->tree_conflicted)
    {
      b->skip = TRUE;
      return SVN_NO_ERROR;
    }


  SVN_ERR(get_path_access(&adm_access, eb->adm_access, pb->wcpath, TRUE,
                          pool));

  SVN_ERR(eb->diff_callbacks->dir_added
          (adm_access, &state, &b->tree_conflicted, b->wcpath,
           eb->target_revision, eb->diff_cmd_baton));

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
      dpn = apr_hash_get(eb->deleted_paths, b->wcpath, APR_HASH_KEY_STRING);
      if (dpn)
        {
          /* If any was found, we will handle the pending 'deleted path
           * notification' (DPN) here. Remove it from the list. */
          apr_hash_set(eb->deleted_paths, b->wcpath,
                       APR_HASH_KEY_STRING, NULL);

          /* the pending delete might be on a different node kind. */
          kind = dpn->kind;
          state = dpn->state;
        }

      /* Determine what the notification (ACTION) should be.
       * In case of a pending 'delete', this might become a 'replace'. */
      if (b->tree_conflicted)
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

      notify = svn_wc_create_notify(b->wcpath, action, pool);
      notify->kind = kind;
      notify->content_state = notify->prop_state = state;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *b;
  svn_wc_adm_access_t *adm_access;

  b = make_dir_baton(path, pb, pb->edit_baton, FALSE, pool);
  *child_baton = b;

  /* Skip *everything* within a newly tree-conflicted directory. */
  if (pb->skip || pb->tree_conflicted)
    {
      b->skip = TRUE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(get_dirprops_from_ra(b, base_revision));

  SVN_ERR(get_path_access(&adm_access, eb->adm_access, pb->wcpath, TRUE,
                          pool));

  return eb->diff_callbacks->dir_opened
         (adm_access, &b->tree_conflicted, b->wcpath, base_revision,
          b->edit_baton->diff_cmd_baton);
}


/* An editor function.  */
static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *pool,
         void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *b;

  /* ### TODO: support copyfrom? */

  b = make_file_baton(path, TRUE, pb->edit_baton, pool);
  *file_baton = b;

  /* Skip *everything* within a newly tree-conflicted directory. */
  if (pb->skip || pb->tree_conflicted)
    {
      b->skip = TRUE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(get_empty_file(b->edit_baton, &(b->path_start_revision)));
  b->pristine_props = pb->edit_baton->empty_hash;

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *b;

  b = make_file_baton(path, FALSE, pb->edit_baton, pool);
  *file_baton = b;

  /* Skip *everything* within a newly tree-conflicted directory. */
  if (pb->skip || pb->tree_conflicted)
    {
      b->skip = TRUE;
      return SVN_NO_ERROR;
    }

  return get_file_from_ra(b, base_revision);
}

/* Do the work of applying the text delta.  */
static svn_error_t *
window_handler(svn_txdelta_window_t *window,
               void *window_baton)
{
  struct file_baton *b = window_baton;

  /* Skip *everything* within a newly tree-conflicted directory. */
  if (b->skip)
    return SVN_NO_ERROR;

  SVN_ERR(b->apply_handler(window, b->apply_baton));

  if (!window)
    {
      SVN_ERR(svn_io_file_close(b->file_start_revision, b->pool));
      SVN_ERR(svn_io_file_close(b->file_end_revision, b->pool));
    }

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct file_baton *b = file_baton;
  svn_wc_adm_access_t *adm_access;

  /* Skip *everything* within a newly tree-conflicted directory. */
  if (b->skip)
    {
      *handler = window_handler;
      *handler_baton = file_baton;
      return SVN_NO_ERROR;
    }

  /* Open the file to be used as the base for second revision */
  SVN_ERR(svn_io_file_open(&(b->file_start_revision),
                           b->path_start_revision,
                           APR_READ, APR_OS_DEFAULT, b->pool));

  /* Open the file that will become the second revision after applying the
     text delta, it starts empty */
  if (b->edit_baton->adm_access)
    {
      svn_error_t *err;

      err = svn_wc_adm_probe_retrieve(&adm_access, b->edit_baton->adm_access,
                                      b->wcpath, pool);
      if (err)
        {
          svn_error_clear(err);
          adm_access = NULL;
        }
    }
  else
    adm_access = NULL;
  SVN_ERR(create_empty_file(&(b->file_end_revision),
                            &(b->path_end_revision), adm_access,
                            svn_io_file_del_on_pool_cleanup, b->pool));

  svn_txdelta_apply(svn_stream_from_aprfile2(b->file_start_revision, TRUE,
                                             b->pool),
                    svn_stream_from_aprfile2(b->file_end_revision, TRUE,
                                             b->pool),
                    NULL, b->path, b->pool,
                    &(b->apply_handler), &(b->apply_baton));

  *handler = window_handler;
  *handler_baton = file_baton;

  return SVN_NO_ERROR;
}

/* An editor function.  When the file is closed we have a temporary
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
           const char *text_checksum,
           apr_pool_t *pool)
{
  struct file_baton *b = file_baton;
  struct edit_baton *eb = b->edit_baton;
  svn_wc_adm_access_t *adm_access;
  svn_error_t *err;
  svn_wc_notify_state_t content_state = svn_wc_notify_state_unknown;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unknown;

  /* Skip *everything* within a newly tree-conflicted directory. */
  if (b->skip)
    return SVN_NO_ERROR;

  err = get_parent_access(&adm_access, eb->adm_access,
                          b->wcpath, eb->dry_run, b->pool);

  if (err && err->apr_err == SVN_ERR_WC_NOT_LOCKED)
    {
      /* ### maybe try to stat the local b->wcpath? */
      /* If the file path doesn't exist, then send a 'skipped' notification. */
      /* Currently, there is no way how this could be a tree-conflict
       * notification, because tree conflicts are only detected below.
       * Neither open_file, add_file or apply_textdelta tack tree-conflicts
       * on the file baton. delete_file is only carried out on dir close.
       * This is a skip due to lock failure. */
      if (eb->notify_func)
        {
          svn_wc_notify_t *notify = svn_wc_create_notify(
                                      b->wcpath,
                                      svn_wc_notify_skip,
                                      pool);
          notify->kind = svn_node_file;
          notify->content_state = svn_wc_notify_state_missing;
          notify->prop_state = prop_state;
          (*eb->notify_func)(eb->notify_baton, notify, pool);
        }

      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else if (err)
    return err;

  if (b->path_end_revision || b->propchanges->nelts > 0)
    {
      const char *mimetype1, *mimetype2;
      get_file_mime_types(&mimetype1, &mimetype2, b);

      if (b->added)
        SVN_ERR(eb->diff_callbacks->file_added
                (adm_access, &content_state, &prop_state, &b->tree_conflicted,
                 b->wcpath,
                 b->path_end_revision ? b->path_start_revision : NULL,
                 b->path_end_revision,
                 0,
                 b->edit_baton->target_revision,
                 mimetype1, mimetype2,
                 b->propchanges, b->pristine_props,
                 b->edit_baton->diff_cmd_baton));
      else
        SVN_ERR(eb->diff_callbacks->file_changed
                (adm_access, &content_state, &prop_state, &b->tree_conflicted,
                 b->wcpath,
                 b->path_end_revision ? b->path_start_revision : NULL,
                 b->path_end_revision,
                 b->edit_baton->revision,
                 b->edit_baton->target_revision,
                 mimetype1, mimetype2,
                 b->propchanges, b->pristine_props,
                 b->edit_baton->diff_cmd_baton));
    }


  if (eb->notify_func)
    {
      deleted_path_notify_t *dpn;
      svn_wc_notify_t *notify;
      svn_wc_notify_action_t action;
      svn_node_kind_t kind = svn_node_file;
      
      /* Find out if a pending delete notification for this path is
       * still around. */
      dpn = apr_hash_get(eb->deleted_paths, b->wcpath, APR_HASH_KEY_STRING);
      if (dpn)
        {
          /* If any was found, we will handle the pending 'deleted path
           * notification' (DPN) here. Remove it from the list. */
          apr_hash_set(eb->deleted_paths, b->wcpath,
                       APR_HASH_KEY_STRING, NULL);

          /* the pending delete might be on a different node kind. */
          kind = dpn->kind;
          content_state = prop_state = dpn->state;
        }

      /* Determine what the notification (ACTION) should be.
       * In case of a pending 'delete', this might become a 'replace'. */
      if (b->tree_conflicted)
        action = svn_wc_notify_tree_conflict;
      else if (dpn)
        {
          if (dpn->action == svn_wc_notify_update_delete
              && b->added)
            action = svn_wc_notify_update_replace;
          else
            /* Note: dpn->action might be svn_wc_notify_tree_conflict */
            action = dpn->action;
        }
      else if ((content_state == svn_wc_notify_state_missing)
                || (content_state == svn_wc_notify_state_obstructed))
        action = svn_wc_notify_skip;
      else if (b->added)
        action = svn_wc_notify_update_add;
      else
        action = svn_wc_notify_update_update;

      notify = svn_wc_create_notify(b->wcpath, action, pool);
      notify->kind = kind;
      notify->content_state = content_state;
      notify->prop_state = prop_state;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton *b = dir_baton;
  struct edit_baton *eb = b->edit_baton;
  svn_wc_notify_state_t content_state = svn_wc_notify_state_unknown;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unknown;
  svn_error_t *err;
  svn_wc_adm_access_t *adm_access;

  /* Skip *everything* within a newly tree-conflicted directory. */
  if (b->skip)
    return SVN_NO_ERROR;

  if (eb->dry_run)
    svn_hash__clear(svn_client__dry_run_deletions(eb->diff_cmd_baton));

  err = get_path_access(&adm_access, eb->adm_access, b->wcpath,
                        eb->dry_run, b->pool);

  if (err && err->apr_err == SVN_ERR_WC_NOT_LOCKED)
    {
      /* ### maybe try to stat the local b->wcpath? */
      /* If the path doesn't exist, then send a 'skipped' notification. 
         Don't notify added directories as they triggered notification
         in add_directory. */
      if (! b->added && eb->notify_func)
        {
          svn_wc_notify_t *notify
            = svn_wc_create_notify(b->wcpath,
                                   b->tree_conflicted
                                     ? svn_wc_notify_tree_conflict
                                     : svn_wc_notify_skip,
                                   pool);
          notify->kind = svn_node_dir;
          notify->content_state = notify->prop_state
            = svn_wc_notify_state_missing;
          (*eb->notify_func)(eb->notify_baton, notify, pool);
        }
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else if (err)
    return err;

  /* Don't do the props_changed stuff if this is a dry_run and we don't
     have an access baton, since in that case the directory will already
     have been recognised as added, in which case they cannot conflict. */
  if ((b->propchanges->nelts > 0) && (! eb->dry_run || adm_access))
    {
      svn_boolean_t tree_conflicted;
      SVN_ERR(eb->diff_callbacks->dir_props_changed
              (adm_access, &prop_state, &tree_conflicted,
               b->wcpath,
               b->propchanges, b->pristine_props,
               b->edit_baton->diff_cmd_baton));
      if (tree_conflicted)
        b->tree_conflicted = TRUE;
    }

  SVN_ERR(eb->diff_callbacks->dir_closed
          (adm_access, NULL, NULL, NULL,
           b->wcpath, b->edit_baton->diff_cmd_baton));

  /* Don't notify added directories as they triggered notification
     in add_directory. */
  if (!b->added && eb->notify_func)
    {
      svn_wc_notify_t *notify;
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(NULL, eb->deleted_paths); hi;
           hi = apr_hash_next(hi))
        {
          const void *deleted_path;
          deleted_path_notify_t *dpn;
          apr_hash_this(hi, &deleted_path, NULL, (void *)&dpn);
          notify = svn_wc_create_notify(deleted_path, dpn->action, pool);
          notify->kind = dpn->kind;
          notify->content_state = notify->prop_state = dpn->state;
          notify->lock_state = svn_wc_notify_lock_state_inapplicable;
          (*eb->notify_func)(eb->notify_baton, notify, pool);
          apr_hash_set(eb->deleted_paths, deleted_path,
                       APR_HASH_KEY_STRING, NULL);
        }

      notify = svn_wc_create_notify(b->wcpath,
                                    b->tree_conflicted
                                      ? svn_wc_notify_tree_conflict
                                      : svn_wc_notify_update_update,
                                    pool);
      notify->kind = svn_node_dir;

      /* In case of a tree conflict during merge, the diff callback
       * sets content_state appropriately. So copy the state into the
       * notify_t to make sure conflicts get displayed. */
      notify->content_state = content_state;

      notify->prop_state = prop_state;
      notify->lock_state = svn_wc_notify_lock_state_inapplicable;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}


/* An editor function.  */
static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton *b = file_baton;
  svn_prop_t *propchange;

  /* Skip *everything* within a newly tree-conflicted directory. */
  if (b->skip)
    return SVN_NO_ERROR;

  propchange = apr_array_push(b->propchanges);
  propchange->name = apr_pstrdup(b->pool, name);
  propchange->value = value ? svn_string_dup(value, b->pool) : NULL;

  return SVN_NO_ERROR;
}

/* An editor function.  */
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

  propchange = apr_array_push(db->propchanges);
  propchange->name = apr_pstrdup(db->pool, name);
  propchange->value = value ? svn_string_dup(value, db->pool) : NULL;

  return SVN_NO_ERROR;
}


/* An editor function.  */
static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  svn_pool_destroy(eb->pool);

  return SVN_NO_ERROR;
}

/* An editor function.  */
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
        = svn_wc_create_notify(svn_path_join(pb->wcpath,
                                             svn_path_basename(path, pool),
                                             pool),
                               svn_wc_notify_skip, pool);
      notify->kind = svn_node_dir;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_missing;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}


/* An editor function.  */
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
        = svn_wc_create_notify(svn_path_join(pb->wcpath,
                                             svn_path_basename(path, pool),
                                             pool),
                               svn_wc_notify_skip, pool);
      notify->kind = svn_node_file;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_missing;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}

/* Create a repository diff editor and baton.  */
svn_error_t *
svn_client__get_diff_editor(const char *target,
                            svn_wc_adm_access_t *adm_access,
                            const svn_wc_diff_callbacks3_t *diff_callbacks,
                            void *diff_cmd_baton,
                            svn_depth_t depth,
                            svn_boolean_t dry_run,
                            svn_ra_session_t *ra_session,
                            svn_revnum_t revision,
                            svn_wc_notify_func2_t notify_func,
                            void *notify_baton,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            const svn_delta_editor_t **editor,
                            void **edit_baton,
                            apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(subpool);
  struct edit_baton *eb = apr_palloc(subpool, sizeof(*eb));

  eb->target = target;
  eb->adm_access = adm_access;
  eb->diff_callbacks = diff_callbacks;
  eb->diff_cmd_baton = diff_cmd_baton;
  eb->dry_run = dry_run;
  eb->ra_session = ra_session;
  eb->revision = revision;
  eb->empty_file = NULL;
  eb->empty_hash = apr_hash_make(subpool);
  eb->deleted_paths = apr_hash_make(subpool);
  eb->pool = subpool;
  eb->notify_func = notify_func;
  eb->notify_baton = notify_baton;

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

  return svn_delta_get_cancellation_editor(cancel_func,
                                           cancel_baton,
                                           tree_editor,
                                           eb,
                                           editor,
                                           edit_baton,
                                           pool);

  /* We don't destroy subpool, as it's managed by the edit baton. */
}
