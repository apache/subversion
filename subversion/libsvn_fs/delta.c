/*
 * delta.c:   an editor driver for svn_fs_dir_delta
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


#include "svn_types.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "apr_hash.h"
#include "fs.h"




/* THINGS TODO:  Currently the code herein gives only a slight nod to
   fully supporting directory deltas that involve renames, copies, and
   such.  */
 

/* Some datatypes and declarations used throughout the file.  */


/* Parameters which remain constant throughout a delta traversal.
   At the top of the recursion, we initialize one of these structures.
   Then, we pass it down, unchanged, to every call.  This way,
   functions invoked deep in the recursion can get access to this
   traversal's global parameters, without using global variables.  */
struct context {
  const svn_delta_edit_fns_t *editor;
  svn_fs_root_t *source_root;
  apr_hash_t *source_rev_diffs;
  svn_fs_root_t *target_root;
  apr_pool_t *pool;
};


/* The type of a function that accepts changes to an object's property
   list.  OBJECT is the object whose properties are being changed.
   NAME is the name of the property to change.  VALUE is the new value
   for the property, or zero if the property should be deleted.  */
typedef svn_error_t *proplist_change_fn_t (struct context *c,
                                           void *object,
                                           svn_string_t *name,
                                           svn_string_t *value);



/* Some prototypes for functions used throughout.  See each individual
   function for information about what it does.  */


/* Retrieving the base revision from the path/revision hash.  */
static svn_revnum_t get_revision_from_hash (apr_hash_t *hash, 
                                            svn_string_t *path,
                                            apr_pool_t *pool);


/* proplist_change_fn_t property changing functions.  */
static svn_error_t *change_dir_prop (struct context *c, 
                                     void *object,
                                     svn_string_t *name, 
                                     svn_string_t *value);

static svn_error_t *change_file_prop (struct context *c, 
                                      void *object,
                                      svn_string_t *name, 
                                      svn_string_t *value);


/* Constructing deltas for properties of files and directories.  */
static svn_error_t *delta_proplists (struct context *c,
                                     svn_string_t *source_path,
                                     svn_string_t *target_path,
                                     proplist_change_fn_t *change_fn,
                                     void *object);


/* Constructing deltas for file constents.  */
static svn_error_t *send_text_delta (struct context *c,
                                     void *file_baton,
                                     svn_txdelta_stream_t *delta_stream);

static svn_error_t *delta_files (struct context *c, 
                                 void *file_baton,
                                 svn_string_t *source_path,
                                 svn_string_t *target_path);


/* Generic directory deltafication routines.  */
static svn_error_t *delete (struct context *c, 
                            void *dir_baton, 
                            svn_string_t *target_entry);

static svn_error_t *add_file_or_dir (struct context *c, 
                                     void *dir_baton, 
                                     svn_string_t *target_parent, 
                                     svn_string_t *target_entry);

static svn_error_t *replace_file_or_dir (struct context *c, 
                                         void *dir_baton,
                                         svn_string_t *target_parent,
                                         svn_string_t *target_entry,
                                         svn_string_t *source_parent, 
                                         svn_string_t *source_entry);

static svn_error_t *replace_with_nearest (struct context *c, 
                                          void *dir_baton,
                                          svn_string_t *source_parent, 
                                          svn_string_t *target_parent,
                                          svn_fs_dirent_t *t_entry);

static svn_error_t *delta_dirs (struct context *c, 
                                void *dir_baton,
                                svn_string_t *source_path, 
                                svn_string_t *target_path);



/* Public interface to computing directory deltas.  */

