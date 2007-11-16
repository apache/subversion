/* node-origins-sqlite-index.c
 *
 * ====================================================================
 * Copyright (c) 2006-2007 CollabNet.  All rights reserved.
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
#include <ctype.h>
#include <assert.h>

#include <apr_general.h>
#include <apr_pools.h>

#include <sqlite3.h>

#include "svn_fs.h"
#include "svn_path.h"

#include "private/svn_fs_sqlite.h"
#include "private/svn_fs_node_origins.h"
#include "../libsvn_fs/fs-loader.h"
#include "svn_private_config.h"

#include "sqlite-util.h"


/* A flow-control helper macro for sending processing to the 'cleanup'
  label when the local variable 'err' is not SVN_NO_ERROR. */
#define MAYBE_CLEANUP if (err) goto cleanup


svn_error_t *
svn_fs__set_node_origins(svn_fs_t *fs,
                         apr_hash_t *node_origins,
                         apr_pool_t *pool)
{
  /* XXXdsg Implement! */
  return SVN_NO_ERROR;
}

/* Set *ORIGIN_ID to the node revision ID from which the history of
   all nodes in FS whose "Node ID" is NODE_ID springs, as determined
   by a look in the index.  Use POOL for allocations.

   If there is no entry for NODE_ID in the cache, return
   SVN_ERR_FS_NO_SUCH_NODE_ORIGIN. */
svn_error_t *
svn_fs__get_node_origin(const svn_fs_id_t **origin_id,
                        svn_fs_t *fs,
                        const char *node_id,
                        apr_pool_t *pool)
{
  /* XXXdsg Implement! */
  return svn_error_createf(SVN_ERR_FS_NO_SUCH_NODE_ORIGIN, NULL,
                           _("No cached node origin for node id '%s' in "
                             "filesystem '%s'"), node_id, fs->path);
}
