/* uuids-table.h : internal interface to `uuids' table
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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

#ifndef SVN_LIBSVN_FS_UUIDS_TABLE_H
#define SVN_LIBSVN_FS_UUIDS_TABLE_H

#include <db.h>
#include "svn_io.h"
#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Open a `uuids' table in @a env.
 *
 * Open a `uuids' table in @a env.  If @a create is non-zero, create
 * one if it doesn't exist.  Set @a *uuids_p to the new table.  
 * Return a Berkeley DB error code.
 */
int svn_fs__bdb_open_uuids_table (DB **uuids_p,
                                  DB_ENV *env,
                                  svn_boolean_t create);

/* Get the UUID at index @a idx in the uuids table within @a fs,
 * storing the result in @a *uuid.
 */

svn_error_t *svn_fs__bdb_get_uuid (svn_fs_t *fs,
                                   int idx,
                                   const char **uuid,
                                   trail_t *trail);

/* Set the UUID at index @a idx in the uuids table within @a fs
 * to @a uuid.
 */

svn_error_t *svn_fs__bdb_set_uuid (svn_fs_t *fs,
                                   int idx,
                                   const char *uuid,
                                   trail_t *trail);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_UUIDS_TABLE_H */