svn_error_t *
svn_fs_dir_delta (svn_fs_root_t *source_root,
                  const char *source_path,
                  apr_hash_t *source_rev_diffs,
                  svn_fs_root_t *target_root,
                  const char *target_path,
                  const svn_delta_edit_fns_t *editor,
                  void *edit_baton,
                  apr_pool_t *pool)
{
  void *root_baton;
  struct context c;
  svn_string_t *source_path_str;
  svn_string_t *target_path_str;

  if (! source_path)
    {
      return
        svn_error_create
        (SVN_ERR_FS_PATH_SYNTAX, 0, 0, pool,
         "directory delta source path is invalid");
    }
    
  if (! target_path)
    {
      return
        svn_error_create
        (SVN_ERR_FS_PATH_SYNTAX, 0, 0, pool,
         "directory delta target path is invalid");
    }

  {
    int is_dir;

    SVN_ERR (svn_fs_is_dir (&is_dir, source_root, source_path, pool));
    if (! is_dir)
      return
        svn_error_create
        (SVN_ERR_FS_NOT_DIRECTORY, 0, 0, pool,
         "directory delta source path is not a directory");

    SVN_ERR (svn_fs_is_dir (&is_dir, target_root, target_path, pool));
    if (! is_dir)
      return
        svn_error_create
        (SVN_ERR_FS_NOT_DIRECTORY, 0, 0, pool,
         "directory delta target path is not a directory");
  }      
    
  /* If our target here is a revision, call set_target_revision to set
     the global target revision for our edit.  Else, whine like a baby
     because we don't want to deal with txn root targets right now.  */
  if (svn_fs_is_revision_root (target_root))
    {
      SVN_ERR (editor->set_target_revision 
               (edit_baton, 
                svn_fs_revision_root_revision (target_root)));
    }
  else
    {
      return
        svn_error_create
        (SVN_ERR_FS_NOT_REVISION_ROOT, 0, 0, pool,
         "directory delta target not a revision root");
    }      

  source_path_str = svn_string_create (source_path, pool);
  target_path_str = svn_string_create (target_path, pool);

  /* Setup our pseudo-global structure here.  We need these variables
     throughout the deltafication process, so pass them around by
     reference to all the helper functions. */
  c.editor = editor;
  c.source_root = source_root;
  c.source_rev_diffs = source_rev_diffs;
  c.target_root = target_root;
  c.pool = pool;

  /* Call replace_root to get our root_baton... */
  SVN_ERR (editor->replace_root 
           (edit_baton, 
            get_revision_from_hash (source_rev_diffs,
                                    target_path_str,
                                    pool),
            &root_baton));

  /* ...and then begin the recursive directory deltafying process!  */
  SVN_ERR (delta_dirs (&c, root_baton, source_path_str, target_path_str));

  /* Make sure we close the root directory we opened above. */
  SVN_ERR (editor->close_directory (root_baton));

  /* All's well that ends well. */
  return SVN_NO_ERROR;
}



/* Public interface to computing file text deltas.  */

svn_error_t *
svn_fs_file_delta (svn_txdelta_stream_t **stream_p,
                   svn_fs_root_t *source_root,
                   const char *source_path,
                   svn_fs_root_t *target_root,
                   const char *target_path,
                   apr_pool_t *pool)
{
  svn_stream_t *source, *target;
  svn_txdelta_stream_t *delta_stream;

  /* Get read functions for the source file contents.  */
  if (source_root && source_path)
    SVN_ERR (svn_fs_file_contents (&source, source_root, source_path, pool));
  else
    source = svn_stream_empty (pool);

  /* Get read functions for the target file contents.  */
  SVN_ERR (svn_fs_file_contents (&target, target_root, target_path, pool));

  /* Create a delta stream that turns the ancestor into the target.  */
  svn_txdelta (&delta_stream, source, target, pool);

  *stream_p = delta_stream;
  return SVN_NO_ERROR;
}




/* Retrieving the base revision from the path/revision hash.  */


/* Look through a HASH (with paths as keys, and pointers to revision
   numbers as values) for the revision associated with the given PATH.
   Perform all necessary memory allocations in POOL.  */
static svn_revnum_t
get_revision_from_hash (apr_hash_t *hash, svn_string_t *path,
                        apr_pool_t *pool)
{
  void *val;
  svn_string_t *path_copy;
  svn_revnum_t revision = SVN_INVALID_REVNUM;

  if (! hash)
    return SVN_INVALID_REVNUM;

  /* See if this path has a revision assigned in the hash. */
  val = apr_hash_get (hash, path->data, APR_HASH_KEY_STRING);
  if (val)
    {
      revision = *((svn_revnum_t *) val);
      if (SVN_IS_VALID_REVNUM(revision))
        return revision;      
    }

  /* Make a copy of our path that we can hack on. */
  path_copy = svn_string_dup (path, pool);

  /* If we haven't found a valid revision yet, and our copy of the
     path isn't empty, hack the last component off the path and see if
     *that* has a revision entry in our hash. */
  while ((! SVN_IS_VALID_REVNUM(revision)) 
         && (! svn_path_is_empty (path_copy, svn_path_repos_style)))
    {
      svn_path_remove_component (path_copy, svn_path_repos_style);

      val = apr_hash_get (hash, path_copy->data, APR_HASH_KEY_STRING);
      if (val)
        revision = *((svn_revnum_t *) val);
    }
  
  return revision;
}




