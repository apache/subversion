/*
 * trace-commit.c : an editor implementation that prints a commit-in-progress
 *                  (when composed to follow after the commit-editor)
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

/* ==================================================================== */



/*** Includes. ***/


#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_path.h"
#include "svn_string.h"

#include "cl.h"

/* The commit process is a complex one that takes advantage of the
   notion of "post-fix text deltas" offered by the editor interface.
   That is, all textual modifications to files can occur after the
   rest of the entire tree changes have been described by the editor
   driver.  This rather complicates the trace output process, which
   would prefer to print only a single descriptive line of text for
   each item modified by the commit, and would like to preserve a sort
   of visual "feeling" of tree traversal in an ordered manner with
   that output. 

   To accomplish these goals, we will limit the output process to
   places where we can know for certain that we are finished
   processing a given file or directory.

   For files, we are not finished with the description of the
   committed changes until the close_file() call.

   For directories, we are not finished until

     - all the entries of the directory have also been finished, and 
     - close_directory() has been called.

   Luckily, while not all of the entries of a directory are guaranteed
   to be finished prior to the close_directory() call, enough
   information can be gathered from other calls required to be made
   before the close_directory() call:
 
     - add_file() must be called before close_directory().
     - open_file() must be called before close_directory().
     - change_file_prop() must be called after add/open_file(), and
       before close_directory().
*/


static const int svn_cl__item_modified = 0;
static const int svn_cl__item_added = 0;
static const int svn_cl__item_added_binary = 0;
static const int svn_cl__item_deleted = 0;
static const int svn_cl__item_replaced = 0;
static const int svn_cl__item_replaced_binary = 0;

#define ITEM_MODIFIED (&svn_cl__item_modified)
#define ITEM_ADDED (&svn_cl__item_added)
#define ITEM_ADDED_BINARY (&svn_cl__item_added_binary)
#define ITEM_DELETED (&svn_cl__item_deleted)
#define ITEM_REPLACED (&svn_cl__item_replaced)
#define ITEM_REPLACED_BINARY (&svn_cl__item_replaced_binary)


struct edit_baton
{
  svn_stringbuf_t *path;
  apr_pool_t *pool;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent_dir_baton;
  svn_stringbuf_t *path;
  svn_boolean_t prop_changed;
  apr_hash_t *entrymods;
  apr_pool_t *pool;
};


struct file_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent_dir_baton;
  svn_stringbuf_t *path;
};



static struct dir_baton *
make_dir_baton (const char *path,
                void *parent_baton,
                void *edit_baton,
                apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = edit_baton;
  struct dir_baton *new_db = apr_pcalloc (pool, sizeof (*new_db));
  svn_stringbuf_t *full_path = svn_stringbuf_dup (eb->path, pool);
  
  /* Don't give me a path without a parent baton! */
  if (path && (! pb))
    abort();

  /* Construct the "full" path of this node. */
  if (pb)
    svn_path_add_component_nts (full_path, path);

  /* Finish populating the baton. */
  new_db->path = full_path;
  new_db->edit_baton = eb;
  new_db->parent_dir_baton = pb;
  new_db->entrymods = apr_hash_make (pool);
  new_db->pool = pool;
  return new_db;
}


static struct file_baton *
make_file_baton (const char *path,
                 void *parent_baton,
                 void *edit_baton,
                 apr_pool_t *pool)
{
  struct file_baton *new_fb = apr_pcalloc (pool, sizeof (*new_fb));
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = edit_baton;
  svn_stringbuf_t *full_path = svn_stringbuf_dup (eb->path, pool);

  /* Constuct the "full" path to this node. */
  svn_path_add_component_nts (full_path, path);

  /* Finish populating the baton. */
  new_fb->path = full_path;
  new_fb->edit_baton = eb;
  new_fb->parent_dir_baton = pb;
  return new_fb;
}


