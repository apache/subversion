/* fs.c --- creating, opening and closing filesystems
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>              /* for EINVAL */

#include "apr_general.h"
#include "apr_pools.h"
#include "apr_file_io.h"

#include "svn_pools.h"
#include "db.h"
#include "svn_fs.h"
#include "fs.h"
#include "err.h"
#include "nodes-table.h"
#include "rev-table.h"
#include "txn-table.h"
#include "reps-table.h"
#include "strings-table.h"
#include "dag.h"
#include "svn_private_config.h"


/* Checking for return values, and reporting errors.  */


/* If FS is already open, then return an SVN_ERR_FS_ALREADY_OPEN
   error.  Otherwise, return zero.  */
static svn_error_t *
check_already_open (svn_fs_t *fs)
{
  int major, minor, patch;

  /* ### check_already_open() doesn't truly have the right semantic for
     ### this, but it is called by both create_berkeley and open_berkeley,
     ### so it happens to be a low-cost point. probably should be
     ### refactored to go elsewhere. note that svn_fs_new() doesn't return
     ### an error, so it isn't quite suitable. */
  db_version (&major, &minor, &patch);
  if (major < SVN_FS_WANT_DB_MAJOR
      || minor < SVN_FS_WANT_DB_MINOR
      || patch < SVN_FS_WANT_DB_PATCH)
    return svn_error_createf (SVN_ERR_FS_GENERAL, 0, 0, fs->pool,
                              "bad database version: %d.%d.%d",
                              major, minor, patch);

  if (fs->env)
    return svn_error_create (SVN_ERR_FS_ALREADY_OPEN, 0, 0, fs->pool,
                             "filesystem object already open");
  else
    return SVN_NO_ERROR;
}


/* A default warning handling function.  */

static void
default_warning_func (void *baton, const char *fmt, ...)
{
  /* The one unforgiveable sin is to fail silently.  Dumping to stderr
     or /dev/tty is not acceptable default behavior for server
     processes, since those may both be equivalent to /dev/null.  */
  abort ();
}


/* Cleanup functions.  */

/* Close a database in the filesystem FS.
   DB_PTR is a pointer to the DB pointer in *FS to close.
   NAME is the name of the database, for use in error messages.  */
static svn_error_t *
cleanup_fs_db (svn_fs_t *fs, DB **db_ptr, const char *name)
{
  if (*db_ptr)
    {
      DB *db = *db_ptr;
      char *msg = apr_psprintf (fs->pool, "closing `%s' database", name);
      int db_err;

      *db_ptr = 0;
      db_err = db->close (db, 0);

      /* We can ignore DB_INCOMPLETE on db->close and db->sync; it
       * just means someone else was using the db at the same time
       * we were.  See the Berkeley documentation at:
       * http://www.sleepycat.com/docs/ref/program/errorret.html#DB_INCOMPLETE
       * http://www.sleepycat.com/docs/api_c/db_close.html
       */
      if (db_err == DB_INCOMPLETE)
        db_err = 0;

      SVN_ERR (DB_WRAP (fs, msg, db_err));
    }

  return SVN_NO_ERROR;
}

/* Close whatever Berkeley DB resources are allocated to FS.  */
static svn_error_t *
cleanup_fs (svn_fs_t *fs)
{
  DB_ENV *env = fs->env;

  if (! env)
    return SVN_NO_ERROR;

  /* Close the databases.  */
  SVN_ERR (cleanup_fs_db (fs, &fs->nodes, "nodes"));
  SVN_ERR (cleanup_fs_db (fs, &fs->revisions, "revisions"));
  SVN_ERR (cleanup_fs_db (fs, &fs->transactions, "transactions"));
  SVN_ERR (cleanup_fs_db (fs, &fs->representations, "representations"));
  SVN_ERR (cleanup_fs_db (fs, &fs->strings, "strings"));

  /* Checkpoint any changes.  */
  {
    int db_err = txn_checkpoint (env, 0, 0, 0);

    while (db_err == DB_INCOMPLETE)
      {
        apr_sleep (1000000L); /* microseconds, so 1000000L == 1 second */
        db_err = txn_checkpoint (env, 0, 0, 0);
      }

    /* If the environment was not (properly) opened, then txn_checkpoint
       will typically return EINVAL. Ignore this case.

       Note: we're passing awfully simple values to txn_checkpoint. Any
             possible EINVAL result is caused entirely by issues internal
             to the DB. We should be safe to ignore EINVAL even if
             something other than open-failure causes the result code.
             (especially because we're just trying to close it down)
    */
    if (db_err != 0 && db_err != EINVAL)
      {
        SVN_ERR (DB_WRAP (fs, "checkpointing environment", db_err));
      }
  }
      
  /* Finally, close the environment.  */
  fs->env = 0;
  SVN_ERR (DB_WRAP (fs, "closing environment",
                    env->close (env, 0)));

  return SVN_NO_ERROR;
}


