/*
 * repos_diff.c -- The diff editor for comparing two repository versions
 *
 * ====================================================================
 * Copyright (c) 2002 CollabNet.  All rights reserved.
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

/* This code uses an svn_delta_edit_fns_t editor driven by a tree delta
 * between two repository revisions (REV1 and REV2). For each file
 * encountered in the delta the editor constructs two temporary files, one
 * for each revision. This necessitates a separate request for the REV1
 * version of the file when the delta shows the file being modified or
 * deleted. Files that are added by the delta do not require a separate
 * request, the REV1 version is empty and the delta is sufficient to
 * construct the REV2 version. When both versions of each file have been
 * created the diff callback is invoked to display the difference between
 * the two files.
 */

#include "svn_client.h"
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_path.h"

#include "client.h"

/* Overall crawler editor baton.
 */
struct edit_baton {
  /* TARGET is a working-copy directory which corresponds to the base
     URL open in RA_SESSION below. */
  svn_stringbuf_t *target;

  /* The callback and calback argument that implement the file comparison
     function */
  const svn_diff_callbacks_t *diff_callbacks;
  void *diff_cmd_baton;

  /* Flags whether to diff recursively or not. If set the diff is
     recursive. */
  svn_boolean_t recurse;

  /* RA_LIB is the vtable for making requests to the RA layer, RA_SESSION
     is the open session for these requests */
  svn_ra_plugin_t *ra_lib;
  void *ra_session;

  /* The rev1 from the '-r Rev1:Rev2' command line option */
  svn_revnum_t revision;

  /* The rev2 from the '-r Rev1:Rev2' option, specifically set by
     set_target_revision(). */
  svn_revnum_t target_revision;

  /* A temporary empty file. Used for add/delete differences. This is
     cached here so that it can be reused, all empty files are the same. */
  svn_stringbuf_t *empty_file;

  apr_pool_t *pool;
};

/* Directory level baton.
 */
struct dir_baton {
  /* Gets set if the directory is added rather than replaced/unchanged. */
  svn_boolean_t added;

  /* The path of the directory within the repository */
  const char *path;

  /* The baton for the parent directory, or null if this is the root of the
     hierarchy to be compared. */
  struct dir_baton *dir_baton;

  /* The overall crawler editor baton. */
  struct edit_baton *edit_baton;

  /* A cache of any property changes (svn_prop_t) received for this dir. */
  apr_array_header_t *propchanges;

  /* The pool passed in by add_dir, open_dir, or open_root.
     Also, the pool this dir baton is allocated in. */
  apr_pool_t *pool;
};

/* File level baton.
 */
struct file_baton {
  /* Gets set if the file is added rather than replaced. */
  svn_boolean_t added;

  /* The path of the file within the repository */
  const char *path;

  /* The path and APR file handle to the temporary file that contains the
     first repository version.  Also, the pristine-property list of
     this file. */
  svn_stringbuf_t *path_start_revision;
  apr_file_t *file_start_revision;
  apr_hash_t *pristine_props;

  /* The path and APR file handle to the temporary file that contains the
     second repository version.  These fields are set when processing
     textdelta and file deletion, and will be NULL if there's no
     textual difference between the two revisions. */
  svn_stringbuf_t *path_end_revision;
  apr_file_t *file_end_revision;

