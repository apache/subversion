/*
 * err.h : interface to routines for returning Berkeley DB errors
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



#ifndef SVN_LIBSVN_FS_ERR_H
#define SVN_LIBSVN_FS_ERR_H

#include "apr_pools.h"
#include "svn_error.h"


/* Return an svn_error_t object that reports a Berkeley DB error.
   DB_ERR is the error value returned by the Berkeley DB routine.
   Allocate the error object from POOL.  */
svn_error_t *svn_fs__dberr (apr_pool_t *pool, int db_err);


/* Allocate an error object for a Berkeley DB error, with a formatted message.

   POOL is the APR pool to allocate the svn_error_t object from.
   DB_ERR is the Berkeley DB error code.
   FMT is a printf-style format string, describing how to format any
      subsequent arguments.

   The svn_error_t object returned has a message consisting of:
   - the text specified by FMT and the subsequent arguments, and
   - the Berkeley DB error message for the error code DB_ERR.

   There is no separator between the two messages; if you want one,
   you should include it in FMT.  */
svn_error_t *svn_fs__dberrf (apr_pool_t *pool, int db_err,
			     char *fmt, ...);


/* Check the return status from the Berkeley DB operation.  If the
   operation succeeded, return zero.  Otherwise, construct an
   appropriate Subversion error object describing what went wrong.
   - FS is the Subversion filesystem we're operating on.
   - OPERATION is a gerund clause describing what we were trying to do.
   - DB_ERR is the return status from the Berkeley DB function.  */
svn_error_t *svn_fs__wrap_db (svn_fs_t *fs,
			      const char *operation,
			      int db_err);


/* A terse wrapper for svn_fs__check_db.  */
#define DB_WRAP(fs, op, err) (svn_fs__wrap_db ((fs), (op), (err)))


/* If EXPR returns a non-zero value, return it.  This is like SVN_ERR,
   but for functions that return a Berkeley DB error code.  */
#define DB_ERR(expr)				\
  do {						\
    int db_err__temp = (expr);			\
    if (db_err__temp)				\
      return db_err__temp;			\
  } while (0)


/* Verify that FS refers to an open database; return an appropriate
   error if this is not the case.  */
svn_error_t *svn_fs__check_fs (svn_fs_t *fs);


/* Try a Berkeley DB transaction repeatedly until it doesn't deadlock.

   That is:
   - Begin a new Berkeley DB transaction, DB_TXN, in the filesystem FS.
   - Apply TXN_BODY to BATON and DB_TXN.  TXN_BODY should try to do
     some series of DB operations which needs to be atomic, using
     DB_TXN as the transaction.  If a DB operation deadlocks, or if
     any other kind of error happens, TXN_BODY should simply return
     with an appropriate svn_error_t.
   - If TXN_BODY returns an error indicating that a deadlock occurred,
     retry the operation.
   - Otherwise, return what TXN_BODY returned.

   One benefit of using this function is that it makes it easy to
   ensure that whatever transactions a filesystem function starts, it
   either aborts or commits before it returns.  If we don't somehow
   complete all our transactions, later operations could deadlock.  */
svn_error_t *svn_fs__retry_txn (svn_fs_t *fs,
				svn_error_t *(*txn_body) (void *baton,
							  DB_TXN *db_txn),
				void *baton);

#endif /* SVN_LIBSVN_FS_ERR_H */
