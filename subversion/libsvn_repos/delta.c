/*
 * delta.c:   an editor driver for svn_repos_dir_delta
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


#include "svn_types.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "apr_hash.h"
#include "svn_repos.h"
#include "svn_pools.h"



/* THINGS TODO:  Currently the code herein gives only a slight nod to
   fully supporting directory deltas that involve renames, copies, and
   such.  */
 

/* Some datatypes and declarations used throughout the file.  */


/* Parameters which remain constant throughout a delta traversal.
   At the top of the recursion, we initialize one of these structures.
   Then we pass it down to every call.  This way, functions invoked
   deep in the recursion can get access to this traversal's global
   parameters, without using global variables.  */
struct context {
  const svn_delta_edit_fns_t *editor;
  svn_fs_root_t *source_root;
  svn_fs_root_t *target_root;
  svn_boolean_t text_deltas;
  svn_boolean_t recurse;
  svn_boolean_t entry_props;
  svn_boolean_t use_copy_history;
};


/* The type of a function that accepts changes to an object's property
   list.  OBJECT is the object whose properties are being changed.
   NAME is the name of the property to change.  VALUE is the new value
   for the property, or zero if the property should be deleted.  */
typedef svn_error_t *proplist_change_fn_t (struct context *c,
                                           void *object,
                                           const char *name,
                                           const svn_string_t *value,
                                           apr_pool_t *pool);



/* Some prototypes for functions used throughout.  See each individual
   function for information about what it does.  */


/* Retrieving the base revision from the path/revision hash.  */
static svn_revnum_t get_path_revision (svn_fs_root_t *root,
                                       const char *path,
                                       apr_pool_t *pool);


/* proplist_change_fn_t property changing functions.  */
static svn_error_t *change_dir_prop (struct context *c, 
                                     void *object,
                                     const char *name, 
                                     const svn_string_t *value,
                                     apr_pool_t *pool);

static svn_error_t *change_file_prop (struct context *c, 
                                      void *object,
                                      const char *name, 
                                      const svn_string_t *value,
                                      apr_pool_t *pool);


/* Constructing deltas for properties of files and directories.  */
static svn_error_t *delta_proplists (struct context *c,
                                     const char *source_path,
                                     const char *target_path,
                                     proplist_change_fn_t *change_fn,
                                     void *object,
                                     apr_pool_t *pool);


/* Constructing deltas for file constents.  */
static svn_error_t *send_text_delta (struct context *c,
                                     void *file_baton,
                                     svn_txdelta_stream_t *delta_stream,
                                     apr_pool_t *pool);

static svn_error_t *delta_files (struct context *c, 
                                 void *file_baton,
                                 const char *source_path,
                                 const char *target_path,
                                 apr_pool_t *pool);


/* Generic directory deltafication routines.  */
static svn_error_t *delete (struct context *c, 
                            void *dir_baton, 
                            const char *target_entry,
                            apr_pool_t *pool);

static svn_error_t *add_file_or_dir (struct context *c, 
                                     void *dir_baton, 
                                     const char *target_parent, 
                                     const char *target_entry,
                                     apr_pool_t *pool);

static svn_error_t *replace_file_or_dir (struct context *c, 
                                         void *dir_baton,
                                         const char *source_parent, 
                                         const char *source_entry,
                                         const char *target_parent,
                                         const char *target_entry,
                                         apr_pool_t *pool);

static svn_error_t *delta_dirs (struct context *c, 
                                void *dir_baton,
                                const char *source_path, 
                                const char *target_path,
                                apr_pool_t *pool);



static svn_error_t *
not_a_dir_error (const char *role, 
                 const char *path,
                 apr_pool_t *pool)
{
  return svn_error_createf 
    (SVN_ERR_FS_NOT_DIRECTORY, 0, 0, pool,
     "not_a_dir_error: invalid %s directory '%s'",
     role, path ? path : "(null)");
}


