/* fs.c --- creating, opening and closing filesystems
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "apr_general.h"
#include "apr_pools.h"
#include "apr_file_io.h"
#include "db.h"
#include "svn_fs.h"
#include "fs.h"
#include "err.h"
#include "nodes-table.h"
#include "clones-table.h"
#include "rev-table.h"
#include "txn-table.h"
#include "dag.h"


/* Checking for return values, and reporting errors.  */


/* If FS is already open, then return an SVN_ERR_FS_ALREADY_OPEN
   error.  Otherwise, return zero.  */
static svn_error_t *
check_already_open (svn_fs_t *fs)
{
  if (fs->env)
    return svn_error_create (SVN_ERR_FS_ALREADY_OPEN, 0, 0, fs->pool,
                             "filesystem object already open");
  else
    return 0;
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

  return 0;
}

/* Close whatever Berkeley DB resources are allocated to FS.  */
static svn_error_t *
cleanup_fs (svn_fs_t *fs)
{
  DB_ENV *env = fs->env;

  if (! env)
    return 0;

  /* Close the databases.  */
  SVN_ERR (cleanup_fs_db (fs, &fs->nodes, "nodes"));
  SVN_ERR (cleanup_fs_db (fs, &fs->clones, "clones"));
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
    SVN_ERR (DB_WRAP (fs, "checkpointing environment", db_err));
  }
      
  /* Finally, close the environment.  */
  fs->env = 0;
  SVN_ERR (DB_WRAP (fs, "closing environment",
		    env->close (env, 0)));

  return 0;
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
	fs->warning (fs->warning_baton, "%s", svn_err->message);
      
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

  apr_register_cleanup (new->pool, (void *) new,
			(apr_status_t (*) (void *)) cleanup_fs_apr,
			apr_null_cleanup);

  return new;
}


void
svn_fs_set_warning_func (svn_fs_t *fs,
			 svn_fs_warning_callback_t *warning,
			 void *warning_baton)
{
  fs->warning = warning;
  fs->warning_baton = warning_baton;
}


svn_error_t *
svn_fs_close_fs (svn_fs_t *fs)
{
  svn_error_t *svn_err = 0;

  /* We've registered cleanup_fs_apr as a cleanup function for this
     pool, so just freeing the pool should shut everything down
     nicely.  But do catch an error, if one occurs.  */
  fs->cleanup_error = &svn_err;
  apr_destroy_pool (fs->pool); 

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

  return 0;
}



/* Creating a new Berkeley DB-based filesystem.  */


svn_error_t *
svn_fs_create_berkeley (svn_fs_t *fs, const char *path)
{
  apr_status_t apr_err;
  svn_error_t *svn_err;

  SVN_ERR (check_already_open (fs));

  fs->env_path = apr_pstrdup (fs->pool, path);

  /* Create the directory for the new environment.  */
  apr_err = apr_make_dir (path, APR_OS_DEFAULT, fs->pool);
  if (apr_err != 0)
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
  svn_err = DB_WRAP (fs, "creating `clones' table",
		     svn_fs__open_clones_table (&fs->clones, fs->env, 1));
  if (svn_err) goto error;
  svn_err = DB_WRAP (fs, "creating `revisions' table",
		     svn_fs__open_revisions_table (&fs->revisions,
                                                   fs->env, 1));
  if (svn_err) goto error;
  svn_err = DB_WRAP (fs, "creating `transactions' table",
		     svn_fs__open_transactions_table (&fs->transactions,
						      fs->env, 1));
  if (svn_err) goto error;
  svn_err = svn_fs__dag_init_fs (fs);
  if (svn_err) goto error;

  return 0;

error:
  cleanup_fs (fs);
  return svn_err;
}


/* Gaining access to an existing filesystem.  */


svn_error_t *
svn_fs_open_berkeley (svn_fs_t *fs, const char *path)
{
  svn_error_t *svn_err;

  SVN_ERR (check_already_open (fs));

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
  svn_err = DB_WRAP (fs, "opening `clones' table",
		     svn_fs__open_clones_table (&fs->clones, fs->env, 0));
  if (svn_err) goto error;
  svn_err = DB_WRAP (fs, "opening `revisions' table",
		     svn_fs__open_revisions_table (&fs->revisions,
                                                   fs->env, 0));
  if (svn_err) goto error;
  svn_err = DB_WRAP (fs, "opening `transactions' table",
		     svn_fs__open_transactions_table (&fs->transactions,
						      fs->env, 0));
  if (svn_err) goto error;

  return 0;
  
 error:
  cleanup_fs (fs);
  return svn_err;
}


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

  return 0;
}
