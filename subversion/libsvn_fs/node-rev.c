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
#include "svn_pools.h"
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


/* In FS, change ID's representation to be a fulltext representation
   as part of TRAIL.  If ID does not exist, do nothing and return
   success. */
static svn_error_t *
undeltify (svn_fs_id_t *id,
           svn_fs_t *fs,
           trail_t *trail)
{
  skel_t *node_rev;
  const char *prop_key = NULL, *data_key = NULL;
  skel_t *pkey_skel, *dkey_skel;

  /* Turn ID into a skel so we can get the rep keys. */
  SVN_ERR (svn_fs__get_node_revision (&node_rev, fs, id, trail));

  /* Check that target exists.  If not, no big deal -- just do
     nothing. */
  if (node_rev == NULL)
    return SVN_NO_ERROR;

  /* Get the property key. */
  pkey_skel = SVN_FS__NR_PROP_KEY (node_rev);
  if (pkey_skel->len != 0)
    prop_key = apr_pstrndup (trail->pool, pkey_skel->data, pkey_skel->len);

  /* Get the data key. */
  dkey_skel = SVN_FS__NR_DATA_KEY (node_rev);
  if (dkey_skel->len != 0)
    data_key = apr_pstrndup (trail->pool, dkey_skel->data, dkey_skel->len);

  /* Undeltify the properties. */
  if (prop_key)
    SVN_ERR (svn_fs__rep_undeltify (fs, prop_key, trail));

  /* Undeltify the data (entries list for directories, file contents
     for files). */
  if (data_key)
    SVN_ERR (svn_fs__rep_undeltify (fs, data_key, trail));

  return SVN_NO_ERROR;
}


struct deltify_args {
  svn_fs_t *fs;
  svn_fs_root_t *root;
  const char *path;
};


/* Find a suitable fulltext against which to deltify ARGS->path under
   ARGS->root in ARGS->fs.  Some thoughts on that.

   In general, we want to deltify against the youngest fulltext
   available, which generally will be youngest revision of a given
   node.

   We can't just blindly deltify against ARGS->path in the head
   revision, because ARGS->path might have been removed or renamed
   somewhere along the way.

   Just because ARGS->path exist under some revision does not mean it
   is an optimal deltification target.  ARGS->path might exist because
   some different node with completely unrelated text was replaced
   ARGS->path.  In other words, we should at least pay attention to
   node ancestry.

   To further complicate matters, while it is trivial to determine a
   node's predecessor, finding its youngest successor using node IDs
   alone is quite a different story.  Each of its successors could
   contain branched node IDs, branching (multiple times, perhaps) at
   any point in revision history.  In other words, while we can easily
   say that the predecessor of node 3.5 is node 3.4, its youngest
   successor could be as close as 3.6, or really any number of things,
   depending on the number of times it has been modified throughout
   revision history.  We might see that successor as 3.100, 3.99.1.1,
   3.98.1.2, 3.98.2.1, 3.98.1.1.1.1, and so on (for a very
   unpredictably long time).  

   Perhaps the best solution is one that combines path searches under
   revisions with careful (crafty) node ID examination.  */
static svn_error_t *
txn_body_deltify (void *baton, trail_t *trail)
{
  struct deltify_args *args = baton;
  svn_fs_id_t *source_id = NULL, *target_id;
  svn_revnum_t i, current, youngest;
  svn_fs_root_t *y_root;
  apr_pool_t *subpool = svn_pool_create (trail->pool);
  svn_error_t *err;

  /* Our current algorithm: search backward through revision history,
     from HEAD to ARGS->root's revision + 1, for the last instance of
     ARGS->path still related to ARGS->path under ARGS->root. */

  /* Get our two bounding revisions. */
  SVN_ERR (svn_fs_youngest_rev (&youngest, args->fs, trail->pool));
  current = svn_fs_revision_root_revision (args->root);

  /* Get the ID of the target, which is the node we're changing. */
  SVN_ERR (svn_fs_node_id (&target_id, args->root, args->path, trail->pool));

  for (i = youngest; i > current; i--)
    {
      svn_pool_clear (subpool);

      /* Get the root of the revision we're examining. */
      SVN_ERR (svn_fs_revision_root (&y_root, args->fs, youngest, subpool));

      /* Get the node id of ARGS->path under that revision.  If this
         fails for some reason, we don't really care.  */
      err = svn_fs_node_id (&source_id, y_root, args->path, subpool);
      if (! err)
        {
          /* We have a source ID.  If it is related to our target ID,
             we'll call it quits on our search.  Else, another
             iteration is in order. */
          if (svn_fs_id_distance (source_id, target_id) != -1)
            {
              /* Copy SOURCE_ID out into TRAIL->pool so it doesn't
                 disappear when we destroy SUBPOOL.  */
              source_id = svn_fs_copy_id (source_id, trail->pool);
              break;
            }
        }

      /* Make sure SOURCE_ID is NULL -- we didn't find a valid one. */
      source_id = NULL;
    }

  /* Destroy the subpool. */
  svn_pool_destroy (subpool);

  /* If we found a valid source ID, perform the deltification step. */
  if (source_id)
    SVN_ERR (deltify (target_id, source_id, args->fs, trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_deltify (svn_fs_root_t *root,
                const char *path,
                int recursive,
                apr_pool_t *pool)
{
  struct deltify_args args;

  if (! svn_fs_is_revision_root (root))
    return svn_error_create (SVN_ERR_FS_NOT_REVISION_ROOT, 0, NULL, pool,
                             "svn_fs_deltify: root is not a revision root");

  /* ### todo:  Support recursiveness. */
  if (recursive)
    return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
                             "Recursive deltification is not implemented");

  args.fs = svn_fs_root_fs (root);
  args.root = root;
  args.path = path;

  SVN_ERR (svn_fs__retry_txn (args.fs, txn_body_deltify, &args, pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
txn_body_undeltify (void *baton, trail_t *trail)
{
  struct deltify_args *args = baton;
  svn_fs_id_t *id;

  /* Get the ID of the target, which is the node we're changing. */
  SVN_ERR (svn_fs_node_id (&id, args->root, args->path, trail->pool));

  /* Perform the un-deltification step. */
  SVN_ERR (undeltify (id, args->fs, trail));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_undeltify (svn_fs_root_t *root,
                  const char *path,
                  int recursive,
                  apr_pool_t *pool)
{
  struct deltify_args args;

  if (! svn_fs_is_revision_root (root))
    return svn_error_create (SVN_ERR_FS_NOT_REVISION_ROOT, 0, NULL, pool,
                             "svn_fs_deltify: root is not a revision root");

  /* ### todo:  Support recursiveness. */
  if (recursive)
    return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
                             "Recursive undeltification is not implemented");

  args.fs = svn_fs_root_fs (root);
  args.root = root;
  args.path = path;

  SVN_ERR (svn_fs__retry_txn (args.fs, txn_body_undeltify, &args, pool));
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
