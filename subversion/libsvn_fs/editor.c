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

/* This is on hold until the fs is robust and complete, at which point
   this editor will be written using the public svn_fs.h functions
   (i.e., the same tree.c stuff the networking layer uses).  Exercise
   those code paths!  Exorcise those code paths! */

#if 0  /* till end of file */

#include "apr_pools.h"
#include "apr_file_io.h"

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "dag.h"



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
  svn_revnum_t base_rev;
  svn_string_t *base_path;

  /* Created during the edit: */

  /* svn transaction associated with this edit (created in replace_root). */
  svn_fs_txn_t *txn;
  const char *txn_name;

  /* The object representing the root directory of the svn txn. */
  svn_fs_root_t *root;

};


struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent;

  svn_string_t *path;  /* the -absolute- path to this dir in the fs */
  svn_string_t *name;  /* basename of the field above */

};


struct file_baton
{
  struct dir_baton *parent;

  svn_string_t *path;  /* the -absolute- path to this file in the fs */
  svn_string_t *name;  /* basename of the field above */

};



/*** Editor functions ***/

static svn_error_t *
replace_root (void *edit_baton,
              svn_revnum_t base_revision,
              void **root_baton)
{
  /* kff todo: figure out breaking into subpools soon */
  struct edit_baton *eb = edit_baton;
  struct dir_baton *dirb = apr_pcalloc (eb->pool, sizeof (*dirb));

  /* Begin a -subversion- transaction, cache its name, and get its
     root object. */
  SVN_ERR (svn_fs_begin_txn (&(eb->txn), eb->fs, eb->base_rev, eb->pool));
  SVN_ERR (svn_fs_txn_name (&(eb->txn_name), eb->txn, eb->pool));
  SVN_ERR (svn_fs_txn_root (&(dirb->root), eb->txn, eb->pool));
  
  /* Finish filling out the root dir baton. */
  dirb->edit_baton = edit_baton;
  dirb->parent = NULL;
  dirb->base_path = svn_string_dup (eb->base_path, eb->pool);
  /* ben todo:  do we really need a dirb->name field? */
 
  *root_baton = dirb;
  return SVN_NO_ERROR;
}



static svn_error_t *
delete_entry (svn_string_t *name,
              void *parent_baton)
{
  struct dir_baton *parent = parent_baton;
  struct edit_baton *eb = dirb->edit_baton;

  svn_string_t *path_to_kill = svn_string_dup (parent->path, eb->pool);
  svn_string_append (path_to_kill, name);

  SVN_ERR (svn_fs_delete (eb->root, path_to_kill, eb->pool));

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
  struct dir_baton *new_dirb
    = apr_pcalloc (pb->edit_baton->pool, sizeof (*new_dirb));
  struct add_repl_args add_args;
  
  add_args.parent = pb;
  add_args.name = name;
  
  SVN_ERR (svn_fs__retry_txn (pb->edit_baton->fs,
                              txn_body_add_directory,
                              &add_args,
                              pb->edit_baton->pool));

  new_dirb->edit_baton = pb->edit_baton;
  new_dirb->parent = pb;
  new_dirb->base_rev = ancestor_revision;
  new_dirb->base_path = ancestor_path;
  new_dirb->name = svn_string_dup (name, pb->edit_baton->pool);
  new_dirb->node = add_args.new_node;

  *child_baton = new_dirb;
  return SVN_NO_ERROR;
}



