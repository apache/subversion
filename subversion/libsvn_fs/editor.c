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
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "dag.h"
#include "err.h"


/*** Editor batons. ***/

struct edit_baton
{
  apr_pool_t *pool;

  /* Supplied when the editor is created: */

  /* Commit message for this commit. */
  svn_string_t *log_msg;

  /* Hook to run when when the commit is done. */
  svn_fs_commit_hook_t *hook;
  void *hook_baton;

  /* The already-open svn filesystem to commit to. */
  svn_fs_t *fs;

  /* Location in fs where where the edit will begin. */
  svn_string_t *base_path;

  /* Created during the edit: */

  /* svn transaction associated with this edit (created in replace_root). */
  svn_fs_txn_t *txn;

  /* The object representing the root directory of the svn txn. */
  svn_fs_root_t *txn_root;

};


struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent;

  svn_revnum_t base_rev;  /* the revision of this dir in the wc */

  svn_string_t *path;  /* the -absolute- path to this dir in the fs */

};


struct file_baton
{
  struct dir_baton *parent;

  svn_string_t *path;  /* the -absolute- path to this file in the fs */

  apr_pool_t *subpool;  /* used by apply_textdelta() */

};



/*** Editor functions ***/

static svn_error_t *
replace_root (void *edit_baton,
              svn_revnum_t base_revision,
              void **root_baton)
{
  /* ben sez: kff todo: figure out breaking into subpools soon */
  struct edit_baton *eb = edit_baton;
  struct dir_baton *dirb = apr_pcalloc (eb->pool, sizeof (*dirb));

  /* Begin a subversion transaction, cache its name, and get its
     root object. */
  SVN_ERR (svn_fs_begin_txn (&(eb->txn), eb->fs, base_revision, eb->pool));
  SVN_ERR (svn_fs_txn_root (&(eb->txn_root), eb->txn, eb->pool));
  
  /* Finish filling out the root dir baton.  The `base_path' field is
     an -absolute- path in the filesystem, upon which all dir batons
     will telescope.  */
  dirb->edit_baton = edit_baton;
  dirb->base_rev = base_revision;
  dirb->parent = NULL;
  dirb->path = svn_string_dup (eb->base_path, eb->pool);
 
  *root_baton = dirb;
  return SVN_NO_ERROR;
}



static svn_error_t *
delete_entry (svn_string_t *name,
              void *parent_baton)
{
  struct dir_baton *parent = parent_baton;
  struct edit_baton *eb = parent->edit_baton;

  svn_string_t *path_to_kill = svn_string_dup (parent->path, eb->pool);
  svn_path_add_component (path_to_kill, name, svn_path_repos_style);

  /* This routine is a mindless wrapper. */
  SVN_ERR (svn_fs_delete (eb->txn_root, path_to_kill->data, eb->pool));

  return SVN_NO_ERROR;
}




static svn_error_t *
add_directory (svn_string_t *name,
               void *parent_baton,
               svn_string_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **child_baton)
{
  struct dir_baton *new_dirb;
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  /* Sanity check. */  
  if (copyfrom_path && (copyfrom_revision <= 0))
    return 
      svn_error_createf 
      (SVN_ERR_FS_GENERAL, 0, NULL, eb->pool,
       "fs editor: add_dir `%s': got copyfrom_path, but no copyfrom_rev",
       name->data);

  /* Build a new dir baton for this directory */
  new_dirb = apr_pcalloc (eb->pool, sizeof (*new_dirb));
  new_dirb->edit_baton = eb;
  new_dirb->parent = pb;
  new_dirb->path = svn_string_dup (pb->path, eb->pool);
  svn_path_add_component (new_dirb->path, name, svn_path_repos_style);

  if (copyfrom_path)
    {
      /* If the driver supplied ancestry args, the filesystem can make a
         "cheap copy" under the hood... how convenient! */
      svn_fs_root_t *copyfrom_root;

      SVN_ERR (svn_fs_revision_root (&copyfrom_root, eb->fs,
                                     copyfrom_revision, eb->pool));

      SVN_ERR (svn_fs_copy (copyfrom_root, copyfrom_path->data,
                            eb->txn_root, new_dirb->path->data, eb->pool));

      /* And don't forget to fill out the the dir baton */
      new_dirb->base_rev = copyfrom_revision;
    }
  else
    {
      /* No ancestry given, just make a new directory. */      
      SVN_ERR (svn_fs_make_dir (eb->txn_root, new_dirb->path->data, eb->pool));

      /* Inherent revision from parent. */
      new_dirb->base_rev = pb->base_rev;
    }

  *child_baton = new_dirb;
  return SVN_NO_ERROR;
}



