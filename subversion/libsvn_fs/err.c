/*
 * err.c : implementation of fs-private error functions
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#include <stdlib.h>
#include <stdarg.h>
#include <db.h>
#include "apr_strings.h"
#include "svn_fs.h"
#include "fs.h"
#include "err.h"

svn_error_t *
svn_fs__dberr (apr_pool_t *pool, int db_err)
{
  return svn_error_create (SVN_ERR_BERKELEY_DB,
			   db_err,
			   0,
			   pool,
                           db_strerror (db_err));
}


svn_error_t *
svn_fs__dberrf (apr_pool_t *pool, int db_err, const char *fmt, ...)
{
  va_list ap;
  char *msg;

  va_start (ap, fmt);
  msg = apr_pvsprintf (pool, fmt, ap);
  va_end (ap);

  return svn_error_createf (SVN_ERR_BERKELEY_DB, db_err, 0, pool, 
			    "%s%s", msg, db_strerror (db_err));
}


svn_error_t *
svn_fs__wrap_db (svn_fs_t *fs, const char *operation, int db_err)
{
  if (! db_err)
    return 0;
  else
    return svn_fs__dberrf (fs->pool, db_err,
			   "Berkeley DB error while %s for "
			   "filesystem %s:\n", operation,
			   fs->env_path ? fs->env_path : "(none)");
}


svn_error_t *
svn_fs__check_fs (svn_fs_t *fs)
{
  if (fs->env)
    return 0;
  else
    return svn_error_create (SVN_ERR_FS_NOT_OPEN, 0, 0, fs->pool,
			     "filesystem object has not been opened yet");
}



/* Building common error objects.  */


static svn_error_t *
corrupt_id (const char *fmt, const svn_fs_id_t *id, svn_fs_t *fs)
{
  svn_string_t *unparsed_id = svn_fs_unparse_id (id, fs->pool);

  return svn_error_createf (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
			    fmt, unparsed_id->data, fs->env_path);
}


svn_error_t *
svn_fs__err_corrupt_representation (svn_fs_t *fs, const svn_fs_id_t *id)
{
  return
    corrupt_id ("corrupt representation for node `%s' in filesystem `%s'",
		id, fs);
}


svn_error_t *
svn_fs__err_corrupt_node_revision (svn_fs_t *fs, const svn_fs_id_t *id)
{
  return
    corrupt_id ("corrupt node revision for node `%s' in filesystem `%s'",
		id, fs);
}


svn_error_t *
svn_fs__err_corrupt_clone (svn_fs_t *fs,
			   const char *svn_txn,
			   const char *base_path)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
     "corrupt clone record for `%s' in transaction `%s' in filesystem `%s'",
     base_path, svn_txn, fs->env_path);
}


svn_error_t *
svn_fs__err_corrupt_id (svn_fs_t *fs, const svn_fs_id_t *id)
{
  return
    corrupt_id ("Corrupt node revision id `%s' appears in filesystem `%s'",
		id, fs);
}


svn_error_t *
svn_fs__err_dangling_id (svn_fs_t *fs, const svn_fs_id_t *id)
{
  return
    corrupt_id ("reference to non-existent node `%s' in filesystem `%s'",
		id, fs);
}


svn_error_t *
svn_fs__err_corrupt_nodes_key (svn_fs_t *fs)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
     "malformed ID as key in `nodes' table of filesystem `%s'", fs->env_path);
}


svn_error_t *
svn_fs__err_corrupt_next_txn_id (svn_fs_t *fs)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
     "corrupt value for `next-id' key in `transactions' table"
     " of filesystem `%s'", fs->env_path);
}


svn_error_t *
svn_fs__err_corrupt_txn (svn_fs_t *fs,
			 const char *txn)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
     "corrupt entry in `transactions' table for `%s'"
     " in filesystem `%s'", txn, fs->env_path);
}


svn_error_t *
svn_fs__err_not_mutable (svn_fs_t *fs, const svn_fs_id_t *id)
{
  svn_string_t *unparsed_id = svn_fs_unparse_id (id, fs->pool);

  return
    svn_error_createf
    (SVN_ERR_FS_NOT_MUTABLE, 0, 0, fs->pool,
     "attempt to modify committed node revision `%s' in filesystem `%s'",
     unparsed_id->data, fs->env_path);
}
