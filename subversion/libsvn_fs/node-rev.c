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
   as part of TRAIL.  If TARGET or SOURCE does not exist, simply
   return success having done nothing.  */
static svn_error_t *
deltify (svn_fs_id_t *target,
         svn_fs_id_t *source,
         svn_fs_t *fs,
         trail_t *trail)
{
#if 0
  svn_stream_t *target_stream, *source_stream, *txdelta_stream;
  skel_t *target_nr, *source_nr;
  svn_txdelta_window_t *window;

  SVN_ERR (svn_fs__get_node_revision (&target_nr, fs, target_id, trail));
  SVN_ERR (svn_fs__get_node_revision (&target_nr, fs, target_id, trail));

  /* It is not an error to attempt to deltify something that does not
     exist, or against something that does not exist.  However, no
     deltification occurs, of course. */
  if ((target_nr == NULL) || (source_nr == NULL))
    return SVN_NO_ERROR;


  /* ### todo: use the new rep reading code to read the reps, etc,
     etc, blah blah blah... */

  source_stream = fooo;
  target_stream = fooo;


  svn_txdelta (&txdelta_stream, source_stream, target_stream, trail->pool);

  do
    {
      SVN_ERR (svn_txdelta_next_window (&window, txdelta_stream));
      if (window)
        {
          fooo;
        }

      if (window)
        svn_txdelta_free_window (window);
    } while (window);
  

  {
    svn_stringbuf_t *targetstr, *sourcestr;
    
    targetstr = svn_fs_unparse_id (target, trail->pool);
    sourcestr = svn_fs_unparse_id (source, trail->pool);
    
    printf ("*** Deltifying %s against %s\n",
            targetstr->data, sourcestr->data);
  }

#endif /* 0 */

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
