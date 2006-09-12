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

svn_error_t *svn_fs_base__lock(svn_lock_t **lock,
                               svn_fs_t *fs,
                               const char *path,
                               const char *token,
                               const char *comment,
                               svn_boolean_t is_dav_comment,
                               apr_time_t expiration_date,
                               svn_revnum_t current_rev,
                               svn_boolean_t steal_lock,
                               apr_pool_t *pool);

svn_error_t *svn_fs_base__generate_lock_token(const char **token,
                                              svn_fs_t *fs,
                                              apr_pool_t *pool);

svn_error_t *svn_fs_base__unlock(svn_fs_t *fs,
                                 const char *path,
                                 const char *token,
                                 svn_boolean_t break_lock,
                                 apr_pool_t *pool);

svn_error_t *svn_fs_base__get_lock(svn_lock_t **lock,
                                   svn_fs_t *fs,
                                   const char *path,
                                   apr_pool_t *pool);

svn_error_t *
svn_fs_base__get_locks(svn_fs_t *fs,
                       const char *path,
                       svn_fs_get_locks_callback_t get_locks_func,
                       void *get_locks_baton,
                       apr_pool_t *pool);



/* These next functions are 'helpers' for internal fs use:
   if a fs function's txn_body needs to enforce existing locks, it
   should use these routines:
*/


/* Implements main logic of 'svn_fs_get_lock' (or in this
   case, svn_fs_base__get_lock() above.)  See svn_fs.h. */
svn_error_t *
svn_fs_base__get_lock_helper(svn_lock_t **lock_p,
                             const char *path,
                             trail_t *trail,
                             apr_pool_t *pool);
  

/* Examine PATH for existing locks, and check whether they can be
   used.  Do all work in the context of TRAIL, using POOL for
   temporary allocations.

   If no locks are present, return SVN_NO_ERROR.

   If PATH is locked (or contains locks "below" it, when RECURSE is
   set), then verify that:

      1. a username has been supplied to TRAIL->fs's access-context,
         else return SVN_ERR_FS_NO_USER.

      2. for every lock discovered, the current username in the access
         context of TRAIL->fs matches the "owner" of the lock, else
         return SVN_ERR_FS_LOCK_OWNER_MISMATCH.

      3. for every lock discovered, a matching lock token has been
         passed into TRAIL->fs's access-context, else return
         SVN_ERR_FS_BAD_LOCK_TOKEN.

   If all three conditions are met, return SVN_NO_ERROR.
*/
svn_error_t *svn_fs_base__allow_locked_operation(const char *path,
                                                 svn_boolean_t recurse,
                                                 trail_t *trail,
                                                 apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_LOCK_H */
