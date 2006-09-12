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

#include <apr_pools.h>

#include "svn_time.h"
#include "svn_fs.h"
#include "svn_props.h"

#include "fs.h"
#include "dag.h"
#include "err.h"
#include "tree.h"
#include "revs-txns.h"
#include "key-gen.h"
#include "fs_fs.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"


/*** Helpers ***/

/* Set *TXN_P to a transaction object allocated in POOL for the
   transaction in FS whose id is TXN_ID.  If EXPECT_DEAD is set, this
   transaction must be a dead one, else an error is returned.  If
   EXPECT_DEAD is not set, an error is thrown if the transaction is
   *not* dead. */
static svn_error_t *
get_txn(transaction_t **txn_p,
        svn_fs_t *fs,
        const char *txn_id,
        svn_boolean_t expect_dead,
        apr_pool_t *pool)
{
  transaction_t *txn;
  SVN_ERR(svn_fs_fs__get_txn(&txn, fs, txn_id, pool));
  if (expect_dead && (txn->kind != transaction_kind_dead))
    return svn_error_createf(SVN_ERR_FS_TRANSACTION_NOT_DEAD, 0,
                             _("Transaction is not dead: '%s'"), txn_id);
  if ((! expect_dead) && (txn->kind == transaction_kind_dead))
    return svn_error_createf(SVN_ERR_FS_TRANSACTION_DEAD, 0,
                             _("Transaction is dead: '%s'"), txn_id);
  *txn_p = txn;
  return SVN_NO_ERROR;
}


/*** Revisions ***/

svn_error_t *
svn_fs_fs__revision_prop(svn_string_t **value_p,
                         svn_fs_t *fs,
                         svn_revnum_t rev,
                         const char *propname,
                         apr_pool_t *pool)
{
  apr_hash_t *table;

  SVN_ERR(svn_fs_fs__check_fs(fs));
  SVN_ERR(svn_fs_fs__revision_proplist(&table, fs, rev, pool));

  *value_p = NULL;
  if (table)
    *value_p = apr_hash_get(table, propname, APR_HASH_KEY_STRING);
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__set_rev_prop(svn_fs_t *fs,
                        svn_revnum_t rev,
                        const char *name,
                        const svn_string_t *value,
                        apr_pool_t *pool)
{
  apr_hash_t *table;

  SVN_ERR(svn_fs_fs__revision_proplist(&table, fs, rev, pool));

  apr_hash_set(table, name, APR_HASH_KEY_STRING, value);

  SVN_ERR(svn_fs_fs__set_revision_proplist(fs, rev, table, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__change_rev_prop(svn_fs_t *fs,
                           svn_revnum_t rev,
                           const char *name,
                           const svn_string_t *value,
                           apr_pool_t *pool)
{
  SVN_ERR(svn_fs_fs__check_fs(fs));
  SVN_ERR(svn_fs_fs__set_rev_prop(fs, rev, name, value, pool));

  return SVN_NO_ERROR;
}



/*** Transactions ***/

svn_error_t *
svn_fs_fs__get_txn_ids(const svn_fs_id_t **root_id_p,
                       const svn_fs_id_t **base_root_id_p,
                       svn_fs_t *fs,
                       const char *txn_name,
                       apr_pool_t *pool)
{
  transaction_t *txn;
  
  SVN_ERR(get_txn(&txn, fs, txn_name, FALSE, pool));
  if (txn->kind != transaction_kind_normal)
    return svn_fs_fs__err_txn_not_mutable(fs, txn_name);

  *root_id_p = txn->root_id;
  *base_root_id_p = txn->base_id;
  return SVN_NO_ERROR;
}


/* Generic transaction operations.  */

svn_error_t *
svn_fs_fs__txn_prop(svn_string_t **value_p,
                    svn_fs_txn_t *txn,
                    const char *propname,
                    apr_pool_t *pool)
{
  apr_hash_t *table;
  svn_fs_t *fs = txn->fs;

  SVN_ERR(svn_fs_fs__check_fs(fs));

  SVN_ERR(svn_fs_fs__txn_proplist(&table, txn, pool));

  /* And then the prop from that list (if there was a list). */
  *value_p = NULL;
  if (table)
    *value_p = apr_hash_get(table, propname, APR_HASH_KEY_STRING);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__begin_txn(svn_fs_txn_t **txn_p,
                     svn_fs_t *fs,
                     svn_revnum_t rev,
                     apr_uint32_t flags,
                     apr_pool_t *pool)
{
  svn_string_t date;

  SVN_ERR(svn_fs_fs__check_fs(fs));

  SVN_ERR(svn_fs_fs__create_txn(txn_p, fs, rev, pool));

  /* Put a datestamp on the newly created txn, so we always know
     exactly how old it is.  (This will help sysadmins identify
     long-abandoned txns that may need to be manually removed.)  When
     a txn is promoted to a revision, this property will be
     automatically overwritten with a revision datestamp. */
  date.data = svn_time_to_cstring(apr_time_now(), pool);
  date.len = strlen(date.data);
  SVN_ERR(svn_fs_fs__change_txn_prop(*txn_p, SVN_PROP_REVISION_DATE, 
                                     &date, pool));
  
  /* Set temporary txn props that represent the requested 'flags'
     behaviors. */
  if (flags & SVN_FS_TXN_CHECK_OOD)
    SVN_ERR(svn_fs_fs__change_txn_prop 
            (*txn_p, SVN_FS_PROP_TXN_CHECK_OOD,
             svn_string_create("true", pool), pool));
  
  if (flags & SVN_FS_TXN_CHECK_LOCKS)
    SVN_ERR(svn_fs_fs__change_txn_prop 
            (*txn_p, SVN_FS_PROP_TXN_CHECK_LOCKS,
             svn_string_create("true", pool), pool));
             
  return SVN_NO_ERROR;
}

