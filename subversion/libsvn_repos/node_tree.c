/*
 * node_tree.c:  an editor for tracking repository deltas changes
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

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "repos.h"


/*** Node creation and assembly structures and routines. ***/
static svn_repos_node_t *
create_node (const char *name, 
             apr_pool_t *pool)
{
  svn_repos_node_t *node = apr_pcalloc (pool, sizeof (svn_repos_node_t));
  node->action = 'R';
  node->kind = svn_node_unknown;
  node->name = apr_pstrdup (pool, name);
  return node;
}


static svn_repos_node_t *
create_sibling_node (svn_repos_node_t *elder, 
                     const char *name, 
                     apr_pool_t *pool)
{
  svn_repos_node_t *tmp_node;
  
  /* No ELDER sibling?  That's just not gonna work out. */
  if (! elder)
    return NULL;

  /* Run to the end of the list of siblings of ELDER. */
  tmp_node = elder;
  while (tmp_node->sibling)
    tmp_node = tmp_node->sibling;

  /* Create a new youngest sibling and return that. */
  return (tmp_node->sibling = create_node (name, pool));
}


static svn_repos_node_t *
create_child_node (svn_repos_node_t *parent, 
                   const char *name, 
                   apr_pool_t *pool)
{
  /* No PARENT node?  That's just not gonna work out. */
  if (! parent)
    return NULL;

  /* If PARENT has no children, create its first one and return that. */
  if (! parent->child)
    return (parent->child = create_node (name, pool));

  /* If PARENT already has a child, create a new sibling for its first
     child and return that. */
  return create_sibling_node (parent->child, name, pool);
}


static svn_repos_node_t *
find_child_by_name (svn_repos_node_t *parent, 
                    const char *name)
{
  svn_repos_node_t *tmp_node;

  /* No PARENT node, or a barren PARENT?  Nothing to find. */
  if ((! parent) || (! parent->child))
    return NULL;

  /* Look through the children for a node with a matching name. */
  tmp_node = parent->child;
  while (1)
    {
      if (! strcmp (tmp_node->name, name))
        {
          return tmp_node;
        }
      else
        {
          if (tmp_node->sibling)
            tmp_node = tmp_node->sibling;
          else
            break;
        }
    }

  return NULL;
}


/*** Editor functions and batons. ***/

struct edit_baton
{
  svn_fs_t *fs;
  svn_fs_root_t *root;
  svn_fs_root_t *base_root;
  apr_pool_t *pool;
  apr_pool_t *node_pool;
  svn_repos_node_t *node;
};


struct dir_baton
{
  svn_stringbuf_t *path;
  struct edit_baton *edit_baton;
  svn_repos_node_t *node;
};


struct file_baton
{
  svn_stringbuf_t *path;
  struct dir_baton *dir_baton;
  svn_repos_node_t *node;
};


struct window_handler_baton
{
  svn_repos_node_t *node;
};


static svn_error_t *
delete_entry (svn_stringbuf_t *name, 
              svn_revnum_t revision,
              void *parent_baton)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  struct edit_baton *eb = (struct edit_baton *) d->edit_baton;
  svn_stringbuf_t *full_path;
  svn_repos_node_t *node;
  int is_dir;

  /* Construct the full path of this entry based on its parent. */
  full_path = svn_stringbuf_dup (d->path, eb->pool);
  svn_path_add_component (full_path, name);

  /* Was this a dir or file (we have to check the base root for this one) */
  SVN_ERR (svn_fs_is_dir (&is_dir, eb->base_root, full_path->data, eb->pool));

  /* Get (or create) the change node and update it. */
  node = find_child_by_name (d->node, name->data);
  if (! node)
    node = create_child_node (d->node, name->data, eb->node_pool);

  if (is_dir)
    node->kind = svn_node_dir;
  else
    node->kind = svn_node_file;

  node->action = 'D';
  return SVN_NO_ERROR;
}


