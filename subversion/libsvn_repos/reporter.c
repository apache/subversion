/*
 * reporter.c : `reporter' vtable routines for updates.
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include "svn_pools.h"
#include "repos.h"

/* A structure used by the routines within the `reporter' vtable,
   driven by the client as it describes its working copy revisions. */
typedef struct svn_repos_report_baton_t
{
  svn_repos_t *repos;

  /* The revision on which we will base our transaction.  This start
     off as SVN_INVALID_REVNUM, and then is set by the first call to
     set_path().  */
  svn_revnum_t txn_base_rev;

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

  /* Whether or not to ignore ancestry in dir_delta */
  svn_boolean_t ignore_ancestry;

  /* the editor to drive */
  const svn_delta_editor_t *update_editor;
  void *update_edit_baton; 

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
  const char *norm_path = strcmp (path, "") ? path : "/";

  /* if there is an actual linkpath given, it is the repos path, else
     our path maps to itself. */
  const char *repos_path = linkpath ? linkpath : norm_path;

  /* now, geez, put the path in the map already! */
  apr_hash_set (hash,
                apr_pstrdup (apr_hash_pool_get (hash), path),
                APR_HASH_KEY_STRING, 
                apr_pstrdup (apr_hash_pool_get (hash), repos_path));
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
    return apr_pstrdup (pool, path);
  
  if ((repos_path = apr_hash_get (hash, path, APR_HASH_KEY_STRING)))
    {
      /* what luck!  this path is a hash key!  if there is a linkpath,
         use that, else return the path itself. */
      return apr_pstrdup (pool, repos_path);
    }

  /* bummer.  PATH wasn't a key in path map, so we get to start
     hacking off components and looking for a parent from which to
     derive a repos_path.  use a stringbuf for convenience. */
  my_path = svn_stringbuf_create (path, pool);
  do 
    {
      apr_size_t len = my_path->len;
      svn_path_remove_component (my_path);
      if (my_path->len == len)
        break;
      if ((repos_path = apr_hash_get (hash, my_path->data, my_path->len)))
        {
          /* we found a mapping ... but of one of PATH's parents.
             soooo, we get to re-append the chunks of PATH that we
             broke off to the REPOS_PATH we found. */
          return apr_pstrcat (pool, repos_path, "/", 
                              path + my_path->len + 1, NULL);
        }
    }
  while (! svn_path_is_empty (my_path->data));
  
  /* well, we simply never found anything worth mentioning the map.
     PATH is its own default finding, then. */
  return apr_pstrdup (pool, path);
}


/* Use POOL to delete all children and props of directory FS_PATH in
   TXN_ROOT. */
static svn_error_t *
remove_directory_children (const char *fs_path,
                           svn_fs_root_t *txn_root,
                           apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_hash_t *children, *props;
  apr_pool_t *subpool = svn_pool_create (pool);
  
  SVN_ERR (svn_fs_dir_entries (&children, txn_root, fs_path, pool));
  
  for (hi = apr_hash_first (pool, children); hi;
       hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_fs_dirent_t *dirent;
      const char *child_path;
      
      apr_hash_this (hi, &key, &klen, &val);
      dirent = val;
      
      child_path = svn_path_join (fs_path, dirent->name, subpool);
      SVN_ERR (svn_fs_delete_tree (txn_root, child_path, subpool));
      
      svn_pool_clear (subpool);
    }

  SVN_ERR (svn_fs_node_proplist (&props, txn_root, fs_path, pool));

  for (hi = apr_hash_first (pool, props); hi;
       hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      const char *propname;
      
      apr_hash_this (hi, &key, &klen, &val);
      propname = key;

      SVN_ERR (svn_fs_change_node_prop (txn_root, fs_path,
                                        propname, NULL, subpool));
      svn_pool_clear (subpool);
    }

  svn_pool_destroy (subpool);  
  return SVN_NO_ERROR;
}


