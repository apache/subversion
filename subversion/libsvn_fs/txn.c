/* txn.c : implementation of transaction functions
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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

#include <string.h>

#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_pools.h>

#include "svn_pools.h"
#include "svn_time.h"
#include "svn_fs.h"

#include "fs.h"
#include "dag.h"
#include "txn.h"
#include "err.h"
#include "trail.h"
#include "tree.h"
#include "revs-txns.h"
#include "bdb/rev-table.h"
#include "bdb/txn-table.h"
#include "bdb/copies-table.h"
#include "bdb/changes-table.h"


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

  /* The revision on which this transaction is based, or
     SVN_INVALID_REVISION if the transaction is not based on a
     revision at all. */
  svn_revnum_t base_rev;

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
          svn_revnum_t base_rev,
          apr_pool_t *pool)
{
  apr_pool_t *new_pool = svn_pool_create (pool);
  svn_fs_txn_t *txn = apr_pcalloc (new_pool, sizeof (*txn));

  txn->pool = new_pool;
  txn->fs = fs;
  txn->id = id;
  txn->base_rev = base_rev;

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
  const svn_fs_id_t *root_id;
  const char *txn_id;

  SVN_ERR (svn_fs__rev_get_root (&root_id, args->fs, args->rev, trail));
  SVN_ERR (svn_fs__bdb_create_txn (&txn_id, args->fs, root_id, trail));

  *args->txn_p = make_txn (args->fs, txn_id, args->rev, trail->pool);
  return SVN_NO_ERROR;
}




/* Note:  it is acceptable for this function to call back into
   public FS API interfaces because it does not itself use trails.  */
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

  /* Put a datestamp on the newly created txn, so we always know
     exactly how old it is.  (This will help sysadmins identify
     long-abandoned txns that may need to be manually removed.)  When
     a txn is promoted to a revision, this property will be
     automatically overwritten with a revision datestamp. */
  {
    svn_string_t date;

    date.data = svn_time_to_cstring (apr_time_now(), pool);
    date.len = strlen (date.data);

    SVN_ERR (svn_fs_change_txn_prop (txn, SVN_PROP_REVISION_DATE, 
                                     &date, pool));
  }

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


apr_pool_t *
svn_fs_txn_pool (svn_fs_txn_t *txn)
{
  return txn->pool;
}


svn_revnum_t
svn_fs_txn_base_revision (svn_fs_txn_t *txn)
{
  return txn->base_rev;
}



/* Closing transactions. */

svn_error_t *
svn_fs_close_txn (svn_fs_txn_t *txn)
{
  /* Anything done with this transaction was written immediately to
     the filesystem (database), so there's no pending state to flush.
     We can just destroy the pool; the transaction will persist, but
     this handle on it will go away, which is the goal. */
  svn_pool_destroy (txn->pool);

  return SVN_NO_ERROR;
}



/* Aborting transactions. */

struct abort_txn_args
{
  svn_fs_txn_t *txn;
};


static svn_error_t *
txn_body_abort_txn (void *baton, trail_t *trail)
{
  struct abort_txn_args *args = baton;
  svn_fs_txn_t *txn = args->txn;
  const char *txn_id;
  svn_fs__transaction_t *fstxn;

  /* Get the transaction by its id. */
  SVN_ERR (svn_fs_txn_name (&txn_id, txn, txn->pool));
  SVN_ERR (svn_fs__bdb_get_txn (&fstxn, txn->fs, txn_id, trail));

  /* Delete the mutable portion of the tree hanging from the
     transaction. */
  SVN_ERR (svn_fs__dag_delete_if_mutable (txn->fs, fstxn->root_id,
                                          txn_id, trail));

  /* If any copies were made in this transaction, remove those. */
  if (fstxn->copies && fstxn->copies->nelts)
    {
      int i;
      for (i = 0; i < fstxn->copies->nelts; i++)
        {
          const char *copy_id = APR_ARRAY_IDX (fstxn->copies, i, const char *);
          SVN_ERR (svn_fs__bdb_delete_copy (txn->fs, copy_id, trail));
        }
    }

  /* Remove any changes that were stored as part of this
     transactions. */
  SVN_ERR (svn_fs__bdb_changes_delete (txn->fs, txn->id, trail));

  /* Finally, delete the transaction itself. */
  SVN_ERR (svn_fs__bdb_delete_txn (txn->fs, txn->id, trail));

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
  const svn_fs_id_t *root_id;
  const svn_fs_id_t *base_root_id;
  dag_node_t *base_root_node;
  svn_revnum_t base_rev;

  SVN_ERR (svn_fs__get_txn_ids (&root_id, &base_root_id,
                                args->fs, args->name, trail));
  SVN_ERR (svn_fs__dag_get_node (&base_root_node, args->fs, 
                                 base_root_id, trail));
  SVN_ERR (svn_fs__dag_get_revision (&base_rev, base_root_node, trail));
  *args->txn_p = make_txn (args->fs, args->name, base_rev, trail->pool); 
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
  apr_array_header_t **names_p;
  svn_fs_t *fs;
  apr_pool_t *pool;
};

static svn_error_t *
txn_body_list_transactions (void* baton,
                            trail_t *trail)
{
  struct list_transactions_args *args = baton;
  SVN_ERR (svn_fs__bdb_get_txn_list (args->names_p, args->fs, args->pool, trail));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_list_transactions (apr_array_header_t **names_p,
                          svn_fs_t *fs,
                          apr_pool_t *pool)
{
  apr_array_header_t *names;
  struct list_transactions_args args;

  SVN_ERR (svn_fs__check_fs (fs));

  args.names_p = &names;
  args.fs = fs;
  args.pool = pool;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_list_transactions, &args, pool));

  *names_p = names;
  return SVN_NO_ERROR;
}



/*** Accessors. ***/

const char *
svn_fs__txn_id (svn_fs_txn_t *txn)
{
  return txn->id;
}


svn_fs_t *
svn_fs__txn_fs (svn_fs_txn_t *txn)
{
  return txn->fs;
}


apr_pool_t *svn_fs__txn_pool (svn_fs_txn_t *txn)
{
  return txn->pool;
}
