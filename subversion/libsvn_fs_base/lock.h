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



/* These functions implement some of the calls in the FS loader
   library's fs vtables. */

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



/* These functions and types are helper functions for the FS to call
   internally. */

struct svn_fs_base__unlock_args_t
{
  const char *token;
  svn_boolean_t force;
};

svn_error_t *svn_fs_base__unlock_helper (void *baton, trail_t *trail);


struct svn_fs_base__get_lock_from_path_args_t
{
  svn_lock_t **lock_p;
  const char *path;
};

svn_error_t *svn_fs_base__get_lock_from_path_helper (void *baton,
                                                     trail_t *trail);


struct svn_fs_base__get_lock_from_token_args_t
{
  svn_lock_t **lock_p;
  const char *lock_token;
};

svn_error_t *svn_fs_base__get_lock_from_token_helper (void *baton,
                                                      trail_t *trail);


struct svn_fs_base__get_locks_args_t
{
  apr_hash_t **locks_p;
  const char *path;
};

svn_error_t *svn_fs_base__get_locks_helper (void *baton, trail_t *trail);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_LOCK_H */
