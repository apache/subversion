/* delta.c --- comparing trees and files
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

#include <string.h>
#include "apr_general.h"
#include "apr_pools.h"
#include "apr_hash.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "fs.h"
#include "err.h"


/* Some datatypes and declarations used throughout the file.  */


/* Parameters which remain constant throughout a delta traversal.
   At the top of the recursion, we initialize one of these structures.
   Then, we pass it down, unchanged, to every call.  This way,
   functions invoked deep in the recursion can get access to this
   traversal's global parameters, without using global variables.  */
struct context {
  svn_delta_edit_fns_t *editor;
  apr_pool_t *pool;
};


/* The type of a function that accepts changes to an object's property
   list.  OBJECT is the object whose properties are being changed.
   NAME is the name of the property to change.  VALUE is the new value
   for the property, or zero if the property should be deleted.  */
typedef svn_error_t *proplist_change_fn_t (void *object,
                                           svn_string_t *name,
                                           svn_string_t *value);



/* Forward declarations for each section's public functions.  */

/* See the functions themselves for descriptions.  */
static svn_error_t *delta_dirs (struct context *c, void *dir_baton,
                                svn_fs_dir_t *source,
                                svn_string_t *source_path,
                                svn_fs_dir_t *target);
static svn_error_t *replace (struct context *c, void *dir_baton,
                             svn_fs_dir_t *source, svn_string_t *source_path,
                             svn_fs_dir_t *target,
                             svn_fs_dirent_t *target_entry);
static svn_error_t *delete (struct context *c, void *dir_baton,
                            svn_string_t *name);
static svn_error_t *add (struct context *c, void *dir_baton,
                         svn_fs_dir_t *source, svn_string_t *source_path,
                         svn_fs_dir_t *target, svn_string_t *name);
static svn_error_t *delta_files (struct context *c, void *file_baton,
                                 svn_fs_file_t *ancestor_file,
                                 svn_fs_file_t *target_file);
static svn_error_t *file_from_scratch (struct context *c,
                                       void *file_baton,
                                       svn_fs_file_t *target_file);
static svn_error_t *delta_proplists (struct context *c,
                                     svn_fs_proplist_t *source,
                                     svn_fs_proplist_t *target,
                                     proplist_change_fn_t *change_fn,
                                     void *object);
static svn_error_t *dir_from_scratch (struct context *c,
                                      void *dir_baton,
                                      svn_fs_dir_t *target);


/* Public interface to delta computation.  */

/* ben sez:  whoa!  this declaration doesn't even match the svn_fs.c
   prototype!   No wonder this file isn't being compiled. :) */

svn_error_t *
svn_fs_dir_delta (svn_fs_dir_t *source,
                  svn_fs_dir_t *target,
                  svn_delta_edit_fns_t *editor,
                  void *edit_baton,
                  apr_pool_t *parent_pool)
{
  svn_error_t *svn_err = 0;
  apr_pool_t *pool = svn_pool_create (parent_pool);
  svn_string_t source_path;
  void *root_baton;
  struct context c;

  source_path.len = 0;

  /* ben sez:  this routine is using an out-of-date editor interface.  

      1.  It must call set_target_revision(), passing the revision
      that is built into the TARGET_ROOT argument it received.

      2.  The call to replace_root() below (and all calls to
      replace_*(), actually), must pass a base_rev argument as a
      sanity check: just to make sure that the thing we're changing in
      the working copy really is what we think it is.
  */

  svn_err = editor->replace_root (edit_baton, &root_baton);
  if (svn_err) goto error;

  c.editor = editor;
  c.pool = pool;

  svn_err = delta_dirs (&c, root_baton, source, &source_path, target);
  if (svn_err) goto error;

  svn_err = editor->close_directory (root_baton);
  if (svn_err) goto error;

 error:
  apr_pool_destroy (pool);
  return svn_err;
}



/* Compare two directories.  */


/* Forward declarations for functions local to this section.
   See the functions themselves for descriptions.  */
static svn_error_t *delta_dir_props (struct context *c,
                                     void *dir_baton,
                                     svn_fs_dir_t *source,
                                     svn_fs_dir_t *target);

/* Emit deltas to turn SOURCE into TARGET_DIR.  Assume that DIR_BATON
   represents the directory we're constructing to the editor in the
   context C.  SOURCE_PATH is the path to SOURCE, relative to the top
   of the delta, or the empty string if SOURCE is the top itself.  */
