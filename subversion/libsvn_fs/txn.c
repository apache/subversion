/* txn.c : implementation of transaction functions
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

#include <string.h>

#include "apr_strings.h"
#include "apr_tables.h"
#include "apr_pools.h"

#include "svn_fs.h"

#include "fs.h"
#include "dag.h"
#include "txn.h"
#include "err.h"
#include "trail.h"
#include "rev-table.h"
#include "node-rev.h"
#include "txn-table.h"
#include "tree.h"


/* The private structure underlying the public svn_fs_txn_t typedef.  */

struct svn_fs_txn_t
{
  /* This transaction's private pool, a subpool of fs->pool.

     Freeing this must completely clean up the transaction object,
     write back any buffered data, and release any database or system
     resources it holds.  (But don't confuse the transaction object
     with the transaction it represents: freeing this does *not* abort
     the transaction.)  */
  apr_pool_t *pool;

  /* The filesystem to which this transaction belongs.  */
  svn_fs_t *fs;

  /* The ID of this transaction --- a null-terminated string.
     This is the key into the `transactions' table.  */
  const char *id;
};



/* Creating transactions.  */


/* Allocate and return a new transaction object in POOL for FS whose
   transaction ID is ID.  ID is not copied.  */
static svn_fs_txn_t *
make_txn (svn_fs_t *fs,
          const char *id,
          apr_pool_t *pool)
{
  apr_pool_t *new_pool = svn_pool_create (pool);
  svn_fs_txn_t *txn = apr_pcalloc (new_pool, sizeof (*txn));

  txn->pool = new_pool;
  txn->fs = fs;
  txn->id = id;

  return txn;
}
          

struct begin_txn_args
{
  svn_fs_txn_t **txn_p;
  svn_fs_t *fs;
  svn_revnum_t rev;
};


static svn_error_t *
txn_body_begin_txn (void *baton,
                    trail_t *trail)
{
  struct begin_txn_args *args = baton;
  svn_fs_id_t *root_id;
  char *svn_txn_id;

  SVN_ERR (svn_fs__rev_get_root (&root_id, args->fs, args->rev, trail));
  SVN_ERR (svn_fs__create_txn (&svn_txn_id, args->fs, root_id, trail));

  *args->txn_p = make_txn (args->fs, svn_txn_id, trail->pool);
  return SVN_NO_ERROR;
}




svn_error_t *
svn_fs_begin_txn (svn_fs_txn_t **txn_p,
                  svn_fs_t *fs,
                  svn_revnum_t rev,
                  apr_pool_t *pool)
{
  svn_fs_txn_t *txn;
  struct begin_txn_args args;

  SVN_ERR (svn_fs__check_fs (fs));

  args.txn_p = &txn;
  args.fs    = fs;
  args.rev   = rev;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_begin_txn, &args, pool));
  
  *txn_p = txn;
  return SVN_NO_ERROR;
}



/* Transaction names. */
svn_error_t *
svn_fs_txn_name (const char **name_p,
                 svn_fs_txn_t *txn,
                 apr_pool_t *pool)
{
  *name_p = apr_pstrdup (pool, txn->id);
  return SVN_NO_ERROR;
}


svn_fs_t *
svn_fs_txn_fs (svn_fs_txn_t *txn)
{
  return txn->fs;
}



/* Closing transactions. */

svn_error_t *
svn_fs_close_txn (svn_fs_txn_t *txn)
{
  /* Anything done with this transaction was written immediately to
     the filesystem (database), so there's no pending state to flush.
     We can just destroy the pool; the transaction will persist, but
     this handle on it will go away, which is the goal. */
  apr_pool_destroy (txn->pool);

  return SVN_NO_ERROR;
}



/* Aborting transactions. */

struct abort_txn_args
{
  svn_fs_txn_t *txn;
};


/* Delete all mutable node revisions reachable from node ID, including
   ID itself, from filesystem FS, as part of TRAIL.  ID may refer to
   a file or directory, which may be mutable or immutable.  */
static svn_error_t *
delete_from_id (svn_fs_t *fs, svn_fs_id_t *id, trail_t *trail)
{
  svn_boolean_t is_mutable;
  dag_node_t *node;

  SVN_ERR (svn_fs__dag_get_node (&node, fs, id, trail));

  /* If immutable, do nothing and return immediately. */
  SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, node, trail));
  if (! is_mutable)
    return SVN_NO_ERROR;

  /* Else it's mutable.  Recurse on directories... */
  if (svn_fs__dag_is_directory (node))
    {
      skel_t *entries, *entry;
      SVN_ERR (svn_fs__dag_dir_entries (&entries, node, trail));
          
      for (entry = entries->children; entry; entry = entry->next)
        {
          skel_t *id_skel = entry->children->next;
          svn_fs_id_t *this_id
            = svn_fs_parse_id (id_skel->data, id_skel->len, trail->pool);

          SVN_ERR (delete_from_id (fs, this_id, trail));
        }
    }

  /* ... then delete the node itself. */
  SVN_ERR (svn_fs__delete_node_revision (fs, id, trail));
  
  return SVN_NO_ERROR;
}


