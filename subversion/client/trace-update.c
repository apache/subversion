/*
 * trace-update.c : an editor implementation that prints status characters
 *                  (when composed to follow after the update-editor)
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

/* ==================================================================== */



/*** Includes. ***/
#include "svn_wc.h"
#include "svn_path.h"
#include "svn_string.h"
#include "cl.h"



struct edit_baton
{
  apr_pool_t *pool;
  svn_string_t *initial_path;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent_dir_baton;
  svn_string_t *path;
  svn_boolean_t added;
  svn_boolean_t prop_changed;
};


struct file_baton
{
  struct dir_baton *parent_dir_baton;
  svn_string_t *path;
  svn_boolean_t added;
  svn_boolean_t text_changed;
  svn_boolean_t prop_changed;
};


static svn_error_t *
replace_root (void *edit_baton, void **root_baton)
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
delete_item (svn_string_t *name, void *parent_baton)
{
  struct dir_baton *d = parent_baton;

  svn_string_t *printable_name = svn_string_dup (d->path, d->edit_baton->pool);
  svn_path_add_component (printable_name, name, svn_path_local_style);

  printf ("D  %s\n", printable_name->data);
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

  printf ("A  %s\n", child_d->path->data);
  *child_baton = child_d;

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_directory (svn_string_t *name,
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

  *child_baton = child_d;

  /* Don't print anything for a directory replace -- this event is
     implied by printing events beneath it. */

  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
  svn_error_t *err;
  struct dir_baton *d = dir_baton;
  char statchar_buf[3] = "_ ";

  if (d->prop_changed)
    {
      /* First, check for conflicted state. */
      svn_wc_entry_t *entry;
      svn_boolean_t merged, text_conflict, prop_conflict;
      
      err = svn_wc_entry (&entry,
                          d->path,
                          d->edit_baton->pool);
      if (err) return err;
      
      err = svn_wc_conflicted_p (&text_conflict, &prop_conflict,
                                 d->path,
                                 entry,
                                 d->edit_baton->pool);
      if (err) return err;
      
      if (! prop_conflict)
        {
          err = svn_wc_props_modified_p 
            (&merged, d->path, d->edit_baton->pool);
          if (err) return err;
        }
      
      if (prop_conflict)
        statchar_buf[1] = 'C';
      else if (merged)
        statchar_buf[1] = 'G';
      else
        statchar_buf[1] = 'U';

      printf ("%s %s\n", statchar_buf, d->path->data);
    }
    
  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
  svn_error_t *err;
  struct file_baton *fb = file_baton;
  char statchar_buf[3] = "_ ";

  if (fb->added)
    {
      statchar_buf[0] = 'A';
    }
  else
    {
      /* First, check for conflicted state. */
      svn_wc_entry_t *entry;
      svn_boolean_t merged, text_conflict, prop_conflict;

      err = svn_wc_entry (&entry,
                          fb->path,
                          fb->parent_dir_baton->edit_baton->pool);
      if (err) return err;

      err = svn_wc_conflicted_p (&text_conflict, &prop_conflict,
                                 fb->parent_dir_baton->path,
                                 entry,
                                 fb->parent_dir_baton->edit_baton->pool);
      if (err) return err;

      if (fb->text_changed)
        {
          if (! text_conflict)
            {
              err = svn_wc_text_modified_p 
                (&merged, fb->path, fb->parent_dir_baton->edit_baton->pool);
              if (err) return err;
            }

          if (text_conflict)
            statchar_buf[0] = 'C';
          else if (merged)
            statchar_buf[0] = 'G';
          else
            statchar_buf[0] = 'U';
        }
      if (fb->prop_changed)
        {
          if (! prop_conflict)
            {
              err = svn_wc_props_modified_p 
                (&merged, fb->path, fb->parent_dir_baton->edit_baton->pool);
              if (err) return err;
            }
          
          if (prop_conflict)
            statchar_buf[1] = 'C';
          else if (merged)
            statchar_buf[1] = 'G';
          else
            statchar_buf[1] = 'U';
        }
    }

  printf ("%s %s\n", statchar_buf, fb->path->data);

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *handler_pair)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t **handler,
                 void **handler_baton)
{
  struct file_baton *fb = file_baton;
  fb->text_changed = TRUE;
  *handler = window_handler;
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

  *file_baton = child_fb;

  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_string_t *name,
                  svn_string_t *value)
{
  struct file_baton *fb = file_baton;
  fb->prop_changed = TRUE;
  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *parent_baton,
                 svn_string_t *name,
                 svn_string_t *value)
{
  struct dir_baton *d = parent_baton;
  d->prop_changed = TRUE;
  return SVN_NO_ERROR;
}




static const svn_delta_edit_fns_t trace_editor =
{
  replace_root,
  delete_item,
  add_directory,
  replace_directory,
  change_dir_prop,
  close_directory,
  add_file,
  replace_file,
  apply_textdelta,
  change_file_prop,
  close_file,
  close_edit
};


svn_error_t *
svn_cl__get_trace_update_editor (const svn_delta_edit_fns_t **editor,
                                 void **edit_baton,
                                 svn_string_t *initial_path,
                                 apr_pool_t *pool)
{
  struct edit_baton *eb = apr_pcalloc (pool, sizeof (*eb));
  eb->initial_path = initial_path;
  eb->pool = pool;

  *edit_baton = eb;
  *editor = &trace_editor;
  
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
