/* env.h : managing the BDB environment
 *
 * ====================================================================
 * Copyright (c) 2000-2005 CollabNet.  All rights reserved.
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

#ifndef SVN_LIBSVN_FS_BDB_ENV_H
#define SVN_LIBSVN_FS_BDB_ENV_H

#define APU_WANT_DB
#include <apu_want.h>

#include <apr_pools.h>

#include "bdb_compat.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Convert PATH_UTF8 to PATH_BDB.
 *
 * The converted path will be in the encoding expected by BDB;
 * specifically, on Wincows as of BDB 4.3, it must also be in UTF-8.
 * Use POOL for temporary allocations.
 */
svn_error_t * svn_fs_bdb__path_from_utf8 (const char **path_bdb,
                                          const char *path_utf8,
                                          apr_pool_t *pool);


/* Flag combination for opening a shared BDB environment. */
#define SVN_BDB_STANDARD_ENV_FLAGS (DB_CREATE       \
                                    | DB_INIT_LOCK  \
                                    | DB_INIT_LOG   \
                                    | DB_INIT_MPOOL \
                                    | DB_INIT_TXN   \
                                    | SVN_BDB_AUTO_RECOVER)

/* Flag combination for opening a private BDB environment. */
#define SVN_BDB_PRIVATE_ENV_FLAGS (DB_CREATE       \
                                   | DB_INIT_LOG   \
                                   | DB_INIT_MPOOL \
                                   | DB_INIT_TXN   \
                                   | DB_PRIVATE)


/* Open the Berkeley DB environment ENV.
 *
 * Open ENV in PATH, using FLAGS and MODE.  If applicable, set the
 * BDB_AUTO_COMMIT flag for this environment.
 * Return a Berkeley DB error code.
 *
 * Note: This function may return a pointer to an already-opened
 * environment.
 */
int svn_fs_bdb__open_env (DB_ENV **env, const char *path,
                          u_int32_t flags, int mode);

/* Close the Berkeley DB environemnt ENV.
 *
 * Note: This function might not actually close the environment if it
 * has been "svn_fs_bdb__open_env'd" more than once.
 */
int svn_fs_bdb__close_env (DB_ENV *env);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_BDB_ENV_H */
