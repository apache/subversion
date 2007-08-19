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
svn_fs_fs__err_dangling_id(svn_fs_t *fs, const svn_fs_id_t *id)
{
  svn_string_t *id_str = svn_fs_fs__id_unparse(id, fs->pool);
  return svn_error_createf
    (SVN_ERR_FS_ID_NOT_FOUND, 0,
     _("Reference to non-existent node '%s' in filesystem '%s'"),
     id_str->data, fs->path);
}

svn_error_t *
svn_fs_fs__err_corrupt_lockfile(svn_fs_t *fs, const char *path)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0,
     _("Corrupt lockfile for path '%s' in filesystem '%s'"),
     path, fs->path);
}

