/* txn.h : interface to Subversion transactions, private to libsvn_fs
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

#ifndef SVN_LIBSVN_FS_TXN_H
#define SVN_LIBSVN_FS_TXN_H

/* Create a new `transactions' table for the new filesystem FS.
   FS->env must already be open; this sets FS->nodes.  */
svn_error_t *svn_fs__create_transactions (svn_fs_t *fs);


/* Open the existing `transactions' table for the filesystem FS.
   FS->env must already be open; this sets FS->nodes.  */
svn_error_t *svn_fs__open_transactions (svn_fs_t *fs);


/* Return a pointer to the ID of TXN.  The return value is live for as
   long as TXN is.  */
char *svn_fs__txn_id (svn_fs_txn_t *txn);


#endif /* SVN_LIBSVN_FS_TXN_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
