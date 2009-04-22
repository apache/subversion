/*
 * err.c : implementation of fs-private error functions
 *
 * ====================================================================
 * Copyright (c) 2000-2006, 2009 CollabNet.  All rights reserved.
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

#include <apr_strings.h>

#include "svn_fs.h"
#include "../fs.h"
#include "../err.h"
#include "../../libsvn_fs/fs-loader.h"
#include "bdb-err.h"

#define SVN_WANT_BDB
#include "svn_private_config.h"


/* Return a distinguished error for any db error code we want to detect
 * programatically; otherwise return a generic error.
 */
static int
bdb_err_to_apr_err(int db_err)
{
  if (db_err == DB_LOCK_DEADLOCK)
    return SVN_ERR_FS_BERKELEY_DB_DEADLOCK;
  else
    return SVN_ERR_FS_BERKELEY_DB;
}


svn_error_t *
svn_fs_bdb__dberr(bdb_env_baton_t *bdb_baton, int db_err)
{
  svn_error_t *child_errors;

  child_errors = bdb_baton->error_info->pending_errors;
  bdb_baton->error_info->pending_errors = NULL;

  return svn_error_create(bdb_err_to_apr_err(db_err), child_errors,
                          db_strerror(db_err));
}


svn_error_t *
svn_fs_bdb__dberrf(bdb_env_baton_t *bdb_baton,
                   int db_err, const char *fmt, ...)
{
  va_list ap;
  char *msg;
  svn_error_t *err;
  svn_error_t *child_errors;

  child_errors = bdb_baton->error_info->pending_errors;
  bdb_baton->error_info->pending_errors = NULL;

  err = svn_error_create(bdb_err_to_apr_err(db_err), child_errors, NULL);

  va_start(ap, fmt);
  msg = apr_pvsprintf(err->pool, fmt, ap);
  va_end(ap);
  err->message = apr_psprintf(err->pool, "%s%s", msg, db_strerror(db_err));
  return err;
}


svn_error_t *
svn_fs_bdb__wrap_db(svn_fs_t *fs, const char *operation, int db_err)
{
  base_fs_data_t *bfd = fs->fsap_data;

  if (! db_err)
    {
      svn_error_clear(bfd->bdb->error_info->pending_errors);
      bfd->bdb->error_info->pending_errors = NULL;
      return SVN_NO_ERROR;
    }

  bfd = fs->fsap_data;
  return svn_fs_bdb__dberrf
    (bfd->bdb, db_err,
     _("Berkeley DB error for filesystem '%s' while %s:\n"),
     fs->path ? fs->path : "(none)", operation);
}
