/*
 * svn_tests_editor.c:  a `dummy' editor implementation for testing
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

/* ==================================================================== */



#include <stdio.h>
#include <string.h>
#include "apr_pools.h"
#include "apr_file_io.h"
#include "svn_types.h"
#include "svn_test.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"


struct edit_baton
{
  svn_stringbuf_t *root_path;
  svn_stringbuf_t *editor_name;
  svn_stream_t *out_stream;
  apr_pool_t *pool;
  int indentation;
  svn_boolean_t verbose;
  svn_stringbuf_t *newline;
};


struct dir_baton
{
  int indent_level;
  svn_stringbuf_t *path;
  struct edit_baton *edit_baton;
};


struct file_baton
{
  int indent_level;
  svn_stringbuf_t *path;
  struct dir_baton *dir_baton;
};


/* Initialize EB->newline if it hasn't already been initialized, then
   print EB->newline to EB->outstream.  */
static svn_error_t *
newline (struct edit_baton *eb)
{
  apr_size_t len;

  if (! eb->newline)
    eb->newline = svn_stringbuf_create ("\n", eb->pool);

  len = eb->newline->len;
  return svn_stream_write (eb->out_stream, eb->newline->data, &len);
}

 
/* Print EB->indentation * LEVEL spaces, followed by STR,
   to EB->out_stream.  */
static svn_error_t *
print (struct edit_baton *eb, int level, svn_stringbuf_t *str)
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
  svn_stringbuf_t *str;

  /* We're done if non-verbose */
  if (! fb->dir_baton->edit_baton->verbose)
    return SVN_NO_ERROR;

  if (window)
    str = svn_stringbuf_createf (pool,
                              "[%s] window_handler (%d ops)\n",
                              fb->dir_baton->edit_baton->editor_name->data,
                              window->num_ops);
  else
    str = svn_stringbuf_createf (pool,
                              "[%s] window_handler (EOT)\n",
                              fb->dir_baton->edit_baton->editor_name->data);
    
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level + 2, str));

  if (window)
    {
      /* Delve into the vcdiff window and print the data. */
      for (i = 1; i <= window->num_ops; i++)
        {
          switch (window->ops[i].action_code)
            {
            case svn_txdelta_new:
              {
                str = svn_stringbuf_createf 
                  (pool,
                   "(%d) new text: length %" APR_SIZE_T_FMT "\n",
                   i, (window->ops[i].length));
            
                SVN_ERR (print (fb->dir_baton->edit_baton,
                                fb->indent_level + 2,
                                str));
                break;
              }
            case svn_txdelta_source:
              {
                str = svn_stringbuf_createf 
                  (pool,
                   "(%d) source text: offset %" APR_SIZE_T_FMT
                   ", length %" APR_SIZE_T_FMT "\n",
                   i, window->ops[i].offset, window->ops[i].length);

                SVN_ERR (print (fb->dir_baton->edit_baton,
                                fb->indent_level + 2,
                                str));

                break;
              }
            case svn_txdelta_target:
              {
                str = svn_stringbuf_createf
                  (pool,
                   "(%d) target text: offset %" APR_SIZE_T_FMT
                   ", length %" APR_SIZE_T_FMT "\n",
                   i,  window->ops[i].offset, window->ops[i].length);
            
                SVN_ERR (print (fb->dir_baton->edit_baton,
                                fb->indent_level + 2,
                                str));

                break;
              }
            default:
              {
                str = svn_stringbuf_createf
                  (pool, "(%d) unknown window type\n", i);
            
                SVN_ERR (print (fb->dir_baton->edit_baton,
                                fb->indent_level + 2,
                                str));
                break;
              }
            }
        }
    }

  SVN_ERR (newline (fb->dir_baton->edit_baton));

  return SVN_NO_ERROR;
}



static svn_error_t *
test_delete_entry (svn_stringbuf_t *filename, 
                   svn_revnum_t revision,
                   void *parent_baton)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  svn_stringbuf_t *path;
  svn_stringbuf_t *str;

  path = svn_stringbuf_dup (d->path, d->edit_baton->pool);
  svn_path_add_component (path, filename);
  str = svn_stringbuf_createf (d->edit_baton->pool,
                            "[%s] delete_entry (%s)\n",
                            d->edit_baton->editor_name->data,
                            path->data);
  SVN_ERR (print (d->edit_baton, d->indent_level + 1, str));

  if (d->edit_baton->verbose)
    SVN_ERR (newline (d->edit_baton));

  return SVN_NO_ERROR;         
}


