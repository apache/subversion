/*
 * delta.c:   an editor driver for svn_fs_dir_delta
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
static svn_error_t *delta_file_props (struct context *c,
                                      void *file_baton,
                                      svn_string_t *source_path,
                                      svn_string_t *target_path);

static svn_error_t *delta_dir_props (struct context *c,
                                     void *file_baton,
                                     svn_string_t *source_path,
                                     svn_string_t *target_path);

static svn_error_t *delta_proplists (struct context *c,
                                     apr_hash_t *s_props,
                                     apr_hash_t *t_props,
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
                            svn_string_t *target_name);

static svn_error_t *add_file_or_dir (struct context *c, 
                                     void *dir_baton, 
                                     svn_string_t *target_path,
                                     svn_string_t *target_name);

static svn_error_t *replace_file_or_dir (struct context *c, 
                                         void *dir_baton,
                                         svn_string_t *target_path, 
                                         svn_string_t *target_name,
                                         svn_string_t *source_path, 
                                         svn_string_t *source_name);

static svn_error_t *replace_with_nearest (struct context *c, 
                                          void *dir_baton,
                                          svn_string_t *source_path, 
                                          svn_string_t *target_path,
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

  /* If our target here is a revision, we'll call set_target_revision
     to set the global target revision for our edit.  Else, we'll
     whine like babies because we don't want to deal with txn root
     targets right now.  */
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
    
  source_path_str = svn_string_create (source_path, pool);
  target_path_str = svn_string_create (target_path, pool);

  /* Setup our pseudo-global structure here.  These variables are
     needed throughout the deltafication process, so we'll just pass
     them around by reference to all the helper functions. */
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


/* Look through a HASH (with paths as keys, and revision numbers as
   values) for the revision associated with the given PATH.  All
   necessary memory allocations will be performed in POOL.  */
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


/* Generate the appropriate change_file_prop calls to turn the properties
   of ANCESTOR_FILE into those of TARGET_FILE.  If ANCESTOR_FILE is zero, 
   treat it as if it were a file with no properties.  */
static svn_error_t *
delta_file_props (struct context *c,
                  void *file_baton,
                  svn_string_t *source_path,
                  svn_string_t *target_path)
{
  apr_hash_t *source_props = 0;
  apr_hash_t *target_props = 0;

  /* Get the source file's properties */
  if (source_path)
    SVN_ERR (svn_fs_node_proplist 
             (&source_props, c->source_root, source_path->data,
              c->pool));

  /* Get the target file's properties */
  if (target_path)
    SVN_ERR (svn_fs_node_proplist 
             (&target_props, c->target_root, target_path->data,
              c->pool));

  /* Return the result of the property delta routine */
  return delta_proplists (c, source_props, target_props,
                          change_file_prop, file_baton);
}


/* Generate the appropriate change_dir_prop calls to turn the properties
   of ANCESTOR_FILE into those of TARGET_FILE.  If ANCESTOR_FILE is zero, 
   treat it as if it were a file with no properties.  */
static svn_error_t *
delta_dir_props (struct context *c,
                 void *file_baton,
                 svn_string_t *source_path,
                 svn_string_t *target_path)
{
  apr_hash_t *source_props = 0;
  apr_hash_t *target_props = 0;

  /* Get the source file's properties */
  if (source_path)
    SVN_ERR (svn_fs_node_proplist 
             (&source_props, c->source_root, source_path->data,
              c->pool));

  /* Get the target file's properties */
  if (target_path)
    SVN_ERR (svn_fs_node_proplist 
             (&target_props, c->target_root, target_path->data,
              c->pool));

  /* Return the result of the property delta routine */
  return delta_proplists (c, source_props, target_props,
                          change_dir_prop, file_baton);
}


/* Compare the two property lists SOURCE_PROPS and TARGET_PROPS.  For
   every difference found, generate an appropriate call to CHANGE_FN,
   on OBJECT.  */
static svn_error_t *
delta_proplists (struct context *c,
                 apr_hash_t *s_props,
                 apr_hash_t *t_props,
                 proplist_change_fn_t *change_fn,
                 void *object)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first (t_props); hi; hi = apr_hash_next (hi))
    {
      svn_string_t *s_value, *t_value, *t_name;
      const void *key;
      void *val;
      apr_size_t klen;
          
      /* KEY will be the property name in target, VAL the value */
      apr_hash_this (hi, &key, &klen, &val);
      t_name = svn_string_ncreate (key, klen, c->pool);
      t_value = val;

      /* See if this property existed in the source */
      if (s_props 
          && ((s_value = apr_hash_get (s_props, key, klen)) != 0))
        {
          /* If the value is not the same in the source as it is in
             the target, we have a replace event. */
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
          
          /* KEY will be the property name in target, VAL the value */
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
   text delta from in DELTA_STREAM.  */
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
    }
  while (window);

  return SVN_NO_ERROR;
}


