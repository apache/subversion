/* revs-txns.c : operations on revision and transactions
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

#include <assert.h>
#include <string.h>

#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_pools.h>

#include "svn_pools.h"
#include "svn_time.h"
#include "svn_fs.h"

#include "fs.h"
#include "dag.h"
#include "err.h"
#include "id.h"
#include "tree.h"
#include "revs-txns.h"
#include "key-gen.h"
#include "fs_fs.h"

/* These defines are for temporary prototyping purposes until all
   references to BDB can be removed. */
#define svn_fs__retry_txn(a,b,c,d) b(c,d)
#define svn_fs__retry(a,b,c,d) b(c,d)
#define put_txn(a, b, c , d) SVN_NO_ERROR


/*** Helpers ***/

/* Set *txn_p to a transaction object allocated in POOL for the
   transaction in FS whose id is TXN_ID.  If EXPECT_DEAD is set, this
   transaction must be a dead one, else an error is returned.  If
   EXPECT_DEAD is not set, an error is thrown if the transaction is
   *not* dead. */
static svn_error_t *
get_txn (svn_fs__transaction_t **txn_p,
         svn_fs_t *fs,
         const char *txn_id,
         svn_boolean_t expect_dead,
         apr_pool_t *pool)
{
  svn_fs__transaction_t *txn;
  SVN_ERR (svn_fs__fs_get_txn (&txn, fs, txn_id, pool));
  if (expect_dead && (txn->kind != svn_fs__transaction_kind_dead))
    return svn_error_createf (SVN_ERR_FS_TRANSACTION_NOT_DEAD, 0,
                              "Transaction is not dead: '%s'", txn_id);
  if ((! expect_dead) && (txn->kind == svn_fs__transaction_kind_dead))
    return svn_error_createf (SVN_ERR_FS_TRANSACTION_NOT_DEAD, 0,
                              "Transaction is dead: '%s'", txn_id);
  *txn_p = txn;
  return SVN_NO_ERROR;
}


/*** Revisions ***/

/* Return the committed transaction record *TXN_P and its ID *TXN_ID
   (as long as those parameters aren't NULL) for the revision REV in
   FS.  */
