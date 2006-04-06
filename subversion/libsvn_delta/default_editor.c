/* 
 * default_editor.c -- provide a basic svn_delta_editor_t
 * 
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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


static svn_error_t *
set_target_revision(void *edit_baton, 
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}
static svn_error_t *
add_item(const char *path,
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
single_baton_func(void *baton,
                  apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
absent_xxx_func(const char *path,
                void *baton,
                apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *dir_pool,
          void **root_baton)
{
  *root_baton = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
open_item(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **baton)
{
  *baton = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
change_prop(void *file_baton,
            const char *name,
            const svn_string_t *value,
            apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

svn_error_t *svn_delta_noop_window_handler(svn_txdelta_window_t *window,
                                           void *baton)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  *handler = svn_delta_noop_window_handler;
  *handler_baton = NULL;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}


static svn_error_t *
rename_from(const char *path,
            void *parent_baton,
            const char *src_path,
            svn_revnum_t src_rev,
            apr_pool_t *pool)
{
  return SVN_NO_ERROR;
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
  absent_xxx_func,
  add_item,
  open_item,
  apply_textdelta,
  change_prop,
  close_file,
  absent_xxx_func,
  single_baton_func,
  single_baton_func
};

svn_delta_editor_t *
svn_delta_default_editor(apr_pool_t *pool)
{
  return apr_pmemdup(pool, &default_editor, sizeof(default_editor));
}

static const svn_delta_editor2_t default_editor2 =
{
  set_target_revision,  
  open_root,
  delete_entry,
  add_item,
  open_item,
  change_prop,
  single_baton_func,
  absent_xxx_func,
  add_item,
  open_item,
  apply_textdelta,
  change_prop,
  close_file,
  absent_xxx_func,
  single_baton_func,
  single_baton_func,
  add_item,
  add_item,
  rename_from,
  rename_from
};

svn_delta_editor2_t *
svn_delta_default_editor2(apr_pool_t *pool)
{
  return apr_pmemdup(pool, &default_editor2, sizeof(default_editor2));
}

svn_delta_editor2_t *
svn_delta_editor_to_editor2(const svn_delta_editor_t *editor,
                            apr_pool_t *pool)
{
  svn_delta_editor2_t *editor2 = svn_delta_default_editor2(pool);

  editor2->set_target_revision = editor->set_target_revision;
  editor2->open_root = editor->open_root;
  editor2->delete_entry = editor->delete_entry;
  editor2->add_directory = editor->add_directory;
  editor2->open_directory = editor->open_directory;
  editor2->change_dir_prop = editor->change_dir_prop;
  editor2->close_directory = editor->close_directory;
  editor2->absent_directory = editor->absent_directory;
  editor2->add_file = editor->add_file;
  editor2->open_file = editor->open_file;
  editor2->apply_textdelta = editor->apply_textdelta;
  editor2->change_file_prop = editor->change_file_prop;
  editor2->close_file = editor->close_file;
  editor2->absent_file = editor->absent_file;
  editor2->close_edit = editor->close_edit;
  editor2->abort_edit = editor->abort_edit;

  return editor2;
}
