/* fs.c --- creating, opening and closing filesystems
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
#include "copies-table.h"
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
  if ((major < SVN_FS_WANT_DB_MAJOR)
      || (major == SVN_FS_WANT_DB_MAJOR && minor < SVN_FS_WANT_DB_MINOR)
      || (major == SVN_FS_WANT_DB_MAJOR && minor == SVN_FS_WANT_DB_MINOR
          && patch < SVN_FS_WANT_DB_PATCH))
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
  SVN_ERR (cleanup_fs_db (fs, &fs->copies, "copies"));
  SVN_ERR (cleanup_fs_db (fs, &fs->representations, "representations"));
  SVN_ERR (cleanup_fs_db (fs, &fs->strings, "strings"));

  /* Checkpoint any changes.  */
  {
    int db_err = env->txn_checkpoint (env, 0, 0, 0);

    while (db_err == DB_INCOMPLETE)
      {
        apr_sleep (1000000L); /* microseconds, so 1000000L == 1 second */
        db_err = env->txn_checkpoint (env, 0, 0, 0);
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
    if ((db_err = fs->env->txn_stat (fs->env, &t, 0)) != 0)
      fprintf (stderr, "Error running fs->env->txn_stat(): %s",
               db_strerror (db_err));
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
    if ((db_err = fs->env->lock_stat (fs->env, &l, 0)) != 0)
      fprintf (stderr, "Error running fs->env->lock_stat(): %s",
               db_strerror (db_err));
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

  /* Berkeley defaults to a 32k log buffer, which is too small for our
     purposes; see this post from Daniel Berlin <dan@dberlin.org>:

     http://subversion.tigris.org/servlets/ReadMsg?msgId=56325&listName=dev

     for details.  Below, we increase it to 256k for better
     throughput.  Note that the size of a logfile must be at least 4
     times this amount; they default to 10 megs, so we're still fine,
     but if you increase the 256 drastically, you'll want to look at
     DB_ENV->set_lg_max(). */
  SVN_ERR (DB_WRAP (fs, "setting in-memory log buffer size",
		    fs->env->set_lg_bsize (fs->env, 256 * 1024)));
  
  return SVN_NO_ERROR;
}



/* Filesystem creation/opening. */
const char *
svn_fs_berkeley_path (svn_fs_t *fs, apr_pool_t *pool)
{
  return apr_pstrdup (pool, fs->path);
}



svn_error_t *
svn_fs_create_berkeley (svn_fs_t *fs, const char *path)
{
  apr_status_t apr_err;
  svn_error_t *svn_err;

  SVN_ERR (check_already_open (fs));

  /* Initialize the fs's path. */
  fs->path = apr_pstrdup (fs->pool, path);

  /* Create the directory for the new Berkeley DB environment.  */
  apr_err = apr_dir_make (fs->path, APR_OS_DEFAULT, fs->pool);
  if (apr_err != APR_SUCCESS)
    return svn_error_createf (apr_err, 0, 0, fs->pool,
                              "creating Berkeley DB environment dir `%s'",
                              fs->path);

  /* Write the DB_CONFIG file. */
  {
    apr_file_t *dbconfig_file = NULL;
    const char *dbconfig_file_name = apr_psprintf (fs->pool,
                                                   "%s/DB_CONFIG", path);
    static const char * const dbconfig_contents =
      "# This is the configuration file for the Berkeley DB environment\n"
      "# used by your Subversion repository.\n"
      "\n"
      "### Lock subsystem\n"
      "#\n"
      "# Make sure you read the documentation at:\n"
      "#\n"
      "#   http://www.sleepycat.com/docs/ref/lock/max.html\n"
      "#\n"
      "# before tweaking these values.\n"
      "set_lk_max_locks   1000\n"
      "set_lk_max_lockers 1000\n"
      "set_lk_max_objects 1000\n";

    apr_err = apr_file_open (&dbconfig_file, dbconfig_file_name,
                             APR_WRITE | APR_CREATE, APR_OS_DEFAULT,
                             fs->pool);
    if (apr_err != APR_SUCCESS)
      return svn_error_createf (apr_err, 0, 0, fs->pool,
                                "opening `%s' for writing", dbconfig_file_name);

    apr_err = apr_file_write_full (dbconfig_file, dbconfig_contents,
                                   strlen (dbconfig_contents), NULL);
    if (apr_err != APR_SUCCESS)
      return svn_error_createf (apr_err, 0, 0, fs->pool,
                                "writing to `%s'", dbconfig_file_name);

    apr_err = apr_file_close (dbconfig_file);
    if (apr_err != APR_SUCCESS)
      return svn_error_createf (apr_err, 0, 0, fs->pool,
                                "closing `%s'", dbconfig_file_name);
  }

  svn_err = allocate_env (fs);
  if (svn_err) goto error;

  /* Create the Berkeley DB environment.  */
  svn_err = DB_WRAP (fs, "creating environment",
                     fs->env->open (fs->env, fs->path,
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
  svn_err = DB_WRAP (fs, "creating `copies' table",
                     svn_fs__open_copies_table (&fs->copies,
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

  /* Initialize the DAG subsystem. */
  svn_err = svn_fs__dag_init_fs (fs);
  if (svn_err) goto error;

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

  svn_err = allocate_env (fs);
  if (svn_err) goto error;

  /* Open the Berkeley DB environment.  */
  svn_err = DB_WRAP (fs, "opening environment",
                     fs->env->open (fs->env, fs->path,
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
  svn_err = DB_WRAP (fs, "opening `copies' table",
                     svn_fs__open_copies_table (&fs->copies,
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
  db_err = env->open (env, path, (DB_RECOVER | DB_CREATE
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
  int db_err;
  DB_ENV *env;

  /* First, use the Berkeley DB library function to remove any shared
     memory segments.  */
  db_err = db_env_create (&env, 0);
  if (db_err)
    return svn_fs__dberr (pool, db_err);
  db_err = env->remove (env, path, DB_FORCE);
  if (db_err)
    return svn_fs__dberr (pool, db_err);

  /* Remove the environment directory. */
  SVN_ERR (svn_io_remove_dir (path, pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
