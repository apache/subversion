/* repos.c : repository creation; shared and exclusive repository locking
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

#include "apr_pools.h"
#include "apr_file_io.h"

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "repos.h"



/* Path accessor functions. */


const char *
svn_repos_path (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup (pool, repos->path);
}


const char *
svn_repos_db_env (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup (pool, repos->db_path);
}


const char *
svn_repos_conf_dir (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup (pool, repos->conf_path);
}


const char *
svn_repos_lock_dir (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup (pool, repos->lock_path);
}


const char *
svn_repos_db_lockfile (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrcat (pool,
                      repos->lock_path, "/" SVN_REPOS__DB_LOCKFILE,
                      NULL);
}


const char *
svn_repos_hook_dir (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrdup (pool, repos->hook_path);
}


const char *
svn_repos_start_commit_hook (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrcat (pool,
                      repos->hook_path, "/" SVN_REPOS__HOOK_START_COMMIT,
                      NULL);
}


const char *
svn_repos_pre_commit_hook (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrcat (pool,
                      repos->hook_path, "/" SVN_REPOS__HOOK_PRE_COMMIT,
                      NULL);
}


const char *
svn_repos_post_commit_hook (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrcat (pool,
                      repos->hook_path, "/" SVN_REPOS__HOOK_POST_COMMIT,
                      NULL);
}


const char *
svn_repos_read_sentinel_hook (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrcat (pool,
                      repos->hook_path, "/" SVN_REPOS__HOOK_READ_SENTINEL,
                      NULL);
}


const char *
svn_repos_write_sentinel_hook (svn_repos_t *repos, apr_pool_t *pool)
{
  return apr_pstrcat (pool,
                      repos->hook_path, "/" SVN_REPOS__HOOK_WRITE_SENTINEL,
                      NULL);
}


static svn_error_t *
create_locks (svn_repos_t *repos, const char *path, apr_pool_t *pool)
{
  apr_status_t apr_err;

  /* Create the locks directory. */
  apr_err = apr_dir_make (path, APR_OS_DEFAULT, pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf (apr_err, 0, 0, pool,
                              "creating lock dir `%s'", path);

  /* Create the DB lockfile under that directory. */
  {
    apr_file_t *f = NULL;
    apr_size_t written;
    const char *contents;
    const char *lockfile_path;

    lockfile_path = svn_repos_db_lockfile (repos, pool);
    apr_err = apr_file_open (&f, lockfile_path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             pool);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "creating lock file `%s'", lockfile_path);
    
    contents = 
      "DB lock file, representing locks on the versioned filesystem.\n"
      "\n"
      "All accessors -- both readers and writers -- of the repository's\n"
      "Berkeley DB environment take out shared locks on this file, and\n"
      "each accessor removes its lock when done.  If and when the DB\n"
      "recovery procedure is run, the recovery code takes out an\n"
      "exclusive lock on this file, so we can be sure no one else is\n"
      "using the DB during the recovery.\n"
      "\n"
      "You should never have to edit or remove this file.\n";
    
    apr_err = apr_file_write_full (f, contents, strlen (contents), &written);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "writing lock file `%s'", lockfile_path);
    
    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "closing lock file `%s'", lockfile_path);
  }

  return SVN_NO_ERROR;
}


