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

#include "fs.h"
#include "err.h"
#include "trail.h"
#include "id.h"
#include "revs-txns.h"
#include "bdb/rev-table.h"
#include "bdb/txn-table.h"



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



/* Generic revision operations.  */


struct revision_prop_args {
  svn_string_t **value_p;
  svn_fs_t *fs;
  svn_revnum_t rev;
  const char *propname;
};


static svn_error_t *
txn_body_revision_prop (void *baton,
                        trail_t *trail)
{
  struct revision_prop_args *args = baton;
  svn_fs__transaction_t *txn;

  SVN_ERR (get_rev_txn (&txn, NULL, args->fs, args->rev, trail));
  *(args->value_p) = NULL;
  if (txn->proplist)
    *(args->value_p) = apr_hash_get (txn->proplist, args->propname,
                                     APR_HASH_KEY_STRING);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_revision_prop (svn_string_t **value_p,
                      svn_fs_t *fs,
                      svn_revnum_t rev,
                      const char *propname,
                      apr_pool_t *pool)
{
  struct revision_prop_args args;
  svn_string_t *value;

  SVN_ERR (svn_fs__check_fs (fs));

  args.value_p = &value;
  args.fs = fs;
  args.rev = rev;
  args.propname = propname;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_revision_prop, &args, pool));

  *value_p = value;
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
  apr_pool_t *pool = trail->pool;

  SVN_ERR (get_rev_txn (&txn, NULL, args->fs, args->rev, trail));
  *(args->table_p) = txn->proplist ? txn->proplist : apr_hash_make (pool);
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

  *table_p = table;
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

static int 
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


struct txn_prop_args {
  svn_string_t **value_p;
  svn_fs_t *fs;
  const char *id;
  const char *propname;
};


static svn_error_t *
txn_body_txn_prop (void *baton,
                   trail_t *trail)
{
  struct txn_prop_args *args = baton;
  svn_fs__transaction_t *txn;
  
  SVN_ERR (svn_fs__bdb_get_txn (&txn, args->fs, args->id, trail)); 
  if (is_committed (txn))
    return svn_fs__err_txn_not_mutable (args->fs, args->id);

  *(args->value_p) = NULL;
  if (txn->proplist)
    *(args->value_p) = apr_hash_get (txn->proplist, 
                                     args->propname, APR_HASH_KEY_STRING);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_txn_prop (svn_string_t **value_p,
                 svn_fs_txn_t *txn,
                 const char *propname,
                 apr_pool_t *pool)
{
  struct txn_prop_args args;
  svn_string_t *value;
  svn_fs_t *fs = svn_fs_txn_fs (txn);

  SVN_ERR (svn_fs__check_fs (fs));

  args.value_p = &value;
  args.fs = fs;
  svn_fs_txn_name (&args.id, txn, pool);
  args.propname = propname;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_txn_prop, &args, pool));

  *value_p = value;
  return SVN_NO_ERROR;
}


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

  *(args->table_p) = txn->proplist 
                     ? txn->proplist : apr_hash_make (trail->pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_txn_proplist (apr_hash_t **table_p,
                     svn_fs_txn_t *txn,
                     apr_pool_t *pool)
{
  struct txn_proplist_args args;
  apr_hash_t *table;
  svn_fs_t *fs = svn_fs_txn_fs (txn);

  SVN_ERR (svn_fs__check_fs (fs));

  args.table_p = &table;
  args.fs = fs;
  svn_fs_txn_name (&args.id, txn, pool);
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_txn_proplist, &args, pool));

  *table_p = table;
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
  svn_fs_t *fs = svn_fs_txn_fs (txn);

  SVN_ERR (svn_fs__check_fs (fs));

  args.fs = fs;
  svn_fs_txn_name (&args.id, txn, pool);
  args.name = name;
  args.value = value;
  SVN_ERR (svn_fs__retry_txn (fs, txn_body_change_txn_prop, &args, pool));

  return SVN_NO_ERROR;
}
