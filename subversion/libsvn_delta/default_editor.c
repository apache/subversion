/* 
 * default_editor.c -- provide a basic svn_delta_edit_fns_t
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


#include <apr_pools.h>
#include <apr_strings.h>

#include "svn_types.h"
#include "svn_delta.h"
#include "svn_pools.h"
#include "svn_path.h"


static svn_error_t *
set_target_revision (void *edit_baton, svn_revnum_t target_revision)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
old_open_root (void *edit_baton, svn_revnum_t base_revision, void **root_baton)
{
  *root_baton = NULL;
  return SVN_NO_ERROR;
}


static svn_error_t *
old_delete_entry (svn_stringbuf_t *name, svn_revnum_t revision,
                  void *parent_baton)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
add_item (const char *path,
          void *parent_baton,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          apr_pool_t *pool,
          void **baton)
{
  *baton = NULL;
  return SVN_NO_ERROR;
}


static svn_error_t *
single_baton_func (void *baton)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *handler_pair)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
old_apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  *handler = window_handler;
  *handler_baton = NULL;
  return SVN_NO_ERROR;
}


static svn_error_t *
old_add_item (svn_stringbuf_t *name,
          void *parent_baton,
          svn_stringbuf_t *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          void **baton)
{
  *baton = NULL;
  return SVN_NO_ERROR;
}


static svn_error_t *
old_open_item (svn_stringbuf_t *name,
              void *parent_baton,
              svn_revnum_t base_revision,
              void **baton)
{
  *baton = NULL;
  return SVN_NO_ERROR;
}


static svn_error_t *
old_change_prop (void *baton,
                  svn_stringbuf_t *name,
                  svn_stringbuf_t *value)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
open_root (void *edit_baton,
           svn_revnum_t base_revision,
           apr_pool_t *dir_pool,
           void **root_baton)
{
  *root_baton = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry (const char *path,
              svn_revnum_t revision,
              void *parent_baton,
              apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
open_item (const char *path,
           void *parent_baton,
           svn_revnum_t base_revision,
           apr_pool_t *pool,
           void **baton)
{
  *baton = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
change_prop (void *file_baton,
             const char *name,
             const svn_string_t *value,
             apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  *handler = *handler_baton = NULL;
  return SVN_NO_ERROR;
}


/* As new editor functions are created, they should be given skeleton
   implementions above, and added here. */

/* NOTE:  also, if a new editor function is added, don't forget to
   write a 'composing' version of the function in compose_editors.c! */

static const svn_delta_edit_fns_t old_default_editor =
{
  set_target_revision,
  old_open_root,
  old_delete_entry,
  old_add_item,
  old_open_item,
  old_change_prop,
  single_baton_func,
  old_add_item,
  old_open_item,
  old_apply_textdelta,
  old_change_prop,
  single_baton_func,
  single_baton_func,
  single_baton_func
};


svn_delta_edit_fns_t *
svn_delta_old_default_editor (apr_pool_t *pool)
{
  return apr_pmemdup (pool, &old_default_editor, sizeof (old_default_editor));
}


static const svn_delta_editor_t default_editor =
{
  set_target_revision,
  open_root,
  delete_entry,
  add_item,
  open_item,
  change_prop,
  single_baton_func,
  add_item,
  open_item,
  apply_textdelta,
  change_prop,
  single_baton_func,
  single_baton_func,
  single_baton_func
};

svn_delta_editor_t *
svn_delta_default_editor (apr_pool_t *pool)
{
  return apr_pmemdup (pool, &default_editor, sizeof (default_editor));
}



/* ### temporary code to map the old editor interface onto a new-style
   ### editor. once all editors are converted, this will go away.  */

struct wrap_edit_baton
{
  const svn_delta_editor_t *real_editor;
  void *real_edit_baton;

  apr_pool_t *edit_pool;
};
struct wrap_dir_baton
{
  struct wrap_edit_baton *eb;

  void *real_dir_baton;

  apr_pool_t *dir_pool;
  const char *path;
};
struct wrap_file_baton
{
  struct wrap_edit_baton *eb;

  void *real_file_baton;

  apr_pool_t *file_pool;
};

static svn_error_t *
wrap_set_target_revision (void *edit_baton, svn_revnum_t target_revision)
{
  struct wrap_edit_baton *eb = edit_baton;

  return (*eb->real_editor->set_target_revision) (eb->real_edit_baton,
                                                  target_revision);
}

