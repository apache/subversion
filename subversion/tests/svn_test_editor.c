/*
 * svn_tests_editor.c:  a `dummy' editor implementation for testing
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



#include <stdio.h>
#include "apr_pools.h"
#include "apr_file_io.h"
#include "svn_types.h"
#include "svn_test.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"


static const int indent_amount = 2;

struct edit_baton
{
  svn_string_t *root_path;
  svn_vernum_t version;
  apr_pool_t *pool;
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


/* For making formatting all purty. */
static void
print_spaces (int total)
{
  int i;

  for (i = 0; i < total; i++)
    printf(" ");
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
  

  if (! window)
    {
      print_spaces (fb->indent_level + indent_amount);
      printf ("end of windows\n");
      return SVN_NO_ERROR;
    }

  /* Delve into the vcdiff window and print the data. */
  for (i = 0; i < window->num_ops; i++)
    {
      switch (window->ops[i].action_code)
        {
        case svn_txdelta_new:
          {
            char *startaddr = (window->new->data +
                                (window->ops[i].offset));
            svn_string_t *str = 
              svn_string_ncreate (startaddr,
                                  (window->ops[i].length),
                                  pool);
            
            print_spaces (fb->indent_level + indent_amount);
            printf ("txdelta window: new text (%ld bytes): %s\n",
                    (long int) window->ops[i].length, str->data);
            break;
          }
        case svn_txdelta_source:
          {
            print_spaces (fb->indent_level + indent_amount);
            printf ("txdelta window: source text: offset %ld, length %ld\n",
                    (long int) window->ops[i].offset,
                    (long int) window->ops[i].length);
            break;
          }
        case svn_txdelta_target:
          {
            print_spaces (fb->indent_level + indent_amount);
            printf ("txdelta window: target text: offset %ld, length %ld\n",
                    (long int) window->ops[i].offset,
                    (long int) window->ops[i].length);
            break;
          }
        default:
          {
            print_spaces (fb->indent_level + indent_amount);
            printf ("txdelta window: whoa, unknown op: %d\n",
                    (int) window->ops[i].action_code);
            break;
          }
        }
              
    }


  return SVN_NO_ERROR;
}



static svn_error_t *
test_delete (svn_string_t *filename, void *parent_baton)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  char *Aname = filename->data ? filename->data : "(unknown)";

  print_spaces (d->indent_level);

  printf ("DELETE file '%s'\n", Aname);
  return SVN_NO_ERROR;         
}


static svn_error_t *
test_replace_root (void *edit_baton,
                   void **root_baton)
{
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  struct dir_baton *d = apr_pcalloc (eb->pool, sizeof (*d));

  d->path = (svn_string_t *) svn_string_dup (eb->root_path, eb->pool);
  d->edit_baton = eb;
  d->indent_level = 0;
  *root_baton = d;

  print_spaces (d->indent_level);  /* probably a no-op */
  printf ("REPLACE_ROOT:  name '%s', version '%ld'\n",
          eb->root_path->data,
          eb->version);


  return SVN_NO_ERROR;
}


static svn_error_t *
add_or_replace_dir (svn_string_t *name,
                    void *parent_baton,
                    svn_string_t *ancestor_path,
                    long int ancestor_version,
                    void **child_baton,
                    const char *pivot_string)
{
  struct dir_baton *pd = (struct dir_baton *) parent_baton;
  char *Aname = name ? name->data : "(unknown)";
  char *ancestor = ancestor_path ? ancestor_path->data : "(unknown)";
  struct dir_baton *d;

  /* Set child_baton to a new dir baton. */
  d = apr_pcalloc (pd->edit_baton->pool, sizeof (*d));
  d->path = svn_string_dup (pd->path, pd->edit_baton->pool);
  svn_path_add_component (d->path,
                          svn_string_create (Aname, pd->edit_baton->pool),
                          svn_path_local_style);
  d->edit_baton = pd->edit_baton;
  d->indent_level = (pd->indent_level + indent_amount);
  print_spaces (d->indent_level);
  *child_baton = d;

  printf ("%s:  name '%s', ancestor '%s' version %ld\n",
          pivot_string, Aname, ancestor, ancestor_version);

  
  return SVN_NO_ERROR;
}


static svn_error_t *
test_add_directory (svn_string_t *name,
                    void *parent_baton,
                    svn_string_t *ancestor_path,
                    long int ancestor_version,
                    void **child_baton)
{
  return add_or_replace_dir (name,
                             parent_baton,
                             ancestor_path,
                             ancestor_version,
                             child_baton,
                             "ADD_DIR");
}


static svn_error_t *
test_replace_directory (svn_string_t *name,
                        void *parent_baton,
                        svn_string_t *ancestor_path,
                        long int ancestor_version,
                        void **child_baton)
{
  return add_or_replace_dir (name,
                             parent_baton,
                             ancestor_path,
                             ancestor_version,
                             child_baton,
                             "REPLACE_DIR");
}


static svn_error_t *
test_close_directory (void *dir_baton)
{
  struct dir_baton *d = (struct dir_baton *) dir_baton;
  print_spaces (d->indent_level);

  if (d->path)
    printf ("CLOSE_DIR '%s'\n", d->path->data);
  else 
    printf ("CLOSE_DIR:  no name!!\n");

  return SVN_NO_ERROR;    
}


