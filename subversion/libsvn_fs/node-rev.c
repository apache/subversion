/* node-rev.c --- storing and retrieving NODE-REVISION skels
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <string.h>
#include <db.h>

#include "svn_fs.h"
#include "fs.h"
#include "dbt.h"
#include "err.h"
#include "nodes-table.h"
#include "node-rev.h"


/* Storing and retrieving NODE-REVISION skels.  */


svn_error_t *
svn_fs__get_node_revision (skel_t **skel_p,
                           svn_fs_t *fs,
                           const svn_fs_id_t *id,
                           trail_t *trail)
{
  skel_t *skel;

  SVN_ERR (svn_fs__get_rep (&skel, fs, id, trail));

  /* This is where we would handle diffy representations, to construct
     a NODE-REVISION given its REPRESENTATION.  But I want to get the
     essentials working before I add that to the mix.

     When we add diffy representations, we'll need to start caching
     the expanded contents of immutable nodes.  */
  if (svn_fs__list_length (skel) != 2
      || ! svn_fs__matches_atom (skel->children, "fulltext"))
    return svn_fs__err_corrupt_representation (fs, id);

  *skel_p = skel->children->next;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__put_node_revision (svn_fs_t *fs,
                           const svn_fs_id_t *id,
                           skel_t *skel,
                           trail_t *trail)
{
  skel_t rep[2];

  /* We always write out new nodes as fulltext.  Converting older
     nodes to deltas against this one happens later, when we call
     svn_fs__stable_node.  */

  /* Build a "fulltext" skel containing the NODE-REVISION provided by
     the caller.  */
  memset (rep, 0, sizeof (rep));
  rep[0].is_atom = 0;
  rep[0].children = &rep[1];

  rep[1].is_atom = 1;
  rep[1].data = "fulltext";
  rep[1].len  = sizeof ("fulltext") - 1;
  rep[1].next = skel;

  skel->next = 0;

  /* This call checks the form of the skel for us, so we're protected
     from errors on the caller's part.  */
  SVN_ERR (svn_fs__put_rep (fs, id, rep, trail));

  return 0;
}



/* Creating completely new nodes.  */


svn_error_t *
svn_fs__create_node (svn_fs_id_t **id_p,
                     svn_fs_t *fs,
                     skel_t *skel,
                     trail_t *trail)
{
  svn_fs_id_t *id;

  /* Find an unused ID for the node.  */
  SVN_ERR (svn_fs__new_node_id (&id, fs, trail));

  /* Store its NODE-REVISION skel.  */
  SVN_ERR (svn_fs__put_node_revision (fs, id, skel, trail));

  *id_p = id;
  return SVN_NO_ERROR;
}



/* Creating new revisions of existing nodes.  */


svn_error_t *
svn_fs__create_successor (svn_fs_id_t **new_id_p,
                          svn_fs_t *fs,
                          svn_fs_id_t *old_id,
                          skel_t *new_skel,
                          trail_t *trail)
{
  svn_fs_id_t *new_id;

  /* Choose an ID for the new node, and store it in the database.  */
  SVN_ERR (svn_fs__new_successor_id (&new_id, fs, old_id, trail));

  /* Store the new skel under that ID.  */
  SVN_ERR (svn_fs__put_node_revision (fs, new_id, new_skel, trail));

  *new_id_p = new_id;
  return SVN_NO_ERROR;
}



/* Stable nodes and deltification.  */


svn_error_t *
svn_fs__stable_node (svn_fs_t *fs,
                     svn_fs_id_t *id,
                     trail_t *trail)
{
  /* As remarked above, we don't actually store anything in a
     deltified form yet, so this function is a no-op.  This
     implementation, along with the definitions of
     svn_fs__get_node_revision and svn_fs__put_node_revision, is
     completely correct, but not efficient.  */
  return SVN_NO_ERROR;
}



/* Deleting a node revision. */

svn_error_t *
svn_fs__delete_node_revision (svn_fs_t *fs,
                              const svn_fs_id_t *id,
                              trail_t *trail)
{
  return svn_fs__delete_nodes_entry (fs, id, trail);
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
