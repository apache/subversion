/*
 * track_editor.c : editor implementation which tracks committed targets
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include "ra_local.h"





/* ------------------------------------------------------------------- */

/*** The editor batons ***/
struct edit_baton
{
  apr_pool_t *pool;
  svn_string_t *initial_path;
  svn_ra_local__commit_closer_t *closer;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent_dir_baton;
  svn_string_t *path;
};


struct file_baton
{
  struct dir_baton *parent_dir_baton;
  svn_string_t *path;
};



/*** the anonymous editor functions ***/

static svn_error_t *
replace_root (void *edit_baton, svn_revnum_t base_revision, void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *rb = apr_pcalloc (eb->pool, sizeof (*rb));

  rb->edit_baton = eb;
  rb->parent_dir_baton = NULL;
  rb->path = eb->initial_path;

  *root_baton = rb;

  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (svn_string_t *name,
               void *parent_baton,
               svn_string_t *ancestor_path,
               long int ancestor_revision,
               void **child_baton)
{
  struct dir_baton *parent_d = parent_baton;
  struct dir_baton *child_d
    = apr_pcalloc (parent_d->edit_baton->pool, sizeof (*child_d));

  child_d->edit_baton = parent_d->edit_baton;
  child_d->parent_dir_baton = parent_d;
  child_d->path = svn_string_dup (parent_d->path, child_d->edit_baton->pool);
  svn_path_add_component (child_d->path, name, svn_path_local_style);
  child_d->added = TRUE;

  *child_baton = child_d;

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_directory (svn_string_t *name,
                   void *parent_baton,
                   long int ancestor_revision,
                   void **child_baton)
{
  struct dir_baton *parent_d = parent_baton;
  struct dir_baton *child_d
    = apr_pcalloc (parent_d->edit_baton->pool, sizeof (*child_d));

  child_d->edit_baton = parent_d->edit_baton;
  child_d->parent_dir_baton = parent_d;
  child_d->path = svn_string_dup (parent_d->path, child_d->edit_baton->pool);
  svn_path_add_component (child_d->path, name, svn_path_local_style);

  *child_baton = child_d;

  return SVN_NO_ERROR;
}

static svn_error_t *
close_file (void *file_baton)
{
  svn_error_t *err;
  struct file_baton *fb = file_baton;

  apr_hash_set (d->edit_baton->closer->target_array,
                fb->path->data,
                fb->path->len,
                1);

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  /* kff todo: Ben, I wasn't sure what to do here.  At first, I
     thought we'd run over the items in the target_array, invoking the
     close_func() and set_func() on them.  But now that I think about
     it, that's the province of the true commit editor, not the
     tracking editor -- after all, it's the commit editor that knows
     the new revision number.  Anyway, if this func here ends up not
     doing anything, we should remove it altogether. */

  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (svn_string_t *name,
          void *parent_baton,
          svn_string_t *ancestor_path,
          long int ancestor_revision,
          void **file_baton)
{
  struct dir_baton *parent_d = parent_baton;
  struct file_baton *child_fb
    = apr_pcalloc (parent_d->edit_baton->pool, sizeof (*child_fb));

  child_fb->parent_dir_baton = parent_d;
  child_fb->path = svn_string_dup (parent_d->path, parent_d->edit_baton->pool);
  svn_path_add_component (child_fb->path, name, svn_path_local_style);
  child_fb->added = TRUE;

  *file_baton = child_fb;

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_file (svn_string_t *name,
              void *parent_baton,
              long int ancestor_revision,
              void **file_baton)
{
  struct dir_baton *parent_d = parent_baton;
  struct file_baton *child_fb
    = apr_pcalloc (parent_d->edit_baton->pool, sizeof (*child_fb));

  child_fb->parent_dir_baton = parent_d;
  child_fb->path = svn_string_dup (parent_d->path, parent_d->edit_baton->pool);
  svn_path_add_component (child_fb->path, name, svn_path_local_style);

  *file_baton = child_fb;

  return SVN_NO_ERROR;
}



/*** exported routine ***/

svn_error_t *
svn_ra_local__get_commit_track_editor (svn_delta_edit_fns_t **editor,
                                       void **edit_baton,
                                       apr_pool_t *pool,
                                       svn_ra_local__commit_closer_t *closer)
{
  svn_error_t *err;
  struct edit_baton *eb = apr_pcalloc (pool, sizeof (*eb));
  svn_delta_edit_fns_t *track_editor = svn_delta_default_editor (pool);

  /* Set up the editor.  These functions are no-ops, so the default
     editor's implementations are used:

        set_target_revision
        close_directory
        window_handler
        apply_textdelta
        change_file_prop
        change_dir_prop
     
     I'm pretty sure delete_entry is a no-op too, so it is also
     unimplemented.  It's true the delete is part of the commit, but
     that entity's record is expunged afterwards, so there's no point
     bumping its revision number.  So, delete_entry is left in the
     default implementation as well. */
  track_editor->replace_root = replace_root;
  track_editor->delete_entry = delete_entry;
  track_editor->add_directory = add_directory;
  track_editor->replace_directory = replace_directory;
  track_editor->add_file = add_file;
  track_editor->replace_file = replace_file;
  track_editor->close_file = close_file;
  track_editor->close_edit = close_edit;

  /* Set up the edit baton. */
  eb->pool = pool;
  eb->initial_path = svn_string_create ("", pool);
  eb->closer = closer;

  *editor = track_editor;
  *edit_baton = eb;

  return SVN_NO_ERROR;
}



/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */





