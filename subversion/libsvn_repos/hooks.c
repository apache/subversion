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

#include <apr_pools.h>
#include <apr_file_io.h>

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

/* NAME, CMD and ARGS are the name, path to and arguments for the hook
   program that is to be run.  If CHECK_EXITCODE is TRUE then the hook's
   exit status will be checked, and if an error occurred the hook's stderr
   output will be added to the returned error.  If CHECK_EXITCODE is FALSE
   the hook's exit status will be ignored. */
static svn_error_t *
run_hook_cmd (const char *name,
              const char *cmd,
              const char **args,
              svn_boolean_t check_exitcode,
              apr_pool_t *pool)
{
  apr_file_t *read_errhandle, *write_errhandle;
  apr_status_t apr_err;
  svn_error_t *err;
  int exitcode;
  apr_exit_why_e exitwhy;

  /* Create a pipe to access stderr of the child. */
  apr_err = apr_file_pipe_create(&read_errhandle, &write_errhandle, pool);
  if (apr_err)
    return svn_error_createf
      (apr_err, 0, NULL, "can't create pipe for %s hook", cmd);

  err = svn_io_run_cmd (".", cmd, args, &exitcode, &exitwhy, FALSE,
                        NULL, NULL, write_errhandle, pool);

  /* This seems to be done automatically if we pass the third parameter of
     apr_procattr_child_in/out_set(), but svn_io_run_cmd()'s interface does
     not support those parameters. */
  apr_err = apr_file_close (write_errhandle);
  if (!err && apr_err)
    return svn_error_create
      (apr_err, 0, NULL, "can't close write end of stderr pipe");

  /* Function failed. */
  if (err)
    {
      err = svn_error_createf
        (SVN_ERR_REPOS_HOOK_FAILURE, 0, err, "failed to run %s hook", cmd);
    }

  if (!err && check_exitcode)
    {
      /* Command failed. */
      if (! APR_PROC_CHECK_EXIT (exitwhy) || exitcode != 0)
        {
          svn_stringbuf_t *error;

          /* Read the file's contents into a stringbuf, allocated in POOL. */
          SVN_ERR (svn_stringbuf_from_aprfile (&error, read_errhandle, pool));

          err = svn_error_createf
              (SVN_ERR_REPOS_HOOK_FAILURE, 0, err,
               "%s hook failed with error output:\n%s",
               name, error->data);
        }
    }

  /* Hooks are fallible, and so hook failure is "expected" to occur at
     times.  When such a failure happens we still want to close the pipe */
  apr_err = apr_file_close (read_errhandle);
  if (!err && apr_err)
    return svn_error_create
      (apr_err, 0, NULL, "can't close read end of stdout pipe");

  return err;
}


/* Run the start-commit hook for REPOS.  Use POOL for any temporary
   allocations.  If the hook fails, return SVN_ERR_REPOS_HOOK_FAILURE.  */