/* Public interface to computing directory deltas.  */
svn_error_t *
svn_repos_dir_delta (svn_fs_root_t *src_root,
                     const char *src_parent_dir,
                     const char *src_entry,
                     svn_fs_root_t *tgt_root,
                     const char *tgt_path,
                     const svn_delta_edit_fns_t *editor,
                     void *edit_baton,
                     svn_boolean_t text_deltas,
                     svn_boolean_t recurse,
                     svn_boolean_t entry_props,
                     svn_boolean_t use_copy_history,
                     apr_pool_t *pool)
{
  void *root_baton;
  struct context c;
  svn_stringbuf_t *tgt_parent_dir, *tgt_entry;
  svn_stringbuf_t *src_fullpath;
  svn_fs_id_t *src_id, *tgt_id;
  svn_error_t *err;
  int distance;

  /* ### need to change svn_path_is_empty() */
  svn_stringbuf_t *tempbuf;

  /* SRC_PARENT_DIR must be valid. */
  if (! src_parent_dir)
    return not_a_dir_error ("source parent", src_parent_dir, pool);

  /* TGT_PATH must be valid. */
  if (! tgt_path)
    return svn_error_create (SVN_ERR_FS_PATH_SYNTAX, 0, 0, pool,
                             "svn_repos_dir_delta: invalid target path");

  tempbuf = svn_stringbuf_create (tgt_path, pool);

  /* Split TGT_PATH into TGT_PARENT_DIR and TGT_ENTRY unless SRC_ENTRY
     is NULL or TGT_PATH cannot be split. */
  if ((! src_entry) || (svn_path_is_empty (tempbuf)))
    {
      tgt_parent_dir = svn_stringbuf_create (tgt_path, pool);
      tgt_entry = NULL;
    }
  else
    {
      svn_path_split (tempbuf, &tgt_parent_dir, &tgt_entry, pool);
    }

  /* Make sure that parent dirs are really directories under both the
     source and target roots.  This also doubles as an existence
     check.  Obviously, an empty parent path is the root of the
     repository, guaranteed to exist as a directory. */
  svn_stringbuf_set (tempbuf, src_parent_dir);
  if (! svn_path_is_empty (tempbuf))
    {
      int s_dir;
      SVN_ERR (svn_fs_is_dir (&s_dir, src_root, src_parent_dir, pool));
      if (! s_dir)
        return not_a_dir_error ("source parent", src_parent_dir, pool);
    }
  if (! svn_path_is_empty (tgt_parent_dir))
    {
      int t_dir;
      SVN_ERR (svn_fs_is_dir (&t_dir, tgt_root, tgt_parent_dir->data, pool));
      if (! t_dir)
        return not_a_dir_error ("target parent", tgt_parent_dir->data, pool);
    }
  
  /* Setup our pseudo-global structure here.  We need these variables
     throughout the deltafication process, so pass them around by
     reference to all the helper functions. */
  c.editor = editor;
  c.source_root = src_root;
  c.target_root = tgt_root;
  c.text_deltas = text_deltas;
  c.recurse = recurse;
  c.entry_props = entry_props;
  c.use_copy_history = use_copy_history;

  /* Set the global target revision if one can be determined. */
  if (svn_fs_is_revision_root (tgt_root))
    {
      SVN_ERR (editor->set_target_revision 
               (edit_baton, svn_fs_revision_root_revision (tgt_root)));
    }
  else if (svn_fs_is_txn_root (tgt_root))
    {
      svn_fs_t *fs = svn_fs_root_fs (tgt_root);
      const char *txn_name = svn_fs_txn_root_name (tgt_root, pool);
      svn_fs_txn_t *txn;

      SVN_ERR (svn_fs_open_txn (&txn, fs, txn_name, pool));
      SVN_ERR (editor->set_target_revision 
               (edit_baton, svn_fs_txn_base_revision (txn)));
      SVN_ERR (svn_fs_close_txn (txn));
    }

  /* Call open_root to get our root_baton... */
  SVN_ERR (editor->open_root 
           (edit_baton, 
            get_path_revision (src_root, src_parent_dir, pool),
            &root_baton));

  /* Construct the full path of the source and target update items. */
  src_fullpath = svn_stringbuf_create (src_parent_dir, pool);
  if (src_entry && *src_entry != '\0')
    svn_path_add_component_nts (src_fullpath, src_entry);

  /* Get the node ids for the source and target paths. */
  err = svn_fs_node_id (&tgt_id, tgt_root, tgt_path, pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
        {
          /* Caller thinks that target still exists, but it doesn't.
             So just delete the target and go home.  */
          svn_error_clear_all (err);
          SVN_ERR (delete (&c, root_baton, src_entry, pool));
          goto cleanup;
        }
      else
        {
          return err;
        }
    }
  err = svn_fs_node_id (&src_id, src_root, src_fullpath->data, pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
        {
          /* The target has been deleted from our working copy. Add
             back a new one. */
          svn_error_clear_all (err);
          SVN_ERR (add_file_or_dir (&c, root_baton,
                                    tgt_parent_dir->data,
                                    tgt_entry->data,
                                    pool));
        }
      else
        {
          return err;
        }
    }
  else if (src_entry && *src_entry != '\0')
    {
      /* Use the distance between the node ids to determine the best
         way to update the requested entry. */
      distance = svn_fs_id_distance (src_id, tgt_id);
      if (distance == 0)
        {
          /* They're the same node!  No-op (you gotta love those). */
        }
      else if (distance == -1)
        {
          /* The nodes are not related at all.  Delete the one, and
             add the other. */
          SVN_ERR (delete (&c, root_baton, src_entry, pool));
          SVN_ERR (add_file_or_dir (&c, root_baton,
                                    tgt_parent_dir->data,
                                    tgt_entry->data,
                                    pool));
        }
      else
        {
          /* The nodes are at least related.  Just replace the one
             with the other. */
          SVN_ERR (replace_file_or_dir (&c, root_baton,
                                        src_parent_dir,
                                        src_entry,
                                        tgt_parent_dir->data,
                                        tgt_entry->data,
                                        pool));
        }
    }
  else
    {
      /* There is no entry given, so update the whole parent directory. */
      SVN_ERR (delta_dirs (&c, root_baton,
                           src_fullpath->data, tgt_path,
                           pool));
    }

 cleanup:

  /* Make sure we close the root directory we opened above. */
  SVN_ERR (editor->close_directory (root_baton));

  /* Close the edit. */
  SVN_ERR (editor->close_edit (edit_baton));

  /* All's well that ends well. */
  return SVN_NO_ERROR;
}