static svn_error_t *
test_set_target_revision (void *edit_baton,
                          svn_revnum_t target_revision)
{
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  svn_stringbuf_t *str;

  str = svn_stringbuf_createf (eb->pool,
                               "[%s] set_target_revision (%"
                               SVN_REVNUM_T_FMT
                               ")\n",
                               eb->editor_name->data,
                               target_revision);
  SVN_ERR (print (eb, 0, str));

  if (eb->verbose)
    SVN_ERR (newline (eb));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_open_root (void *edit_baton,
                svn_revnum_t base_revision,
                void **root_baton)
{
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  struct dir_baton *d = apr_pcalloc (eb->pool, sizeof (*d));
  svn_stringbuf_t *str;

  d->path = (svn_stringbuf_t *) svn_stringbuf_dup (eb->root_path, eb->pool);
  d->edit_baton = eb;
  d->indent_level = 0;
  *root_baton = d;

  str = svn_stringbuf_createf (eb->pool,
                            "[%s] open_root (%s)\n",
                            eb->editor_name->data,
                            eb->root_path->data);
  SVN_ERR (print (eb, d->indent_level, str));

  /* We're done if non-verbose */
  if (! eb->verbose)
    return SVN_NO_ERROR;

  str = svn_stringbuf_createf (eb->pool, 
                            "base_revision: %" SVN_REVNUM_T_FMT "\n",
                            base_revision);
  SVN_ERR (print (eb, d->indent_level, str));
  SVN_ERR (newline (eb));

  return SVN_NO_ERROR;
}


static svn_error_t *
add_or_open_dir (svn_stringbuf_t *name,
                 void *parent_baton,
                 svn_stringbuf_t *base_path,
                 svn_revnum_t base_revision,
                 void **child_baton,
                 const char *pivot_string)
{
  struct dir_baton *pd = (struct dir_baton *) parent_baton;
  struct dir_baton *d = apr_pcalloc (pd->edit_baton->pool, sizeof (*d));
  svn_stringbuf_t *str;

  /* Set child_baton to a new dir baton. */
  d->path = svn_stringbuf_dup (pd->path, pd->edit_baton->pool);
  svn_path_add_component (d->path, name);
  d->edit_baton = pd->edit_baton;
  d->indent_level = (pd->indent_level + 1);
  *child_baton = d;

  str = svn_stringbuf_createf (pd->edit_baton->pool,
                            "[%s] %s_directory (%s)\n",
                            pd->edit_baton->editor_name->data,
                            pivot_string,
                            d->path->data);

  SVN_ERR (print (d->edit_baton, d->indent_level, str));

  /* We're done if non-verbose */
  if (! pd->edit_baton->verbose)
    return SVN_NO_ERROR;

  str = svn_stringbuf_createf (pd->edit_baton->pool,
                            "parent: %s\n", pd->path->data);
  SVN_ERR (print (d->edit_baton, d->indent_level, str));

  if (strcmp (pivot_string, "add") == 0)
    {
      str = svn_stringbuf_createf (pd->edit_baton->pool,
                                "copyfrom_path: %s\n",
                                base_path ? base_path->data : "");
      SVN_ERR (print (d->edit_baton, d->indent_level, str));

      str = svn_stringbuf_createf (pd->edit_baton->pool,
                                "copyfrom_revision: %" SVN_REVNUM_T_FMT "\n",
                                base_revision);
      SVN_ERR (print (d->edit_baton, d->indent_level, str));

    }
  else
    {
      str = svn_stringbuf_createf (pd->edit_baton->pool,
                                "base_revision: %" SVN_REVNUM_T_FMT "\n",
                                base_revision);
      SVN_ERR (print (d->edit_baton, d->indent_level, str));
    }

  SVN_ERR (newline (d->edit_baton));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_add_directory (svn_stringbuf_t *name,
                    void *parent_baton,
                    svn_stringbuf_t *copyfrom_path,
                    svn_revnum_t copyfrom_revision,
                    void **child_baton)
{
  return add_or_open_dir (name,
                          parent_baton,
                          copyfrom_path,
                          copyfrom_revision,
                          child_baton,
                          "add");
}


static svn_error_t *
test_open_directory (svn_stringbuf_t *name,
                     void *parent_baton,
                     svn_revnum_t base_revision,
                     void **child_baton)
{
  return add_or_open_dir (name,
                          parent_baton,
                          NULL,
                          base_revision,
                          child_baton,
                          "open");
}


static svn_error_t *
test_close_directory (void *dir_baton)
{
  struct dir_baton *d = (struct dir_baton *) dir_baton;
  svn_stringbuf_t *str;

  str = svn_stringbuf_createf (d->edit_baton->pool,
                            "[%s] close_directory (%s)\n",
                            d->edit_baton->editor_name->data,
                            d->path->data);
  SVN_ERR (print (d->edit_baton, d->indent_level, str));

  if (d->edit_baton->verbose)
    SVN_ERR (newline (d->edit_baton));

  return SVN_NO_ERROR;    
}


static svn_error_t *
test_close_file (void *file_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  svn_stringbuf_t *str;

  str = svn_stringbuf_createf (fb->dir_baton->edit_baton->pool,
                            "[%s] close_file (%s)\n",
                            fb->dir_baton->edit_baton->editor_name->data,
                            fb->path->data);
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

  if (fb->dir_baton->edit_baton->verbose)
    SVN_ERR (newline (fb->dir_baton->edit_baton));

  return SVN_NO_ERROR;    
}


static svn_error_t *
test_close_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;
  svn_stringbuf_t *str;

  str = svn_stringbuf_createf (eb->pool,
                            "[%s] close_edit\n",
                            eb->editor_name->data);
  SVN_ERR (print (eb, 0, str));

  if (eb->verbose)
    SVN_ERR (newline (eb));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_apply_textdelta (void *file_baton,
                      svn_txdelta_window_handler_t *handler,
                      void **handler_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  svn_stringbuf_t *str;

  /* Set the value of HANDLER and HANDLER_BATON here */
  *handler        = my_vcdiff_windoweater;
  *handler_baton  = fb;

  str = svn_stringbuf_createf (fb->dir_baton->edit_baton->pool,
                            "[%s] apply_textdelta (%s)\n",
                            fb->dir_baton->edit_baton->editor_name->data,
                            fb->path->data);
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level + 1, str));

  if (fb->dir_baton->edit_baton->verbose)
    SVN_ERR (newline (fb->dir_baton->edit_baton));

  return SVN_NO_ERROR;
}


