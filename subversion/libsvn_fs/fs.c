/* fs.c --- creating, opening and closing filesystems
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of Collab.Net.
 */


/* Header files.  */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "apr_general.h"
#include "apr_pools.h"
#include "db.h"
#include "svn_fs.h"
#include "fs.h"
#include "err.h"


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
      SVN_ERR (DB_ERR (fs, msg, db->close (db, 0)));
    }

  return 0;
}

/* Close whatever Berkeley DB resources are allocated to FS.  */
static svn_error_t *
cleanup_fs (svn_fs_t *fs)
{
  /* First, close the databases.  */
  SVN_ERR (cleanup_fs_db (fs, &fs->versions, "versions"));
  SVN_ERR (cleanup_fs_db (fs, &fs->nodes, "nodes"));

  /* Finally, close the environment.  */
  if (fs->env)
    SVN_ERR (DB_ERR (fs, "closing environment",
		     fs->env->close (fs->env, 0)));

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

    new = NEW (pool, svn_fs_t);    
    memset (new, 0, sizeof (*new));
    new->pool = pool;
  }

  new->node_cache = apr_make_hash (new->pool);

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
  SVN_ERR (DB_ERR (fs, "allocating environment object",
		   db_env_create (&fs->env, 0)));

  /* If we detect a deadlock, select a transaction to abort at random
     from those participating in the deadlock.  */
  SVN_ERR (DB_ERR (fs, "setting deadlock detection policy",
		   fs->env->set_lk_detect (fs->env, DB_LOCK_RANDOM)));

  return 0;
}



/* Creating a new Berkeley DB-based filesystem.  */


/* Allocate a DB object, and create a table for it to refer to, for a
   Subversion file system.
   - FS is the filesystem we're creating the table for.
   - *DB_PTR is where we should put the new database pointer.
   - NAME is the name the table should have.
   - TYPE is the access method to use for the table (btree, hash, etc.)
   Return an svn_error_t if anything goes wrong.  */

static svn_error_t *
create_table (svn_fs_t *fs, DB **db_ptr, const char *name, DBTYPE type)
{
  char *obj_msg = alloca (strlen (name) + 50);
  char *table_msg = alloca (strlen (name) + 50);

  sprintf (obj_msg, "allocating `%s' table object", name);
  SVN_ERR (DB_ERR (fs, obj_msg, db_create (db_ptr, fs->env, 0)));

  sprintf (table_msg, "creating `%s' table", name);
  SVN_ERR (DB_ERR (fs, table_msg,
		   (*db_ptr)->open (*db_ptr, name, 0, type,
				    DB_CREATE | DB_EXCL,
				    0666)));

  return 0;
}


svn_error_t *
svn_fs_create_berkeley (svn_fs_t *fs, const char *path)
{
  svn_error_t *svn_err;

  SVN_ERR (check_already_open (fs));

  svn_err = allocate_env (fs);
  if (svn_err) goto error;

  /* Create the Berkeley DB environment.  */
  svn_err = DB_ERR (fs, "creating environment",
		    fs->env->open (fs->env, path,
				   (DB_CREATE
				    | DB_INIT_LOCK 
				    | DB_INIT_LOG
				    | DB_INIT_MPOOL
				    | DB_INIT_TXN),
				   0666));
  if (svn_err) goto error;

  /* Create the databases in the environment.  */
  svn_err = create_table (fs, &fs->versions, "versions", DB_BTREE);
  if (svn_err) goto error;
  /* ... don't forget to set the comparison function for the nodes table ... */
  svn_err = create_table (fs, &fs->nodes, "nodes", DB_BTREE);
  if (svn_err) goto error;

  return 0;

error:
  cleanup_fs (fs);
  return svn_err;
}


/* Gaining access to an existing filesystem.  */

/* Allocate a DB object, and use it to open a table in a Subversion
   file system.
   - FS is the filesystem whose table we're opening.
   - *DB_PTR is where we should put the new database pointer.
   - NAME is the name of the table.
   - TYPE is the access method to use for the table (btree, hash, etc.)
   Return an svn_error_t if anything goes wrong.  */

static svn_error_t *
open_table (svn_fs_t *fs, DB **db_ptr, const char *name, DBTYPE type)
{
  char *obj_msg = alloca (strlen (name) + 50);
  char *table_msg = alloca (strlen (name) + 50);

  sprintf (obj_msg, "allocating `%s' table object", name);
  SVN_ERR (DB_ERR (fs, obj_msg, db_create (db_ptr, fs->env, 0)));

  sprintf (table_msg, "opening `%s' table", name);
  SVN_ERR (DB_ERR (fs, table_msg,
		   (*db_ptr)->open (*db_ptr, name, 0, type,
				    DB_EXCL, 0666)));

  return 0;
}


svn_error_t *
svn_fs_open_berkeley (svn_fs_t *fs, const char *path)
{
  svn_error_t *svn_err;

  SVN_ERR (check_already_open (fs));

  svn_err = allocate_env (fs);
  if (svn_err) goto error;

  /* Open the Berkeley DB environment.  */
  svn_err = DB_ERR (fs, "opening environment",
		    fs->env->open (fs->env, path,
				   (DB_INIT_LOCK
				    | DB_INIT_LOG
				    | DB_INIT_MPOOL
				    | DB_INIT_TXN),
				   0666));
  if (svn_err) goto error;

  /* Open the various databases.  */
  svn_err = open_table (fs, &fs->versions, "versions", DB_BTREE);
  if (svn_err) goto error;
  svn_err = open_table (fs, &fs->nodes, "nodes", DB_BTREE);
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