/* Retrieving the base revision from the path/revision hash.  */


static svn_revnum_t 
get_path_revision (svn_fs_root_t *root,
                   const char *path,
                   apr_pool_t *pool)
{
  svn_revnum_t revision;

  /* Easy out -- if ROOT is a revision root, we can use the revision
     that it's a root of. */
  if (svn_fs_is_revision_root (root))
    return svn_fs_revision_root_revision (root);

  /* Else, this must be a transaction root, so ask the filesystem in
     what revision this path was created. */
  if (svn_fs_node_created_rev (&revision, root, path, pool))
    revision = SVN_INVALID_REVNUM;

  /* If we don't get back a valid revision, this path is mutable in
     the transaction.  We should probably examing the node on which it
     is based, doable by querying for the node-id of the path, and
     thing examining that node-id's predecessor.  ### this predecessor
     determination isn't exposed via the FS public API right now, so
     for now, we'll just return the SVN_INVALID_REVNUM. */
  return revision;
}


/* proplist_change_fn_t property changing functions.  */


/* Call the directory property-setting function of C->editor to set
   the property NAME to given VALUE on the OBJECT passed to this
   function. */
static svn_error_t *
change_dir_prop (struct context *c, void *object,
                 const char *name, const svn_string_t *value,
                 apr_pool_t *pool)
{
  /* ### fix editor interface */
  svn_stringbuf_t *namebuf = svn_stringbuf_create (name, pool);
  svn_stringbuf_t *valbuf =
    value ? svn_stringbuf_create_from_string (value, pool) : NULL;

  return c->editor->change_dir_prop (object, namebuf, valbuf);
}


/* Call the file property-setting function of C->editor to set the
   property NAME to given VALUE on the OBJECT passed to this
   function. */
static svn_error_t *
change_file_prop (struct context *c, void *object,
                  const char *name, const svn_string_t *value,
                  apr_pool_t *pool)
{
  /* ### fix editor interface */
  svn_stringbuf_t *namebuf = svn_stringbuf_create (name, pool);
  svn_stringbuf_t *valbuf =
    value ? svn_stringbuf_create_from_string (value, pool) : NULL;

  return c->editor->change_file_prop (object, namebuf, valbuf);
}




/* Constructing deltas for properties of files and directories.  */


