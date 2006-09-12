/* trail.c : backing out of aborted Berkeley DB transactions
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#define APU_WANT_DB
#include <apu_want.h>

#include <apr_pools.h>
#include "svn_pools.h"
#include "svn_fs.h"
#include "fs.h"
#include "err.h"
#include "bdb/bdb-err.h"
#include "bdb/bdb_compat.h"
#include "trail.h"
#include "../libsvn_fs/fs-loader.h"


#if defined(SVN_FS__TRAIL_DEBUG)

struct trail_debug_t
{
  struct trail_debug_t *prev;
  const char *table;
  const char *op;
};

void
svn_fs_base__trail_debug(trail_t *trail, const char *table, const char *op)
{
  struct trail_debug_t *trail_debug;

  trail_debug = apr_palloc(trail->pool, sizeof(*trail_debug));
  trail_debug->prev = trail->trail_debug;
  trail_debug->table = table;
  trail_debug->op = op;
  trail->trail_debug = trail_debug;
}

static void
print_trail_debug(trail_t *trail,
                  const char *txn_body_fn_name,
                  const char *filename, int line)
{
  struct trail_debug_t *trail_debug;

  fprintf(stderr, "(%s, %s, %u, %u): ",
          txn_body_fn_name, filename, line, trail->db_txn ? 1 : 0);

  trail_debug = trail->trail_debug;
  while (trail_debug)
    {
      fprintf(stderr, "(%s, %s) ", trail_debug->table, trail_debug->op);
      trail_debug = trail_debug->prev;
    }
  fprintf(stderr, "\n");
}
#else
#define print_trail_debug(trail, txn_body_fn_name, filename, line)
#endif /* defined(SVN_FS__TRAIL_DEBUG) */


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

  void (*func)(void *baton);
  void *baton;
  struct undo *prev;
};


static svn_error_t *
begin_trail(trail_t **trail_p,
            svn_fs_t *fs,
            svn_boolean_t use_txn,
            apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  trail_t *trail = apr_pcalloc(pool, sizeof(*trail));

  trail->pool = svn_pool_create(pool);
  trail->fs = fs;
  trail->undo = 0;
  if (use_txn)
    {
      /* [*]
         If we're already inside a trail operation, abort() -- this is
         a coding problem (and will likely hang the repository anyway). */
      if (bfd->in_txn_trail)
        abort();

      SVN_ERR(BDB_WRAP(fs, "beginning Berkeley DB transaction",
                       bfd->bdb->env->txn_begin(bfd->bdb->env, 0,
                                                &trail->db_txn, 0)));
      bfd->in_txn_trail = TRUE;
    }
  else
    {
      trail->db_txn = NULL;
    }

  *trail_p = trail;
  return SVN_NO_ERROR;
}


static svn_error_t *
abort_trail(trail_t *trail)
{
  struct undo *undo;
  svn_fs_t *fs = trail->fs;
  base_fs_data_t *bfd = fs->fsap_data;

  /* Undo those changes which should only persist when the transaction
     succeeds.  */
  for (undo = trail->undo; undo; undo = undo->prev)
    if (undo->when & undo_on_failure)
      undo->func(undo->baton);

  if (trail->db_txn)
    {
      /* [**]
         We have to reset the in_txn_trail flag *before* calling
         DB_TXN->abort().  If we did it the other way around, the next
         call to begin_trail() (e.g., as part of a txn retry) would
         cause an abort, even though there's strictly speaking no
         programming error involved (see comment [*] above).

         In any case, if aborting the txn fails, restarting it will
         most likely fail for the same reason, and so it's better to
         see the returned error than to abort.  An obvious example is
         when DB_TXN->abort() returns DB_RUNRECOVERY. */
      bfd->in_txn_trail = FALSE;
      SVN_ERR(BDB_WRAP(fs, "aborting Berkeley DB transaction",
                       trail->db_txn->abort(trail->db_txn)));
    }
  svn_pool_destroy(trail->pool);

  return SVN_NO_ERROR;
}


