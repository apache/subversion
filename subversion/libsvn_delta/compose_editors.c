/* 
 * compose_editors.c -- composing two svn_delta_edit_fns_t's
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
 * software developed by CollabNet (http://www.Collab.Net/)."
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
replace_root (void *edit_baton, void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  svn_error_t *err;
  struct dir_baton *d = apr_pcalloc (eb->pool, sizeof (*d));

  d->edit_baton = eb;
  d->parent_dir_baton = NULL;

  if (eb->editor_1->replace_root)
    {
      err = (* (eb->editor_1->replace_root)) (eb->edit_baton_1,
                                              &(d->dir_baton_1));
      if (err)
        return err;
    }
  
  if (eb->editor_2->replace_root)
    {
      err = (* (eb->editor_2->replace_root)) (eb->edit_baton_2,
                                              &(d->dir_baton_2));
      if (err)
        return err;
    }
  
  *root_baton = d;
  
  return SVN_NO_ERROR;
}


static svn_error_t *
delete (svn_string_t *name, void *parent_baton)
{
  struct dir_baton *d = parent_baton;
  svn_error_t *err;

  if (d->edit_baton->editor_1->delete)
    {
      err = (* (d->edit_baton->editor_1->delete)) (name, d->dir_baton_1);
      if (err)
        return err;
    }
  
  if (d->edit_baton->editor_2->delete)
    {
      err = (* (d->edit_baton->editor_2->delete)) (name, d->dir_baton_2);
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
  struct dir_baton *child = apr_pcalloc (d->edit_baton->pool, sizeof (*child));

  child->edit_baton = d->edit_baton;
  child->parent_dir_baton = NULL;

  if (d->edit_baton->editor_1->add_directory)
    {
      err = (* (d->edit_baton->editor_1->add_directory))
        (name, d->dir_baton_1, ancestor_path, ancestor_revision,
         &(child->dir_baton_1));
      if (err)
        return err;
    }

  if (d->edit_baton->editor_2->add_directory)
    {
      err = (* (d->edit_baton->editor_2->add_directory))
        (name, d->dir_baton_2, ancestor_path, ancestor_revision,
         &(child->dir_baton_2));
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
  struct dir_baton *child = apr_pcalloc (d->edit_baton->pool, sizeof (*child));

  child->edit_baton = d->edit_baton;
  child->parent_dir_baton = NULL;

  if (d->edit_baton->editor_1->replace_directory)
    {
      err = (* (d->edit_baton->editor_1->replace_directory))
        (name, d->dir_baton_1, ancestor_path, ancestor_revision,
         &(child->dir_baton_1));
      if (err)
        return err;
    }

  if (d->edit_baton->editor_2->replace_directory)
    {
      err = (* (d->edit_baton->editor_2->replace_directory))
        (name, d->dir_baton_2, ancestor_path, ancestor_revision,
         &(child->dir_baton_2));
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

  if (d->edit_baton->editor_1->close_directory)
    {
      err = (* (d->edit_baton->editor_1->close_directory)) (d->dir_baton_1);
      if (err)
        return err;
    }
  
  if (d->edit_baton->editor_2->close_directory)
    {
      err = (* (d->edit_baton->editor_2->close_directory)) (d->dir_baton_2);
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

  if (fb->dir_baton->edit_baton->editor_1->close_file)
    {
      err = (* (fb->dir_baton->edit_baton->editor_1->close_file))
        (fb->file_baton_1);
      if (err)
        return err;
    }
  
  if (fb->dir_baton->edit_baton->editor_2->close_file)
    {
      err = (* (fb->dir_baton->edit_baton->editor_2->close_file))
        (fb->file_baton_2);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;
  svn_error_t *err;

  if (eb->editor_1->close_edit)
    {
      err = (* (eb->editor_1->close_edit)) (eb->edit_baton_1);
      if (err)
        return err;
    }
  
  if (eb->editor_2->close_edit)
    {
      err = (* (eb->editor_2->close_edit)) (eb->edit_baton_2);
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
    = apr_pcalloc (fb->dir_baton->edit_baton->pool, sizeof (*hp));
  
  hp->file_baton = fb;

  if (fb->dir_baton->edit_baton->editor_1->apply_textdelta)
    {
      err = (* (fb->dir_baton->edit_baton->editor_1->apply_textdelta))
        (fb->file_baton_1, &(hp->handler_1), &(hp->handler_baton_1));
      if (err)
        return err;
    }

  if (fb->dir_baton->edit_baton->editor_2->apply_textdelta)
    {
      err = (* (fb->dir_baton->edit_baton->editor_2->apply_textdelta))
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
  struct file_baton *fb = apr_pcalloc (d->edit_baton->pool, sizeof (*fb));

  fb->dir_baton = d;

  if (d->edit_baton->editor_1->add_file)
    {
      err = (* (d->edit_baton->editor_1->add_file))
        (name, d->dir_baton_1, ancestor_path, ancestor_revision,
         &(fb->file_baton_1));
      if (err)
        return err;
    }

  if (d->edit_baton->editor_2->add_file)
    {
      err = (* (d->edit_baton->editor_2->add_file))
        (name, d->dir_baton_2, ancestor_path, ancestor_revision,
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
  struct file_baton *fb = apr_pcalloc (d->edit_baton->pool, sizeof (*fb));

  fb->dir_baton = d;

  if (d->edit_baton->editor_1->replace_file)
    {
      err = (* (d->edit_baton->editor_1->replace_file))
        (name, d->dir_baton_1, ancestor_path, ancestor_revision,
         &(fb->file_baton_1));
      if (err)
        return err;
    }

  if (d->edit_baton->editor_2->replace_file)
    {
      err = (* (d->edit_baton->editor_2->replace_file))
        (name, d->dir_baton_2, ancestor_path, ancestor_revision,
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

  if (fb->dir_baton->edit_baton->editor_1->change_file_prop)
    {
      err = (* (fb->dir_baton->edit_baton->editor_1->change_file_prop))
        (fb->file_baton_1, name, value);
      if (err)
        return err;
    }

  if (fb->dir_baton->edit_baton->editor_2->change_file_prop)
    {
      err = (* (fb->dir_baton->edit_baton->editor_2->change_file_prop))
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

  if (d->edit_baton->editor_1->change_dir_prop)
    {
      err = (* (d->edit_baton->editor_1->change_dir_prop))
        (d->dir_baton_1, name, value);
      if (err)
        return err;
    }

  if (d->edit_baton->editor_2->change_dir_prop)
    {
      err = (* (d->edit_baton->editor_2->change_dir_prop))
        (d->dir_baton_2, name, value);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}




static const svn_delta_edit_fns_t composed_editor =
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


void
svn_delta_compose_editors (const svn_delta_edit_fns_t **new_editor,
                           void **new_edit_baton,
                           const svn_delta_edit_fns_t *editor_1,
                           void *edit_baton_1,
                           const svn_delta_edit_fns_t *editor_2,
                           void *edit_baton_2,
                           apr_pool_t *pool)
{
  struct edit_baton *eb = apr_pcalloc (pool, sizeof (*eb));
  
  eb->editor_1 = editor_1;
  eb->editor_2 = editor_2;
  eb->edit_baton_1 = edit_baton_1;
  eb->edit_baton_2 = edit_baton_2;
  eb->pool = pool;

  *new_edit_baton = eb;
  *new_editor = &composed_editor;
}


void
svn_delta_wrap_editor (const svn_delta_edit_fns_t **new_editor,
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
 * eval: (load-file "../svn-dev.el")
 * end:
 */
