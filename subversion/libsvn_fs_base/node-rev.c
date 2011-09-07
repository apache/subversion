/* node-rev.c --- storing and retrieving NODE-REVISION skels
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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

#include <string.h>

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include "svn_pools.h"
#include "svn_fs.h"
#include "fs.h"
#include "err.h"
#include "node-rev.h"
#include "reps-strings.h"
#include "id.h"
#include "../libsvn_fs/fs-loader.h"
#include "revs-txns.h"

#include "bdb/nodes-table.h"
#include "bdb/node-origins-table.h"
#include "bdb/successors-table.h"


/* Creating completely new nodes.  */


svn_error_t *
svn_fs_base__create_node(const svn_fs_id_t **id_p,
                         svn_fs_t *fs,
                         node_revision_t *noderev,
                         const char *copy_id,
                         const char *txn_id,
                         trail_t *trail,
                         apr_pool_t *pool)
{
  svn_fs_id_t *id;
  base_fs_data_t *bfd = fs->fsap_data;

  /* Find an unused ID for the node.  */
  SVN_ERR(svn_fs_bdb__new_node_id(&id, fs, copy_id, txn_id, trail, pool));

  /* Store its NODE-REVISION skel.  */
  SVN_ERR(svn_fs_bdb__put_node_revision(fs, id, noderev, trail, pool));

  /* Add a record in the node origins index table if our format
     supports it.  */
  if (bfd->format >= SVN_FS_BASE__MIN_NODE_ORIGINS_FORMAT)
    {
      SVN_ERR(svn_fs_bdb__set_node_origin(fs, svn_fs_base__id_node_id(id),
                                          id, trail, pool));
    }

  *id_p = id;
  return SVN_NO_ERROR;
}



/* Creating new revisions of existing nodes.  */

svn_error_t *
svn_fs_base__create_successor(const svn_fs_id_t **new_id_p,
                              svn_fs_t *fs,
                              const svn_fs_id_t *old_id,
                              node_revision_t *new_noderev,
                              const char *copy_id,
                              const char *txn_id,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  svn_fs_id_t *new_id;
  base_fs_data_t *bfd = fs->fsap_data;

  /* Choose an ID for the new node, and store it in the database.  */
  SVN_ERR(svn_fs_bdb__new_successor_id(&new_id, fs, old_id, copy_id,
                                       txn_id, trail, pool));

  /* Store the new skel under that ID.  */
  SVN_ERR(svn_fs_bdb__put_node_revision(fs, new_id, new_noderev,
                                        trail, pool));

  /* Record the successor relationship. */
  if (bfd->format >= SVN_FS_BASE__MIN_SUCCESSOR_IDS_FORMAT)
    {
      svn_string_t *old_id_str = svn_fs_base__id_unparse(old_id, pool);
      svn_string_t *new_id_str = svn_fs_base__id_unparse(new_id, pool);

      SVN_ERR(svn_fs_bdb__successors_add(fs, old_id_str->data, new_id_str->data,
                                         trail, pool));
    }

  *new_id_p = new_id;
  return SVN_NO_ERROR;
}



/* Deleting a node revision. */

svn_error_t *
svn_fs_base__delete_node_revision(svn_fs_t *fs,
                                  const svn_fs_id_t *id,
                                  const svn_fs_id_t *pred_id,
                                  trail_t *trail,
                                  apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;

  /* ### TODO: here, we should adjust other nodes to compensate for
     the missing node. */

  /* If there is a predecessor node-rev-ID, we need to remove this
     node as a successor to that node-rev-ID.  Otherwise (if this node
     has no predecessor), we can remove it as a node origin. */
  if (pred_id)
    {
      if (bfd->format >= SVN_FS_BASE__MIN_SUCCESSOR_IDS_FORMAT)
        {
          svn_string_t *node_id_str = svn_fs_base__id_unparse(pred_id, pool);
          svn_string_t *succ_id_str = svn_fs_base__id_unparse(id, pool);

          SVN_ERR(svn_fs_bdb__successors_delete(fs, node_id_str->data,
                                                succ_id_str->data, trail,
                                                pool));
        }
    }
  else
    {
      if (bfd->format >= SVN_FS_BASE__MIN_NODE_ORIGINS_FORMAT)
        SVN_ERR(svn_fs_bdb__delete_node_origin(fs, svn_fs_base__id_node_id(id),
                                               trail, pool));
    }

  /* ...and then the node itself. */
  return svn_fs_bdb__delete_nodes_entry(fs, id, trail, pool);
}



/* Fetching node successors. */

svn_error_t *
svn_fs_base__get_node_successors(apr_array_header_t **successors_p,
                                 svn_fs_t *fs,
                                 const svn_fs_id_t *id,
                                 svn_boolean_t committed_only,
                                 trail_t *trail,
                                 apr_pool_t *pool)
{
  apr_array_header_t *all_successors, *successors;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_string_t *node_id_str = svn_fs_base__id_unparse(id, pool);
  base_fs_data_t *bfd = fs->fsap_data;
  int i;

  if (bfd->format < SVN_FS_BASE__MIN_SUCCESSOR_IDS_FORMAT)
    {
      return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                              _("FS-BDB version too old to fetch node "
                                "successors"));
    }

  SVN_ERR(svn_fs_bdb__successors_fetch(&all_successors, fs, node_id_str->data,
                                       trail, pool));
  successors = apr_array_make(pool, all_successors->nelts,
                              sizeof(const svn_fs_id_t *));
  for (i = 0; i < all_successors->nelts; i++)
    {
      svn_revnum_t revision;
      const char *succ_id_str = APR_ARRAY_IDX(all_successors, i, const char *);
      const svn_fs_id_t *succ_id = svn_fs_base__id_parse(succ_id_str,
                                                   strlen(succ_id_str), pool);

      svn_pool_clear(subpool);

      /* If we only want stable, committed successor IDs, then we need
         to check each ID's txn-id component to verify that's been
         committed. */
      if (committed_only)
        {
          SVN_ERR(svn_fs_base__txn_get_revision
                  (&revision, fs, svn_fs_base__id_txn_id(succ_id),
                   trail, subpool));
          if (! SVN_IS_VALID_REVNUM(revision))
            continue;
        }

      APR_ARRAY_PUSH(successors, const svn_fs_id_t *) = succ_id;
    }
  svn_pool_destroy(subpool);

  *successors_p = successors;
  return SVN_NO_ERROR;
}