static svn_error_t *
begin_txn (svn_repos_report_baton_t *rbaton)
{
  /* Start a transaction based on initial_rev. */
  SVN_ERR (svn_repos_fs_begin_txn_for_update (&(rbaton->txn),
                                              rbaton->repos,
                                              rbaton->txn_base_rev,
                                              rbaton->username,
                                              rbaton->pool));
  SVN_ERR (svn_fs_txn_root (&(rbaton->txn_root), rbaton->txn, rbaton->pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_set_path (void *report_baton,
                    const char *path,
                    svn_revnum_t revision,
                    svn_boolean_t start_empty,
                    apr_pool_t *pool)
{
  svn_repos_report_baton_t *rbaton = report_baton;
  svn_boolean_t first_time = FALSE;

  /* Sanity check: make that we didn't call this with a bogus revision. */
  if (! SVN_IS_VALID_REVNUM (revision))
    return svn_error_create
      (SVN_ERR_REPOS_BAD_REVISION_REPORT, NULL,
       "svn_repos_set_path: invalid revision passed to report.");

  if (! SVN_IS_VALID_REVNUM (rbaton->txn_base_rev))
    { 
      /* Sanity check: make that we didn't call this with real data
         before simply informing the reporter of our base revision. */
      if (! svn_path_is_empty (path))
        return svn_error_create
          (SVN_ERR_REPOS_BAD_REVISION_REPORT, NULL,
           "svn_repos_set_path: initial revision report was bogus.");

      /* Barring previous problems, squirrel away our based-on revision. */
      rbaton->txn_base_rev = revision;
      first_time = TRUE;
    }

  /* If we haven't yet created a transaction, we either haven't made
     it past our first set_path, or we haven't seen any interesting
     reporter calls yet.  So what's interesting?  link_path()s,
     delete_path()s, and set_path()s with either a different REVISION
     than the based-on revision, or the START_EMPTY flag set. */
  if ((! rbaton->txn) && (revision == rbaton->txn_base_rev) && (! start_empty))
    return SVN_NO_ERROR;
  
  if (first_time)
    {
      if (start_empty)
        {
          /* If our first call has START_EMPTY set, we need to start
             up the transaction stuffs and then clean out the starting
             directory. */
          SVN_ERR (begin_txn (rbaton));
          SVN_ERR (remove_directory_children (rbaton->base_path, 
                                              rbaton->txn_root, pool));
        }
    }
  else
    {
      const char *from_path;
      svn_fs_root_t *from_root;
      const char *link_path;

      /* Create the transaction if we haven't yet done so. */
      if (! rbaton->txn)
        SVN_ERR (begin_txn (rbaton));
        
      /* The path we are dealing with is the anchor (where the
         reporter is rooted) + target (the top-level thing being
         reported) + path (stuff relative to the target...this is the
         empty string in the file case since the target is the file
         itself, not a directory containing the file). */
      from_path = svn_path_join_many (pool, 
                                      rbaton->base_path,
                                      rbaton->target ? rbaton->target : path,
                                      rbaton->target ? path : NULL,
                                      NULL);
      
      /* However, the path may be the child of a linked thing, in
         which case we'll be linking from somewhere entirely
         different. */
      link_path = get_from_path_map (rbaton->linked_paths, from_path, pool);
          
      /* Create the "from" root. */
      SVN_ERR (svn_fs_revision_root (&from_root, rbaton->repos->fs,
                                     revision, pool));
      
      /* Copy into our txn (use svn_fs_revision_link if we can). */
      if (strcmp (link_path, from_path))
        SVN_ERR (svn_fs_copy (from_root, link_path,
                              rbaton->txn_root, from_path, pool));
      else
        SVN_ERR (svn_fs_revision_link (from_root, rbaton->txn_root, 
                                       from_path, pool));

      if (start_empty)
        {
          /* Destroy any children & props of the path.  We assume that
             the client will (later) re-add the entries it knows about. */
          SVN_ERR (remove_directory_children (from_path, rbaton->txn_root, 
                                              pool));
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_link_path (void *report_baton,
                     const char *path,
                     const char *link_path,
                     svn_revnum_t revision,
                     svn_boolean_t start_empty,
                     apr_pool_t *pool)
{
  svn_fs_root_t *from_root;
  const char *from_path;
  svn_repos_report_baton_t *rbaton = report_baton;

  /* If we haven't already started a main transaction, we need to do
     so now. */
  if (! rbaton->txn)
    SVN_ERR (begin_txn (rbaton));

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
  from_path = svn_path_join_many (pool, 
                                  rbaton->base_path,
                                  rbaton->target ? rbaton->target : path,
                                  rbaton->target ? path : NULL,
                                  NULL);
  
  /* Copy into our txn. */
  SVN_ERR (svn_fs_revision_root (&from_root, rbaton->repos->fs,
                                 revision, pool));
  SVN_ERR (svn_fs_copy (from_root, link_path,
                        rbaton->txn_root, from_path, pool));

  /* Copy into our second "goal" txn (re-use FROM_ROOT) if we're using
     it. */
  if (rbaton->txn2)
    {
      SVN_ERR (svn_fs_revision_root (&from_root, rbaton->repos->fs,
                                     rbaton->revnum_to_update_to, 
                                     pool));
      SVN_ERR (svn_fs_copy (from_root, link_path,
                            rbaton->txn2_root, from_path, pool));
    }

  /* Remove this path/link_path in our hashtable of linked paths. */
  if (! rbaton->linked_paths)
    rbaton->linked_paths = apr_hash_make (rbaton->pool);
  add_to_path_map (rbaton->linked_paths, from_path, link_path);
  
  if (start_empty)
    /* Destroy any children & props of the path.  We assume that the
       client will (later) re-add the entries it knows about.  */
    SVN_ERR (remove_directory_children (from_path, rbaton->txn_root, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_delete_path (void *report_baton,
                       const char *path,
                       apr_pool_t *pool)
{
  svn_error_t *err;
  const char *delete_path;
  svn_repos_report_baton_t *rbaton = report_baton;
  
  /* If we haven't already started a main transaction, we need to do
     so now. */
  if (! rbaton->txn)
    SVN_ERR (begin_txn (rbaton));

  /* The path we are dealing with is the anchor (where the
     reporter is rooted) + target (the top-level thing being
     reported) + path (stuff relative to the target...this is the
     empty string in the file case since the target is the file
     itself, not a directory containing the file). */
  delete_path = svn_path_join_many (pool, 
                                    rbaton->base_path,
                                    rbaton->target ? rbaton->target : path,
                                    rbaton->target ? path : NULL,
                                    NULL);

  /* Remove the file or directory (recursively) from the txn. */
  err = svn_fs_delete_tree (rbaton->txn_root, delete_path, pool);

  /* If the delete is a no-op, don't throw an error;  just ignore. */
  if (err)
    {
      if (err->apr_err != SVN_ERR_FS_NOT_FOUND)
        return err;
      svn_error_clear (err);
    }

  return SVN_NO_ERROR;
}




/* Implements the bulk of svn_repos_finish_report, that's everything except
 * aborting any txns.  This function can return an error and still rely on
 * any txns being aborted.
 */
static svn_error_t *
finish_report (void *report_baton)
{
  svn_fs_root_t *root1, *root2;
  svn_repos_report_baton_t *rbaton = report_baton;
  const char *tgt_path;

  /* If nothing was described, then we have an error */
  if (! SVN_IS_VALID_REVNUM (rbaton->txn_base_rev))
    return svn_error_create (SVN_ERR_REPOS_NO_DATA_FOR_REPORT, NULL,
                             "svn_repos_finish_report: no transaction was "
                             "present, meaning no data was provided.");

  /* Use the first transaction as a source if we made one, else get
     the root of the revision we would have based a transaction on. */
  if (rbaton->txn)
    root1 = rbaton->txn_root;
  else
    SVN_ERR (svn_fs_revision_root (&root1, rbaton->repos->fs,
                                   rbaton->txn_base_rev,
                                   rbaton->pool));

  /* Use the second transacation as a target if we made one, else get
     the root of the revision we want to update to. */
  if (rbaton->txn2)
    root2 = rbaton->txn2_root;
  else
    SVN_ERR (svn_fs_revision_root (&root2, rbaton->repos->fs,
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
  SVN_ERR (svn_repos_dir_delta (root1,
                                rbaton->base_path, 
                                rbaton->target,
                                root2,
                                tgt_path,
                                rbaton->update_editor,
                                rbaton->update_edit_baton,
                                rbaton->text_deltas,
                                rbaton->recurse,
                                TRUE,
                                rbaton->ignore_ancestry,
                                rbaton->pool));
  return SVN_NO_ERROR;
}
  
/* Wrapper to call finish_report, and then abort any txns even if
 * finish_report returns an error.
 */
svn_error_t *
svn_repos_finish_report (void *report_baton)
{
  svn_error_t *err1 = finish_report (report_baton);
  svn_error_t *err2 = svn_repos_abort_report (report_baton);
  if (err1)
    {
      svn_error_clear (err2);
      return err1;
    }
  return err2;
}


svn_error_t *
svn_repos_abort_report (void *report_baton)
{
  svn_repos_report_baton_t *rbaton = report_baton;

  /* ### To avoid uncommitted txns, perhaps we should we try to abort the
     ### second transacation even if aborting the first returns an
     ### error? */
  if (rbaton->txn)
    SVN_ERR (svn_fs_abort_txn (rbaton->txn));
  if (rbaton->txn2)
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
                        svn_boolean_t ignore_ancestry,
                        const svn_delta_editor_t *editor,
                        void *edit_baton,
                        apr_pool_t *pool)
{
  svn_repos_report_baton_t *rbaton;

  /* Build a reporter baton. */
  rbaton = apr_pcalloc (pool, sizeof(*rbaton));
  rbaton->txn_base_rev = SVN_INVALID_REVNUM;
  rbaton->revnum_to_update_to = revnum;
  rbaton->update_editor = editor;
  rbaton->update_edit_baton = edit_baton;
  rbaton->repos = repos;
  rbaton->text_deltas = text_deltas;
  rbaton->recurse = recurse;
  rbaton->ignore_ancestry = ignore_ancestry;
  rbaton->pool = pool;

  /* Copy these since we're keeping them past the end of this function call.
     We don't know what the caller might do with them after we return... */
  rbaton->username = username ? apr_pstrdup (pool, username) : NULL;
  rbaton->base_path = apr_pstrdup (pool, fs_base);
  rbaton->target = target ? apr_pstrdup (pool, target) : NULL;
  rbaton->tgt_path = tgt_path ? apr_pstrdup (pool, tgt_path) : NULL;

  /* Hand reporter back to client. */
  *report_baton = rbaton;
  return SVN_NO_ERROR;
}
