/* revs-txns.c : operations on revision and transactions
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
#include "trail.h"
#include "tree.h"
#include "revs-txns.h"
#include "bdb/rev-table.h"
#include "bdb/txn-table.h"
#include "bdb/copies-table.h"
#include "bdb/changes-table.h"



/*** Revisions ***/

/* Return the committed transaction record *TXN_P and its ID *TXN_ID
   (as long as those parameters aren't NULL) for the revision REV in
   FS as part of TRAIL.  */
static svn_error_t *
get_rev_txn (svn_fs__transaction_t **txn_p,
             const char **txn_id,
             svn_fs_t *fs,
             svn_revnum_t rev,
             trail_t *trail)
{
  svn_fs__revision_t *revision;
  svn_fs__transaction_t *txn;
  
  SVN_ERR (svn_fs__bdb_get_rev (&revision, fs, rev, trail));
  if (revision->txn_id == NULL)
    return svn_fs__err_corrupt_fs_revision (fs, rev);

  SVN_ERR (svn_fs__bdb_get_txn (&txn, fs, revision->txn_id, trail));
  if (txn->revision != rev)
    return svn_fs__err_corrupt_txn (fs, revision->txn_id);

  if (txn_p)
    *txn_p = txn;
  if (txn_id)
    *txn_id = revision->txn_id;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__rev_get_root (const svn_fs_id_t **root_id_p,
                      svn_fs_t *fs,
                      svn_revnum_t rev,
                      trail_t *trail)
{
  svn_fs__transaction_t *txn;
  
  SVN_ERR (get_rev_txn (&txn, NULL, fs, rev, trail));
  if (txn->root_id == NULL)
    return svn_fs__err_corrupt_fs_revision (fs, rev);

  *root_id_p = txn->root_id;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__rev_get_txn_id (const char **txn_id_p,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        trail_t *trail)
{
  svn_fs__revision_t *revision;

  SVN_ERR (svn_fs__bdb_get_rev (&revision, fs, rev, trail));
  if (revision->txn_id == NULL)
    return svn_fs__err_corrupt_fs_revision (fs, rev);

  *txn_id_p = revision->txn_id;
  return SVN_NO_ERROR;
}


struct youngest_rev_args {
  svn_revnum_t youngest;
  svn_fs_t *fs;
};


static svn_error_t *
txn_body_youngest_rev (void *baton,
                       trail_t *trail)
{
  struct youngest_rev_args *args = baton;
  SVN_ERR (svn_fs__bdb_youngest_rev (&(args->youngest), args->fs, trail));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_youngest_rev (svn_revnum_t *youngest_p,
                     svn_fs_t *fs,
                     apr_pool_t *pool)
{
  struct youngest_rev_args args;

  SVN_ERR (svn_fs__check_fs (fs));

  args.fs = fs;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_youngest_rev, &args, pool));

  *youngest_p = args.youngest;
  return SVN_NO_ERROR;
}


struct revision_proplist_args {
  apr_hash_t **table_p;
  svn_fs_t *fs;
  svn_revnum_t rev;
};


static svn_error_t *
txn_body_revision_proplist (void *baton, trail_t *trail)
{
  struct revision_proplist_args *args = baton;
  svn_fs__transaction_t *txn;

  SVN_ERR (get_rev_txn (&txn, NULL, args->fs, args->rev, trail));
  *(args->table_p) = txn->proplist;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_revision_proplist (apr_hash_t **table_p,
                          svn_fs_t *fs,
                          svn_revnum_t rev,
                          apr_pool_t *pool)
{
  struct revision_proplist_args args;
  apr_hash_t *table;

  SVN_ERR (svn_fs__check_fs (fs));

  args.table_p = &table;
  args.fs = fs;
  args.rev = rev;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_revision_proplist, &args, pool));

  *table_p = table ? table : apr_hash_make (pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_revision_prop (svn_string_t **value_p,
                      svn_fs_t *fs,
                      svn_revnum_t rev,
                      const char *propname,
                      apr_pool_t *pool)
{
  struct revision_proplist_args args;
  apr_hash_t *table;

  SVN_ERR (svn_fs__check_fs (fs));

  /* Get the proplist. */
  args.table_p = &table;
  args.fs = fs;
  args.rev = rev;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_revision_proplist, &args, pool));

  /* And then the prop from that list (if there was a list). */
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
                      trail_t *trail)
{
  svn_fs__transaction_t *txn;
  const char *txn_id;

  SVN_ERR (get_rev_txn (&txn, &txn_id, fs, rev, trail));

  /* If there's no proplist, but we're just deleting a property, exit now. */
  if ((! txn->proplist) && (! value))
    return SVN_NO_ERROR;

  /* Now, if there's no proplist, we know we need to make one. */
  if (! txn->proplist)
    txn->proplist = apr_hash_make (trail->pool);

  /* Set the property. */
  apr_hash_set (txn->proplist, name, APR_HASH_KEY_STRING, value);

  /* Overwrite the revision. */
  return svn_fs__bdb_put_txn (fs, txn, txn_id, trail);
}


