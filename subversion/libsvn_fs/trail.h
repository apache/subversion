/* trail.h : internal interface to backing out of aborted Berkeley DB txns
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

#ifndef SVN_LIBSVN_FS_TRAIL_H
#define SVN_LIBSVN_FS_TRAIL_H

#include "db.h"
#include "apr_pools.h"
#include "svn_fs.h"


/* A `trail' structure provides a context for a Berkeley DB transaction,
   helping us ensure that:
   - changes made to in-memory structures during an aborted transaction
     are undone, leaving data structures ready for a retry, and
   - memory allocated during an aborted transaction is freed, so we
     don't chew up memory as we retry.  */


struct trail_t
{
  /* A Berkeley DB transaction.  */
  DB_TXN *db_txn;

  /* A pool to allocate things in as part of that transaction --- a
     subpool of the one passed to `begin_trail'.  We destroy this pool
     if we abort the transaction, and leave it around otherwise.  */
  apr_pool_t *pool;

  /* A record of the side-effects to be undone if we abort db_txn.  */
  struct undo *undo;
};
typedef struct trail_t trail_t;


/* Try a Berkeley DB transaction repeatedly until it doesn't deadlock.

   That is:
   - Begin a new Berkeley DB transaction, DB_TXN, in the filesystem FS.
   - Allocate a subpool of POOL, TXN_POOL.
   - Start a new trail, TRAIL based on DB_TXN and TXN_POOL.
   - Apply TXN_BODY to BATON and TRAIL.  TXN_BODY should try to do
     some series of DB operations which needs to be atomic, using
     TRAIL->db_txn as the transaction.  If a DB operation deadlocks, or if
     any other kind of error happens, TXN_BODY should simply return
     with an appropriate svn_error_t, E.
   - If E is a Berkeley DB error indicating that a deadlock occurred,
     unroll the operations recorded in TRAIL->undo, free TRAIL->pool,
     abort TRAIL->db_txn, and retry the operation.
   - Otherwise, return what TXN_BODY returned.

   One benefit of using this function is that it makes it easy to
   ensure that whatever transactions a filesystem function starts, it
   either aborts or commits before it returns.  If we don't somehow
   complete all our transactions, later operations could deadlock.  */
svn_error_t *svn_fs__retry_txn (svn_fs_t *fs,
                                svn_error_t *(*txn_body) (void *baton,
                                                          trail_t *trail),
                                void *baton,
                                apr_pool_t *pool);


/* Record that a change to an in-memory structure took place as part
   of TRAIL which can be undone by applying FUNC to BATON.  */
void svn_fs__record_undo (trail_t *trail,
                          void (*func) (void *baton),
                          void *baton);


#endif /* SVN_LIBSVN_FS_TRAIL_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
