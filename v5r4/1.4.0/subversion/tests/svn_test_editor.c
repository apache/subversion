/*
 * svn_tests_editor.c:  a `dummy' editor implementation for testing
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include <apr_pools.h>
#include <apr_file_io.h>

#include "svn_types.h"
#include "svn_test.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"


struct edit_baton
{
  const char *root_path;
  const char *editor_name;
  svn_stream_t *out_stream;
  apr_pool_t *pool;
  int indentation;
  svn_boolean_t verbose;
};


struct node_baton
{
  struct edit_baton *edit_baton;
  struct node_baton *parent_baton;
  int indent_level;
  const char *path;
};


/* Print newline character to EB->outstream.  */
static svn_error_t *
newline(struct edit_baton *eb)
{
  apr_size_t len = 1;
  return svn_stream_write(eb->out_stream, "\n", &len);
}

 
/* Print EB->indentation * LEVEL spaces, followed by STR,
   to EB->out_stream.  */
static svn_error_t *
print(struct edit_baton *eb, int level, svn_stringbuf_t *str)
{
  apr_size_t len;
  int i;

  len = 1;
  for (i = 0; i < (eb->indentation * level); i++)
    SVN_ERR(svn_stream_write(eb->out_stream, " ", &len));

  len = str->len;
  SVN_ERR(svn_stream_write(eb->out_stream, str->data, &len));

  return SVN_NO_ERROR;
}


/* A dummy routine designed to consume windows of vcdiff data, (of
   type svn_text_delta_window_handler_t).  This will be called by the
   vcdiff parser everytime it has a window ready to go. */
static svn_error_t *
my_vcdiff_windoweater(svn_txdelta_window_t *window, void *baton)
{
  int i;
  struct node_baton *nb = baton;
  struct edit_baton *eb = nb->edit_baton;
  apr_pool_t *pool = eb->pool;
  svn_stringbuf_t *str;

  /* We're done if non-verbose */
  if (! eb->verbose)
    return SVN_NO_ERROR;

  if (window)
    str = svn_stringbuf_createf(pool,
                                "[%s] window_handler (%d ops)\n",
                                eb->editor_name,
                                window->num_ops);
  else
    str = svn_stringbuf_createf(pool,
                                "[%s] window_handler (EOT)\n",
                                eb->editor_name);
    
  SVN_ERR(print(eb, nb->indent_level + 2, str));

  if (window)
    {
      /* Delve into the vcdiff window and print the data. */
      for (i = 1; i <= window->num_ops; i++)
        {
          switch (window->ops[i].action_code)
            {
            case svn_txdelta_new:
              str = svn_stringbuf_createf 
                (pool,
                 "(%d) new text: length %" APR_SIZE_T_FMT "\n",
                 i, (window->ops[i].length));
                break;

            case svn_txdelta_source:
              str = svn_stringbuf_createf 
                (pool,
                 "(%d) source text: offset %" APR_SIZE_T_FMT
                 ", length %" APR_SIZE_T_FMT "\n",
                 i, window->ops[i].offset, window->ops[i].length);
              break;

            case svn_txdelta_target:
              str = svn_stringbuf_createf
                (pool,
                 "(%d) target text: offset %" APR_SIZE_T_FMT
                 ", length %" APR_SIZE_T_FMT "\n",
                 i,  window->ops[i].offset, window->ops[i].length);
              break;

            default:
              str = svn_stringbuf_createf(pool, 
                                          "(%d) unknown window type\n", i);
              break;
            }
          SVN_ERR(print(eb, nb->indent_level + 2, str));
        }
    }

  SVN_ERR(newline(eb));

  return SVN_NO_ERROR;
}