/* Make the appropriate edits on FILE_BATON to change its contents and
   properties from those on ANCESTOR_FILE to those on TARGET_FILE.  */
static svn_error_t *
delta_files (struct context *c, void *file_baton,
             svn_string_t *source_path,
             svn_string_t *target_path)
{
  svn_txdelta_stream_t *delta_stream;

  /* Compare the files' property lists.  */
  SVN_ERR (delta_file_props (c, file_baton, source_path, target_path));

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


/* Emit a delta to delete the entry named NAME from DIR_BATON.  */
static svn_error_t *
delete (struct context *c, void *dir_baton, svn_string_t *target_name)
{
  return c->editor->delete_entry (target_name, dir_baton);
}


/* Emit a delta to create the entry named NAME in DIR_BATON.  */
static svn_error_t *
add_file_or_dir (struct context *c, void *dir_baton, 
                 svn_string_t *target_path,
                 svn_string_t *target_name)
{
  int is_dir;
  svn_string_t *target_full_path = 0;

  if (target_path && target_name)
    {
      /* Get the target's full path */
      target_full_path = svn_string_dup (target_path, c->pool);
      svn_path_add_component 
        (target_full_path, target_name, svn_path_repos_style);
    }

  /* Is the target a file or a directory?  */
  SVN_ERR (svn_fs_is_dir (&is_dir, c->target_root, 
                          target_full_path->data, c->pool));

  if (is_dir)
    {
      void *subdir_baton;

      SVN_ERR (c->editor->add_directory 
               (target_name, dir_baton, 
                NULL, SVN_INVALID_REVNUM, &subdir_baton));
      SVN_ERR (delta_dirs (c, subdir_baton, 0, target_full_path));
      SVN_ERR (c->editor->close_directory (subdir_baton));
    }
  else
    {
      void *file_baton;

      SVN_ERR (c->editor->add_file 
               (target_name, dir_baton, 
                NULL, SVN_INVALID_REVNUM, &file_baton));
      SVN_ERR (delta_files (c, file_baton, 0, target_full_path));
      SVN_ERR (c->editor->close_file (file_baton));
    }

  return SVN_NO_ERROR;
}


/* Modify the directory TARGET_PATH (which is associated with
   DIR_BATON) by adding (if TARGET_NAME is NULL) or replacing (if
   TARGET_NAME is non-NULL) a file or directory.  If SOURCE_PATH and
   SOURCE_NAME are both non-NULL, the add/replace will occur with
   deltas against the source path, else we will be adding/replacing
   "from scratch". */
static svn_error_t *
replace_file_or_dir (struct context *c, void *dir_baton,
                     svn_string_t *target_path, svn_string_t *target_name,
                     svn_string_t *source_path, svn_string_t *source_name)
{
  int is_dir;
  svn_string_t *source_full_path = 0;
  svn_string_t *target_full_path = 0;
  svn_revnum_t base_revision = SVN_INVALID_REVNUM;

  if (target_path && target_name)
    {
      /* Get the target's full path */
      target_full_path = svn_string_dup (target_path, c->pool);
      svn_path_add_component 
        (target_full_path, target_name, svn_path_repos_style);

      /* Is the target a file or a directory?  */
      SVN_ERR (svn_fs_is_dir (&is_dir, c->target_root, 
                              target_full_path->data, c->pool));

    }

  if (source_path && source_name)
    {
      /* Get the source's full path */
      source_full_path = svn_string_dup (source_path, c->pool);
      svn_path_add_component 
        (source_full_path, source_name, svn_path_repos_style);

      /* Get the base revision for the entry from the hash. */
      base_revision = get_revision_from_hash (c->source_rev_diffs,
                                              source_full_path,
                                              c->pool);
    }

  if (is_dir)
    {
      void *subdir_baton;

      SVN_ERR (c->editor->replace_directory 
               (target_name, dir_baton, base_revision, &subdir_baton));
      SVN_ERR (delta_dirs 
               (c, subdir_baton, source_full_path, target_full_path));
      SVN_ERR (c->editor->close_directory (subdir_baton));
    }
  else
    {
      void *file_baton;

      SVN_ERR (c->editor->replace_file 
               (target_name, dir_baton, base_revision, &file_baton));
      SVN_ERR (delta_files 
               (c, file_baton, source_full_path, target_full_path));
      SVN_ERR (c->editor->close_file (file_baton));
    }

  return SVN_NO_ERROR;
}


/* Do a `replace' edit in DIR_BATON turning the entry named
   T_ENTRY->name in SOURCE_PATH into the corresponding entry in
   TARGET_PATH.  Emit a replace_dir or replace_file as needed.  Choose
   an appropriate ancestor, or describe the file/tree from scratch.  */
static svn_error_t *
replace_with_nearest (struct context *c, void *dir_baton,
                      svn_string_t *source_path, svn_string_t *target_path,
                      svn_fs_dirent_t *t_entry)
{
  apr_hash_t *s_entries;
  apr_hash_index_t *hi;
  int best_distance = -1;
  svn_fs_dirent_t *best_entry = NULL;

  /* Get the list of entries in source.  */
  SVN_ERR (svn_fs_dir_entries (&s_entries, c->source_root,
                               source_path->data, c->pool));

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
      svn_fs_dirent_t *this_entry;
    
      /* KEY will be the entry name in source, VAL the dirent */
      apr_hash_this (hi, &key, &klen, &val);
      this_entry = val;

      /* Find the distance between the target entry and this source
         entry.  This returns -1 if they're completely unrelated.
         Here we're using ID distance as an approximation for delta
         size.  */
      distance = svn_fs_id_distance (t_entry->id, this_entry->id);

      /* If these nodes are completely unrelated, move along. */
      if (distance == -1)
        continue;

      /* If this is the first related node we've found, or just a
         closer node than previously discovered, update our
         best_distance tracker. */
      if ((best_distance == -1) || (distance < best_distance))
        {
          best_distance = distance;
          best_entry = this_entry;
        }
    }

  /* If our best_distance is still an invalid distance, we'll replace
     this from scratch.  Else, replace it relative to the ancestor we
     found. */
  if (best_distance == -1)
    SVN_ERR (replace_file_or_dir (c, dir_baton,
                                  target_path, 
                                  svn_string_create (t_entry->name, c->pool), 
                                  0, 
                                  0));
  else
    SVN_ERR (replace_file_or_dir (c, dir_baton,
                                  target_path, 
                                  svn_string_create (t_entry->name, c->pool),
                                  source_path, 
                                  svn_string_create 
                                      (best_entry->name, c->pool)));

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
  SVN_ERR (delta_dir_props (c, dir_baton, source_path, target_path));

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
     partner in the source.  If the partner is found, determine if we
     need to replace the one in target with a new version or not, then
     remove that entry from the source entries hash.  If the partner
     is not found, the entry must be added to the target.   When all the
     existing target entries have been handled, those entries still
     remaining in the source hash are ones that need to be deleted
     from the target tree. */
  for (hi = apr_hash_first (t_entries); hi; hi = apr_hash_next (hi))
    {
      svn_fs_dirent_t *s_entry, *t_entry;
      const void *key;
      void *val;
      apr_size_t klen;
          
      /* KEY will be the entry name in target, VAL the dirent */
      apr_hash_this (hi, &key, &klen, &val);
      t_entry = val;
          
      /* Can we find something with the same name in the source
         entries hash? */
      if (s_entries 
          && ((s_entry = apr_hash_get (s_entries, key, klen)) != 0))
        {
          int distance;

          /* Check the distance between the ids.  0 means they are the
             same id, and this is a noop.  -1 means they are
             unrelated, so we'll try to find a relative somewhere else
             in the directory.  Any other positive value means they
             are related through ancestry, so we'll go ahead and
             do the replace directly.  */
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
                        source_path, 
                        svn_string_create (s_entry->name, c->pool),
                        target_path,
                        svn_string_create (t_entry->name, c->pool)));
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

  /* All that should be left in the source entries hash are things
     that need to be deleted. */
  if (s_entries)
    {
      for (hi = apr_hash_first (s_entries); hi; hi = apr_hash_next (hi))
        {
          svn_fs_dirent_t *s_entry;
          const void *key;
          void *val;
          apr_size_t klen;
          
          /* KEY will be the entry name in source, VAL the dirent */
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
