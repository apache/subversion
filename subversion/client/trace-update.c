/*
 * status.c:  the command-line's portion of the "svn status" command
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
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
  svn_wc_status_t *status;
  svn_string_t *path;
  svn_boolean_t added;
  svn_boolean_t prop_changed;
};


struct file_baton
{
  struct dir_baton *parent_dir_baton;
  svn_wc_status_t *status;
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
delete (svn_string_t *name, void *parent_baton)
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
  struct dir_baton *d = dir_baton;

  if (d->prop_changed)
    printf ("X  %s\n", d->path->data);

  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *fb = file_baton;

  if (fb->added)
    printf ("A  %s\n", fb->path->data);
  else if (fb->text_changed && fb->prop_changed)
    printf ("UX %s\n", fb->path->data);
  else if (fb->text_changed)
    printf ("U  %s\n", fb->path->data);
  else if (fb->prop_changed)
    printf ("X  %s\n", fb->path->data);

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
  delete,
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
svn_cl__get_trace_editor (const svn_delta_edit_fns_t **editor,
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