static svn_error_t *
commit_trail(trail_t *trail)
{
  struct undo *undo;
  int db_err;
  svn_fs_t *fs = trail->fs;
  base_fs_data_t *bfd = fs->fsap_data;

  /* Undo those changes which should persist only while the
     transaction is active.  */
  for (undo = trail->undo; undo; undo = undo->prev)
    if (undo->when & undo_on_success)
      undo->func(undo->baton);

  /* According to the example in the Berkeley DB manual, txn_commit
     doesn't return DB_LOCK_DEADLOCK --- all deadlocks are reported
     earlier.  */
  if (trail->db_txn)
    {
      /* See comment [**] in abort_trail() above.
         An error during txn commit will abort the transaction anyway. */
      bfd->in_txn_trail = FALSE;
      SVN_ERR(BDB_WRAP(fs, "committing Berkeley DB transaction",
                       trail->db_txn->commit(trail->db_txn, 0)));
    }

  /* Do a checkpoint here, if enough has gone on.
     The checkpoint parameters below are pretty arbitrary.  Perhaps
     there should be an svn_fs_berkeley_mumble function to set them.  */
  db_err = bfd->bdb->env->txn_checkpoint(bfd->bdb->env, 1024, 5, 0);

  /* Pre-4.1 Berkeley documentation says:

        The DB_ENV->txn_checkpoint function returns a non-zero error
        value on failure, 0 on success, and returns DB_INCOMPLETE if
        there were pages that needed to be written to complete the
        checkpoint but that DB_ENV->memp_sync was unable to write
        immediately.

     It's safe to ignore DB_INCOMPLETE if we get it while
     checkpointing.  (Post-4.1 Berkeley doesn't have DB_INCOMPLETE
     anymore, so it's not an issue there.)  */
  if (db_err)
    {
#if SVN_BDB_HAS_DB_INCOMPLETE
      if (db_err != DB_INCOMPLETE)
#endif /* SVN_BDB_HAS_DB_INCOMPLETE */
        {
          return svn_fs_bdb__wrap_db
            (fs, "checkpointing after Berkeley DB transaction", db_err);
        }
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
do_retry(svn_fs_t *fs,
         svn_error_t *(*txn_body)(void *baton, trail_t *trail),
         void *baton,
         svn_boolean_t use_txn,
         apr_pool_t *pool,
         const char *txn_body_fn_name,
         const char *filename,
         int line)
{
  for (;;)
    {
      trail_t *trail;
      svn_error_t *svn_err, *err;
      svn_boolean_t deadlocked = FALSE;

      SVN_ERR(begin_trail(&trail, fs, use_txn, pool));

      /* Do the body of the transaction.  */
      svn_err = (*txn_body)(baton, trail);

      if (! svn_err)
        {
          /* The transaction succeeded!  Commit it.  */
          SVN_ERR(commit_trail(trail));

          if (use_txn)
            print_trail_debug(trail, txn_body_fn_name, filename, line);

          return SVN_NO_ERROR;
        }

      /* Search for a deadlock error on the stack. */
      for (err = svn_err; err; err = err->child)
        if (err->apr_err == SVN_ERR_FS_BERKELEY_DB_DEADLOCK)
          deadlocked = TRUE;

      /* Is this a real error, or do we just need to retry?  */
      if (! deadlocked)
        {
          /* Ignore any error returns.  The first error is more valuable.  */
          svn_error_clear(abort_trail(trail));
          return svn_err;
        }

      svn_error_clear(svn_err);

      /* We deadlocked.  Abort the transaction, and try again.  */
      SVN_ERR(abort_trail(trail));
    }
}


svn_error_t *
svn_fs_base__retry_debug(svn_fs_t *fs,
                         svn_error_t *(*txn_body)(void *baton, trail_t *trail),
                         void *baton,
                         apr_pool_t *pool,
                         const char *txn_body_fn_name,
                         const char *filename,
                         int line)
{
  return do_retry(fs, txn_body, baton, TRUE, pool,
                  txn_body_fn_name, filename, line);
}


#if defined(SVN_FS__TRAIL_DEBUG)
#undef svn_fs_base__retry_txn
#endif

svn_error_t *
svn_fs_base__retry_txn(svn_fs_t *fs,
                       svn_error_t *(*txn_body)(void *baton, trail_t *trail),
                       void *baton,
                       apr_pool_t *pool)
{
  return do_retry(fs, txn_body, baton, TRUE, pool,
                  "unknown", "", 0);
}


svn_error_t *
svn_fs_base__retry(svn_fs_t *fs,
                   svn_error_t *(*txn_body)(void *baton, trail_t *trail),
                   void *baton,
                   apr_pool_t *pool)
{
  return do_retry(fs, txn_body, baton, FALSE, pool,
                  NULL, NULL, 0);
}



static void
record_undo(trail_t *trail,
            void (*func)(void *baton),
            void *baton,
            int when)
{
  struct undo *undo = apr_pcalloc(trail->pool, sizeof(*undo));

  undo->when = when;
  undo->func = func;
  undo->baton = baton;
  undo->prev = trail->undo;
  trail->undo = undo;
}


void
svn_fs_base__record_undo(trail_t *trail,
                         void (*func)(void *baton),
                         void *baton)
{
  record_undo(trail, func, baton, undo_on_failure);
}


void
svn_fs_base__record_completion(trail_t *trail,
                               void (*func)(void *baton),
                               void *baton)
{
  record_undo(trail, func, baton, undo_on_success | undo_on_failure);
}
