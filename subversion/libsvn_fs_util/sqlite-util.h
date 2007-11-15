/* sqlite-util.h
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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


#ifndef SVN_LIBSVN_FS_UTIL_SQLITE_H
#define SVN_LIBSVN_FS_UTIL_SQLITE_H

#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* SQLITE->SVN quick error wrap, much like SVN_ERR. */
#define SVN_FS__SQLITE_ERR(x, db) do                            \
{                                                               \
  if ((x) != SQLITE_OK)                                         \
    return svn_error_create(SVN_ERR_FS_SQLITE_ERROR, NULL,      \
                            sqlite3_errmsg((db)));              \
} while (0)

/* Execute SQL on the sqlite database DB, and raise an SVN error if the
   result is not okay.  */
svn_error_t *
svn_fs__sqlite_exec(sqlite3 *db, const char *sql);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_UTIL_SQLITE_H */
