/* 
 * compose_editors.c -- composing two svn_delta_editor_t's
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
  const svn_delta_editor_t *editor_1;
  void *edit_baton_1;
  const svn_delta_editor_t *editor_2;
  void *edit_baton_2;
  apr_pool_t *pool;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
  void *dir_baton_1;
  void *dir_baton_2;
};


struct file_baton
{
  struct edit_baton *edit_baton;
  void *file_baton_1;
  void *file_baton_2;
  apr_pool_t *file_pool;
};


static svn_error_t *
set_target_revision (void *edit_baton, svn_revnum_t target_revision)
{
  struct edit_baton *eb = edit_baton;

  SVN_ERR ((* (eb->editor_1->set_target_revision)) (eb->edit_baton_1,
                                                    target_revision));
  
  SVN_ERR ((* (eb->editor_2->set_target_revision)) (eb->edit_baton_2,
                                                    target_revision));

  return SVN_NO_ERROR;
}


static svn_error_t *
open_root (void *edit_baton,
           svn_revnum_t base_revision,
           apr_pool_t *dir_pool,
           void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *d = apr_pcalloc (dir_pool, sizeof (*d));

  d->edit_baton = eb;

  SVN_ERR ((* (eb->editor_1->open_root)) (eb->edit_baton_1,
                                          base_revision,
                                          dir_pool,
                                          &(d->dir_baton_1)));
  
  SVN_ERR ((* (eb->editor_2->open_root)) (eb->edit_baton_2,
                                          base_revision,
                                          dir_pool,
                                          &(d->dir_baton_2)));
  
  *root_baton = d;
  
  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry (const char *path,
              svn_revnum_t revision,
              void *parent_baton,
              apr_pool_t *pool)
{
  struct dir_baton *d = parent_baton;

  SVN_ERR ((* (d->edit_baton->editor_1->delete_entry)) (path,
                                                        revision,
                                                        d->dir_baton_1,
                                                        pool));
  
  SVN_ERR ((* (d->edit_baton->editor_2->delete_entry)) (path,
                                                        revision,
                                                        d->dir_baton_2,
                                                        pool));
  
  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (const char *path,
               void *parent_baton,
               const char *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               apr_pool_t *dir_pool,
               void **child_baton)
{
  struct dir_baton *d = parent_baton;
  struct dir_baton *child = apr_pcalloc (dir_pool, sizeof (*child));

  child->edit_baton = d->edit_baton;

  SVN_ERR ((* (d->edit_baton->editor_1->add_directory)) (path,
                                                         d->dir_baton_1,
                                                         copyfrom_path,
                                                         copyfrom_revision,
                                                         dir_pool,
                                                         &child->dir_baton_1));
    
  SVN_ERR ((* (d->edit_baton->editor_2->add_directory)) (path,
                                                         d->dir_baton_2,
                                                         copyfrom_path,
                                                         copyfrom_revision,
                                                         dir_pool,
                                                         &child->dir_baton_2));

  *child_baton = child;

  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory (const char *path,
                void *parent_baton,
                svn_revnum_t base_revision,
                apr_pool_t *dir_pool,
                void **child_baton)
{
  struct dir_baton *d = parent_baton;
  struct dir_baton *child = apr_pcalloc (dir_pool, sizeof (*child));

  child->edit_baton = d->edit_baton;

  SVN_ERR ((* (d->edit_baton->editor_1->open_directory))
           (path, d->dir_baton_1, base_revision, dir_pool,
            &(child->dir_baton_1)));
  
  SVN_ERR ((* (d->edit_baton->editor_2->open_directory))
           (path, d->dir_baton_2, base_revision, dir_pool,
            &(child->dir_baton_2)));

  *child_baton = child;

  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
  struct dir_baton *d = dir_baton;

  SVN_ERR ((* (d->edit_baton->editor_1->close_directory)) (d->dir_baton_1));
  
  SVN_ERR ((* (d->edit_baton->editor_2->close_directory)) (d->dir_baton_2));

  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *fb = file_baton;

  SVN_ERR ((* (fb->edit_baton->editor_1->close_file))
           (fb->file_baton_1));
  
  SVN_ERR ((* (fb->edit_baton->editor_2->close_file))
           (fb->file_baton_2));

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;

  SVN_ERR ((* (eb->editor_1->close_edit)) (eb->edit_baton_1));
  
  SVN_ERR ((* (eb->editor_2->close_edit)) (eb->edit_baton_2));

  return SVN_NO_ERROR;
}


static svn_error_t *
abort_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;

  SVN_ERR ((* (eb->editor_1->abort_edit)) (eb->edit_baton_1));
  
  SVN_ERR ((* (eb->editor_2->abort_edit)) (eb->edit_baton_2));

  return SVN_NO_ERROR;
}



struct handler_pair
{
  svn_txdelta_window_handler_t handler_1;
  svn_txdelta_window_handler_t handler_2;
  void *handler_baton_1;
  void *handler_baton_2;
};


static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *handler_pair)
{
  struct handler_pair *hp = handler_pair;

  if (hp->handler_1)
    SVN_ERR ((* (hp->handler_1)) (window, hp->handler_baton_1));

  if (hp->handler_2)
    SVN_ERR ((* (hp->handler_2)) (window, hp->handler_baton_2));

  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct file_baton *fb = file_baton;
  svn_txdelta_window_handler_t h1;
  svn_txdelta_window_handler_t h2;
  void *hb1;
  void *hb2;
  
  SVN_ERR ((* (fb->edit_baton->editor_1->apply_textdelta)) (fb->file_baton_1,
                                                            &h1,
                                                            &hb1));

  SVN_ERR ((* (fb->edit_baton->editor_2->apply_textdelta)) (fb->file_baton_2,
                                                            &h2,
                                                            &hb2));

  if (h1 || h2)
    {
      struct handler_pair *hp = apr_pcalloc (fb->file_pool, sizeof (*hp));

      hp->handler_1 = h1;
      hp->handler_2 = h2;
      hp->handler_baton_1 = hb1;
      hp->handler_baton_2 = hb2;

      *handler = window_handler;
      *handler_baton = hp;
    }
  else
    {
      *handler = NULL;
      *handler_baton = NULL;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (const char *path,
          void *parent_baton,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          apr_pool_t *file_pool,
          void **file_baton)
{
  struct dir_baton *d = parent_baton;
  struct file_baton *fb = apr_pcalloc (file_pool, sizeof (*fb));

  fb->edit_baton = d->edit_baton;
  fb->file_pool = file_pool;

  SVN_ERR ((* (d->edit_baton->editor_1->add_file)) (path,
                                                    d->dir_baton_1,
                                                    copyfrom_path, 
                                                    copyfrom_revision,
                                                    file_pool,
                                                    &(fb->file_baton_1)));

  SVN_ERR ((* (d->edit_baton->editor_2->add_file)) (path,
                                                    d->dir_baton_2,
                                                    copyfrom_path, 
                                                    copyfrom_revision,
                                                    file_pool,
                                                    &(fb->file_baton_2)));

  *file_baton = fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_file (const char *path,
           void *parent_baton,
           svn_revnum_t base_revision,
           apr_pool_t *file_pool,
           void **file_baton)
{
  struct dir_baton *d = parent_baton;
  struct file_baton *fb = apr_pcalloc (file_pool, sizeof (*fb));

  fb->edit_baton = d->edit_baton;
  fb->file_pool = file_pool;

  SVN_ERR ((* (d->edit_baton->editor_1->open_file)) (path,
                                                     d->dir_baton_1,
                                                     base_revision,
                                                     file_pool,
                                                     &(fb->file_baton_1)));

  SVN_ERR ((* (d->edit_baton->editor_2->open_file)) (path,
                                                     d->dir_baton_2,
                                                     base_revision,
                                                     file_pool,
                                                     &(fb->file_baton_2)));

  *file_baton = fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  const char *name,
                  const svn_string_t *value,
                  apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;

  SVN_ERR ((* (fb->edit_baton->editor_1->change_file_prop)) (fb->file_baton_1,
                                                             name,
                                                             value,
                                                             pool));

  SVN_ERR ((* (fb->edit_baton->editor_2->change_file_prop)) (fb->file_baton_2,
                                                             name,
                                                             value,
                                                             pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct dir_baton *d = dir_baton;

  SVN_ERR ((* (d->edit_baton->editor_1->change_dir_prop)) (d->dir_baton_1,
                                                           name,
                                                           value,
                                                           pool));

  SVN_ERR ((* (d->edit_baton->editor_2->change_dir_prop)) (d->dir_baton_2,
                                                           name,
                                                           value,
                                                           pool));

  return SVN_NO_ERROR;
}



/*** Public interfaces. ***/

