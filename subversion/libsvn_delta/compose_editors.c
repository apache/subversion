/* 
 * compose_editors.c -- composing two svn_delta_edit_fns_t's
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


#include <assert.h>
#include <apr_pools.h>
#include "svn_delta.h"



struct edit_baton
{
  const svn_delta_edit_fns_t *editor_1;
  void *edit_baton_1;
  const svn_delta_edit_fns_t *editor_2;
  void *edit_baton_2;
  apr_pool_t *pool;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent_dir_baton;
  void *dir_baton_1;
  void *dir_baton_2;
};


struct file_baton
{
  struct dir_baton *dir_baton;
  void *file_baton_1;
  void *file_baton_2;
};


static svn_error_t *
set_target_revision (void *edit_baton, svn_revnum_t target_revision)
{
  struct edit_baton *eb = edit_baton;
  svn_error_t *err;

  err = (* (eb->editor_1->set_target_revision)) (eb->edit_baton_1,
                                                 target_revision);
  if (err)
    return err;
  
  err = (* (eb->editor_2->set_target_revision)) (eb->edit_baton_2,
                                                 target_revision);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


static svn_error_t *
open_root (void *edit_baton, svn_revnum_t base_revision, void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  svn_error_t *err;
  struct dir_baton *d = apr_pcalloc (eb->pool, sizeof (*d));

  d->edit_baton = eb;
  d->parent_dir_baton = NULL;

  err = (* (eb->editor_1->open_root)) (eb->edit_baton_1,
                                       base_revision,
                                       &(d->dir_baton_1));
  if (err)
    return err;
  
  err = (* (eb->editor_2->open_root)) (eb->edit_baton_2,
                                       base_revision,
                                       &(d->dir_baton_2));
  if (err)
    return err;
  
  *root_baton = d;
  
  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry (svn_stringbuf_t *name, svn_revnum_t revision, void *parent_baton)
{
  struct dir_baton *d = parent_baton;
  svn_error_t *err;

  err = (* (d->edit_baton->editor_1->delete_entry)) 
    (name, revision, d->dir_baton_1);
  if (err)
    return err;
  
  err = (* (d->edit_baton->editor_2->delete_entry)) 
    (name, revision, d->dir_baton_2);
  if (err)
    return err;
  
  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (svn_stringbuf_t *name,
               void *parent_baton,
               svn_stringbuf_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **child_baton)
{
  struct dir_baton *d = parent_baton;
  svn_error_t *err;
  struct dir_baton *child = apr_pcalloc (d->edit_baton->pool, sizeof (*child));

  child->edit_baton = d->edit_baton;
  child->parent_dir_baton = d;

  err = (* (d->edit_baton->editor_1->add_directory))
    (name, d->dir_baton_1, copyfrom_path, copyfrom_revision,
     &(child->dir_baton_1));
  if (err)
    return err;
    
  err = (* (d->edit_baton->editor_2->add_directory))
    (name, d->dir_baton_2, copyfrom_path, copyfrom_revision,
     &(child->dir_baton_2));
  if (err)
    return err;

  *child_baton = child;

  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory (svn_stringbuf_t *name,
                void *parent_baton,
                svn_revnum_t base_revision,
                void **child_baton)
{
  struct dir_baton *d = parent_baton;
  svn_error_t *err;
  struct dir_baton *child = apr_pcalloc (d->edit_baton->pool, sizeof (*child));

  child->edit_baton = d->edit_baton;
  child->parent_dir_baton = d;

  err = (* (d->edit_baton->editor_1->open_directory))
    (name, d->dir_baton_1, base_revision, &(child->dir_baton_1));
  if (err)
    return err;
  
  err = (* (d->edit_baton->editor_2->open_directory))
    (name, d->dir_baton_2, base_revision, &(child->dir_baton_2));
  if (err)
    return err;

  *child_baton = child;

  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
  struct dir_baton *d = dir_baton;
  svn_error_t *err;

  err = (* (d->edit_baton->editor_1->close_directory)) (d->dir_baton_1);
  if (err)
    return err;
  
  err = (* (d->edit_baton->editor_2->close_directory)) (d->dir_baton_2);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *fb = file_baton;
  svn_error_t *err;

  err = (* (fb->dir_baton->edit_baton->editor_1->close_file))
    (fb->file_baton_1);
  if (err)
    return err;
  
  err = (* (fb->dir_baton->edit_baton->editor_2->close_file))
    (fb->file_baton_2);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;
  svn_error_t *err;

  err = (* (eb->editor_1->close_edit)) (eb->edit_baton_1);
  if (err)
    return err;
  
  err = (* (eb->editor_2->close_edit)) (eb->edit_baton_2);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


static svn_error_t *
abort_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;
  svn_error_t *err;

  err = (* (eb->editor_1->abort_edit)) (eb->edit_baton_1);
  if (err)
    return err;
  
  err = (* (eb->editor_2->abort_edit)) (eb->edit_baton_2);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



struct handler_pair
{
  struct file_baton *file_baton;
  svn_txdelta_window_handler_t handler_1;
  svn_txdelta_window_handler_t handler_2;
  void *handler_baton_1;
  void *handler_baton_2;
};


static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *handler_pair)
{
  struct handler_pair *hp = handler_pair;
  svn_error_t *err;
  
  err = (* (hp->handler_1)) (window, hp->handler_baton_1);
  if (err)
    return err;

  err = (* (hp->handler_2)) (window, hp->handler_baton_2);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct file_baton *fb = file_baton;
  svn_error_t *err;
  struct handler_pair *hp
    = apr_pcalloc (fb->dir_baton->edit_baton->pool, sizeof (*hp));
  
  hp->file_baton = fb;

  err = (* (fb->dir_baton->edit_baton->editor_1->apply_textdelta))
    (fb->file_baton_1, &(hp->handler_1), &(hp->handler_baton_1));
  if (err)
    return err;

  err = (* (fb->dir_baton->edit_baton->editor_2->apply_textdelta))
    (fb->file_baton_2, &(hp->handler_2), &(hp->handler_baton_2));
  if (err)
    return err;

  *handler = window_handler;
  *handler_baton = hp;

  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (svn_stringbuf_t *name,
          void *parent_baton,
          svn_stringbuf_t *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          void **file_baton)
{
  struct dir_baton *d = parent_baton;
  svn_error_t *err;
  struct file_baton *fb = apr_pcalloc (d->edit_baton->pool, sizeof (*fb));

  fb->dir_baton = d;

  err = (* (d->edit_baton->editor_1->add_file))
    (name, d->dir_baton_1, copyfrom_path, 
     copyfrom_revision, &(fb->file_baton_1));
  if (err)
    return err;

  err = (* (d->edit_baton->editor_2->add_file))
    (name, d->dir_baton_2, copyfrom_path, 
     copyfrom_revision, &(fb->file_baton_2));
  if (err)
    return err;

  *file_baton = fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_file (svn_stringbuf_t *name,
           void *parent_baton,
           svn_revnum_t base_revision,
           void **file_baton)
{
  struct dir_baton *d = parent_baton;
  svn_error_t *err;
  struct file_baton *fb = apr_pcalloc (d->edit_baton->pool, sizeof (*fb));

  fb->dir_baton = d;

  err = (* (d->edit_baton->editor_1->open_file))
    (name, d->dir_baton_1, base_revision, &(fb->file_baton_1));
  if (err)
    return err;

  err = (* (d->edit_baton->editor_2->open_file))
    (name, d->dir_baton_2, base_revision, &(fb->file_baton_2));
  if (err)
    return err;

  *file_baton = fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_stringbuf_t *name,
                  svn_stringbuf_t *value)
{
  struct file_baton *fb = file_baton;
  svn_error_t *err;

  err = (* (fb->dir_baton->edit_baton->editor_1->change_file_prop))
    (fb->file_baton_1, name, value);
  if (err)
    return err;

  err = (* (fb->dir_baton->edit_baton->editor_2->change_file_prop))
    (fb->file_baton_2, name, value);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_stringbuf_t *name,
                 svn_stringbuf_t *value)
{
  struct dir_baton *d = dir_baton;
  svn_error_t *err;

  err = (* (d->edit_baton->editor_1->change_dir_prop))
    (d->dir_baton_1, name, value);
  if (err)
    return err;

  err = (* (d->edit_baton->editor_2->change_dir_prop))
    (d->dir_baton_2, name, value);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/*** Public interfaces. ***/

