/* commit-editor.c --- editor for commiting changes to a filesystem.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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


#include <string.h>

#include "apr_pools.h"
#include "apr_file_io.h"

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "ra_local.h"



/*** Editor batons. ***/

struct edit_baton
{
  apr_pool_t *pool;

  /** Supplied when the editor is created: **/

  /* The active RA session */
  svn_ra_local__session_baton_t *session;

  /* The user doing the commit.  Presumably, some higher layer has
     already authenticated this user. */
  const char *user;

  /* Commit message for this commit. */
  svn_string_t log_msg;

  /* Hook to run when when the commit is done. */
  svn_ra_local__commit_hook_t *hook;
  void *hook_baton;

  /* The already-open svn repository to commit to. */
  svn_repos_t *repos;

  /* The filesystem associated with the REPOS above (here for
     convenience). */
  svn_fs_t *fs;

  /* Location in fs where where the edit will begin. */
  svn_stringbuf_t *base_path;

  /** Created during the edit: **/

  /* svn transaction associated with this edit (created in open_root). */
  svn_fs_txn_t *txn;

  /* The object representing the root directory of the svn txn. */
  svn_fs_root_t *txn_root;

  /* The name of the transaction. */
  const char *txn_name;

  /** Filled in when the edit is closed: **/

  /* The new revision created by this commit. */
  svn_revnum_t *new_rev;

  /* The date (according to the repository) of this commit. */
  const char **committed_date;

  /* The author (also according to the repository) of this commit. */
  const char **committed_author;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent;
  svn_stringbuf_t *path; /* the -absolute- path to this dir in the fs */
  apr_pool_t *subpool;   /* my personal subpool, in which I am allocated. */
  int ref_count;         /* how many still-open batons depend on my pool. */
};


struct file_baton
{
  struct dir_baton *parent;
  svn_stringbuf_t *path; /* the -absolute- path to this file in the fs */
  apr_pool_t *subpool;   /* used by apply_textdelta() */
};



/* Helper function:  knows when to free dir batons. */
static svn_error_t *
decrement_dir_ref_count (struct dir_baton *db)
{
  if (db == NULL)
    return SVN_NO_ERROR;

  db->ref_count--;

  /* Check to see if *any* child batons still depend on this
     directory's pool. */
  if (db->ref_count == 0)
    {
      struct dir_baton *dbparent = db->parent;

      /* Destroy all memory used by this baton, including the baton
         itself! */
      svn_pool_destroy (db->subpool);
      
      /* Tell your parent that you're gone. */
      SVN_ERR (decrement_dir_ref_count (dbparent));
    }

  return SVN_NO_ERROR;
}


/* Create and return a generic out-of-dateness error. */
static svn_error_t *
out_of_date (const char *path, const char *txn_name, apr_pool_t *pool)
{
  return svn_error_createf (SVN_ERR_TXN_OUT_OF_DATE, 0, NULL, pool, 
                            "out of date: `%s' in txn `%s'", path, txn_name);
}



/*** Editor functions ***/

static svn_error_t *
open_root (void *edit_baton,
           svn_revnum_t base_revision,
           void **root_baton)
{
  apr_pool_t *subpool;
  struct dir_baton *dirb;
  struct edit_baton *eb = edit_baton;

  /* Ignore BASE_REVISION.  We always build our transaction against
     HEAD. */
  SVN_ERR (svn_fs_youngest_rev (&base_revision, eb->fs, eb->pool));

  /* Begin a subversion transaction, cache its name, and get its
     root object. */
  SVN_ERR (svn_repos_fs_begin_txn_for_commit (&(eb->txn), 
                                              eb->repos, 
                                              base_revision, 
                                              eb->user, 
                                              &(eb->log_msg),
                                              eb->pool));
  SVN_ERR (svn_fs_txn_root (&(eb->txn_root), eb->txn, eb->pool));
  SVN_ERR (svn_fs_txn_name (&(eb->txn_name), eb->txn, eb->pool));
  
  /* Finish filling out the root dir baton.  The `base_path' field is
     an -absolute- path in the filesystem, upon which all dir batons
     will telescope.  */
  subpool = svn_pool_create (eb->pool);
  dirb = apr_pcalloc (subpool, sizeof (*dirb));
  dirb->edit_baton = edit_baton;
  dirb->parent = NULL;
  dirb->subpool = subpool;
  dirb->path = svn_stringbuf_dup (eb->base_path, dirb->subpool);
  dirb->ref_count = 1;

  *root_baton = dirb;
  return SVN_NO_ERROR;
}



