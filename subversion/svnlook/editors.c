/*
 * editors.c:  an editor for tracking commit changes
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
#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svnlook.h"



/*** Editor functions and batons. ***/

struct edit_baton
{
  svn_fs_t *fs;
  svn_fs_root_t *root;
  svn_fs_root_t *base_root;
  apr_pool_t *pool;
  repos_node_t *node;
};


struct dir_baton
{
  svn_stringbuf_t *path;
  struct edit_baton *edit_baton;
  repos_node_t *node;
};


struct file_baton
{
  svn_stringbuf_t *path;
  struct dir_baton *dir_baton;
  repos_node_t *node;
};


static svn_error_t *
delete_entry (svn_stringbuf_t *name, 
              void *parent_baton)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  struct edit_baton *eb = (struct edit_baton *) d->edit_baton;
  svn_stringbuf_t *full_path;
  repos_node_t *node;
  int is_dir;

  /* Construct the full path of this entry based on its parent. */
  full_path = svn_stringbuf_dup (d->path, eb->pool);
  svn_path_add_component (full_path, name, svn_path_repos_style);

  /* Was this a dir or file (we have to check the base root for this one) */
  SVN_ERR (svn_fs_is_dir (&is_dir, eb->base_root, full_path->data, eb->pool));

  /* Get (or create) the change node and update it. */
  node = svnlook_find_child_by_name (d->node, name->data);
  if (! node)
    node = svnlook_create_child_node (d->node, name->data, eb->pool);

  if (is_dir)
    node->kind = svn_node_dir;
  else
    node->kind = svn_node_file;

  node->action = 'D';

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_root (void *edit_baton,
              svn_revnum_t base_revision,
              void **root_baton)
{
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  struct dir_baton *d = apr_pcalloc (eb->pool, sizeof (*d));

  d->path = (svn_stringbuf_t *) svn_stringbuf_create ("", eb->pool);
  d->edit_baton = eb;
  d->node = (eb->node = svnlook_create_node ("", eb->pool));
  d->node->kind = svn_node_dir;
  *root_baton = d;
  
  return SVN_NO_ERROR;
}


