/* lock.c :  functions for manipulating filesystem locks.
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


#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"

#include "lock.h"
#include "../libsvn_fs/fs-loader.h"



svn_error_t *
svn_fs_fs__lock (svn_lock_t **lock,
                 svn_fs_t *fs,
                 const char *path,
                 svn_boolean_t force,
                 long int timeout,
                 const char *current_token,
                 apr_pool_t *pool)
{
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0,
                           "Function not yet implemented.");
}


svn_error_t *
svn_fs_fs__unlock (svn_fs_t *fs,
                   const char *token,
                   svn_boolean_t force,
                   apr_pool_t *pool)
{
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0,
                           "Function not yet implemented.");
}



svn_error_t *
svn_fs_fs__get_lock_from_path (svn_lock_t **lock,
                               svn_fs_t *fs,
                               const char *path,
                               apr_pool_t *pool)
{
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0,
                           "Function not yet implemented.");
}



svn_error_t *
svn_fs_fs__get_lock_from_token (svn_lock_t **lock,
                                svn_fs_t *fs,
                                const char *token,
                                apr_pool_t *pool)
{
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0,
                           "Function not yet implemented.");
}



svn_error_t *
svn_fs_fs__get_locks (apr_hash_t **locks,
                      svn_fs_t *fs,
                      const char *path,
                      apr_pool_t *pool)
{
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0,
                           "Function not yet implemented.");
}