static svn_error_t *
replace_directory (svn_string_t *name,
                   void *parent_baton,
                   svn_revnum_t base_revision,
                   void **child_baton)
{
  struct dir_baton *new_dirb;
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  /* Build a new dir baton for this directory */
  new_dirb = apr_pcalloc (eb->pool, sizeof (*new_dirb));
  new_dirb->edit_baton = eb;
  new_dirb->parent = pb;
  new_dirb->path = svn_string_dup (pb->path, eb->pool);
  svn_path_add_component (new_dirb->path, name, svn_path_repos_style);

  /* If this dir is at a different revision than its parent, make a
     cheap copy into our transaction. */
  if (base_revision != pb->base_rev)
    {
      svn_fs_root_t *other_root;
      /* First we have to remove the subtree in our current txn. */
      SVN_ERR (svn_fs_delete_tree (eb->txn_root, new_dirb->path->data,
                                   eb->pool));

      /* Now copy in the subtree from the other revision. */
      SVN_ERR (svn_fs_revision_root (&other_root, eb->fs,
                                     base_revision, eb->pool));
      SVN_ERR (svn_fs_copy (other_root, new_dirb->path->data,
                            eb->txn_root, new_dirb->path->data, eb->pool));
    }
  else
    /* If it's the same rev as parent, just inherit the rev_root. */
    new_dirb->base_rev = pb->base_rev;

  *child_baton = new_dirb;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
  /* The fs doesn't give one whit that we're done making changes to
     any particular directory... it's all happening inside one svn
     transaction tree.

     Thus this routine is a no-op! */

  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *fb = file_baton;

  /* Free any memory used while streamily writing file contents. */
  apr_pool_destroy (fb->subpool);
  
  return SVN_NO_ERROR;
}



static svn_error_t *
apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t **handler,
                 void **handler_baton)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->parent->edit_baton;
  
  /* This routine is a mindless wrapper. */
  SVN_ERR (svn_fs_apply_textdelta (handler, handler_baton,
                                   eb->txn_root, fb->path->data,
                                   fb->subpool));
  
  return SVN_NO_ERROR;
}




static svn_error_t *
add_file (svn_string_t *name,
          void *parent_baton,
          svn_string_t *copy_path,
          long int copy_revision,
          void **file_baton)
{
  struct file_baton *new_fb;
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  /* Sanity check. */  
  if (copy_path && (copy_revision <= 0))
    return 
      svn_error_createf 
      (SVN_ERR_FS_GENERAL, 0, NULL, eb->pool,
       "fs editor: add_file `%s': got copy_path, but no copy_rev",
       name->data);

  /* Build a new file baton */
  new_fb = apr_pcalloc (eb->pool, sizeof (*new_fb));
  new_fb->parent = pb;
  new_fb->subpool = svn_pool_create (eb->pool);
  new_fb->path = svn_string_dup (pb->path, new_fb->subpool);
  svn_path_add_component (new_fb->path, name, svn_path_repos_style);

  if (copy_path)
    {
      /* If the driver supplied ancestry args, the filesystem can make a
         "cheap copy" under the hood... how convenient! */
      svn_fs_root_t *copy_root;

      SVN_ERR (svn_fs_revision_root (&copy_root, eb->fs,
                                     copy_revision, eb->pool));

      SVN_ERR (svn_fs_copy (copy_root, copy_path->data,
                            eb->txn_root, new_fb->path->data, eb->pool));
    }
  else
    {
      /* No ancestry given, just make a new file. */      
      SVN_ERR (svn_fs_make_file (eb->txn_root, new_fb->path->data, eb->pool));
    }

  *file_baton = new_fb;
  return SVN_NO_ERROR;
}




