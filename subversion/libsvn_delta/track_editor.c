/*
 * track_editor.c : editor implementation which tracks committed targets
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

#include "svn_path.h"
#include "svn_delta.h"
#include "svn_pools.h"
#include "svn_ra.h"
#include "apr_tables.h"

#define APR_WANT_STRFUNC
#include <apr_want.h>

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

  /* Stores paths as we declare them committed. */
  apr_hash_t *committed_targets;

  /* These are defined only if the caller wants close_edit() to bump
     revisions */
  svn_revnum_t new_rev;
  svn_delta_bump_func_t bump_func;

  void *bump_baton;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent_dir_baton;
  svn_stringbuf_t *path;
};


struct file_baton
{
  struct dir_baton *parent_dir_baton;
  svn_stringbuf_t *path;
};



/*** the anonymous editor functions ***/

static svn_error_t *
open_root (void *edit_baton,
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
  child_d->path = svn_stringbuf_dup (parent_d->path,
                                     child_d->edit_baton->pool);
  svn_path_add_component (child_d->path, name);

  /* If this was an add-with-history (copy), then indicate in the
     hash-value that this dir needs to be RECURSIVELY bumped after the
     commit completes. */
  if (copyfrom_path && SVN_IS_VALID_REVNUM(copyfrom_revision))
    apr_hash_set (parent_d->edit_baton->committed_targets,
                  child_d->path->data, APR_HASH_KEY_STRING,
                  (void *) svn_recursive);
  else
    apr_hash_set (parent_d->edit_baton->committed_targets,
                  child_d->path->data, APR_HASH_KEY_STRING,
                  (void *) svn_nonrecursive);


  *child_baton = child_d;

  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory (svn_stringbuf_t *name,
                void *parent_baton,
                svn_revnum_t base_revision,
                void **child_baton)
{
  struct dir_baton *parent_d = parent_baton;
  struct dir_baton *child_d
    = apr_pcalloc (parent_d->edit_baton->pool, sizeof (*child_d));

  child_d->edit_baton = parent_d->edit_baton;
  child_d->parent_dir_baton = parent_d;
  child_d->path = svn_stringbuf_dup (parent_d->path,
                                     child_d->edit_baton->pool);
  svn_path_add_component (child_d->path, name);

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
  child_fb->path = svn_stringbuf_dup (parent_d->path,
                                      parent_d->edit_baton->pool);
  svn_path_add_component (child_fb->path, name);

  apr_hash_set (parent_d->edit_baton->committed_targets,
                child_fb->path->data, APR_HASH_KEY_STRING,
                (void *) svn_nonrecursive);

  *file_baton = child_fb;

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
    = apr_pcalloc (parent_d->edit_baton->pool, sizeof (*child_fb));

  child_fb->parent_dir_baton = parent_d;
  child_fb->path = svn_stringbuf_dup (parent_d->path,
                                      parent_d->edit_baton->pool);
  svn_path_add_component (child_fb->path, name);

  *file_baton = child_fb;

  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry (svn_stringbuf_t *name,
              svn_revnum_t revision,
              void *parent_baton)
{
  struct dir_baton *parent_d = parent_baton;
  svn_stringbuf_t *path = svn_stringbuf_dup (parent_d->path,
                                       parent_d->edit_baton->pool);
  svn_path_add_component (path, name);
  
  apr_hash_set (parent_d->edit_baton->committed_targets,
                path->data, APR_HASH_KEY_STRING, (void *) svn_nonrecursive);

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_stringbuf_t *name,
                 svn_stringbuf_t *value)
{
  struct dir_baton *db = dir_baton;

  apr_hash_set (db->edit_baton->committed_targets,
                db->path->data, APR_HASH_KEY_STRING,
                (void *) svn_nonrecursive);

  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_stringbuf_t *name,
                  svn_stringbuf_t *value)
{
  struct file_baton *fb = file_baton;

  apr_hash_set (fb->parent_dir_baton->edit_baton->committed_targets,
                fb->path->data, APR_HASH_KEY_STRING,
                (void *) svn_nonrecursive);
  
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
  
  apr_hash_set (fb->parent_dir_baton->edit_baton->committed_targets,
                fb->path->data, APR_HASH_KEY_STRING,
                (void *) svn_nonrecursive);
  
  *handler = window_handler;
  *handler_baton = NULL;

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  apr_hash_index_t *hi;
  struct edit_baton *eb = edit_baton;
  apr_pool_t *subpool = svn_pool_create (eb->pool);

  /* Bump all targets if the caller wants us to. */
  if ((! SVN_IS_VALID_REVNUM(eb->new_rev)) || (! eb->bump_func))
    return SVN_NO_ERROR;

  for (hi = apr_hash_first (eb->pool, eb->committed_targets);
       hi;
       hi = apr_hash_next (hi))
    {
      char *path;
      void *val;
      svn_stringbuf_t path_str;
      enum svn_recurse_kind r;

      apr_hash_this (hi, (void *) &path, NULL, &val);

      /* Sigh. */
      path_str.data = path;
      path_str.len = strlen (path);
      r = (enum svn_recurse_kind) val;

      SVN_ERR (eb->bump_func (eb->bump_baton, &path_str,
                              (r == svn_recursive) ? TRUE : FALSE,
                              eb->new_rev, NULL, NULL,
                              subpool));

      svn_pool_clear (subpool);
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}



/*** exported routine ***/

svn_error_t *
svn_delta_get_commit_track_editor (svn_delta_edit_fns_t **editor,
                                   void **edit_baton,
                                   apr_pool_t *pool,
                                   apr_hash_t *committed_targets,
                                   svn_revnum_t new_rev,
                                   svn_delta_bump_func_t bump_func,
                                   void *bump_baton)
{
  struct edit_baton *eb = apr_pcalloc (pool, sizeof (*eb));
  svn_delta_edit_fns_t *track_editor = svn_delta_old_default_editor (pool);

  /* Set up the editor.  These functions are no-ops, so the default
     editor's implementations are used:

        set_target_revision
        close_directory
        window_handler
     
  */
  track_editor->open_root = open_root;
  track_editor->add_directory = add_directory;
  track_editor->open_directory = open_directory;
  track_editor->add_file = add_file;
  track_editor->open_file = open_file;
  track_editor->delete_entry = delete_entry;
  track_editor->change_dir_prop = change_dir_prop;
  track_editor->change_file_prop = change_file_prop;
  track_editor->apply_textdelta = apply_textdelta;
  track_editor->close_edit = close_edit;

  /* Set up the edit baton. */
  eb->pool = pool;
  eb->initial_path = svn_stringbuf_create ("", pool);
  eb->committed_targets = committed_targets;
  eb->new_rev = new_rev;
  eb->bump_func = bump_func;
  eb->bump_baton = bump_baton;

  *editor = track_editor;
  *edit_baton = eb;

  return SVN_NO_ERROR;
}



/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */





