/*
 * err.c : implementation of fs-private error functions
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
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
 * software developed by CollabNet (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
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
 * individuals on behalf of CollabNet.
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
