/* hooks.c : running repository hooks and sentinels
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

#include "apr_pools.h"
#include "apr_file_io.h"

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_repos.h"




/* Run the pre-commit hooks for FS, expanding "$txn" to TXN_NAME.  Use
   POOL for any temporary allocations.  If any of the hooks fail,
   destroy the txn identified by TXN_NAME and return the error
   SVN_ERR_REPOS_HOOK_FAILURE.  */
static svn_error_t  *
run_pre_commit_hooks (svn_fs_t *fs,
                      const char *txn_name,
                      apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}


/* Run the post-commit hooks for FS, expanding "$rev" to REV.  Use
   POOL for any temporary allocations.  All hooks are run regardless
   of failure, but if any hooks fails, return the error
   SVN_ERR_REPOS_HOOK_FAILURE.  */
static svn_error_t  *
run_post_commit_hooks (svn_fs_t *fs,
                       svn_revnum_t rev,
                       apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}



/*** Public interface. ***/

svn_error_t *
svn_repos_fs_commit_txn (const char **conflict_p,
                         svn_revnum_t *new_rev,
                         svn_fs_txn_t *txn)
{
  svn_fs_t *fs = svn_fs_txn_fs (txn);
  apr_pool_t *pool = svn_fs_txn_pool (txn);

  /* Run pre-commit hooks. */
  {
    const char *txn_name;

    SVN_ERR (svn_fs_txn_name (&txn_name, txn, pool));
    SVN_ERR (run_pre_commit_hooks (fs, txn_name, pool));
  }

  /* Commit. */
  SVN_ERR (svn_fs_commit_txn (conflict_p, new_rev, txn));

  /* Run post-commit hooks. */
  SVN_ERR (run_post_commit_hooks (fs, *new_rev, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_fs_begin_txn_for_commit (svn_fs_txn_t **txn_p,
                                   svn_fs_t *fs,
                                   svn_revnum_t rev,
                                   const char *author,
                                   svn_string_t *log_msg,
                                   apr_pool_t *pool)
{
  svn_string_t log_prop_name = { SVN_PROP_REVISION_LOG,
                                 sizeof(SVN_PROP_REVISION_LOG) - 1};
  svn_string_t author_prop_name = { SVN_PROP_REVISION_AUTHOR,
                                    sizeof(SVN_PROP_REVISION_AUTHOR) - 1};

  /* Begin the transaction. */
  SVN_ERR (svn_fs_begin_txn (txn_p, fs, rev, pool));

  /* We pass the author and log message to the filesystem by adding
     them as properties on the txn.  Later, when we commit the txn,
     these properties will be copied into the newly created revision. */

  /* User (author). */
  {
    svn_string_t val;
    val.data = author;
    val.len = strlen (author);

    SVN_ERR (svn_fs_change_txn_prop (*txn_p, &author_prop_name,
                                     &val, pool));
  }

  /* Log message. */
  SVN_ERR (svn_fs_change_txn_prop (*txn_p, &log_prop_name,
                                   log_msg, pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
