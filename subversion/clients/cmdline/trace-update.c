/*
 * trace-update.c : an editor implementation that prints status characters
 *                  (when composed to follow after the update-editor)
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
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "svn_string.h"
#include "cl.h"



struct edit_baton
{
  svn_stringbuf_t *path;
  apr_pool_t *pool;
  svn_revnum_t target_revision;
  svn_boolean_t is_checkout;
  svn_boolean_t suppress_final_line;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent_dir_baton;
  svn_stringbuf_t *path;
  svn_boolean_t prop_changed;
};


struct file_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent_dir_baton;
  svn_stringbuf_t *path;
  svn_boolean_t added;
  svn_boolean_t text_changed;
  svn_boolean_t prop_changed;
};


static struct dir_baton *
make_dir_baton (const char *path,
                void *edit_baton,
                void *parent_dir_baton,
                apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *pb = parent_dir_baton;
  struct dir_baton *new_db = apr_pcalloc (pool, sizeof (*new_db));
  svn_stringbuf_t *full_path = svn_stringbuf_dup (eb->path, pool);

  /* A path relative to nothing?  I don't think so. */
  if (path && (! pb))
    abort();

  /* Construct the full path of this node. */
  if (pb)
    svn_path_add_component_nts (full_path, path);

  new_db->edit_baton = eb;
  new_db->parent_dir_baton = pb;
  new_db->path = full_path;
  new_db->prop_changed = FALSE;

  return new_db;
}


static struct file_baton *
make_file_baton (const char *path,
                 void *parent_dir_baton,
                 svn_boolean_t added,
                 apr_pool_t *pool)
{
  struct dir_baton *pb = parent_dir_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *new_fb = apr_pcalloc (pool, sizeof (*new_fb));
  svn_stringbuf_t *full_path = svn_stringbuf_dup (eb->path, pool);

  /* Construct the full path of this node. */
  svn_path_add_component_nts (full_path, path);

  new_fb->edit_baton = eb;
  new_fb->parent_dir_baton = pb;
  new_fb->path = full_path;
  new_fb->added = added;
  new_fb->text_changed = FALSE;
  new_fb->prop_changed = FALSE;

  return new_fb;
}