static svn_error_t *
delta_dirs (struct context *c, void *dir_baton,
            svn_fs_dir_t *source, svn_string_t *source_path,
            svn_fs_dir_t *target)
{
  svn_fs_dirent_t **source_entries, **target_entries;
  int si, ti;

  /* Compare the property lists.  */
  SVN_ERR (delta_dir_props (c, dir_baton, source, target));

  /* Get the list of entries in each of source and target.  */
  SVN_ERR (svn_fs_dir_entries (&source_entries, source));
  SVN_ERR (svn_fs_dir_entries (&target_entries, target));

  si = 0, ti = 0;
  while (source_entries[si] || target_entries[ti])
    {
      /* Compare the names of the current directory entries in both
         source and target.  If they're equal, then we've found an
         entry common to both directories.  Otherwise, whichever entry
         comes `earlier' in the sort order doesn't exist in the other
         directory, so we've got an add or a delete.

         (Note: it's okay if si or ti point at the zero that
         terminates the arrays; see the comments for
         svn_fs_compare_dirents.)  */
      int name_cmp = svn_fs_compare_dirents (source_entries[si],
                                             target_entries[ti]);

      /* Does an entry by this name exist in both the source and the
         target?  */
      if (name_cmp == 0)
        {
          /* Compare the node numbers.  */
          if (! svn_fs_id_eq (source_entries[si]->id, target_entries[ti]->id))
            {
              /* The name is the same, but the node has changed.
                 This is a replace.  */
              SVN_ERR (replace (c, dir_baton,
                                source, source_path,
                                target, source_entries[si]));
            }

          /* This entry is now dealt with in both the source and target.  */
          si++, ti++;
        }

      /* If the current source entry is "before" the current target
         entry, then that source entry was deleted.  */
      else if (name_cmp < 0)
        {
          SVN_ERR (delete (c, dir_baton, source_entries[si]->name));
          si++;
        }

      /* A new entry has been added.  */
      else
        {
          SVN_ERR (add (c, dir_baton,
                        source, source_path, target,
                        target_entries[ti]->name));
          ti++;
        }
    }

  return 0;
}


/* Comparing directories' property lists.  */
static svn_error_t *
delta_dir_props (struct context *c,
                 void *dir_baton,
                 svn_fs_dir_t *source,
                 svn_fs_dir_t *target)
{
  svn_fs_proplist_t *source_props
    = (source ? svn_fs_node_proplist (svn_fs_dir_to_node (source)) : 0);
  svn_fs_proplist_t *target_props
    = svn_fs_node_proplist (svn_fs_dir_to_node (target));

  return delta_proplists (c, source_props, target_props,
                          c->editor->change_dir_prop, dir_baton);
}



/* A temporary baton for changing directory entry property lists.  */
struct dirent_plist_baton {

  /* The editor for these changes.  */
  svn_delta_edit_fns_t *editor;
  
  /* The baton for the directory whose entry's properties are being
     changed.  */
  void *dir_baton;

  /* The name of the entry whose properties are being changed.  */
  svn_string_t *entry_name;
};



/* Doing replaces.  */


/* Forward declarations for functions local to this section.
   See the functions themselves for descriptions.  */
static svn_error_t *replace_related (struct context *c,
                                     void *dir_baton,
                                     svn_fs_dir_t *target,
                                     svn_string_t *target_name,
                                     svn_fs_dir_t *ancestor_dir,
                                     svn_string_t *ancestor_dir_path,
                                     svn_string_t *ancestor_name);
static svn_error_t *replace_from_scratch (struct context *c, void *dir_baton,
                                          svn_fs_dir_t *target,
                                          svn_string_t *name);


/* Do a `replace' edit in DIR_BATON turning the entry named
   TARGET_ENTRY->name in SOURCE into the corresponding entry in
   TARGET.  SOURCE_PATH is the path to SOURCE, relative to the top of
   the delta, or the empty string if SOURCE is the top itself.

   Emit a replace_dir or replace_file as needed.  Choose an
   appropriate ancestor, or describe the tree from scratch.  */
   
