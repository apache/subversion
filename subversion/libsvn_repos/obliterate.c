/*
 * obliterate.c: permanently delete history from the repository
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */


#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_dirent_uri.h"

#include "repos.h"
#include "private/svn_repos_private.h"
#include "private/svn_fs_private.h"
#include "svn_private_config.h"



svn_error_t *
svn_repos__obliterate_path_rev(svn_repos_t *repos,
                               const char *username,
                               svn_revnum_t revision,
                               const char *path,
                               apr_pool_t *pool)
{
  svn_fs_t *fs = svn_repos_fs(repos);
  svn_fs_root_t *rev_root, *txn_root;
  svn_fs_txn_t *txn;
  const svn_fs_id_t *node_id;
  svn_string_t *obliteration_set;

  SVN_ERR_ASSERT(path[0] == '/' && svn_relpath_is_canonical(path + 1, pool));

  /* Sanity check: ensure the path exists in fs at the revision.
   * ### TODO: May want to allow non-existent node as a no-op.
   * ### This is an error for now to help catch wrong-node-reached bugs. */
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, revision, pool));
  SVN_ERR(svn_fs_node_id(&node_id, rev_root, path, pool));

  /* Run the pre-obliterate hook. Fail if it doesn't exist or if it rejects
   * access. */
  obliteration_set = svn_string_createf(pool, "%s@%ld\n", path, revision);
  SVN_ERR(svn_repos__hooks_pre_obliterate(repos, revision, username,
                                          obliteration_set, pool));

  /* Check the node kind of PATH in our transaction.  */
  /* SVN_ERR(svn_fs_check_path(&kind, rev_root, path, pool));
   * if (kind == svn_node_none) ... */

  /* ### authz checks: see commit.c:delete_entry() */

  /* Begin a new transaction, based on the revision we want to modify. */
  SVN_ERR(svn_fs__begin_obliteration_txn(&txn, fs, revision, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Make the required changes in this txn */
  SVN_ERR(svn_fs_delete(txn_root, path, pool));

  /* Commit the new transaction in place of the old revision */
  SVN_ERR(svn_fs__commit_obliteration_txn(revision, txn, pool));

  return SVN_NO_ERROR;
}


