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

  /* Transaction associated with this edit.
     This is zero until the driver calls replace_root.  */
  svn_fs_txn_t *txn;

  /* The txn name.  This is just the cached result of applying
     svn_fs_txn_name to TXN, above.
     This is zero until the driver calls replace_root.  */
  char *txn_name;

  /* The root directory of the transaction. */
  svn_fs_root_t *root_p;



  

  /* Subversion file system.
     Supplied by the user when we create the editor.  */
  svn_fs_t *fs;

  /* Existing revision number upon which this edit is based.
     Supplied by the user when we create the editor.  */
  svn_revnum_t base_rev;

  /* Commit message for this commit.
     Supplied by the user when we create the editor.  */
  svn_string_t *log_msg;

  /* Hook to run when when the commit is done. 
     Supplied by the user when we create the editor.  */
  svn_fs_commit_hook_t *hook;
  void *hook_baton;

};


struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent;
  svn_string_t *name;  /* just this entry, not full path */

  /* The revision we should base differences against. */
  svn_revnum_t base_rev;

  /* If non-null, base differences against this path at the
     base_revision specified above.  Else if null, the path to this
     node is implied.  (In the add_* editor calls, this var is called
     ancestor_path; I'm not clear on which is a better name.)  */
  svn_string_t *base_path;

  /* This directory, guaranteed to be mutable. */
  dag_node_t *node;
};


struct file_baton
{
  struct dir_baton *parent;
  svn_string_t *name;  /* just this entry, not full path */

  /* The revision we should base differences against. */
  svn_revnum_t base_rev;

  /* If non-null, base differences against this path at the
     base_revision specified above.  Else if null, the path to this
     node is implied.  (In the add_* editor calls, this var is called
     ancestor_path; I'm not clear on which is a better name.)  */
  svn_string_t *base_path;

  /* This file, guaranteed to be mutable. */
  dag_node_t *node;
};



/*** Editor functions and their helpers. ***/