static svn_error_t *
delete_entry (svn_stringbuf_t *name,
              svn_revnum_t revision,
              void *parent_baton)
{
  struct dir_baton *parent = parent_baton;
  struct edit_baton *eb = parent->edit_baton;
  svn_node_kind_t kind;
  svn_revnum_t cr_rev;
  svn_stringbuf_t *path = svn_stringbuf_dup (parent->path, parent->subpool);
  svn_path_add_component (path, name);

  /* Check PATH in our transaction.  */
  kind = svn_fs_check_path (eb->txn_root, path->data, parent->subpool);

  /* If PATH doesn't exist in the txn, that's fine (merge
     allows this). */
  if (kind == svn_node_none)
    return SVN_NO_ERROR;

  /* Now, make sure we're deleting the node we *think* we're
     deleting, else return an out-of-dateness error. */
  SVN_ERR (svn_fs_node_created_rev (&cr_rev, eb->txn_root, path->data,
                                    parent->subpool));
  if (SVN_IS_VALID_REVNUM (revision) && (revision < cr_rev))
    return out_of_date (path->data, eb->txn_name, parent->subpool);
  
  /* This routine is a mindless wrapper.  We call svn_fs_delete_tree
     because that will delete files and recursively delete
     directories.  */
  return svn_fs_delete_tree (eb->txn_root, path->data, parent->subpool);
}




static svn_error_t *
add_directory (svn_stringbuf_t *name,
               void *parent_baton,
               svn_stringbuf_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **child_baton)
{
  struct dir_baton *new_dirb;
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  apr_pool_t *subpool = svn_pool_create (pb->subpool);
  svn_stringbuf_t *path = svn_stringbuf_dup (pb->path, subpool);
  svn_path_add_component (path, name);

  /* Sanity check. */  
  if (copyfrom_path && (copyfrom_revision <= 0))
    return 
      svn_error_createf 
      (SVN_ERR_FS_GENERAL, 0, NULL, eb->pool,
       "fs editor: add_dir `%s': got copyfrom_path, but no copyfrom_rev",
       name->data);

  /* Build a new dir baton for this directory in a subpool of parent's
     pool. */
  new_dirb = apr_pcalloc (subpool, sizeof (*new_dirb));
  new_dirb->edit_baton = eb;
  new_dirb->parent = pb;
  new_dirb->ref_count = 1;
  new_dirb->subpool = subpool;
  new_dirb->path = path;
  
  /* Increment parent's refcount. */
  pb->ref_count++;

  if (copyfrom_path)
    {
      const svn_string_t *repos_path;
      const svn_string_t *fs_path;
      svn_fs_root_t *copyfrom_root;
      svn_node_kind_t kind;

      /* Check PATH in our transaction.  Make sure it does not exist,
         else return an out-of-dateness error. */
      kind = svn_fs_check_path (eb->txn_root, path->data, subpool);
      if (kind != svn_node_none)
        return out_of_date (path->data, eb->txn_name, subpool);

      /* This add has history.  Let's split the copyfrom_url. */
      SVN_ERR (svn_ra_local__split_URL (&repos_path, &fs_path,
                                        copyfrom_path, new_dirb->subpool));

      /* For now, require that the url come from the same repository
         that this commit is operating on. */
      if (strcmp(eb->session->repos_path->data, repos_path->data) != 0)
            return 
              svn_error_createf 
              (SVN_ERR_FS_GENERAL, 0, NULL, eb->pool,
               "fs editor: add_dir`%s': copyfrom_url is from different repo",
               name->data);
      
      /* Now use the "fs_path" as an absolute path within the
         repository to make the copy from. */      
      SVN_ERR (svn_fs_revision_root (&copyfrom_root, eb->fs,
                                     copyfrom_revision, new_dirb->subpool));

      SVN_ERR (svn_fs_copy (copyfrom_root, fs_path->data,
                            eb->txn_root, new_dirb->path->data,
                            new_dirb->subpool));
    }
  else
    {
      /* No ancestry given, just make a new directory.  We don't
         bother with an out-of-dateness check here because
         svn_fs_make_dir will error out if PATH already exists.  */      
      SVN_ERR (svn_fs_make_dir (eb->txn_root, new_dirb->path->data,
                                new_dirb->subpool));
    }

  *child_baton = new_dirb;
  return SVN_NO_ERROR;
}



