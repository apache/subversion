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
  const char *target;

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
  apr_hash_t *path_revs;

  /* This hash contains any `linked paths', and what they were linked
     from. */
  apr_hash_t *linked_paths;

  /* Pool from the session baton. */
  apr_pool_t *pool;

} svn_repos_report_baton_t;


/* add PATH to the pathmap HASH with a repository path of LINKPATH.
   if LINKPATH is NULL, PATH will map to itself. */
static void add_to_path_map(apr_hash_t *hash,
                            const char *path,
                            const char *linkpath)
{
  /* normalize 'root paths' to have a slash */
  const char *norm_path = strcmp(path, "") ? path : "/";

  /* if there is an actual linkpath given, it is the repos path, else
     our path maps to itself. */
  const char *repos_path = linkpath ? linkpath : norm_path;

  /* now, geez, put the path in the map already! */
  apr_hash_set(hash, path, APR_HASH_KEY_STRING, (void *)repos_path);
}


/* return the actual repository path referred to by the editor's PATH,
   allocated in POOL, determined by examining the pathmap HASH. */
static const char *get_from_path_map(apr_hash_t *hash,
                                     const char *path,
                                     apr_pool_t *pool)
{
  const char *repos_path;
  svn_stringbuf_t *my_path;
  
  /* no hash means no map.  that's easy enough. */
  if (! hash)
    return apr_pstrdup(pool, path);
  
  if ((repos_path = apr_hash_get(hash, path, APR_HASH_KEY_STRING)))
    {
      /* what luck!  this path is a hash key!  if there is a linkpath,
         use that, else return the path itself. */
      return apr_pstrdup(pool, repos_path);
    }

  /* bummer.  PATH wasn't a key in path map, so we get to start
     hacking off components and looking for a parent from which to
     derive a repos_path.  use a stringbuf for convenience. */
  my_path = svn_stringbuf_create(path, pool);
  do 
    {
      svn_path_remove_component(my_path);
      if ((repos_path = apr_hash_get(hash, my_path->data, my_path->len)))
        {
          /* we found a mapping ... but of one of PATH's parents.
             soooo, we get to re-append the chunks of PATH that we
             broke off to the REPOS_PATH we found. */
          return apr_pstrcat(pool, repos_path, "/", 
                             path + my_path->len + 1, NULL);
        }
    }
  while (! svn_path_is_empty(my_path));
  
  /* well, we simply never found anything worth mentioning the map.
     PATH is its own default finding, then. */
  return apr_pstrdup(pool, path);
}



svn_error_t *
svn_repos_set_path (void *report_baton,
                    const char *path,
                    svn_revnum_t revision)
{
  svn_repos_report_baton_t *rbaton = report_baton;
  svn_revnum_t *rev_ptr = apr_palloc (rbaton->pool, sizeof (*rev_ptr));
  
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
      apr_hash_set (rbaton->path_revs, "", APR_HASH_KEY_STRING, rev_ptr);
    }

  else  /* this is not the first call to set_path. */ 
    {
      svn_fs_root_t *from_root;
      const char *from_path;
      const char *link_path;

      /* The path we are dealing with is the anchor (where the
         reporter is rooted) + target (the top-level thing being
         reported) + path (stuff relative to the target...this is the
         empty string in the file case since the target is the file
         itself, not a directory containing the file). */
      from_path = svn_path_join_many (rbaton->pool, 
                                      rbaton->base_path,
                                      rbaton->target ? rbaton->target : path,
                                      rbaton->target ? path : NULL,
                                      NULL);

      /* However, the path may be the child of a linked thing, in
         which case we'll be linking from somewhere entirely
         different. */
      link_path = get_from_path_map (rbaton->linked_paths, from_path, 
                                     rbaton->pool);

      /* Create the "from" root. */
      SVN_ERR (svn_fs_revision_root (&from_root, rbaton->repos->fs,
                                     revision, rbaton->pool));

      /* Copy into our txn. */
      SVN_ERR (svn_fs_link (from_root, link_path,
                            rbaton->txn_root, from_path, rbaton->pool));
      
      /* Remember this path in our hashtable. */
      *rev_ptr = revision;
      apr_hash_set (rbaton->path_revs, from_path, APR_HASH_KEY_STRING,
                    rev_ptr);    

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
  const char *from_path;
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
  from_path = svn_path_join_many (rbaton->pool, 
                                  rbaton->base_path,
                                  rbaton->target ? rbaton->target : path,
                                  rbaton->target ? path : NULL,
                                  NULL);
  
  /* Copy into our txn. */
  SVN_ERR (svn_fs_revision_root (&from_root, rbaton->repos->fs,
                                 revision, rbaton->pool));
  SVN_ERR (svn_fs_link (from_root, link_path,
                        rbaton->txn_root, from_path, rbaton->pool));

  /* Copy into our second "goal" txn (re-use FROM_ROOT) if we're using
     it. */
  if (rbaton->txn2)
    {
      SVN_ERR (svn_fs_revision_root (&from_root, rbaton->repos->fs,
                                     rbaton->revnum_to_update_to, 
                                     rbaton->pool));
      SVN_ERR (svn_fs_link (from_root, link_path,
                            rbaton->txn2_root, from_path, rbaton->pool));
    }

  /* Remember this path in our hashtable of revision.  It doesn't
     matter that the path comes from a different repository location. */
  *rev_ptr = revision;
  apr_hash_set (rbaton->path_revs, from_path, APR_HASH_KEY_STRING, rev_ptr);    
  /* Remove this path/link_path in our hashtable of linked paths. */
  if (! rbaton->linked_paths)
    rbaton->linked_paths = apr_hash_make (rbaton->pool);
  add_to_path_map (rbaton->linked_paths, from_path, link_path);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_delete_path (void *report_baton,
                       const char *path)
{
  const char *delete_path;
  svn_repos_report_baton_t *rbaton = report_baton;
  
  /* The path we are dealing with is the anchor (where the
     reporter is rooted) + target (the top-level thing being
     reported) + path (stuff relative to the target...this is the
     empty string in the file case since the target is the file
     itself, not a directory containing the file). */
  delete_path = svn_path_join_many (rbaton->pool, 
                                    rbaton->base_path,
                                    rbaton->target ? rbaton->target : path,
                                    rbaton->target ? path : NULL,
                                    NULL);

  /* Remove the file or directory (recursively) from the txn. */
  return svn_fs_delete_tree (rbaton->txn_root, delete_path, rbaton->pool);
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
    tgt_path = svn_path_join_many (rbaton->pool, 
                                   rbaton->base_path,
                                   rbaton->target ? rbaton->target : NULL, 
                                   NULL);

  /* Drive the update-editor. */
  SVN_ERR (svn_repos_dir_delta (rbaton->txn_root, 
                                rbaton->base_path, 
                                rbaton->target,
                                rbaton->path_revs,
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
  rbaton->path_revs = apr_hash_make (pool);
  rbaton->repos = repos;
  rbaton->text_deltas = text_deltas;
  rbaton->recurse = recurse;
  rbaton->pool = pool;

  /* Copy these since we're keeping them past the end of this function call.
     We don't know what the caller might do with them after we return... */
  rbaton->username = apr_pstrdup (pool, username);
  rbaton->base_path = apr_pstrdup (pool, fs_base);
  rbaton->target = target ? apr_pstrdup (pool, target) : NULL;
  rbaton->tgt_path = tgt_path ? apr_pstrdup (pool, tgt_path) : NULL;

  /* Hand reporter back to client. */
  *report_baton = rbaton;
  return SVN_NO_ERROR;
}



/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
