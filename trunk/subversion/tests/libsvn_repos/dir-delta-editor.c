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
#include "apr_pools.h"
#include "apr_file_io.h"
#include "svn_types.h"
#include "svn_test.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "dir-delta-editor.h"

struct edit_baton
{
  svn_fs_t *fs;
  svn_fs_root_t *txn_root;
  svn_stringbuf_t *root_path;
  apr_pool_t *pool;
};


struct dir_baton
{
  svn_stringbuf_t *path;
  struct edit_baton *edit_baton;
};


struct file_baton
{
  svn_stringbuf_t *path;
  struct dir_baton *dir_baton;
};



static svn_error_t *
test_delete_entry (svn_stringbuf_t *filename, 
                   svn_revnum_t revision,
                   void *parent_baton)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  svn_stringbuf_t *full_path;

  /* Construct the full path of this entry based on its parent. */
  full_path = svn_stringbuf_dup (d->path, d->edit_baton->pool);
  svn_path_add_component (full_path, filename);

  /* Now delete item from the txn. */
  return svn_fs_delete_tree (d->edit_baton->txn_root,
                             full_path->data,
                             d->edit_baton->pool);
}


static svn_error_t *
test_open_root (void *edit_baton,
                svn_revnum_t base_revision,
                void **root_baton)
{
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  struct dir_baton *d = apr_pcalloc (eb->pool, sizeof (*d));

  d->path = (svn_stringbuf_t *) svn_stringbuf_dup (eb->root_path, eb->pool);
  d->edit_baton = eb;
  *root_baton = d;
  
  return SVN_NO_ERROR;
}


static svn_error_t *
test_open_directory (svn_stringbuf_t *name,
                     void *parent_baton,
                     svn_revnum_t base_revision,
                     void **child_baton)
{
  struct dir_baton *pd = (struct dir_baton *) parent_baton;
  struct dir_baton *d = apr_pcalloc (pd->edit_baton->pool, sizeof (*d));
  svn_fs_root_t *rev_root = NULL;

  /* Construct the full path of the new directory */
  d->path = svn_stringbuf_dup (pd->path, pd->edit_baton->pool);
  svn_path_add_component (d->path, name);

  /* Fill in other baton members */
  d->edit_baton = pd->edit_baton;
  *child_baton = d;

  SVN_ERR (svn_fs_revision_root (&rev_root,
                                 pd->edit_baton->fs,
                                 base_revision,
                                 pd->edit_baton->pool));

  SVN_ERR (svn_fs_link (rev_root,
                        d->path->data,
                        pd->edit_baton->txn_root,
                        d->path->data,
                        pd->edit_baton->pool));


  return SVN_NO_ERROR;
}


