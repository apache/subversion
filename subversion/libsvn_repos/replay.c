/*
 * replay.c:   an editor driver for changes made in a given revision
 *             or transaction
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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


#include <assert.h>
#include <apr_hash.h>
#include <apr_md5.h>

#include "svn_types.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_md5.h"
#include "svn_path.h"
#include "svn_repos.h"
#include "svn_pools.h"


/*** Backstory ***/

/* The year was 2003.  Subversion usage was rampant in the world, and
   there was a rapidly growing issues database to prove it.  To make
   matters worse, svn_repos_dir_delta() had simply outgrown itself.
   No longer content to simply describe the differences between two
   trees, the function had been slowly bearing the added
   responsibility of representing the actions that had been taken to
   cause those differences -- a burden it was never meant to bear.
   Now grown into a twisted mess of razor-sharp metal and glass, and
   trembling with a sort of momentarily stayed spring force,
   svn_repos_dir_delta was a timebomb poised for total annihilation of
   the American Midwest.
   
   Subversion needed a change.

   Changes, in fact.  And not just in the literary segue sense.  What
   Subversion desperately needed was a new mechanism solely
   responsible for replaying repository actions back to some
   interested party -- to translate and retransmit the contents of the
   Berkeley 'changes' database file. */

/*** Overview ***/

/* The filesystem keeps a record of high-level actions that affect the
   files and directories in itself.  The 'changes' table records
   additions, deletions, textual and property modifications, and so
   on.  The goal of the functions in this file is to examine those
   change records, and use them to drive an editor interface in such a
   way as to effectively replay those actions.

   This is critically different than what svn_repos_dir_delta() was
   designed to do.  That function describes, in the simplest way it
   can, how to transform one tree into another.  It doesn't care
   whether or not this was the same way a user might have done this
   transformation.  More to the point, it doesn't care if this is how
   those differences *did* come into being.  And it is for this reason
   that it cannot be relied upon for tasks such as the repository
   dumpfile-generation code, which is supposed to represent not
   changes, but actions that cause changes.

   So, what's the plan here?

   First, we fetch the changes for a particular revision or
   transaction.  We get these as an array, sorted chronologically.
   From this array we will build a hash, keyed on the path associated
   with each change item, and whose values are arrays of changes made
   to that path, again preserving the chronological ordering.

   Once our hash it built, we then sort all the keys of the hash (the
   paths) using a depth-first directory sort routine.

   Finally, we drive an editor, moving down our list of sorted paths,
   and manufacturing any intermediate editor calls (directory openings
   and closures) needed to navigate between each successive path.  For
   each path, we replay the sorted actions that occured at that path.

   We we've finished the editor drive, we should have fully replayed
   the filesystem events that occured in that revision or transactions
   (though not necessarily in the same order in which they
   occured). */
   


/*** Helper functions. ***/


static svn_revnum_t 
get_path_revision (svn_fs_root_t *root,
                   const char *path,
                   apr_pool_t *pool)
{
  svn_revnum_t revision;
  svn_error_t *err;

  /* Easy out -- if ROOT is a revision root, we can use the revision
     that it's a root of. */
  if (svn_fs_is_revision_root (root))
    return svn_fs_root_revision (root);

  /* Else, this must be a transaction root, so ask the filesystem in
     what revision this path was created. */
  if ((err = svn_fs_node_created_rev (&revision, root, path, pool)))
    {
      revision = SVN_INVALID_REVNUM;
      svn_error_clear (err);
    }

  /* If we don't get back a valid revision, this path is mutable in
     the transaction.  We should probably examing the node on which it
     is based, doable by querying for the node-id of the path, and
     thing examining that node-id's predecessor.  ### this predecessor
     determination isn't exposed via the FS public API right now, so
     for now, we'll just return the SVN_INVALID_REVNUM. */
  return revision;
}


struct path_driver_cb_baton
{
  const svn_delta_editor_t *editor;
  void *edit_baton;
  svn_fs_root_t *root;
  apr_hash_t *changed_paths;
};