/* An APR pool cleanup function for a filesystem.  DATA must be a
   pointer to the filesystem to clean up.

   When the filesystem object's pool is freed, we want the resources
   held by Berkeley DB to go away, just like everything else.  So we
   register this cleanup function with the filesystem's pool, and let
   it take care of closing the databases, the environment, and any
   other DB objects we might be using.  APR calls this function before
   actually freeing the pool's memory.

   It's a pity that we can't return an svn_error_t object from an APR
   cleanup function.  For now, we return the rather generic
   SVN_ERR_FS_CLEANUP, and store a pointer to the real svn_error_t
   object in *(FS->cleanup_error), for someone else to discover, if
   they like.  */

static apr_status_t
cleanup_fs_apr (void *data)
{
  svn_fs_t *fs = (svn_fs_t *) data;
  svn_error_t *svn_err = cleanup_fs (fs);

  if (! svn_err)
    return APR_SUCCESS;
  else
    {
      /* Try to pass the error back up to the caller, if they're
         prepared to receive it.  Don't overwrite a previously stored
         error --- in a cascade, the first message is usually the most
         helpful.  */
      if (fs->cleanup_error 
          && ! *fs->cleanup_error)
        *fs->cleanup_error = svn_err;
      else
        /* If we can't return this error, print it as a warning.
           (Feel free to replace this with some more sensible
           behavior.  I just don't want to throw any information into
           the bit bucket.)  */
        (*fs->warning) (fs->warning_baton, "%s", svn_err->message);
      
      return SVN_ERR_FS_CLEANUP;
    }
}


/* Allocating and freeing filesystem objects.  */

svn_fs_t *
svn_fs_new (apr_pool_t *parent_pool)
{
  svn_fs_t *new;

  /* Allocate a new filesystem object in its own pool, which is a
     subpool of POOL.  */
  {
    apr_pool_t *pool = svn_pool_create (parent_pool);

    new = apr_pcalloc (pool, sizeof (svn_fs_t));
    new->pool = pool;
  }

  new->warning = default_warning_func;

  apr_pool_cleanup_register (new->pool, (void *) new,
                             (apr_status_t (*) (void *)) cleanup_fs_apr,
                             apr_pool_cleanup_null);

  return new;
}


void
svn_fs_set_warning_func (svn_fs_t *fs,
                         svn_fs_warning_callback_t warning,
                         void *warning_baton)
{
  fs->warning = warning;
  fs->warning_baton = warning_baton;
}


