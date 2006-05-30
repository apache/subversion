/* bdb_compat.c --- Compatibility wrapper for different BDB versions.
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

#include "bdb_compat.h"

int
svn_fs_bdb__check_version(void)
{
  int major, minor;

  db_version(&major, &minor, NULL);
  if (major != DB_VERSION_MAJOR || minor != DB_VERSION_MINOR)
    return DB_OLD_VERSION;
  return 0;
}
