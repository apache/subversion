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



/* The private structure underlying the public svn_fs_txn_t typedef.  */

struct svn_fs_txn_t
{
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



/*** Revisions ***/

/* Set *ROOT_ID_P to the ID of the root directory of revision REV in
   FS.  Allocate the ID in POOL.  */
svn_error_t *svn_fs__rev_get_root (const svn_fs_id_t **root_id_p,
                                   svn_fs_t *fs,
                                   svn_revnum_t rev,
                                   apr_pool_t *pool);


/* Set *TXN_ID_P to the ID of the transaction that was committed to
   create REV in FS.  Allocate the ID in POOL.  */
svn_error_t *svn_fs__rev_get_txn_id (const char **txn_id_p,
                                     svn_fs_t *fs,
                                     svn_revnum_t rev,
                                     apr_pool_t *pool);


/* Set property NAME to VALUE on REV in FS, allocation from POOL.  */
svn_error_t *svn_fs__set_rev_prop (svn_fs_t *fs,
                                   svn_revnum_t rev,
                                   const char *name,
                                   const svn_string_t *value,
                                   apr_pool_t *pool);



/*** Transactions ***/

/* Convert the unfinished transaction in FS named TXN_NAME to a
   committed transaction that refers to REVISION allocating from POOL.

   Returns SVN_ERR_FS_TRANSACTION_NOT_MUTABLE if TXN_NAME refers to a
   transaction that has already been committed.  */
svn_error_t *svn_fs__txn_make_committed (svn_fs_t *fs,
                                         const char *txn_name,
                                         svn_revnum_t revision,
                                         apr_pool_t *pool);


/* Set *REVISION to the revision which was created when FS transaction
   TXN_NAME was committed, or to SVN_INVALID_REVNUM if the transaction
   has not been committed.  Do all allocations in POOL.  */
svn_error_t *svn_fs__txn_get_revision (svn_revnum_t *revision,
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
svn_error_t *svn_fs__get_txn_ids (const svn_fs_id_t **root_id_p,
                                  const svn_fs_id_t **base_root_id_p,
                                  svn_fs_t *fs,
                                  const char *txn_name,
                                  apr_pool_t *pool);


/* Set the root directory of the Subversion transaction TXN_NAME in FS
   to ROOT_ID.  Do any necessary temporary allocation in POOL.

   Returns SVN_ERR_FS_TRANSACTION_NOT_MUTABLE if TXN_NAME refers to a
   transaction that has already been committed.  */
svn_error_t *svn_fs__set_txn_root (svn_fs_t *fs,
                                   const char *txn_name,
                                   const svn_fs_id_t *root_id,
                                   apr_pool_t *pool);


/* Add COPY_ID to the list of copies made under the Subversion
   transaction TXN_NAME in FS allocating from POOL.

   Returns SVN_ERR_FS_TRANSACTION_NOT_MUTABLE if TXN_NAME refers to a
   transaction that has already been committed.  */
svn_error_t *svn_fs__add_txn_copy (svn_fs_t *fs,
                                   const char *txn_name,
                                   const char *copy_id,
                                   apr_pool_t *pool);


/* Set the base root directory of TXN_NAME in FS to NEW_ID.  Do any
   necessary temporary allocation in POOL.

   Returns SVN_ERR_FS_TRANSACTION_NOT_MUTABLE if TXN_NAME refers to a
   transaction that has already been committed.  */
svn_error_t *svn_fs__set_txn_base (svn_fs_t *fs,
                                   const char *txn_name,
                                   const svn_fs_id_t *new_id,
                                   apr_pool_t *pool);


/* Set a property NAME to VALUE on transaction TXN_NAME in FS.  Use
   POOL for any necessary allocations.

   Returns SVN_ERR_FS_TRANSACTION_NOT_MUTABLE if TXN_NAME refers to a
   transaction that has already been committed.  */
svn_error_t *svn_fs__set_txn_prop (svn_fs_t *fs,
                                   const char *txn_name,
                                   const char *name,
                                   const svn_string_t *value,
                                   apr_pool_t *pool);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_REVS_TXNS_H */
