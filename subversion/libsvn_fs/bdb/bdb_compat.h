/* svn_bdb_compat.h --- Compatibility wrapper for different BDB versions.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

#ifndef SVN_LIBSVN_FS_BDB_COMPAT_H
#define SVN_LIBSVN_FS_BDB_COMPAT_H

#include <db.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* BDB 4.1 introduced the DB_AUTO_COMMIT flag. Older versions can just
   use 0 instead. */
#ifdef DB_AUTO_COMMIT
#define SVN_BDB_AUTO_COMMIT (DB_AUTO_COMMIT)
#else
#define SVN_BDB_AUTO_COMMIT (0)
#endif

/* DB_INCOMPLETE is obsolete in BDB 4.1. */
#ifdef DB_INCOMPLETE
#define SVN_BDB_HAS_DB_INCOMPLETE 1
#else
#define SVN_BDB_HAS_DB_INCOMPLETE 0
#endif

/* In BDB 4.1, DB->open takes a transaction parameter. We'll ignore it
   when building with 4.0. */
#if (DB_VERSION_MAJOR > 4) \
    || (DB_VERSION_MAJOR == 4) && (DB_VERSION_MINOR >= 1)
#define SVN_BDB_OPEN_PARAMS(env,txn) (env), (txn)
#else
#define SVN_BDB_OPEN_PARAMS(env,txn) (env)
#endif


/* Before calling db_create, we must check that the version of the BDB
   libraries we're linking with is the same as the one we compiled
   against, because the DB->open call is not binary compatible between
   BDB 4.0 and 4.1. This function returns DB_OLD_VERSION if the
   compile-time and run-time versions of BDB don't match. */
int svn_fs__bdb_check_version (void);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_BDB_COMPAT_H */
