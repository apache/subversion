/*
 * err.c : implementation of fs-private error functions
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



#include <stdlib.h>
#include <stdarg.h>
#include <db.h>
#include <apr_strings.h>

#include "svn_fs.h"
#include "../fs.h"
#include "../err.h"
#include "bdb-err.h"


/* Return a distinguished error for any db error code we want to detect
 * programatically; otherwise return a generic error.
 */
static int
bdb_err_to_apr_err (int db_err)
{
  if (db_err == DB_LOCK_DEADLOCK)
    return SVN_ERR_FS_BERKELEY_DB_DEADLOCK;
  else
    return SVN_ERR_FS_BERKELEY_DB;
}
 

svn_error_t *
svn_fs__bdb_dberr (int db_err)
{
  return svn_error_create (bdb_err_to_apr_err(db_err),
                           0,
                           db_strerror (db_err));
}


svn_error_t *
svn_fs__bdb_dberrf (int db_err, const char *fmt, ...)
{
  va_list ap;
  char *msg;
  svn_error_t *err;

  err = svn_error_create (bdb_err_to_apr_err(db_err), 0, "");

  va_start (ap, fmt);
  msg = apr_pvsprintf (err->pool, fmt, ap);
  va_end (ap);
  err->message = apr_psprintf (err->pool, "%s%s", msg, db_strerror (db_err));
  return err;
}


svn_error_t *
svn_fs__bdb_wrap_db (svn_fs_t *fs, const char *operation, int db_err)
{
  if (! db_err)
    return SVN_NO_ERROR;
  else
    return svn_fs__bdb_dberrf (db_err,
                           "Berkeley DB error while %s for "
                           "filesystem %s:\n", operation,
                           fs->path ? fs->path : "(none)");
}
