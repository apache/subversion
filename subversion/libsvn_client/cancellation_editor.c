/*
 * cancellation_editor.c: an editor implementation that calls a
 *    user-supplied callback to determine if the user decided to
 *    cancel the pending request.  Compose this editor before a
 *    commit/update-editor, for example.
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
#include "svn_client.h"


/* NOTE: There are no separate dir_baton and file_baton structs;
   everyone uses the edit_baton. */

struct edit_baton
{
  apr_pool_t *pool;
  svn_client_cancellation_func_t should_i_cancel;
  void *cancel_baton;
};

static svn_error_t *
check_cancel (struct edit_baton *eb)
{
  if (eb->should_i_cancel (eb->cancel_baton))
    {
      return svn_error_create (SVN_ERR_CANCELED,
                               0, NULL, eb->pool,
                               "Operation canceled, presumably by user.");
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
open_root (void *edit_baton, svn_revnum_t base_revision, void **root_baton)
{
  *root_baton = edit_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry (svn_stringbuf_t *name, svn_revnum_t revision, void *parent_baton)
{
  return check_cancel (parent_baton);
}


static svn_error_t *
add_directory (svn_stringbuf_t *name,
               void *parent_baton,
               svn_stringbuf_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **child_baton)
{
  SVN_ERR (check_cancel (parent_baton));

  *child_baton = parent_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory (svn_stringbuf_t *name,
                void *parent_baton,
                svn_revnum_t base_revision,
                void **child_baton)
{
  SVN_ERR (check_cancel (parent_baton));

  *child_baton = parent_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_something (void *baton)
{
  return check_cancel (baton);
}


static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *handler_baton)
{
  return check_cancel (handler_baton);
}


static svn_error_t *
apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  SVN_ERR (check_cancel (file_baton));

  *handler = window_handler;
  *handler_baton = file_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (svn_stringbuf_t *name,
          void *parent_baton,
          svn_stringbuf_t *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          void **file_baton)
{
  SVN_ERR (check_cancel (parent_baton));

  *file_baton = parent_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_file (svn_stringbuf_t *name,
              void *parent_baton,
              svn_revnum_t ancestor_revision,
              void **file_baton)
{
  SVN_ERR (check_cancel (parent_baton));
  
  *file_baton = parent_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
change_something_prop (void *baton,
                       svn_stringbuf_t *name,
                       svn_stringbuf_t *value)
{
  return check_cancel (baton);
}



svn_error_t *
svn_client_get_cancellation_editor
   (const svn_delta_edit_fns_t **editor,
    void **edit_baton,
    svn_client_cancellation_func_t should_i_cancel,
    void *cancel_baton,
    apr_pool_t *pool)
{
  struct edit_baton *eb = apr_pcalloc (pool, sizeof (*eb));
  svn_delta_edit_fns_t *cancel_editor = svn_delta_old_default_editor (pool);

  /* Set up the edit context. */
  eb->pool = svn_pool_create (pool);
  eb->cancel_baton = cancel_baton;
  eb->should_i_cancel = should_i_cancel;

  /* Set up the editor. */
  cancel_editor->open_root = open_root;
  cancel_editor->delete_entry = delete_entry;
  cancel_editor->add_directory = add_directory;
  cancel_editor->open_directory = open_directory;
  cancel_editor->add_file = add_file;
  cancel_editor->open_file = open_file;
  cancel_editor->apply_textdelta = apply_textdelta;
  cancel_editor->change_dir_prop = change_something_prop;
  cancel_editor->change_file_prop = change_something_prop;
  cancel_editor->close_file = close_something;
  cancel_editor->close_edit = close_something;
  cancel_editor->close_directory = close_something;

  *edit_baton = eb;
  *editor = cancel_editor;
  
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: 
 */