static svn_error_t *
create_hooks (svn_repos_t *repos, const char *path, apr_pool_t *pool)
{
  const char *this_path, *contents;
  apr_status_t apr_err;
  apr_file_t *f;
  apr_size_t written;

  /* Create the hook directory. */
  apr_err = apr_dir_make (path, APR_OS_DEFAULT, pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf 
      (apr_err, 0, 0, pool, "creating hook directory `%s'", path);

  /*** Write a default template for each standard hook file. */

  /* Start-commit hooks. */
  {
    this_path = apr_psprintf (pool, "%s%s",
                              svn_repos_start_commit_hook (repos, pool),
                              SVN_REPOS__HOOK_DESC_EXT);
    
    apr_err = apr_file_open (&f, this_path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             pool);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "creating hook file `%s'", this_path);
    
    contents = 
      "#!/bin/sh\n"
      "\n"
      "# START-COMMIT HOOK\n"
      "#\n"
      "# The start-commit hook is invoked before a Subversion txn is created\n"
      "# in the process of doing a commit.  Subversion runs this hook\n"
      "# by invoking a program (script, executable, binary, etc.) named\n"
      "# `" 
      SVN_REPOS__HOOK_START_COMMIT
      "' (for which this file is a template)\n"
      "# with the following ordered arguments:\n"
      "#\n"
      "#   [1] REPOS-PATH   (the path to this repository)\n"
      "#   [2] USER         (the authenticated user attempting to commit)\n"
      "#\n"
      "# If the hook program exits with success, the commit continues; but\n"
      "# if it exits with failure (non-zero), the commit is stopped before\n"
      "# even a Subversion txn is created.\n"
      "#\n"
      "# On a Unix system, the normal procedure is to have "
      "`"
      SVN_REPOS__HOOK_START_COMMIT
      "'\n" 
      "# invoke other programs to do the real work, though it may do the\n"
      "# work itself too.\n"
      "#\n"
      "# On a Windows system, you should name the hook program\n"
      "# `" SVN_REPOS__HOOK_START_COMMIT ".bat' or "
      "`" SVN_REPOS__HOOK_START_COMMIT ".exe', but the basic idea is\n"
      "# the same.\n"
      "# \n"
      "# Here is an example hook script, for a Unix /bin/sh interpreter:\n"
      "#\n"
      "# REPOS=${1}\n"
      "# USER=${2}\n"
      "#\n"
      "# commit_allower.pl --repository ${REPOS} --user ${USER}\n"
      "# special-auth-check.py --user ${USER} --auth-level 3\n";

    apr_err = apr_file_write_full (f, contents, strlen (contents), &written);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "writing hook file `%s'", this_path);

    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "closing hook file `%s'", this_path);
  }  /* end start-commit hooks */

  /* Pre-commit hooks. */
  {
    this_path = apr_psprintf (pool, "%s%s",
                              svn_repos_pre_commit_hook (repos, pool),
                              SVN_REPOS__HOOK_DESC_EXT);

    apr_err = apr_file_open (&f, this_path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             pool);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "creating hook file `%s'", this_path);

    contents =
      "#!/bin/sh\n"
      "\n"
      "# PRE-COMMIT HOOK\n"
      "#\n"
      "# The pre-commit hook is invoked before a Subversion txn is\n"
      "# committed.  Subversion runs this hook by invoking a program\n"
      "# (script, executable, binary, etc.) named "
      "`" 
      SVN_REPOS__HOOK_PRE_COMMIT "' (for which\n"
      "# this file is a template), with the following ordered arguments:\n"
      "#\n"
      "#   [1] REPOS-PATH   (the path to this repository)\n"
      "#   [2] TXN-NAME     (the name of the txn about to be committed)\n"
      "#\n"
      "# If the hook program exits with success, the txn is committed; but\n"
      "# if it exits with failure (non-zero), the txn is aborted and no\n"
      "# commit takes place.  The hook program can use the `svnlook'\n"
      "# utility to help it examine the txn.\n"
      "#\n"
      "# On a Unix system, the normal procedure is to have "
      "`"
      SVN_REPOS__HOOK_PRE_COMMIT
      "'\n" 
      "# invoke other programs to do the real work, though it may do the\n"
      "# work itself too.\n"
      "#\n"
      "# On a Windows system, you should name the hook program\n"
      "# `" SVN_REPOS__HOOK_PRE_COMMIT ".bat' or "
      "`" SVN_REPOS__HOOK_PRE_COMMIT ".exe', but the basic idea is\n"
      "# the same.\n"
      "#\n"
      "# Here is an example hook script, for a Unix /bin/sh interpreter:\n"
      "#\n"
      "# REPOS=${1}\n"
      "# TXN=${2}\n"
      "#\n"
      "# SVNLOOK=/usr/local/bin/svnlook\n"
      "# LOG=`${SVNLOOK} ${REPOS} txn ${TXN} log`\n"
      "# echo ${LOG} | grep \"[a-zA-Z0-9]\" > /dev/null || exit 1\n"
      "# exit 0\n"
      "#\n";
    
    apr_err = apr_file_write_full (f, contents, strlen (contents), &written);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "writing hook file `%s'", this_path);

    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "closing hook file `%s'", this_path);
  }  /* end pre-commit hooks */

  /* Post-commit hooks. */
  {
    this_path = apr_psprintf (pool, "%s%s",
                              svn_repos_post_commit_hook (repos, pool),
                              SVN_REPOS__HOOK_DESC_EXT);

    apr_err = apr_file_open (&f, this_path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             pool);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "creating hook file `%s'", this_path);
    
    contents =
      "#!/bin/sh\n"
      "\n"
      "# POST-COMMIT HOOK\n"
      "#\n"
      "# The post-commit hook is invoked after a commit. Subversion runs\n"
      "# this hook by invoking a program (script, executable, binary,\n"
      "# etc.) named `" 
      SVN_REPOS__HOOK_POST_COMMIT 
      "' "
      "(for which this file is a template),\n"
      "# with the following ordered arguments:\n"
      "#\n"
      "#   [1] REPOS-PATH   (the path to this repository)\n"
      "#   [2] REV          (the number of the revision just committed)\n"
      "#\n"
      "# Because the commit has already completed and cannot be undone,\n"
      "# the exit code of the hook program is ignored.  The hook program\n"
      "# can use the `svnlook' utility to help it examine the\n"
      "# newly-committed tree.\n"
      "#\n"
      "# On a Unix system, the normal procedure is to have "
      "`"
      SVN_REPOS__HOOK_POST_COMMIT
      "'\n" 
      "# invoke other programs to do the real work, though it may do the\n"
      "# work itself too.\n"
      "#\n"
      "# On a Windows system, you should name the hook program\n"
      "# `" SVN_REPOS__HOOK_POST_COMMIT ".bat' or "
      "`" SVN_REPOS__HOOK_POST_COMMIT ".exe', but the basic idea is\n"
      "# the same.\n"
      "# \n"
      "# Here is an example hook script, for a Unix /bin/sh interpreter:\n"
      "#\n"
      "# REPOS=${1}\n"
      "# REV=${2}\n"
      "#\n"
      "# commit-email.pl ${REPOS} ${REV} commit-watchers@example.org\n"
      "# log-commit.py --repository ${REPOS} --revision ${REV}\n";

    apr_err = apr_file_write_full (f, contents, strlen (contents), &written);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "writing hook file `%s'", this_path);

    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "closing hook file `%s'", this_path);
  } /* end post-commit hooks */

  /* Read sentinels. */
  {
    this_path = apr_psprintf (pool, "%s%s",
                              svn_repos_read_sentinel_hook (repos, pool),
                              SVN_REPOS__HOOK_DESC_EXT);

    apr_err = apr_file_open (&f, this_path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             pool);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "creating hook file `%s'", this_path);
    
    contents =
      "READ-SENTINEL\n"
      "\n"
      "The invocation convention and protocol for the read-sentinel\n"
      "is yet to be defined.\n"
      "\n";

    apr_err = apr_file_write_full (f, contents, strlen (contents), &written);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "writing hook file `%s'", this_path);

    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "closing hook file `%s'", this_path);
  }  /* end read sentinels */

  /* Write sentinels. */
  {
    this_path = apr_psprintf (pool, "%s%s",
                              svn_repos_write_sentinel_hook (repos, pool),
                              SVN_REPOS__HOOK_DESC_EXT);

    apr_err = apr_file_open (&f, this_path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             pool);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "creating hook file `%s'", this_path);
    
    contents =
      "WRITE-SENTINEL\n"
      "\n"
      "The invocation convention and protocol for the write-sentinel\n"
      "is yet to be defined.\n"
      "\n";

    apr_err = apr_file_write_full (f, contents, strlen (contents), &written);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "writing hook file `%s'", this_path);

    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, pool, 
                                "closing hook file `%s'", this_path);
  }  /* end write sentinels */

  return SVN_NO_ERROR;
}


