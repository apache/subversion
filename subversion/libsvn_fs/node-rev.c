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
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
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
#include "reps-strings.h"




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

/* In FS, change TARGET's representation to be a delta against SOURCE,
   as part of TRAIL.  If TARGET or SOURCE does not exist, do nothing
   and return success.  */
static svn_error_t *
deltify (svn_fs_id_t *target_id,
         svn_fs_id_t *source_id,
         svn_fs_t *fs,
         trail_t *trail)
{
  skel_t
    *source_nr,           /* source node revision */
    *target_nr;           /* target node revision */

  const char
    *target_pkey,         /* target property rep key  */
    *target_dkey,         /* target data rep key      */
    *source_pkey,         /* source property rep key  */
    *source_dkey;         /* source data rep key      */

  /* Turn those IDs into skels, so we can get the rep keys. */
  SVN_ERR (svn_fs__get_node_revision (&target_nr, fs, target_id, trail));
  SVN_ERR (svn_fs__get_node_revision (&source_nr, fs, source_id, trail));

  /* Check that target and source exist.  It is not an error to
     attempt to deltify something that does not exist, or deltify
     against a non-existent base.  However, nothing happens. */
  if ((target_nr == NULL) || (source_nr == NULL))
    return SVN_NO_ERROR;

  /* We have a target and a source.  Get all the rep keys... */
  {
    skel_t
      *target_pkey_skel,
      *target_dkey_skel,
      *source_pkey_skel,
      *source_dkey_skel;

    /* Target property key. */
    target_pkey_skel = SVN_FS__NR_PROP_KEY (target_nr);
    if (target_pkey_skel->len != 0) {
      target_pkey = apr_pstrndup (trail->pool,
                                  target_pkey_skel->data,
                                  target_pkey_skel->len);
    }
    else
      target_pkey = NULL;

    /* Target data key. */
    target_dkey_skel = SVN_FS__NR_DATA_KEY (target_nr);
    if (target_dkey_skel->len != 0) {
      target_dkey = apr_pstrndup (trail->pool,
                                  target_dkey_skel->data,
                                  target_dkey_skel->len);
    }
    else
      target_dkey = NULL;
    
    /* Source property key. */
    source_pkey_skel = SVN_FS__NR_PROP_KEY (source_nr);
    if (source_pkey_skel->len != 0) {
      source_pkey = apr_pstrndup (trail->pool,
                                  source_pkey_skel->data,
                                  source_pkey_skel->len);
    }
    else
      source_pkey = NULL;

    /* Source data key. */
    source_dkey_skel = SVN_FS__NR_DATA_KEY (source_nr);
    if (source_dkey_skel->len != 0) {
      source_dkey = apr_pstrndup (trail->pool,
                                  source_dkey_skel->data,
                                  source_dkey_skel->len);
    }
    else
      source_dkey = NULL;
  }

  if ((target_pkey && source_pkey)
      && (strcmp (target_pkey, source_pkey)))
    SVN_ERR (svn_fs__rep_deltify (fs, target_pkey, source_pkey, trail));

  if ((target_dkey && source_dkey)
      && (strcmp (target_dkey, source_dkey)))     
   SVN_ERR (svn_fs__rep_deltify (fs, target_dkey, source_dkey, trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__stable_node (svn_fs_t *fs,
                     svn_fs_id_t *id,
                     trail_t *trail)
{
  svn_fs_id_t *predecessor_id = svn_fs_predecessor_id (id, trail->pool);

  if (predecessor_id != NULL)
    SVN_ERR (deltify (predecessor_id, id, fs, trail));

  return SVN_NO_ERROR;
}



/* Deleting a node revision. */

svn_error_t *
svn_fs__delete_node_revision (svn_fs_t *fs,
                              const svn_fs_id_t *id,
                              trail_t *trail)
{
  /* ### todo: here, we should adjust other nodes to compensate for
     the missing node. */

  return svn_fs__delete_nodes_entry (fs, id, trail);
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