void
svn_delta_compose_old_editors (const svn_delta_edit_fns_t **new_editor,
                               void **new_edit_baton,
                               const svn_delta_edit_fns_t *editor_1,
                               void *edit_baton_1,
                               const svn_delta_edit_fns_t *editor_2,
                               void *edit_baton_2,
                               apr_pool_t *pool)
{
  struct edit_baton *eb = apr_pcalloc (pool, sizeof (*eb));
  svn_delta_edit_fns_t *editor = svn_delta_old_default_editor (pool);
  
  /* Set up the editor. */
  editor->set_target_revision = set_target_revision;
  editor->open_root = open_root;
  editor->delete_entry = delete_entry;
  editor->add_directory = add_directory;
  editor->open_directory = open_directory;
  editor->change_dir_prop = change_dir_prop;
  editor->close_directory = close_directory;
  editor->add_file = add_file;
  editor->open_file = open_file;
  editor->apply_textdelta = apply_textdelta;
  editor->change_file_prop = change_file_prop;
  editor->close_file = close_file;
  editor->close_edit = close_edit;
  editor->abort_edit = abort_edit;

  /* Set up the edit baton. */
  eb->editor_1 = editor_1;
  eb->editor_2 = editor_2;
  eb->edit_baton_1 = edit_baton_1;
  eb->edit_baton_2 = edit_baton_2;
  eb->pool = pool;

  *new_edit_baton = eb;
  *new_editor = editor;
}


void
svn_delta_wrap_old_editor (const svn_delta_edit_fns_t **new_editor,
                           void **new_edit_baton,
                           const svn_delta_edit_fns_t *before_editor,
                           void *before_edit_baton,
                           const svn_delta_edit_fns_t *middle_editor,
                           void *middle_edit_baton,
                           const svn_delta_edit_fns_t *after_editor,
                           void *after_edit_baton,
                           apr_pool_t *pool)
{
  assert (middle_editor != NULL);

  if (before_editor)
    {
      svn_delta_compose_old_editors (new_editor, new_edit_baton,
                                     before_editor, before_edit_baton,
                                     middle_editor, middle_edit_baton,
                                     pool);
      middle_editor = *new_editor;
      middle_edit_baton = *new_edit_baton;
    }

  if (after_editor)
    {
      svn_delta_compose_old_editors (new_editor, new_edit_baton,
                                     middle_editor, middle_edit_baton,
                                     after_editor, after_edit_baton,
                                     pool);
    }
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