static svn_error_t *
replace_directory (svn_string_t *name,
                   void *parent_baton,
                   svn_revnum_t base_revision,
                   void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct dir_baton *dirb = apr_pcalloc (pb->edit_baton->pool, sizeof (*dirb));
  struct add_repl_args repl_args;
  
  repl_args.parent = pb;
  repl_args.name   = name;

  SVN_ERR (svn_fs__retry_txn (pb->edit_baton->fs,
                              txn_body_replace_directory,
                              &repl_args,
                              pb->edit_baton->pool));

  dirb->edit_baton = pb->edit_baton;
  dirb->parent = pb;
  dirb->base_rev = base_revision;
  dirb->base_path = NULL;
  dirb->name = svn_string_dup (name, pb->edit_baton->pool);
  dirb->node = repl_args.new_node;

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
window_handler (svn_txdelta_window_t *window, void *handler_baton)
{
#if 0
  struct handle_txdelta_args *txdelta_args = handler_baton;

  /* fooo */
#endif /* 0 */

  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t **handler,
                 void **handler_baton)
{
  struct file_baton *fb = file_baton;
  struct handle_txdelta_args txdelta_args;

  txdelta_args.fb = fb;

  /* fooo; */

  /* Get the base against to which the incoming delta should be
     applied to produce the new file. */
  SVN_ERR (svn_fs__retry_txn (fb->parent->edit_baton->fs,
                              txn_body_get_base_contents,
                              &txdelta_args,
                              fb->parent->edit_baton->pool));

  
  *handler = window_handler;
  *handler_baton = &txdelta_args;
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
  struct file_baton *new_fb
    = apr_pcalloc (pb->edit_baton->pool, sizeof (*new_fb));
  struct add_repl_args add_args;

  add_args.parent = pb;
  add_args.name = name;

  SVN_ERR (svn_fs__retry_txn (pb->edit_baton->fs,
                              txn_body_add_file,
                              &add_args,
                              pb->edit_baton->pool));

  new_fb->parent = pb;
  new_fb->name = svn_string_dup (name, pb->edit_baton->pool);
  new_fb->base_rev = ancestor_revision;
  new_fb->base_path = ancestor_path;
  new_fb->node = add_args.new_node;

  *file_baton = new_fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
txn_body_replace_file (void *rargs, trail_t *trail)
{
  struct add_repl_args *repl_args = rargs;

  SVN_ERR (svn_fs__dag_clone_child (&(repl_args->new_node),
                                    repl_args->parent->node,
                                    repl_args->name->data,
                                    trail));
  
  if (! svn_fs__dag_is_file (repl_args->new_node))
    {
      return svn_error_createf (SVN_ERR_FS_NOT_DIRECTORY,
                                0,
                                NULL,
                                trail->pool,
                                "trying to replace directory, but %s "
                                "is not a directory",
                                repl_args->name->data);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_file (svn_string_t *name,
              void *parent_baton,
              svn_revnum_t base_revision,
              void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *fb = apr_pcalloc (pb->edit_baton->pool, sizeof (*fb));
  struct add_repl_args repl_args;

  repl_args.parent = pb;
  repl_args.name   = name;

  SVN_ERR (svn_fs__retry_txn (pb->edit_baton->fs, txn_body_replace_file,
                              &repl_args, pb->edit_baton->pool));

  fb->parent = pb;
  fb->base_rev = base_revision;
  fb->base_path = NULL;
  fb->name = svn_string_dup (name, pb->edit_baton->pool);
  fb->node = repl_args.new_node;

  *file_baton = fb;
  return SVN_NO_ERROR;
}


struct change_prop_args
{
  dag_node_t *node;
  svn_string_t *name;
  svn_string_t *value;
};


static svn_error_t *
txn_body_change_prop (void *void_args, trail_t *trail)
{
  struct change_prop_args *args = void_args;
  skel_t *proplist;
  skel_t *this, *last;
  svn_string_t *name = args->name, *value = args->value;
  svn_boolean_t found_it;

  /* todo: we should decide if change_file_prop()'s semantics require
     an error if deleting a non-existent property. */

  SVN_ERR (svn_fs__dag_get_proplist (&proplist, args->node, trail));

  /* From structure:
   *
   *   PROPLIST ::= (PROP ...) ;
   *      PROP ::= atom atom ;
   * 
   * The proplist returned by svn_fs__dag_get_proplist is guaranteed
   * to be well-formed, so we don't bother to error check as we walk
   * it.
   */
  for (this = proplist->children, last = proplist->children;
       this != NULL;
       last = this, this = this->next->next)
    {
      if ((this->len == name->len)
          && (memcmp (this->data, name->data, name->len) == 0))
        {
          found_it = 1;
          
          if (value)  /* set a new value */
            {
              skel_t *value_skel = this->next;
              value_skel->data = value->data;
              value_skel->len = value->len;
            }
          else        /* make the property disappear */
            {
              if (last == proplist->children)
                proplist->children = this->next->next;
              else
                last->next->next = this->next->next;
            }
          
          break;
        }
    }

  if (! found_it)
    {
      if (value)
        {
          skel_t *new_name_skel
            = svn_fs__mem_atom (name->data, name->len, trail->pool);
          skel_t *new_value_skel
            = svn_fs__mem_atom (value->data, value->len, trail->pool);

          svn_fs__prepend (new_value_skel, proplist);
          svn_fs__prepend (new_name_skel, proplist);
        }
      else
        {
          /* todo: see comment at start of function about erroring if try
             to delete a non-existent property */
        }
    }

  SVN_ERR (svn_fs__dag_set_proplist (args->node, proplist, trail));

  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_string_t *name,
                  svn_string_t *value)
{
  struct file_baton *fb = file_baton;
  struct change_prop_args args;

  args.node = fb->node;
  args.name = name;
  args.value = value;

  SVN_ERR (svn_fs__retry_txn (fb->parent->edit_baton->fs,
                              txn_body_change_prop, &args,
                              fb->parent->edit_baton->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_string_t *name,
                 svn_string_t *value)
{
  struct dir_baton *dirb = dir_baton;
  struct change_prop_args args;

  args.node = dirb->node;
  args.name = name;
  args.value = value;

  SVN_ERR (svn_fs__retry_txn (dirb->edit_baton->fs,
                              txn_body_change_prop, &args,
                              dirb->edit_baton->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;
  svn_revnum_t new_revision = SVN_INVALID_REVNUM;
  svn_error_t *err;

  err = svn_fs_commit_txn (&new_revision, eb->txn);
  if (err)
    {
      /* If the commit failed, it's *probably* due to an out-of-date
         conflict.  Now, the filesystem gives us the ability to
         continue diddling the transaction and try again; but let's
         face it: that's not how the cvs or svn works from a suser
         interface standpoint.  Thus we don't make use of this fs
         feature (for now, at least.)

         So, in a nutshell: svn commits are an all-or-nothing deal.
         Each commit creates a new fs txn which either succeeds or is
         aborted completely.  No second chances.  :) */

      SVN_ERR (svn_fs_abort_txn (eb->txn));
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
                   svn_revnum_t base_revision,
                   svn_string_t *base_path,
                   svn_string_t *log_msg,
                   svn_fs_commit_hook_t *hook,
                   void *hook_baton,
                   apr_pool_t *pool)
{
  svn_delta_edit_fns_t *e = svn_delta_default_editor (pool);
  apr_pool_t *subpool = svn_pool_create (pool);
  struct edit_baton *eb = apr_pcalloc (subpool, sizeof (*eb));

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
  eb->base_rev = base_revision;
  eb->base_path = svn_string_dup (base_path, subpool);

  *edit_baton = eb;
  *editor = e;
  
  return SVN_NO_ERROR;
}


#endif /* 0 */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
