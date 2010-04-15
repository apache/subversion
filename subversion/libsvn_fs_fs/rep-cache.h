/* id.h : interface to node ID functions, private to libsvn_fs_fs
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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

#ifndef SVN_LIBSVN_FS_FS_REP_CACHE_H
#define SVN_LIBSVN_FS_FS_REP_CACHE_H

#include "svn_error.h"

#include "fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define REP_CACHE_DB_NAME        "rep-cache.db"

/* Open and create, if needed, the rep cache database associated with FS.
   Use POOL for temporary allocations. */
svn_error_t *
svn_fs_fs__open_rep_cache(svn_fs_t *fs,
                          apr_pool_t *pool);

/* Return the representation REP in FS which has fulltext CHECKSUM.
   REP is allocated in POOL.  If the rep cache database has not been
   opened, just set *REP to NULL. */
svn_error_t *
svn_fs_fs__get_rep_reference(representation_t **rep,
                             svn_fs_t *fs,
                             svn_checksum_t *checksum,
                             apr_pool_t *pool);

/* Set the representation REP in FS, using REP->CHECKSUM.
   Use POOL for temporary allocations.

   If the rep cache database has not been opened, this may be a no op.

   If REJECT_DUP is TRUE, return an error if there is an existing
   match for REP->CHECKSUM. */
svn_error_t *
svn_fs_fs__set_rep_reference(svn_fs_t *fs,
                             representation_t *rep,
                             svn_boolean_t reject_dup,
                             apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_FS_REP_CACHE_H */