static svn_error_t *
replace_directory (svn_stringbuf_t *name,
                   void *parent_baton,
                   svn_revnum_t base_revision,
                   void **child_baton)
{
  struct dir_baton *pd = (struct dir_baton *) parent_baton;
  struct edit_baton *eb = (struct edit_baton *) pd->edit_baton;
  struct dir_baton *d = apr_pcalloc (pd->edit_baton->pool, sizeof (*d));

  /* Construct the full path of the new directory */
  d->path = svn_stringbuf_dup (pd->path, eb->pool);
  svn_path_add_component (d->path, name, svn_path_local_style);

  /* Fill in other baton members */
  d->edit_baton = eb;
  d->node = svnlook_create_child_node (pd->node, name->data, eb->pool);
  d->node->kind = svn_node_dir;
  *child_baton = d;

  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (svn_stringbuf_t *name,
               void *parent_baton,
               svn_stringbuf_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **child_baton)
{
  struct dir_baton *pd = (struct dir_baton *) parent_baton;
  struct edit_baton *eb = (struct edit_baton *) pd->edit_baton;
  struct dir_baton *d = apr_pcalloc (pd->edit_baton->pool, sizeof (*d));

  /* Construct the full path of the new directory */
  d->path = svn_stringbuf_dup (pd->path, eb->pool);
  svn_path_add_component (d->path, name, svn_path_local_style);

  /* Fill in other baton members */
  d->edit_baton = eb;
  d->node = svnlook_create_child_node (pd->node, name->data, eb->pool);
  d->node->kind = svn_node_dir;
  d->node->action = 'A';
  *child_baton = d;

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_file (svn_stringbuf_t *name,
              void *parent_baton,
              svn_revnum_t base_revision,
              void **file_baton)
{
  struct dir_baton *pd = (struct dir_baton *) parent_baton;
  struct edit_baton *eb = (struct edit_baton *) pd->edit_baton;
  struct file_baton *fb = apr_pcalloc (pd->edit_baton->pool, sizeof (*fb));

  /* Construct the full path of the new directory */
  fb->path = svn_stringbuf_dup (pd->path, eb->pool);
  svn_path_add_component (fb->path, name, svn_path_local_style);

  /* Fill in other baton members */
  fb->dir_baton = pd;
  fb->node = svnlook_create_child_node (pd->node, name->data, eb->pool);
  fb->node->kind = svn_node_file;
  *file_baton = fb;

  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (svn_stringbuf_t *name,
          void *parent_baton,
          svn_stringbuf_t *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          void **file_baton)
{
  struct dir_baton *pd = (struct dir_baton *) parent_baton;
  struct edit_baton *eb = (struct edit_baton *) pd->edit_baton;
  struct file_baton *fb = apr_pcalloc (pd->edit_baton->pool, sizeof (*fb));

  /* Construct the full path of the new directory */
  fb->path = svn_stringbuf_dup (pd->path, eb->pool);
  svn_path_add_component (fb->path, name, svn_path_local_style);

  /* Fill in other baton members */
  fb->dir_baton = pd;
  fb->node = svnlook_create_child_node (pd->node, name->data, eb->pool);
  fb->node->kind = svn_node_file;
  fb->node->action = 'A';
  *file_baton = fb;

  return SVN_NO_ERROR;
}


static svn_error_t *
window_handler (svn_txdelta_window_t *window, 
                void *baton)
{
  /* Do nothing. */
  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  fb->node->text_mod = TRUE;
  *handler = window_handler;
  *handler_baton = NULL;

  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_stringbuf_t *name, 
                  svn_stringbuf_t *value)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  fb->node->prop_mod = TRUE;

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *parent_baton,
                 svn_stringbuf_t *name, 
                 svn_stringbuf_t *value)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  d->node->prop_mod = TRUE;

  return SVN_NO_ERROR;
}


svn_error_t *
svnlook_rev_changes_editor (const svn_delta_edit_fns_t **editor,
                            void **edit_baton,
                            svn_fs_t *fs,
                            svn_fs_root_t *root,
                            svn_fs_root_t *base_root,
                            apr_pool_t *pool)
{
  svn_delta_edit_fns_t *my_editor;
  struct edit_baton *my_edit_baton;

  /* Set up the editor. */
  my_editor = svn_delta_default_editor (pool);
  my_editor->replace_root        = replace_root;
  my_editor->delete_entry        = delete_entry;
  my_editor->add_directory       = add_directory;
  my_editor->replace_directory   = replace_directory;
  my_editor->add_file            = add_file;
  my_editor->replace_file        = replace_file;
  my_editor->apply_textdelta     = apply_textdelta;
  my_editor->change_file_prop    = change_file_prop;
  my_editor->change_dir_prop     = change_dir_prop;

  /* Set up the edit baton. */
  my_edit_baton = apr_pcalloc (pool, sizeof (*my_edit_baton));
  my_edit_baton->pool = pool;
  my_edit_baton->fs = fs;
  my_edit_baton->root = root;
  my_edit_baton->base_root = base_root;

  *editor = my_editor;
  *edit_baton = my_edit_baton;

  return SVN_NO_ERROR;
}


svn_error_t *
svnlook_txn_changes_editor (const svn_delta_edit_fns_t **editor,
                            void **edit_baton,
                            svn_fs_t *fs,
                            svn_fs_root_t *root,
                            apr_pool_t *pool)
{
  svn_delta_edit_fns_t *my_editor;
  struct edit_baton *my_edit_baton;

  /* Set up the editor. */
  my_editor = svn_delta_default_editor (pool);
  my_editor->replace_root        = replace_root;
  my_editor->delete_entry        = delete_entry;
  my_editor->add_directory       = add_directory;
  my_editor->replace_directory   = replace_directory;
  my_editor->add_file            = add_file;
  my_editor->replace_file        = replace_file;
  my_editor->apply_textdelta     = apply_textdelta;
  my_editor->change_file_prop    = change_file_prop;
  my_editor->change_dir_prop     = change_dir_prop;

  /* Set up the edit baton. */
  my_edit_baton = apr_pcalloc (pool, sizeof (*my_edit_baton));
  my_edit_baton->pool = pool;
  my_edit_baton->fs = fs;
  my_edit_baton->root = root;

  *editor = my_editor;
  *edit_baton = my_edit_baton;

  return SVN_NO_ERROR;
}


repos_node_t *
svnlook_edit_baton_tree (void *edit_baton)
{
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  return eb->node;
}



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
