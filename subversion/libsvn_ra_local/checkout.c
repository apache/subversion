/*
 * checkout.c : read a repository tree and drive a checkout editor.
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include "ra_local.h"
#include <assert.h>


/* Helper to read data out of a file at ROOT:PATH and push it to
   EDITOR via FILE_BATON.

   ben sez: whoa.  The elegance and level of abstraction going on here
   is amazing.  What an amazing design.  It's like a set of opaque
   legos that all perfectly fit together. :) */
static svn_error_t *
send_file_contents (svn_fs_root_t *root,
                    svn_string_t *path,
                    void *file_baton,
                    const svn_delta_edit_fns_t *editor,
                    apr_pool_t *pool)
{
  svn_stream_t *contents;
  svn_txdelta_window_handler_t *handler;
  void *handler_baton;
  svn_txdelta_stream_t *delta_stream;
  svn_txdelta_window_t *window;

  /* Get a readable stream of the file's contents. */
  SVN_ERR (svn_fs_file_contents (&contents, root, path->data, pool));  

  /* Create a delta stream which converts an *empty* bytestream into the
     file's contents bytestream. */
  svn_txdelta (&delta_stream, svn_stream_empty (pool), contents, pool);

  /* Get an editor func that wants to consume the delta stream. */
  SVN_ERR (editor->apply_textdelta (file_baton, &handler, &handler_baton));

  /* Pull windows from the delta stream and feed to the consumer. */
  do 
    {
      SVN_ERR (svn_txdelta_next_window (&window, delta_stream));
      SVN_ERR ((*handler) (window, handler_baton));

    } while (window);

  return SVN_NO_ERROR;
}



/* Helper to push any properties attached to ROOT:PATH at EDITOR,
   using OBJECT_BATON.  IS_DIR indicates which editor func to call. */
static svn_error_t *
set_any_props (svn_fs_root_t *root,
               svn_string_t *path,
               void *object_baton,
               const svn_delta_edit_fns_t *editor,
               int is_dir,
               apr_pool_t *pool)
{
  apr_hash_t *props;
  apr_hash_index_t *hi;

  SVN_ERR (svn_fs_node_proplist (&props, root, path->data, pool));

  for (hi = apr_hash_first (props); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      apr_size_t klen;
      svn_string_t *name, *value;

      apr_hash_this (hi, &key, &klen, &val);
      value = (svn_string_t *) val;
      name = svn_string_ncreate (key, klen, pool);
      
      if (is_dir)
        SVN_ERR (editor->change_dir_prop (object_baton, name, value));
      else
        SVN_ERR (editor->change_file_prop (object_baton, name, value));  
    }

  return SVN_NO_ERROR;
}



/* A depth-first recursive walk of DIR_PATH under a fs ROOT that adds
   dirs and files via EDITOR and DIR_BATON.   URL represents the
   current repos location, and is stored in DIR_BATON's working copy.

   Note: we're conspicuously creating a subpool in POOL and freeing it
   at each level of subdir recursion; this is a safety measure that
   protects us when checking out outrageously large or deep trees.

   Note: we aren't driving EDITOR with "postfix" text deltas; that
   style only exists to recognize skeletal conflicts as early as
   possible (during a commit).  There are no conflicts in a checkout,
   however.  :) */
static svn_error_t *
walk_tree (svn_fs_root_t *root,
           svn_string_t *dir_path,
           void *dir_baton,
           const svn_delta_edit_fns_t *editor, 
           void *edit_baton,
           svn_string_t *URL,
           apr_pool_t *pool)
{
  apr_hash_t *dirents;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create (pool);

  SVN_ERR (svn_fs_dir_entries (&dirents, root, dir_path->data, subpool));

  /* Loop over this directory's dirents: */
  for (hi = apr_hash_first (dirents); hi; hi = apr_hash_next (hi))
    {
      int is_dir, is_file;
      const void *key;
      void *val;
      apr_size_t klen;
      svn_fs_dirent_t *dirent;
      svn_string_t *dirent_name;
      svn_string_t *URL_path = svn_string_dup (URL, subpool);
      svn_string_t *dirent_path = svn_string_dup (dir_path, subpool);

      apr_hash_this (hi, &key, &klen, &val);
      dirent = (svn_fs_dirent_t *) val;
      dirent_name = svn_string_create (dirent->name, subpool);
      svn_path_add_component (dirent_path, dirent_name, svn_path_repos_style);
      svn_path_add_component (URL_path, dirent_name, svn_path_url_style);

      /* What is dirent? */
      SVN_ERR (svn_fs_is_dir (&is_dir, root, dirent_path->data, subpool));
      SVN_ERR (svn_fs_is_file (&is_file, root, dirent_path->data, subpool));

      if (is_dir)
        {
          void *new_dir_baton;

          SVN_ERR (editor->add_directory (dirent_name, dir_baton,
                                          URL_path, NULL, &new_dir_baton));
          SVN_ERR (set_any_props (root, dirent_path, new_dir_baton,
                                  editor, 1, subpool));
          /* Recurse */
          SVN_ERR (walk_tree (root, dirent_path, new_dir_baton,
                              editor, edit_baton, URL_path, subpool));
        }
        
      else if (is_file)
        {
          void *file_baton;

          SVN_ERR (editor->add_file (dirent_name, dir_baton,
                                     URL_path, NULL, &file_baton));          
          SVN_ERR (set_any_props (root, dirent_path, file_baton,
                                  editor, 0, subpool));
          SVN_ERR (send_file_contents (root, dirent_path, file_baton,
                                       editor, subpool));
          SVN_ERR (editor->close_file (file_baton));
        }

      else
        {
          /* It's not a file or dir.  What the heck?  Instead of
             returning an error, let's just ignore the thing. */ 
        }
    }

  /* Close the dir and remove the subpool we used at this level. */
  SVN_ERR (editor->close_directory (dir_baton));

  apr_pool_destroy (subpool);

  return SVN_NO_ERROR;
}



/* The main editor driver.  Short and elegant! */
svn_error_t *
svn_ra_local__checkout (svn_fs_t *fs, 
                        svn_revnum_t revnum, 
                        svn_string_t *URL,
                        svn_string_t *fs_path,
                        const svn_delta_edit_fns_t *editor, 
                        void *edit_baton,
                        apr_pool_t *pool)
{
  svn_fs_root_t *root;
  void *root_dir_baton;

  SVN_ERR (svn_fs_revision_root (&root, fs, revnum, pool));

  SVN_ERR (editor->set_target_revision (edit_baton, revnum));
  SVN_ERR (editor->replace_root (edit_baton, SVN_INVALID_REVNUM,
                                 &root_dir_baton));

  SVN_ERR (walk_tree (root, fs_path, root_dir_baton,
                      editor, edit_baton, URL, pool));

  SVN_ERR (editor->close_edit (edit_baton));

  return SVN_NO_ERROR;
}




/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */





