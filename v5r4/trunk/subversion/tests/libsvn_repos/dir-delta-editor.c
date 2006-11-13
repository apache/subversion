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

#include <apr_pools.h>
#include <apr_file_io.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"

#include "../svn_test.h"
#include "dir-delta-editor.h"

/* Also used for dir batons. */
struct edit_baton
{
  svn_fs_t *fs;
  svn_fs_root_t *txn_root;
  const char *root_path;
  apr_pool_t *pool;
};


struct file_baton
{
  struct edit_baton *edit_baton;
  const char *path;
};



static svn_error_t *
test_delete_entry(const char *path,
                  svn_revnum_t revision,
                  void *parent_baton,
                  apr_pool_t *pool)
{
  struct edit_baton *eb = parent_baton;
  const char *full_path;

  /* Construct the full path of this entry and delete it from the txn. */
  full_path = svn_path_join(eb->root_path, path, pool);
  return svn_fs_delete(eb->txn_root, full_path, pool);
}


static svn_error_t *
test_open_root(void *edit_baton,
               svn_revnum_t base_revision,
               apr_pool_t *dir_pool,
               void **root_baton)
{
  *root_baton = edit_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
test_open_directory(const char *path,
                    void *parent_baton,
                    svn_revnum_t base_revision,
                    apr_pool_t *dir_pool,
                    void **child_baton)
{
  struct edit_baton *eb = parent_baton;
  svn_fs_root_t *rev_root = NULL;
  const char *full_path;

  /* Construct the full path of the new directory */
  full_path = svn_path_join(eb->root_path, path, eb->pool);

  SVN_ERR(svn_fs_revision_root(&rev_root, eb->fs, base_revision, dir_pool));
  SVN_ERR(svn_fs_revision_link(rev_root, eb->txn_root, full_path, dir_pool));

  *child_baton = parent_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
test_add_directory(const char *path,
                   void *parent_baton,
                   const char *copyfrom_path,
                   svn_revnum_t copyfrom_revision,
                   apr_pool_t *dir_pool,
                   void **child_baton)
{
  struct edit_baton *eb = parent_baton;
  const char *full_path;

  /* Construct the full path of the new directory */
  full_path = svn_path_join(eb->root_path, path, eb->pool);

  if (copyfrom_path)  /* add with history */
    {
      svn_fs_root_t *rev_root = NULL;

      SVN_ERR(svn_fs_revision_root(&rev_root,
                                   eb->fs,
                                   copyfrom_revision,
                                   dir_pool));
      
      SVN_ERR(svn_fs_copy(rev_root,
                          copyfrom_path,
                          eb->txn_root,
                          full_path,
                          dir_pool));
    }
  else  /* add without history */
    SVN_ERR(svn_fs_make_dir(eb->txn_root, full_path, dir_pool));

  *child_baton = parent_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
test_open_file(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *file_pool,
               void **file_baton)
{
  struct edit_baton *eb = parent_baton;
  struct file_baton *fb = apr_pcalloc(file_pool, sizeof(*fb));
  svn_fs_root_t *rev_root = NULL;

  /* Fill in the file baton. */
  fb->path = svn_path_join(eb->root_path, path, eb->pool);
  fb->edit_baton = eb;

  SVN_ERR(svn_fs_revision_root(&rev_root, eb->fs, base_revision, file_pool));
  SVN_ERR(svn_fs_revision_link(rev_root, eb->txn_root, fb->path, file_pool));

  *file_baton = fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
test_add_file(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *file_pool,
              void **file_baton)
{
  struct edit_baton *eb = parent_baton;
  struct file_baton *fb = apr_pcalloc(file_pool, sizeof(*fb));

  /* Fill in the file baton. */
  fb->path = svn_path_join(eb->root_path, path, eb->pool);
  fb->edit_baton = eb;

  if (copyfrom_path)  /* add with history */
    {
      svn_fs_root_t *rev_root = NULL;

      SVN_ERR(svn_fs_revision_root(&rev_root,
                                   eb->fs,
                                   copyfrom_revision,
                                   file_pool));

      SVN_ERR(svn_fs_copy(rev_root,
                          copyfrom_path,
                          eb->txn_root,
                          fb->path,
                          file_pool));
    }
  else  /* add without history */
    SVN_ERR(svn_fs_make_file(eb->txn_root, fb->path, file_pool));

  *file_baton = fb;
  return SVN_NO_ERROR;
}



static svn_error_t *
test_apply_textdelta(void *file_baton,
                     const char *base_checksum,
                     apr_pool_t *pool,
                     svn_txdelta_window_handler_t *handler,
                     void **handler_baton)
{
  struct file_baton *fb = file_baton;

  return svn_fs_apply_textdelta(handler, handler_baton,
                                fb->edit_baton->txn_root, 
                                fb->path,
                                base_checksum,
                                NULL,
                                pool);
}


static svn_error_t *
test_change_file_prop(void *file_baton,
                      const char *name, const svn_string_t *value,
                      apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;

  return svn_fs_change_node_prop(fb->edit_baton->txn_root,
                                 fb->path, name, value, pool);
}


static svn_error_t *
test_change_dir_prop(void *parent_baton,
                     const char *name, const svn_string_t *value,
                     apr_pool_t *pool)
{
  struct edit_baton *eb = parent_baton;
  const char *full_path;

  /* Construct the full path of this entry and change the property. */
  full_path = svn_path_join(eb->root_path, name, pool);
  return svn_fs_change_node_prop(eb->txn_root, full_path, name, value, pool);
}


/*---------------------------------------------------------------*/



svn_error_t *
dir_delta_get_editor(const svn_delta_editor_t **editor,
                     void **edit_baton,
                     svn_fs_t *fs,
                     svn_fs_root_t *txn_root,
                     const char *path,
                     apr_pool_t *pool)
{
  svn_delta_editor_t *my_editor;
  struct edit_baton *my_edit_baton;

  /* Wondering why we don't include test_close_directory,
     test_close_file, test_absent_directory, and test_absent_file
     here...?  -kfogel, 3 Nov 2003 */

  /* Set up the editor. */
  my_editor = svn_delta_default_editor(pool);
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
  my_edit_baton = apr_pcalloc(pool, sizeof(*my_edit_baton));
  my_edit_baton->root_path = apr_pstrdup(pool, path);
  my_edit_baton->pool = pool;
  my_edit_baton->fs = fs;
  my_edit_baton->txn_root = txn_root;

  *editor = my_editor;
  *edit_baton = my_edit_baton;

  return SVN_NO_ERROR;
}
