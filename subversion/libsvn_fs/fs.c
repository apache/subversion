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
      char *msg = alloca (strlen (name) + 50);
      sprintf (msg, "closing `%s' database", name);

      *db_ptr = 0;
      SVN_ERR (DB_WRAP (fs, msg, db->close (db, 0)));
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

  /* Checkpoint any changes.  */
  {
    int db_err = txn_checkpoint (env, 0, 0, 0);

    while (db_err == DB_INCOMPLETE)
      {
        apr_sleep (1000000L);
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


svn_error_t *
svn_fs_create_berkeley (svn_fs_t *fs, const char *path)
{
  apr_status_t apr_err;
  svn_error_t *svn_err;

  SVN_ERR (check_already_open (fs));

  fs->env_path = apr_pstrdup (fs->pool, path);

  /* Create the directory for the new environment (if needed).  */
  apr_err = apr_dir_make (path, APR_OS_DEFAULT, fs->pool);
  if (apr_err != 0 && !APR_STATUS_IS_EEXIST(apr_err))
    return svn_error_createf (apr_err, 0, 0, fs->pool,
                              "creating Berkeley DB environment dir `%s'",
                              path);

  svn_err = allocate_env (fs);
  if (svn_err) goto error;

  /* Create the Berkeley DB environment.  */
  svn_err = DB_WRAP (fs, "creating environment",
                     fs->env->open (fs->env, path,
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

  fs->env_path = apr_pstrdup (fs->pool, path);

  svn_err = allocate_env (fs);
  if (svn_err) goto error;

  /* Open the Berkeley DB environment.  */
  svn_err = DB_WRAP (fs, "opening environment",
                     fs->env->open (fs->env, path,
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


/* Return zero if STATUS is APR_SUCCESS.  Otherwise, return a
   Subversion error wrapping STATUS.  */
static svn_error_t *
check_apr (apr_status_t status, apr_pool_t *pool)
{
  if (status == APR_SUCCESS)
    return SVN_NO_ERROR;
  else
    return svn_error_create (status, 0, 0, pool, "");
}


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
  
  /* Now, delete all the files in the directory.  */
  {
    apr_dir_t *dir;

    SVN_ERR (check_apr (apr_dir_open (&dir, path, pool), pool));
    for (;;)
      {
        apr_status_t status;
        apr_finfo_t finfo;

        status = apr_dir_read (&finfo, APR_FINFO_NAME, dir);
        if (APR_STATUS_IS_ENOENT(status))
          break;
        SVN_ERR (check_apr (status, pool));

        /* Delete every file, except the `.' and `..' links.  */
        if (strcmp (finfo.name, ".")
            && strcmp (finfo.name, ".."))
          {
            /* Of course, APR has already done this concatenation for us,
               and thrown away the result.  */
            char *fullname = apr_pstrcat (pool, path, "/", finfo.name, 0);
            SVN_ERR (check_apr (apr_file_remove (fullname, pool), pool));
          }
      }
    SVN_ERR (check_apr (apr_dir_close (dir), pool));
  }

  /* Now, delete the directory itself.  */
  SVN_ERR (check_apr (apr_dir_remove (path, pool), pool));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