static svn_error_t *
open_root (void *edit_baton, 
           svn_revnum_t base_revision, 
           apr_pool_t *pool,
           void **root_baton)
{
  *root_baton = make_dir_baton (NULL, edit_baton, NULL, pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
set_target_revision (void *edit_baton,
                     svn_revnum_t target_revision)
{
  struct edit_baton *eb = edit_baton;
  eb->target_revision = target_revision;
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
  printf ("D  %s\n", svn_path_join (eb->path->data, path, pool));
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
  struct dir_baton *new_db = make_dir_baton (path, eb, pb, pool);

  printf ("A  %s\n", new_db->path->data);
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
  struct dir_baton *new_db = make_dir_baton (path, eb, pb, pool);

  /* Don't print anything for a directory open -- this event is
     implied by printing events beneath it. */

  *child_baton = new_db;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;
  char statchar_buf[3] = "_ ";

  if (db->prop_changed)
    {
      /* First, check for conflicted state. */
      svn_wc_entry_t *entry;
      svn_boolean_t merged, tc, pc;
      apr_pool_t *subpool = svn_pool_create (eb->pool);

      SVN_ERR (svn_wc_entry (&entry, db->path, subpool));
      SVN_ERR (svn_wc_conflicted_p (&tc, &pc, db->path, entry, subpool));
      if (! pc)
        SVN_ERR (svn_wc_props_modified_p (&merged, db->path, subpool));
      
      if (pc)
        statchar_buf[1] = 'C';
      else if (merged)
        statchar_buf[1] = 'G';
      else
        statchar_buf[1] = 'U';

      printf ("%s %s\n", statchar_buf, db->path->data);

      /* Destroy the subpool. */
      svn_pool_destroy (subpool);
    }
    
  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;
  char statchar_buf[3] = "_ ";

  if (fb->added)
    statchar_buf[0] = 'A';

  /* We need to check the state of the file now to see if it was
     merged or is in a state of conflict.  Believe it or not, this can
     be the case even when FB->ADDED is set.  */
  {
    /* First, check for conflicted state. */
    svn_wc_entry_t *entry;
    svn_boolean_t merged, tc, pc;
    apr_pool_t *subpool = svn_pool_create (eb->pool);
    svn_stringbuf_t *pdir = svn_stringbuf_dup (fb->path, subpool);
    svn_path_remove_component (pdir);

    SVN_ERR (svn_wc_entry (&entry, fb->path, subpool));
    SVN_ERR (svn_wc_conflicted_p (&tc, &pc, pdir, entry, subpool));
    if (fb->text_changed)
      {
        if (! tc)
          SVN_ERR (svn_wc_text_modified_p (&merged, fb->path, subpool));
        
        if (tc)
          statchar_buf[0] = 'C';
        else if (merged)
          statchar_buf[0] = 'G';
        else if (! fb->added)
          statchar_buf[0] = 'U';
      }
    if (fb->prop_changed)
      {
        if (! pc)
          SVN_ERR (svn_wc_props_modified_p (&merged, fb->path, subpool));
        
        if (pc)
          statchar_buf[1] = 'C';
        else if (merged)
          statchar_buf[1] = 'G';
        else if (! fb->added)
          statchar_buf[1] = 'U';
      }
    
    /* Destroy the subpool. */
    svn_pool_destroy (subpool);
  }

  printf ("%s %s\n", statchar_buf, fb->path->data);

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


static svn_error_t *
add_file (const char *path,
          void *parent_baton,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *new_fb = make_file_baton (path, pb, TRUE, pool);
  *file_baton = new_fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_file (const char *path,
           void *parent_baton,
           svn_revnum_t ancestor_revision,
           apr_pool_t *pool,
           void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *new_fb = make_file_baton (path, pb, FALSE, pool);
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
  if (svn_wc_is_normal_prop (name))
    fb->prop_changed = TRUE;
  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *parent_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct dir_baton *db = parent_baton;
  if (svn_wc_is_normal_prop (name))
    db->prop_changed = TRUE;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  struct edit_baton *eb = edit_baton;

  if (! eb->suppress_final_line)
    {
      if (eb->is_checkout)
        printf ("Checked out revision %" SVN_REVNUM_T_FMT ".\n",
                eb->target_revision);
      else
        printf ("Updated to revision %" SVN_REVNUM_T_FMT ".\n",
                eb->target_revision);
    }

  svn_pool_destroy (eb->pool);
  return SVN_NO_ERROR;
}



svn_error_t *
svn_cl__get_trace_update_editor (const svn_delta_editor_t **editor,
                                 void **edit_baton,
                                 svn_stringbuf_t *initial_path,
                                 svn_boolean_t is_checkout,
                                 svn_boolean_t suppress_final_line,
                                 apr_pool_t *pool)
{
  /* Allocate an edit baton to be stored in every directory baton.
     Set it up for the directory baton we create here, which is the
     root baton. */
  apr_pool_t *subpool = svn_pool_create (pool);
  struct edit_baton *eb = apr_pcalloc (subpool, sizeof (*eb));
  svn_delta_editor_t *trace_editor = svn_delta_default_editor (pool);

  /* Set up the edit context. */
  eb->pool = subpool;
  eb->path = svn_stringbuf_dup (initial_path, eb->pool);
  eb->target_revision = SVN_INVALID_REVNUM;
  eb->is_checkout = is_checkout;
  eb->suppress_final_line = suppress_final_line;

  /* Set up the editor. */
  trace_editor->open_root = open_root;
  trace_editor->set_target_revision = set_target_revision;
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
