/* 
 * default_editor.c -- provide a basic svn_delta_edit_fns_t
 * 
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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


#include <assert.h>
#include <apr_pools.h>
#include "svn_delta.h"
#include <string.h>


static svn_error_t *
set_target_revision (void *edit_baton, svn_revnum_t target_revision)
{
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
  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (svn_stringbuf_t *name,
               void *parent_baton,
               svn_stringbuf_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **child_baton)
{
  *child_baton = parent_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory (svn_stringbuf_t *name,
                   void *parent_baton,
                   svn_revnum_t base_revision,
                   void **child_baton)
{
  *child_baton = parent_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
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
  *file_baton = parent_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_file (svn_stringbuf_t *name,
              void *parent_baton,
              svn_revnum_t base_revision,
              void **file_baton)
{
  *file_baton = parent_baton;
  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_stringbuf_t *name,
                  svn_stringbuf_t *value)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_stringbuf_t *name,
                 svn_stringbuf_t *value)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
abort_edit (void *edit_baton)
{
  return SVN_NO_ERROR;
}



/* As new editor functions are created, they should be given skeleton
   implementions above, and added here. */

/* NOTE:  also, if a new editor function is added, don't forget to
   write a 'composing' version of the function in compose_editors.c! */

static const svn_delta_edit_fns_t default_editor =
{
  set_target_revision,
  open_root,
  delete_entry,
  add_directory,
  open_directory,
  change_dir_prop,
  close_directory,
  add_file,
  open_file,
  apply_textdelta,
  change_file_prop,
  close_file,
  close_edit,
  abort_edit
};


svn_delta_edit_fns_t *
svn_delta_default_editor (apr_pool_t *pool)
{
  svn_delta_edit_fns_t *e = apr_pcalloc (pool, sizeof (*e));
  memcpy (e, &default_editor, sizeof (default_editor));
  return e;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