static svn_error_t *
replace (struct context *c, void *dir_baton,
         svn_fs_dir_t *source, svn_string_t *source_path,
         svn_fs_dir_t *target,
         svn_fs_dirent_t *target_entry)
{
  svn_fs_dirent_t **source_entries;
  int best, best_distance;

  /* Get the list of entries in SOURCE.  */
  SVN_ERR (svn_fs_dir_entries (&source_entries, source));

  /* Find the closest relative to TARGET_ENTRY in SOURCE.
     
     In principle, a replace operation can choose the ancestor from
     anywhere in the delta's whole source tree.  In this
     implementation, we only search SOURCE for possible ancestors.
     This will need to improve, so we can find the best ancestor, no
     matter where it's hidden away in the source tree.  */
  {
    int i;

    best = -1;
    for (i = 0; source_entries[i]; i++)
      {
        /* Find the distance between the target entry and this source
           entry.  This returns -1 if they're completely unrelated.
           Here we're using ID distance as an approximation for delta
           size.  */
        int distance = svn_fs_id_distance (target_entry->id,
                                           source_entries[i]->id);

        if (distance != -1
            && (best == -1 || distance < best_distance))
          {
            best = i;
            best_distance = distance;
          }
      }
  }

  if (best == -1)
    /* We can't find anything related to this file / directory.
       Send it from scratch.  */
    SVN_ERR (replace_from_scratch (c, dir_baton, target, target_entry->name));
  else
    /* We've found an ancestor; do a replace relative to that.  */
    SVN_ERR (replace_related (c, dir_baton,
                              target, target_entry->name, 
                              source, source_path,
                              source_entries[best]->name));

  return 0;
}


/* Replace the directory entry named NAME in DIR_BATON with a new
   node, for which we have no ancestor.  The new node is the entry
   named NAME in TARGET.  */
static svn_error_t *
replace_from_scratch (struct context *c, void *dir_baton,
                      svn_fs_dir_t *target, svn_string_t *name)
{
  svn_fs_node_t *new;

  /* Open the new node.  */
  SVN_ERR (svn_fs_open_node (&new, target, name));
  svn_fs_cleanup_node (c->pool, new);

  /* Is it a file or a directory?  */
  if (svn_fs_node_is_file (new))
    {
      void *file_baton;

      SVN_ERR (c->editor->replace_file (name, dir_baton, 0, 0, &file_baton));
      SVN_ERR (file_from_scratch (c, file_baton,
                                  svn_fs_node_to_file (new)));
      SVN_ERR (c->editor->close_file (file_baton));
    }
  else if (svn_fs_node_is_dir (new))
    {
      void *subdir_baton;

      SVN_ERR (c->editor->replace_directory (name, dir_baton,
                                             0, 0, &subdir_baton));
      SVN_ERR (dir_from_scratch (c, subdir_baton, 
                                 svn_fs_node_to_dir (new)));
      SVN_ERR (c->editor->close_directory (subdir_baton));
    }
  else
    abort ();

  svn_fs_run_cleanup_node (c->pool, new);

  return 0;
}


/* Do a replace, with a known ancestor.

   Replace the entry named TARGET_NAME in the directory DIR_BATON with
   the node of the same name in TARGET, using the entry named
   ANCESTOR_NAME in ANCESTOR_DIR as the ancestor.  ANCESTOR_DIR_PATH
   is the path to ANCESTOR_DIR from the top of the delta.  */
static svn_error_t *
replace_related (struct context *c, void *dir_baton,
                 svn_fs_dir_t *target, svn_string_t *target_name,
                 svn_fs_dir_t *ancestor_dir, svn_string_t *ancestor_dir_path,
                 svn_string_t *ancestor_name)
{
  svn_string_t *ancestor_path;
  svn_revnum_t ancestor_revision;
  svn_fs_node_t *a, *t;

  /* Open the ancestor and target nodes.  */
  SVN_ERR (svn_fs_open_node (&a, ancestor_dir, ancestor_name));
  svn_fs_cleanup_node (c->pool, a);
  SVN_ERR (svn_fs_open_node (&t, target, target_name));
  svn_fs_cleanup_node (c->pool, t);

  /* Compute the full name of the ancestor.  */
  ancestor_path = svn_string_dup (ancestor_dir_path, c->pool);
  svn_path_add_component (ancestor_path, ancestor_name,
                          svn_path_repos_style);

  /* Get the ancestor's revision number.  */
  ancestor_revision = svn_fs_node_revision (a);

  if (svn_fs_node_is_file (t))
    {
      void *file_baton;

      /* Do the replace, yielding a baton for the file.  */
      SVN_ERR (c->editor->replace_file (target_name, dir_baton,
                                        ancestor_path, ancestor_revision,
                                        &file_baton));

      /* Apply the text delta.  */
      SVN_ERR (delta_files (c, file_baton,
                            svn_fs_node_to_file (a),
                            svn_fs_node_to_file (t)));

      /* Close the editor's file baton.  */
      SVN_ERR (c->editor->close_file (file_baton));
    }
  else if (svn_fs_node_is_dir (t))
    {
      void *subdir_baton;

      /* Do the replace, yielding a baton for the new subdirectory.  */
      SVN_ERR (c->editor->replace_directory (target_name,
                                             dir_baton,
                                             ancestor_path, ancestor_revision,
                                             &subdir_baton));

      /* Compute the delta for those subdirs.  */
      SVN_ERR (delta_dirs (c, subdir_baton,
                           svn_fs_node_to_dir (a),
                           ancestor_path,
                           svn_fs_node_to_dir (t)));

      /* Close the editor's subdirectory baton.  */
      SVN_ERR (c->editor->close_directory (subdir_baton));
    }
  else
    abort ();

  /* Close the ancestor and target files.  */
  svn_fs_run_cleanup_node (c->pool, a);
  svn_fs_run_cleanup_node (c->pool, t);

  return 0;
}



