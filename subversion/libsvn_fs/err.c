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
/* #include <db.h> */
#include <apr_strings.h>

#include "svn_fs.h"
#include "fs.h"
#include "err.h"

svn_error_t *
svn_fs__check_fs (svn_fs_t *fs)
{
  if (fs->env)
    return SVN_NO_ERROR;
  else
    return svn_error_create (SVN_ERR_FS_NOT_OPEN, 0,
                             "filesystem object has not been opened yet");
}



/* Building common error objects.  */


static svn_error_t *
corrupt_id (const char *fmt, const svn_fs_id_t *id, svn_fs_t *fs)
{
  svn_string_t *unparsed_id = svn_fs_unparse_id (id, fs->pool);
  return svn_error_createf (SVN_ERR_FS_CORRUPT, 0,
                            fmt, unparsed_id->data, fs->path);
}


svn_error_t *
svn_fs__err_corrupt_node_revision (svn_fs_t *fs, const svn_fs_id_t *id)
{
  return
    corrupt_id ("corrupt node revision for node `%s' in filesystem `%s'",
                id, fs);
}


svn_error_t *
svn_fs__err_corrupt_fs_revision (svn_fs_t *fs, svn_revnum_t rev)
{
  return svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0,
     "corrupt filesystem revision `%" SVN_REVNUM_T_FMT "' in filesystem `%s'",
     rev, fs->path);
}


svn_error_t *
svn_fs__err_corrupt_clone (svn_fs_t *fs,
                           const char *svn_txn,
                           const char *base_path)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0,
     "corrupt clone record for `%s' in transaction `%s' in filesystem `%s'",
     base_path, svn_txn, fs->path);
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
  svn_string_t *id_str = svn_fs_unparse_id (id, fs->pool);
  return svn_error_createf
    (SVN_ERR_FS_ID_NOT_FOUND, 0,
     "reference to non-existent node `%s' in filesystem `%s'",
     id_str->data, fs->path);
}


svn_error_t *
svn_fs__err_dangling_rev (svn_fs_t *fs, svn_revnum_t rev)
{
  return svn_error_createf
    (SVN_ERR_FS_NO_SUCH_REVISION, 0,
     "reference to non-existent revision `%"
     SVN_REVNUM_T_FMT
     "' in filesystem `%s'",
     rev, fs->path);
}


svn_error_t *
svn_fs__err_corrupt_nodes_key (svn_fs_t *fs)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0,
     "malformed ID as key in `nodes' table of filesystem `%s'", fs->path);
}


svn_error_t *
svn_fs__err_corrupt_next_id (svn_fs_t *fs, const char *table)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0,
     "corrupt value for `next-id' key in `%s' table of filesystem `%s'", 
     table, fs->path);
}


svn_error_t *
svn_fs__err_corrupt_txn (svn_fs_t *fs,
                         const char *txn)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0,
     "corrupt entry in `transactions' table for `%s'"
     " in filesystem `%s'", txn, fs->path);
}


svn_error_t *
svn_fs__err_corrupt_copy (svn_fs_t *fs, const char *copy_id)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0,
     "corrupt entry in `copies' table for `%s' in filesystem `%s'", 
     copy_id, fs->path);
}


svn_error_t *
svn_fs__err_not_mutable (svn_fs_t *fs, svn_revnum_t rev, const char *path)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NOT_MUTABLE, 0,
     "File is not mutable: filesystem `%s', revision %" SVN_REVNUM_T_FMT
     ", path `%s'", fs->path, rev, path);
}


svn_error_t *
svn_fs__err_path_syntax (svn_fs_t *fs, const char *path)
{
  return
    svn_error_createf
    (SVN_ERR_FS_PATH_SYNTAX, 0,
     "search for malformed path `%s' in filesystem `%s'",
     path, fs->path);
}


svn_error_t *
svn_fs__err_no_such_txn (svn_fs_t *fs, const char *txn)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NO_SUCH_TRANSACTION, 0,
     "no transaction named `%s' in filesystem `%s'",
     txn, fs->path);
}


svn_error_t *
svn_fs__err_txn_not_mutable (svn_fs_t *fs, const char *txn)
{
  return
    svn_error_createf
    (SVN_ERR_FS_TRANSACTION_NOT_MUTABLE, 0,
     "cannot modify transaction named `%s' in filesystem `%s'",
     txn, fs->path);
}


svn_error_t *
svn_fs__err_no_such_copy (svn_fs_t *fs, const char *copy_id)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NO_SUCH_COPY, 0,
     "no copy with id `%s' in filesystem `%s'", copy_id, fs->path);
}


svn_error_t *
svn_fs__err_not_directory (svn_fs_t *fs, const char *path)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NOT_DIRECTORY, 0,
     "`%s' is not a directory in filesystem `%s'",
     path, fs->path);
}