/* Generate the appropriate property editing calls to turn the
   properties of SOURCE_PATH into those of TARGET_PATH.  If
   SOURCE_PATH is NULL, treat it as if it were a file with no
   properties.  Pass OBJECT on to the editor function wrapper
   CHANGE_FN. */
static svn_error_t *
delta_proplists (struct context *c,
                 const char *source_path,
                 const char *target_path,
                 proplist_change_fn_t *change_fn,
                 void *object,
                 apr_pool_t *pool)
{
  apr_hash_t *s_props = 0;
  apr_hash_t *t_props = 0;
  apr_hash_index_t *hi;
  apr_pool_t *subpool;

  /* Make a subpool for local allocations. */ 
  subpool = svn_pool_create (pool);

  /* If we're supposed to send entry props for all non-deleted items,
     here we go! */
  if (target_path && c->entry_props)
    {
      svn_revnum_t committed_rev = SVN_INVALID_REVNUM;
      svn_string_t *cr_str = NULL;
      svn_string_t *committed_date = NULL;
      svn_string_t *last_author = NULL;
      
      /* Get the CR and two derivative props. ### check for error returns. */
      svn_fs_node_created_rev (&committed_rev, c->target_root, 
                               target_path, subpool);
      if (SVN_IS_VALID_REVNUM (committed_rev))
        {
          svn_fs_t *fs = svn_fs_root_fs (c->target_root);

          /* Transmit the committed-rev. */
          cr_str = svn_string_createf (subpool, "%" SVN_REVNUM_T_FMT,
                                       committed_rev);
          SVN_ERR (change_fn (c, object, SVN_PROP_ENTRY_COMMITTED_REV, 
                              cr_str, subpool));

          /* Transmit the committed-date. */
          svn_fs_revision_prop (&committed_date, fs, committed_rev, 
                                SVN_PROP_REVISION_DATE, subpool);
          SVN_ERR (change_fn (c, object, SVN_PROP_ENTRY_COMMITTED_DATE, 
                              committed_date, subpool));

          /* Transmit the last-author. */
          svn_fs_revision_prop (&last_author, fs, committed_rev, 
                                SVN_PROP_REVISION_AUTHOR, subpool);
          SVN_ERR (change_fn (c, object, SVN_PROP_ENTRY_LAST_AUTHOR,
                              last_author, subpool));
        }
    }

  if (source_path && target_path)
    {
      int changed;

      /* Is this deltification worth our time? */
      SVN_ERR (svn_fs_props_changed (&changed, c->target_root, target_path,
                                     c->source_root, source_path, subpool));
      if (! changed)
        goto cleanup;
    }

  /* Get the source file's properties */
  if (source_path)
    SVN_ERR (svn_fs_node_proplist (&s_props, c->source_root, 
                                   source_path, subpool));

  /* Get the target file's properties */
  if (target_path)
    SVN_ERR (svn_fs_node_proplist (&t_props, c->target_root, 
                                   target_path, subpool));

  for (hi = apr_hash_first (subpool, t_props); hi; hi = apr_hash_next (hi))
    {
      const svn_string_t *s_value;
      const void *key;
      void *val;
      apr_ssize_t klen;
          
      /* KEY is property name in target, VAL the value */
      apr_hash_this (hi, &key, &klen, &val);

      /* See if this property existed in the source.  If so, and if
         the values in source and target differ, open the value in
         target with the one in source. */
      if (s_props && ((s_value = apr_hash_get (s_props, key, klen)) != 0))
        {
          if (! svn_string_compare (s_value, val))
            SVN_ERR (change_fn (c, object, key, val, subpool));

          /* Remove the property from source list so we can track
             which items have matches in the target list. */
          apr_hash_set (s_props, key, klen, NULL);
        }
      else
        {
          /* This property didn't exist in the source, so this is just
             an add. */
          SVN_ERR (change_fn (c, object, key, val, subpool));
        }
    }

  /* All the properties remaining in the source list are not present
     in the target, and so must be deleted. */
  if (s_props)
    {
      for (hi = apr_hash_first (subpool, s_props); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          
          /* KEY is property name in target, VAL the value */
          apr_hash_this (hi, &key, NULL, NULL);

          SVN_ERR (change_fn (c, object, key, NULL, subpool));
        }
    }

 cleanup:
  /* Destroy local subpool. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}




/* Constructing deltas for file constents.  */


/* Change the contents of FILE_BATON in C->editor, according to the
   text delta from DELTA_STREAM.  */
static svn_error_t *
send_text_delta (struct context *c,
                 void *file_baton,
                 svn_txdelta_stream_t *delta_stream,
                 apr_pool_t *pool)
{
  svn_txdelta_window_handler_t delta_handler;
  void *delta_handler_baton;

  /* Get a handler that will apply the delta to the file.  */
  SVN_ERR (c->editor->apply_textdelta 
           (file_baton, &delta_handler, &delta_handler_baton));

  
  if (c->text_deltas && delta_stream)
    {
      /* Deliver the delta stream to the file.  */
      SVN_ERR (svn_txdelta_send_txstream (delta_stream,
                                          delta_handler,
                                          delta_handler_baton,
                                          pool));
    }
  else
    {
      /* The caller doesn't want text delta data.  Just send a single
         NULL window. */
      SVN_ERR (delta_handler (NULL, delta_handler_baton));
    }

  return SVN_NO_ERROR;
}


/* Make the appropriate edits on FILE_BATON to change its contents and
   properties from those in SOURCE_PATH to those in TARGET_PATH. */
static svn_error_t *
delta_files (struct context *c, 
             void *file_baton,
             const char *source_path,
             const char *target_path,
             apr_pool_t *pool)
{
  apr_pool_t *subpool;
  int changed = 1;

  /* Make a subpool for local allocations. */
  subpool = svn_pool_create (pool);

  /* Compare the files' property lists.  */
  SVN_ERR (delta_proplists (c, source_path, target_path,
                            change_file_prop, file_baton, subpool));

  if (source_path)
    {
      /* Is this deltification worth our time? */
      SVN_ERR (svn_fs_contents_changed (&changed, 
                                        c->target_root, target_path,
                                        c->source_root, source_path,
                                        subpool));
    }
  else
    {
      /* If there isn't a source path, this is an add, which
         necessarily has textual mods. */
    }

  /* If we care about text_deltas, and there, we need to get a delta
     stream and hand that off to . */
  if (changed)
    {
      svn_txdelta_stream_t *delta_stream = NULL;

      if (c->text_deltas)
        {
          /* Get a delta stream turning an empty file into one having
             TARGET_PATH's contents.  */
          SVN_ERR (svn_fs_get_file_delta_stream 
                   (&delta_stream, 
                    source_path ? c->source_root : NULL,
                    source_path ? source_path : NULL,
                    c->target_root, target_path, subpool));
        }

      SVN_ERR (send_text_delta (c, file_baton, delta_stream, subpool));
    }

  /* Cleanup. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}




/* Generic directory deltafication routines.  */


/* Emit a delta to delete the entry named TARGET_ENTRY from DIR_BATON.  */
static svn_error_t *
delete (struct context *c, 
        void *dir_baton, 
        const char *target_entry,
        apr_pool_t *pool)
{
  /* ### change the editor prototypes... */
  svn_stringbuf_t *entrybuf = svn_stringbuf_create (target_entry, pool);

  return c->editor->delete_entry (entrybuf, SVN_INVALID_REVNUM, dir_baton);
}


/* Emit a delta to create the entry named TARGET_ENTRY in the
   directory TARGET_PARENT.  Pass DIR_BATON through to editor
   functions that require it. */
static svn_error_t *
add_file_or_dir (struct context *c, void *dir_baton,
                 const char *target_parent,
                 const char *target_entry,
                 apr_pool_t *pool)
{
  int is_dir;
  svn_stringbuf_t *t_fullpath;
  svn_revnum_t copied_from_revision = SVN_INVALID_REVNUM;
  const char *copied_from_path = NULL;
  struct context *context = c;

  /* ### Upgrade this driver to the svn_delta_editor_t interface! */
  svn_stringbuf_t *namebuf;
  svn_stringbuf_t *copied_path_buf = NULL;

  if (!target_parent || !target_entry)
    abort();

  /* Get the target's full path */
  t_fullpath = svn_stringbuf_create (target_parent, pool);
  svn_path_add_component_nts (t_fullpath, target_entry);

  /* Is the target a file or a directory?  */
  SVN_ERR (svn_fs_is_dir (&is_dir, c->target_root, t_fullpath->data, pool));

  namebuf = svn_stringbuf_create (target_entry, pool);

  if (c->use_copy_history)
    {
      SVN_ERR (svn_fs_copied_from (&copied_from_revision,
                                   &copied_from_path,
                                   c->target_root,
                                   t_fullpath->data,
                                   pool));
    }

  if ((SVN_IS_VALID_REVNUM (copied_from_revision)) && copied_from_path)
    {
      context = apr_palloc (pool, sizeof (*context));
      *context = *c;
      SVN_ERR (svn_fs_revision_root (&(context->source_root),
                                     svn_fs_root_fs (context->target_root),
                                     copied_from_revision, pool));
      
      /* ### Also deal with legacy interface. */
      copied_path_buf = svn_stringbuf_create (copied_from_path, pool);
    }

  if (is_dir)
    {
      void *subdir_baton;

      SVN_ERR (context->editor->add_directory 
               (namebuf, dir_baton,
                copied_path_buf, copied_from_revision, &subdir_baton));
      SVN_ERR (delta_dirs (context, subdir_baton,
                           copied_from_path, t_fullpath->data, pool));
      SVN_ERR (context->editor->close_directory (subdir_baton));
    }
  else
    {
      void *file_baton;

      SVN_ERR (context->editor->add_file 
               (namebuf, dir_baton,
                copied_path_buf, copied_from_revision, &file_baton));
      SVN_ERR (delta_files (context, file_baton,
                            copied_from_path, t_fullpath->data, pool));
      SVN_ERR (context->editor->close_file (file_baton));
    }

  return SVN_NO_ERROR;
}


/* Modify the directory TARGET_PARENT by replacing its entry named
   TARGET_ENTRY with the SOURCE_ENTRY found in SOURCE_PARENT.  Pass
   DIR_BATON through to editor functions that require it. */
static svn_error_t *
replace_file_or_dir (struct context *c, 
                     void *dir_baton,
                     const char *source_parent, 
                     const char *source_entry,
                     const char *target_parent, 
                     const char *target_entry,
                     apr_pool_t *pool)
{
  int is_dir;
  svn_stringbuf_t *s_fullpath = 0;
  svn_stringbuf_t *t_fullpath = 0;
  svn_revnum_t base_revision = SVN_INVALID_REVNUM;

  /* ### change the delta interface */
  svn_stringbuf_t *namebuf;

  if (!target_parent || !target_entry)
    abort();

  if (!source_parent || !source_entry)
    abort();

  /* Get the target's full path */
  t_fullpath = svn_stringbuf_create (target_parent, pool);
  svn_path_add_component_nts (t_fullpath, target_entry);

  /* Is the target a file or a directory?  */
  SVN_ERR (svn_fs_is_dir (&is_dir, c->target_root, 
                          t_fullpath->data, pool));

  /* Get the source's full path */
  s_fullpath = svn_stringbuf_create (source_parent, pool);
  svn_path_add_component_nts (s_fullpath, source_entry);

  /* Get the base revision for the entry from the hash. */
  base_revision = get_path_revision (c->source_root, s_fullpath->data, pool);

  namebuf = svn_stringbuf_create (target_entry, pool);
  if (is_dir)
    {
      void *subdir_baton;

      SVN_ERR (c->editor->open_directory 
               (namebuf, dir_baton, base_revision, &subdir_baton));
      SVN_ERR (delta_dirs (c, subdir_baton,
                           s_fullpath->data, t_fullpath->data,
                           pool));
      SVN_ERR (c->editor->close_directory (subdir_baton));
    }
  else
    {
      void *file_baton;

      SVN_ERR (c->editor->open_file 
               (namebuf, dir_baton, base_revision, &file_baton));
      SVN_ERR (delta_files (c, file_baton,
                            s_fullpath->data, t_fullpath->data,
                            pool));
      SVN_ERR (c->editor->close_file (file_baton));
    }

  return SVN_NO_ERROR;
}


/* Emit deltas to turn SOURCE_PATH into TARGET_PATH.  Assume that
   DIR_BATON represents the directory we're constructing to the editor
   in the context C.  */
static svn_error_t *
delta_dirs (struct context *c, 
            void *dir_baton,
            const char *source_path, 
            const char *target_path,
            apr_pool_t *pool)
{
  apr_hash_t *s_entries = 0, *t_entries = 0;
  apr_hash_index_t *hi;
  apr_pool_t *subpool;

  /* Compare the property lists.  */
  SVN_ERR (delta_proplists (c, source_path, target_path,
                            change_dir_prop, dir_baton, pool));

  /* Get the list of entries in each of source and target.  */
  if (target_path)
    {
      SVN_ERR (svn_fs_dir_entries (&t_entries, c->target_root,
                                   target_path, pool));
    }
  else
    {
      /* Return a viscious error. */
      abort();
    }

  if (source_path)
    {
      SVN_ERR (svn_fs_dir_entries (&s_entries, c->source_root,
                                   source_path, pool));
    }

  /* Make a subpool for local allocations. */
  subpool = svn_pool_create (pool);

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
  for (hi = apr_hash_first (pool, t_entries); hi; hi = apr_hash_next (hi))
    {
      const svn_fs_dirent_t *s_entry, *t_entry;
      const void *key;
      void *val;
      apr_ssize_t klen;
      svn_stringbuf_t *t_fullpath =
        svn_stringbuf_create (target_path, subpool);
          
      /* KEY is the entry name in target, VAL the dirent */
      apr_hash_this (hi, &key, &klen, &val);
      t_entry = val;

      svn_path_add_component_nts (t_fullpath, t_entry->name);

      /* Can we find something with the same name in the source
         entries hash? */
      if (s_entries 
          && ((s_entry = apr_hash_get (s_entries, key, klen)) != 0))
        {
          int distance;
          int is_dir;
          SVN_ERR (svn_fs_is_dir (&is_dir, c->target_root, 
                                  t_fullpath->data, subpool));

          if (c->recurse || !is_dir)
            {

              /* Check the distance between the ids.  

                 0 means they are the same id, and this is a noop.

                 -1 means they are unrelated, so try to find an ancestor
                 elsewhere in the directory.  Theoretically, using an
                 ancestor as a baseline will reduce the size of the deltas.

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
                  SVN_ERR (delete (c, dir_baton, t_entry->name, subpool));
                  SVN_ERR (add_file_or_dir (c, dir_baton, target_path, 
                                            t_entry->name, subpool));
                }
              else
                {
                  SVN_ERR (replace_file_or_dir (c, dir_baton, source_path,
                                                s_entry->name, target_path,
                                                t_entry->name, subpool));
                }
            }

          /*  Remove the entry from the source_hash. */
          apr_hash_set (s_entries, key, APR_HASH_KEY_STRING, NULL);
        }            
      else
        {
          int is_dir;
          SVN_ERR (svn_fs_is_dir (&is_dir, c->target_root,
                                  t_fullpath->data, subpool));

          if (c->recurse || (! is_dir))
            {
              SVN_ERR (add_file_or_dir (c, dir_baton, target_path, 
                                        t_entry->name, subpool));
            } 
        }

      /* Clear out our subpool for the next iteration... */
      svn_pool_clear (subpool);
    }

  /* All that is left in the source entries hash are things that need
     to be deleted.  Delete them.  */
  if (s_entries)
    {
      for (hi = apr_hash_first (pool, s_entries); hi; hi = apr_hash_next (hi))
        {
          const svn_fs_dirent_t *s_entry;
          const void *key;
          void *val;
          apr_ssize_t klen;
          svn_stringbuf_t *s_fullpath = 
            svn_stringbuf_create (source_path, subpool);
          int is_dir;
          
          /* KEY is the entry name in source, VAL the dirent */
          apr_hash_this (hi, &key, &klen, &val);
          s_entry = val;
          svn_path_add_component_nts (s_fullpath, s_entry->name);

          /* Do we actually want to delete the dir if we're non-recursive? */
          SVN_ERR (svn_fs_is_dir (&is_dir, c->source_root, 
                                  s_fullpath->data, subpool));

          if (c->recurse || (! is_dir))
            {
              SVN_ERR (delete (c, dir_baton, s_entry->name, subpool));
            }

          /* Clear out our subpool for the next iteration... */
          svn_pool_clear (subpool);
        }
    }

  /* Destroy local allocation subpool. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}




/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
