/* fsguid.h : internal interface to operations on FS-global unique identifiers
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

#ifndef SVN_LIBSVN_FS_FSGUID_H
#define SVN_LIBSVN_FS_FSGUID_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Reserve for use a unique identifier global in scope within FS, and return
   that identifier in *FSGUID.  Use POOL for allocations.  If TRAIL
   is non-NULL, use it (otherwise a one-off trail will be used, so be
   careful not to pass a NULL TRAIL if the code stack is really
   inside a Berkeley DB transaction).  */
svn_error_t *
svn_fs_base__reserve_fsguid(svn_fs_t *fs, 
                            const char **fsguid,
                            trail_t *trail,
                            apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_FSGUID_H */