static svn_error_t *
replace_file (svn_string_t *name,
              void *parent_baton,
              svn_revnum_t base_revision,
              void **file_baton)
{
  struct file_baton *new_fb;
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  /* Build a new file baton */
  new_fb = apr_pcalloc (eb->pool, sizeof (*new_fb));
  new_fb->parent = pb;
  new_fb->subpool = svn_pool_create (eb->pool);
  new_fb->path = svn_string_dup (pb->path, new_fb->subpool);
  svn_path_add_component (new_fb->path, name, svn_path_repos_style);

  /* If this file is at a different revision than its parent, make a
     cheap copy into our transaction. */
  if (base_revision != pb->base_rev)
    {
      svn_fs_root_t *other_root;
      /* First we have to remove the file in our current txn. */
      SVN_ERR (svn_fs_delete (eb->txn_root, new_fb->path->data, eb->pool));

      /* Now copy in the file from the other revision. */
      SVN_ERR (svn_fs_revision_root (&other_root, eb->fs,
                                     base_revision, eb->pool));
      SVN_ERR (svn_fs_copy (other_root, new_fb->path->data,
                            eb->txn_root, new_fb->path->data, eb->pool));
    }


  *file_baton = new_fb;
  return SVN_NO_ERROR;
}



static svn_error_t *
change_file_prop (void *file_baton,
                  svn_string_t *name,
                  svn_string_t *value)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->parent->edit_baton;

  /* This routine is a mindless wrapper. */
  SVN_ERR (svn_fs_change_node_prop (eb->txn_root, fb->path->data,
                                    name, value, eb->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_string_t *name,
                 svn_string_t *value)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;

  /* This routine is a mindless wrapper. */
  SVN_ERR (svn_fs_change_node_prop (eb->txn_root, db->path->data,
                                    name, value, eb->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;
  svn_revnum_t new_revision = SVN_INVALID_REVNUM;
  svn_error_t *err;
  const char *conflict;

  err = svn_fs_commit_txn (&conflict, &new_revision, eb->txn);

  if (err)
    {
      /* ### todo: we should check whether it really was a conflict,
         and return the conflict info if so? */

      /* If the commit failed, it's *probably* due to an out-of-date
         conflict.  Now, the filesystem gives us the ability to
         continue diddling the transaction and try again; but let's
         face it: that's not how the cvs or svn works from a user
         interface standpoint.  Thus we don't make use of this fs
         feature (for now, at least.)

         So, in a nutshell: svn commits are an all-or-nothing deal.
         Each commit creates a new fs txn which either succeeds or is
         aborted completely.  No second chances;  the user simply
         needs to update and commit again  :) */

      SVN_ERR (svn_fs_abort_txn (eb->txn));
      return err;
    }

  /* The commit succeeded.  Save the log message as a property of the
     new revision.

     TODO:  What if we crash right at this line?  We'd have a new
     revision with no log message.  In the future, we need to make the
     log message part of the same db txn that executes within
     svn_fs_commit_txn -- probably by passing it right in.  */
  SVN_ERR (svn_fs_change_rev_prop (eb->fs, new_revision,
                                   svn_string_create (SVN_PROP_REVISION_LOG,
                                                      eb->pool),
                                   eb->log_msg, eb->pool));

  /* Pass the new revision number to the caller's hook. */
  SVN_ERR ((*eb->hook) (new_revision, eb->hook_baton));

  return SVN_NO_ERROR;
}



/*** Public interface. ***/

svn_error_t *
svn_fs_get_editor (svn_delta_edit_fns_t **editor,
                   void **edit_baton,
                   svn_fs_t *fs,
                   svn_string_t *base_path,
                   svn_string_t *log_msg,
                   svn_fs_commit_hook_t *hook,
                   void *hook_baton,
                   apr_pool_t *pool)
{
  svn_delta_edit_fns_t *e = svn_delta_default_editor (pool);
  apr_pool_t *subpool = svn_pool_create (pool);
  struct edit_baton *eb = apr_pcalloc (subpool, sizeof (*eb));

  SVN_ERR (svn_fs__check_fs (fs));

  /* Set up the editor. */
  e->replace_root      = replace_root;
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
  eb->base_path = svn_string_dup (base_path, subpool);
  eb->fs = fs;

  *edit_baton = eb;
  *editor = e;
  
  return SVN_NO_ERROR;
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
