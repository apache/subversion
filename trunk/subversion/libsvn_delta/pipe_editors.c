/* 
 * pipe_editors.c -- an editor that acts as a "pipe" to another editor
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




static svn_error_t *
set_target_revision (void *edit_baton, svn_revnum_t target_revision)
{
  struct svn_pipe_edit_baton *eb = edit_baton;

  SVN_ERR ((* (eb->real_editor->set_target_revision)) (eb->real_edit_baton,
                                                       target_revision));
  return SVN_NO_ERROR;
}


static svn_error_t *
open_root (void *edit_baton, svn_revnum_t base_revision, void **root_baton)
{
  struct svn_pipe_edit_baton *eb = edit_baton;
  struct svn_pipe_dir_baton *d = apr_pcalloc (eb->pool, sizeof (*d));

  d->edit_baton = eb;
  d->parent_dir_baton = NULL;

  SVN_ERR ((* (eb->real_editor->open_root)) (eb->real_edit_baton,
                                             base_revision,
                                             &(d->real_dir_baton)));
    
  *root_baton = d;
  
  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry (svn_stringbuf_t *name, svn_revnum_t revision, void *parent_baton)
{
  struct svn_pipe_dir_baton *d = parent_baton;

  SVN_ERR ((* (d->edit_baton->real_editor->delete_entry)) 
           (name, revision, d->real_dir_baton));
  
  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (svn_stringbuf_t *name,
               void *parent_baton,
               svn_stringbuf_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **child_baton)
{
  struct svn_pipe_dir_baton *d = parent_baton;
  struct svn_pipe_dir_baton *child = apr_pcalloc (d->edit_baton->pool, sizeof (*child));

  child->edit_baton = d->edit_baton;
  child->parent_dir_baton = d;

  SVN_ERR ((* (d->edit_baton->real_editor->add_directory))
           (name, d->real_dir_baton, copyfrom_path, copyfrom_revision,
            &(child->real_dir_baton)));
           
  *child_baton = child;

  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory (svn_stringbuf_t *name,
                void *parent_baton,
                svn_revnum_t base_revision,
                void **child_baton)
{
  struct svn_pipe_dir_baton *d = parent_baton;
  struct svn_pipe_dir_baton *child = apr_pcalloc (d->edit_baton->pool, sizeof (*child));

  child->edit_baton = d->edit_baton;
  child->parent_dir_baton = d;

  SVN_ERR ((* (d->edit_baton->real_editor->open_directory))
           (name, d->real_dir_baton, base_revision, &(child->real_dir_baton)));

  *child_baton = child;

  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
  struct svn_pipe_dir_baton *d = dir_baton;

  SVN_ERR ((* (d->edit_baton->real_editor->close_directory)) 
           (d->real_dir_baton));

  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
  struct svn_pipe_file_baton *fb = file_baton;

  SVN_ERR ((* (fb->dir_baton->edit_baton->real_editor->close_file))
           (fb->real_file_baton));

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  struct svn_pipe_edit_baton *eb = edit_baton;

  SVN_ERR ((* (eb->real_editor->close_edit)) (eb->real_edit_baton));

  return SVN_NO_ERROR;
}


static svn_error_t *
abort_edit (void *edit_baton)
{
  struct svn_pipe_edit_baton *eb = edit_baton;

  SVN_ERR ((* (eb->real_editor->abort_edit)) (eb->real_edit_baton));
  
  return SVN_NO_ERROR;
}


static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *handler)
{
  struct svn_pipe_handler_wrapper *hw = handler;
  
  SVN_ERR ((* (hw->real_handler)) (window, hw->real_handler_baton));

  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct svn_pipe_file_baton *fb = file_baton;
  struct svn_pipe_handler_wrapper *hw
    = apr_pcalloc (fb->dir_baton->edit_baton->pool, sizeof (*hw));
  
  hw->file_baton = fb;

  SVN_ERR ((* (fb->dir_baton->edit_baton->real_editor->apply_textdelta))
           (fb->real_file_baton,
            &(hw->real_handler), &(hw->real_handler_baton)));

  *handler = window_handler;
  *handler_baton = hw;

  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (svn_stringbuf_t *name,
          void *parent_baton,
          svn_stringbuf_t *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          void **file_baton)
{
  struct svn_pipe_dir_baton *d = parent_baton;
  struct svn_pipe_file_baton *fb
    = apr_pcalloc (d->edit_baton->pool, sizeof (*fb));

  fb->dir_baton = d;

  SVN_ERR ((* (d->edit_baton->real_editor->add_file))
           (name, d->real_dir_baton, copyfrom_path, 
            copyfrom_revision, &(fb->real_file_baton)));

  *file_baton = fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_file (svn_stringbuf_t *name,
           void *parent_baton,
           svn_revnum_t base_revision,
           void **file_baton)
{
  struct svn_pipe_dir_baton *d = parent_baton;
  struct svn_pipe_file_baton *fb
    = apr_pcalloc (d->edit_baton->pool, sizeof (*fb));

  fb->dir_baton = d;

  SVN_ERR ((* (d->edit_baton->real_editor->open_file))
           (name, d->real_dir_baton, base_revision, &(fb->real_file_baton)));

  *file_baton = fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_stringbuf_t *name,
                  svn_stringbuf_t *value)
{
  struct svn_pipe_file_baton *fb = file_baton;

  SVN_ERR ((* (fb->dir_baton->edit_baton->real_editor->change_file_prop))
           (fb->real_file_baton, name, value));

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_stringbuf_t *name,
                 svn_stringbuf_t *value)
{
  struct svn_pipe_dir_baton *d = dir_baton;

  SVN_ERR ((* (d->edit_baton->real_editor->change_dir_prop))
           (d->real_dir_baton, name, value));

  return SVN_NO_ERROR;
}



/*** Public interfaces. ***/

void
svn_delta_old_default_pipe_editor (svn_delta_edit_fns_t **new_editor,
                                   struct svn_pipe_edit_baton **new_edit_baton,
                                   const svn_delta_edit_fns_t *editor_to_wrap,
                                   void *edit_baton_to_wrap,
                                   apr_pool_t *pool)
{
  struct svn_pipe_edit_baton *eb = apr_pcalloc (pool, sizeof (*eb));
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
  eb->real_editor = editor_to_wrap;
  eb->real_edit_baton = edit_baton_to_wrap;
  eb->pool = pool;

  *new_edit_baton = eb;
  *new_editor = editor;
}


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