static svn_error_t *
run_start_commit_hook (svn_repos_t *repos,
                       const char *user,
                       apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *hook = svn_repos_start_commit_hook (repos, pool);

  if ((! svn_io_check_path (hook, &kind, pool)) 
      && (kind == svn_node_file))
    {
      const char *args[4];

      args[0] = hook;
      args[1] = svn_repos_path (repos, pool);
      args[2] = user;
      args[3] = NULL;

      SVN_ERR (run_hook_cmd ("start-commit", hook, args, TRUE, pool));
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
  svn_node_kind_t kind;
  const char *hook = svn_repos_pre_commit_hook (repos, pool);

  if ((! svn_io_check_path (hook, &kind, pool)) 
      && (kind == svn_node_file))
    {
      const char *args[4];

      args[0] = hook;
      args[1] = svn_repos_path (repos, pool);
      args[2] = txn_name;
      args[3] = NULL;

      SVN_ERR (run_hook_cmd ("pre-commit", hook, args, TRUE, pool));
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
  svn_node_kind_t kind;
  const char *hook = svn_repos_post_commit_hook (repos, pool);

  if ((! svn_io_check_path (hook, &kind, pool)) 
      && (kind == svn_node_file))
    {
      const char *args[4];

      args[0] = hook;
      args[1] = svn_repos_path (repos, pool);
      args[2] = apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, rev);
      args[3] = NULL;

      SVN_ERR (run_hook_cmd ("post-commit", hook, args, FALSE, pool));
    }

  return SVN_NO_ERROR;
}


/* Run the pre-revprop-change hook for REPOS.  Use POOL for any
   temporary allocations.  If the hook fails, return
   SVN_ERR_REPOS_HOOK_FAILURE.  */
static svn_error_t  *
run_pre_revprop_change_hook (svn_repos_t *repos,
                             svn_revnum_t rev,
                             const char *author,
                             const char *name,
                             const svn_string_t *value,
                             apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *hook = svn_repos_pre_revprop_change_hook(repos, pool);

  if ((! svn_io_check_path (hook, &kind, pool)) 
      && (kind == svn_node_file))
    {
      const char *args[6];

      /* ### somehow pass VALUE as stdin to hook?! */

      args[0] = hook;
      args[1] = svn_repos_path (repos, pool);
      args[2] = apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, rev);
      args[3] = author;
      args[4] = name;
      args[5] = NULL;

      SVN_ERR (run_hook_cmd ("pre-revprop-change", hook, args, TRUE, pool));
    }
  else
    {
      /* If the pre- hook doesn't exist at all, then default to
         MASSIVE PARANOIA.  Changing revision properties is a lossy
         operation; so unless the repository admininstrator has
         *deliberately* created the pre-hook, disallow all changes. */
      return 
        svn_error_create 
        (SVN_ERR_REPOS_DISABLED_FEATURE, 0, NULL,
         "Repository has not been enabled to accept revision propchanges;\n"
         "ask the administrator to create a pre-revprop-change hook.");
    }

  return SVN_NO_ERROR;
}


/* Run the pre-revprop-change hook for REPOS.  Use POOL for any
   temporary allocations.  If the hook fails, return
   SVN_ERR_REPOS_HOOK_FAILURE.  */
static svn_error_t  *
run_post_revprop_change_hook (svn_repos_t *repos,
                              svn_revnum_t rev,
                              const char *author,
                              const char *name,
                              apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *hook = svn_repos_post_revprop_change_hook(repos, pool);
  
  if ((! svn_io_check_path (hook, &kind, pool)) 
      && (kind == svn_node_file))
    {
      const char *args[6];

      args[0] = hook;
      args[1] = svn_repos_path (repos, pool);
      args[2] = apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, rev);
      args[3] = author;
      args[4] = name;
      args[5] = NULL;

      SVN_ERR (run_hook_cmd ("post-revprop-change", hook, args, FALSE, pool));
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
      (SVN_ERR_FS_GENERAL, 0, NULL,
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
svn_repos_fs_change_rev_prop (svn_repos_t *repos,
                              svn_revnum_t rev,
                              const char *author,
                              const char *name,
                              const svn_string_t *value,
                              apr_pool_t *pool)
{
  svn_fs_t *fs = repos->fs;

  /* Run pre-revprop-change hook */
  SVN_ERR (run_pre_revprop_change_hook (repos, rev, author, name, 
                                        value, pool));

  /* Change the revision prop. */
  SVN_ERR (svn_fs_change_rev_prop (fs, rev, name, value, pool));

  /* Run post-revprop-change hook */
  SVN_ERR (run_post_revprop_change_hook (repos, rev, author, name, pool));

  return SVN_NO_ERROR;
}



svn_error_t *
svn_repos_fs_begin_txn_for_commit (svn_fs_txn_t **txn_p,
                                   svn_repos_t *repos,
                                   svn_revnum_t rev,
                                   const char *author,
                                   const char *log_msg,
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




/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 * vim:ts=4:sw=4:expandtab:tw=80:fo=tcroq 
 * vim:isk=a-z,A-Z,48-57,_,.,-,> 
 * vim:cino=>1s,e0,n0,f0,{.5s,}0,^-.5s,=.5s,t0,+1s,c3,(0,u0,\:0
 */
