/* lock.h : internal interface to lock functions
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

#ifndef SVN_LIBSVN_FS_LOCK_H
#define SVN_LIBSVN_FS_LOCK_H

#include "trail.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* These functions implement part of the FS loader library's fs
   vtables.  See the public svn_fs.h for docstrings.*/

svn_error_t *svn_fs_base__lock (svn_lock_t **lock,
                                svn_fs_t *fs,
                                const char *path,
                                svn_boolean_t force,
                                long int timeout,
                                const char *current_token,
                                apr_pool_t *pool);

svn_error_t *svn_fs_base__unlock (svn_fs_t *fs,
                                  const char *token,
                                  svn_boolean_t force,
                                  apr_pool_t *pool);

svn_error_t *svn_fs_base__get_lock_from_path (svn_lock_t **lock,
                                              svn_fs_t *fs,
                                              const char *path,
                                              apr_pool_t *pool);

svn_error_t *svn_fs_base__get_lock_from_token (svn_lock_t **lock,
                                               svn_fs_t *fs,
                                               const char *token,
                                               apr_pool_t *pool);

svn_error_t *svn_fs_base__get_locks (apr_hash_t **locks,
                                     svn_fs_t *fs,
                                     const char *path,
                                     apr_pool_t *pool);



/* These next functions are 'helpers' for internal libsvn_fs_base use. */


/* Implements main logic of 'svn_fs_get_lock_from_path' (or in this
   case, svn_fs_base__get_lock_from_path() above.)  See svn_fs.h. */
svn_error_t *svn_fs_base__get_lock_from_path_helper (svn_lock_t **lock_p,
                                                     const char *path,
                                                     trail_t *trail);


/* Implements main logic of 'svn_fs_get_locks' (or in this case,
   svn_fs_base__get_lock_from_path() above.)  See svn_fs.h */
svn_error_t *svn_fs_base__get_locks_helper (apr_hash_t **locks_p,
                                            const char *path,
                                            trail_t *trail);


/* Examine PATH (of kind KIND) for locks.  
   If no locks are present, then set *ALLOW to true.

   If PATH is locked (or contains locks "below" it, when RECURSE is
   set), then set *ALLOW to true iff:

      1. for every lock discovered, an appropriate lock token has been
         passed into TRAIL->fs, and

      2. for every lock discovered, the current username in the access
         context of TRAIL->fs matches the "owner" of the lock.

   Otherwise, set *ALLOW to false. */
svn_error_t *svn_fs_base__allow_locked_operation (svn_boolean_t *allow,
                                                  const char *path,
                                                  svn_node_kind_t kind,
                                                  svn_boolean_t recurse,
                                                  trail_t *trail);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_LOCK_H */
