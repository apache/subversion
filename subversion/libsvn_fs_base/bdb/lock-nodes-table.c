/* lock-nodes-table.c : operations on the `lock-nodes' table
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

#include <string.h>
#include <assert.h>
#include "bdb_compat.h"

#include "svn_pools.h"
#include "dbt.h"
#include "../err.h"
#include "../fs.h"
#include "../key-gen.h"
#include "../util/skel.h"
#include "../util/fs_skels.h"
#include "../trail.h"
#include "../../libsvn_fs/fs-loader.h"
#include "bdb-err.h"
#include "lock-nodes-table.h"




int
svn_fs_bdb__open_lock_nodes_table (DB **lock_nodes_p,
                                   DB_ENV *env,
                                   svn_boolean_t create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *lock_nodes;

  BDB_ERR (svn_fs_bdb__check_version());
  BDB_ERR (db_create (&lock_nodes, env, 0));
  BDB_ERR (lock_nodes->open (SVN_BDB_OPEN_PARAMS(lock_nodes, NULL),
                             "lock-nodes", 0, DB_BTREE,
                             open_flags | SVN_BDB_AUTO_COMMIT,
                             0666));

  *lock_nodes_p = lock_nodes;
  return 0;
}
