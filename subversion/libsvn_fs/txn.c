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
      SVN_ERR (svn_fs__dag_dir_entries_skel (&entries, node, trail));
          
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
  const char *conflict;
};


static svn_error_t *
txn_body_commit_txn (void *baton, trail_t *trail)
{
  struct commit_txn_args *args = baton;

  svn_fs_txn_t *txn = args->txn;
  svn_revnum_t youngest_rev;
  svn_fs_id_t *txn_root_id, *base_root_id, *youngest_root_id;
  dag_node_t *txn_root_node;

  /* ### kff todo:
   *
   * Completely punting on the question of committing an
   * unmodified txn.  I'm not really sure what should happen in
   * that case.  Consider this sequence of events:
   * 
   *    1.  Bill begins txn T, based on revision 3.
   *    2.  Jane commits revisions 4, 5, and 6.
   *    3.  Bill commits T, never having modified it.
   *
   * T can't conflict with Jane's changes, because Bill made no
   * changes himself -- T is essentially a reversion from 6 to 3.
   * But honestly: could Bill really have meant to revert 4, 5,
   * and 6?  One doubts that he's ever even seen those changes.
   *
   * So should we...
   *
   *    a) Robotically do the merge, pointlessly committing
   *       revision 7 with the same root as revision 6? 
   *
   *    b) Commit revision 7 with the same root as revision 3,
   *       even though it's hard to believe the committer intended
   *       to revert the intervening changes? 
   *
   *    c) Not commit 7 at all?
   *
   * Danged if I know.  Thoughts, anyone?  For now, I'm going with
   * option (c), as it seems the least surprising result.
   */
  
  SVN_ERR (svn_fs__get_txn (&txn_root_id, &base_root_id,
                            txn->fs, txn->id, trail));
  SVN_ERR (svn_fs__dag_get_node (&txn_root_node, txn->fs,
                                 txn_root_id, trail));

  /* See if we need to punt as promised above. */
  {
    svn_boolean_t is_mutable;
    SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, txn_root_node, trail));
    if (! is_mutable)
      {
        /* ### kff todo: not sure this is really what this error was
           designed for, but don't want to create a new error for
           behavior that may or may not change soon anyway. */
        return svn_error_createf
          (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
           "Pointless attempt to commit unchanged txn `%s'", txn->id);
      }
  }

  SVN_ERR (svn_fs__youngest_rev (&youngest_rev, txn->fs, trail));
  SVN_ERR (svn_fs__rev_get_root (&youngest_root_id, txn->fs,
                                 youngest_rev, trail));
  
  if (! svn_fs_id_eq (base_root_id, youngest_root_id))
    {
      /* ### kff todo:
       *
       * We don't use svn_fs_merge here, because uses the
       * root/path public interface, and we've got dag nodes and
       * trails here.
       */
      
      svn_error_t *err;
      const char *conflict;
      dag_node_t *base_root_node, *youngest_root_node;
  
      SVN_ERR (svn_fs__dag_get_node (&base_root_node, txn->fs,
                                     base_root_id, trail));
      SVN_ERR (svn_fs__dag_get_node (&youngest_root_node, txn->fs,
                                     youngest_root_id, trail));

      err = svn_fs__dag_merge (&conflict,
                               "",
                               youngest_root_node,
                               txn_root_node,
                               base_root_node,
                               trail);

      if (err && (err->apr_err == SVN_ERR_FS_CONFLICT))
        {
          args->conflict = conflict;
          return err;
        }
      else if (err)
        return err;
    }

  /* Any merging has been done.  Commit the txn. */
  SVN_ERR (svn_fs__dag_commit_txn (&(args->new_rev), txn->fs, txn->id, trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_commit_txn (const char **conflict_p,
                   svn_revnum_t *new_rev, 
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
   * the txn tree (see svn_fs__dag_merge).  The txn tree is the only
   * one of the three trees that is mutable, so it has to be the one
   * to adjust.
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
   * Lather, rinse, repeat.
   */

  svn_error_t *err;
  struct commit_txn_args args;
  svn_revnum_t youngest_rev, last_youngest_rev;

  /* Get the current youngest revision. */
  SVN_ERR (svn_fs_youngest_rev (&youngest_rev, txn->fs, txn->pool));

  do {
    last_youngest_rev = youngest_rev;
    
    /* Try to commit. */
    args.txn = txn;
    err = svn_fs__retry_txn (txn->fs, txn_body_commit_txn,
                             &args, txn->pool);
    
    if (! err)
      {
        *new_rev = args.new_rev;
        break;
      }
    else if (err && (err->apr_err == SVN_ERR_FS_CONFLICT))
      {
        *conflict_p = args.conflict;
        return err;
      }
    else if (err)
      {
        /* Another possibility is that someone finished committing a
           new revision while we were in mid-commit.  If so, updating
           youngest_rev here will cause the loop to run again. */  
        SVN_ERR (svn_fs_youngest_rev (&youngest_rev, txn->fs, txn->pool));
      }
  } while (youngest_rev != last_youngest_rev);

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