void
svn_delta_compose_editors (const svn_delta_editor_t **new_editor,
                           void **new_edit_baton,
                           const svn_delta_editor_t *editor_1,
                           void *edit_baton_1,
                           const svn_delta_editor_t *editor_2,
                           void *edit_baton_2,
                           apr_pool_t *pool)
{
  struct edit_baton *eb = apr_pcalloc (pool, sizeof (*eb));
  svn_delta_editor_t *editor = svn_delta_default_editor (pool);
  
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
svn_delta_wrap_editor (const svn_delta_editor_t **new_editor,
                       void **new_edit_baton,
                       const svn_delta_editor_t *before_editor,
                       void *before_edit_baton,
                       const svn_delta_editor_t *middle_editor,
                       void *middle_edit_baton,
                       const svn_delta_editor_t *after_editor,
                       void *after_edit_baton,
                       apr_pool_t *pool)
{
  assert (middle_editor != NULL);

  if ((! before_editor) && (! after_editor))
    {
      *new_editor = middle_editor;
      *new_edit_baton = middle_edit_baton;
      return;
    }

  if (before_editor)
    {
      svn_delta_compose_editors (new_editor, new_edit_baton,
                                 before_editor, before_edit_baton,
                                 middle_editor, middle_edit_baton,
                                 pool);
      middle_editor = *new_editor;
      middle_edit_baton = *new_edit_baton;
    }

  if (after_editor)
    {
      svn_delta_compose_editors (new_editor, new_edit_baton,
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
