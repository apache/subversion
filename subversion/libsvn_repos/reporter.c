/*
 * reporter.c : `reporter' vtable routines for updates.
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

#include "svn_path.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "repos.h"

/* A structure used by the routines within the `reporter' vtable,
   driven by the client as it describes its working copy revisions. */
typedef struct svn_repos_report_baton_t
{
  svn_repos_t *repos;

  /* The transaction being built in the repository, a mirror of the
     working copy. */
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  /* An optional second transaction used when preserving linked paths
     in the delta report. */
  svn_fs_txn_t *txn2;
  svn_fs_root_t *txn2_root;

  /* Which user is doing the update (building the temporary txn) */
  const char *username;

  /* The fs path under which all reporting will happen */
  const char *base_path;

  /* The actual target of the report */
  svn_stringbuf_t *target;

  /* -- These items are used by finish_report() when it calls
        svn_repos_dir_delta(): --  */

  /* whether or not to generate text-deltas */
  svn_boolean_t text_deltas; 

  /* which revision to compare against */
  svn_revnum_t revnum_to_update_to; 

  /* The fs path that will be the 'target' of dir_delta.
     In the case of 'svn switch', this is probably distinct from BASE_PATH.
     In the case of 'svn update', this is probably identical to BASE_PATH */
  const char *tgt_path;

  /* Whether or not to recurse into the directories */
  svn_boolean_t recurse;

  /* the editor to drive */
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
                    const char *path,
                    svn_revnum_t revision)
{
  svn_fs_root_t *from_root;
  svn_stringbuf_t *from_path;
  svn_repos_report_baton_t *rbaton = report_baton;
  svn_revnum_t *rev_ptr = apr_palloc (rbaton->pool, sizeof(*rev_ptr));

  /* If this is the very first call, no txn exists yet. */
  if (! rbaton->txn)
    {
      /* ### need to change svn_path_is_empty() */
      svn_stringbuf_t *pathbuf = svn_stringbuf_create (path, rbaton->pool);

      /* Sanity check: make that we didn't call this with real data
         before simply informing the reporter of our base revision. */
      if (! svn_path_is_empty (pathbuf))
        return 
          svn_error_create
          (SVN_ERR_RA_BAD_REVISION_REPORT, 0, NULL, rbaton->pool,
           "svn_repos_set_path: initial revision report was bogus.");

      /* Start a transaction based on REVISION. */
      SVN_ERR (svn_repos_fs_begin_txn_for_update (&(rbaton->txn),
                                                  rbaton->repos,
                                                  revision,
                                                  rbaton->username,
                                                  rbaton->pool));
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
      SVN_ERR (svn_fs_revision_root (&from_root, rbaton->repos->fs,
                                     revision, rbaton->pool));

      /* The path we are dealing with is the anchor (where the
         reporter is rooted) + target (the top-level thing being
         reported) + path (stuff relative to the target...this is the
         empty string in the file case since the target is the file
         itself, not a directory containing the file). */
      from_path = svn_stringbuf_create (rbaton->base_path, rbaton->pool);
      if (rbaton->target)
        svn_path_add_component (from_path, rbaton->target);
      svn_path_add_component_nts (from_path, path);

      /* Copy into our txn. */
      SVN_ERR (svn_fs_link (from_root, from_path->data,
                            rbaton->txn_root, from_path->data, rbaton->pool));
      
      /* Remember this path in our hashtable. */
      *rev_ptr = revision;
      apr_hash_set (rbaton->path_rev_hash, from_path->data,
                    from_path->len, rev_ptr);    

    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_link_path (void *report_baton,
                     const char *path,
                     const char *link_path,
                     svn_revnum_t revision)
{
  svn_fs_root_t *from_root;
  svn_stringbuf_t *from_path;
  svn_repos_report_baton_t *rbaton = report_baton;
  svn_revnum_t *rev_ptr = apr_palloc (rbaton->pool, sizeof(*rev_ptr));

  /* If this is the very first call, no second txn exists yet.  Of
     course, we'll only use it if we're "updating", not when we're
     "switching" */
  if ((! rbaton->txn2) && (! rbaton->tgt_path))
    {
      /* Start a transaction based on the revision to which we to
         update. */
      SVN_ERR (svn_repos_fs_begin_txn_for_update (&(rbaton->txn2),
                                                  rbaton->repos,
                                                  rbaton->revnum_to_update_to,
                                                  rbaton->username,
                                                  rbaton->pool));
      SVN_ERR (svn_fs_txn_root (&(rbaton->txn2_root), rbaton->txn2,
                                rbaton->pool));
      
    }

  /* The path we are dealing with is the anchor (where the
     reporter is rooted) + target (the top-level thing being
     reported) + path (stuff relative to the target...this is the
     empty string in the file case since the target is the file
     itself, not a directory containing the file). */
  from_path = svn_stringbuf_create (rbaton->base_path, rbaton->pool);
  if (rbaton->target)
    svn_path_add_component (from_path, rbaton->target);
  svn_path_add_component_nts (from_path, path);
  
  /* Copy into our txn. */
  SVN_ERR (svn_fs_revision_root (&from_root, rbaton->repos->fs,
                                 revision, rbaton->pool));
  SVN_ERR (svn_fs_link (from_root, link_path,
                        rbaton->txn_root, from_path->data, rbaton->pool));

  /* Copy into our second "goal" txn (re-use FROM_ROOT) if we're using
     it. */
  if (rbaton->txn2)
    {
      SVN_ERR (svn_fs_revision_root (&from_root, rbaton->repos->fs,
                                     rbaton->revnum_to_update_to, 
                                     rbaton->pool));
      SVN_ERR (svn_fs_link (from_root, link_path,
                            rbaton->txn2_root, from_path->data, rbaton->pool));
    }

  /* Remember this path in our hashtable.  ### todo: Come back to
     this, as the original hash table idea mapped only paths to
     revisions, not paths to linkedpaths+revisions!  */
  *rev_ptr = revision;
  apr_hash_set (rbaton->path_rev_hash, from_path->data,
                from_path->len, rev_ptr);    

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_delete_path (void *report_baton,
                       const char *path)
{
  svn_stringbuf_t *delete_path;
  svn_repos_report_baton_t *rbaton = report_baton;
  
  /* The path we are dealing with is the anchor (where the
     reporter is rooted) + target (the top-level thing being
     reported) + path (stuff relative to the target...this is the
     empty string in the file case since the target is the file
     itself, not a directory containing the file). */
  delete_path = svn_stringbuf_create (rbaton->base_path, rbaton->pool);
  if (rbaton->target)
    svn_path_add_component (delete_path, rbaton->target);
  svn_path_add_component_nts (delete_path, path);

  /* Remove the file or directory (recursively) from the txn. */
  SVN_ERR (svn_fs_delete_tree (rbaton->txn_root, delete_path->data, 
                               rbaton->pool));

  return SVN_NO_ERROR;
}




svn_error_t *
svn_repos_finish_report (void *report_baton)
{
  svn_fs_root_t *target_root;
  svn_repos_report_baton_t *rbaton = (svn_repos_report_baton_t *) report_baton;
  const char *tgt_path;

  /* If nothing was described, then we have an error */
  if (rbaton->txn == NULL)
    return svn_error_create(SVN_ERR_REPOS_NO_DATA_FOR_REPORT, 0, NULL,
                            rbaton->pool,
                            "svn_repos_finish_report: no transaction was "
                            "present, meaning no data was provided.");

  /* Use the second transacation as a target if we made one, else get
     the root of the revision we want to update to. */
  if (rbaton->txn2)
    target_root = rbaton->txn2_root;
  else
    SVN_ERR (svn_fs_revision_root (&target_root, rbaton->repos->fs,
                                   rbaton->revnum_to_update_to,
                                   rbaton->pool));

  /* Calculate the tgt_path if none was given. */
  if (rbaton->tgt_path)
    tgt_path = rbaton->tgt_path;
  else
    tgt_path = svn_path_join_many 
      (rbaton->pool, rbaton->base_path,
       rbaton->target ? rbaton->target->data : NULL, NULL);

  /* Drive the update-editor. */
  SVN_ERR (svn_repos_dir_delta (rbaton->txn_root, 
                                rbaton->base_path, 
                                rbaton->target ? 
                                rbaton->target->data : NULL,
                                rbaton->path_rev_hash,
                                target_root, 
                                tgt_path,
                                rbaton->update_editor,
                                rbaton->update_edit_baton,
                                rbaton->text_deltas,
                                rbaton->recurse,
                                TRUE,
                                FALSE,
                                rbaton->pool));
  
  /* Still here?  Great!  Throw out the transaction. */
  SVN_ERR (svn_fs_abort_txn (rbaton->txn));

  return SVN_NO_ERROR;
}



svn_error_t *
svn_repos_abort_report (void *report_baton)
{
  svn_repos_report_baton_t *rbaton = (svn_repos_report_baton_t *) report_baton;

  /* if we have transactions, then abort them. */
  if (rbaton->txn != NULL)
    SVN_ERR (svn_fs_abort_txn (rbaton->txn));

  if (rbaton->txn2 != NULL)
    SVN_ERR (svn_fs_abort_txn (rbaton->txn2));

  return SVN_NO_ERROR;
}




svn_error_t *
svn_repos_begin_report (void **report_baton,
                        svn_revnum_t revnum,
                        const char *username,
                        svn_repos_t *repos,
                        const char *fs_base,
                        const char *target,
                        const char *tgt_path,
                        svn_boolean_t text_deltas,
                        svn_boolean_t recurse,
                        const svn_delta_edit_fns_t *editor,
                        void *edit_baton,
                        apr_pool_t *pool)
{
  svn_repos_report_baton_t *rbaton;

  /* Build a reporter baton. */
  rbaton = apr_pcalloc (pool, sizeof(*rbaton));
  rbaton->revnum_to_update_to = revnum;
  rbaton->update_editor = editor;
  rbaton->update_edit_baton = edit_baton;
  rbaton->path_rev_hash = apr_hash_make (pool);
  rbaton->repos = repos;
  rbaton->text_deltas = text_deltas;
  rbaton->recurse = recurse;
  rbaton->pool = pool;

  /* Copy these since we're keeping them past the end of this function call.
     We don't know what the caller might do with them after we return... */
  rbaton->username = apr_pstrdup (pool, username);
  rbaton->base_path = apr_pstrdup (pool, fs_base);
  rbaton->target = target ? svn_stringbuf_create (target, pool) : NULL;
  rbaton->tgt_path = tgt_path ? apr_pstrdup (pool, tgt_path) : NULL;

  /* Hand reporter back to client. */
  *report_baton = rbaton;
  return SVN_NO_ERROR;
}



/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