  /* APPLY_HANDLER/APPLY_BATON represent the delta applcation baton. */
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

/* Data used by the apr pool temp file cleanup handler */
struct temp_file_cleanup_s {
  /* The path to the file to be deleted */
  svn_stringbuf_t *path;
  /* The pool to which the deletion of the file is linked. */
  apr_pool_t *pool;
};

/* Create a new directory baton for PATH in POOL.  ADDED is set if
 * this directory is being added rather than replaced. PARENT_BATON is
 * the baton of the parent directory, it will be null if this is the
 * root of the comparison hierarchy. The directory and its parent may
 * or may not exist in the working copy. EDIT_BATON is the overall
 * crawler editor baton.
 */
static struct dir_baton *
make_dir_baton (const char *path,
                struct dir_baton *parent_baton,
                svn_boolean_t added,
                apr_pool_t *pool)
{
  struct dir_baton *dir_baton = apr_pcalloc (pool, sizeof (*dir_baton));

  dir_baton->dir_baton = parent_baton;
  dir_baton->edit_baton = parent_baton->edit_baton;
  dir_baton->added = added;
  dir_baton->pool = pool;
  dir_baton->path = apr_pstrdup (pool, path);
  dir_baton->propchanges  = apr_array_make (pool, 1, sizeof (svn_prop_t));

  return dir_baton;
}

/* Create a new file baton for PATH in POOL, which is a child of
 * directory PARENT_PATH. ADDED is set if this file is being added
 * rather than replaced.  EDIT_BATON is a pointer to the global edit
 * baton.
 */
static struct file_baton *
make_file_baton (const char *path,
                 svn_boolean_t added,
                 void *edit_baton,
                 apr_pool_t *pool)
{
  struct file_baton *file_baton = apr_pcalloc (pool, sizeof (*file_baton));

  file_baton->edit_baton = edit_baton;
  file_baton->added = added;
  file_baton->pool = pool;
  file_baton->path = apr_pstrdup (pool, path);
  file_baton->propchanges  = apr_array_make (pool, 1, sizeof (svn_prop_t));

  return file_baton;
}

/* An apr pool cleanup handler, this deletes one of the temporary files.
 */
static apr_status_t
temp_file_plain_cleanup_handler (void *arg)
{
  struct temp_file_cleanup_s *s = arg;

  return apr_file_remove (s->path->data, s->pool);
}

/* An apr pool cleanup handler, this removes a cleanup handler.
 */
static apr_status_t
temp_file_child_cleanup_handler (void *arg)
{
  struct temp_file_cleanup_s *s = arg;

  apr_pool_cleanup_kill (s->pool, s, temp_file_plain_cleanup_handler);

  return APR_SUCCESS;
}

/* Register a pool cleanup to delete PATH when POOL is destroyed.
 *
 * The main "gotcha" is that if the process forks a child by calling
 * apr_proc_create, then the child's copy of the cleanup handler will run
 * and delete the file while the parent still expects it to be around. To
 * avoid this a child cleanup handler is also installed to kill the plain
 * cleanup handler in the child.
 *
 * ### TODO: This a candidate to be a general utility function.
 */
static svn_error_t *
temp_file_cleanup_register (svn_stringbuf_t *path,
                            apr_pool_t *pool)
{
  struct temp_file_cleanup_s *s = apr_palloc (pool, sizeof (*s));
  s->path = path;
  s->pool = pool;
  apr_pool_cleanup_register (s->pool, s, temp_file_plain_cleanup_handler,
                             temp_file_child_cleanup_handler);
  return SVN_NO_ERROR;
}


/* Get the repository version of a file. This makes an RA request to
 * retrieve the file contents. A pool cleanup handler is installed to
 * delete this file.
 *
 * ### TODO: The editor calls this function to get REV1 of the file. Can we
 * get the file props as well?  Then get_wc_prop() could return them later
 * on enabling the REV1:REV2 request to send diffs.
 */
static svn_error_t *
get_file_from_ra (struct file_baton *b)
{
  apr_status_t status;
  apr_file_t *file;
  svn_stream_t *fstream;

  /* ### TODO: Need some apr temp file support */
  SVN_ERR (svn_io_open_unique_file (&file, &b->path_start_revision,
                                    "tmp", "", FALSE, b->pool));

  /* Install a pool cleanup handler to delete the file */
  SVN_ERR (temp_file_cleanup_register (b->path_start_revision, b->pool));

  fstream = svn_stream_from_aprfile (file, b->pool);
  SVN_ERR (b->edit_baton->ra_lib->get_file (b->edit_baton->ra_session,
                                            b->path,
                                            b->edit_baton->revision,
                                            fstream, NULL,
                                            &(b->pristine_props)));

  status = apr_file_close (file);
  if (status)
    return svn_error_createf (status, 0, NULL, b->pool,
                              "failed to close file '%s'",
                              b->path_start_revision->data);

  return SVN_NO_ERROR;
}

/* Create an empty file, the path to the file is returned in EMPTY_FILE
 */
static svn_error_t *
create_empty_file (svn_stringbuf_t **empty_file,
                   apr_pool_t *pool)
{
  apr_status_t status;
  apr_file_t *file;

  /* ### TODO: Need some apr temp file support */
  SVN_ERR (svn_io_open_unique_file (&file, empty_file, "tmp", "", FALSE,
                                    pool));

  status = apr_file_close (file);
  if (status)
    return svn_error_createf (status, 0, NULL, pool,
                              "failed to create empty file '%s'",
                              (*empty_file)->data);
  return SVN_NO_ERROR;
}

/* Get the empty file associated with the edit baton. This is cached so
 * that it can be reused, all empty files are the same.
 */
static svn_error_t *
get_empty_file (struct edit_baton *b,
                svn_stringbuf_t **empty_file)
{
  /* Create the file if it does not exist */
  if (!b->empty_file)
    {
      SVN_ERR (create_empty_file (&b->empty_file, b->pool));

      /* Install a pool cleanup handler to delete the file */
      SVN_ERR (temp_file_cleanup_register (b->empty_file, b->pool));
    }

  *empty_file = b->empty_file;

  return SVN_NO_ERROR;
}

/* An svn_delta_edit_fns_t editor function. The root of the comparison
 * hierarchy
 */
static svn_error_t *
set_target_revision (void *edit_baton, svn_revnum_t target_revision)
{
  struct edit_baton *eb = edit_baton;
  
  eb->target_revision = target_revision;
  return SVN_NO_ERROR;
}

/* An svn_delta_edit_fns_t editor function. The root of the comparison
 * hierarchy
 */
static svn_error_t *
open_root (void *edit_baton,
           svn_revnum_t base_revision,
           apr_pool_t *pool,
           void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *dir_baton = apr_pcalloc (pool, sizeof (*dir_baton));

  dir_baton->dir_baton = NULL;
  dir_baton->edit_baton = eb;
  dir_baton->added = FALSE;
  dir_baton->pool = pool;
  dir_baton->path = eb->target ? apr_pstrdup (pool, eb->target->data) : "";
  dir_baton->propchanges  = apr_array_make (pool, 1, sizeof (svn_prop_t));

  *root_baton = dir_baton;

  return SVN_NO_ERROR;
}

/* An svn_delta_edit_fns_t editor function.
 */
static svn_error_t *
delete_entry (const char *path,
              svn_revnum_t base_revision,
              void *parent_baton,
              apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  svn_node_kind_t kind;

  /* We need to know if this is a directory or a file */
  /* ### over ra_dav, this breaks if PATH doesn't exist in HEAD; see
       issue #581.  Obviously, this kind of misses the point of
       passing in a revision.  :-)  */
  SVN_ERR (pb->edit_baton->ra_lib->check_path (&kind,
                                               pb->edit_baton->ra_session,
                                               path,
                                               pb->edit_baton->revision));

  switch (kind)
    {
    case svn_node_file:
      {
        /* Compare a file being deleted against an empty file */
        struct file_baton *b = make_file_baton (path,
                                                FALSE,
                                                pb->edit_baton,
                                                pool);
        SVN_ERR (get_file_from_ra (b));
        SVN_ERR (get_empty_file(b->edit_baton, &b->path_end_revision));
        
        SVN_ERR (pb->edit_baton->diff_callbacks->file_deleted 
                 (b->path,
                  b->path_start_revision->data,
                  b->path_end_revision->data,
                  b->edit_baton->diff_cmd_baton));
        break;
      }
    case svn_node_dir:
      {
        SVN_ERR (pb->edit_baton->diff_callbacks->dir_deleted 
                 (pb->path,
                  pb->edit_baton->diff_cmd_baton));
        break;
      }
    default:
      break;
    }

  return SVN_NO_ERROR;
}

/* An svn_delta_edit_fns_t editor function.
 */
static svn_error_t *
add_directory (const char *path,
               void *parent_baton,
               const char *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct dir_baton *b;

  /* ### TODO: support copyfrom? */

  b = make_dir_baton (path, pb, TRUE, pool);
  *child_baton = b;

  SVN_ERR (pb->edit_baton->diff_callbacks->dir_added 
           (path,
            pb->edit_baton->diff_cmd_baton));

  return SVN_NO_ERROR;
}

/* An svn_delta_edit_fns_t editor function.
 */
static svn_error_t *
open_directory (const char *path,
                void *parent_baton,
                svn_revnum_t base_revision,
                apr_pool_t *pool,
                void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct dir_baton *b;

  b = make_dir_baton (path, pb, FALSE, pool);
  *child_baton = b;

  return SVN_NO_ERROR;
}


/* An svn_delta_edit_fns_t editor function.
 */
static svn_error_t *
add_file (const char *path,
          void *parent_baton,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *b;

  /* ### TODO: support copyfrom? */

  b = make_file_baton (path, TRUE, pb->edit_baton, pool);
  *file_baton = b;

  SVN_ERR (get_empty_file (b->edit_baton, &b->path_start_revision));

  return SVN_NO_ERROR;
}

/* An svn_delta_edit_fns_t editor function.
 */
static svn_error_t *
open_file (const char *path,
           void *parent_baton,
           svn_revnum_t base_revision,
           apr_pool_t *pool,
           void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *b;

  b = make_file_baton (path, FALSE, pb->edit_baton, pool);
  *file_baton = b;

  SVN_ERR (get_file_from_ra (b));

  return SVN_NO_ERROR;
}

/* An svn_delta_edit_fns_t editor function.  Do the work of applying the
 * text delta.
 */
static svn_error_t *
window_handler (svn_txdelta_window_t *window,
                void *window_baton)
{
  struct file_baton *b = window_baton;

  SVN_ERR (b->apply_handler (window, b->apply_baton));

  if (!window)
    {
      apr_status_t status;

      status = apr_file_close (b->file_start_revision);
      if (status)
        return svn_error_createf (status, 0, NULL, b->pool,
                                  "failed to close file '%s'",
                                  b->path_start_revision->data);

      status = apr_file_close (b->file_end_revision);
      if (status)
        return svn_error_createf (status, 0, NULL, b->pool,
                                  "failed to close file '%s'",
                                  b->path_end_revision->data);
    }

  return SVN_NO_ERROR;
}

/* An svn_delta_edit_fns_t editor function.
 */
static svn_error_t *
apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct file_baton *b = file_baton;
  apr_status_t status;

