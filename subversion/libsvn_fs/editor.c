/* editor.c --- a tree editor for commiting changes to a filesystem.
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include "apr_pools.h"
#include "apr_file_io.h"

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "dag.h"


/*** The editor functions. ***/
struct edit_baton
{
  apr_pool_t *pool;

  /* Subversion file system */
  svn_fs_t *fs;

  /* Transaction associated with this edit */
  svn_fs_txn_t *txn;

  /* Existing revision number upon which this edit is based */
  svn_revnum_t base_rev;

  /* Commit message for this commit. */
  svn_string_t *log_msg;

  /* Hook to run when when the commit is done. */
  svn_fs_commit_hook_t *hook;
  void *hook_baton;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent;
  svn_string_t *name;  /* just this entry, not full path */

  /* This directory. */
  dag_node_t *node;

  /* Revision number of this directory */
  svn_revnum_t base_rev;
};


struct file_baton
{
  struct dir_baton *parent;
  svn_string_t *name;  /* just this entry, not full path */

  /* This file. */
  dag_node_t *node;

  /* Revision number of this file */
  svn_revnum_t base_rev;
};


static svn_error_t *
begin_edit (void *edit_baton, void **root_baton)
{
  /* kff todo: figure out breaking into subpools soon */

  struct edit_baton *eb = edit_baton;
  struct dir_baton *dirb = apr_pcalloc (eb->pool, sizeof (*dirb));
  svn_error_t *err;
  trail_t *trail = apr_pcalloc (eb->pool, sizeof (*trail));

  dirb->edit_baton = edit_baton;
  dirb->parent = NULL;
  dirb->name = svn_string_create ("", eb->pool);
  dirb->base_rev = eb->base_rev;

  /* Any trail setup necessary?  I think that all happens in
     svn_fs__dag_revision_root() and/or dag_clone_root().. */

  /* Begin a transaction. */
  err = svn_fs_begin_txn (&(eb->txn), eb->fs, eb->base_rev, eb->pool);
  if (err) return err;
  
  /* Get the root directory of the revision, immutable for now, but
     that may change, watch this space, sign our guestbook... */
  err = svn_fs__dag_revision_root (&(dirb->node), eb->fs,
                                   dirb->base_rev, trail);
  if (err) return err;

  *root_baton = dirb;
  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry (svn_string_t *name, void *parent_baton)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (svn_string_t *name,
               void *parent_baton,
               svn_string_t *ancestor_path,
               long int ancestor_revision,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct dir_baton *dirb = apr_pcalloc (pb->edit_baton->pool, sizeof (*dirb));
  
  dirb->parent = pb;
  dirb->edit_baton = pb->edit_baton;
  dirb->name = svn_string_dup (name, pb->edit_baton->pool);

  *child_baton = dirb;
  return SVN_NO_ERROR;
}


static svn_error_t *
replace_directory (svn_string_t *name,
                   void *parent_baton,
                   svn_string_t *ancestor_path,
                   long int ancestor_revision,
                   void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct dir_baton *dirb = apr_pcalloc (pb->edit_baton->pool, sizeof (*dirb));
  
  dirb->parent = pb;
  dirb->edit_baton = pb->edit_baton;
  dirb->name = svn_string_dup (name, pb->edit_baton->pool);

  *child_baton = dirb;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
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
  *handler = window_handler;
  *handler_baton = file_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (svn_string_t *name,
          void *parent_baton,
          svn_string_t *ancestor_path,
          long int ancestor_revision,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *fb = apr_pcalloc (pb->edit_baton->pool, sizeof (*fb));

  fb->parent = pb;
  fb->name = svn_string_dup (name, pb->edit_baton->pool);

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
  struct dir_baton *pb = parent_baton;
  struct file_baton *fb = apr_pcalloc (pb->edit_baton->pool, sizeof (*fb));

  fb->parent = pb;
  fb->name = svn_string_dup (name, pb->edit_baton->pool);

  *file_baton = fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_string_t *name,
                  svn_string_t *value)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_string_t *name,
                 svn_string_t *value)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  svn_error_t *err;
  struct edit_baton *eb = edit_baton;
  svn_revnum_t new_revision = SVN_INVALID_REVNUM;

  err = svn_fs_commit_txn (&new_revision, eb->txn);
  if (err) return err;

  err = (*eb->hook) (new_revision, eb->hook_baton);
  return err;
}



/*** Public interface. ***/

svn_error_t *
svn_fs_get_editor (svn_delta_edit_fns_t **editor,
                   void **edit_baton,
                   svn_fs_t *fs,
                   svn_revnum_t base_revision,
                   svn_string_t *log_msg,
                   svn_fs_commit_hook_t *hook,
                   void *hook_baton,
                   apr_pool_t *pool)
{
  svn_delta_edit_fns_t *e = svn_delta_default_editor (pool);
  apr_pool_t *subpool = svn_pool_create (pool);
  struct edit_baton *eb = apr_pcalloc (subpool, sizeof (*eb));

  /* Set up the editor. */
  e->begin_edit        = begin_edit;
  e->delete_entry      = delete_entry;
  e->add_directory     = add_directory;
  e->replace_directory = replace_directory;
  e->change_dir_prop   = change_dir_prop;
  e->close_directory   = close_directory;
  e->add_file          = add_file;
  e->replace_file      = replace_file;
  e->apply_textdelta   = apply_textdelta;
  e->change_file_prop  = change_file_prop;
  e->close_file        = close_file;
  e->close_edit        = close_edit;

  /* Set up the edit baton. */
  eb->pool = subpool;
  eb->log_msg = svn_string_dup (log_msg, subpool);
  eb->hook = hook;
  eb->hook_baton = hook_baton;
  eb->base_rev = base_revision;

  *edit_baton = eb;
  *editor = e;
  
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
