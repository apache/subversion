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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* These functions implement some of the calls in the FS loader
   library's fs vtables. */

svn_error_t *svn_fs_fs__lock (svn_lock_t **lock,
                              svn_fs_t *fs,
                              const char *path,
                              const char *comment,
                              svn_boolean_t force,
                              long int timeout,
                              svn_revnum_t current_rev,
                              apr_pool_t *pool);

svn_error_t *svn_fs_fs__attach_lock (svn_lock_t *lock,
                                     svn_fs_t *fs,
                                     svn_boolean_t force,
                                     svn_revnum_t current_rev,
                                     apr_pool_t *pool);

svn_error_t *svn_fs_fs__generate_token (const char **token,
                                        svn_fs_t *fs,
                                        apr_pool_t *pool);

svn_error_t *svn_fs_fs__unlock (svn_fs_t *fs,
                                const char *token,
                                svn_boolean_t force,
                                apr_pool_t *pool);

svn_error_t *svn_fs_fs__get_lock_from_path (svn_lock_t **lock,
                                            svn_fs_t *fs,
                                            const char *path,
                                            apr_pool_t *pool);

svn_error_t *svn_fs_fs__get_lock_from_token (svn_lock_t **lock,
                                             svn_fs_t *fs,
                                             const char *token,
                                             apr_pool_t *pool);
  
svn_error_t *svn_fs_fs__get_locks (apr_hash_t **locks,
                                   svn_fs_t *fs,
                                   const char *path,
                                   apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_LOCK_H */