static svn_error_t *
txn_body_abort_txn (void *baton, trail_t *trail)
{
  struct abort_txn_args *args = baton;
  svn_fs_txn_t *txn = args->txn;
  const char *txn_name;
  svn_fs_id_t *root_id, *ignored_id;

  SVN_ERR (svn_fs_txn_name (&txn_name, txn, txn->pool));
  SVN_ERR (svn_fs__get_txn (&root_id, &ignored_id, txn->fs, txn_name, trail));
  SVN_ERR (delete_from_id (txn->fs, root_id, trail));
  SVN_ERR (svn_fs__delete_txn (txn->fs, txn->id, trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_abort_txn (svn_fs_txn_t *txn)
{
  struct abort_txn_args args;
  args.txn = txn;
  SVN_ERR (svn_fs__retry_txn (txn->fs, txn_body_abort_txn, &args, txn->pool));
  return SVN_NO_ERROR;
}




/* Committing transactions. */


struct commit_txn_args
{
  svn_fs_txn_t *txn;
  svn_revnum_t new_rev;
  char *conflict;
};


/* ### kff todo: in progress */
static svn_error_t *
txn_body_commit_txn (void *baton, trail_t *trail)
{
#if 0
  struct commit_txn_args *commit_args = baton;

  while (1)
  {
    svn_error_t *err;

    /* Merge any changes since base into the txn. */
    {
      svn_fs_root_t *base_root, *latest_root;
      svn_revnum_t base, latest;
      
      /* Aha.  Houston, we have a problem.
       *
       * The interface to svn_fs_merge() uses svn_fs_root_t's and
       * paths.  If we had the svn_fs_root_t for the base revision here,
       * we'd be fine -- we could call svn_fs_merge() straight.
       *
       * Unfortunately, svn_fs_commit_txn takes just a txn.  The txn
       * gets us a base_root_id and a txn_root_id, but it doesn't tell
       * us what revision that base_root_id came from.  So we can't
       * construct the right svn_fs_root_t to pass to svn_fs_merge()!
       *
       * But we _do_ have all the information we need to do a merge
       * here, that's not at issue.  What is needed is an interface to
       * merging that takes an txn and IDs instead of svn_fs_root_t's
       * and paths.  Sigh.  A little bit of rewriting, but most of the
       * work is (I think) done already.  
       *
       * Wish I had seen this coming. :-(
       *
       * This is not going to happen tonight.  I'll see y'all Monday.
       */
    }

  /* Try to commit the next revision.  If someone already committed
     it, re-merge and try again.  Else break outta here.  */

    err = svn_fs__retry_txn (something here);

    if (err && (err == whatever)) /* commit succeeded */
      {
        SVN_ERR (immutate_nodes (commit_args->txn));
        SVN_ERR (unlock (the revisions table));
        return SVN_NO_ERROR;
      }
    else if (err)
      {
        some other error, return it;
      }
  }

#endif /* 0 */

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_commit_txn (svn_revnum_t *new_rev, 
                   svn_fs_txn_t *txn)
{
  /* How do commits work in Subversion?
   *
   * When you're ready to commit, here's what you have:
   *
   *    1. A transaction, with a mutable tree hanging off it.
   *    2. A base revision, against which TXN_TREE was made.
   *    3. A latest revision, which may be newer than the base rev.
   *
   * The problem is that if latest != base, then one can't simply
   * attach the txn root as the root of the new revision, because that
   * would lose all the changes between base and latest.  It is also
   * not acceptable to insist that base == latest; in a busy
   * repository, commits happen too fast to insist that everyone keep
   * their entire tree up-to-date at all times.  Non-overlapping
   * changes should not interfere with each other.
   *
   * The solution is to merge the changes between base and latest into
   * the txn tree (see svn_fs_merge).  The txn tree is the only one of
   * the three trees that is mutable, so it has to be the one to
   * adjust.
   *
   * You might have to adjust it more than once, if a new latest
   * revision gets committed while you were merging in the previous
   * one.  For example:
   *
   *    1. Jane starts txn T, based at revision 6.
   *    2. Someone commits (or already committed) revision 7.
   *    3. Jane's starts merging the changes between 6 and 7 into T.
   *    4. Meanwhile, someone commits revision 8.
   *    5. Jane finishes the 6-->7 merge.  T could now be committed
   *       against a latest revision of 7, if only that were still the
   *       latest.  Unfortunately, 8 is now the latest, so... 
   *    6. Jane starts merging the changes between 7 and 8 into T.
   *    7. Meanwhile, no one commits any new revisions.  Whew.
   *    8. Jane commits T, creating revision 9, whose tree is exactly
   *       T's tree, except immutable now.
   *
   * In steps 5 and 8, how did Jane know that she couldn't commit in
   * the first case, and could in the second?  The answer is that she
   * simply tried to commit a new revision latest + 1, using the
   * latest revision number as of when she started the merge.  If
   * Berkeley doesn't complain that latest + 1 already exists, then
   * the commit succeeds.  If it does complain, then someone else must
   * have committed during the merge, so Jane does another merge.
   *
   * Lather, rinse, repeat.
   *
   * There are a couple of ways to handle the walk that changes
   * mutable->immutable...
   *
   * Method A:
   *
   *   When the merge is done, but before attempting the commit, walk
   *   the txn tree changing mutable nodes to immutable, and recording
   *   these actions in the trail's undo list.  If the commit fails,
   *   the undo list will be run.
   *
   * Method B:
   *
   *   Leave the nodes immutable when you commit, but make sure
   *   successful commits leave the revision tree locked (the lock
   *   must be created in the same Berkeley transaction that creates
   *   the new revision number).
   *
   *   With this method, if the commit fails, there's no harm done --
   *   your txn tree is still mutable.  If the commit succeeds, then
   *   you have to walk a committed revision tree and make mutables
   *   immutable (at the end of which, you remove that lock).  No one
   *   else can commit a new revision until this is done.
   *
   *   Crash recovery is easier this way, because you can do a
   *   constant-time inspection of the revisions table to know that
   *   there's pending work.  The recovery algorithm is roughly: see
   *   if the revisions table is locked, and if it is, do the
   *   mutable->immutable walk in the latest revision tree, removing
   *   the lock when done (in the same Berkeley transaction as the
   *   mutability changes, of course).
   *
   * Now, method A has a problem: trail undo functions return void,
   * because they're meant to revert in-memory data structures, not db
   * stuff.
   *
   * Method B also has a problem, but maybe not as serious: it
   * increases the amount of time writers wait for writers.  The
   * revisions table would be locked while so-and-so walks her tree
   * immutating nodes.  Big deal.  Let's go with it for now, unless
   * there's a Method C...
   */

  struct commit_txn_args args;

  args.new_rev = SVN_INVALID_REVNUM;
  args.txn = txn;
  SVN_ERR (svn_fs__retry_txn (txn->fs, txn_body_commit_txn, &args, txn->pool));
  *new_rev = args.new_rev;

  return SVN_NO_ERROR;
}



/*** Opening transactions. ***/

struct open_txn_args
{
  svn_fs_txn_t **txn_p;
  svn_fs_t *fs;
  const char *name;
};


static svn_error_t *
txn_body_open_txn (void *baton,
                   trail_t *trail)
{
  struct open_txn_args *args = baton;
  svn_fs_id_t *root_id;
  svn_fs_id_t *base_root_id;

  SVN_ERR (svn_fs__get_txn (&root_id, &base_root_id,
                            args->fs, args->name, trail));

  *args->txn_p = make_txn (args->fs, args->name, trail->pool); 
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_open_txn (svn_fs_txn_t **txn_p,
                 svn_fs_t *fs,
                 const char *name,
                 apr_pool_t *pool)
{
  svn_fs_txn_t *txn;
  struct open_txn_args args;

  SVN_ERR (svn_fs__check_fs (fs));

  args.txn_p = &txn;
  args.fs = fs;
  args.name = name;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_open_txn, &args, pool));
  
  *txn_p = txn;
  return SVN_NO_ERROR;
}


struct list_transactions_args
{
  char ***names_p;
  svn_fs_t *fs;
  apr_pool_t *pool;
};

static svn_error_t *
txn_body_list_transactions (void* baton,
                            trail_t *trail)
{
  struct list_transactions_args *args = baton;
  SVN_ERR (svn_fs__get_txn_list (args->names_p, args->fs, args->pool, trail));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_list_transactions (char ***names_p,
                          svn_fs_t *fs,
                          apr_pool_t *pool)
{
  char **names;
  struct list_transactions_args args;

  SVN_ERR (svn_fs__check_fs (fs));

  args.names_p = &names;
  args.fs = fs;
  args.pool = pool;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_list_transactions, &args, pool));

  *names_p = names;
  return SVN_NO_ERROR;
}

const char *
svn_fs__txn_id (svn_fs_txn_t *txn)
{
  return txn->id;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
