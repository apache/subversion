/* txn.h : interface to Subversion transactions, private to libsvn_fs
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

#ifndef SVN_LIBSVN_FS_TXN_H
#define SVN_LIBSVN_FS_TXN_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Return a pointer to the ID of TXN.  The return value is live for as
   long as TXN is.  */
const char *svn_fs__txn_id (svn_fs_txn_t *txn);


/* Return a pointer to the FS of TXN.  The return value is live for as
   long as TXN is.  */
svn_fs_t *svn_fs__txn_fs (svn_fs_txn_t *txn);


/* Return a pointer to the POOL of TXN.  Freeing this pool frees TXN
   (but see svn_fs_close_txn).  */ 
apr_pool_t *svn_fs__txn_pool (svn_fs_txn_t *txn);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_TXN_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