/* Doing deletes.  */


/* Emit a delta to delete the entry named NAME from DIR_BATON.  */
static svn_error_t *
delete (struct context *c, void *dir_baton,
        svn_string_t *name)
{
  return c->editor->delete_entry (name, dir_baton);
}



/* Doing adds.  */

static svn_error_t *
add (struct context *c, void *dir_baton,
     svn_fs_dir_t *source, svn_string_t *source_path,
     svn_fs_dir_t *target, svn_string_t *name)
{
  /* ...; */
  return SVN_NO_ERROR;
}


/* Compare two files.  */


/* Forward declarations for functions local to this section.
   See the functions themselves for descriptions.  */
static svn_error_t *delta_file_props (struct context *c,
                                      void *file_baton,
                                      svn_fs_file_t *ancestor_file,
                                      svn_fs_file_t *target_file);
static svn_error_t *send_text_delta (struct context *c,
                                     void *file_baton,
                                     svn_txdelta_stream_t *delta_stream);


/* Make the appropriate edits on FILE_BATON to change its contents and
   properties from those on ANCESTOR_FILE to those on TARGET_FILE.  */
static svn_error_t *
delta_files (struct context *c, void *file_baton,
             svn_fs_file_t *ancestor_file,
             svn_fs_file_t *target_file)
{
  svn_txdelta_stream_t *delta_stream;

  /* Compare the files' property lists.  */
  SVN_ERR (delta_file_props (c, file_baton, ancestor_file, target_file));

  /* Get a delta stream turning ANCESTOR_FILE's contents into
     TARGET_FILE's contents.  */
  SVN_ERR (svn_fs_file_delta (&delta_stream, ancestor_file, target_file,
                              c->pool));

  SVN_ERR (send_text_delta (c, file_baton, delta_stream));

  svn_txdelta_free (delta_stream);

  return 0;
}


/* Make the appropriate edits on FILE_BATON to change its contents and
   properties from the empty file (no contents, no properties) to
   those of TARGET_FILE.  */
static svn_error_t *
file_from_scratch (struct context *c,
                   void *file_baton,
                   svn_fs_file_t *target_file)
{
  svn_txdelta_stream_t *delta_stream;

  /* Put the right properties on there.  */
  SVN_ERR (delta_file_props (c, file_baton, 0, target_file));

  /* Get a text delta turning the empty string into TARGET_FILE.  */
  SVN_ERR (svn_fs_file_delta (&delta_stream, 0, target_file, c->pool));

  SVN_ERR (send_text_delta (c, file_baton, delta_stream));

  svn_txdelta_free (delta_stream);

  return 0;
}


/* Generate the appropriate change_file_prop calls to turn the properties
   of ANCESTOR_FILE into those of TARGET_FILE.  If ANCESTOR_FILE is zero, 
   treat it as if it were a file with no properties.  */
static svn_error_t *
delta_file_props (struct context *c,
                  void *file_baton,
                  svn_fs_file_t *ancestor_file,
                  svn_fs_file_t *target_file)
{
  svn_fs_proplist_t *ancestor_props
    = (ancestor_file
       ? svn_fs_node_proplist (svn_fs_file_to_node (ancestor_file))
       : 0);
  svn_fs_proplist_t *target_props
    = svn_fs_node_proplist (svn_fs_file_to_node (target_file));

  return delta_proplists (c, ancestor_props, target_props,
                          c->editor->change_file_prop, file_baton);
}


/* Change the contents of FILE_BATON in C->editor, according to the
   text delta from in DELTA_STREAM.  */
