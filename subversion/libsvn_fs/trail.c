/* trail.c : backing out of aborted Berkeley DB transactions
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

#include "db.h"
#include "apr_pools.h"
#include "svn_pools.h"
#include "svn_fs.h"
#include "fs.h"
#include "err.h"
#include "trail.h"


/* A single action to be undone.  Actions are chained so that later
   actions point to earlier actions.  Thus, walking the chain and
   applying the functions should undo actions in the reverse of the
   order they were performed.  */
struct undo {

  /* A bitmask indicating when this action should be run.  */
  enum {
    undo_on_failure = 1,
    undo_on_success = 2
  } when;

  void (*func) (void *baton);
  void *baton;
  struct undo *prev;
};


static svn_error_t *
begin_trail (trail_t **trail_p,
             svn_fs_t *fs,
             apr_pool_t *pool)
{
  trail_t *trail = apr_pcalloc (pool, sizeof (*trail));

  trail->pool = svn_pool_create (pool);
  trail->undo = 0;
  SVN_ERR (DB_WRAP (fs, "beginning Berkeley DB transaction",
                    fs->env->txn_begin (fs->env, 0, &trail->db_txn, 0)));

  *trail_p = trail;
  return SVN_NO_ERROR;
}


static svn_error_t *
abort_trail (trail_t *trail,
             svn_fs_t *fs)
{
  struct undo *undo;

  /* Undo those changes which should only persist when the transaction
     succeeds.  */
  for (undo = trail->undo; undo; undo = undo->prev)
    if (undo->when & undo_on_failure)
      undo->func (undo->baton);

  SVN_ERR (DB_WRAP (fs, "aborting Berkeley DB transaction",
                    trail->db_txn->abort (trail->db_txn)));
 
  svn_pool_destroy (trail->pool);

  return SVN_NO_ERROR;
}


static svn_error_t *
commit_trail (trail_t *trail,
              svn_fs_t *fs)
{
  struct undo *undo;

  /* Undo those changes which should persist only while the
     transaction is active.  */
  for (undo = trail->undo; undo; undo = undo->prev)
    if (undo->when & undo_on_success)
      undo->func (undo->baton);

  /* According to the example in the Berkeley DB manual, txn_commit
     doesn't return DB_LOCK_DEADLOCK --- all deadlocks are reported
     earlier.  */
  SVN_ERR (DB_WRAP (fs, "committing Berkeley DB transaction",
                    trail->db_txn->commit (trail->db_txn, 0)));

  /* Do a checkpoint here, if enough has gone on.
     The checkpoint parameters below are pretty arbitrary.  Perhaps
     there should be an svn_fs_berkeley_mumble function to set them.  */
  SVN_ERR (DB_WRAP (fs, "checkpointing after Berkeley DB transaction",
                    fs->env->txn_checkpoint (fs->env, 1024, 5, 0)));

  /* We don't destroy the pool; we assume it contains stuff which will
     be useful beyond the transaction.  */

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__retry_txn (svn_fs_t *fs,
                   svn_error_t *(*txn_body) (void *baton, trail_t *trail),
                   void *baton,
                   apr_pool_t *pool)
{
  for (;;)
    {
      trail_t *trail;
      svn_error_t *svn_err;
      
      SVN_ERR (begin_trail (&trail, fs, pool));

      /* Do the body of the transaction.  */
      svn_err = (*txn_body) (baton, trail);

      if (! svn_err)
        {
          /* The transaction succeeded!  Commit it.  */
          SVN_ERR (commit_trail (trail, fs));

          return SVN_NO_ERROR;
        }

      /* Is this a real error, or do we just need to retry?  */
      if (svn_err->apr_err != SVN_ERR_FS_BERKELEY_DB
          || svn_err->src_err != DB_LOCK_DEADLOCK)
        {
          /* Ignore any error returns.  The first error is more valuable.  */
          abort_trail (trail, fs);
          return svn_err;
        }

      /* We deadlocked.  Abort the transaction, and try again.  */
      SVN_ERR (abort_trail (trail, fs));
    }
}


static void
record_undo (trail_t *trail,
             void (*func) (void *baton),
             void *baton,
             int when)
{
  struct undo *undo = apr_pcalloc (trail->pool, sizeof (*undo));

  undo->when = when;
  undo->func = func;
  undo->baton = baton;
  undo->prev = trail->undo;
  trail->undo = undo;
}
             

void
svn_fs__record_undo (trail_t *trail,
                     void (*func) (void *baton),
                     void *baton)
{
  record_undo (trail, func, baton, undo_on_failure);
}


void
svn_fs__record_completion (trail_t *trail,
                           void (*func) (void *baton),
                           void *baton)
{
  record_undo (trail, func, baton, undo_on_success | undo_on_failure);
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