  /* Open the file to be used as the base for second revision */
  status = apr_file_open (&b->file_start_revision, b->path_start_revision->data,
                          APR_READ, APR_OS_DEFAULT, b->pool);
  if (status)
    return svn_error_createf (status, 0, NULL, b->pool,
                              "failed to open file '%s'",
                              b->path_start_revision->data);

  /* Open the file that will become the second revision after applying the
     text delta, it starts empty */
  SVN_ERR (create_empty_file (&b->path_end_revision, b->pool));
  SVN_ERR (temp_file_cleanup_register (b->path_end_revision, b->pool));
  status = apr_file_open (&b->file_end_revision, b->path_end_revision->data,
                          APR_WRITE, APR_OS_DEFAULT, b->pool);
  if (status)
    return svn_error_createf (status, 0, NULL, b->pool,
                              "failed to open file '%s'",
                              b->path_end_revision->data);

  svn_txdelta_apply (svn_stream_from_aprfile (b->file_start_revision, b->pool),
                     svn_stream_from_aprfile (b->file_end_revision, b->pool),
                     b->pool,
                     &b->apply_handler, &b->apply_baton);

  *handler = window_handler;
  *handler_baton = file_baton;

  return SVN_NO_ERROR;
}

/* An svn_delta_edit_fns_t editor function.  When the file is closed we
 * have a temporary file containing a pristine version of the repository
 * file. This can be compared against the working copy.
 */