static svn_error_t *
path_driver_cb_func (void **dir_baton,
                     void *parent_baton,
                     void *callback_baton,
                     const char *path,
                     apr_pool_t *pool)
{
  struct path_driver_cb_baton *cb = callback_baton;
  const svn_delta_editor_t *editor = cb->editor;
  void *edit_baton = cb->edit_baton;
  svn_fs_root_t *root = cb->root;
  svn_fs_path_change_t *change;
  svn_boolean_t do_add = FALSE, do_delete = FALSE;
  svn_node_kind_t kind;
  void *file_baton = NULL;

  *dir_baton = NULL;
  change = apr_hash_get (cb->changed_paths, path, APR_HASH_KEY_STRING);
  switch (change->change_kind)
    {
    case svn_fs_path_change_add:
      do_add = TRUE;
      break;

    case svn_fs_path_change_delete:
      do_delete = TRUE;
      break;

    case svn_fs_path_change_replace:
      do_add = TRUE;
      do_delete = TRUE;
      break;

    case svn_fs_path_change_modify:
    default:
      /* do nothing */
      break;
    }

  /* Handle any deletions. */
  if (do_delete)
    {
      svn_revnum_t rev = get_path_revision (root, path, pool);
      SVN_ERR (editor->delete_entry (path, rev, parent_baton, pool));
    }

  /* Fetch the node kind if it makes sense to do so. */
  if (! do_delete || do_add)
    {
      SVN_ERR (svn_fs_check_path (&kind, root, path, pool));
      if ((kind != svn_node_dir) && (kind != svn_node_file))
        return svn_error_createf 
          (SVN_ERR_FS_NOT_FOUND, NULL, 
           "Filesystem path `%s' is neither a file nor a directory", path);
    }

  /* Handle any adds/opens. */
  if (do_add)
    {
      const char *copyfrom_path;
      svn_revnum_t copyfrom_rev;

      /* Was this node copied? */
      SVN_ERR (svn_fs_copied_from (&copyfrom_rev, &copyfrom_path,
                                   root, path, pool));

      /* Do the right thing based on the path KIND. */
      if (kind == svn_node_dir)
        {
          SVN_ERR (editor->add_directory (path, parent_baton, 
                                          copyfrom_path, copyfrom_rev, 
                                          pool, dir_baton));
        }
      else
        {
          SVN_ERR (editor->add_file (path, parent_baton, 
                                     copyfrom_path, copyfrom_rev, 
                                     pool, &file_baton));
        }
    }
  else if (! do_delete)
    {
      svn_revnum_t rev = get_path_revision (root, path, pool);

      /* Do the right thing based on the path KIND (and the presence
         of a PARENT_BATON). */
      if (kind == svn_node_dir)
        {
          if (parent_baton)
            {
              SVN_ERR (editor->open_directory (path, parent_baton, rev,
                                               pool, dir_baton));
            }
          else
            {
              SVN_ERR (editor->open_root (edit_baton, rev, pool, dir_baton));
            }
        }
      else
        {
          SVN_ERR (editor->open_file (path, parent_baton, rev,
                                      pool, &file_baton));
        }
    }

  /* Handle property modifications (by sending a dummy notification). */
  if (! do_delete || do_add)
    {
      if (change->prop_mod)
        {
          if (kind == svn_node_dir)
            SVN_ERR (editor->change_dir_prop (*dir_baton, "", NULL, pool));
          else if (kind == svn_node_file)
            SVN_ERR (editor->change_file_prop (file_baton, "", NULL, pool));
        }

      /* Handle textual modifications (by sending a single NULL delta
         window). */
      if (change->text_mod)
        {
          svn_txdelta_window_handler_t delta_handler;
          void *delta_handler_baton;
          SVN_ERR (editor->apply_textdelta (file_baton, NULL, pool, 
                                            &delta_handler, 
                                            &delta_handler_baton));
          if (delta_handler)
            SVN_ERR (delta_handler (NULL, delta_handler_baton));
        }
    }

  /* Close the file baton if we opened it. */
  if (file_baton)
    SVN_ERR (editor->close_file (file_baton, NULL, pool));

  return SVN_NO_ERROR;
}




svn_error_t *
svn_repos_replay (svn_fs_root_t *root,
                  const svn_delta_editor_t *editor,
                  void *edit_baton,
                  apr_pool_t *pool)
{
  apr_hash_t *fs_changes;
  apr_hash_t *changed_paths;
  apr_hash_index_t *hi;
  apr_array_header_t *paths;
  struct path_driver_cb_baton cb_baton;
  svn_revnum_t revision;

  /* Fetch the paths changed under ROOT. */
  SVN_ERR (svn_fs_paths_changed (&fs_changes, root, pool));

  /* Make an array from the keys of our CHANGED_PATHS hash, and copy
     the values into a new hash whose keys have no leading slashes. */
  paths = apr_array_make (pool, apr_hash_count (fs_changes),
                          sizeof (const char *));
  changed_paths = apr_hash_make (pool);
  for (hi = apr_hash_first (pool, fs_changes); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      apr_size_t keylen;
      const char *path;
      svn_fs_path_change_t *change;

      apr_hash_this (hi, &key, &keylen, &val);
      path = key;
      change = val;
      if (path[0] == '/')
        {
          path++;
          keylen--;
        }
      APR_ARRAY_PUSH (paths, const char *) = path;
      apr_hash_set (changed_paths, path, keylen, change);
    }
  
  /* Initialize our callback baton. */
  cb_baton.editor = editor;
  cb_baton.edit_baton = edit_baton;
  cb_baton.root = root;
  cb_baton.changed_paths = changed_paths;
    
  /* Determine the revision to use throughout the edit, and call
     EDITOR's set_target_revision() function.  */
  revision = svn_fs_root_revision (root);
  SVN_ERR (editor->set_target_revision (edit_baton, revision, pool));
  if (svn_fs_is_txn_root (root))
    revision = SVN_INVALID_REVNUM;

  /* Call the path-based editor driver. */
  SVN_ERR (svn_delta_path_driver (editor, edit_baton, revision, paths, 
                                  path_driver_cb_func, &cb_baton, pool));
  
  return SVN_NO_ERROR;
}
