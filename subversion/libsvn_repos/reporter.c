/*
 * reporter.c : `reporter' vtable routines for updates.
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

#include "svn_repos.h"
#include "svn_fs.h"
#include "svn_path.h"


/* A structure used by the routines within the `reporter' vtable,
   driven by the client as it describes its working copy revisions. */
typedef struct svn_repos_report_baton_t
{
  /* The transaction being built in the repository, a mirror of the
     working copy. */
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  /* The location under which all reporting will happen (in the fs) */
  svn_string_t *base_path;

  /* The actual target of the report */
  svn_string_t *target;

  /* finish_report() calls svn_fs_dir_delta(), and uses this arg to
     decide which revision to compare the transaction against. */
  svn_revnum_t revnum_to_update_to;

  /* The working copy editor driven by svn_fs_dir_delta(). */
  const svn_delta_edit_fns_t *update_editor;
  void *update_edit_baton;

  /* This hash describes the mixed revisions in the transaction; it
     maps pathnames (char *) to revision numbers (svn_revnum_t). */
  apr_hash_t *path_rev_hash;

  /* Pool from the session baton. */
  apr_pool_t *pool;

} svn_repos_report_baton_t;


svn_error_t *
svn_repos_set_path (void *report_baton,
                    svn_string_t *path,
                    svn_revnum_t revision)
{
  svn_fs_root_t *from_root;
  svn_string_t *from_path;
  svn_repos_report_baton_t *rbaton = report_baton;
  svn_revnum_t *rev_ptr = apr_palloc (rbaton->pool, sizeof(*rev_ptr));

  /* If this is the very first call, no txn exists yet. */
  if (! rbaton->txn)
    {
      /* Sanity check: make that we didn't call this with real data
         before simply informing the reporter of our base revision. */
      if (! svn_path_is_empty (path, svn_path_repos_style))
        return 
          svn_error_create
          (SVN_ERR_RA_BAD_REVISION_REPORT, 0, NULL, rbaton->pool,
           "svn_repos_set_path: initial revision report was bogus.");

      /* Start a transaction based on REVISION. */
      SVN_ERR (svn_fs_begin_txn (&(rbaton->txn), rbaton->fs,
                                 revision, rbaton->pool));
      SVN_ERR (svn_fs_txn_root (&(rbaton->txn_root), rbaton->txn,
                                rbaton->pool));
      
      /* In our hash, map the root of the txn ("") to the initial base
         revision. */
      *rev_ptr = revision;
      apr_hash_set (rbaton->path_rev_hash, "", APR_HASH_KEY_STRING, rev_ptr);
    }

  else  /* this is not the first call to set_path. */ 
    {
      /* Create the "from" root and path. */
      SVN_ERR (svn_fs_revision_root (&from_root, rbaton->fs,
                                     revision, rbaton->pool));

      /* The path we are dealing with is the anchor (where the
         reporter is rooted) + target (the top-level thing being
         reported) + path (stuff relative to the target...this is the
         empty string in the file case since the target is the file
         itself, not a directory containing the file). */
      from_path = svn_string_dup (rbaton->base_path, rbaton->pool);
      svn_path_add_component (from_path, rbaton->target, 
                              svn_path_repos_style);
      svn_path_add_component (from_path, path, svn_path_repos_style);

      /* Copy into our txn. */
      SVN_ERR (svn_fs_copy (from_root, from_path->data,
                            rbaton->txn_root, from_path->data, rbaton->pool));
      
      /* Remember this path in our hashtable. */
      *rev_ptr = revision;
      apr_hash_set (rbaton->path_rev_hash, from_path->data,
                    from_path->len, rev_ptr);    

    }

  return SVN_NO_ERROR;
}



svn_error_t *
svn_repos_delete_path (void *report_baton,
                       svn_string_t *path)
{
  svn_string_t *delete_path;
  svn_repos_report_baton_t *rbaton = report_baton;
  
  /* The path we are dealing with is the anchor (where the
     reporter is rooted) + target (the top-level thing being
     reported) + path (stuff relative to the target...this is the
     empty string in the file case since the target is the file
     itself, not a directory containing the file). */
  delete_path = svn_string_dup (rbaton->base_path, rbaton->pool);
  svn_path_add_component (delete_path, rbaton->target, 
                          svn_path_repos_style);
  svn_path_add_component (delete_path, path, svn_path_repos_style);
  

  /* Remove the file or directory (recursively) from the txn. */
  SVN_ERR (svn_fs_delete_tree (rbaton->txn_root, delete_path->data, 
                               rbaton->pool));

  return SVN_NO_ERROR;
}




svn_error_t *
svn_repos_finish_report (void *report_baton)
{
  svn_fs_root_t *rev_root;
  svn_repos_report_baton_t *rbaton = (svn_repos_report_baton_t *) report_baton;

  /* Get the root of the revision we want to update to. */
  SVN_ERR (svn_fs_revision_root (&rev_root, rbaton->fs,
                                 rbaton->revnum_to_update_to,
                                 rbaton->pool));
  
  /* Ah!  The good stuff!  svn_repos_update does all the hard work. */
  SVN_ERR (svn_repos_update (rev_root, rbaton->txn_root, 
                             rbaton->base_path, rbaton->target,
                             rbaton->path_rev_hash,
                             rbaton->update_editor,
                             rbaton->update_edit_baton,
                             rbaton->pool));
                           
  /* Still here?  Great!  Throw out the transaction. */
  SVN_ERR (svn_fs_abort_txn (rbaton->txn));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_begin_report (void **report_baton,
                        svn_revnum_t revnum,
                        svn_fs_t *fs,
                        svn_string_t *fs_base,
                        const svn_delta_edit_fns_t *update_editor,
                        void *update_baton,
                        apr_pool_t *pool)
{
  svn_repos_report_baton_t *rbaton;

  /* Build a reporter baton. */
  rbaton = apr_pcalloc (pool, sizeof(*rbaton));
  rbaton->revnum_to_update_to = revnum;
  rbaton->update_editor = update_editor;
  rbaton->update_edit_baton = update_baton;
  rbaton->path_rev_hash = apr_hash_make (pool);
  rbaton->fs = fs;

  /* Split the filesystem path given to us into an ANCHOR (which is
     the root of the report) and a TARGET (which is the target of the
     report). */
  svn_path_split (fs_base, &rbaton->base_path, &rbaton->target,
                  svn_path_repos_style, pool);

  /* If the target is "this dir", clear it out. */
  if (svn_path_is_thisdir (rbaton->target, svn_path_repos_style))
    svn_string_setempty (rbaton->target);

  rbaton->pool = pool;
  
  /* Hand reporter back to client. */
  *report_baton = rbaton;
  return SVN_NO_ERROR;
}



/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
