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
  svn_stringbuf_t *path;
  apr_hash_t *committed_targets; /* Paths we declare as committed. */
  svn_delta_bump_func_t bump_func; /* May be NULL */
  svn_revnum_t new_rev;
  void *bump_baton;
};


struct item_baton
{
  struct edit_baton *edit_baton;
  struct item_baton *parent_dir_baton;
  svn_stringbuf_t *path;
};



/*** the anonymous editor functions ***/

static struct item_baton *
make_item_baton (const char *path,
                 void *edit_baton,
                 void *parent_dir_baton,
                 apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  struct item_baton *pd = parent_dir_baton;
  struct item_baton *new_ib = apr_pcalloc (pool, sizeof (*new_ib));
  svn_stringbuf_t *full_path = svn_stringbuf_dup (eb->path, pool);

  if (path)
    svn_path_add_component_nts (full_path, path);

  new_ib->edit_baton = eb;
  new_ib->parent_dir_baton = pd;
  new_ib->path = full_path;

  return new_ib;
}

static svn_error_t *
open_root (void *edit_baton,
           svn_revnum_t base_revision,
           apr_pool_t *pool,
           void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  *root_baton = make_item_baton (NULL, eb, NULL, pool);
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
  struct item_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct item_baton *new_ib = make_item_baton (path, eb, pb, pool);

  /* Copy the full path into the edit baton's pool, then: if this was
     an add-with-history (copy), indicate in the hash-value that
     this dir needs to be RECURSIVELY bumped after the commit
     completes. */
  svn_stringbuf_t *full_path = svn_stringbuf_dup (new_ib->path, eb->pool);
  if (copyfrom_path && SVN_IS_VALID_REVNUM (copyfrom_revision))
    apr_hash_set (eb->committed_targets,
                  full_path->data, full_path->len,
                  (void *) svn_recursive);
  else
    apr_hash_set (eb->committed_targets,
                  full_path->data, full_path->len,
                  (void *) svn_nonrecursive);

  *child_baton = new_ib;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_item (const char *path,
           void *parent_baton,
           svn_revnum_t base_revision,
           apr_pool_t *pool,
           void **child_baton)
{
  struct item_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  *child_baton = make_item_baton (path, eb, pb, pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (const char *path,
          void *parent_baton,
          const char *copy_path,
          svn_revnum_t copy_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct item_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct item_baton *new_ib = make_item_baton (path, eb, pb, pool);

  /* Copy the full path into the edit baton's pool, and stuff a
     reference to it in the edit baton's hash. */
  svn_stringbuf_t *full_path = svn_stringbuf_dup (new_ib->path, eb->pool);
  apr_hash_set (eb->committed_targets,
                full_path->data, full_path->len,
                (void *) svn_nonrecursive);

  *file_baton = new_ib;
  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry (const char *path,
              svn_revnum_t revision,
              void *parent_baton,
              apr_pool_t *pool)
{
  struct item_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  /* Construct the full path in the edit baton's pool, and stuff a
     reference to it in the edit baton's hash. */
  svn_stringbuf_t *full_path = svn_stringbuf_dup (eb->path, eb->pool);
  svn_path_add_component_nts (full_path, path);
  apr_hash_set (eb->committed_targets,
                full_path->data, full_path->len, 
                (void *) svn_nonrecursive);

  return SVN_NO_ERROR;
}


static svn_error_t *
change_item_prop (void *item_baton,
                  const char *name,
                  const svn_string_t *value,
                  apr_pool_t *pool)
{
  struct item_baton *ib = item_baton;
  struct edit_baton *eb = ib->edit_baton;

  /* Copy the full path into the edit baton's pool, and stuff a
     reference to it in the edit baton's hash. */
  svn_stringbuf_t *full_path = svn_stringbuf_dup (ib->path, eb->pool);
  apr_hash_set (eb->committed_targets,
                full_path->data, full_path->len,
                (void *) svn_nonrecursive);

  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta (void *file_baton, 
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct item_baton *ib = file_baton;
  struct edit_baton *eb = ib->edit_baton;

  /* Copy the full path into the edit baton's pool, and stuff a
     reference to it in the edit baton's hash. */
  svn_stringbuf_t *full_path = svn_stringbuf_dup (ib->path, eb->pool);
  apr_hash_set (eb->committed_targets,
                full_path->data, full_path->len,
                (void *) svn_nonrecursive);
  
  *handler = NULL;
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
  if ((! SVN_IS_VALID_REVNUM (eb->new_rev)) || (! eb->bump_func))
    return SVN_NO_ERROR;

  for (hi = apr_hash_first (eb->pool, eb->committed_targets);
       hi;
       hi = apr_hash_next (hi))
    {
      void *val;
      svn_stringbuf_t path_str;
      enum svn_recurse_kind r;

      apr_hash_this (hi, (void *) (&(path_str.data)), &(path_str.len), &val);
      r = (enum svn_recurse_kind) val;
      SVN_ERR (eb->bump_func (eb->bump_baton, &path_str,
                              (r == svn_recursive) ? TRUE : FALSE,
                              eb->new_rev, NULL, NULL,
                              subpool));

      svn_pool_clear (subpool);
    }

  svn_pool_destroy (eb->pool); /* this also takes care of `subpool' */
  return SVN_NO_ERROR;
}



/*** exported routine ***/

svn_error_t *
svn_delta_get_commit_track_editor (const svn_delta_editor_t **editor,
                                   void **edit_baton,
                                   apr_pool_t *pool,
                                   apr_hash_t *committed_targets,
                                   svn_revnum_t new_rev,
                                   svn_delta_bump_func_t bump_func,
                                   void *bump_baton)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  struct edit_baton *eb = apr_pcalloc (subpool, sizeof (*eb));
  svn_delta_editor_t *track_editor = svn_delta_default_editor (pool);

  /* Set up the editor.  These functions are no-ops, so the default
     editor's implementations are used:

        set_target_revision
        close_directory
        window_handler
     
  */
  track_editor->open_root = open_root;
  track_editor->add_directory = add_directory;
  track_editor->open_directory = open_item;
  track_editor->add_file = add_file;
  track_editor->open_file = open_item;
  track_editor->delete_entry = delete_entry;
  track_editor->change_dir_prop = change_item_prop;
  track_editor->change_file_prop = change_item_prop;
  track_editor->apply_textdelta = apply_textdelta;
  track_editor->close_edit = close_edit;

  /* Set up the edit baton. */
  eb->pool = subpool;
  eb->path = svn_stringbuf_create ("", eb->pool);
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
