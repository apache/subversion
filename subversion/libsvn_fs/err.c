/*
 * err.c : implementation of fs-private error functions
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



#include <stdlib.h>
#include <stdarg.h>
#include <db.h>
#include "apr_strings.h"
#include "svn_fs.h"
#include "fs.h"
#include "err.h"

svn_error_t *
svn_fs__dberr (apr_pool_t *pool, int db_err)
{
  return svn_error_create (SVN_ERR_BERKELEY_DB,
			   db_err,
			   0,
			   pool,
                           db_strerror (db_err));
}


svn_error_t *
svn_fs__dberrf (apr_pool_t *pool, int db_err, char *fmt, ...)
{
  va_list ap;
  char *msg;

  va_start (ap, fmt);
  msg = apr_pvsprintf (pool, fmt, ap);
  va_end (ap);

  return svn_error_createf (SVN_ERR_BERKELEY_DB, db_err, 0, pool, 
			    "%s%s", msg, db_strerror (db_err));
}


svn_error_t *
svn_fs__wrap_db (svn_fs_t *fs, const char *operation, int db_err)
{
  if (! db_err)
    return 0;
  else
    return svn_fs__dberrf (fs->pool, db_err,
			   "Berkeley DB error while %s for "
			   "filesystem %s:\n", operation,
			   fs->env_path ? fs->env_path : "(none)");
}


svn_error_t *
svn_fs__check_fs (svn_fs_t *fs)
{
  if (fs->env)
    return 0;
  else
    return svn_error_create (SVN_ERR_FS_NOT_OPEN, 0, 0, fs->pool,
			     "filesystem object has not been opened yet");
}


svn_error_t *
svn_fs__retry_txn (svn_fs_t *fs,
		   svn_error_t *(*txn_body) (void *baton,
					     DB_TXN *db_txn),
		   void *baton)
{
  for (;;)
    {
      DB_TXN *db_txn;
      svn_error_t *svn_err;
      
      SVN_ERR (DB_WRAP (fs, "creating transaction (beginning DB transaction)",
			txn_begin (fs->env, 0, &db_txn, 0)));

      /* Do the body of the transaction.  */
      svn_err = (*txn_body) (baton, db_txn);

      if (! svn_err)
	{
	  /* The transaction succeeded!  Commit it.  */
	  SVN_ERR (DB_WRAP (fs,
			    "creating transaction (committing DB transaction)",
			    txn_commit (db_txn, 0)));
	  return 0;
	}

      /* Is this a real error, or do we just need to retry?  */
      if (svn_err->apr_err != SVN_ERR_BERKELEY_DB
	  || svn_err->src_err != DB_LOCK_DEADLOCK)
	{
	  /* Ignore any error returns.  The first error is more valuable.  */
	  txn_abort (db_txn);
	  return svn_err;
	}

      /* We deadlocked.  Abort the transaction, and try again.  */
      SVN_ERR (DB_WRAP (fs, "creating transaction (aborting DB transaction)",
			txn_abort (db_txn)));
    }
}
