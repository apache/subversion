/* hooks.c : running repository hooks and sentinels
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

#include "apr_pools.h"
#include "apr_file_io.h"

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "repos.h"

/* In the code below, "hook" is sometimes used indiscriminately to
   mean either hook or sentinel.  */



/*** Hook drivers. ***/

static svn_error_t *
run_cmd_with_output (const char *cmd,
                     const char **args,
                     int *exitcode,
                     apr_exit_why_e *exitwhy,
                     apr_pool_t *pool)
{
  apr_file_t *outhandle, *errhandle;
  apr_status_t apr_err;
  
  /* Get an apr_file_t representing stdout and stderr. */
  apr_err = apr_file_open_stdout (&outhandle, pool);
  if (apr_err)
    return svn_error_create 
      (apr_err, 0, NULL, pool,
       "run_cmd_with_output: can't open handle to stdout");
  apr_err = apr_file_open_stderr (&errhandle, pool);
  if (apr_err)
    return svn_error_create 
      (apr_err, 0, NULL, pool,
       "run_cmd_with_output: can't open handle to stderr");

  return svn_io_run_cmd (".", cmd, args, exitcode, exitwhy, FALSE,
                         NULL, outhandle, errhandle, pool);
}


/* Run the start-commit hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, return SVN_ERR_REPOS_HOOK_FAILURE.  */
static svn_error_t *
run_start_commit_hook (svn_repos_t *repos,
                       const char *user,
                       apr_pool_t *pool)
{
  enum svn_node_kind kind;
  const char *hook = svn_repos_start_commit_hook (repos, pool);

  if ((! svn_io_check_path (hook, &kind, pool)) 
      && (kind == svn_node_file))
    {
      svn_error_t *err;
      const char *args[4];

      args[0] = hook;
      args[1] = svn_repos_path (repos, pool);
      args[2] = user;
      args[3] = NULL;

      if ((err = run_cmd_with_output (hook, args, NULL, NULL, pool)))
        {
          return svn_error_createf 
            (SVN_ERR_REPOS_HOOK_FAILURE, 0, err, pool,
             "run_start_commit_hook: error running cmd `%s'", hook);
        }
    }

  return SVN_NO_ERROR;
}


/* Run the pre-commit hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, return SVN_ERR_REPOS_HOOK_FAILURE.  */
static svn_error_t  *
run_pre_commit_hook (svn_repos_t *repos,
                     const char *txn_name,
                     apr_pool_t *pool)
{
  enum svn_node_kind kind;
  const char *hook = svn_repos_pre_commit_hook (repos, pool);

  if ((! svn_io_check_path (hook, &kind, pool)) 
      && (kind == svn_node_file))
    {
      svn_error_t *err;
      int exitcode;
      apr_exit_why_e exitwhy;
      const char *args[4];

      args[0] = hook;
      args[1] = svn_repos_path (repos, pool);
      args[2] = txn_name;
      args[3] = NULL;

      if ((err = run_cmd_with_output (hook, args, &exitcode, &exitwhy, pool)))
        {
          return svn_error_createf 
            (SVN_ERR_REPOS_HOOK_FAILURE, 0, err, pool,
             "run_pre_commit_hook: error running cmd `%s'", hook);
        }
      if (! APR_PROC_CHECK_EXIT (exitwhy) || exitcode != 0)
        {
          return svn_error_create
              (SVN_ERR_REPOS_HOOK_FAILURE, 0, err, pool,
               "pre-commit hook return non-zero status.  Aborting txn.");
        }
    }

  return SVN_NO_ERROR;
}


/* Run the post-commit hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, run SVN_ERR_REPOS_HOOK_FAILURE.  */
static svn_error_t  *
run_post_commit_hook (svn_repos_t *repos,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  enum svn_node_kind kind;
  const char *hook = svn_repos_post_commit_hook (repos, pool);

  if ((! svn_io_check_path (hook, &kind, pool)) 
      && (kind == svn_node_file))
    {
      svn_error_t *err;
      const char *args[4];

      args[0] = hook;
      args[1] = svn_repos_path (repos, pool);
      args[2] = apr_psprintf (pool, "%lu", rev);
      args[3] = NULL;

      if ((err = run_cmd_with_output (hook, args, NULL, NULL, pool)))
        {
          return svn_error_createf 
            (SVN_ERR_REPOS_HOOK_FAILURE, 0, err, pool,
             "run_post_commit_hook: error running cmd `%s'", hook);
        }
    }

  return SVN_NO_ERROR;
}



/*** Public interface. ***/

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
      (SVN_ERR_FS_GENERAL, 0, NULL, pool,
       "Transaction does not belong to given repository's filesystem");

  /* Run pre-commit hooks. */
  {
    const char *txn_name;

    SVN_ERR (svn_fs_txn_name (&txn_name, txn, pool));
    SVN_ERR (run_pre_commit_hook (repos, txn_name, pool));
  }

  /* Commit. */
  SVN_ERR (svn_fs_commit_txn (conflict_p, new_rev, txn));

  /* Run post-commit hooks. */
  SVN_ERR (run_post_commit_hook (repos, *new_rev, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_fs_begin_txn_for_commit (svn_fs_txn_t **txn_p,
                                   svn_repos_t *repos,
                                   svn_revnum_t rev,
                                   const char *author,
                                   svn_string_t *log_msg,
                                   apr_pool_t *pool)
{
  /* Run start-commit hooks. */
  SVN_ERR (run_start_commit_hook (repos, author, pool));

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
      SVN_ERR (svn_fs_change_txn_prop (*txn_p, SVN_PROP_REVISION_LOG,
                                       log_msg, pool));
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




/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 * vim:ts=4:sw=4:expandtab:tw=80:fo=tcroq 
 * vim:isk=a-z,A-Z,48-57,_,.,-,> 
 * vim:cino=>1s,e0,n0,f0,{.5s,}0,^-.5s,=.5s,t0,+1s,c3,(0,u0,\:0
 */