static svn_error_t *
open_directory (svn_stringbuf_t *name,
                void *parent_baton,
                svn_revnum_t base_revision,
                void **child_baton)
{
  struct dir_baton *new_dirb;
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  apr_pool_t *subpool = svn_pool_create (pb->subpool);
  svn_node_kind_t kind;
  svn_stringbuf_t *path = svn_stringbuf_dup (pb->path, subpool);
  svn_path_add_component (path, name);

  /* Check PATH in our transaction.  Make sure it does not exist,
     else return an out-of-dateness error. */
  kind = svn_fs_check_path (eb->txn_root, path->data, subpool);
  if (kind == svn_node_none)
    return out_of_date (path->data, eb->txn_name, subpool);

  /* Build a new dir baton for this directory */
  new_dirb = apr_pcalloc (subpool, sizeof (*new_dirb));
  new_dirb->edit_baton = eb;
  new_dirb->parent = pb;
  new_dirb->subpool = subpool;
  new_dirb->ref_count = 1;
  new_dirb->path = path;

  /* Increment parent's refcount. */
  pb->ref_count++;

  *child_baton = new_dirb;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
  /* Don't free the baton, just decrement its ref count.  If the
     refcount is 0, *then* it will be freed. */
  SVN_ERR (decrement_dir_ref_count (dir_baton));

  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *fb = file_baton;
  struct dir_baton *parent_baton = fb->parent;

  /* Destroy all memory used by this baton, including the baton
     itself! */
  svn_pool_destroy (fb->subpool);

  /* Tell the parent that one less subpool depends on its own pool. */
  SVN_ERR (decrement_dir_ref_count (parent_baton));

  return SVN_NO_ERROR;
}



static svn_error_t *
apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t *handler,
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
add_file (svn_stringbuf_t *name,
          void *parent_baton,
          svn_stringbuf_t *copy_path,
          long int copy_revision,
          void **file_baton)
{
  struct file_baton *new_fb;
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  apr_pool_t *subpool = svn_pool_create (pb->subpool);
  svn_stringbuf_t *path = svn_stringbuf_dup (pb->path, subpool);
  svn_path_add_component (path, name);

  /* Sanity check. */  
  if (copy_path && (copy_revision <= 0))
    return 
      svn_error_createf 
      (SVN_ERR_FS_GENERAL, 0, NULL, eb->pool,
       "fs editor: add_file `%s': got copy_path, but no copy_rev",
       name->data);

  /* Build a new file baton */
  new_fb = apr_pcalloc (subpool, sizeof (*new_fb));
  new_fb->parent = pb;
  new_fb->subpool = subpool;
  new_fb->path = path;

  /* Increment parent's refcount. */
  pb->ref_count++;

  if (copy_path)
    {      
      const svn_string_t *repos_path; 
      const svn_string_t *fs_path;
      svn_fs_root_t *copy_root;
      svn_node_kind_t kind;

      /* Check PATH in our transaction.  It had better not exist, or
         our transaction is out of date. */
      kind = svn_fs_check_path (eb->txn_root, path->data, subpool);
      if (kind != svn_node_none)
        return out_of_date (path->data, eb->txn_name, subpool);

      /* This add has history.  Let's split the copyfrom_url. */
      SVN_ERR (svn_ra_local__split_URL (&repos_path, &fs_path,
                                        copy_path, new_fb->subpool));

      /* For now, require that the url come from the same repository
         that this commit is operating on. */
      if (strcmp(eb->session->repos_path->data, repos_path->data) != 0)
            return 
              svn_error_createf 
              (SVN_ERR_FS_GENERAL, 0, NULL, eb->pool,
               "fs editor: add_file`%s': copyfrom_url is from different repo",
               name->data);
      
      /* Now use the "fs_path" as an absolute path within the
         repository to make the copy from. */      
      SVN_ERR (svn_fs_revision_root (&copy_root, eb->fs,
                                     copy_revision, new_fb->subpool));

      SVN_ERR (svn_fs_copy (copy_root, fs_path->data,
                            eb->txn_root, new_fb->path->data,
                            new_fb->subpool));
    }
  else
    {
      /* No ancestry given, just make a new, empty file.  Note that we
         don't perform an existence check here like the copy-from case
         does -- that's because svn_fs_make_file() already errors out
         if the file already exists.  */
      SVN_ERR (svn_fs_make_file (eb->txn_root, new_fb->path->data,
                                 new_fb->subpool));
    }

  *file_baton = new_fb;
  return SVN_NO_ERROR;
}




static svn_error_t *
open_file (svn_stringbuf_t *name,
           void *parent_baton,
           svn_revnum_t base_revision,
           void **file_baton)
{
  struct file_baton *new_fb;
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  svn_revnum_t cr_rev;
  apr_pool_t *subpool = svn_pool_create (pb->subpool);
  svn_stringbuf_t *path = svn_stringbuf_dup (pb->path, subpool);
  svn_path_add_component (path, name);

  /* Build a new file baton */
  new_fb = apr_pcalloc (subpool, sizeof (*new_fb));
  new_fb->parent = pb;
  new_fb->subpool = subpool;
  new_fb->path = path;
  
  /* Get this node's creation revision (doubles as an existence check). */
  SVN_ERR (svn_fs_node_created_rev (&cr_rev, eb->txn_root,
                                    path->data, new_fb->subpool));
  
  /* If the node our caller has has an older revision number than the
     one in our transaction, return an out-of-dateness error. */
  if (base_revision < cr_rev)
    return out_of_date (path->data, eb->txn_name, subpool);
  
  /* Increment parent's refcount. */
  pb->ref_count++;

  *file_baton = new_fb;
  return SVN_NO_ERROR;
}