svn_error_t *
svn_fs_set_berkeley_errcall (svn_fs_t *fs, 
                             void (*db_errcall_fcn) (const char *errpfx,
                                                     char *msg))
{
  SVN_ERR (svn_fs__check_fs (fs));
  fs->env->set_errcall(fs->env, db_errcall_fcn);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_close_fs (svn_fs_t *fs)
{
  svn_error_t *svn_err = 0;

#if 0   /* Set to 1 for instrumenting. */
  {
    DB_TXN_STAT *t;
    DB_LOCK_STAT *l;
    int db_err;

    /* Print transaction statistics for this DB env. */
    if ((db_err = txn_stat (fs->env, &t)) != 0)
      fprintf (stderr, "Error running txn_stat(): %s", db_strerror (db_err));
    else
      {
        printf ("*** DB txn stats, right before closing env:\n");
        printf ("   Number of txns currently active: %d\n",
                t->st_nactive);
        printf ("   Max number of active txns at any one time: %d\n",
                t->st_maxnactive);
        printf ("   Number of transactions that have begun: %d\n",
                t->st_nbegins);
        printf ("   Number of transactions that have aborted: %d\n",
                t->st_naborts);
        printf ("   Number of transactions that have committed: %d\n",
                t->st_ncommits);
        printf ("   Number of times a thread was forced to wait: %d\n",
                t->st_region_wait);
        printf ("   Number of times a thread didn't need to wait: %d\n",
                t->st_region_nowait);
        printf ("*** End DB txn stats.\n\n");
      }

    /* Print transaction statistics for this DB env. */
    if ((db_err = lock_stat (fs->env, &l)) != 0)
      fprintf (stderr, "Error running lock_stat(): %s", db_strerror (db_err));
    else
      {
        printf ("*** DB lock stats, right before closing env:\n");
        printf ("   The number of current locks: %d\n",
                l->st_nlocks);
        printf ("   Max number of locks at any one time: %d\n",
                l->st_maxnlocks);
        printf ("   Number of current lockers: %d\n",
                l->st_nlockers);
        printf ("   Max number of lockers at any one time: %d\n",
                l->st_maxnlockers);
        printf ("   Number of current objects: %d\n",
                l->st_nobjects);
        printf ("   Max number of objects at any one time: %d\n",
                l->st_maxnobjects);
        printf ("   Total number of locks requested: %d\n",
                l->st_nrequests);
        printf ("   Total number of locks released: %d\n",
                l->st_nreleases);
        printf ("   Total number of lock reqs failed because "
                "DB_LOCK_NOWAIT was set: %d\n", l->st_nnowaits);
        printf ("   Total number of locks not immediately available "
                "due to conflicts: %d\n", l->st_nconflicts);
        printf ("   Number of deadlocks detected: %d\n", l->st_ndeadlocks);
        printf ("   Number of times a thread waited before "
                "obtaining the region lock: %d\n", l->st_region_wait);
        printf ("   Number of times a thread didn't have to wait: %d\n",
                l->st_region_nowait);
        printf ("*** End DB lock stats.\n\n");
      }
  }
#endif /* 0/1 */

  /* We've registered cleanup_fs_apr as a cleanup function for this
     pool, so just freeing the pool should shut everything down
     nicely.  But do catch an error, if one occurs.  */
  fs->cleanup_error = &svn_err;
  svn_pool_destroy (fs->pool); 

  return svn_err;
}



/* Allocating an appropriate Berkeley DB environment object.  */

/* Allocate a Berkeley DB environment object for the filesystem FS,
   and set up its default parameters appropriately.  */
static svn_error_t *
allocate_env (svn_fs_t *fs)
{
  /* Allocate a Berkeley DB environment object.  */
  SVN_ERR (DB_WRAP (fs, "allocating environment object",
                    db_env_create (&fs->env, 0)));

  /* If we detect a deadlock, select a transaction to abort at random
     from those participating in the deadlock.  */
  SVN_ERR (DB_WRAP (fs, "setting deadlock detection policy",
                    fs->env->set_lk_detect (fs->env, DB_LOCK_RANDOM)));

  return SVN_NO_ERROR;
}



/* Creating a new Berkeley DB-based filesystem.  */

/* Return APR_SUCCESS if directory PATH is an empty directory,
   APR_EGENERAL if it is not empty, or the associated apr error if
   there was any trouble finding out whether or not it's empty.  */
static apr_status_t
dir_empty (const char *path, apr_pool_t *pool)
{
  apr_status_t apr_err, retval;
  apr_dir_t *dir;
  apr_finfo_t finfo;
  
  apr_err = apr_dir_open (&dir, path, pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return apr_err;
      
  /* All systems return "." and ".." as the first two files, so read
     past them unconditionally. */
  apr_err = apr_dir_read (&finfo, APR_FINFO_NAME, dir);
  if (! APR_STATUS_IS_SUCCESS (apr_err)) return apr_err;
  apr_err = apr_dir_read (&finfo, APR_FINFO_NAME, dir);
  if (! APR_STATUS_IS_SUCCESS (apr_err)) return apr_err;

  /* Now, there should be nothing left.  If there is something left,
     return EGENERAL. */
  apr_err = apr_dir_read (&finfo, APR_FINFO_NAME, dir);
  if (APR_STATUS_IS_ENOENT (apr_err))
    retval = APR_SUCCESS;
  else if (APR_STATUS_IS_SUCCESS (apr_err))
    retval = APR_EGENERAL;
  else
    retval = apr_err;

  apr_err = apr_dir_close (dir);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return apr_err;

  return retval;
}



/* Paths. */

const char *
svn_fs_repository (svn_fs_t *fs, apr_pool_t *pool)
{
  return apr_pstrdup (pool, fs->path);
}


const char *
svn_fs_db_env (svn_fs_t *fs, apr_pool_t *pool)
{
  return apr_pstrdup (pool, fs->env_path);
}


const char *
svn_fs_conf_dir (svn_fs_t *fs, apr_pool_t *pool)
{
  return apr_pstrdup (pool, fs->conf_path);
}


const char *
svn_fs_lock_dir (svn_fs_t *fs, apr_pool_t *pool)
{
  return apr_pstrdup (pool, fs->lock_path);
}


const char *
svn_fs_db_lockfile (svn_fs_t *fs, apr_pool_t *pool)
{
  return apr_pstrcat (pool,
                      fs->lock_path, "/" SVN_FS__REPOS_DB_LOCKFILE,
                      NULL);
}


const char *
svn_fs_hook_dir (svn_fs_t *fs, apr_pool_t *pool)
{
  return apr_pstrdup (pool, fs->hook_path);
}


const char *
svn_fs_start_commit_hook (svn_fs_t *fs, apr_pool_t *pool)
{
  return apr_pstrcat (fs->pool,
                      fs->hook_path, "/" SVN_FS__REPOS_HOOK_START_COMMIT,
                      NULL);
}


const char *
svn_fs_pre_commit_hook (svn_fs_t *fs, apr_pool_t *pool)
{
  return apr_pstrcat (fs->pool,
                      fs->hook_path, "/" SVN_FS__REPOS_HOOK_PRE_COMMIT,
                      NULL);
}


const char *
svn_fs_post_commit_hook (svn_fs_t *fs, apr_pool_t *pool)
{
  return apr_pstrcat (fs->pool,
                      fs->hook_path, "/" SVN_FS__REPOS_HOOK_POST_COMMIT,
                      NULL);
}


const char *
svn_fs_read_sentinel_hook (svn_fs_t *fs, apr_pool_t *pool)
{
  return apr_pstrcat (fs->pool,
                      fs->hook_path, "/" SVN_FS__REPOS_HOOK_READ_SENTINEL,
                      NULL);
}


const char *
svn_fs_write_sentinel_hook (svn_fs_t *fs, apr_pool_t *pool)
{
  return apr_pstrcat (fs->pool,
                      fs->hook_path, "/" SVN_FS__REPOS_HOOK_WRITE_SENTINEL,
                      NULL);
}


static svn_error_t *
create_locks (svn_fs_t *fs, const char *path)
{
  apr_status_t apr_err;

  /* Create the locks directory. */
  fs->lock_path = apr_psprintf (fs->pool, "%s/%s",
                                path, SVN_FS__REPOS_LOCK_DIR);
  apr_err = apr_dir_make (fs->lock_path, APR_OS_DEFAULT, fs->pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf (apr_err, 0, 0, fs->pool,
                              "creating lock dir `%s'", fs->lock_path);

  /* Create the DB lockfile under that directory. */
  {
    apr_file_t *f = NULL;
    apr_size_t written;
    const char *contents;
    const char *lockfile_path;

    lockfile_path = svn_fs_db_lockfile (fs, fs->pool);
    apr_err = apr_file_open (&f, lockfile_path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             fs->pool);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
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
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
                                "writing lock file `%s'", lockfile_path);
    
    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
                                "closing lock file `%s'", lockfile_path);
  }

  return SVN_NO_ERROR;
}


static svn_error_t *
create_hooks (svn_fs_t *fs, const char *path)
{
  const char *this_path, *contents;
  apr_status_t apr_err;
  apr_file_t *f;
  apr_size_t written;

  /* Create the hook directory. */
  fs->hook_path = apr_psprintf (fs->pool, "%s/%s",
                                path, SVN_FS__REPOS_HOOK_DIR);
  apr_err = apr_dir_make (fs->hook_path, APR_OS_DEFAULT, fs->pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf (apr_err, 0, 0, fs->pool,
                              "creating hook directory `%s'", fs->hook_path);

  /* Write a default template for each standard hook file. */

  /* Start-commit hooks. */
  {
    this_path = apr_psprintf (fs->pool, "%s%s",
                              svn_fs_start_commit_hook (fs, fs->pool),
                              SVN_FS__REPOS_HOOK_DESC_EXT);
    
    apr_err = apr_file_open (&f, this_path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             fs->pool);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
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
      SVN_FS__REPOS_HOOK_START_COMMIT
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
      SVN_FS__REPOS_HOOK_START_COMMIT
      "'\n" 
      "# invoke other programs to do the real work, though it may do the\n"
      "# work itself too.\n"
      "#\n"
      "# On a Windows system, you should name the hook program\n"
      "# `" SVN_FS__REPOS_HOOK_START_COMMIT ".bat' or "
      "`" SVN_FS__REPOS_HOOK_START_COMMIT ".exe', but the basic idea is\n"
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
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
                                "writing hook file `%s'", this_path);

    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
                                "closing hook file `%s'", this_path);
  }  /* end start-commit hooks */

  /* Pre-commit hooks. */
  {
    this_path = apr_psprintf (fs->pool, "%s%s",
                              svn_fs_pre_commit_hook (fs, fs->pool),
                              SVN_FS__REPOS_HOOK_DESC_EXT);

    apr_err = apr_file_open (&f, this_path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             fs->pool);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
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
      SVN_FS__REPOS_HOOK_PRE_COMMIT "' (for which\n"
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
      SVN_FS__REPOS_HOOK_PRE_COMMIT
      "'\n" 
      "# invoke other programs to do the real work, though it may do the\n"
      "# work itself too.\n"
      "#\n"
      "# On a Windows system, you should name the hook program\n"
      "# `" SVN_FS__REPOS_HOOK_PRE_COMMIT ".bat' or "
      "`" SVN_FS__REPOS_HOOK_PRE_COMMIT ".exe', but the basic idea is\n"
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
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
                                "writing hook file `%s'", this_path);

    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
                                "closing hook file `%s'", this_path);
  }  /* end pre-commit hooks */

  /* Post-commit hooks. */
  {
    this_path = apr_psprintf (fs->pool, "%s%s",
                              svn_fs_post_commit_hook (fs, fs->pool),
                              SVN_FS__REPOS_HOOK_DESC_EXT);

    apr_err = apr_file_open (&f, this_path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             fs->pool);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
                                "creating hook file `%s'", this_path);
    
    contents =
      "#!/bin/sh\n"
      "\n"
      "# POST-COMMIT HOOK\n"
      "#\n"
      "# The post-commit hook is invoked after a commit. Subversion runs\n"
      "# this hook by invoking a program (script, executable, binary,\n"
      "# etc.) named `" 
      SVN_FS__REPOS_HOOK_POST_COMMIT 
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
      SVN_FS__REPOS_HOOK_POST_COMMIT
      "'\n" 
      "# invoke other programs to do the real work, though it may do the\n"
      "# work itself too.\n"
      "#\n"
      "# On a Windows system, you should name the hook program\n"
      "# `" SVN_FS__REPOS_HOOK_POST_COMMIT ".bat' or "
      "`" SVN_FS__REPOS_HOOK_POST_COMMIT ".exe', but the basic idea is\n"
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
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
                                "writing hook file `%s'", this_path);

    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
                                "closing hook file `%s'", this_path);
  } /* end post-commit hooks */

  /* Read sentinels. */
  {
    this_path = apr_psprintf (fs->pool, "%s%s",
                              svn_fs_read_sentinel_hook (fs, fs->pool),
                              SVN_FS__REPOS_HOOK_DESC_EXT);

    apr_err = apr_file_open (&f, this_path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             fs->pool);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
                                "creating hook file `%s'", this_path);
    
    contents =
      "READ-SENTINEL\n"
      "\n"
      "The invocation convention and protocol for the read-sentinel\n"
      "is yet to be defined.\n"
      "\n";

    apr_err = apr_file_write_full (f, contents, strlen (contents), &written);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
                                "writing hook file `%s'", this_path);

    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
                                "closing hook file `%s'", this_path);
  }  /* end read sentinels */

  /* Write sentinels. */
  {
    this_path = apr_psprintf (fs->pool, "%s%s",
                              svn_fs_write_sentinel_hook (fs, fs->pool),
                              SVN_FS__REPOS_HOOK_DESC_EXT);

    apr_err = apr_file_open (&f, this_path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             fs->pool);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
                                "creating hook file `%s'", this_path);
    
    contents =
      "WRITE-SENTINEL\n"
      "\n"
      "The invocation convention and protocol for the write-sentinel\n"
      "is yet to be defined.\n"
      "\n";

    apr_err = apr_file_write_full (f, contents, strlen (contents), &written);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
                                "writing hook file `%s'", this_path);

    apr_err = apr_file_close (f);
    if (apr_err)
      return svn_error_createf (apr_err, 0, NULL, fs->pool, 
                                "closing hook file `%s'", this_path);
  }  /* end write sentinels */

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_create_berkeley (svn_fs_t *fs, const char *path)
{
  apr_status_t apr_err;
  svn_error_t *svn_err;

  SVN_ERR (check_already_open (fs));

  /* Create the repository directory. */
  apr_err = apr_dir_make (path, APR_OS_DEFAULT, fs->pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    {
      if (APR_STATUS_IS_EEXIST (apr_err))
        {
          apr_status_t empty = dir_empty (path, fs->pool);
          if (! APR_STATUS_IS_SUCCESS (empty))
            return svn_error_createf
              (apr_err, 0, 0, fs->pool,
               "`%s' exists and is non-empty, repository creation failed",
               path);
        }
      else
        {
          return svn_error_createf
            (apr_err, 0, 0, fs->pool, "unable to create repository `%s'",
             path);
        }
    }

  /* Initialize the fs's path. */
  fs->path = apr_pstrdup (fs->pool, path);

  /* Create the DAV sandbox directory.  */
  fs->dav_path = apr_psprintf (fs->pool, "%s/%s",
                               path, SVN_FS__REPOS_DAV_DIR);
  apr_err = apr_dir_make (fs->dav_path, APR_OS_DEFAULT, fs->pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf (apr_err, 0, 0, fs->pool,
                              "creating DAV sandbox dir `%s'", fs->dav_path);

  /* Create the conf directory.  */
  fs->conf_path = apr_psprintf (fs->pool, "%s/%s",
                               path, SVN_FS__REPOS_CONF_DIR);
  apr_err = apr_dir_make (fs->conf_path, APR_OS_DEFAULT, fs->pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf (apr_err, 0, 0, fs->pool,
                              "creating conf dir `%s'", fs->conf_path);

  /* Create the lock directory.  */
  SVN_ERR (create_locks (fs, path));

  /* Create the hooks directory.  */
  SVN_ERR (create_hooks (fs, path));

  /* Create the directory for the new Berkeley DB environment.  */
  fs->env_path = apr_psprintf (fs->pool, "%s/%s", path, SVN_FS__REPOS_DB_DIR);
  apr_err = apr_dir_make (fs->env_path, APR_OS_DEFAULT, fs->pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf (apr_err, 0, 0, fs->pool,
                              "creating Berkeley DB environment dir `%s'",
                              fs->env_path);

  svn_err = allocate_env (fs);
  if (svn_err) goto error;

  /* Create the Berkeley DB environment.  */
  svn_err = DB_WRAP (fs, "creating environment",
                     fs->env->open (fs->env, fs->env_path,
                                    (DB_CREATE
                                     | DB_INIT_LOCK 
                                     | DB_INIT_LOG
                                     | DB_INIT_MPOOL
                                     | DB_INIT_TXN),
                                    0666));
  if (svn_err) goto error;

  /* Create the databases in the environment.  */
  svn_err = DB_WRAP (fs, "creating `nodes' table",
                     svn_fs__open_nodes_table (&fs->nodes, fs->env, 1));
  if (svn_err) goto error;
  svn_err = DB_WRAP (fs, "creating `revisions' table",
                     svn_fs__open_revisions_table (&fs->revisions,
                                                   fs->env, 1));
  if (svn_err) goto error;
  svn_err = DB_WRAP (fs, "creating `transactions' table",
                     svn_fs__open_transactions_table (&fs->transactions,
                                                      fs->env, 1));
  if (svn_err) goto error;
  svn_err = DB_WRAP (fs, "creating `representations' table",
                     svn_fs__open_reps_table (&fs->representations,
                                              fs->env, 1));
  if (svn_err) goto error;
  svn_err = DB_WRAP (fs, "creating `strings' table",
                     svn_fs__open_strings_table (&fs->strings,
                                                 fs->env, 1));
  if (svn_err) goto error;
  svn_err = svn_fs__dag_init_fs (fs);
  if (svn_err) goto error;

  /* Write the top-level README file. */
  {
    const char *readme_contents =
      "This is a Subversion repository; use the `svnadmin' tool to examine\n"
      "it.  Do not add, delete, or modify files here unless you know how\n"
      "to avoid corrupting the repository.\n"
      "\n"
      "The directory \""
      SVN_FS__REPOS_DB_DIR
      "\" contains a Berkeley DB environment.\n"
      "\n"
      "Visit http://subversion.tigris.org/ for more information.\n";

    const char *readme_file_name
      = apr_psprintf (fs->pool, "%s/%s", path, SVN_FS__REPOS_README);

    apr_file_t *readme_file = NULL;
    apr_size_t written = 0;

    apr_err = apr_file_open (&readme_file, readme_file_name,
                             APR_WRITE | APR_CREATE, APR_OS_DEFAULT,
                             fs->pool);
    if (! APR_STATUS_IS_SUCCESS (apr_err))
      return svn_error_createf (apr_err, 0, 0, fs->pool,
                                "opening `%s' for writing", readme_file_name);

    apr_err = apr_file_write_full (readme_file, readme_contents,
                                   strlen (readme_contents), &written);
    if (! APR_STATUS_IS_SUCCESS (apr_err))
      return svn_error_createf (apr_err, 0, 0, fs->pool,
                                "writing to `%s'", readme_file_name);
    
    apr_err = apr_file_close (readme_file);
    if (! APR_STATUS_IS_SUCCESS (apr_err))
      return svn_error_createf (apr_err, 0, 0, fs->pool,
                                "closing `%s'", readme_file_name);
  }

  return SVN_NO_ERROR;

error:
  (void) cleanup_fs (fs);
  return svn_err;
}


/* Gaining access to an existing Berkeley DB-based filesystem.  */


svn_error_t *
svn_fs_open_berkeley (svn_fs_t *fs, const char *path)
{
  svn_error_t *svn_err;

  SVN_ERR (check_already_open (fs));

  /* Initialize paths. */
  fs->path = apr_pstrdup (fs->pool, path);
  fs->dav_path = apr_psprintf (fs->pool, "%s/%s",
                               path, SVN_FS__REPOS_DAV_DIR);
  fs->hook_path = apr_psprintf (fs->pool, "%s/%s",
                                path, SVN_FS__REPOS_HOOK_DIR);
  fs->lock_path = apr_psprintf (fs->pool, "%s/%s",
                                path, SVN_FS__REPOS_LOCK_DIR);
  fs->conf_path = apr_psprintf (fs->pool, "%s/%s",
                                path, SVN_FS__REPOS_CONF_DIR);
  fs->env_path = apr_psprintf (fs->pool, "%s/%s",
                               path, SVN_FS__REPOS_DB_DIR);

  svn_err = allocate_env (fs);
  if (svn_err) goto error;

  /* Open the Berkeley DB environment.  */
  svn_err = DB_WRAP (fs, "opening environment",
                     fs->env->open (fs->env, fs->env_path,
                                    (DB_INIT_LOCK
                                     | DB_INIT_LOG
                                     | DB_INIT_MPOOL
                                     | DB_INIT_TXN),
                                    0666));
  if (svn_err) goto error;

  /* Open the various databases.  */
  svn_err = DB_WRAP (fs, "opening `nodes' table",
                     svn_fs__open_nodes_table (&fs->nodes, fs->env, 0));
  if (svn_err) goto error;
  svn_err = DB_WRAP (fs, "opening `revisions' table",
                     svn_fs__open_revisions_table (&fs->revisions,
                                                   fs->env, 0));
  if (svn_err) goto error;
  svn_err = DB_WRAP (fs, "opening `transactions' table",
                     svn_fs__open_transactions_table (&fs->transactions,
                                                      fs->env, 0));
  if (svn_err) goto error;
  svn_err = DB_WRAP (fs, "creating `representations' table",
                     svn_fs__open_reps_table (&fs->representations,
                                              fs->env, 0));
  if (svn_err) goto error;
  svn_err = DB_WRAP (fs, "creating `strings' table",
                     svn_fs__open_strings_table (&fs->strings,
                                                 fs->env, 0));
  if (svn_err) goto error;

  return SVN_NO_ERROR;
  
 error:
  cleanup_fs (fs);
  return svn_err;
}



/* Running recovery on a Berkeley DB-based filesystem.  */


svn_error_t *
svn_fs_berkeley_recover (const char *path,
                         apr_pool_t *pool)
{
  int db_err;
  DB_ENV *env;
  const char *db_path
    = apr_psprintf (pool, "%s/%s", path, SVN_FS__REPOS_DB_DIR);

  db_err = db_env_create (&env, 0);
  if (db_err)
    return svn_fs__dberr (pool, db_err);

  /* Here's the comment copied from db_recover.c:
   
     Initialize the environment -- we don't actually do anything
     else, that all that's needed to run recovery.
   
     Note that we specify a private environment, as we're about to
     create a region, and we don't want to to leave it around.  If
     we leave the region around, the application that should create
     it will simply join it instead, and will then be running with
     incorrectly sized (and probably terribly small) caches.  */
  db_err = env->open (env, db_path, (DB_RECOVER | DB_CREATE
                                     | DB_INIT_LOCK | DB_INIT_LOG
                                     | DB_INIT_MPOOL | DB_INIT_TXN
                                     | DB_PRIVATE),
                      0666);
  if (db_err)
    return svn_fs__dberr (pool, db_err);

  db_err = env->close (env, 0);
  if (db_err)
    return svn_fs__dberr (pool, db_err);

  return SVN_NO_ERROR;
}



/* Deleting a Berkeley DB-based filesystem.  */


svn_error_t *
svn_fs_delete_berkeley (const char *path,
                        apr_pool_t *pool)
{
  apr_status_t apr_err;
  const char *db_path;
  int db_err;
  DB_ENV *env;

  /* First, use the Berkeley DB library function to remove any shared
     memory segments.  */
  db_path = apr_psprintf (pool, "%s/%s", path, SVN_FS__REPOS_DB_DIR);
  db_err = db_env_create (&env, 0);
  if (db_err)
    return svn_fs__dberr (pool, db_err);
  db_err = env->remove (env, db_path, DB_FORCE);
  if (db_err)
    return svn_fs__dberr (pool, db_err);
  
  /* Remove the repository. */
  apr_err = apr_dir_remove_recursively (path, pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf (apr_err, 0, 0, pool,
                              "recursively removing `%s'", path);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
