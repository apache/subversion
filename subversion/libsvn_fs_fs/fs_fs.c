/* fs_fs.c --- filesystem operations specific to fs_fs
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
#include <apr_uuid.h>

#include "svn_pools.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_hash.h"
#include "id.h"
#include "fs.h"
#include "err.h"
#include "dag.h"
#include "fs_fs.h"

/* Notes:

   To avoid opening and closing the rev-files all the time, it would
   probably be advantageous to keep each rev-file open for the
   lifetime of the transaction object.  I'll leave that as a later
   optimization for now.
   
*/


svn_error_t *
svn_fs__fs_open (svn_fs_t *fs, const char *path, apr_pool_t *pool)
{
  abort ();

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_youngest_revision (svn_revnum_t *youngest_p,
                              svn_fs_t *fs,
                              apr_pool_t *pool)
{
  abort ();

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_get_rev (svn_fs__revision_t **rev_p,
                    svn_fs_t *fs,
                    svn_revnum_t rev,
                    apr_pool_t *pool)
{
  abort ();

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_get_node_revision (svn_fs__node_revision_t **noderev_p,
                              svn_fs_t *fs,
                              const svn_fs_id_t *id,
                              apr_pool_t *pool)
{
  abort ();

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_put_node_revision (svn_fs_t *fs,
                              const svn_fs_id_t *id,
                              svn_fs__node_revision_t *noderev,
                              apr_pool_t *pool)
{
  abort();

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__fs_get_proplist (apr_hash_t **proplist_p,
                         svn_fs_t *fs,
                         svn_fs__node_revision_t *noderev,
                         apr_pool_t *pool)
{
  abort ();

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_rep_contents_dir (apr_hash_t **entries_p,
                             svn_fs_t *fs,
                             svn_fs__node_revision_t *noderev,
                             apr_pool_t *pool)
{
  abort ();
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__fs_rev_get_root (svn_fs_id_t **root_id_p,
                         svn_fs_t *fs,
                         svn_revnum_t rev,
                         apr_pool_t *pool)
{
  abort ();

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_revision_proplist (apr_hash_t **proplist_p,
                              svn_fs_t *fs,
                              svn_revnum_t rev,
                              apr_pool_t *pool)
{
  abort ();

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_get_contents (svn_stream_t **contents_p,
                         svn_fs_t *fs,
                         svn_fs__node_revision_t *noderev,
                         apr_pool_t *pool)
{
  abort ();
  
  return SVN_NO_ERROR;
}
