/* fs.c --- creating, opening and closing filesystems
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
#include <stdio.h>
#include <string.h>
#include <errno.h>              /* for EINVAL */

#include <apr_general.h>
#include <apr_pools.h>
#include <apr_file_io.h>

#include "svn_pools.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "fs.h"
#include "err.h"
#include "dag.h"
#include "fs_fs.h"
#include "revs-txns.h"
#include "tree.h"
#include "svn_private_config.h"

#include "../libsvn_fs/fs-loader.h"



/* If filesystem FS is already open, then return an
   SVN_ERR_FS_ALREADY_OPEN error.  Otherwise, return zero.  */
static svn_error_t *
check_already_open (svn_fs_t *fs)
{
  if (fs->fsap_data)
    return svn_error_create (SVN_ERR_FS_ALREADY_OPEN, 0,
                             "Filesystem object already open");
  else
    return SVN_NO_ERROR;
}




/* This function is provided for Subversion 1.0.x compatibility.  It
   has no effect for fsfs backed Subversion filesystems. */
static svn_error_t *
fs_set_errcall (svn_fs_t *fs,
                void (*db_errcall_fcn) (const char *errpfx, char *msg))
{

  return SVN_NO_ERROR;
}

/* The vtable associated with a specific open filesystem. */
static fs_vtable_t fs_vtable = {
  svn_fs_fs__youngest_rev,
  svn_fs_fs__revision_prop,
  svn_fs_fs__revision_proplist,
  svn_fs_fs__change_rev_prop,
  svn_fs_fs__get_uuid,
  svn_fs_fs__set_uuid,
  svn_fs_fs__revision_root,
  svn_fs_fs__begin_txn,
  svn_fs_fs__open_txn,
  svn_fs_fs__purge_txn,
  svn_fs_fs__list_transactions,
  svn_fs_fs__deltify
};


/* Creating a new filesystem. */

/* Create a new fsfs backed Subversion filesystem at path PATH and
   link it to the filesystem FS.  Perform temporary allocations in
   POOL. */
static svn_error_t *
fs_create (svn_fs_t *fs, const char *path, apr_pool_t *pool)
{
  SVN_ERR (check_already_open (fs));

  fs->vtable = &fs_vtable;
  fs->fsap_data = NULL;

  SVN_ERR (svn_fs_fs__create (fs, path, pool));

  return SVN_NO_ERROR;
}



/* Gaining access to an existing filesystem.  */

/* Implements the svn_fs_open API.  Opens a Subversion filesystem
   located at PATH and sets FS to point to the correct vtable for the
   fsfs filesystem.  All allocations are from POOL. */
static svn_error_t *
fs_open (svn_fs_t *fs, const char *path, apr_pool_t *pool)
{
  SVN_ERR (svn_fs_fs__open (fs, path, fs->pool));

  fs->vtable = &fs_vtable;
  fs->fsap_data = NULL;

  return SVN_NO_ERROR;
}



/* Copy a possibly live Subversion filesystem from SRC_PATH to
   DEST_PATH.  The CLEAN_LOGS argument is ignored and included for
   Subversion 1.0.x compatibility.  Perform all temporary allocations
   in POOL. */
static svn_error_t *
fs_hotcopy (const char *src_path, 
            const char *dest_path, 
            svn_boolean_t clean_logs, 
            apr_pool_t *pool)
{
  SVN_ERR (svn_fs_fs__hotcopy (src_path, dest_path, pool));

  return SVN_NO_ERROR;
}



/* This function is included for Subversion 1.0.x compability.  It has
   no effect for fsfs backed Subversion filesystems. */
static svn_error_t *
fs_recover (const char *path,
            apr_pool_t *pool)
{
  /* This is a no-op for FSFS. */

  return SVN_NO_ERROR;
}




/* This function is included for Subversion 1.0.x compatability.  It
   has no effect for fsfs backed Subversion filesystems. */
static svn_error_t *
fs_logfiles (apr_array_header_t **logfiles,
             const char *path,
             svn_boolean_t only_unused,
             apr_pool_t *pool)
{
  /* A no-op for FSFS. */
  *logfiles = NULL;

  return SVN_NO_ERROR;
}





/* Delete the filesystem located at path PATH.  Perform any temporary
   allocations in POOL. */
static svn_error_t *
fs_delete_fs (const char *path,
              apr_pool_t *pool)
{
  /* Remove everything. */
  SVN_ERR (svn_io_remove_dir (path, pool));

  return SVN_NO_ERROR;
}



/* Base FS library vtable, used by the FS loader library. */

fs_library_vtable_t svn_fs_fs__vtable = {
  fs_create,
  fs_open,
  fs_delete_fs,
  fs_hotcopy,
  fs_set_errcall,
  fs_recover,
  fs_logfiles
};
