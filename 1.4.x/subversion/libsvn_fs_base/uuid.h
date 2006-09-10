/* uuid.h : internal interface to uuid functions
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

#ifndef SVN_LIBSVN_FS_UUID_H
#define SVN_LIBSVN_FS_UUID_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* These functions implement some of the calls in the FS loader
   library's fs vtable. */

svn_error_t *svn_fs_base__get_uuid(svn_fs_t *fs, const char **uuid,
                                   apr_pool_t *pool);

svn_error_t *svn_fs_base__set_uuid(svn_fs_t *fs, const char *uuid,
                                   apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_UUID_H */