struct change_rev_prop_args {
  svn_fs_t *fs;
  svn_revnum_t rev;
  const char *name;
  const svn_string_t *value;
};


static svn_error_t *
txn_body_change_rev_prop (void *baton, trail_t *trail)
{
  struct change_rev_prop_args *args = baton;

  SVN_ERR (svn_fs__set_rev_prop (args->fs, args->rev,
                                 args->name, args->value, trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_change_rev_prop (svn_fs_t *fs,
                        svn_revnum_t rev,
                        const char *name,
                        const svn_string_t *value,
                        apr_pool_t *pool)
{
  struct change_rev_prop_args args;

  SVN_ERR (svn_fs__check_fs (fs));

  args.fs = fs;
  args.rev = rev;
  args.name = name;
  args.value = value;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_change_rev_prop, &args, pool));

  return SVN_NO_ERROR;
}



/*** Transactions ***/

static svn_boolean_t 
is_committed (svn_fs__transaction_t *txn)
{
  return SVN_IS_VALID_REVNUM (txn->revision);
}


svn_error_t *
svn_fs__txn_make_committed (svn_fs_t *fs,
                            const char *txn_name,
                            svn_revnum_t revision,
                            trail_t *trail)
{
  svn_fs__transaction_t *txn;

  /* Don't you dare call this with an invalid REVISION. */
  assert (SVN_IS_VALID_REVNUM (revision));

  /* Make sure the TXN is not committed already. */
  SVN_ERR (svn_fs__bdb_get_txn (&txn, fs, txn_name, trail));
  if (is_committed (txn))
    return svn_fs__err_txn_not_mutable (fs, txn_name);

  /* Convert TXN to a committed transaction. */
  txn->revision = revision;
  return svn_fs__bdb_put_txn (fs, txn, txn_name, trail);
}


svn_error_t *
svn_fs__txn_get_revision (svn_revnum_t *revision,
                          svn_fs_t *fs,
                          const char *txn_name,
                          trail_t *trail)
{
  svn_fs__transaction_t *txn;
  SVN_ERR (svn_fs__bdb_get_txn (&txn, fs, txn_name, trail));
  *revision = txn->revision;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__get_txn_ids (const svn_fs_id_t **root_id_p,
                     const svn_fs_id_t **base_root_id_p,
                     svn_fs_t *fs,
                     const char *txn_name,
                     trail_t *trail)
{
  svn_fs__transaction_t *txn;
  
  SVN_ERR (svn_fs__bdb_get_txn (&txn, fs, txn_name, trail));
  if (is_committed (txn))
    return svn_fs__err_txn_not_mutable (fs, txn_name);

  *root_id_p = txn->root_id;
  *base_root_id_p = txn->base_id;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__set_txn_root (svn_fs_t *fs,
                      const char *txn_name,
                      const svn_fs_id_t *new_id,
                      trail_t *trail)
{
  svn_fs__transaction_t *txn;

  SVN_ERR (svn_fs__bdb_get_txn (&txn, fs, txn_name, trail));
  if (is_committed (txn))
    return svn_fs__err_txn_not_mutable (fs, txn_name);

  if (! svn_fs__id_eq (txn->root_id, new_id))
    {
      txn->root_id = new_id;
      SVN_ERR (svn_fs__bdb_put_txn (fs, txn, txn_name, trail));
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__set_txn_base (svn_fs_t *fs,
                      const char *txn_name,
                      const svn_fs_id_t *new_id,
                      trail_t *trail)
{
  svn_fs__transaction_t *txn;

  SVN_ERR (svn_fs__bdb_get_txn (&txn, fs, txn_name, trail));
  if (is_committed (txn))
    return svn_fs__err_txn_not_mutable (fs, txn_name);

  if (! svn_fs__id_eq (txn->base_id, new_id))
    {
      txn->base_id = new_id;
      SVN_ERR (svn_fs__bdb_put_txn (fs, txn, txn_name, trail));
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__add_txn_copy (svn_fs_t *fs,
                      const char *txn_name,
                      const char *copy_id,
                      trail_t *trail)
{
  svn_fs__transaction_t *txn;

  /* Get the transaction and ensure its mutability. */
  SVN_ERR (svn_fs__bdb_get_txn (&txn, fs, txn_name, trail));
  if (is_committed (txn))
    return svn_fs__err_txn_not_mutable (fs, txn_name);

  /* Allocate a new array if this transaction has no copies. */
  if (! txn->copies)
    txn->copies = apr_array_make (trail->pool, 1, sizeof (copy_id));

  /* Add COPY_ID to the array. */
  (*((const char **)(apr_array_push (txn->copies)))) = copy_id;

  /* Finally, write out the transaction. */
  return svn_fs__bdb_put_txn (fs, txn, txn_name, trail);
}



/* Generic transaction operations.  */

struct txn_proplist_args {
  apr_hash_t **table_p;
  svn_fs_t *fs;
  const char *id;
  svn_revnum_t rev;
};


static svn_error_t *
txn_body_txn_proplist (void *baton, trail_t *trail)
{
  svn_fs__transaction_t *txn;
  struct txn_proplist_args *args = baton;

  SVN_ERR (svn_fs__bdb_get_txn (&txn, args->fs, args->id, trail));
  if (is_committed (txn))
    return svn_fs__err_txn_not_mutable (args->fs, args->id);

  *(args->table_p) = txn->proplist; 
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
  args.fs = fs;
  svn_fs_txn_name (&args.id, txn, pool);
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
  args.fs = fs;
  svn_fs_txn_name (&args.id, txn, pool);
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
                      trail_t *trail)
{
  svn_fs__transaction_t *txn;

  SVN_ERR (svn_fs__bdb_get_txn (&txn, fs, txn_name, trail));
  if (is_committed (txn))
    return svn_fs__err_txn_not_mutable (fs, txn_name);

  /* If there's no proplist, but we're just deleting a property, exit now. */
  if ((! txn->proplist) && (! value))
    return SVN_NO_ERROR;

  /* Now, if there's no proplist, we know we need to make one. */
  if (! txn->proplist)
    txn->proplist = apr_hash_make (trail->pool);

  /* Set the property. */
  apr_hash_set (txn->proplist, name, APR_HASH_KEY_STRING, value);

  /* Now overwrite the transaction. */
  return svn_fs__bdb_put_txn (fs, txn, txn_name, trail);
}


static svn_error_t *
txn_body_change_txn_prop (void *baton, trail_t *trail)
{
  struct change_txn_prop_args *args = baton;
  return svn_fs__set_txn_prop (args->fs, args->id, args->name, 
                               args->value, trail);
}


svn_error_t *
svn_fs_change_txn_prop (svn_fs_txn_t *txn,
                        const char *name,
                        const svn_string_t *value,
                        apr_pool_t *pool)
{
  struct change_txn_prop_args args;
  svn_fs_t *fs = txn->fs;

  SVN_ERR (svn_fs__check_fs (fs));

  args.fs = fs;
  svn_fs_txn_name (&args.id, txn, pool);
  args.name = name;
  args.value = value;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_change_txn_prop, &args, pool));

  return SVN_NO_ERROR;
}


/* Allocate and return a new transaction object in POOL for FS whose
   transaction ID is ID.  ID is not copied.  */
static svn_fs_txn_t *
make_txn (svn_fs_t *fs,
          const char *id,
          svn_revnum_t base_rev,
          apr_pool_t *pool)
{
  svn_fs_txn_t *txn = apr_pcalloc (pool, sizeof (*txn));

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
  svn_string_t date;

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
  date.data = svn_time_to_cstring (apr_time_now(), pool);
  date.len = strlen (date.data);
  SVN_ERR (svn_fs_change_txn_prop (txn, SVN_PROP_REVISION_DATE, 
                                   &date, pool));

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


svn_revnum_t
svn_fs_txn_base_revision (svn_fs_txn_t *txn)
{
  return txn->base_rev;
}


static svn_error_t *
txn_body_abort_txn (void *baton, trail_t *trail)
{
  svn_fs_txn_t *txn = baton;
  const char *txn_id = txn->id;
  svn_fs__transaction_t *fstxn;

  /* Get the transaction by its id. */
  SVN_ERR (svn_fs__bdb_get_txn (&fstxn, txn->fs, txn_id, trail));

  /* Verify that this transaction hasn't been assigned a revision (and
     therefore that it is mutable). */
  if (SVN_IS_VALID_REVNUM (fstxn->revision))
    return svn_fs__err_txn_not_mutable (txn->fs, txn_id);

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
          const char *copy_id = 
            APR_ARRAY_IDX (fstxn->copies, i, const char *);
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
svn_fs_abort_txn (svn_fs_txn_t *txn,
                  apr_pool_t *pool)
{
  return svn_fs__retry_txn (txn->fs, txn_body_abort_txn, txn, pool);
}


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
  SVN_ERR (svn_fs__bdb_get_txn_list (args->names_p, args->fs, 
                                     args->pool, trail));

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
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_list_transactions, 
                              &args, pool));

  *names_p = names;
  return SVN_NO_ERROR;
}