/* proplist_change_fn_t property changing functions.  */


/* Call the directory property-setting function of C->editor to set
   the property NAME to given VALUE on the OBJECT passed to this
   function. */
static svn_error_t *
change_dir_prop (struct context *c, void *object,
                 svn_string_t *name, svn_string_t *value)
{
  return c->editor->change_dir_prop (object, name, value);
}


/* Call the file property-setting function of C->editor to set the
   property NAME to given VALUE on the OBJECT passed to this
   function. */
static svn_error_t *
change_file_prop (struct context *c, void *object,
                  svn_string_t *name, svn_string_t *value)
{
  return c->editor->change_file_prop (object, name, value);
}




/* Constructing deltas for properties of files and directories.  */


/* Generate the appropriate property editing calls to turn the
   properties of SOURCE_PATH into those of TARGET_PATH.  If
   SOURCE_PATH is NULL, treat it as if it were a file with no
   properties.  Pass OBJECT on to the editor function wrapper
   CHANGE_FN. */
static svn_error_t *
delta_proplists (struct context *c,
                 svn_string_t *source_path,
                 svn_string_t *target_path,
                 proplist_change_fn_t *change_fn,
                 void *object)
{
  apr_hash_t *s_props = 0;
  apr_hash_t *t_props = 0;
  apr_hash_index_t *hi;

  /* Get the source file's properties */
  if (source_path)
    SVN_ERR (svn_fs_node_proplist 
             (&s_props, c->source_root, source_path->data,
              c->pool));

  /* Get the target file's properties */
  if (target_path)
    SVN_ERR (svn_fs_node_proplist 
             (&t_props, c->target_root, target_path->data,
              c->pool));

  for (hi = apr_hash_first (t_props); hi; hi = apr_hash_next (hi))
    {
      svn_string_t *s_value, *t_value, *t_name;
      const void *key;
      void *val;
      apr_size_t klen;
          
      /* KEY is property name in target, VAL the value */
      apr_hash_this (hi, &key, &klen, &val);
      t_name = svn_string_ncreate (key, klen, c->pool);
      t_value = val;

      /* See if this property existed in the source.  If so, and if
         the values in source and target differ, replace the value in
         target with the one in source. */
      if (s_props 
          && ((s_value = apr_hash_get (s_props, key, klen)) != 0))
        {
          if (svn_string_compare (s_value, t_value))
            SVN_ERR (change_fn (c, object, t_name, t_value));

          /* Remove the property from source list so we can track
             which items have matches in the target list. */
          apr_hash_set (s_props, key, klen, NULL);
        }
      else
        {
          /* This property didn't exist in the source, so this is just
             and add. */
          SVN_ERR (change_fn (c, object, t_name, t_value));
        }
    }

  /* All the properties remaining in the source list are not present
     in the target, and so must be deleted. */
  if (s_props)
    {
      for (hi = apr_hash_first (s_props); hi; hi = apr_hash_next (hi))
        {
          svn_string_t *s_value, *s_name;
          const void *key;
          void *val;
          apr_size_t klen;
          
          /* KEY is property name in target, VAL the value */
          apr_hash_this (hi, &key, &klen, &val);
          s_name = svn_string_ncreate (key, klen, c->pool);
          s_value = val;

          SVN_ERR (change_fn (c, object, s_name, s_value));
        }
    }

  return SVN_NO_ERROR;
}




/* Constructing deltas for file constents.  */


/* Change the contents of FILE_BATON in C->editor, according to the
   text delta from DELTA_STREAM.  */
static svn_error_t *
send_text_delta (struct context *c,
                 void *file_baton,
                 svn_txdelta_stream_t *delta_stream)
{
  svn_txdelta_window_handler_t delta_handler;
  svn_txdelta_window_t *window;
  void *delta_handler_baton;

  /* Get a handler that will apply the delta to the file.  */
  SVN_ERR (c->editor->apply_textdelta 
           (file_baton, &delta_handler, &delta_handler_baton));

  /* Read windows from the delta stream, and apply them to the file.  */
  do
    {
      SVN_ERR (svn_txdelta_next_window (&window, delta_stream));
      SVN_ERR (delta_handler (window, delta_handler_baton));
      if (window)
        svn_txdelta_free_window (window);
    }
  while (window);

  return SVN_NO_ERROR;
}


