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
#include "nodes-table.h"
#include "node-rev.h"
#include "reps-strings.h"
#include "dag.h"
#include "id.h"



/* Deltification.  */

/* In FS, change ID's representation to be a fulltext representation
   as part of TRAIL.  If ID does not exist, do nothing and return
   success. */
static svn_error_t *
undeltify (svn_fs_id_t *id,
           svn_fs_t *fs,
           trail_t *trail)
{
  svn_fs__node_revision_t *noderev;

  /* Turn ID into a skel so we can get the rep keys. */
  SVN_ERR (svn_fs__get_node_revision (&noderev, fs, id, trail));

  /* Check that target exists.  If not, no big deal -- just do
     nothing. */
  if (noderev == NULL)
    return SVN_NO_ERROR;

  /* Undeltify the properties. */
  if (noderev->prop_key)
    SVN_ERR (svn_fs__rep_undeltify (fs, noderev->prop_key, trail));

  /* Undeltify the data (entries list for directories, file contents
     for files). */
  if (noderev->data_key)
    SVN_ERR (svn_fs__rep_undeltify (fs, noderev->data_key, trail));

  return SVN_NO_ERROR;
}



/* Deltify or undeltify a directory PATH under ROOT in FS (whose
   node-id is ID).  If DO_DELTIFY is non-zero, drive deltify_by_id(),
   else drive undeltify().  Use RECURSE to determine if this function
   should recursively deltify or undeltify directories.  */
static svn_error_t *
deltify_undeltify (svn_fs_t *fs,
                   svn_fs_root_t *root,
                   const char *path,
                   svn_fs_id_t *id,
                   int do_deltify,
                   int recurse,
                   trail_t *trail)
{
  apr_pool_t *subpool = svn_pool_create (trail->pool);
  svn_stringbuf_t *full_path = svn_stringbuf_create (path, subpool);
  dag_node_t *node;
  int is_dir = 0;

  /* Get the node from its ID, and figure out if it is a directory. */
  SVN_ERR (svn_fs__dag_get_node (&node, fs, id, trail));
  is_dir = svn_fs__dag_is_directory (node);

  /* If this is a directory and we have been asked to recurse, then
     call this function for each of this ID's entries. */
  if (is_dir && recurse)
    {
      apr_hash_t *entries;
      apr_hash_index_t *hi;

      SVN_ERR (svn_fs__dag_dir_entries (&entries, node, trail));
      if (entries)
        {
          for (hi = apr_hash_first (subpool, entries); 
               hi; 
               hi = apr_hash_next (hi))
            {
              void *val;
              svn_fs_dirent_t *entry;
              
              /* KEY (unneeded) is be the entry name in source, and
                 VAL is the dirent */
              apr_hash_this (hi, NULL, NULL, &val);
              entry = val;
              
              /* Construct the full path of this entry, and recurse. */
              svn_stringbuf_set (full_path, path);
              svn_path_add_component_nts (full_path, entry->name);
              SVN_ERR (deltify_undeltify (fs, root, full_path->data, entry->id,
                                          do_deltify, recurse, trail));
            }
        }
    }

  /* Do the real work of ... */
  if (do_deltify)
    {
      /* ... deltification. */
      /* ### todo:  actually do this */
    }
  else
    {
      /* ... un-deltification. */
      SVN_ERR (undeltify (id, fs, trail));
    }

  /* Destroy per-iteration subpool. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}


struct deltify_args {
  svn_fs_t *fs;
  svn_fs_root_t *root;
  const char *path;
  svn_fs_id_t *id;
  int recursive;
};


static svn_error_t *
txn_body_deltify (void *baton, trail_t *trail)
{
  struct deltify_args *args = baton;
  int is_dir = 0;
  dag_node_t *node;

  /* Use the ID to determine if the target here is a directory. */
  SVN_ERR (svn_fs__dag_get_node (&node, args->fs, args->id, trail));
  is_dir = svn_fs__dag_is_directory (node);

  /* Perform the deltification step. */
  SVN_ERR (deltify_undeltify (args->fs, args->root, args->path, args->id,
                              1, args->recursive, trail));

  return SVN_NO_ERROR;
}


static svn_error_t *
txn_body_undeltify (void *baton, trail_t *trail)
{
  struct deltify_args *args = baton;
  int is_dir = 0;
  dag_node_t *node;

  /* Use the ID to determine if the target here is a directory. */
  SVN_ERR (svn_fs__dag_get_node (&node, args->fs, args->id, trail));
  is_dir = svn_fs__dag_is_directory (node);

  /* Perform the un-deltification step. */
  SVN_ERR (deltify_undeltify (args->fs, args->root, args->path, args->id,
                              0, args->recursive, trail));

  return SVN_NO_ERROR;
}



/*** Public (or semi-public) interfaces. ***/

svn_error_t *
svn_fs_deltify (svn_fs_root_t *root,
                const char *path,
                int recursive,
                apr_pool_t *pool)
{
  struct deltify_args args;

  /* ### todo: make this work [currently, deltify_undeltify() does
     nothing in the deltify case].  */
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
                           "svn_fs_deltify: currently not implemented");

  if (! svn_fs_is_revision_root (root))
    return svn_error_create (SVN_ERR_FS_NOT_REVISION_ROOT, 0, NULL, pool,
                             "svn_fs_deltify: root is not a revision root");


  args.fs = svn_fs_root_fs (root);
  args.root = root;
  args.recursive = recursive;
  args.path = path;

  /* Get the ID of the target, which is the node we're changing. */
  SVN_ERR (svn_fs_node_id (&(args.id), root, path, pool));

  SVN_ERR (svn_fs__retry_txn (args.fs, txn_body_deltify, &args, pool));
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

  args.fs = svn_fs_root_fs (root);
  args.root = root;
  args.recursive = recursive;
  args.path = path;

  /* Get the ID of the target, which is the node we're changing. */
  SVN_ERR (svn_fs_node_id (&(args.id), root, path, pool));

  SVN_ERR (svn_fs__retry_txn (args.fs, txn_body_undeltify, &args, pool));
  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