static svn_error_t *
test_add_directory (svn_stringbuf_t *name,
                    void *parent_baton,
                    svn_stringbuf_t *copyfrom_path,
                    svn_revnum_t copyfrom_revision,
                    void **child_baton)
{
  struct dir_baton *pd = (struct dir_baton *) parent_baton;
  struct dir_baton *d = apr_pcalloc (pd->edit_baton->pool, sizeof (*d));

  /* Construct the full path of the new directory */
  d->path = svn_stringbuf_dup (pd->path, pd->edit_baton->pool);
  svn_path_add_component (d->path, name);

  /* Fill in other baton members */
  d->edit_baton = pd->edit_baton;
  *child_baton = d;

  if (copyfrom_path)  /* add with history */
    {
      svn_fs_root_t *rev_root = NULL;

      SVN_ERR (svn_fs_revision_root (&rev_root,
                                     pd->edit_baton->fs, 
                                     copyfrom_revision,
                                     pd->edit_baton->pool));   
      
      SVN_ERR (svn_fs_copy (rev_root,
                            copyfrom_path->data,
                            pd->edit_baton->txn_root,
                            d->path->data,
                            pd->edit_baton->pool));
    }
  else  /* add without history */
    {
      SVN_ERR (svn_fs_make_dir (pd->edit_baton->txn_root,
                                d->path->data,
                                pd->edit_baton->pool));
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_open_file (svn_stringbuf_t *name,
                void *parent_baton,
                svn_revnum_t base_revision,
                void **file_baton)
{
  struct dir_baton *pd = (struct dir_baton *) parent_baton;
  struct file_baton *fb = apr_pcalloc (pd->edit_baton->pool, sizeof (*fb));
  svn_fs_root_t *rev_root = NULL;

  /* Construct the full path of the new directory */
  fb->path = svn_stringbuf_dup (pd->path, pd->edit_baton->pool);
  svn_path_add_component (fb->path, name);

  /* Fill in other baton members */
  fb->dir_baton = pd;
  *file_baton = fb;

  SVN_ERR (svn_fs_revision_root (&rev_root,
                                 pd->edit_baton->fs,
                                 base_revision,
                                 pd->edit_baton->pool));

  SVN_ERR (svn_fs_link (rev_root,
                        fb->path->data,
                        pd->edit_baton->txn_root,
                        fb->path->data,
                        pd->edit_baton->pool));


  return SVN_NO_ERROR;
}


static svn_error_t *
test_add_file (svn_stringbuf_t *name,
               void *parent_baton,
               svn_stringbuf_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **file_baton)
{
  struct dir_baton *pd = (struct dir_baton *) parent_baton;
  struct file_baton *fb = apr_pcalloc (pd->edit_baton->pool, sizeof (*fb));

  /* Construct the full path of the new directory */
  fb->path = svn_stringbuf_dup (pd->path, pd->edit_baton->pool);
  svn_path_add_component (fb->path, name);

  /* Fill in other baton members */
  fb->dir_baton = pd;
  *file_baton = fb;

  if (copyfrom_path)  /* add with history */
    {
      svn_fs_root_t *rev_root = NULL;

      SVN_ERR (svn_fs_revision_root (&rev_root,
                                     pd->edit_baton->fs,
                                     copyfrom_revision,
                                     pd->edit_baton->pool));

      SVN_ERR (svn_fs_copy (rev_root,
                            copyfrom_path->data,
                            pd->edit_baton->txn_root,
                            fb->path->data,
                            pd->edit_baton->pool));
    }
  else  /* add without history */
    {
      SVN_ERR (svn_fs_make_file (pd->edit_baton->txn_root,
                                 fb->path->data,
                                 pd->edit_baton->pool));
    }

  return SVN_NO_ERROR;
}



static svn_error_t *
test_apply_textdelta (void *file_baton,
                      svn_txdelta_window_handler_t *handler,
                      void **handler_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;

  return svn_fs_apply_textdelta (handler, handler_baton,
                                 fb->dir_baton->edit_baton->txn_root, 
                                 fb->path->data,
                                 fb->dir_baton->edit_baton->pool);
}


static svn_error_t *
test_change_file_prop (void *file_baton,
                       svn_stringbuf_t *name, svn_stringbuf_t *value)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  svn_string_t propvalue;
  propvalue.data = value->data;
  propvalue.len = value->len;

  return svn_fs_change_node_prop (fb->dir_baton->edit_baton->txn_root,
                                  fb->path->data, name->data, &propvalue,
                                  fb->dir_baton->edit_baton->pool);
}


static svn_error_t *
test_change_dir_prop (void *parent_baton,
                      svn_stringbuf_t *name, svn_stringbuf_t *value)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  svn_string_t propvalue;
  propvalue.data = value->data;
  propvalue.len = value->len;

  return svn_fs_change_node_prop (d->edit_baton->txn_root,
                                  d->path->data, name->data, &propvalue,
                                  d->edit_baton->pool);
}


/*---------------------------------------------------------------*/



svn_error_t *
dir_delta_get_editor (const svn_delta_edit_fns_t **editor,
                      void **edit_baton,
                      svn_fs_t *fs,
                      svn_fs_root_t *txn_root,
                      svn_stringbuf_t *path,
                      apr_pool_t *pool)
{
  svn_delta_edit_fns_t *my_editor;
  struct edit_baton *my_edit_baton;

  /* Set up the editor. */
  my_editor = svn_delta_old_default_editor (pool);
  my_editor->open_root           = test_open_root;
  my_editor->delete_entry        = test_delete_entry;
  my_editor->add_directory       = test_add_directory;
  my_editor->open_directory      = test_open_directory;
  my_editor->add_file            = test_add_file;
  my_editor->open_file           = test_open_file;
  my_editor->apply_textdelta     = test_apply_textdelta;
  my_editor->change_file_prop    = test_change_file_prop;
  my_editor->change_dir_prop     = test_change_dir_prop;

  /* Set up the edit baton. */
  my_edit_baton = apr_pcalloc (pool, sizeof (*my_edit_baton));
  my_edit_baton->root_path = svn_stringbuf_dup (path, pool);
  my_edit_baton->pool = pool;
  my_edit_baton->fs = fs;
  my_edit_baton->txn_root = txn_root;

  *editor = my_editor;
  *edit_baton = my_edit_baton;

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