/* Make the appropriate edits on FILE_BATON to change its contents and
   properties from those in SOURCE_PATH to those in TARGET_PATH. */
static svn_error_t *
delta_files (struct context *c, void *file_baton,
             svn_string_t *source_path,
             svn_string_t *target_path)
{
  svn_txdelta_stream_t *delta_stream;

  /* Compare the files' property lists.  */
  SVN_ERR (delta_proplists (c, source_path, target_path,
                            change_file_prop, file_baton));

  if (source_path)
    {
      /* Get a delta stream turning SOURCE_PATH's contents into
         TARGET_PATH's contents.  */
      SVN_ERR (svn_fs_file_delta (&delta_stream, 
                                  c->source_root, source_path->data,
                                  c->target_root, target_path->data,
                                  c->pool));
    }
  else
    {
      /* Get a delta stream turning an empty file into one having
         TARGET_PATH's contents.  */
      SVN_ERR (svn_fs_file_delta (&delta_stream, 
                                  0, 0,
                                  c->target_root, target_path->data,
                                  c->pool));
    }

  SVN_ERR (send_text_delta (c, file_baton, delta_stream));

  svn_txdelta_free (delta_stream);

  return 0;
}




/* Generic directory deltafication routines.  */


/* Emit a delta to delete the entry named TARGET_ENTRY from DIR_BATON.  */
static svn_error_t *
delete (struct context *c, void *dir_baton, svn_string_t *target_entry)
{
  return c->editor->delete_entry (target_entry, dir_baton);
}


/* Emit a delta to create the entry named TARGET_ENTRY in the directory
   TARGET_PARENT.  Pass DIR_BATON through to editor functions that
   require it.  */
static svn_error_t *
add_file_or_dir (struct context *c, void *dir_baton,
                 svn_string_t *target_parent,
                 svn_string_t *target_entry)
{
  int is_dir;
  svn_string_t *target_full_path = 0;

  if (!target_parent || !target_entry)
    abort();

  /* Get the target's full path */
  target_full_path = svn_string_dup (target_parent, c->pool);
  svn_path_add_component 
    (target_full_path, target_entry, svn_path_repos_style);

  /* Is the target a file or a directory?  */
  SVN_ERR (svn_fs_is_dir (&is_dir, c->target_root, 
                          target_full_path->data, c->pool));    

  if (is_dir)
    {
      void *subdir_baton;

      SVN_ERR (c->editor->add_directory 
               (target_entry, dir_baton, 
                NULL, SVN_INVALID_REVNUM, &subdir_baton));
      SVN_ERR (delta_dirs (c, subdir_baton, 0, target_full_path));
      SVN_ERR (c->editor->close_directory (subdir_baton));
    }
  else
    {
      void *file_baton;

      SVN_ERR (c->editor->add_file 
               (target_entry, dir_baton, 
                NULL, SVN_INVALID_REVNUM, &file_baton));
      SVN_ERR (delta_files (c, file_baton, 0, target_full_path));
      SVN_ERR (c->editor->close_file (file_baton));
    }

  return SVN_NO_ERROR;
}


/* Modify the directory TARGET_PARENT by replacing its entry named
   TARGET_ENTRY with the SOURCE_ENTRY found in SOURCE_PARENT.  Pass
   DIR_BATON through to editor functions that require it. */
static svn_error_t *
replace_file_or_dir (struct context *c, void *dir_baton,
                     svn_string_t *target_parent, svn_string_t *target_entry,
                     svn_string_t *source_parent, svn_string_t *source_entry)
{
  int is_dir;
  svn_string_t *source_full_path = 0;
  svn_string_t *target_full_path = 0;
  svn_revnum_t base_revision = SVN_INVALID_REVNUM;

  if (!target_parent || !target_entry)
    abort();

  if (!source_parent || !source_entry)
    abort();

  /* Get the target's full path */
  target_full_path = svn_string_dup (target_parent, c->pool);
  svn_path_add_component 
    (target_full_path, target_entry, svn_path_repos_style);

  /* Is the target a file or a directory?  */
  SVN_ERR (svn_fs_is_dir (&is_dir, c->target_root, 
                          target_full_path->data, c->pool));

  /* Get the source's full path */
  source_full_path = svn_string_dup (source_parent, c->pool);
  svn_path_add_component 
    (source_full_path, source_entry, svn_path_repos_style);

  /* Get the base revision for the entry from the hash. */
  base_revision = get_revision_from_hash (c->source_rev_diffs,
                                          source_full_path,
                                          c->pool);

  if (! SVN_IS_VALID_REVNUM(base_revision))
    return
      svn_error_create
      (SVN_ERR_FS_NO_SUCH_REVISION, 0, 0, c->pool,
       "unable to ascertain base revision for source path");

  if (is_dir)
    {
      void *subdir_baton;

      SVN_ERR (c->editor->replace_directory 
               (target_entry, dir_baton, base_revision, &subdir_baton));
      SVN_ERR (delta_dirs 
               (c, subdir_baton, source_full_path, target_full_path));
      SVN_ERR (c->editor->close_directory (subdir_baton));
    }
  else
    {
      void *file_baton;

      SVN_ERR (c->editor->replace_file 
               (target_entry, dir_baton, base_revision, &file_baton));
      SVN_ERR (delta_files 
               (c, file_baton, source_full_path, target_full_path));
      SVN_ERR (c->editor->close_file (file_baton));
    }

  return SVN_NO_ERROR;
}