static svn_error_t *
open_root (void *edit_baton, 
           svn_revnum_t base_revision, 
           apr_pool_t *pool,
           void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  *root_baton = make_dir_baton (NULL, NULL, eb, pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry (const char *path,
              svn_revnum_t revision, 
              void *parent_baton,
              apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  void *vp;

  /* Construct the "full" path of this node in the parent directory's
     pool (the same pool that contains the hash). */
  svn_stringbuf_t *full_path = svn_stringbuf_dup (eb->path, pb->pool);
  svn_path_add_component_nts (full_path, path);

  /* Let the parent directory know that one of its entries has been
     deleted.  If this thing was just added, this is really a noop. */
  vp = apr_hash_get (pb->entrymods, full_path->data, full_path->len);
  if (vp == NULL)
    vp = (void *)ITEM_DELETED;
  if ((vp == ITEM_ADDED) || (vp == ITEM_ADDED_BINARY))
    vp = NULL;
  apr_hash_set (pb->entrymods, full_path->data, full_path->len, vp);

  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (const char *path,
               void *parent_baton,
               const char *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *new_db = make_dir_baton (path, pb, eb, pool);
  svn_stringbuf_t *full_path;
  void *vp;
  
  /* Copy the path into the parent directorie's pool (where its hash
     lives) and let the parent know that this was added (or replaced) */
  full_path = svn_stringbuf_dup (new_db->path, pb->pool);

  /* Let the parent directory know that one of its entries has been
     deleted.  If this thing was just added, this is really a noop. */
  vp = apr_hash_get (pb->entrymods, full_path->data, full_path->len);
  if (vp == NULL)
    vp = (void *)ITEM_ADDED;
  if (vp == ITEM_DELETED)
    vp = (void *)ITEM_REPLACED;
  apr_hash_set (pb->entrymods, full_path->data, full_path->len, vp);

  *child_baton = new_db;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory (const char *path,
                void *parent_baton,
                svn_revnum_t base_revision,
                apr_pool_t *pool,
                void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  *child_baton = make_dir_baton (path, pb, eb, pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
  struct dir_baton *db = dir_baton;
  struct dir_baton *pb = db->parent_dir_baton;
  apr_hash_index_t *hi;
  void *vp = NULL;

  /* See if there is an entry in the parent's hash for this
     directory. */
  if (pb)
    vp = apr_hash_get (pb->entrymods, db->path->data, db->path->len);

  /* If this item was added to its parent's hash, print such.  Else,
     if it has propchanges, print that.  Otherwise, it should have
     just been 'open'ed, and that's not interesting enough to print.  */
  if (vp == ITEM_ADDED)
    printf ("Adding          %s\n", db->path->data);
  else if (db->prop_changed)
    printf ("Sending         %s\n", db->path->data); 

  /* Now remove this from the parent's hash. */
  if (vp)
    apr_hash_set (pb->entrymods, db->path->data, db->path->len, NULL);

  /* For each modified entry of this directory, print out a
     description of those mods. */
  for (hi = apr_hash_first (db->pool, db->entrymods); 
       hi; 
       hi = apr_hash_next (hi))
    {
      const char *pattern;
      const void *key;
      void *val;
      
      apr_hash_this (hi, &key, NULL, &val);
      
      if (val == ITEM_REPLACED)
        pattern = "Replacing       %s\n";
      else if (val == ITEM_REPLACED_BINARY)
        pattern = "Replacing (bin) %s\n";
      else if (val == ITEM_DELETED)
        pattern = "Deleting        %s\n";
      else if (val == ITEM_ADDED)
        pattern = "Adding          %s\n";
      else if (val == ITEM_ADDED_BINARY)
        pattern = "Adding   (bin)  %s\n";
      else if (val == ITEM_MODIFIED)
        pattern = "Sending         %s\n";
      else
        abort(); /* this should never happen */
      
      printf (pattern, (const char *)key);
    }

  return SVN_NO_ERROR;
}


#if 0
static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *fb = file_baton;

  if (fb->added)
    {
      void *vp = apr_hash_get (fb->parent_dir_baton->entrymods, 
                               fb->path->data, fb->path->len);
      if (vp == NULL)
        {
          if (fb->binary)
            vp = (void *)ITEM_ADDED_BINARY;
          else
            vp = (void *)ITEM_ADDED;
        }
      else
        {
          if (fb->binary)
            vp = (void *)ITEM_REPLACED_BINARY;
          else
            vp = (void*)ITEM_REPLACED;
        }

      apr_hash_set (fb->parent_dir_baton->entrymods, 
                    fb->path->data, fb->path->len, vp);

    }
  else
    printf ("Sending         %s\n", fb->path->data);

  decrement_dir_ref_count (fb->parent_dir_baton);
  return SVN_NO_ERROR;
}


static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *handler_pair)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct file_baton *fb = file_baton;
  fb->text_changed = TRUE;
  *handler = NULL;
  *handler_baton = NULL;
  return SVN_NO_ERROR;
}
#endif


static svn_error_t *
add_file (const char *path,
          void *parent_baton,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *new_fb = make_file_baton (path, pb, eb, pool);
  void *vp;

  /* Copy the path into the parent's pool (where its hash lives). */
  svn_stringbuf_t *full_path = svn_stringbuf_dup (new_fb->path, pb->pool);
  
  /* Tell the parent directory that one of its children has been
     added (or replaced). */
  vp = apr_hash_get (pb->entrymods, full_path->data, full_path->len);
  if (vp == NULL)
    vp = (void *)ITEM_ADDED;
  if (vp == ITEM_DELETED)
    vp = (void *)ITEM_REPLACED;
  apr_hash_set (pb->entrymods, full_path->data, full_path->len, vp);

  *file_baton = new_fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_file (const char *path,
           void *parent_baton,
           svn_revnum_t base_revision,
           apr_pool_t *pool,
           void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *new_fb = make_file_baton (path, pb, eb, pool);

  /* Copy the path into the parent's pool (where its hash lives). */
  svn_stringbuf_t *path_copy = svn_stringbuf_dup (new_fb->path, pb->pool);

  /* Tell the parent directory that one of its children has been
     added. */
  apr_hash_set (pb->entrymods, path_copy->data, path_copy->len, ITEM_MODIFIED);

  *file_baton = new_fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  const char *name,
                  const svn_string_t *value,
                  apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct dir_baton *pb = fb->parent_dir_baton;

  /* If the mime-type property is being set to non-NULL, and something
     that doesn't start with 'text/', call this a binary file. */
  if ((! strcmp (name, SVN_PROP_MIME_TYPE))
      && value && (strncmp (value->data, "text/", 5)))
    {
      void *vp = apr_hash_get (pb->entrymods, fb->path->data, fb->path->len);
      if (vp == ITEM_ADDED)
        vp = (void *)ITEM_ADDED_BINARY;
      else if (vp == ITEM_MODIFIED)
        ; /* do nothing. */
      else
        abort(); /* this shouldn't happen. */
      apr_hash_set (pb->entrymods, fb->path->data, fb->path->len, vp);
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  db->prop_changed = TRUE;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;
  svn_pool_destroy (eb->pool);
  return SVN_NO_ERROR;
}




svn_error_t *
svn_cl__get_trace_commit_editor (const svn_delta_editor_t **editor,
                                 void **edit_baton,
                                 svn_stringbuf_t *initial_path,
                                 apr_pool_t *pool)
{
  /* Allocate an edit baton to be stored in every directory baton. */
  svn_delta_editor_t *trace_editor = svn_delta_default_editor (pool);
  apr_pool_t *subpool = svn_pool_create (pool);
  struct edit_baton *eb = apr_pcalloc (subpool, sizeof (*eb));

  /* Set up the edit context. */
  eb->pool = subpool;
  if (initial_path && (! svn_path_is_empty (initial_path)))
    eb->path = svn_stringbuf_dup (initial_path, subpool);
  else
    eb->path = svn_stringbuf_create (".", subpool);

  /* Set up the editor. */
  trace_editor->open_root = open_root;
  trace_editor->delete_entry = delete_entry;
  trace_editor->add_directory = add_directory;
  trace_editor->open_directory = open_directory;
  trace_editor->change_dir_prop = change_dir_prop;
  trace_editor->close_directory = close_directory;
  trace_editor->add_file = add_file;
  trace_editor->open_file = open_file;
  trace_editor->change_file_prop = change_file_prop;
#if 0
  trace_editor->apply_textdelta = apply_textdelta;
  trace_editor->close_file = close_file;
#endif
  trace_editor->close_edit = close_edit;

  *edit_baton = eb;
  *editor = trace_editor;
  
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