/* This code manages repository locking, which is motivated by the
 * need to support DB_RUN_RECOVERY.  Here's how it works:
 *
 * Every accessor of a repository's database takes out a shared lock
 * on the repository -- both readers and writers get shared locks, and
 * there can be an unlimited number of shared locks simultaneously.
 *
 * Sometimes, a db access returns the error DB_RUN_RECOVERY.  When
 * this happens, we need to run svn_fs_berkeley_recover() on the db
 * with no other accessors present.  So we take out an exclusive lock
 * on the repository.  From the moment we request the exclusive lock,
 * no more shared locks are granted, and when the last shared lock
 * disappears, the exclusive lock is granted.  As soon as we get it,
 * we can run recovery.
 *
 * We assume that once any berkeley call returns DB_RUN_RECOVERY, they
 * all do, until recovery is run.
 */

/* Clear all outstanding locks on ARG, an open apr_file_t *. */
static apr_status_t
clear_and_close (void *arg)
{
  apr_status_t apr_err;
  apr_file_t *f = arg;

  /* Remove locks. */
  apr_err = apr_file_unlock (f);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return apr_err;

  /* Close the file. */
  apr_err = apr_file_close (f);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return apr_err;

  return 0;
}


static void
init_repos_dirs (svn_repos_t *repos, apr_pool_t *pool)
{
  repos->db_path = apr_psprintf 
    (pool, "%s/%s", repos->path, SVN_REPOS__DB_DIR);
  repos->dav_path = apr_psprintf 
    (pool, "%s/%s", repos->path, SVN_REPOS__DAV_DIR);
  repos->conf_path = apr_psprintf 
    (pool, "%s/%s", repos->path, SVN_REPOS__CONF_DIR);
  repos->hook_path = apr_psprintf 
    (pool, "%s/%s", repos->path, SVN_REPOS__HOOK_DIR);
  repos->lock_path = apr_psprintf 
    (pool, "%s/%s", repos->path, SVN_REPOS__LOCK_DIR);
}