/* Do a `replace' edit in DIR_BATON, replacing the entry named
   T_ENTRY->name in the directory TARGET_PARENT with the closest
   related node available in SOURCE_PARENT.  If no relative can be
   found, simply delete in the entry from TARGET_PARENT, and then
   re-add the new one. */
static svn_error_t *
replace_with_nearest (struct context *c, void *dir_baton,
                      svn_string_t *source_parent, 
                      svn_string_t *target_parent,
                      svn_fs_dirent_t *t_entry)
{
  apr_hash_t *s_entries;
  apr_hash_index_t *hi;
  int best_distance = -1;
  svn_fs_dirent_t *best_entry = NULL;
  svn_string_t *source_full_path = svn_string_create ("", c->pool);
  svn_string_t *target_full_path;
  svn_string_t *target_entry;
  int t_is_dir;

  /* Get the list of entries in source.  */
  SVN_ERR (svn_fs_dir_entries (&s_entries, c->source_root,
                               source_parent->data, c->pool));

  target_full_path = svn_string_dup (target_parent, c->pool);
  target_entry = svn_string_create (t_entry->name, c->pool);
  svn_path_add_component (target_full_path, target_entry,
                          svn_path_repos_style);

  /* Is the target a file or a directory?  */
  SVN_ERR (svn_fs_is_dir (&t_is_dir, c->target_root, 
                          target_full_path->data, c->pool));

  /* Find the closest relative to TARGET_ENTRY in SOURCE.
     
     In principle, a replace operation can choose the ancestor from
     anywhere in the delta's whole source tree.  In this
     implementation, we only search SOURCE for possible ancestors.
     This will need to improve, so we can find the best ancestor, no
     matter where it's hidden away in the source tree.  */
  for (hi = apr_hash_first (s_entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      apr_size_t klen;
      int distance;
      svn_fs_dirent_t *s_entry;
      int s_is_dir;
     
      /* KEY will be the entry name in source, VAL the dirent */
      apr_hash_this (hi, &key, &klen, &val);
      s_entry = val;

      svn_string_set (source_full_path, source_parent->data);
      svn_path_add_component (source_full_path,
                              svn_string_create (s_entry->name, c->pool),
                              svn_path_repos_style);

      /* Is this entry a file or a directory?  */
      SVN_ERR (svn_fs_is_dir (&s_is_dir, c->source_root, 
                              source_full_path->data, c->pool));

      /* If we aren't looking at the same node type, skip this
         entry. */
      if ((s_is_dir && (! t_is_dir)) || ((! s_is_dir) && t_is_dir))
        continue;

      /* Find the distance between the target entry and this source
         entry.  This returns -1 if they're completely unrelated.
         Here we're using ID distance as an approximation for delta
         size.  */
      distance = svn_fs_id_distance (t_entry->id, s_entry->id);

      /* If these nodes are completely unrelated, move along. */
      if (distance == -1)
        continue;

      /* If this is the first related node we've found, or just a
         closer node than previously discovered, update our
         best_distance tracker. */
      if ((best_distance == -1) || (distance < best_distance))
        {
          best_distance = distance;
          best_entry = s_entry;
        }
    }

  /* If our best_distance is still an invalid distance, replace this
     from scratch (meaning, delete the old and add the new).  Else,
     replace it relative to the ancestor we found. */
  if (best_distance == -1)
    {
      SVN_ERR (delete (c, dir_baton, target_entry)); 
      SVN_ERR (add_file_or_dir (c, dir_baton, target_parent, target_entry));
    }
  else
    SVN_ERR (replace_file_or_dir 
             (c, dir_baton,
              target_parent,
              target_entry,
              source_parent, 
              svn_string_create (best_entry->name, c->pool)));

  return SVN_NO_ERROR;
}


/* Emit deltas to turn SOURCE_PATH into TARGET_PATH.  Assume that
   DIR_BATON represents the directory we're constructing to the editor
   in the context C.  */
static svn_error_t *
delta_dirs (struct context *c, void *dir_baton,
            svn_string_t *source_path, svn_string_t *target_path)
{
  apr_hash_t *s_entries = 0, *t_entries = 0;
  apr_hash_index_t *hi;

  /* Compare the property lists.  */
  SVN_ERR (delta_proplists (c, source_path, target_path,
                            dir_baton, change_dir_prop));

  /* Get the list of entries in each of source and target.  */
  if (target_path)
    {
      SVN_ERR (svn_fs_dir_entries (&t_entries, c->target_root,
                                   target_path->data, c->pool));
    }
  else
    {
      /* Return a viscious error. */
      abort();
    }

  if (source_path)
    {
      SVN_ERR (svn_fs_dir_entries (&s_entries, c->source_root,
                                   source_path->data, c->pool));
    }

  /* Loop over the hash of entries in the target, searching for its
     partner in the source.  If we find the matching partner entry,
     use editor calls to replace the one in target with a new version
     if necessary, then remove that entry from the source entries
     hash.  If we can't find a related node in the source, we use
     editor calls to add the entry as a new item in the target.
     Having handled all the entries that exist in target, any entries
     still remaining the source entries hash represent entries that no
     longer exist in target.  Use editor calls to delete those entries
     from the target tree. */
  for (hi = apr_hash_first (t_entries); hi; hi = apr_hash_next (hi))
    {
      svn_fs_dirent_t *s_entry, *t_entry;
      const void *key;
      void *val;
      apr_size_t klen;
          
      /* KEY is the entry name in target, VAL the dirent */
      apr_hash_this (hi, &key, &klen, &val);
      t_entry = val;
          
      /* Can we find something with the same name in the source
         entries hash? */
      if (s_entries 
          && ((s_entry = apr_hash_get (s_entries, key, klen)) != 0))
        {
          int distance;

          /* Check the distance between the ids.  

             0 means they are the same id, and this is a noop.

             -1 means they are unrelated, so delete this entry and add
             a new one of the same name.  In the future, we should try
             to find a relative somewhere else in the directory, or
             perhaps elsewhere in the tree.  This will theoretically
             reduce the size of the deltas.

             Any other positive value means the nodes are related
             through ancestry, so go ahead and do the replace
             directly.  */
          distance = svn_fs_id_distance (s_entry->id, t_entry->id);
          if (distance == 0)
            {
              /* no-op */
            }
          else if (distance == -1)
            {
              SVN_ERR (replace_with_nearest (c, dir_baton, source_path, 
                                             target_path, t_entry));
            }
          else
            {
              SVN_ERR (replace_file_or_dir
                       (c, dir_baton, 
                        target_path,
                        svn_string_create (t_entry->name, c->pool),
                        source_path,
                        svn_string_create (s_entry->name, c->pool)));
            }

          /*  Remove the entry from the source_hash. */
          apr_hash_set (s_entries, key, APR_HASH_KEY_STRING, NULL);
        }
      else
        {
          /* We didn't find an entry with this name in the source
             entries hash.  This must be something new that needs to
             be added.  */
          SVN_ERR (add_file_or_dir 
                   (c, dir_baton, target_path, 
                    svn_string_create (t_entry->name, c->pool)));
        }
    }

  /* All that is left in the source entries hash are things that need
     to be deleted.  Delete them.  */
  if (s_entries)
    {
      for (hi = apr_hash_first (s_entries); hi; hi = apr_hash_next (hi))
        {
          svn_fs_dirent_t *s_entry;
          const void *key;
          void *val;
          apr_size_t klen;
          
          /* KEY is the entry name in source, VAL the dirent */
          apr_hash_this (hi, &key, &klen, &val);
          s_entry = val;
          
          SVN_ERR (delete (c, dir_baton, 
                           svn_string_create (s_entry->name, c->pool)));
        }
    }

  return SVN_NO_ERROR;
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
