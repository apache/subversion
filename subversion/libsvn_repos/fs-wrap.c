/* fs-wrap.c --- filesystem interface wrappers.
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

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "repos.h"


/*** Commit wrappers ***/

svn_error_t *
svn_repos_fs_commit_txn (const char **conflict_p,
                         svn_repos_t *repos,
                         svn_revnum_t *new_rev,
                         svn_fs_txn_t *txn)
{
  svn_fs_t *fs = repos->fs;
  apr_pool_t *pool = svn_fs_txn_pool (txn);

  if (fs != svn_fs_txn_fs (txn))
    return svn_error_createf 
      (SVN_ERR_FS_GENERAL, NULL,
       "Transaction does not belong to given repository's filesystem");

  /* Run pre-commit hooks. */
  {
    const char *txn_name;

    SVN_ERR (svn_fs_txn_name (&txn_name, txn, pool));
    SVN_ERR (svn_repos__hooks_pre_commit (repos, txn_name, pool));
  }

  /* Commit. */
  SVN_ERR (svn_fs_commit_txn (conflict_p, new_rev, txn));

  /* Run post-commit hooks. */
  SVN_ERR (svn_repos__hooks_post_commit (repos, *new_rev, pool));

  return SVN_NO_ERROR;
}



/*** Transaction creation wrappers. ***/

svn_error_t *
svn_repos_fs_begin_txn_for_commit (svn_fs_txn_t **txn_p,
                                   svn_repos_t *repos,
                                   svn_revnum_t rev,
                                   const char *author,
                                   const char *log_msg,
                                   apr_pool_t *pool)
{
  /* Run start-commit hooks. */
  SVN_ERR (svn_repos__hooks_start_commit (repos, author, pool));

  /* Begin the transaction. */
  SVN_ERR (svn_fs_begin_txn (txn_p, repos->fs, rev, pool));

  /* We pass the author and log message to the filesystem by adding
     them as properties on the txn.  Later, when we commit the txn,
     these properties will be copied into the newly created revision. */
  {
    /* User (author). */
    {
      svn_string_t val;
      val.data = author;
      val.len = strlen (author);
      
      SVN_ERR (svn_fs_change_txn_prop (*txn_p, SVN_PROP_REVISION_AUTHOR,
                                       &val, pool));
    }
    
    /* Log message. */
    if (log_msg != NULL)
      {
        /* Heh heh -- this is unexpected fallout from changing most
           code to use plain strings instead of svn_stringbuf_t and
           svn_string_t.  The log_msg is passed in as const char *
           data, but svn_fs_change_txn_prop() is a generic propset
           function that must accept arbitrary data as values.  So we
           create an svn_string_t as wrapper here. */

        svn_string_t l;
        l.data = log_msg;
        l.len = strlen (log_msg);

        SVN_ERR (svn_fs_change_txn_prop (*txn_p, SVN_PROP_REVISION_LOG,
                                         &l, pool));
      }
  }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_fs_begin_txn_for_update (svn_fs_txn_t **txn_p,
                                   svn_repos_t *repos,
                                   svn_revnum_t rev,
                                   const char *author,
                                   apr_pool_t *pool)
{
  /* ### someday, we might run a read-hook here. */

  /* Begin the transaction. */
  SVN_ERR (svn_fs_begin_txn (txn_p, repos->fs, rev, pool));

  /* We pass the author to the filesystem by adding it as a property
     on the txn. */
  {
    /* User (author). */
    {
      svn_string_t val;
      val.data = author;
      val.len = strlen (author);
      
      SVN_ERR (svn_fs_change_txn_prop (*txn_p, SVN_PROP_REVISION_AUTHOR,
                                       &val, pool));
    }    
  }

  return SVN_NO_ERROR;
}



/*** Property change wrappers ***/

svn_error_t *
svn_repos_fs_change_rev_prop (svn_repos_t *repos,
                              svn_revnum_t rev,
                              const char *author,
                              const char *name,
                              const svn_string_t *value,
                              apr_pool_t *pool)
{
  svn_fs_t *fs = repos->fs;

  /* Run pre-revprop-change hook */
  SVN_ERR (svn_repos__hooks_pre_revprop_change (repos, rev, author, name, 
                                                value, pool));

  /* Change the revision prop. */
  SVN_ERR (svn_fs_change_rev_prop (fs, rev, name, value, pool));

  /* Run post-revprop-change hook */
  SVN_ERR (svn_repos__hooks_post_revprop_change (repos, rev, author, 
                                                 name, pool));

  return SVN_NO_ERROR;
}






/* 
 * vim:ts=4:sw=4:expandtab:tw=80:fo=tcroq 
 * vim:isk=a-z,A-Z,48-57,_,.,-,> 
 * vim:cino=>1s,e0,n0,f0,{.5s,}0,^-.5s,=.5s,t0,+1s,c3,(0,u0,\:0
 */
