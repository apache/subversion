/* revs-txns.h : internal interface to revision and transactions operations
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

#include <db.h>
#include "svn_fs.h"

#include "fs.h"
#include "trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Revisions ***/

/* Set *ROOT_ID_P to the ID of the root directory of revision REV in FS,
   as part of TRAIL.  Allocate the ID in TRAIL->pool.  */
svn_error_t *svn_fs__rev_get_root (const svn_fs_id_t **root_id_p,
                                   svn_fs_t *fs,
                                   svn_revnum_t rev,
                                   trail_t *trail);


/* Set *TXN_ID_P to the ID of the transaction that was committed to
   create REV in FS, as part of TRAIL.  Allocate the ID in
   TRAIL->pool.  */
svn_error_t *svn_fs__rev_get_txn_id (const char **txn_id_p,
                                     svn_fs_t *fs,
                                     svn_revnum_t rev,
                                     trail_t *trail);


/* Set property NAME to VALUE on REV in FS, as part of TRAIL.  */
svn_error_t *svn_fs__set_rev_prop (svn_fs_t *fs,
                                   svn_revnum_t rev,
                                   const char *name,
                                   const svn_string_t *value,
                                   trail_t *trail);



/*** Transactions ***/

/* Convert the unfinished transaction in FS named TXN_NAME to a
   committed on that refers to REVISION as part of TRAIL.  

   Returns SVN_ERR_FS_TRANSACTION_NOT_MUTABLE if TXN_NAME refers to a
   transaction that has already been committed.  */
svn_error_t *svn_fs__commit_txn (svn_fs_t *fs,
                                 const char *txn_name,
                                 svn_revnum_t revision,
                                 trail_t *trail);


/* Retrieve information about the Subversion transaction SVN_TXN from
   the `transactions' table of FS, as part of TRAIL.
   Set *ROOT_ID_P to the ID of the transaction's root directory.
   Set *BASE_ROOT_ID_P to the ID of the root directory of the
   transaction's base revision.

   If there is no such transaction, SVN_ERR_FS_NO_SUCH_TRANSACTION is
   the error returned.

   Returns SVN_ERR_FS_TRANSACTION_NOT_MUTABLE if TXN_NAME refers to a
   transaction that has already been committed.

   Allocate *ROOT_ID_P and *BASE_ROOT_ID_P in TRAIL->pool.  */
svn_error_t *svn_fs__get_txn_ids (const svn_fs_id_t **root_id_p,
                                  const svn_fs_id_t **base_root_id_p,
                                  svn_fs_t *fs,
                                  const char *txn_name,
                                  trail_t *trail);


/* Set the root directory of the Subversion transaction TXN_NAME in FS
   to ROOT_ID, as part of TRAIL.  Do any necessary temporary
   allocation in TRAIL->pool. 

   Returns SVN_ERR_FS_TRANSACTION_NOT_MUTABLE if TXN_NAME refers to a
   transaction that has already been committed.  */
svn_error_t *svn_fs__set_txn_root (svn_fs_t *fs,
                                   const char *txn_name,
                                   const svn_fs_id_t *root_id,
                                   trail_t *trail);


/* Add COPY_ID to the list of copies made under the Subversion
   transaction TXN_NAME in FS as part of TRAIL.

   Returns SVN_ERR_FS_TRANSACTION_NOT_MUTABLE if TXN_NAME refers to a
   transaction that has already been committed.  */
svn_error_t *svn_fs__add_txn_copy (svn_fs_t *fs,
                                   const char *txn_name,
                                   const char *copy_id,
                                   trail_t *trail);


/* Set the base root directory of TXN_NAME in FS to NEW_ID, as part of
   TRAIL.  Do any necessary temporary allocation in TRAIL->pool. 

   Returns SVN_ERR_FS_TRANSACTION_NOT_MUTABLE if TXN_NAME refers to a
   transaction that has already been committed.  */
svn_error_t *
svn_fs__set_txn_base (svn_fs_t *fs,
                      const char *txn_name,
                      const svn_fs_id_t *new_id,
                      trail_t *trail);


/* Set a property NAME to VALUE on transaction TXN_NAME in FS as part
   of TRAIL.  Use TRAIL->pool for any necessary allocations.  

   Returns SVN_ERR_FS_TRANSACTION_NOT_MUTABLE if TXN_NAME refers to a
   transaction that has already been committed.  */
svn_error_t *svn_fs__set_txn_prop (svn_fs_t *fs,
                                   const char *txn_name,
                                   const char *name,
                                   const svn_string_t *value,
                                   trail_t *trail);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_REVS_TXNS_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
