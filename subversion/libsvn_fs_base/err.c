/*
 * err.c : implementation of fs-private error functions
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



#include <stdlib.h>
#include <stdarg.h>

#include "svn_private_config.h"
#include "svn_fs.h"
#include "err.h"
#include "id.h"

#include "../libsvn_fs/fs-loader.h"



/* Building common error objects.  */


svn_error_t *
svn_fs_base__err_corrupt_fs_revision(svn_fs_t *fs, svn_revnum_t rev)
{
  return svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0,
     _("Corrupt filesystem revision %ld in filesystem '%s'"),
     rev, fs->path);
}


svn_error_t *
svn_fs_base__err_dangling_id(svn_fs_t *fs, const svn_fs_id_t *id)
{
  svn_string_t *id_str = svn_fs_base__id_unparse(id, fs->pool);
  return svn_error_createf
    (SVN_ERR_FS_ID_NOT_FOUND, 0,
     _("Reference to non-existent node '%s' in filesystem '%s'"),
     id_str->data, fs->path);
}


svn_error_t *
svn_fs_base__err_dangling_rev(svn_fs_t *fs, svn_revnum_t rev)
{
  return svn_error_createf
    (SVN_ERR_FS_NO_SUCH_REVISION, 0,
     _("No such revision %ld in filesystem '%s'"),
     rev, fs->path);
}


svn_error_t *
svn_fs_base__err_corrupt_txn(svn_fs_t *fs,
                             const char *txn)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0,
     _("Corrupt entry in 'transactions' table for '%s'"
       " in filesystem '%s'"), txn, fs->path);
}


svn_error_t *
svn_fs_base__err_corrupt_copy(svn_fs_t *fs, const char *copy_id)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0,
     _("Corrupt entry in 'copies' table for '%s' in filesystem '%s'"),
     copy_id, fs->path);
}


svn_error_t *
svn_fs_base__err_no_such_txn(svn_fs_t *fs, const char *txn)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NO_SUCH_TRANSACTION, 0,
     _("No transaction named '%s' in filesystem '%s'"),
     txn, fs->path);
}


svn_error_t *
svn_fs_base__err_txn_not_mutable(svn_fs_t *fs, const char *txn)
{
  return
    svn_error_createf
    (SVN_ERR_FS_TRANSACTION_NOT_MUTABLE, 0,
     _("Cannot modify transaction named '%s' in filesystem '%s'"),
     txn, fs->path);
}


svn_error_t *
svn_fs_base__err_no_such_copy(svn_fs_t *fs, const char *copy_id)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NO_SUCH_COPY, 0,
     _("No copy with id '%s' in filesystem '%s'"), copy_id, fs->path);
}


svn_error_t *
svn_fs_base__err_bad_lock_token(svn_fs_t *fs, const char *lock_token)
{
  return
    svn_error_createf
    (SVN_ERR_FS_BAD_LOCK_TOKEN, 0,
     _("Token '%s' does not point to any existing lock in filesystem '%s'"),
     lock_token, fs->path);
}

svn_error_t *
svn_fs_base__err_no_lock_token(svn_fs_t *fs, const char *path)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NO_LOCK_TOKEN, 0,
     _("No token given for path '%s' in filesystem '%s'"), path, fs->path);
}

svn_error_t *
svn_fs_base__err_corrupt_lock(svn_fs_t *fs, const char *lock_token)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0,
     _("Corrupt lock in 'locks' table for '%s' in filesystem '%s'"),
     lock_token, fs->path);
}

svn_error_t *
svn_fs_base__err_no_such_node_origin(svn_fs_t *fs, const char *node_id)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NO_SUCH_NODE_ORIGIN, 0,
     _("No record in 'node-origins' table for node id '%s' in "
       "filesystem '%s'"), node_id, fs->path);
}

svn_error_t *
svn_fs_base__err_no_such_checksum_rep(svn_fs_t *fs, svn_checksum_t *checksum)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NO_SUCH_CHECKSUM_REP, 0,
     _("No record in 'checksum-reps' table for checksum '%s' in "
       "filesystem '%s'"), svn_checksum_to_cstring_display(checksum,
                                                           fs->pool),
                           fs->path);
}