svn_error_t *
svn_repos_create (svn_repos_t **repos_p, const char *path, apr_pool_t *pool)
{
  svn_repos_t *repos;
  apr_status_t apr_err;

  /* Allocate a repository object. */
  repos = apr_pcalloc (pool, sizeof (*repos));
  repos->pool = pool;

  /* Create the top-level repository directory. */
  apr_err = apr_dir_make (path, APR_OS_DEFAULT, pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    {
      if (APR_STATUS_IS_EEXIST (apr_err))
        {
          apr_status_t empty = apr_check_dir_empty (path, pool);
          if (! APR_STATUS_IS_SUCCESS (empty))
            return svn_error_createf
              (apr_err, 0, 0, pool,
               "`%s' exists and is non-empty, repository creation failed",
               path);
        }
      else
        {
          return svn_error_createf
            (apr_err, 0, 0, pool, "unable to create repository `%s'",
             path);
        }
    }

  /* Initialize the repository paths. */
  repos->path = apr_pstrdup (pool, path);
  init_repos_dirs (repos, pool);
  
  /* Initialize the filesystem object. */
  repos->fs = svn_fs_new (pool);

  /* Create a Berkeley DB environment for the filesystem. */
  SVN_ERR (svn_fs_create_berkeley (repos->fs, repos->db_path));

  /* Create the DAV sandbox directory.  */
  apr_err = apr_dir_make (repos->dav_path, APR_OS_DEFAULT, pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf 
      (apr_err, 0, 0, pool, "creating DAV sandbox dir `%s'", repos->dav_path);

  /* Create the conf directory.  */
  apr_err = apr_dir_make (repos->conf_path, APR_OS_DEFAULT, pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf 
      (apr_err, 0, 0, pool, "creating conf dir `%s'", repos->conf_path);

  /* Create the lock directory.  */
  SVN_ERR (create_locks (repos, repos->lock_path, pool));

  /* Create the hooks directory.  */
  SVN_ERR (create_hooks (repos, repos->hook_path, pool));

  /* Write the top-level README file. */
  {
    apr_file_t *readme_file = NULL;
    apr_size_t written = 0;
    const char *readme_file_name
      = apr_psprintf (pool, "%s/%s", path, SVN_REPOS__README);
    const char *readme_contents =
      "This is a Subversion repository; use the `svnadmin' tool to examine\n"
      "it.  Do not add, delete, or modify files here unless you know how\n"
      "to avoid corrupting the repository.\n"
      "\n"
      "The directory \""
      SVN_REPOS__DB_DIR
      "\" contains a Berkeley DB environment.\n"
      "You may need to tweak the values in \""
      SVN_REPOS__DB_DIR
      "/DB_CONFIG\" to match the\n"
      "requirements of your site.\n"
      "\n"
      "Visit http://subversion.tigris.org/ for more information.\n";

    apr_err = apr_file_open (&readme_file, readme_file_name,
                             APR_WRITE | APR_CREATE, APR_OS_DEFAULT,
                             pool);
    if (! APR_STATUS_IS_SUCCESS (apr_err))
      return svn_error_createf (apr_err, 0, 0, pool,
                                "opening `%s' for writing", readme_file_name);

    apr_err = apr_file_write_full (readme_file, readme_contents,
                                   strlen (readme_contents), &written);
    if (! APR_STATUS_IS_SUCCESS (apr_err))
      return svn_error_createf (apr_err, 0, 0, pool,
                                "writing to `%s'", readme_file_name);
    
    apr_err = apr_file_close (readme_file);
    if (! APR_STATUS_IS_SUCCESS (apr_err))
      return svn_error_createf (apr_err, 0, 0, pool,
                                "closing `%s'", readme_file_name);
  }

  *repos_p = repos;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_open (svn_repos_t **repos_p,
                const char *path,
                apr_pool_t *pool)
{
  apr_status_t apr_err;
  svn_repos_t *repos;

  /* Allocate a repository object. */
  repos = apr_pcalloc (pool, sizeof (*repos));
  repos->pool = pool;

  /* Initialize the repository paths. */
  repos->path = apr_pstrdup (pool, path);
  init_repos_dirs (repos, pool);
  
  /* Initialize the filesystem object. */
  repos->fs = svn_fs_new (pool);

  /* Open up the Berkeley filesystem. */
  SVN_ERR (svn_fs_open_berkeley (repos->fs, repos->db_path));

  /* Locking. */
  {
    const char *lockfile_path;
    apr_file_t *lockfile_handle;

    /* Get a filehandle for the repository's db lockfile. */
    lockfile_path = svn_repos_db_lockfile (repos, pool);
    apr_err = apr_file_open (&lockfile_handle, lockfile_path,
                             APR_READ, APR_OS_DEFAULT, pool);
    if (! APR_STATUS_IS_SUCCESS (apr_err))
      return svn_error_createf
        (apr_err, 0, NULL, pool,
         "svn_repos_open: error opening db lockfile `%s'", lockfile_path);
    
    /* Get shared lock on the filehandle. */
    apr_err = apr_file_lock (lockfile_handle, APR_FLOCK_SHARED);
    if (! APR_STATUS_IS_SUCCESS (apr_err))
      return svn_error_createf
        (apr_err, 0, NULL, pool,
         "svn_repos_open: shared db lock on repository `%s' failed", path);
    
    /* Register an unlock function for the shared lock. */
    apr_pool_cleanup_register (pool, lockfile_handle, clear_and_close,
                               apr_pool_cleanup_null);
  }

  *repos_p = repos;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_delete (const char *path, 
                  apr_pool_t *pool)
{
  apr_status_t apr_err;
  const char *db_path = apr_psprintf (pool, "%s/%s", path, SVN_REPOS__DB_DIR);

  /* Delete the Berkeley environment... */
  SVN_ERR (svn_fs_delete_berkeley (db_path, pool));

  /* ...then blow away everything else.  */
  apr_err = apr_dir_remove_recursively (path, pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf (apr_err, 0, 0, pool,
                              "recursively removing `%s'", path);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_close (svn_repos_t *repos)
{
  /* Shut down the filesystem. */
  svn_fs_close_fs (repos->fs);

  /* Null out the repos pointer. */
  repos = NULL;
  return SVN_NO_ERROR;
}


svn_fs_t *
svn_repos_fs (svn_repos_t *repos)
{
  if (! repos)
    return NULL;
  return repos->fs;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