static svn_error_t *
wrap_open_root (void *edit_baton,
                svn_revnum_t base_revision,
                void **root_baton)
{
  struct wrap_edit_baton *eb = edit_baton;
  apr_pool_t *dir_pool = svn_pool_create (eb->edit_pool);
  struct wrap_dir_baton *db = apr_palloc (dir_pool, sizeof (*db));

  db->eb = eb;
  db->dir_pool = dir_pool;
  db->path = "";

  *root_baton = db;

  return (*eb->real_editor->open_root) (eb->real_edit_baton,
                                        base_revision,
                                        dir_pool,
                                        &db->real_dir_baton);
}

static svn_error_t *
wrap_delete_entry (svn_stringbuf_t *name,
                   svn_revnum_t revision,
                   void *parent_baton)
{
  struct wrap_dir_baton *db = parent_baton;
  apr_pool_t *subpool = svn_pool_create (db->dir_pool);
  svn_error_t *err;
  const char *path = svn_path_join (db->path, name->data, subpool);

  err = (*db->eb->real_editor->delete_entry) (path,
                                              revision,
                                              db->real_dir_baton,
                                              subpool);
  svn_pool_destroy (subpool);
  return err;
}

static struct wrap_dir_baton *
make_dir_baton (struct wrap_dir_baton *db_parent, const char *name)
{
  apr_pool_t *dir_pool = svn_pool_create (db_parent->dir_pool);
  struct wrap_dir_baton *db = apr_palloc (dir_pool, sizeof (*db));

  db->eb = db_parent->eb;
  db->dir_pool = dir_pool;
  db->path = svn_path_join (db_parent->path, name, dir_pool);

  return db;
}

static svn_error_t *
wrap_add_directory (svn_stringbuf_t *name,
                    void *parent_baton,
                    svn_stringbuf_t *copyfrom_path,
                    svn_revnum_t copyfrom_revision,
                    void **dir_baton)
{
  struct wrap_dir_baton *db_parent = parent_baton;
  struct wrap_dir_baton *db = make_dir_baton (db_parent, name->data);

  *dir_baton = db;

  return (*db->eb->real_editor->add_directory) (db->path,
                                                db_parent->real_dir_baton,
                                                copyfrom_path ? 
                                                copyfrom_path->data : NULL,
                                                copyfrom_revision,
                                                db->dir_pool,
                                                &db->real_dir_baton);
}

static svn_error_t *
wrap_open_directory (svn_stringbuf_t *name,
                     void *parent_baton,
                     svn_revnum_t base_revision,
                     void **dir_baton)
{
  struct wrap_dir_baton *db_parent = parent_baton;
  struct wrap_dir_baton *db = make_dir_baton (db_parent, name->data);

  *dir_baton = db;

  return (*db->eb->real_editor->open_directory) (db->path,
                                                 db_parent->real_dir_baton,
                                                 base_revision,
                                                 db->dir_pool,
                                                 &db->real_dir_baton);
}

static svn_error_t *
wrap_change_dir_prop (void *dir_baton,
                      svn_stringbuf_t *name,
                      svn_stringbuf_t *value)
{
  struct wrap_dir_baton *db = dir_baton;
  apr_pool_t *subpool = svn_pool_create (db->dir_pool);
  svn_string_t new_value;
  svn_error_t *err;

  if (value)
    {
      new_value.data = value->data;
      new_value.len = value->len;
    }

  err = (*db->eb->real_editor->change_dir_prop) (db->real_dir_baton,
                                                 name->data,
                                                 value ? &new_value : NULL,
                                                 subpool);
  svn_pool_destroy (subpool);
  return err;
}

static svn_error_t *
wrap_close_directory (void *dir_baton)
{
  struct wrap_dir_baton *db = dir_baton;
  svn_error_t *err;

  err = (*db->eb->real_editor->close_directory) (db->real_dir_baton);

  svn_pool_destroy (db->dir_pool);
  return err;
}

static struct wrap_file_baton *
make_file_baton (struct wrap_dir_baton *db)
{
  apr_pool_t *file_pool = svn_pool_create (db->dir_pool);
  struct wrap_file_baton *fb = apr_palloc (file_pool, sizeof (*fb));

  fb->eb = db->eb;
  fb->file_pool = file_pool;

  return fb;
}

