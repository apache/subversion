/* 
 * compose_editors.c -- composing two svn_delta_edit_fns_t's
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


#include <assert.h>
#include <apr_pools.h>
#include "svn_delta.h"



struct dir_baton
{
  struct dir_baton *parent_dir_baton;
  apr_pool_t *pool;
  const svn_delta_edit_fns_t *editor_1;
  const svn_delta_edit_fns_t *editor_2;
  void *root_dir_baton_1;
  void *root_dir_baton_2;
};


struct file_baton
{
  struct dir_baton *dir_baton;
  void *file_baton_1;
  void *file_baton_2;
};


static svn_error_t *
delete_item (svn_string_t *name, void *parent_baton)
{
  struct dir_baton *d = parent_baton;
  svn_error_t *err;

  if (d->editor_1->delete_item)
    {
      err = (* (d->editor_1->delete_item)) (name, d->root_dir_baton_1);
      if (err)
        return err;
    }
  
  if (d->editor_2->delete_item)
    {
      err = (* (d->editor_2->delete_item)) (name, d->root_dir_baton_2);
      if (err)
        return err;
    }
  
  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (svn_string_t *name,
               void *parent_baton,
               svn_string_t *ancestor_path,
               long int ancestor_revision,
               void **child_baton)
{
  struct dir_baton *d = parent_baton;
  svn_error_t *err;
  struct dir_baton *child = apr_pcalloc (d->pool, sizeof (*child));

  child->pool = d->pool;
  child->editor_1 = d->editor_1;
  child->editor_2 = d->editor_2;
  child->parent_dir_baton = parent_baton;

  if (d->editor_1->add_directory)
    {
      err = (* (d->editor_1->add_directory))
        (name, d->root_dir_baton_1, ancestor_path, ancestor_revision,
         &(child->root_dir_baton_1));
      if (err)
        return err;
    }

  if (d->editor_2->add_directory)
    {
      err = (* (d->editor_2->add_directory))
        (name, d->root_dir_baton_2, ancestor_path, ancestor_revision,
         &(child->root_dir_baton_2));
      if (err)
        return err;
    }

  *child_baton = child;

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_directory (svn_string_t *name,
                   void *parent_baton,
                   svn_string_t *ancestor_path,
                   long int ancestor_revision,
                   void **child_baton)
{
  struct dir_baton *d = parent_baton;
  svn_error_t *err;
  struct dir_baton *child = apr_pcalloc (d->pool, sizeof (*child));

  child->pool = d->pool;
  child->editor_1 = d->editor_1;
  child->editor_2 = d->editor_2;
  child->parent_dir_baton = parent_baton;

  if (d->editor_1->replace_directory)
    {
      err = (* (d->editor_1->replace_directory))
        (name, d->root_dir_baton_1, ancestor_path, ancestor_revision,
         &(child->root_dir_baton_1));
      if (err)
        return err;
    }

  if (d->editor_2->replace_directory)
    {
      err = (* (d->editor_2->replace_directory))
        (name, d->root_dir_baton_2, ancestor_path, ancestor_revision,
         &(child->root_dir_baton_2));
      if (err)
        return err;
    }

  *child_baton = child;

  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
  struct dir_baton *d = dir_baton;
  svn_error_t *err;

  if (d->editor_1->close_directory)
    {
      err = (* (d->editor_1->close_directory)) (d->root_dir_baton_1);
      if (err)
        return err;
    }
  
  if (d->editor_2->close_directory)
    {
      err = (* (d->editor_2->close_directory)) (d->root_dir_baton_2);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *fb = file_baton;
  svn_error_t *err;

  if (fb->dir_baton->editor_1->close_file)
    {
      err = (* (fb->dir_baton->editor_1->close_file))
        (fb->file_baton_1);
      if (err)
        return err;
    }
  
  if (fb->dir_baton->editor_2->close_file)
    {
      err = (* (fb->dir_baton->editor_2->close_file))
        (fb->file_baton_2);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}


struct handler_pair
{
  struct file_baton *file_baton;
  svn_txdelta_window_handler_t *handler_1;
  svn_txdelta_window_handler_t *handler_2;
  void *handler_baton_1;
  void *handler_baton_2;
};


static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *handler_pair)
{
  struct handler_pair *hp = handler_pair;
  svn_error_t *err;
  
  if (hp->handler_1)
    {
      err = (* (hp->handler_1)) (window, hp->handler_baton_1);
      if (err)
        return err;
    }

  if (hp->handler_2)
    {
      err = (* (hp->handler_2)) (window, hp->handler_baton_2);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t **handler,
                 void **handler_baton)
{
  struct file_baton *fb = file_baton;
  svn_error_t *err;
  struct handler_pair *hp
    = apr_pcalloc (fb->dir_baton->pool, sizeof (*hp));
  
  hp->file_baton = fb;

  if (fb->dir_baton->editor_1->apply_textdelta)
    {
      err = (* (fb->dir_baton->editor_1->apply_textdelta))
        (fb->file_baton_1, &(hp->handler_1), &(hp->handler_baton_1));
      if (err)
        return err;
    }

  if (fb->dir_baton->editor_2->apply_textdelta)
    {
      err = (* (fb->dir_baton->editor_2->apply_textdelta))
        (fb->file_baton_2, &(hp->handler_2), &(hp->handler_baton_2));
      if (err)
        return err;
    }

  *handler = window_handler;
  *handler_baton = hp;

  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (svn_string_t *name,
          void *parent_baton,
          svn_string_t *ancestor_path,
          long int ancestor_revision,
          void **file_baton)
{
  struct dir_baton *d = parent_baton;
  svn_error_t *err;
  struct file_baton *fb = apr_pcalloc (d->pool, sizeof (*fb));

  fb->dir_baton = d;

  if (d->editor_1->add_file)
    {
      err = (* (d->editor_1->add_file))
        (name, d->root_dir_baton_1, ancestor_path, ancestor_revision,
         &(fb->file_baton_1));
      if (err)
        return err;
    }

  if (d->editor_2->add_file)
    {
      err = (* (d->editor_2->add_file))
        (name, d->root_dir_baton_2, ancestor_path, ancestor_revision,
         &(fb->file_baton_2));
      if (err)
        return err;
    }

  *file_baton = fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
replace_file (svn_string_t *name,
              void *parent_baton,
              svn_string_t *ancestor_path,
              long int ancestor_revision,
              void **file_baton)
{
  struct dir_baton *d = parent_baton;
  svn_error_t *err;
  struct file_baton *fb = apr_pcalloc (d->pool, sizeof (*fb));

  fb->dir_baton = d;

  if (d->editor_1->replace_file)
    {
      err = (* (d->editor_1->replace_file))
        (name, d->root_dir_baton_1, ancestor_path, ancestor_revision,
         &(fb->file_baton_1));
      if (err)
        return err;
    }

  if (d->editor_2->replace_file)
    {
      err = (* (d->editor_2->replace_file))
        (name, d->root_dir_baton_2, ancestor_path, ancestor_revision,
         &(fb->file_baton_2));
      if (err)
        return err;
    }

  *file_baton = fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_string_t *name,
                  svn_string_t *value)
{
  struct file_baton *fb = file_baton;
  svn_error_t *err;

  if (fb->dir_baton->editor_1->change_file_prop)
    {
      err = (* (fb->dir_baton->editor_1->change_file_prop))
        (fb->file_baton_1, name, value);
      if (err)
        return err;
    }

  if (fb->dir_baton->editor_2->change_file_prop)
    {
      err = (* (fb->dir_baton->editor_2->change_file_prop))
        (fb->file_baton_2, name, value);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *parent_baton,
                 svn_string_t *name,
                 svn_string_t *value)
{
  struct dir_baton *d = parent_baton;
  svn_error_t *err;

  if (d->editor_1->change_dir_prop)
    {
      err = (* (d->editor_1->change_dir_prop))
        (d->root_dir_baton_1, name, value);
      if (err)
        return err;
    }

  if (d->editor_2->change_dir_prop)
    {
      err = (* (d->editor_2->change_dir_prop))
        (d->root_dir_baton_2, name, value);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}



void
svn_delta_compose_editors (const svn_delta_edit_fns_t **new_editor,
                           void **new_root_dir_baton,
                           const svn_delta_edit_fns_t *editor_1,
                           void *root_dir_baton_1,
                           const svn_delta_edit_fns_t *editor_2,
                           void *root_dir_baton_2,
                           apr_pool_t *pool)
{
  svn_delta_edit_fns_t *editor = svn_delta_default_editor (pool);
  struct dir_baton *rb = apr_pcalloc (pool, sizeof (*rb));
  
  /* Set up the editor. */
  editor->delete_item = delete_item;
  editor->add_directory = add_directory;
  editor->replace_directory = replace_directory;
  editor->change_dir_prop = change_dir_prop;
  editor->close_directory = close_directory;
  editor->add_file = add_file;
  editor->replace_file = replace_file;
  editor->apply_textdelta = apply_textdelta;
  editor->change_file_prop = change_file_prop;
  editor->close_file = close_file;

  /* Set up the root directory baton. */
  rb->editor_1 = editor_1;
  rb->editor_2 = editor_2;
  rb->root_dir_baton_1 = root_dir_baton_1;
  rb->root_dir_baton_2 = root_dir_baton_2;
  rb->pool = pool;

  *new_root_dir_baton = rb;
  *new_editor = editor;
}


void
svn_delta_wrap_editor (const svn_delta_edit_fns_t **new_editor,
                       void **new_root_dir_baton,
                       const svn_delta_edit_fns_t *before_editor,
                       void *before_root_dir_baton,
                       const svn_delta_edit_fns_t *middle_editor,
                       void *middle_root_dir_baton,
                       const svn_delta_edit_fns_t *after_editor,
                       void *after_root_dir_baton,
                       apr_pool_t *pool)
{
  assert (middle_editor != NULL);

  if (before_editor)
    {
      svn_delta_compose_editors (new_editor, new_root_dir_baton,
                                 before_editor, before_root_dir_baton,
                                 middle_editor, middle_root_dir_baton,
                                 pool);
      middle_editor = *new_editor;
      middle_root_dir_baton = *new_root_dir_baton;
    }

  if (after_editor)
    {
      svn_delta_compose_editors (new_editor, new_root_dir_baton,
                                 middle_editor, middle_root_dir_baton,
                                 after_editor, after_root_dir_baton,
                                 pool);
    }
}





/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
