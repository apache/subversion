/* hooks.h : interface to driving fs hooks and sentinels
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_FS_HOOKS_H
#define SVN_LIBSVN_FS_HOOKS_H

#include "apr_pools.h"
#include "svn_fs.h"



/* Run the pre-commit hooks for FS, expanding "$txn" to TXN_NAME.  Use
   POOL for temporary allocation.  */
svn_error_t *svn_fs__run_pre_commit_hooks (svn_fs_t *fs,
                                           const char *txn_name,
                                           apr_pool_t *pool);


/* Run the pre-commit hooks for FS, expanding "$rev" to REV.  Use
   POOL for temporary allocation.  */
svn_error_t *svn_fs__run_post_commit_hooks (svn_fs_t *fs,
                                            svn_revnum_t rev,
                                            apr_pool_t *pool);

#endif /* SVN_LIBSVN_FS_HOOKS_H */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
