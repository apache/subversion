/* locks-table.h : internal interface to ops on `locks' table
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

#ifndef SVN_LIBSVN_FS_LOCKS_TABLE_H
#define SVN_LIBSVN_FS_LOCKS_TABLE_H

#include "svn_fs.h"
#include "svn_error.h"
#include "../trail.h"
#include "../fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Open a `locks' table in ENV.  If CREATE is non-zero, create
   one if it doesn't exist.  Set *LOCKS_P to the new table.
   Return a Berkeley DB error code.  */
int svn_fs_bdb__open_locks_table(DB **locks_p,
                                 DB_ENV *env,
                                 svn_boolean_t create);


/* Add a lock to the `locks' table in FS, as part of TRAIL. 

   Use LOCK_TOKEN as the key, presumably a string form of an apr_uuid_t.
   Convert LOCK into a skel and store it as the value.

   Warning:  if LOCK_TOKEN already exists as a key, then its value
   will be overwritten. */
svn_error_t *svn_fs_bdb__lock_add(svn_fs_t *fs,
                                  const char *lock_token,
                                  svn_lock_t *lock,
                                  trail_t *trail,
                                  apr_pool_t *pool);


/* Remove the lock whose key is LOCK_TOKEN from the `locks' table of
   FS, as part of TRAIL.  

   Return SVN_ERR_FS_BAD_LOCK_TOKEN if LOCK_TOKEN does not exist as a
   table key. */
svn_error_t *svn_fs_bdb__lock_delete(svn_fs_t *fs,
                                     const char *lock_token,
                                     trail_t *trail,
                                     apr_pool_t *pool);


/* Retrieve the lock *LOCK_P pointed to by LOCK_TOKEN from the `locks'
   table of FS, as part of TRAIL.  Perform all allocations in POOL.

   Return SVN_ERR_FS_BAD_LOCK_TOKEN if LOCK_TOKEN does not exist as a
   table key.

   Before returning LOCK_P, check its expiration date.  If expired,
   remove the row from the `locks' table and return SVN_ERR_FS_LOCK_EXPIRED.
 */
svn_error_t *svn_fs_bdb__lock_get(svn_lock_t **lock_p,
                                  svn_fs_t *fs,
                                  const char *lock_token,
                                  trail_t *trail,
                                  apr_pool_t *pool);


/* Retrieve locks representing all locks that exist at or below PATH
   in FS. Pass each lock to GET_LOCKS_FUNC callback along with
   GET_LOCKS_BATON.

   This function promises to auto-expire any locks encountered while
   building the hash.  That means that the caller can trust that each
   returned lock hasn't yet expired.
*/
svn_error_t *svn_fs_bdb__locks_get(svn_fs_t *fs,
                                   const char *path,
                                   svn_fs_get_locks_callback_t get_locks_func,
                                   void *get_locks_baton,
                                   trail_t *trail,
                                   apr_pool_t *pool);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_LOCKS_TABLE_H */