static svn_error_t *
change_file_prop (void *file_baton,
                  svn_stringbuf_t *name,
                  svn_stringbuf_t *value)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->parent->edit_baton;
  svn_string_t propvalue;

  if (value)
    {
      propvalue.data = value->data;
      propvalue.len = value->len;
    }

  /* This routine is a mindless wrapper. */
  SVN_ERR (svn_fs_change_node_prop (eb->txn_root, fb->path->data, name->data, 
                                    value ? &propvalue : NULL, fb->subpool));

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_stringbuf_t *name,
                 svn_stringbuf_t *value)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;
  svn_string_t propvalue;
  propvalue.data = value->data;
  propvalue.len = value->len;

  /* This routine is a mindless wrapper. */
  SVN_ERR (svn_fs_change_node_prop (eb->txn_root, db->path->data,
                                    name->data, &propvalue, db->subpool));

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;
  svn_revnum_t new_revision = SVN_INVALID_REVNUM;
  svn_error_t *err;
  const char *conflict;

  /* Commit. */
  err = svn_repos_fs_commit_txn (&conflict, eb->repos, &new_revision, eb->txn);

  if (err)
    {
      /* ### todo: we should check whether it really was a conflict,
         and return the conflict info if so? */

      /* If the commit failed, it's *probably* due to a conflict --
         that is, the txn being out-of-date.  The filesystem gives us
         the ability to continue diddling the transaction and try
         again; but let's face it: that's not how the cvs or svn works
         from a user interface standpoint.  Thus we don't make use of
         this fs feature (for now, at least.)

         So, in a nutshell: svn commits are an all-or-nothing deal.
         Each commit creates a new fs txn which either succeeds or is
         aborted completely.  No second chances;  the user simply
         needs to update and commit again  :) */

      SVN_ERR (svn_fs_abort_txn (eb->txn));
      return err;
    }

  /* Pass new revision information to the caller's hook.  Note that
     this hook is unrelated to the standard repository post-commit
     hooks.  See svn_repos.h for more on this. */
  {
    svn_string_t *date, *author;

    SVN_ERR (svn_fs_revision_prop (&date, svn_repos_fs (eb->repos),
                                   new_revision, SVN_PROP_REVISION_DATE,
                                   eb->pool));

    SVN_ERR (svn_fs_revision_prop (&author, svn_repos_fs (eb->repos),
                                   new_revision, SVN_PROP_REVISION_AUTHOR,
                                   eb->pool));

    SVN_ERR ((*eb->hook) (new_revision, date->data, author->data,
                          eb->hook_baton));
  }

  return SVN_NO_ERROR;
}



static svn_error_t *
abort_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;

  if (eb->txn)
    return svn_fs_abort_txn (eb->txn);
  else
    return SVN_NO_ERROR;
}




/*** Public interface. ***/

svn_error_t *
svn_ra_local__get_editor (svn_delta_edit_fns_t **editor,
                          void **edit_baton,
                          svn_ra_local__session_baton_t *session,
                          svn_stringbuf_t *log_msg,
                          svn_ra_local__commit_hook_t *hook,
                          void *hook_baton,
                          apr_pool_t *pool)
{
  svn_delta_edit_fns_t *e = svn_delta_default_editor (pool);
  apr_pool_t *subpool = svn_pool_create (pool);
  struct edit_baton *eb = apr_pcalloc (subpool, sizeof (*eb));

  /* Set up the editor. */
  e->open_root         = open_root;
  e->delete_entry      = delete_entry;
  e->add_directory     = add_directory;
  e->open_directory    = open_directory;
  e->change_dir_prop   = change_dir_prop;
  e->close_directory   = close_directory;
  e->add_file          = add_file;
  e->open_file         = open_file;
  e->apply_textdelta   = apply_textdelta;
  e->change_file_prop  = change_file_prop;
  e->close_file        = close_file;
  e->close_edit        = close_edit;
  e->abort_edit        = abort_edit;

  /* Set up the edit baton. */
  eb->pool = subpool;
  eb->user = apr_pstrdup (subpool, session->username);
  eb->log_msg.data = apr_pmemdup (subpool, log_msg->data, log_msg->len + 1);
  eb->log_msg.len = log_msg->len;
  eb->hook = hook;
  eb->hook_baton = hook_baton;
  eb->base_path = svn_stringbuf_create_from_string (session->fs_path, subpool);
  eb->session = session;
  eb->repos = session->repos;
  eb->fs = svn_repos_fs (session->repos);
  eb->txn = NULL;

  *edit_baton = eb;
  *editor = e;
  
  return SVN_NO_ERROR;
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