static svn_error_t *
test_delete_entry(const char *path,
                  svn_revnum_t revision,
                  void *parent_baton,
                  apr_pool_t *pool)
{
  struct node_baton *nb = parent_baton;
  struct edit_baton *eb = nb->edit_baton;
  const char *full_path;
  svn_stringbuf_t *str;

  full_path = svn_path_join(eb->root_path, path, pool);
  str = svn_stringbuf_createf(pool,
                              "[%s] delete_entry (%s)\n",
                              eb->editor_name, full_path);
  SVN_ERR(print(eb, nb->indent_level + 1, str));

  if (eb->verbose)
    SVN_ERR(newline(eb));

  return SVN_NO_ERROR;         
}


static svn_error_t *
test_set_target_revision(void *edit_baton,
                         svn_revnum_t target_revision,
                         apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  svn_stringbuf_t *str;

  str = svn_stringbuf_createf(pool,
                              "[%s] set_target_revision (%ld)\n",
                              eb->editor_name,
                              target_revision);
  SVN_ERR(print(eb, 0, str));

  if (eb->verbose)
    SVN_ERR(newline(eb));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_open_root(void *edit_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct node_baton *nb = apr_pcalloc(pool, sizeof(*nb));
  svn_stringbuf_t *str;

  nb->path = apr_pstrdup(pool, eb->root_path);
  nb->edit_baton = eb;
  nb->indent_level = 0;
  *root_baton = nb;

  str = svn_stringbuf_createf(pool,
                              "[%s] open_root (%s)\n",
                              eb->editor_name,
                              nb->path);
  SVN_ERR(print(eb, nb->indent_level, str));

  /* We're done if non-verbose */
  if (! eb->verbose)
    return SVN_NO_ERROR;

  str = svn_stringbuf_createf(pool, 
                              "base_revision: %ld\n",
                              base_revision);
  SVN_ERR(print(eb, nb->indent_level, str));
  SVN_ERR(newline(eb));

  return SVN_NO_ERROR;
}


static svn_error_t *
add_or_open(const char *path,
            void *parent_baton,
            const char *base_path,
            svn_revnum_t base_revision,
            apr_pool_t *pool,
            void **child_baton,
            svn_boolean_t is_dir,
            const char *pivot_string)
{
  struct node_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct node_baton *nb = apr_pcalloc(pool, sizeof(*nb));
  svn_stringbuf_t *str;

  /* Set child_baton to a new dir baton. */
  nb->path = svn_path_join(eb->root_path, path, pool);
  nb->edit_baton = pb->edit_baton;
  nb->parent_baton = pb;
  nb->indent_level = (pb->indent_level + 1);
  *child_baton = nb;

  str = svn_stringbuf_createf(pool, "[%s] %s_%s (%s)\n",
                              eb->editor_name, pivot_string, 
                              is_dir ? "directory" : "file", nb->path);
  SVN_ERR(print(eb, nb->indent_level, str));

  /* We're done if non-verbose */
  if (! eb->verbose)
    return SVN_NO_ERROR;

  str = svn_stringbuf_createf(pool, "parent: %s\n", pb->path);
  SVN_ERR(print(eb, nb->indent_level, str));

  if (strcmp(pivot_string, "add") == 0)
    {
      str = svn_stringbuf_createf(pool, "copyfrom_path: %s\n",
                                  base_path ? base_path : "");
      SVN_ERR(print(eb, nb->indent_level, str));

      str = svn_stringbuf_createf(pool, "copyfrom_revision: %ld\n",
                                  base_revision);
      SVN_ERR(print(eb, nb->indent_level, str));
    }
  else
    {
      str = svn_stringbuf_createf(pool, "base_revision: %ld\n",
                                  base_revision);
      SVN_ERR(print(eb, nb->indent_level, str));
    }

  SVN_ERR(newline(eb));

  return SVN_NO_ERROR;
}


static svn_error_t *
close_file_or_dir(void *baton,
                  svn_boolean_t is_dir,
                  apr_pool_t *pool)
{
  struct node_baton *nb = baton;
  struct edit_baton *eb = nb->edit_baton;
  svn_stringbuf_t *str;

  str = svn_stringbuf_createf(pool,
                              "[%s] close_%s (%s)\n",
                              eb->editor_name, 
                              is_dir ? "directory" : "file",
                              nb->path);
  SVN_ERR(print(eb, nb->indent_level, str));
  if (eb->verbose)
    SVN_ERR(newline(eb));

  return SVN_NO_ERROR;    
}


static svn_error_t *
test_add_directory(const char *path,
                   void *parent_baton,
                   const char *copyfrom_path,
                   svn_revnum_t copyfrom_revision,
                   apr_pool_t *pool,
                   void **child_baton)
{
  return add_or_open(path, parent_baton, copyfrom_path, copyfrom_revision,
                     pool, child_baton, TRUE, "add");
}


static svn_error_t *
test_open_directory(const char *path,
                    void *parent_baton,
                    svn_revnum_t base_revision,
                    apr_pool_t *pool,
                    void **child_baton)
{
  return add_or_open(path, parent_baton, NULL, base_revision,
                     pool, child_baton, TRUE, "open");
}


static svn_error_t *
test_add_file(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *pool,
              void **file_baton)
{
  return add_or_open(path, parent_baton, copyfrom_path, copyfrom_revision,
                     pool, file_baton, FALSE, "add");
}


static svn_error_t *
test_open_file(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **file_baton)
{
  return add_or_open(path, parent_baton, NULL, base_revision,
                     pool, file_baton, FALSE, "open");
}


static svn_error_t *
test_close_directory(void *dir_baton,
                     apr_pool_t *pool)
{
  return close_file_or_dir(dir_baton, TRUE, pool);
}


static svn_error_t *
absent_file_or_dir(const char *path,
                   void *baton,
                   svn_boolean_t is_dir,
                   apr_pool_t *pool)
{
  struct node_baton *nb = baton;
  struct edit_baton *eb = nb->edit_baton;
  svn_stringbuf_t *str;

  str = svn_stringbuf_createf(pool,
                              "[%s] absent_%s (%s)\n",
                              eb->editor_name, 
                              is_dir ? "directory" : "file",
                              nb->path);
  SVN_ERR(print(eb, nb->indent_level, str));
  if (eb->verbose)
    SVN_ERR(newline(eb));

  return SVN_NO_ERROR;    
}


static svn_error_t *
test_absent_directory(const char *path,
                      void *baton,
                      apr_pool_t *pool)
{
  return absent_file_or_dir(path, baton, TRUE, pool);
}


static svn_error_t *
test_close_file(void *file_baton,
                const char *text_checksum,
                apr_pool_t *pool)
{
  return close_file_or_dir(file_baton, FALSE, pool);
}


static svn_error_t *
test_absent_file(const char *path,
                 void *baton,
                 apr_pool_t *pool)
{
  return absent_file_or_dir(path, baton, FALSE, pool);
}


static svn_error_t *
test_close_edit(void *edit_baton,
                apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  svn_stringbuf_t *str;

  str = svn_stringbuf_createf(pool, "[%s] close_edit\n", eb->editor_name);
  SVN_ERR(print(eb, 0, str));

  if (eb->verbose)
    SVN_ERR(newline(eb));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_abort_edit(void *edit_baton,
                apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  svn_stringbuf_t *str;

  str = svn_stringbuf_createf(pool, "[%s] ***ABORT_EDIT***\n", 
                              eb->editor_name);
  SVN_ERR(print(eb, 0, str));

  if (eb->verbose)
    SVN_ERR(newline(eb));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_apply_textdelta(void *file_baton,
                     const char *base_checksum,
                     apr_pool_t *pool,
                     svn_txdelta_window_handler_t *handler,
                     void **handler_baton)
{
  struct node_baton *nb = file_baton;
  struct edit_baton *eb = nb->edit_baton;
  svn_stringbuf_t *str;

  /* Set the value of HANDLER and HANDLER_BATON here */
  *handler        = my_vcdiff_windoweater;
  *handler_baton  = nb;

  str = svn_stringbuf_createf(pool, "[%s] apply_textdelta (%s)\n",
                              eb->editor_name, nb->path);
  SVN_ERR(print(eb, nb->indent_level + 1, str));

  if (eb->verbose)
    SVN_ERR(newline(eb));

  return SVN_NO_ERROR;
}


static svn_error_t * 
change_prop(void *baton,
            const char *name,
            const svn_string_t *value,
            apr_pool_t *pool,
            svn_boolean_t is_dir)
{
  struct node_baton *nb = baton;
  struct edit_baton *eb = nb->edit_baton;
  svn_stringbuf_t *str;

  str = svn_stringbuf_createf(pool, "[%s] change_%s_prop (%s)\n", 
                              eb->editor_name, 
                              is_dir ? "directory" : "file", nb->path);
  SVN_ERR(print(eb, nb->indent_level + 1, str));

  /* We're done if non-verbose */
  if (! eb->verbose)
    return SVN_NO_ERROR;

  str = svn_stringbuf_createf(pool, "name: %s\n", name);
  SVN_ERR(print(eb, nb->indent_level + 1, str));

  str = svn_stringbuf_createf(pool, "value: %s\n", 
                              value ? value->data : "(null)");
  SVN_ERR(print(eb, nb->indent_level + 1, str));

  SVN_ERR(newline(eb));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_change_file_prop(void *file_baton,
                      const char *name, 
                      const svn_string_t *value,
                      apr_pool_t *pool)
{
  return change_prop(file_baton, name, value, pool, FALSE);
}


static svn_error_t *
test_change_dir_prop(void *parent_baton,
                     const char *name, 
                     const svn_string_t *value,
                     apr_pool_t *pool)
{
  return change_prop(parent_baton, name, value, pool, TRUE);
}


/*---------------------------------------------------------------*/

/** Public interface:  svn_test_get_editor() **/


svn_error_t *
svn_test_get_editor(const svn_delta_editor_t **editor,
                    void **edit_baton,
                    const char *editor_name,
                    svn_stream_t *out_stream,
                    int indentation,
                    svn_boolean_t verbose,
                    const char *path,
                    apr_pool_t *pool)
{
  svn_delta_editor_t *my_editor;
  struct edit_baton *my_edit_baton;

  /* Set up the editor. */
  my_editor = svn_delta_default_editor(pool);
  my_editor->set_target_revision = test_set_target_revision;
  my_editor->open_root           = test_open_root;
  my_editor->delete_entry        = test_delete_entry;
  my_editor->add_directory       = test_add_directory;
  my_editor->open_directory      = test_open_directory;
  my_editor->close_directory     = test_close_directory;
  my_editor->absent_directory    = test_absent_directory;
  my_editor->add_file            = test_add_file;
  my_editor->open_file           = test_open_file;
  my_editor->close_file          = test_close_file;
  my_editor->absent_file         = test_absent_file;
  my_editor->apply_textdelta     = test_apply_textdelta;
  my_editor->change_file_prop    = test_change_file_prop;
  my_editor->change_dir_prop     = test_change_dir_prop;
  my_editor->close_edit          = test_close_edit;
  my_editor->abort_edit          = test_abort_edit;

  /* Set up the edit baton. */
  my_edit_baton = apr_pcalloc(pool, sizeof(*my_edit_baton));
  my_edit_baton->root_path = apr_pstrdup(pool, path);
  my_edit_baton->editor_name = apr_pstrdup(pool, editor_name);
  my_edit_baton->pool = pool;
  my_edit_baton->indentation = indentation;
  my_edit_baton->verbose = verbose;
  my_edit_baton->out_stream = out_stream;

  *editor = my_editor;
  *edit_baton = my_edit_baton;

  return SVN_NO_ERROR;
}

