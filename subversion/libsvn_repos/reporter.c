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


svn_error_t *
svn_repos_set_path (void *report_baton,
                    svn_string_t *path,
                    svn_revnum_t revision)
{
  svn_fs_root_t *from_root;
  svn_string_t *from_path;
  svn_repos_report_baton_t *rbaton = (svn_repos_report_baton_t *) report_baton;
  svn_revnum_t *rev_ptr = apr_palloc (rbaton->pool, sizeof(*rev_ptr));

  /* If this is the very first call, no txn exists yet. */
  if (! rbaton->txn)
    {
      /* Sanity check: make sure that PATH is really the target dir. */
      if (! svn_string_compare (path, svn_string_create ("", rbaton->pool)))
        return 
          svn_error_create
          (SVN_ERR_RA_BAD_REVISION_REPORT, 0, NULL, rbaton->pool,
           "svn_ra_local__set_path: initial revision report was bogus.");

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
      from_path = svn_string_dup (rbaton->base_path, rbaton->pool);
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
svn_repos_finish_report (void *report_baton)
{
  svn_fs_root_t *rev_root;
  svn_repos_report_baton_t *rbaton = (svn_repos_report_baton_t *) report_baton;

  /* Get the root of the revision we want to update to. */
  SVN_ERR (svn_fs_revision_root (&rev_root, rbaton->fs,
                                 rbaton->revnum_to_update_to,
                                 rbaton->pool));
  
  /* Ah!  The good stuff!  dir_delta does all the hard work. */  
  SVN_ERR (svn_fs_dir_delta (rbaton->txn_root, rbaton->base_path->data,
                             rbaton->path_rev_hash,
                             rev_root, rbaton->base_path->data,
                             rbaton->update_editor,             
                             rbaton->update_edit_baton,
                             rbaton->pool));
  
  /* Still here?  Great!  Throw out the transaction. */
  SVN_ERR (svn_fs_abort_txn (rbaton->txn));

  return SVN_NO_ERROR;
}




/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */





