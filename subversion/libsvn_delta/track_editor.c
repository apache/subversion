/*
 * track_editor.c : editor implementation which tracks committed targets
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

#include "svn_path.h"
#include "svn_delta.h"
#include "svn_ra.h"
#include "apr_tables.h"

/* Philosophy:  how does the track editor know when to store a path as
   a "committed target"?

   Here is the logic used by the commit-editor-driver
   (svn_wc_crawl_local_mods): 
   
   Store a path if:

      - an entry is marked for addition
      - an entry is marked for deletion
      - a file's text or props are modified
      - a dir's props are modified.

   Since the track editor will be driven by crawl_local_mods, it needs
   to line itself up along these semantics.  This means storing a
   target inside:

      - add_file & add_dir
      - delete_entry
      - apply_textdelta or change_file_prop
      - change_dir_prop

 */




/* ------------------------------------------------------------------- */

/*** The editor batons ***/
struct edit_baton
{
  apr_pool_t *pool;
  svn_stringbuf_t *initial_path;

  /* An already-intitialized array, ready to store (svn_stringbuf_t *)
     objects */
  apr_array_header_t *array;

  /* These are defined only if the caller wants close_edit() to bump
     revisions */
  svn_revnum_t new_rev;
  svn_error_t *(*bump_func) (void *baton, svn_stringbuf_t *path,
                             svn_revnum_t new_rev);
  void *bump_baton;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent_dir_baton;
  svn_stringbuf_t *path;
  
  /* Has this path been stored in the array already? */
  int stored;
};


struct file_baton
{
  struct dir_baton *parent_dir_baton;
  svn_stringbuf_t *path;

  /* Has this path been stored in the array already? */
  int stored;
};



/*** Helpers ***/

/* Store PATH in EB's array. */
static void
store_path (svn_stringbuf_t *path, struct edit_baton *eb)
{
  svn_stringbuf_t **receiver;
  
  receiver = (svn_stringbuf_t **) apr_array_push (eb->array);
  *receiver = path;
}




/*** the anonymous editor functions ***/

