/* deltify.c --- deltification and undeltification of nodes.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include "svn_path.h"

#include "fs.h"
#include "node-rev.h"
#include "dag.h"
#include "id.h"
#include "reps-strings.h"

#include "bdb/nodes-table.h"



/*** Deltification.  ***/

svn_error_t *
svn_fs_deltify (svn_fs_root_t *root,
                const char *path,
                int recursive,
                apr_pool_t *pool)
{
  /* ### todo: make this work [currently, deltify_undeltify() does
     nothing in the deltify case].  */
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL,
                           "svn_fs_deltify: currently not implemented");
}




/*** Undeltification ***/

struct undeltify_args
{
  svn_fs_t *fs;
  const svn_fs_id_t *id;
};


/* In FS, change ID's representation to be a fulltext representation
   as part of TRAIL.  If ID does not exist, do nothing and return
   success. */
static svn_error_t *
txn_body_undeltify (void *baton, trail_t *trail)
{
  svn_fs__node_revision_t *noderev;
  struct undeltify_args *args = baton;

  /* Turn ID into a skel so we can get the rep keys. */
  SVN_ERR (svn_fs__get_node_revision (&noderev, args->fs, args->id, trail));
  
  /* Check that target exists.  If not, no big deal -- just do
     nothing. */
  if (noderev == NULL)
    return SVN_NO_ERROR;
  
  /* Undeltify the properties. */
  if (noderev->prop_key)
    SVN_ERR (svn_fs__rep_undeltify (args->fs, noderev->prop_key, trail));
  
  /* Undeltify the data (entries list for directories, file contents
     for files). */
  if (noderev->data_key)
    SVN_ERR (svn_fs__rep_undeltify (args->fs, noderev->data_key, trail));
  
  return SVN_NO_ERROR;
}


/* Note:  it is acceptable for this function to call back into
   public FS API interfaces because it does not itself use trails.  */
svn_error_t *
svn_fs_undeltify (svn_fs_root_t *root,
                  const char *path,
                  int recursive,
                  apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_pool_t *subpool;
  apr_hash_index_t *hi;
  int is_dir;
  struct undeltify_args args;

  /* Get the node revision ID associated with PATH in ROOT, and
     populate the ARGS baton.  */
  SVN_ERR (svn_fs_node_id (&(args.id), root, path, pool));
  args.fs = svn_fs_root_fs (root);

  /* Now, do the undeltification. */
  SVN_ERR (svn_fs__retry_txn (args.fs, txn_body_undeltify, &args, pool));

  /* If we aren't recursing, we're done. */
  if (! recursive)
    return SVN_NO_ERROR;

  /* If PATH is not a directory, we're done. */
  SVN_ERR (svn_fs_is_dir (&is_dir, root, path, pool));
  if (! is_dir)
    return SVN_NO_ERROR;

  /* Else, read PATH's entries and recurse. */
  subpool = svn_pool_create (pool);
  SVN_ERR (svn_fs_dir_entries (&entries, root, path, pool));
  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_hash_this (hi, &key, NULL, NULL);
      svn_fs_undeltify (root, svn_path_join (path, key, subpool),
                        recursive, subpool);
      svn_pool_clear (subpool);
    }
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