static svn_error_t *
add_or_open_file (svn_stringbuf_t *name,
                  void *parent_baton,
                  svn_stringbuf_t *base_path,
                  svn_revnum_t base_revision,
                  void **file_baton,
                  const char *pivot_string)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  struct file_baton *fb = apr_pcalloc (d->edit_baton->pool, sizeof (*fb));
  svn_stringbuf_t *str;

  /* Put the filename in file_baton */
  fb->dir_baton = d;
  fb->path = (svn_stringbuf_t *) svn_stringbuf_dup (d->path,
                                                    d->edit_baton->pool);
  svn_path_add_component (fb->path, name);
  fb->indent_level = (d->indent_level + 1);
  *file_baton = fb;
 
  str = svn_stringbuf_createf (d->edit_baton->pool,
                            "[%s] %s_file (%s)\n", 
                            d->edit_baton->editor_name->data,
                            pivot_string,
                            fb->path->data);

  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

  /* We're done if non-verbose */
  if (! d->edit_baton->verbose)
    return SVN_NO_ERROR;

  str = svn_stringbuf_createf (d->edit_baton->pool,
                            "parent: %s\n", d->path->data);
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

  if (strcmp (pivot_string, "add") == 0)
    {
      str = svn_stringbuf_createf (d->edit_baton->pool,
                                "copyfrom_path: %s\n", 
                                base_path ? base_path->data : "");
      SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));

      str = svn_stringbuf_createf (d->edit_baton->pool,
                                "copyfrom_revision: %" SVN_REVNUM_T_FMT "\n",
                                base_revision);
      SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));
    }
  else
    {
      str = svn_stringbuf_createf (d->edit_baton->pool,
                                "base_revision: %" SVN_REVNUM_T_FMT "\n",
                                base_revision);
      SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level, str));
    }

  SVN_ERR (newline (fb->dir_baton->edit_baton));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_add_file (svn_stringbuf_t *name,
               void *parent_baton,
               svn_stringbuf_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **file_baton)
{
  return add_or_open_file (name,
                           parent_baton,
                           copyfrom_path,
                           copyfrom_revision,
                           file_baton,
                           "add");
}