static svn_error_t *
replace_root (void *edit_baton,
              svn_revnum_t base_revision,
              void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *rb = apr_pcalloc (eb->pool, sizeof (*rb));

  rb->edit_baton = eb;
  rb->parent_dir_baton = NULL;
  rb->path = eb->initial_path;

  *root_baton = rb;

  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (svn_stringbuf_t *name,
               void *parent_baton,
               svn_stringbuf_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **child_baton)
{
  struct dir_baton *parent_d = parent_baton;
  struct dir_baton *child_d
    = apr_pcalloc (parent_d->edit_baton->pool, sizeof (*child_d));

  child_d->edit_baton = parent_d->edit_baton;
  child_d->parent_dir_baton = parent_d;
  child_d->path = svn_stringbuf_dup (parent_d->path, child_d->edit_baton->pool);
  svn_path_add_component (child_d->path, name, svn_path_local_style);

  store_path (child_d->path, parent_d->edit_baton);
  child_d->stored = TRUE;

  *child_baton = child_d;

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_directory (svn_stringbuf_t *name,
                   void *parent_baton,
                   svn_revnum_t base_revision,
                   void **child_baton)
{
  struct dir_baton *parent_d = parent_baton;
  struct dir_baton *child_d
    = apr_pcalloc (parent_d->edit_baton->pool, sizeof (*child_d));

  child_d->edit_baton = parent_d->edit_baton;
  child_d->parent_dir_baton = parent_d;
  child_d->path = svn_stringbuf_dup (parent_d->path, child_d->edit_baton->pool);
  svn_path_add_component (child_d->path, name, svn_path_local_style);

  *child_baton = child_d;

  return SVN_NO_ERROR;
}




static svn_error_t *
add_file (svn_stringbuf_t *name,
          void *parent_baton,
          svn_stringbuf_t *copy_path,
          svn_revnum_t copy_revision,
          void **file_baton)
{
  struct dir_baton *parent_d = parent_baton;
  struct file_baton *child_fb
    = apr_pcalloc (parent_d->edit_baton->pool, sizeof (*child_fb));

  child_fb->parent_dir_baton = parent_d;
  child_fb->path = svn_stringbuf_dup (parent_d->path, parent_d->edit_baton->pool);
  svn_path_add_component (child_fb->path, name, svn_path_local_style);

  store_path (child_fb->path, parent_d->edit_baton);
  child_fb->stored = TRUE;

  *file_baton = child_fb;

  return SVN_NO_ERROR;
}


static svn_error_t *
replace_file (svn_stringbuf_t *name,
              void *parent_baton,
              svn_revnum_t base_revision,
              void **file_baton)
{
  struct dir_baton *parent_d = parent_baton;
  struct file_baton *child_fb
    = apr_pcalloc (parent_d->edit_baton->pool, sizeof (*child_fb));

  child_fb->parent_dir_baton = parent_d;
  child_fb->path = svn_stringbuf_dup (parent_d->path, parent_d->edit_baton->pool);
  svn_path_add_component (child_fb->path, name, svn_path_local_style);

  *file_baton = child_fb;

  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry (svn_stringbuf_t *name,
              void *parent_baton)
{
  struct dir_baton *parent_d = parent_baton;
  svn_stringbuf_t *path = svn_stringbuf_dup (parent_d->path,
                                       parent_d->edit_baton->pool);
  svn_path_add_component (path, name, svn_path_local_style);
  
  store_path (path, parent_d->edit_baton);  

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_stringbuf_t *name,
                 svn_stringbuf_t *value)
{
  struct dir_baton *db = dir_baton;

  if (! db->stored)
    {
      store_path (db->path, db->edit_baton);  
      db->stored = TRUE;
    }
  
  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_stringbuf_t *name,
                  svn_stringbuf_t *value)
{
  struct file_baton *fb = file_baton;

  if (! fb->stored)
    {
      store_path (fb->path, fb->parent_dir_baton->edit_baton);  
      fb->stored = TRUE;
    }
  
  return SVN_NO_ERROR;
}


static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *handler_pair)
{
  /* No-op, but required for proper editor composition. */
  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta (void *file_baton, 
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct file_baton *fb = file_baton;
  
  if (! fb->stored)
    {
      store_path (fb->path, fb->parent_dir_baton->edit_baton);  
      fb->stored = TRUE;
    }
  
  *handler = window_handler;
  *handler_baton = NULL;

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  int i;
  struct edit_baton *eb = edit_baton;

  /* Bump all targets if the caller wants us to. */
  if (SVN_IS_VALID_REVNUM(eb->new_rev) && eb->bump_func)
    for (i = 0; i < eb->array->nelts; i++)
      {
        svn_stringbuf_t *target;
        target = (((svn_stringbuf_t **)(eb->array)->elts)[i]);
        SVN_ERR (eb->bump_func (eb->bump_baton, target, eb->new_rev));
      }
  
  return SVN_NO_ERROR;
}



/*** exported routine ***/

svn_error_t *
svn_delta_get_commit_track_editor (svn_delta_edit_fns_t **editor,
                                   void **edit_baton,
                                   apr_pool_t *pool,
                                   apr_array_header_t *array,
                                   svn_revnum_t new_rev,
                                   svn_error_t *(*bump_func) 
                                     (void *baton,
                                      svn_stringbuf_t *path,
                                      svn_revnum_t new_rev),
                                   void *bump_baton)
{
  struct edit_baton *eb = apr_pcalloc (pool, sizeof (*eb));
  svn_delta_edit_fns_t *track_editor = svn_delta_default_editor (pool);

  /* Set up the editor.  These functions are no-ops, so the default
     editor's implementations are used:

        set_target_revision
        close_directory
        window_handler
     
  */
  track_editor->replace_root = replace_root;
  track_editor->add_directory = add_directory;
  track_editor->replace_directory = replace_directory;
  track_editor->add_file = add_file;
  track_editor->replace_file = replace_file;
  track_editor->delete_entry = delete_entry;
  track_editor->change_dir_prop = change_dir_prop;
  track_editor->change_file_prop = change_file_prop;
  track_editor->apply_textdelta = apply_textdelta;
  track_editor->close_edit = close_edit;

  /* Set up the edit baton. */
  eb->pool = pool;
  eb->initial_path = svn_stringbuf_create ("", pool);
  eb->array = array;
  eb->new_rev = new_rev;
  eb->bump_func = bump_func;
  eb->bump_baton = bump_baton;

  *editor = track_editor;
  *edit_baton = eb;

  return SVN_NO_ERROR;
}



/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */





