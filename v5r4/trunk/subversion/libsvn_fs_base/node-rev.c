/* node-rev.c --- storing and retrieving NODE-REVISION skels
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

#define APU_WANT_DB
#include <apu_want.h>

#include "svn_fs.h"
#include "fs.h"
#include "err.h"
#include "node-rev.h"
#include "reps-strings.h"

#include "bdb/nodes-table.h"


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

  /* Find an unused ID for the node.  */
  SVN_ERR(svn_fs_bdb__new_node_id(&id, fs, copy_id, txn_id, trail, pool));

  /* Store its NODE-REVISION skel.  */
  SVN_ERR(svn_fs_bdb__put_node_revision(fs, id, noderev, trail, pool));

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

  /* Choose an ID for the new node, and store it in the database.  */
  SVN_ERR(svn_fs_bdb__new_successor_id(&new_id, fs, old_id, copy_id,
                                       txn_id, trail, pool));

  /* Store the new skel under that ID.  */
  SVN_ERR(svn_fs_bdb__put_node_revision(fs, new_id, new_noderev, 
                                        trail, pool));

  *new_id_p = new_id;
  return SVN_NO_ERROR;
}



/* Deleting a node revision. */

svn_error_t *
svn_fs_base__delete_node_revision(svn_fs_t *fs,
                                  const svn_fs_id_t *id,
                                  trail_t *trail,
                                  apr_pool_t *pool)
{
  /* ### todo: here, we should adjust other nodes to compensate for
     the missing node. */

  return svn_fs_bdb__delete_nodes_entry(fs, id, trail, pool);
}