static svn_error_t *
get_rev_txn (svn_fs__transaction_t **txn_p,
             const char **txn_id,
             svn_fs_t *fs,
             svn_revnum_t rev,
             apr_pool_t *pool)
{
  abort ();
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__rev_get_root (const svn_fs_id_t **root_id_p,
                      svn_fs_t *fs,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  svn_fs__transaction_t *txn;
  
  SVN_ERR (get_rev_txn (&txn, NULL, fs, rev, pool));
  if (txn->root_id == NULL)
    return svn_fs__err_corrupt_fs_revision (fs, rev);

  *root_id_p = txn->root_id;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__rev_get_txn_id (const char **txn_id_p,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_pool_t *pool)
{
  abort ();
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_youngest_rev (svn_revnum_t *youngest_p,
                     svn_fs_t *fs,
                     apr_pool_t *pool)
{
  svn_revnum_t youngest;
  
  SVN_ERR (svn_fs__check_fs (fs));
  SVN_ERR (svn_fs__fs_youngest_revision (&youngest, fs, pool));
  
  *youngest_p = youngest;
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_revision_proplist (apr_hash_t **table_p,
                          svn_fs_t *fs,
                          svn_revnum_t rev,
                          apr_pool_t *pool)
{
  apr_hash_t *table;

  SVN_ERR (svn_fs__check_fs (fs));
  SVN_ERR (svn_fs__fs_revision_proplist (&table, fs, rev, pool));

  *table_p = table;
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_revision_prop (svn_string_t **value_p,
                      svn_fs_t *fs,
                      svn_revnum_t rev,
                      const char *propname,
                      apr_pool_t *pool)
{
  apr_hash_t *table;

  SVN_ERR (svn_fs__check_fs (fs));
  SVN_ERR (svn_fs__fs_revision_proplist (&table, fs, rev, pool));

  *value_p = NULL;
  if (table)
    *value_p = apr_hash_get (table, propname, APR_HASH_KEY_STRING);
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__set_rev_prop (svn_fs_t *fs,
                      svn_revnum_t rev,
                      const char *name,
                      const svn_string_t *value,
                      apr_pool_t *pool)
{
  apr_hash_t *table;

  SVN_ERR (svn_fs__fs_revision_proplist (&table, fs, rev, pool));

  apr_hash_set (table, name, APR_HASH_KEY_STRING, value);

  SVN_ERR (svn_fs__fs_set_revision_proplist (fs, rev, table, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_change_rev_prop (svn_fs_t *fs,
                        svn_revnum_t rev,
                        const char *name,
                        const svn_string_t *value,
                        apr_pool_t *pool)
{
  SVN_ERR (svn_fs__set_rev_prop (fs, rev, name, value, pool));

  return SVN_NO_ERROR;
}



/*** Transactions ***/

svn_error_t *
svn_fs__txn_make_committed (svn_fs_t *fs,
                            const char *txn_name,
                            svn_revnum_t revision,
                            apr_pool_t *pool)
{
  svn_fs__transaction_t *txn;

  /* Don't you dare call this with an invalid REVISION. */
  assert (SVN_IS_VALID_REVNUM (revision));

  /* Make sure the TXN is not committed already. */
  SVN_ERR (get_txn (&txn, fs, txn_name, FALSE, pool));
  if (txn->kind != svn_fs__transaction_kind_normal)
    return svn_fs__err_txn_not_mutable (fs, txn_name);

  /* Convert TXN to a committed transaction. */
  txn->base_id = NULL;
  txn->revision = revision;
  txn->kind = svn_fs__transaction_kind_committed;
  return put_txn (fs, txn, txn_name, pool);
}


svn_error_t *
svn_fs__txn_get_revision (svn_revnum_t *revision,
                          svn_fs_t *fs,
                          const char *txn_name,
                          apr_pool_t *pool)
{
  svn_fs__transaction_t *txn;
  SVN_ERR (get_txn (&txn, fs, txn_name, FALSE, pool));
  *revision = txn->revision;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__get_txn_ids (const svn_fs_id_t **root_id_p,
                     const svn_fs_id_t **base_root_id_p,
                     svn_fs_t *fs,
                     const char *txn_name,
                     apr_pool_t *pool)
{
  svn_fs__transaction_t *txn;
  
  SVN_ERR (get_txn (&txn, fs, txn_name, FALSE, pool));
  if (txn->kind != svn_fs__transaction_kind_normal)
    return svn_fs__err_txn_not_mutable (fs, txn_name);

  *root_id_p = txn->root_id;
  *base_root_id_p = txn->base_id;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__set_txn_root (svn_fs_t *fs,
                      const char *txn_name,
                      const svn_fs_id_t *new_id,
                      apr_pool_t *pool)
{
  svn_fs__transaction_t *txn;

  SVN_ERR (get_txn (&txn, fs, txn_name, FALSE, pool));
  if (txn->kind != svn_fs__transaction_kind_normal)
    return svn_fs__err_txn_not_mutable (fs, txn_name);

  if (! svn_fs__id_eq (txn->root_id, new_id))
    {
      txn->root_id = new_id;
      SVN_ERR (put_txn (fs, txn, txn_name, pool));
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__set_txn_base (svn_fs_t *fs,
                      const char *txn_name,
                      const svn_fs_id_t *new_id,
                      apr_pool_t *pool)
{
  svn_fs__transaction_t *txn;

  SVN_ERR (get_txn (&txn, fs, txn_name, FALSE, pool));
  if (txn->kind != svn_fs__transaction_kind_normal)
    return svn_fs__err_txn_not_mutable (fs, txn_name);

  if (! svn_fs__id_eq (txn->base_id, new_id))
    {
      txn->base_id = new_id;
      SVN_ERR (put_txn (fs, txn, txn_name, pool));
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__add_txn_copy (svn_fs_t *fs,
                      const char *txn_name,
                      const char *copy_id,
                      apr_pool_t *pool)
{
  svn_fs__transaction_t *txn;

  /* Get the transaction and ensure its mutability. */
  SVN_ERR (get_txn (&txn, fs, txn_name, FALSE, pool));
  if (txn->kind != svn_fs__transaction_kind_normal)
    return svn_fs__err_txn_not_mutable (fs, txn_name);

  /* Allocate a new array if this transaction has no copies. */
  if (! txn->copies)
    txn->copies = apr_array_make (pool, 1, sizeof (copy_id));

  /* Add COPY_ID to the array. */
  (*((const char **)(apr_array_push (txn->copies)))) = copy_id;

  /* Finally, write out the transaction. */
  return put_txn (fs, txn, txn_name, pool);
}



/* Generic transaction operations.  */

struct txn_proplist_args {
  apr_hash_t **table_p;
  const char *id;
  svn_revnum_t rev;
};


static svn_error_t *
txn_body_txn_proplist (void *baton, apr_pool_t *pool)
{
  abort ();
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_txn_proplist (apr_hash_t **table_p,
                     svn_fs_txn_t *txn,
                     apr_pool_t *pool)
{
  struct txn_proplist_args args;
  apr_hash_t *table;
  svn_fs_t *fs = txn->fs;

  SVN_ERR (svn_fs__check_fs (fs));

  args.table_p = &table;
  SVN_ERR (svn_fs_txn_name (&args.id, txn, pool));
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_txn_proplist, &args, pool));

  *table_p = table ? table : apr_hash_make (pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_txn_prop (svn_string_t **value_p,
                 svn_fs_txn_t *txn,
                 const char *propname,
                 apr_pool_t *pool)
{
  struct txn_proplist_args args;
  apr_hash_t *table;
  svn_fs_t *fs = txn->fs;

  SVN_ERR (svn_fs__check_fs (fs));

  /* Get the proplist. */
  args.table_p = &table;
  SVN_ERR (svn_fs_txn_name (&args.id, txn, pool));
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_txn_proplist, &args, pool));

  /* And then the prop from that list (if there was a list). */
  *value_p = NULL;
  if (table)
    *value_p = apr_hash_get (table, propname, APR_HASH_KEY_STRING);
  return SVN_NO_ERROR;
}



struct change_txn_prop_args {
  svn_fs_t *fs;
  const char *id;
  const char *name;
  const svn_string_t *value;
};


svn_error_t *
svn_fs__set_txn_prop (svn_fs_t *fs,
                      const char *txn_name,
                      const char *name,
                      const svn_string_t *value,
                      apr_pool_t *pool)
{
  svn_fs__transaction_t *txn;

  SVN_ERR (get_txn (&txn, fs, txn_name, FALSE, pool));
  if (txn->kind != svn_fs__transaction_kind_normal)
    return svn_fs__err_txn_not_mutable (fs, txn_name);

  /* If there's no proplist, but we're just deleting a property, exit now. */
  if ((! txn->proplist) && (! value))
    return SVN_NO_ERROR;

  /* Now, if there's no proplist, we know we need to make one. */
  if (! txn->proplist)
    txn->proplist = apr_hash_make (pool);

  /* Set the property. */
  apr_hash_set (txn->proplist, name, APR_HASH_KEY_STRING, value);

  /* Now overwrite the transaction. */
  return put_txn (fs, txn, txn_name, pool);
}

svn_error_t *
svn_fs_change_txn_prop (svn_fs_txn_t *txn,
                        const char *name,
                        const svn_string_t *value,
                        apr_pool_t *pool)
{
  svn_fs_t *fs = txn->fs;

  SVN_ERR (svn_fs__check_fs (fs));

  SVN_ERR (svn_fs__fs_change_txn_prop (txn, name, value, pool));

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
  svn_string_t date;

  SVN_ERR (svn_fs__check_fs (fs));

  SVN_ERR (svn_fs__fs_begin_txn (txn_p, fs, rev, pool));

  /* Put a datestamp on the newly created txn, so we always know
     exactly how old it is.  (This will help sysadmins identify
     long-abandoned txns that may need to be manually removed.)  When
     a txn is promoted to a revision, this property will be
     automatically overwritten with a revision datestamp. */
  date.data = svn_time_to_cstring (apr_time_now(), pool);
  date.len = strlen (date.data);
  SVN_ERR (svn_fs_change_txn_prop (*txn_p, SVN_PROP_REVISION_DATE, 
                                   &date, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_txn_name (const char **name_p,
                 svn_fs_txn_t *txn,
                 apr_pool_t *pool)
{
  *name_p = apr_pstrdup (pool, txn->id);
  return SVN_NO_ERROR;
}


svn_revnum_t
svn_fs_txn_base_revision (svn_fs_txn_t *txn)
{
  return txn->base_rev;
}


struct open_txn_args
{
  svn_fs_txn_t **txn_p;
  const char *name;
};


static svn_error_t *
txn_body_open_txn (void *baton,
                   apr_pool_t *pool)
{
  abort ();
  
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
  args.name = name;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_open_txn, &args, pool));
  
  *txn_p = txn;
  return SVN_NO_ERROR;
}


struct cleanup_txn_args
{
  svn_fs__transaction_t **txn_p;
  const char *name;
};


struct get_dirents_args
{
  apr_hash_t **dirents;
  const svn_fs_id_t *id;
  const char *txn_id;
};


static svn_error_t *
txn_body_get_dirents (void *baton, apr_pool_t *pool)
{
  abort ();

  return SVN_NO_ERROR;
}


struct remove_node_args
{
  const svn_fs_id_t *id;
  const char *txn_id;
};


static svn_error_t *
txn_body_remove_node (void *baton, apr_pool_t *pool)
{
  abort ();

  return SVN_NO_ERROR;
}


static svn_error_t *
delete_txn_tree (svn_fs_t *fs,
                 const svn_fs_id_t *id,
                 const char *txn_id,
                 apr_pool_t *pool)
{
  struct get_dirents_args dirent_args;
  struct remove_node_args rm_args;
  apr_hash_t *dirents = NULL;
  apr_hash_index_t *hi;
  svn_error_t *err;

  /* If this sucker isn't mutable, there's nothing to do. */
  if (svn_fs__key_compare (svn_fs__id_txn_id (id), txn_id) != 0)
    return SVN_NO_ERROR;

  /* See if the thing has dirents that need to be recursed upon.  If
     you can't find the thing itself, don't sweat it.  We probably
     already cleaned it up. */
  dirent_args.dirents = &dirents;
  dirent_args.id = id;
  dirent_args.txn_id = txn_id;
  err = svn_fs__retry_txn (fs, txn_body_get_dirents, &dirent_args, pool);
  if (err && (err->apr_err == SVN_ERR_FS_ID_NOT_FOUND))
    {
      svn_error_clear (err);
      return SVN_NO_ERROR;
    }
  SVN_ERR (err);

  /* If there are dirents upon which to recurse ... recurse. */
  if (dirents)
    {
      apr_pool_t *subpool = svn_pool_create (pool);

      /* Loop over hash entries */
      for (hi = apr_hash_first (pool, dirents); hi; hi = apr_hash_next (hi))
        {
          void *val;
          svn_fs_dirent_t *dirent;

          svn_pool_clear (subpool);
          apr_hash_this (hi, NULL, NULL, &val);
          dirent = val;
          SVN_ERR (delete_txn_tree (fs, dirent->id, txn_id, subpool));
        }
      svn_pool_destroy (subpool);
    }

  /* Remove the node. */
  rm_args.id = id;
  rm_args.txn_id = txn_id;
  return svn_fs__retry_txn (fs, txn_body_remove_node, &rm_args, pool);
}


svn_error_t *
svn_fs_purge_txn (svn_fs_t *fs,
                  const char *txn_id,
                  apr_pool_t *pool)
{
  SVN_ERR (svn_fs__check_fs (fs));

  SVN_ERR (svn_fs__fs_purge_txn (fs, txn_id, pool));
  
  return SVN_NO_ERROR;
}


static svn_error_t *
txn_body_abort_txn (void *baton, apr_pool_t *pool)
{
  svn_fs_txn_t *txn = baton;
  svn_fs__transaction_t *fstxn;

  /* Get the transaction by its id, set it to "dead", and store the
     transaction. */
  SVN_ERR (get_txn (&fstxn, txn->fs, txn->id, FALSE, pool));
  if (fstxn->kind != svn_fs__transaction_kind_normal)
    return svn_fs__err_txn_not_mutable (txn->fs, txn->id);

  fstxn->kind = svn_fs__transaction_kind_dead;
  return put_txn (txn->fs, fstxn, txn->id, pool);
}


svn_error_t *
svn_fs_abort_txn (svn_fs_txn_t *txn,
                  apr_pool_t *pool)
{
  SVN_ERR (svn_fs__check_fs (txn->fs));

  /* Set the transaction to "dead". */
  SVN_ERR (svn_fs__retry_txn (txn->fs, txn_body_abort_txn, txn, pool));
  
  /* Now, purge it. */
  SVN_ERR_W (svn_fs_purge_txn (txn->fs, txn->id, pool),
             "Transaction aborted, but cleanup failed");

  return SVN_NO_ERROR;
}


struct list_transactions_args
{
  apr_array_header_t **names_p;
  apr_pool_t *pool;
};

static svn_error_t *
txn_body_list_transactions (void* baton,
                            apr_pool_t *pool)
{
  abort ();

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
  args.pool = pool;
  SVN_ERR (svn_fs__retry (fs, txn_body_list_transactions, &args, pool));

  *names_p = names;
  return SVN_NO_ERROR;
}
