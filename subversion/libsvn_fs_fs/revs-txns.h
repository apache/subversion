/* revs-txns.h : internal interface to revision and transactions operations
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

#ifndef SVN_LIBSVN_FS_REVS_TXNS_H
#define SVN_LIBSVN_FS_REVS_TXNS_H

#include "svn_fs.h"

#include "fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Revisions ***/

/* Set property NAME to VALUE on REV in FS, allocation from POOL.  */
svn_error_t *svn_fs_fs__set_rev_prop (svn_fs_t *fs,
                                   svn_revnum_t rev,
                                   const char *name,
                                   const svn_string_t *value,
                                   apr_pool_t *pool);



/*** Transactions ***/

/* Set *REVISION to the revision which was created when FS transaction
   TXN_NAME was committed, or to SVN_INVALID_REVNUM if the transaction
   has not been committed.  Do all allocations in POOL.  */
svn_error_t *svn_fs_fs__txn_get_revision (svn_revnum_t *revision,
                                       svn_fs_t *fs,
                                       const char *txn_name,
                                       apr_pool_t *pool);


/* Retrieve information about the Subversion transaction SVN_TXN from
   the `transactions' table of FS, allocating from POOL.  Set
   *ROOT_ID_P to the ID of the transaction's root directory.  Set
   *BASE_ROOT_ID_P to the ID of the root directory of the
   transaction's base revision.

   If there is no such transaction, SVN_ERR_FS_NO_SUCH_TRANSACTION is
   the error returned.

   Returns SVN_ERR_FS_TRANSACTION_NOT_MUTABLE if TXN_NAME refers to a
   transaction that has already been committed.

   Allocate *ROOT_ID_P and *BASE_ROOT_ID_P in POOL.  */
svn_error_t *svn_fs_fs__get_txn_ids (const svn_fs_id_t **root_id_p,
                                  const svn_fs_id_t **base_root_id_p,
                                  svn_fs_t *fs,
                                  const char *txn_name,
                                  apr_pool_t *pool);


/* These functions implement some of the calls in the FS loader
   library's fs and txn vtables. */
svn_error_t *svn_fs_fs__youngest_rev (svn_revnum_t *youngest_p, svn_fs_t *fs,
                                      apr_pool_t *pool);

svn_error_t *svn_fs_fs__revision_prop (svn_string_t **value_p, svn_fs_t *fs,
                                       svn_revnum_t rev,
                                       const char *propname,
                                       apr_pool_t *pool);

svn_error_t *svn_fs_fs__revision_proplist (apr_hash_t **table_p,
                                           svn_fs_t *fs,
                                           svn_revnum_t rev,
                                           apr_pool_t *pool);

svn_error_t *svn_fs_fs__change_rev_prop (svn_fs_t *fs, svn_revnum_t rev,
                                         const char *name,
                                         const svn_string_t *value,
                                         apr_pool_t *pool);

svn_error_t *svn_fs_fs__begin_txn (svn_fs_txn_t **txn_p, svn_fs_t *fs,
                                   svn_revnum_t rev, apr_pool_t *pool);

svn_error_t *svn_fs_fs__open_txn (svn_fs_txn_t **txn, svn_fs_t *fs,
                                  const char *name, apr_pool_t *pool);

svn_error_t *svn_fs_fs__purge_txn (svn_fs_t *fs, const char *txn_id,
                                   apr_pool_t *pool);

svn_error_t *svn_fs_fs__list_transactions (apr_array_header_t **names_p,
                                           svn_fs_t *fs, apr_pool_t *pool);

svn_error_t *svn_fs_fs__abort_txn (svn_fs_txn_t *txn, apr_pool_t *pool);

svn_error_t *svn_fs_fs__txn_prop (svn_string_t **value_p, svn_fs_txn_t *txn,
                                  const char *propname, apr_pool_t *pool);

svn_error_t *svn_fs_fs__txn_proplist (apr_hash_t **table_p,
                                      svn_fs_txn_t *txn,
                                      apr_pool_t *pool);

svn_error_t *svn_fs_fs__change_txn_prop (svn_fs_txn_t *txn, const char *name,
                                         const svn_string_t *value,
                                         apr_pool_t *pool);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_REVS_TXNS_H */