static svn_error_t *
test_close_file (void *file_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;

  print_spaces (fb->indent_level);

  if (file_baton)
    printf ("CLOSE_FILE '%s'\n", fb->path->data);
  else
    printf ("CLOSE_FILE:  no name!!\n");

  return SVN_NO_ERROR;    
}

static svn_error_t *
test_close_edit (void *edit_baton)
{
  printf ("EDIT COMPLETE.\n");

  return SVN_NO_ERROR;
}


static svn_error_t *
test_apply_textdelta (void *file_baton,
                      svn_txdelta_window_handler_t **handler,
                      void **handler_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;

  char *Aname = fb->path ? fb->path->data : "(unknown)";

  print_spaces (fb->indent_level + indent_amount);

  printf ("TEXT-DELTA on file '%s':\n", Aname);

  /* Set the value of HANDLER and HANDLER_BATON here */
  *handler        = my_vcdiff_windoweater;
  *handler_baton  = fb;

  return SVN_NO_ERROR;
}


static svn_error_t *
add_or_replace_file (svn_string_t *name,
                     void *parent_baton,
                     svn_string_t *ancestor_path,
                     long int ancestor_version,
                     void **file_baton,
                     const char *pivot_string)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  struct file_baton *fb;
  char *Aname = name ? name->data : "(unknown)";
  char *ancestor = ancestor_path ? ancestor_path->data : "(unknown)";

  /* Put the filename in file_baton */
  fb = apr_pcalloc (d->edit_baton->pool, sizeof (*fb));
  fb->dir_baton = d;
  fb->path = (svn_string_t *) svn_string_dup (name, d->edit_baton->pool);
  fb->indent_level = (d->indent_level + indent_amount);
  print_spaces (fb->indent_level);
  *file_baton = fb;

  printf ("%s:  name '%s', ancestor '%s' version %ld\n",
          pivot_string, Aname, ancestor, ancestor_version);

  return SVN_NO_ERROR;
}


static svn_error_t *
test_add_file (svn_string_t *name,
               void *parent_baton,
               svn_string_t *ancestor_path,
               long int ancestor_version,
               void **file_baton)
{
  return add_or_replace_file (name,
                              parent_baton,
                              ancestor_path,
                              ancestor_version,
                              file_baton,
                              "ADD_FILE");
}


static svn_error_t *
test_replace_file (svn_string_t *name,
                   void *parent_baton,
                   svn_string_t *ancestor_path,
                   long int ancestor_version,
                   void **file_baton)
{
  return add_or_replace_file (name,
                              parent_baton,
                              ancestor_path,
                              ancestor_version,
                              file_baton,
                              "REPLACE_FILE");
}


static svn_error_t *
test_change_file_prop (void *file_baton,
                       svn_string_t *name, svn_string_t *value)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  print_spaces (fb->indent_level + indent_amount);

  printf ("PROPCHANGE on file '%s': ", fb->path->data);

  if (value == NULL)
    printf (" delete `%s'\n", (char *) name->data);

  else
    printf (" set `%s' to `%s'\n",
            (char *) name->data, (char *) value->data);

  return SVN_NO_ERROR;
}


static svn_error_t *
test_change_dir_prop (void *parent_baton,
                      svn_string_t *name, svn_string_t *value)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  print_spaces (d->indent_level + indent_amount);

  printf ("PROPCHANGE on directory '%s': ", d->path->data);

  if (value == NULL)
    printf (" delete `%s'\n", (char *) name->data);

  else
    printf (" set  `%s' to `%s'\n",
            (char *) name->data, (char *) value->data);

  return SVN_NO_ERROR;
}


/*---------------------------------------------------------------*/

/** Public interface:  svn_test_get_editor() **/


svn_error_t *
svn_test_get_editor (const svn_delta_edit_fns_t **editor,
                     void **edit_baton,
                     svn_string_t *path,
                     svn_vernum_t version,
                     apr_pool_t *pool)
{
  svn_delta_edit_fns_t *my_editor;
  struct edit_baton *my_edit_baton;

  my_editor = apr_pcalloc (pool, sizeof(*my_editor));

  my_editor->delete             = test_delete;
  my_editor->replace_root       = test_replace_root;
  my_editor->add_directory      = test_add_directory;
  my_editor->replace_directory  = test_replace_directory;
  my_editor->close_directory    = test_close_directory;
  my_editor->add_file           = test_add_file;
  my_editor->replace_file       = test_replace_file;
  my_editor->close_file         = test_close_file;
  my_editor->apply_textdelta    = test_apply_textdelta;
  my_editor->change_file_prop   = test_change_file_prop;
  my_editor->change_dir_prop    = test_change_dir_prop;
  my_editor->close_edit         = test_close_edit;

  my_edit_baton = apr_pcalloc (pool, sizeof (struct edit_baton));

  my_edit_baton->root_path = svn_string_dup (path, pool);
  my_edit_baton->version = version;
  my_edit_baton->pool = pool;


  *editor = my_editor;
  *edit_baton = my_edit_baton;

  return SVN_NO_ERROR;
}

