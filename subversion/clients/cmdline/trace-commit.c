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
#include <assert.h>

#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_path.h"
#include "svn_string.h"

#include "cl.h"



struct edit_baton
{
  apr_pool_t *pool;
  svn_stringbuf_t *initial_path;
};


static const int svn_cl__item_added = 0;
static const int svn_cl__item_added_binary = 0;
static const int svn_cl__item_deleted = 0;
static const int svn_cl__item_replaced = 0;
static const int svn_cl__item_replaced_binary = 0;

#define ITEM_ADDED (&svn_cl__item_added)
#define ITEM_ADDED_BINARY (&svn_cl__item_added_binary)
#define ITEM_DELETED (&svn_cl__item_deleted)
#define ITEM_REPLACED (&svn_cl__item_replaced)
#define ITEM_REPLACED_BINARY (&svn_cl__item_replaced_binary)


struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent_dir_baton;
  svn_stringbuf_t *path;
  svn_boolean_t added;
  svn_boolean_t prop_changed;
  apr_hash_t *added_or_deleted;
  apr_pool_t *subpool;
  int ref_count;
};


struct file_baton
{
  struct dir_baton *parent_dir_baton;
  svn_stringbuf_t *path;
  svn_boolean_t added;
  svn_boolean_t text_changed;
  svn_boolean_t prop_changed;
  svn_boolean_t binary;
};


