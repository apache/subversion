/*
 * svn_tests_editor.c:  a `dummy' editor implementation for testing
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

/* ==================================================================== */



#include <stdio.h>
#include "apr_pools.h"
#include "apr_file_io.h"
#include "svn_types.h"
#include "svn_test.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"


struct edit_baton
{
  svn_string_t *root_path;
  apr_pool_t *pool;
  int indentation;
  svn_stream_t *out_stream;
};


struct dir_baton
{
  int indent_level;
  svn_string_t *path;
  struct edit_baton *edit_baton;
};


struct file_baton
{
  int indent_level;
  svn_string_t *path;
  struct dir_baton *dir_baton;
};


/* Print EB->indentation * LEVEL spaces, followed by STR,
   to EB->out_stream.  */
static svn_error_t *
print (struct edit_baton *eb, int level, svn_string_t *str)
{
  apr_size_t len;
  int i;

  len = 1;
  for (i = 0; i < (eb->indentation * level); i++)
    SVN_ERR (svn_stream_write (eb->out_stream, " ", &len));

  len = str->len;
  SVN_ERR (svn_stream_write (eb->out_stream, str->data, &len));

  return SVN_NO_ERROR;
}


/* A dummy routine designed to consume windows of vcdiff data, (of
   type svn_text_delta_window_handler_t).  This will be called by the
   vcdiff parser everytime it has a window ready to go. */
static svn_error_t *
my_vcdiff_windoweater (svn_txdelta_window_t *window, void *baton)
{
  int i;
  struct file_baton *fb = (struct file_baton *) baton;
  apr_pool_t *pool = fb->dir_baton->edit_baton->pool;
  svn_string_t *str = svn_string_create ("CALLED window_handler\n", pool);

  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level + 1, str));

  if (! window)
    {
      str = svn_string_create ("end\n\n", pool);
      SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level + 1, str));

      return SVN_NO_ERROR;
    }

  /* Delve into the vcdiff window and print the data. */
  for (i = 0; i < window->num_ops; i++)
    {
      switch (window->ops[i].action_code)
        {
        case svn_txdelta_new:
          {
            str = svn_string_createf (pool,
                                      "new text: length %ld\n\n", 
                                      (long int) (window->ops[i].length));
            
            SVN_ERR (print (fb->dir_baton->edit_baton,
                            fb->indent_level + 1,
                            str));
            break;
          }
        case svn_txdelta_source:
          {
            str = svn_string_createf 
              (pool,
               "source text: offset %ld, length %ld\n\n",
               (long int) window->ops[i].offset,
               (long int) window->ops[i].length);

            SVN_ERR (print (fb->dir_baton->edit_baton,
                            fb->indent_level + 1,
                            str));

            break;
          }
        case svn_txdelta_target:
          {
            str = svn_string_createf
              (pool,
               "target text: offset %ld, length %ld\n\n",
               (long int) window->ops[i].offset,
               (long int) window->ops[i].length);
            
            SVN_ERR (print (fb->dir_baton->edit_baton,
                            fb->indent_level + 1,
                            str));

            break;
          }
        default:
          {
            str = svn_string_create ("unknown window type\n\n", pool);
            
            SVN_ERR (print (fb->dir_baton->edit_baton,
                            fb->indent_level + 1,
                            str));
            break;
          }
        }
    }

  return SVN_NO_ERROR;
}



static svn_error_t *
test_delete_entry (svn_string_t *filename, void *parent_baton)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  svn_string_t *str = svn_string_create ("CALLED delete_entry\n",
                                         d->edit_baton->pool);

  SVN_ERR (print (d->edit_baton, d->indent_level, str));

  str = svn_string_createf (d->edit_baton->pool,
                            "name: %s\n\n", filename->data);
  SVN_ERR (print (d->edit_baton, d->indent_level, str));

  return SVN_NO_ERROR;         
}