static svn_error_t *
open_root (void *edit_baton,
              svn_revnum_t base_revision,
              void **root_baton)
{
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  struct dir_baton *d = apr_pcalloc (eb->pool, sizeof (*d));

  d->path = (svn_stringbuf_t *) svn_stringbuf_create ("", eb->pool);
  d->edit_baton = eb;
  d->node = (eb->node = create_node ("", eb->node_pool));
  d->node->kind = svn_node_dir;
  d->node->action = 'R';
  *root_baton = d;
  
  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory (svn_stringbuf_t *name,
                void *parent_baton,
                svn_revnum_t base_revision,
                void **child_baton)
{
  struct dir_baton *pd = (struct dir_baton *) parent_baton;
  struct edit_baton *eb = (struct edit_baton *) pd->edit_baton;
  struct dir_baton *d = apr_pcalloc (pd->edit_baton->pool, sizeof (*d));

  /* Construct the full path of the new directory */
  d->path = svn_stringbuf_dup (pd->path, eb->pool);
  svn_path_add_component (d->path, name);

  /* Fill in other baton members */
  d->edit_baton = eb;
  d->node = create_child_node (pd->node, name->data, eb->node_pool);
  d->node->kind = svn_node_dir;
  d->node->action = 'R';
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
  svn_path_add_component (d->path, name);

  /* Fill in other baton members */
  d->edit_baton = eb;
  d->node = create_child_node (pd->node, name->data, eb->node_pool);
  d->node->kind = svn_node_dir;
  d->node->action = 'A';
  d->node->copyfrom_rev = copyfrom_revision;
  d->node->copyfrom_path
    = copyfrom_path ? apr_pstrdup (eb->node_pool, copyfrom_path->data) : NULL;
  
  *child_baton = d;

  return SVN_NO_ERROR;
}


static svn_error_t *
open_file (svn_stringbuf_t *name,
           void *parent_baton,
           svn_revnum_t base_revision,
           void **file_baton)
{
  struct dir_baton *pd = (struct dir_baton *) parent_baton;
  struct edit_baton *eb = (struct edit_baton *) pd->edit_baton;
  struct file_baton *fb = apr_pcalloc (pd->edit_baton->pool, sizeof (*fb));

  /* Construct the full path of the new directory */
  fb->path = svn_stringbuf_dup (pd->path, eb->pool);
  svn_path_add_component (fb->path, name);

  /* Fill in other baton members */
  fb->dir_baton = pd;
  fb->node = create_child_node (pd->node, name->data, eb->node_pool);
  fb->node->kind = svn_node_file;
  fb->node->action = 'R';
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
  svn_path_add_component (fb->path, name);

  /* Fill in other baton members */
  fb->dir_baton = pd;
  fb->node = create_child_node (pd->node, name->data, eb->node_pool);
  fb->node->kind = svn_node_file;
  fb->node->action = 'A';
  fb->node->copyfrom_rev = copyfrom_revision;
  fb->node->copyfrom_path
    = copyfrom_path ? apr_pstrdup (eb->node_pool, copyfrom_path->data) : NULL;

  *file_baton = fb;

  return SVN_NO_ERROR;
}


static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *baton)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta (void *file_baton, 
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  struct edit_baton *eb = (struct edit_baton *) fb->dir_baton->edit_baton;
  struct window_handler_baton *whb = apr_palloc (eb->pool, sizeof (*whb));

  whb->node = fb->node;
  whb->node->text_mod = TRUE;
  *handler = window_handler;
  *handler_baton = whb;

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
svn_repos_node_editor (const svn_delta_edit_fns_t **editor,
                       void **edit_baton,
                       svn_repos_t *repos,
                       svn_fs_root_t *base_root,
                       svn_fs_root_t *root,
                       apr_pool_t *node_pool,
                       apr_pool_t *pool)
{
  svn_delta_edit_fns_t *my_editor;
  struct edit_baton *my_edit_baton;

  /* Set up the editor. */
  my_editor = svn_delta_old_default_editor (pool);
  my_editor->open_root           = open_root;
  my_editor->delete_entry        = delete_entry;
  my_editor->add_directory       = add_directory;
  my_editor->open_directory      = open_directory;
  my_editor->add_file            = add_file;
  my_editor->open_file           = open_file;
  my_editor->apply_textdelta     = apply_textdelta;
  my_editor->change_file_prop    = change_file_prop;
  my_editor->change_dir_prop     = change_dir_prop;

  /* Set up the edit baton. */
  my_edit_baton = apr_pcalloc (pool, sizeof (*my_edit_baton));
  my_edit_baton->node_pool = node_pool;
  my_edit_baton->pool = pool;
  my_edit_baton->fs = repos->fs;
  my_edit_baton->root = root;
  my_edit_baton->base_root = base_root;

  *editor = my_editor;
  *edit_baton = my_edit_baton;

  return SVN_NO_ERROR;
}



svn_repos_node_t *
svn_repos_node_from_baton (void *edit_baton)
{
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  return eb->node;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
