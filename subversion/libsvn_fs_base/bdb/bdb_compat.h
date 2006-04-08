/* svn_bdb_compat.h --- Compatibility wrapper for different BDB versions.
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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

#define APU_WANT_DB
#include <apu_want.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Symbols and constants */

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

/* In BDB 4.3, "buffer too small" errors come back with
   DB_BUFFER_SMALL (instead of ENOMEM, which is now fatal). */
#ifdef DB_BUFFER_SMALL
#define SVN_BDB_DB_BUFFER_SMALL DB_BUFFER_SMALL
#else
#define SVN_BDB_DB_BUFFER_SMALL ENOMEM
#endif

/* BDB 4.4 introdiced the DB_REGISTER flag for DBEnv::open that allows
   for automatic recovery of the databases after a program crash. */
#ifdef DB_REGISTER
#define SVN_BDB_AUTO_RECOVER (DB_REGISTER | DB_RECOVER)
#else
#define SVN_BDB_AUTO_RECOVER (0)
#endif


/* Explicit BDB version check. */
#define SVN_BDB_VERSION_AT_LEAST(major,minor) \
    (DB_VERSION_MAJOR > (major) \
     || (DB_VERSION_MAJOR == (major) && DB_VERSION_MINOR >= (minor)))


/* Parameter lists */

/* In BDB 4.1, DB->open takes a transaction parameter. We'll ignore it
   when building with 4.0. */
#if SVN_BDB_VERSION_AT_LEAST(4,1)
#define SVN_BDB_OPEN_PARAMS(env,txn) (env), (txn)
#else
#define SVN_BDB_OPEN_PARAMS(env,txn) (env)
#endif

/* In BDB 4.3, the error gatherer function grew a new DBENV parameter,
   and the MSG parameter's type changed. */
#if SVN_BDB_VERSION_AT_LEAST(4,3)
/* Prevents most compilers from whining about unused parameters. */
#define SVN_BDB_ERROR_GATHERER_IGNORE(varname) ((void)(varname))
#else
#define bdb_error_gatherer(param1, param2, param3) \
  bdb_error_gatherer(param2, char *msg)
#define SVN_BDB_ERROR_GATHERER_IGNORE(varname) ((void)0)
#endif

/* In BDB 4.3 and later, the file names in DB_ENV->open and DB->open
   are assumed to be encoded in UTF-8 on Windows. */
#if defined(WIN32) && SVN_BDB_VERSION_AT_LEAST(4,3)
#define SVN_BDB_PATH_UTF8 (1)
#else
#define SVN_BDB_PATH_UTF8 (0)
#endif


/* Before calling db_create, we must check that the version of the BDB
   libraries we're linking with is the same as the one we compiled
   against, because the DB->open call is not binary compatible between
   BDB 4.0 and 4.1. This function returns DB_OLD_VERSION if the
   compile-time and run-time versions of BDB don't match. */
int svn_fs_bdb__check_version(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_BDB_COMPAT_H */