static svn_error_t *
decrement_dir_ref_count (struct dir_baton *db)
{
  if (db == NULL)
    return SVN_NO_ERROR;

  db->ref_count--;

  /* Check to see if *any* child batons still depend on this
     directory's pool. */
  if (db->ref_count == 0)
    {
      struct dir_baton *dbparent = db->parent_dir_baton;
      apr_hash_index_t *hi;

      for (hi = apr_hash_first (db->subpool, db->added_or_deleted); 
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
          else
            assert(0); /* this should never happen */

          printf (pattern, (const char *)key);
        }

      /* Destroy all memory used by this baton, including the baton
         itself! */
      svn_pool_destroy (db->subpool);
      
      /* Tell your parent that you're gone. */
      SVN_ERR (decrement_dir_ref_count (dbparent));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
open_root (void *edit_baton, svn_revnum_t base_revision, void **root_baton)
{
  apr_pool_t *subpool;
  struct edit_baton *eb = edit_baton;
  struct dir_baton *rb;

  subpool = svn_pool_create (eb->pool);;
  rb = apr_pcalloc (subpool, sizeof (*rb));
  rb->edit_baton = eb;
  rb->parent_dir_baton = NULL;
  rb->path = eb->initial_path;
  rb->subpool = subpool;
  rb->ref_count = 1;

  rb->added_or_deleted = apr_hash_make (subpool);

  *root_baton = rb;

  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry (svn_stringbuf_t *name, svn_revnum_t revision, void *parent_baton)
{
  struct dir_baton *d = parent_baton;
  svn_stringbuf_t *printable_name = 
    svn_stringbuf_dup (d->path, d->edit_baton->pool);
  void *vp;

  svn_path_add_component (printable_name, name);

  vp = apr_hash_get (d->added_or_deleted, printable_name->data, 
                     printable_name->len);

  if (vp == ITEM_ADDED)
    vp = (void *)ITEM_REPLACED;
  else if (vp == ITEM_ADDED_BINARY)
    vp = (void *)ITEM_REPLACED_BINARY;
  else
    vp = (void *)ITEM_DELETED;

  apr_hash_set (d->added_or_deleted, printable_name->data, printable_name->len, 
                vp);

  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (svn_stringbuf_t *name,
               void *parent_baton,
               svn_stringbuf_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **child_baton)
{
  apr_pool_t *subpool;
  struct dir_baton *parent_d = parent_baton;
  struct dir_baton *child_d;
  void *vp;

  subpool = svn_pool_create (parent_d->edit_baton->pool);
  child_d = apr_pcalloc (subpool, sizeof (*child_d));
  child_d->edit_baton = parent_d->edit_baton;
  child_d->parent_dir_baton = parent_d;
  child_d->path = svn_stringbuf_dup (parent_d->path, 
                                     child_d->edit_baton->pool);
  svn_path_add_component (child_d->path, name);
  child_d->added = TRUE;
  child_d->subpool = subpool;
  child_d->ref_count = 1;
  parent_d->ref_count++;

  child_d->added_or_deleted = apr_hash_make (subpool);

  vp = apr_hash_get (parent_d->added_or_deleted, child_d->path->data, 
                     child_d->path->len);
  if (vp == ITEM_DELETED)
    apr_hash_set (parent_d->added_or_deleted, child_d->path->data, 
                  child_d->path->len, ITEM_REPLACED);
  else
    apr_hash_set (parent_d->added_or_deleted, child_d->path->data, 
                  child_d->path->len, ITEM_ADDED);

  *child_baton = child_d;

  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory (svn_stringbuf_t *name,
                void *parent_baton,
                svn_revnum_t base_revision,
                void **child_baton)
{
  apr_pool_t *subpool;
  struct dir_baton *parent_d = parent_baton;
  struct dir_baton *child_d;

  subpool = svn_pool_create (parent_d->edit_baton->pool);
  child_d = apr_pcalloc (subpool, sizeof (*child_d));
  child_d->edit_baton = parent_d->edit_baton;
  child_d->parent_dir_baton = parent_d;
  child_d->path = svn_stringbuf_dup (parent_d->path, 
                                     child_d->edit_baton->pool);
  svn_path_add_component (child_d->path, name);
  child_d->subpool = subpool;
  child_d->ref_count = 1;
  parent_d->ref_count++;

  child_d->added_or_deleted = apr_hash_make (subpool);

  *child_baton = child_d;

  /* Don't print anything for a directory open -- this event is
     implied by printing events beneath it. */

  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
  struct dir_baton *db = dir_baton;

  if (db->prop_changed)
    printf ("Sending         %s\n", db->path->data); 

  decrement_dir_ref_count (db);
  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *fb = file_baton;

  if (fb->added)
    {
      void *vp = apr_hash_get (fb->parent_dir_baton->added_or_deleted, 
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

      apr_hash_set (fb->parent_dir_baton->added_or_deleted, 
                    fb->path->data, fb->path->len, vp);

    }
  else
    printf ("Sending         %s\n", fb->path->data);

  decrement_dir_ref_count (fb->parent_dir_baton);
  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
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
  *handler = window_handler;
  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (svn_stringbuf_t *name,
          void *parent_baton,
          svn_stringbuf_t *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          void **file_baton)
{
  struct dir_baton *parent_d = parent_baton;
  struct file_baton *child_fb
    = apr_pcalloc (parent_d->subpool, sizeof (*child_fb));

  child_fb->parent_dir_baton = parent_d;
  child_fb->path = svn_stringbuf_dup (parent_d->path, 
                                      parent_d->edit_baton->pool);
  svn_path_add_component (child_fb->path, name);
  child_fb->added = TRUE;
  child_fb->binary = FALSE;
  *file_baton = child_fb;

  parent_d->ref_count++;

  return SVN_NO_ERROR;
}


static svn_error_t *
open_file (svn_stringbuf_t *name,
           void *parent_baton,
           svn_revnum_t base_revision,
           void **file_baton)
{
  struct dir_baton *parent_d = parent_baton;
  struct file_baton *child_fb
    = apr_pcalloc (parent_d->subpool, sizeof (*child_fb));

  child_fb->parent_dir_baton = parent_d;
  child_fb->path = svn_stringbuf_dup (parent_d->path, 
                                      parent_d->edit_baton->pool);
  svn_path_add_component (child_fb->path, name);

  *file_baton = child_fb;

  parent_d->ref_count++;

  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_stringbuf_t *name,
                  svn_stringbuf_t *value)
{
  struct file_baton *fb = file_baton;

  fb->prop_changed = TRUE;

  /* If the mime-type property is being set to non-NULL, and something
     that doesn't start with 'text/', call this a binary file. */
  if ((! strcmp (name->data, SVN_PROP_MIME_TYPE))
      && value && (strncmp (value->data, "text/", 5)))
    fb->binary = TRUE;

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *parent_baton,
                 svn_stringbuf_t *name,
                 svn_stringbuf_t *value)
{
  struct dir_baton *d = parent_baton;
  d->prop_changed = TRUE;
  return SVN_NO_ERROR;
}




svn_error_t *
svn_cl__get_trace_commit_editor (const svn_delta_edit_fns_t **editor,
                                 void **edit_baton,
                                 svn_stringbuf_t *initial_path,
                                 apr_pool_t *pool)
{
  /* Allocate an edit baton to be stored in every directory baton. */
  struct edit_baton *eb = apr_pcalloc (pool, sizeof (*eb));
  svn_delta_edit_fns_t *trace_editor = svn_delta_old_default_editor (pool);

  /* kff todo: hmm, that's a bit of a kluge now, isn't it? */
  if ((initial_path == NULL) || (initial_path->len == 0))
    initial_path = svn_stringbuf_create (".", pool);

  /* Set up the edit context. */
  eb->pool = svn_pool_create (pool);
  eb->initial_path = svn_stringbuf_dup (initial_path, eb->pool);

  /* Set up the editor. */
  trace_editor->open_root = open_root;
  trace_editor->delete_entry = delete_entry;
  trace_editor->add_directory = add_directory;
  trace_editor->open_directory = open_directory;
  trace_editor->change_dir_prop = change_dir_prop;
  trace_editor->close_directory = close_directory;
  trace_editor->add_file = add_file;
  trace_editor->open_file = open_file;
  trace_editor->apply_textdelta = apply_textdelta;
  trace_editor->change_file_prop = change_file_prop;
  trace_editor->close_file = close_file;
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