static svn_error_t *
test_open_file (svn_stringbuf_t *name,
                void *parent_baton,
                svn_revnum_t base_revision,
                void **file_baton)
{
  return add_or_open_file (name,
                           parent_baton,
                           NULL,
                           base_revision,
                           file_baton,
                           "open");
}


static svn_error_t *
test_change_file_prop (void *file_baton,
                       svn_stringbuf_t *name, svn_stringbuf_t *value)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  svn_stringbuf_t *str;

  str = svn_stringbuf_createf (fb->dir_baton->edit_baton->pool,
                            "[%s] change_file_prop (%s)\n",
                            fb->dir_baton->edit_baton->editor_name->data,
                            fb->path->data);
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level + 1, str));

  /* We're done if non-verbose */
  if (! fb->dir_baton->edit_baton->verbose)
    return SVN_NO_ERROR;

  str = svn_stringbuf_createf (fb->dir_baton->edit_baton->pool,
                            "name: %s\n", 
                            name->data);
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level + 1, str));

  str = svn_stringbuf_createf (fb->dir_baton->edit_baton->pool,
                            "value: %s\n", 
                            value ? value->data : "(null)");
  SVN_ERR (print (fb->dir_baton->edit_baton, fb->indent_level + 1, str));

  SVN_ERR (newline (fb->dir_baton->edit_baton));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_change_dir_prop (void *parent_baton,
                      svn_stringbuf_t *name, svn_stringbuf_t *value)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  svn_stringbuf_t *str;

  str = svn_stringbuf_createf (d->edit_baton->pool,
                            "[%s] change_dir_prop (%s)\n",
                            d->edit_baton->editor_name->data,
                            d->path->data);
  SVN_ERR (print (d->edit_baton, d->indent_level + 1, str));

  /* We're done if non-verbose */
  if (! d->edit_baton->verbose)
    return SVN_NO_ERROR;

  str = svn_stringbuf_createf (d->edit_baton->pool,
                            "name: %s\n", 
                            name->data);
  SVN_ERR (print (d->edit_baton, d->indent_level + 1, str));

  str = svn_stringbuf_createf (d->edit_baton->pool,
                            "value: %s\n", 
                            value ? value->data : "(null)");
  SVN_ERR (print (d->edit_baton, d->indent_level + 1, str));

  SVN_ERR (newline (d->edit_baton));

  return SVN_NO_ERROR;
}


/*---------------------------------------------------------------*/

/** Public interface:  svn_test_get_editor() **/


svn_error_t *
svn_test_get_editor (const svn_delta_edit_fns_t **editor,
                     void **edit_baton,
                     svn_stringbuf_t *editor_name,
                     svn_stream_t *out_stream,
                     int indentation,
                     svn_boolean_t verbose,
                     svn_stringbuf_t *path,
                     apr_pool_t *pool)
{
  svn_delta_edit_fns_t *my_editor;
  struct edit_baton *my_edit_baton;

  /* Set up the editor. */
  my_editor = svn_delta_old_default_editor (pool);
  my_editor->set_target_revision = test_set_target_revision;
  my_editor->open_root           = test_open_root;
  my_editor->delete_entry        = test_delete_entry;
  my_editor->add_directory       = test_add_directory;
  my_editor->open_directory      = test_open_directory;
  my_editor->close_directory     = test_close_directory;
  my_editor->add_file            = test_add_file;
  my_editor->open_file           = test_open_file;
  my_editor->close_file          = test_close_file;
  my_editor->apply_textdelta     = test_apply_textdelta;
  my_editor->change_file_prop    = test_change_file_prop;
  my_editor->change_dir_prop     = test_change_dir_prop;
  my_editor->close_edit          = test_close_edit;

  /* Set up the edit baton. */
  my_edit_baton = apr_pcalloc (pool, sizeof (*my_edit_baton));
  my_edit_baton->root_path = svn_stringbuf_dup (path, pool);
  my_edit_baton->editor_name = svn_stringbuf_dup (editor_name, pool);
  my_edit_baton->pool = pool;
  my_edit_baton->indentation = indentation;
  my_edit_baton->verbose = verbose;
  my_edit_baton->out_stream = out_stream;
  my_edit_baton->newline = NULL; /* allocated when first needed */

  *editor = my_editor;
  *edit_baton = my_edit_baton;

  return SVN_NO_ERROR;
}