static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *b = file_baton;
  struct edit_baton *eb = b->edit_baton;

  if (b->path_end_revision)
    {
      if (b->added)
        SVN_ERR (eb->diff_callbacks->file_added
                 (b->path,
                  b->path_start_revision->data,
                  b->path_end_revision->data,
                  b->edit_baton->diff_cmd_baton));
      else
        SVN_ERR (eb->diff_callbacks->file_changed
                 (b->path,
                  b->path_start_revision->data,
                  b->path_end_revision->data,
                  b->edit_baton->revision,
                  b->edit_baton->target_revision,
                  b->edit_baton->diff_cmd_baton));
    }

  if (b->propchanges->nelts > 0)
    {
      SVN_ERR (eb->diff_callbacks->props_changed
               (b->path,
                b->propchanges, b->pristine_props,
                b->edit_baton->diff_cmd_baton));
    }

  return SVN_NO_ERROR;
}

/* An svn_delta_edit_fns_t editor function.
 */
static svn_error_t *
close_directory (void *dir_baton)
{
  struct dir_baton *b = dir_baton;
  struct edit_baton *eb = b->edit_baton;

  if (b->propchanges->nelts > 0)
    {
      /* ### HACK.  We have no way of finding the original proplist
         that these property diffs are *against*.  We need an
         RA->get_dir() or something!  */
      SVN_ERR (eb->diff_callbacks->props_changed
               (b->path,
                b->propchanges, apr_hash_make(b->pool),
                b->edit_baton->diff_cmd_baton));
    }

  return SVN_NO_ERROR;
}


/* An svn_delta_edit_fns_t editor function.
 */
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
  
  return SVN_NO_ERROR;
}

/* An svn_delta_edit_fns_t editor function.
 */
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

  return SVN_NO_ERROR;
}


/* An svn_delta_edit_fns_t editor function.
 */
static svn_error_t *
close_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;

  svn_pool_destroy (eb->pool);

  return SVN_NO_ERROR;
}

/* Create a repository diff editor and baton.
 */
svn_error_t *
svn_client__get_diff_editor (svn_stringbuf_t *target,
                             const svn_diff_callbacks_t *diff_callbacks,
                             void *diff_cmd_baton,
                             svn_boolean_t recurse,
                             svn_ra_plugin_t *ra_lib,
                             void *ra_session,
                             svn_revnum_t revision,
                             const svn_delta_editor_t **editor,
                             void **edit_baton,
                             apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_delta_editor_t *tree_editor = svn_delta_default_editor (subpool);
  struct edit_baton *eb = apr_palloc (subpool, sizeof (*eb));

  eb->target = target;
  eb->diff_callbacks = diff_callbacks;
  eb->diff_cmd_baton = diff_cmd_baton;
  eb->recurse = recurse;
  eb->ra_lib = ra_lib;
  eb->ra_session = ra_session;
  eb->revision = revision;
  eb->empty_file = NULL;
  eb->pool = subpool;

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

  *edit_baton = eb;
  *editor = tree_editor;

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
