/* txn-table.h : internal interface to ops on `transactions' table
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

#ifndef SVN_LIBSVN_FS_TXN_TABLE_H
#define SVN_LIBSVN_FS_TXN_TABLE_H

#include "svn_fs.h"
#include "trail.h"


/* Open a `transactions' table in ENV.  If CREATE is non-zero, create
   one if it doesn't exist.  Set *TRANSACTIONS_P to the new table.
   Return a Berkeley DB error code.  */
int svn_fs__open_transactions_table (DB **transactions_p,
				     DB_ENV *env,
				     int create);


/* Create a new transaction in FS as part of TRAIL, with an initial
   root and base root ID of ROOT_ID.  Set *TXN_ID_P to the ID of the new
   transaction, allocated in TRAIL->pool.  */
svn_error_t *svn_fs__create_txn (char **txn_id_p,
				 svn_fs_t *fs,
				 const svn_fs_id_t *root_id,
				 trail_t *trail);


/* Retrieve information about the Subversion transaction SVN_TXN from
   the `transactions' table of FS, as part of TRAIL.
   Set *ROOT_ID_P to the ID of the transaction's root directory.
   Set *BASE_ROOT_ID_P to the ID of the root directory of the
   transaction's base revision.
   Allocate *ROOT_ID_P and *BASE_ROOT_ID_P in TRAIL->pool.  */
svn_error_t *svn_fs__get_txn (svn_fs_id_t **root_id_p,
			      svn_fs_id_t **base_root_id_p,
			      svn_fs_t *fs,
			      const char *svn_txn,
			      trail_t *trail);


/* Set the root directory of the Subversion transaction SVN_TXN in FS
   to ROOT_ID, as part of TRAIL.  Do any necessary temporary
   allocation in TRAIL->pool.  */
svn_error_t *svn_fs__set_txn_root (svn_fs_t *fs,
				   const char *svn_txn,
				   const svn_fs_id_t *root_id,
				   trail_t *trail);


/* Make a list of currently active transactions FS in NAMES_P, as part
   of TRAIL. Allocate the array in POOL; do any necessary temporary
   allocation in TRAIL->pool.. */
svn_error_t *svn_fs__get_txn_list (char ***names_p,
                                   svn_fs_t *fs,
                                   apr_pool_t *pool,
                                   trail_t *trail);

#endif /* SVN_LIBSVN_FS_TXN_TABLE_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
