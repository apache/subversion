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


/* Stable nodes and deltification.  */

/* In FS, change TARGET's representation to be a delta against SOURCE,
   as part of TRAIL.  If TARGET or SOURCE does not exist, do nothing
   and return success.  */
static svn_error_t *
deltify (svn_fs_id_t *target_id,
         svn_fs_id_t *source_id,
         svn_fs_t *fs,
         int props_only,
         trail_t *trail)
{
  svn_fs__node_revision_t *source_nr, *target_nr;

  /* Turn those IDs into skels, so we can get the rep keys. */
  SVN_ERR (svn_fs__get_node_revision (&target_nr, fs, target_id, trail));
  SVN_ERR (svn_fs__get_node_revision (&source_nr, fs, source_id, trail));

  /* Check that target and source exist.  It is not an error to
     attempt to deltify something that does not exist, or deltify
     against a non-existent base.  However, nothing happens. */
  if ((target_nr == NULL) || (source_nr == NULL))
    return SVN_NO_ERROR;

  /* If there are props to deltify, deltify them. */
  if ((target_nr->prop_key && source_nr->prop_key)
      && (strcmp (target_nr->prop_key, source_nr->prop_key)))
    SVN_ERR (svn_fs__rep_deltify (fs, target_nr->prop_key, 
                                  source_nr->prop_key, trail));

  /* If there are contents to deltify (and we're not only looking at
     props), deltify them. */
  if ((target_nr->data_key && source_nr->data_key) && (! props_only)
      && (strcmp (target_nr->data_key, source_nr->data_key)))     
   SVN_ERR (svn_fs__rep_deltify (fs, target_nr->data_key, 
                                 source_nr->data_key, trail));

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



/* Deltify TARGET_ID in FS against its immediately successor if that
   successor exists in FS and is immutable.  Pass IS_DIR through to
   deltify(), and do all of this stuff as part of TRAIL.  */
static svn_error_t *
deltify_by_id (svn_fs_t *fs,
               svn_fs_id_t *target_id,
               int is_dir,
               trail_t *trail)
{
  svn_fs_id_t *source_id = NULL, *tmp_id;
  apr_size_t len = svn_fs__id_length (target_id);
  dag_node_t *node;
  int is_mutable = 0;

  /* Increment TMP_ID as a regular successor of TARGET_ID, and see if
     it exists in FS. */
  tmp_id = svn_fs__id_copy (target_id, trail->pool);
  tmp_id->digits[len - 1]++;
  if (SVN_NO_ERROR == svn_fs__dag_get_node (&node, fs, tmp_id, trail))
    {
      /* If TMP_ID is *immutable*, we'll use that our deltification
         baseline. */
      SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, node, trail));
      if (! is_mutable)
        source_id = tmp_id;
    }
  else
    {
      /* If that doesn't exist, we'll branch TARGET_ID, and see if
         that exists.  */
      tmp_id = apr_palloc (trail->pool, sizeof (*tmp_id));
      tmp_id->digits = apr_pmemdup (trail->pool, target_id->digits, 
                                    (len + 3) * sizeof (target_id->digits[0]));
      tmp_id->digits[len] = 1;
      tmp_id->digits[len + 1] = 1;
      tmp_id->digits[len + 2] = -1;
      
      if (SVN_NO_ERROR == svn_fs__dag_get_node (&node, fs, tmp_id, trail))
        {
          /* If TMP_ID is *immutable*, we'll use that our deltification
             baseline. */
          SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, node, trail));
          if (! is_mutable)
            source_id = tmp_id;
        }
    }

  /* If we found a valid source ID, perform the deltification step. */
  if (source_id)
    SVN_ERR (deltify (target_id, source_id, fs, is_dir, trail));

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
              const void *key;
              void *val;
              apr_ssize_t klen;
              svn_fs_dirent_t *entry;
              
              /* KEY will be the entry name in source, VAL the dirent */
              apr_hash_this (hi, &key, &klen, &val);
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
      SVN_ERR (deltify_by_id (fs, id, is_dir, trail));
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



svn_error_t *
svn_fs__stable_node (svn_fs_t *fs,
                     svn_fs_id_t *id,
                     trail_t *trail)
{
  svn_fs_id_t *predecessor_id = svn_fs__id_predecessor (id, trail->pool);
  int is_dir = 0;
  dag_node_t *node;

  SVN_ERR (svn_fs__dag_get_node (&node, fs, id, trail));
  is_dir = svn_fs__dag_is_directory (node);

  if (predecessor_id->digits[0] != -1)
    SVN_ERR (deltify (predecessor_id, id, fs, is_dir ? 1 : 0, trail));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