static svn_error_t *
send_text_delta (struct context *c,
                 void *file_baton,
                 svn_txdelta_stream_t *delta_stream)
{
  svn_txdelta_window_handler_t *delta_handler;
  void *delta_handler_baton;

  /* Get a handler that will apply the delta to the file.  */
  SVN_ERR (c->editor->apply_textdelta (file_baton,
                                       &delta_handler, &delta_handler_baton));

  /* Read windows from the delta stream, and apply them to the file.  */
  {
    svn_txdelta_window_t *window;
    do
      {
        SVN_ERR (svn_txdelta_next_window (&window, delta_stream));
        SVN_ERR (delta_handler (window, delta_handler_baton));
      }
    while (window);
  }

  return 0;
}



/* Compare two property lists.  */


/* Compare the two property lists SOURCE and TARGET.  For every
   difference found, generate an appropriate call to CHANGE_FN, on
   OBJECT.  */
static svn_error_t *
delta_proplists (struct context *c,
                 svn_fs_proplist_t *source,
                 svn_fs_proplist_t *target,
                 proplist_change_fn_t *change_fn,
                 void *object)
{
  /* It would be nice if we could figure out some way to use the
     history information to avoid reading in and scanning the entire
     property lists.

     It's also kind of stupid to allocate two namelists, sort them,
     and then iterate over them.  If you believe that hash accesses
     are constant time, it's faster to walk once over each hash table.
     You have to do that much work anyway just to generate the name
     lists.  */

  svn_string_t **source_names, **target_names;
  apr_hash_t *source_values, *target_values;
  int si, ti;

  /* Get the names and values of the source object's properties.  If
     SOURCE is zero, treat that like an empty property list.  */
  if (source)
    {
      SVN_ERR (svn_fs_proplist_names (&source_names, source, c->pool));
      SVN_ERR (svn_fs_proplist_hash_table (&source_values, source, c->pool));
    }
  else
    {
      static svn_string_t *null_prop_name_list[] = { 0 };

      source_names = null_prop_name_list;
      /* It doesn't matter what we set source_values to, because we should
         never fetch anything from it.  */
      source_values = 0;
    }

  /* Get the names and values of the target object's properties.  */
  SVN_ERR (svn_fs_proplist_names (&target_names, target, c->pool));
  SVN_ERR (svn_fs_proplist_hash_table (&target_values, target, c->pool));

  si = ti = 0;
  while (source_names[si] || target_names[ti])
    {
      svn_string_t *sn = source_names[si];
      svn_string_t *tn = target_names[ti];
      int cmp = svn_fs_compare_prop_names (sn, tn);

      /* If the two names are equal, then a property by the given name
         exists on both files.  */
      if (cmp == 0)
        {
          /* Get the values of the property.  */
          svn_string_t *sv = apr_hash_get (source_values, sn->data, sn->len);
          svn_string_t *tv = apr_hash_get (target_values, tn->data, tn->len);

          /* Does the property have the same value on both files?  */
          if (! svn_string_compare (sv, tv))
            SVN_ERR (change_fn (object, tn, tv));

          si++, ti++;
        }
      /* If the source name comes earlier, then it's been deleted.  */
      else if (cmp < 0)
        {
          SVN_ERR (change_fn (object, sn, 0));
          si++;
        }
      /* If the target name comes earlier, then it's been added.  */
      else
        {
          /* Get the value of the property.  */
          svn_string_t *tv = apr_hash_get (target_values, tn->data, tn->len);
          SVN_ERR (change_fn (object, tn, tv));
          ti++;
        }
    }

  return 0;
}



/* Building directory trees from scratch.  */

static svn_error_t *
dir_from_scratch (struct context *c,
                  void *dir_baton,
                  svn_fs_dir_t *target)
{
  /* ...; */
  return SVN_NO_ERROR;
}



/* Computing file text deltas.  */

svn_error_t *
svn_fs_file_delta (svn_txdelta_stream_t **stream,
                   svn_fs_file_t *source_file,
                   svn_fs_file_t *target_file,
                   apr_pool_t *pool)
{
  svn_stream_t *source, *target;
  svn_txdelta_stream_t *delta_stream;

  /* Get read functions for the file contents.  */
  if (source_file)
    SVN_ERR (svn_fs_file_contents (&source, source_file, pool));
  else
    source = svn_stream_empty (pool);
  SVN_ERR (svn_fs_file_contents (&target, target_file, pool));

  /* Create a delta stream that turns the ancestor into the target.  */
  svn_txdelta (&delta_stream, source, target, pool);

  *stream = delta_stream;
  return 0;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