/* Helper for replace_root. */
static svn_error_t *
txn_body_clone_root (void *dir_baton, trail_t *trail)
{
  struct dir_baton *dirb = dir_baton;

  SVN_ERR (svn_fs__dag_clone_root (&(dirb->node),
                                   dirb->edit_baton->fs,
                                   dirb->edit_baton->txn_name,
                                   trail));

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_root (void *edit_baton, svn_revnum_t base_revision, void **root_baton)
{
  /* kff todo: figure out breaking into subpools soon */
  struct edit_baton *eb = edit_baton;
  struct dir_baton *dirb = apr_pcalloc (eb->pool, sizeof (*dirb));

  /* Begin a transaction. */
  SVN_ERR (svn_fs_begin_txn (&(eb->txn), eb->fs, eb->base_rev, eb->pool));

  /* Cache the transaction's name. */
  SVN_ERR (svn_fs_txn_name (&(eb->txn_name), eb->txn, eb->pool));
  
  /* What don't we do?
   * 
   * What we don't do is start a single Berkeley DB transaction here,
   * keep it open throughout the entire edit, and then call
   * txn_commit() inside close_edit().  That would result in writers
   * interfering with writers unnecessarily.
   * 
   * Instead, we take small steps.  As the driver calls editing
   * functions to build the new tree from the old one, we clone each
   * node that is changed, using a separate Berkeley DB transaction
   * for each cloning.  When it's time to commit, we'll walk those
   * nodes (it doesn't matter in what order), looking for
   * irreconcileable conflicts but otherwise merging changes from
   * revisions committed since we started work into our transaction.
   * 
   * When our private tree is all in order, we lock a revision and
   * walk again, making sure the final merge states are sane.  Then we
   * mark them all as immutable and hook in the new root.
   */

  /* Set up the root directory baton, the last step of which is to get
     a new root directory for this txn, cloned from the root dir of
     the txn's base revision. */
  dirb->edit_baton = edit_baton;
  dirb->parent = NULL;
  dirb->name = svn_string_create ("", eb->pool);
  dirb->base_rev = eb->base_rev;
  SVN_ERR (svn_fs__retry_txn (eb->fs, txn_body_clone_root, dirb, eb->pool));

  /* kff todo: If there was any error, this transaction will have to
     be cleaned up, including removing its nodes from the nodes
     table. */

  *root_baton = dirb;
  return SVN_NO_ERROR;
}


/* Helper for delete_node and delete_entry. */
struct delete_args
{
  struct dir_baton *parent;
  svn_string_t *name;
};


/* Helper for delete_entry. */
static svn_error_t *
txn_body_delete (void *del_baton, trail_t *trail)
{
  struct delete_args *del_args = del_baton;

  SVN_ERR (svn_fs__dag_delete (del_args->parent->node,
                               del_args->name->data,
                               trail));

  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry (svn_string_t *name, void *parent_baton)
{
  struct dir_baton *dirb = parent_baton;
  struct edit_baton *eb = dirb->edit_baton;
  struct delete_args del_args;
  
  del_args.parent = dirb;
  del_args.name   = name;
  
  SVN_ERR (svn_fs__retry_txn (eb->fs, txn_body_delete, &del_args, eb->pool));

  return SVN_NO_ERROR;
}


/* Helper for addition and replacement of files and directories. */
struct add_repl_args
{
  struct dir_baton *parent;  /* parent in which we're adding|replacing */
  svn_string_t *name;        /* name of what we're adding|replacing */
  dag_node_t *new_node;      /* what we added|replaced */
};


/* Helper for add_directory. */
static svn_error_t *
txn_body_add_directory (void *add_baton, trail_t *trail)
{
  struct add_repl_args *add_args = add_baton;
  
  SVN_ERR (svn_fs__dag_make_dir (&(add_args->new_node),
                                 add_args->parent->node,
                                 add_args->name->data,
                                 trail));

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
txn_body_replace_directory (void *rargs, trail_t *trail)
{
  struct add_repl_args *repl_args = rargs;

  SVN_ERR (svn_fs__dag_clone_child (&(repl_args->new_node),
                                    repl_args->parent->node,
                                    repl_args->name->data,
                                    trail));

  if (! svn_fs__dag_is_directory (repl_args->new_node))
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
  /* One might be tempted to make this function mark the directory as
     immutable; that way, if the traversal order is violated somehow,
     we'll get an error the second time we visit the directory.

     However, that would be incorrect --- the node must remain
     mutable, since we may have to merge changes into it before we can
     commit the transaction.  

     Thank you, Jim, for adding that second paragraph. :-)  Well, I
     can't think of anything for close_directory to do.  Is it really
     a no-op?  How delightfully demulcent. */

  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
  /* This function could mark the file as immutable, since even the
     final pre-commit merge doesn't touch file contents.  (See the
     comment above in `close_directory'.)  */
  return SVN_NO_ERROR;
}


/* Helper for txn_body_get_base_contents and txn_body_handle_window. */
struct handle_txdelta_args
{
  struct file_baton *fb;
  dag_node_t *base_node;
};


/* Helper for apply_txdelta, and indirectly for window_handler. */
static svn_error_t *
txn_body_get_base_contents (void *args, trail_t *trail)
{
#if 0
  struct handle_txdelta_args *txdelta_args = args;

  /* Make txdelta_args->base_node be a dag_node_t for the immutable
     base node ancestral to the file-in-progress. */
#endif /* 0 */

  return SVN_NO_ERROR;
}


/* Helper for window_handler. */
static svn_error_t *
txn_body_handle_window (void *handler_baton, trail_t *trail)
{
#if 0
  struct handlle_txdelta_args *txdelta_args = handler_baton;

  /* Accumulate more and more of the new contents as each window is
   * received, patching txdelta_args->fb->node based on
   * txdelta_args->base_node's contents.
   *
   * When receive the null window, write out txdelta_args->fb->node to
   * the database.
   */
#endif /* 0 */

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


/* Helper for add_file. */
static svn_error_t *
txn_body_add_file (void *add_baton, trail_t *trail)
{
  struct add_repl_args *add_args = add_baton;

  SVN_ERR (svn_fs__dag_make_file (&(add_args->new_node),
                                  add_args->parent->node,
                                  add_args->name->data,
                                  trail));

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

  SVN_ERR (svn_fs_commit_txn (&new_revision, eb->txn));
  SVN_ERR ((*eb->hook) (new_revision, eb->hook_baton));

  return SVN_NO_ERROR;
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
