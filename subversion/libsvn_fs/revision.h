/* revision.h : interface to revision functions, private to libsvn_fs
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

/* ==================================================================== */


#ifndef SVN_LIBSVN_FS_REVISION_H
#define SVN_LIBSVN_FS_REVISION_H

#include "apr_pools.h"
#include "fs.h"

/* Create a new `revisions' table for the new filesystem FS.  FS->env
   must already be open; this sets FS->revisions.  */
svn_error_t *svn_fs__create_revisions (svn_fs_t *fs);


/* Open the existing `revisions' table for the filesystem FS.  FS->env
   must already be open; this sets FS->revisions.  */
svn_error_t *svn_fs__open_revisions (svn_fs_t *fs);


/* Set *ID to ID of the root of revision V of the filesystem FS.
   Allocate the ID in POOL.  */
svn_error_t *svn_fs__revision_root (svn_fs_id_t **id,
                                    svn_fs_t *fs,
                                    svn_revnum_t v,
                                    apr_pool_t *pool);


#endif /* SVN_LIBSVN_FS_REVISION_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