static svn_error_t *
wrap_add_file (svn_stringbuf_t *name,
               void *parent_baton,
               svn_stringbuf_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **file_baton)
{
  struct wrap_dir_baton *db = parent_baton;
  struct wrap_file_baton *fb = make_file_baton (db);
  const char *path = svn_path_join (db->path, name->data, fb->file_pool);

  *file_baton = fb;

  return (*fb->eb->real_editor->add_file) (path,
                                           db->real_dir_baton,
                                           copyfrom_path ? 
                                           copyfrom_path->data : NULL,
                                           copyfrom_revision,
                                           fb->file_pool,
                                           &fb->real_file_baton);
}

static svn_error_t *
wrap_open_file (svn_stringbuf_t *name,
                void *parent_baton,
                svn_revnum_t base_revision,
                void **file_baton)
{
  struct wrap_dir_baton *db = parent_baton;
  struct wrap_file_baton *fb = make_file_baton (db);
  const char *path = svn_path_join (db->path, name->data, fb->file_pool);

  *file_baton = fb;

  return (*fb->eb->real_editor->open_file) (path,
                                            db->real_dir_baton,
                                            base_revision,
                                            fb->file_pool,
                                            &fb->real_file_baton);
}

static svn_error_t *
wrap_apply_textdelta (void *file_baton,
                      svn_txdelta_window_handler_t *handler,
                      void **handler_baton)
{
  struct wrap_file_baton *fb = file_baton;
  svn_txdelta_window_handler_t real_handler;

  SVN_ERR ((*fb->eb->real_editor->apply_textdelta) (fb->real_file_baton,
                                                    &real_handler,
                                                    handler_baton));
  if (real_handler == NULL)
    *handler = window_handler;
  else
    *handler = real_handler;

  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_change_file_prop (void *file_baton,
                       svn_stringbuf_t *name,
                       svn_stringbuf_t *value)
{
  struct wrap_file_baton *fb = file_baton;
  apr_pool_t *subpool = svn_pool_create (fb->file_pool);
  svn_string_t new_value;
  svn_error_t *err;

  if (value)
    {
      new_value.data = value->data;
      new_value.len = value->len;
    }

  err = (*fb->eb->real_editor->change_file_prop) (fb->real_file_baton,
                                                  name->data,
                                                  value ? &new_value : NULL,
                                                  subpool);
  svn_pool_destroy (subpool);
  return err;
}

static svn_error_t *
wrap_close_file (void *file_baton)
{
  struct wrap_file_baton *fb = file_baton;
  svn_error_t *err;

  err = (*fb->eb->real_editor->close_file) (fb->real_file_baton);

  svn_pool_destroy (fb->file_pool);
  return err;
}

static svn_error_t *
wrap_close_edit (void *edit_baton)
{
  struct wrap_edit_baton *eb = edit_baton;

  return (*eb->real_editor->close_edit) (eb->real_edit_baton);
}

static svn_error_t *
wrap_abort_edit (void *edit_baton)
{
  struct wrap_edit_baton *eb = edit_baton;

  return (*eb->real_editor->abort_edit) (eb->real_edit_baton);
}

static const svn_delta_edit_fns_t wrapper_editor =
{
  wrap_set_target_revision,
  wrap_open_root,
  wrap_delete_entry,
  wrap_add_directory,
  wrap_open_directory,
  wrap_change_dir_prop,
  wrap_close_directory,
  wrap_add_file,
  wrap_open_file,
  wrap_apply_textdelta,
  wrap_change_file_prop,
  wrap_close_file,
  wrap_close_edit,
  wrap_abort_edit
};

void svn_delta_compat_wrap (const svn_delta_edit_fns_t **wrap_editor,
                            void **wrap_baton,
                            const svn_delta_editor_t *editor,
                            void *edit_baton,
                            apr_pool_t *pool)
{
  struct wrap_edit_baton *eb = apr_palloc (pool, sizeof (*eb));

  eb->real_editor = editor;
  eb->real_edit_baton = edit_baton;
  eb->edit_pool = pool;

  *wrap_editor = apr_pmemdup (pool, &wrapper_editor, sizeof (wrapper_editor));
  *wrap_baton = eb;
}


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