static svn_error_t *
test_set_target_revision (void *edit_baton,
                          svn_revnum_t target_revision)
{
  struct edit_baton *eb = (struct edit_baton *) edit_baton;

  svn_string_t *str
    = svn_string_create ("CALLED set_target_revision\n", eb->pool);
  SVN_ERR (print (eb, 0, str));

  str = svn_string_createf (eb->pool, "target_revision: %ld\n\n",
                            (long int) target_revision);
  SVN_ERR (print (eb, 0, str));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_replace_root (void *edit_baton,
                   svn_revnum_t base_revision,
                   void **root_baton)
{
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  struct dir_baton *d = apr_pcalloc (eb->pool, sizeof (*d));
  svn_string_t *str;

  d->path = (svn_string_t *) svn_string_dup (eb->root_path, eb->pool);
  d->edit_baton = eb;
  d->indent_level = 1;
  *root_baton = d;

  str = svn_string_create ("CALLED replace_root\n", eb->pool);
  SVN_ERR (print (eb, d->indent_level, str));

  str = svn_string_createf (eb->pool, "path: %s\n", eb->root_path->data);
  SVN_ERR (print (eb, d->indent_level, str));

  str = svn_string_createf (eb->pool, "base_revision: %ld\n\n",
                            (long int) base_revision);
  SVN_ERR (print (eb, d->indent_level, str));

  return SVN_NO_ERROR;
}


static svn_error_t *
add_or_replace_dir (svn_string_t *name,
                    void *parent_baton,
                    svn_string_t *base_path,
                    svn_revnum_t base_revision,
                    void **child_baton,
                    const char *pivot_string)
{
  struct dir_baton *pd = (struct dir_baton *) parent_baton;
  const char *Aname = name ? name->data : "(unknown)";
  struct dir_baton *d;
  svn_string_t *str
    = svn_string_createf (pd->edit_baton->pool,
                          "CALLED %s_directory\n",
                          pivot_string);

  /* Set child_baton to a new dir baton. */
  d = apr_pcalloc (pd->edit_baton->pool, sizeof (*d));
  d->path = svn_string_dup (pd->path, pd->edit_baton->pool);
  svn_path_add_component (d->path,
                          svn_string_create (Aname, pd->edit_baton->pool),
                          svn_path_local_style);
  d->edit_baton = pd->edit_baton;
  d->indent_level = (pd->indent_level + 1);
  *child_baton = d;

  SVN_ERR (print (d->edit_baton, d->indent_level, str));

  str = svn_string_createf (pd->edit_baton->pool,
                            "parent: %s\n", pd->path->data);
  SVN_ERR (print (d->edit_baton, d->indent_level, str));
  
  str = svn_string_createf (pd->edit_baton->pool,
                            "name: %s\n", name->data);
  SVN_ERR (print (d->edit_baton, d->indent_level, str));
  

  if (strcmp (pivot_string, "add") == 0)
    {
      str = svn_string_createf (pd->edit_baton->pool,
                                "copyfrom_path: %s\n",
                                base_path->data);
      SVN_ERR (print (d->edit_baton, d->indent_level, str));

      str = svn_string_createf (pd->edit_baton->pool,
                                "copyfrom_revision: %ld\n\n",
                                (long int) base_revision);
      SVN_ERR (print (d->edit_baton, d->indent_level, str));

    }
  else
    {
      str = svn_string_createf (pd->edit_baton->pool,
                                "base_revision: %ld\n\n",
                                (long int) base_revision);
      SVN_ERR (print (d->edit_baton, d->indent_level, str));
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_add_directory (svn_string_t *name,
                    void *parent_baton,
                    svn_string_t *copyfrom_path,
                    svn_revnum_t copyfrom_revision,
                    void **child_baton)
{
  return add_or_replace_dir (name,
                             parent_baton,
                             copyfrom_path,
                             copyfrom_revision,
                             child_baton,
                             "add");
}


static svn_error_t *
test_replace_directory (svn_string_t *name,
                        void *parent_baton,
                        svn_revnum_t base_revision,
                        void **child_baton)
{
  return add_or_replace_dir (name,
                             parent_baton,
                             NULL,
                             base_revision,
                             child_baton,
                             "replace");
}


static svn_error_t *
test_close_directory (void *dir_baton)
{
  struct dir_baton *d = (struct dir_baton *) dir_baton;

  svn_string_t *str = svn_string_create ("CALLED close_directory\n",
                                         d->edit_baton->pool);
  SVN_ERR (print (d->edit_baton, d->indent_level, str));

  str = svn_string_createf (d->edit_baton->pool,
                            "path: %s\n\n", d->path->data);
  SVN_ERR (print (d->edit_baton, d->indent_level, str));

  return SVN_NO_ERROR;    
}


static svn_error_t *
test_close_file (void *file_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;

  svn_string_t *str = svn_string_create ("CALLED close_file\n",
                                         fb->dir_baton->edit_baton->pool);
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

  str = svn_string_createf (fb->dir_baton->edit_baton->pool,
                            "path: %s\n\n", fb->path->data);
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

  return SVN_NO_ERROR;    
}


static svn_error_t *
test_close_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;

  svn_string_t *str = svn_string_create ("CALLED close_edit\n\n", eb->pool);
  SVN_ERR (print (eb, 0, str));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_apply_textdelta (void *file_baton,
                      svn_txdelta_window_handler_t *handler,
                      void **handler_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;

  svn_string_t *str = svn_string_create ("CALLED apply_textdelta\n",
                                         fb->dir_baton->edit_baton->pool);
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

  str = svn_string_createf (fb->dir_baton->edit_baton->pool,
                            "path: %s\n\n", fb->path->data);
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

  /* Set the value of HANDLER and HANDLER_BATON here */
  *handler        = my_vcdiff_windoweater;
  *handler_baton  = fb;

  return SVN_NO_ERROR;
}


static svn_error_t *
add_or_replace_file (svn_string_t *name,
                     void *parent_baton,
                     svn_string_t *base_path,
                     svn_revnum_t base_revision,
                     void **file_baton,
                     const char *pivot_string)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  struct file_baton *fb;

  svn_string_t *str
    = svn_string_createf (d->edit_baton->pool,
                          "CALLED %s_file\n", pivot_string);

  /* Put the filename in file_baton */
  fb = apr_pcalloc (d->edit_baton->pool, sizeof (*fb));
  fb->dir_baton = d;
  fb->path = (svn_string_t *) svn_string_dup (name, d->edit_baton->pool);
  fb->indent_level = (d->indent_level + 1);
  *file_baton = fb;

  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

  str = svn_string_createf (d->edit_baton->pool,
                            "parent: %s\n", d->path->data);
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

  str = svn_string_createf (d->edit_baton->pool,
                            "name: %s\n", name->data);
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

  if (strcmp (pivot_string, "add") == 0)
    {
      str = svn_string_createf (d->edit_baton->pool,
                                "copyfrom_path: %s\n", base_path->data);
      SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

      str = svn_string_createf (d->edit_baton->pool,
                                "copyfrom_revision: %ld\n\n",
                                (long int) base_revision);
      SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));
    }
  else
    {
      str = svn_string_createf (d->edit_baton->pool,
                                "base_revision: %ld\n\n",
                                (long int) base_revision);
      SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_add_file (svn_string_t *name,
               void *parent_baton,
               svn_string_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **file_baton)
{
  return add_or_replace_file (name,
                              parent_baton,
                              copyfrom_path,
                              copyfrom_revision,
                              file_baton,
                              "add");
}


static svn_error_t *
test_replace_file (svn_string_t *name,
                   void *parent_baton,
                   svn_revnum_t base_revision,
                   void **file_baton)
{
  return add_or_replace_file (name,
                              parent_baton,
                              NULL,
                              base_revision,
                              file_baton,
                              "replace");
}


static svn_error_t *
test_change_file_prop (void *file_baton,
                       svn_string_t *name, svn_string_t *value)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  svn_string_t *str
    = svn_string_create ("CALLED change_file_prop\n",
                         fb->dir_baton->edit_baton->pool);

  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

  str = svn_string_createf (fb->dir_baton->edit_baton->pool,
                            "path: %s\n", fb->path->data);
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

  str = svn_string_createf (fb->dir_baton->edit_baton->pool,
                            "name: %s\n", name->data);
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

  str = svn_string_createf (fb->dir_baton->edit_baton->pool,
                            "value: %s\n\n", value ? value->data : "(null)");
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_change_dir_prop (void *parent_baton,
                      svn_string_t *name, svn_string_t *value)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  svn_string_t *str
    = svn_string_create ("CALLED change_dir_prop\n", d->edit_baton->pool);

  SVN_ERR (print (d->edit_baton, d->indent_level, str));

  str = svn_string_createf (d->edit_baton->pool,
                            "path: %s\n", d->path->data);
  SVN_ERR (print (d->edit_baton, d->indent_level, str));

  str = svn_string_createf (d->edit_baton->pool,
                            "name: %s\n", name->data);
  SVN_ERR (print (d->edit_baton, d->indent_level, str));

  str = svn_string_createf (d->edit_baton->pool,
                            "value: %s\n\n", value ? value->data : "(null)");
  SVN_ERR (print (d->edit_baton, d->indent_level, str));

  return SVN_NO_ERROR;
}


/*---------------------------------------------------------------*/

/** Public interface:  svn_test_get_editor() **/


svn_error_t *
svn_test_get_editor (const svn_delta_edit_fns_t **editor,
                     void **edit_baton,
                     svn_stream_t *out_stream,
                     int indentation,
                     svn_string_t *path,
                     apr_pool_t *pool)
{
  svn_delta_edit_fns_t *my_editor;
  struct edit_baton *my_edit_baton;

  /* Set up the editor. */
  my_editor = svn_delta_default_editor (pool);
  my_editor->set_target_revision = test_set_target_revision;
  my_editor->replace_root        = test_replace_root;
  my_editor->delete_entry        = test_delete_entry;
  my_editor->add_directory       = test_add_directory;
  my_editor->replace_directory   = test_replace_directory;
  my_editor->close_directory     = test_close_directory;
  my_editor->add_file            = test_add_file;
  my_editor->replace_file        = test_replace_file;
  my_editor->close_file          = test_close_file;
  my_editor->apply_textdelta     = test_apply_textdelta;
  my_editor->change_file_prop    = test_change_file_prop;
  my_editor->change_dir_prop     = test_change_dir_prop;
  my_editor->close_edit          = test_close_edit;

  /* Set up the edit baton. */
  my_edit_baton = apr_pcalloc (pool, sizeof (*my_edit_baton));
  my_edit_baton->root_path = svn_string_dup (path, pool);
  my_edit_baton->pool = pool;
  my_edit_baton->indentation = indentation;
  my_edit_baton->out_stream = out_stream;

  *editor = my_editor;
  *edit_baton = my_edit_baton;

  return SVN_NO_ERROR;
}

